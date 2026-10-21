// The text draws (task P2.6.7e): Draw(GEText *, GESurface *) (slot 16) and Draw(GEText *, GERectangle const &, GESurface *) (15).
// Both draw with the GDI on the text device context of the surface (the surface's TextLock hands it out, and is called here
// when the surface is not text locked yet; the draw never unlocks it): the background mode follows the text's transparency flag,
// the text colour and background colour are the text's colours (each channel truncated after the product with 255, the low byte
// kept, COLORREF 0x00BBGGRR), the font is the GEFont's HFONT (the font's count is raised for the draw and dropped again by the
// engine's Dereference).  The text is measured with GetTextExtentPoint32A over the string's CAPACITY (an original quirk; the draw
// itself uses the length): alignment 1 moves the start left by half the width (the width halved towards zero), alignment 2 by the
// whole width, anything else leaves it.  A font whose style has bit 0x10 first draws the text shifted one pixel right and down in
// black, or in white when the text's colour channels add up to 0.1 or less.  The position is truncated to integers; the rectangle
// variant clips (ETO_CLIPPED) to the rectangle with each side rounded down.  A failed ExtTextOut records the error and the draw
// answers 1; the DC's state (mode, colours, font) is put back before the engine drops the font.  If the text lock fails the draw
// answers 0 and does nothing.
//
// tests/dx7_rasterizer/text_checks.h compares both builds on 25 recorded steps on a real memory device context (the return
// value, the GDI state afterwards, the font's count and a hash of the pixels).  Not verified: a font with a bitmap font (GEFont
// 0x24 set), which the rectangle variant of the original hands to the engine's GEFontBitmap::Draw; this build refuses it
// (EEWEB_NOT_SUPPORTED).  Differs from the original, deliberately: a null surface or text and a text that fails its layout canary are refused.
#include "null_rasterizer.h"

#include "dx7_math.h"
#include "engine_imports.h"
#include "rasterizer_abi.h"

extern "C" {
__declspec(dllimport) void* __stdcall SelectObject(HDC__* dc, void* object);
__declspec(dllimport) int __stdcall GetTextExtentPoint32A(HDC__* dc, const char* text, int count, long* size);
__declspec(dllimport) unsigned long __stdcall SetTextColor(HDC__* dc, unsigned long color);
__declspec(dllimport) unsigned long __stdcall SetBkColor(HDC__* dc, unsigned long color);
__declspec(dllimport) int __stdcall SetBkMode(HDC__* dc, int mode);
__declspec(dllimport) int __stdcall ExtTextOutA(HDC__* dc, int x, int y, unsigned options, const long* rect, const char* text, unsigned count, const int* spacing);
__declspec(dllimport) unsigned long __stdcall GetLastError();
}

typedef unsigned long ulong;

namespace {

const unsigned kClipped = 4; // ETO_CLIPPED
const ulong kShadowStyle = 0x10;

ulong ColorRef(const GEColorLayout& color)
{
    return (dx7math::TruncatedChannel(color.b) & 0xff) << 16 | (dx7math::TruncatedChannel(color.g) & 0xff) << 8 | (dx7math::TruncatedChannel(color.r) & 0xff);
}

// green + blue + red <= 0.1, summed in extended precision as the original does (an unordered sum counts as a yes).
bool IsDark(const GEColorLayout& color)
{
    const float limit = 0.1f;
    unsigned short status;
    __asm__ volatile("flds %1\n\tfadds %2\n\tfadds %3\n\tfcomps %4\n\tfnstsw %0" : "=a"(status) : "m"(color.g), "m"(color.b), "m"(color.r), "m"(limit) : "st", "cc");
    return (status & 0x4100) != 0;
}

// One ExtTextOut; a failure is recorded with the engine.  Answers 1 on failure.
long Out(HDC__* dc, int x, int y, const GETextLayout* text, const long* clip)
{
    if (ExtTextOutA(dc, x, y, clip ? kClipped : 0, clip, reinterpret_cast<const char*>(text->text.chars), text->text.length, 0)) {
        return 0;
    }
    ESRecordNativeError(GetLastError());
    return 1;
}

} // namespace

long EEWebNullRasterizer::DrawTextOn(GEText* object, const GERectangleLayout* clipRectangle, GESurface* surface)
{
    if (Note(EEWebCheckText(object)) != EEWEB_LAYOUT_OK) {
        return EEWEB_LAYOUT_VIOLATION;
    }
    GETextLayout* text = reinterpret_cast<GETextLayout*>(object);
    GEFontLayout* font = text->font;
    if (!surface || !text->text.chars || (clipRectangle && font->fontBitmap)) {
        return EEWEB_NOT_SUPPORTED;
    }
    long clip[4];
    if (clipRectangle) {
        clip[0] = dx7math::FloorOf(clipRectangle->left);
        clip[1] = dx7math::FloorOf(clipRectangle->top);
        clip[2] = dx7math::FloorOf(clipRectangle->right);
        clip[3] = dx7math::FloorOf(clipRectangle->bottom);
    }
    if (!surface->GetIsTextLocked() && surface->TextLock() != 0) {
        return 0;
    }
    HDC__* dc = surface->GetTextDC();
    long result = 0;
    int oldMode = SetBkMode(dc, text->isTransparent ? 1 : 2);
    ulong oldText = SetTextColor(dc, ColorRef(text->fgColor));
    ulong oldBackground = SetBkColor(dc, ColorRef(text->bgColor));
    font->refCount++;
    void* oldFont = SelectObject(dc, font->osFont);
    float x = text->location.x;
    float y = text->location.y;
    long size[2];
    if (GetTextExtentPoint32A(dc, reinterpret_cast<const char*>(text->text.chars), static_cast<int>(text->text.capacity), size)) {
        if (text->alignment == 1) {
            x = dx7math::FloatMinusInt(x, size[0] / 2);
        } else if (text->alignment == 2) {
            x = dx7math::FloatMinusInt(x, size[0]);
        }
    }
    const long* clipPointer = clipRectangle ? clip : 0;
    if (font->style & kShadowStyle) {
        ulong shade = IsDark(text->fgColor) ? 0xff : 0;
        ulong previous = SetTextColor(dc, shade << 16 | shade << 8 | shade);
        result = Out(dc, static_cast<int>(x) + 1, static_cast<int>(y) + 1, text, clipPointer);
        SetTextColor(dc, previous);
    }
    if (Out(dc, static_cast<int>(x), static_cast<int>(y), text, clipPointer)) {
        result = 1;
    }
    SelectObject(dc, oldFont);
    SetTextColor(dc, oldText);
    SetBkColor(dc, oldBackground);
    SetBkMode(dc, oldMode);
    reinterpret_cast<UReferenceCounter*>(font)->Dereference();
    return result;
}

long EEWebNullRasterizer::Draw(GEText* text, GESurface* surface)
{
    return DrawTextOn(text, 0, surface);
}

long EEWebNullRasterizer::Draw(GEText* text, GERectangle const& clip, GESurface* surface)
{
    return DrawTextOn(text, reinterpret_cast<const GERectangleLayout*>(&clip), surface);
}
