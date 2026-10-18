#include "dx7_surface.h"

#include "engine_imports.h"

extern "C" {
__declspec(dllimport) void __stdcall Sleep(unsigned long milliseconds);
}

namespace {

typedef long double Register; // a value the original keeps in an x87 register (the compiler does the same)

// Calls `expression` again while DirectDraw answers that it is still drawing (the original sleeps for a time slice
// between the calls).
#define EEWEB_UNTIL_DONE_DRAWING(result, expression)                       \
    do {                                                                    \
        (result) = (expression);                                            \
        if (static_cast<unsigned long>(result) != DDERR_WASSTILLDRAWING_) { \
            break;                                                          \
        }                                                                   \
        Sleep(0);                                                           \
    } while (true)

// The status the engine gets for a DirectDraw result: 0 success, 0xd lost (or busy, where `busyIsLost`), else 1
// with the DirectDraw error recorded.
long Status(long result, bool busyIsLost = false)
{
    if (result == 0) {
        return 0;
    }
    const unsigned long code = static_cast<unsigned long>(result);
    if (code == DDERR_SURFACELOST_ || (busyIsLost && code == DDERR_SURFACEBUSY_)) {
        return 0xd;
    }
    ESRecordNativeError(code);
    return 1;
}

// A GERectangle (bottom, left, right, top floats) as the RECT DirectDraw wants: the engine truncates.
RECT_ ToRect(const GERectangleLayout& rectangle)
{
    RECT_ rect;
    rect.left = static_cast<long>(rectangle.left);
    rect.top = static_cast<long>(rectangle.top);
    rect.right = static_cast<long>(rectangle.right);
    rect.bottom = static_cast<long>(rectangle.bottom);
    return rect;
}

// The x87 `fistp`: round to nearest (even), as the original's filter does.
long RoundToLong(Register value)
{
    long result;
    __asm__("fistpl %0" : "=m"(result) : "t"(value) : "st");
    return result;
}

const GERectangleLayout& Layout(GERectangle& rectangle)
{
    return reinterpret_cast<const GERectangleLayout&>(rectangle);
}

} // namespace

// ---- life cycle --------------------------------------------------------------------------------------------

DX7Surface::DX7Surface(IDirectDrawSurface7* surface, const GESurfaceDescriptionLayout& description, GEScreen* screen)
    : m_refCount(1),
      m_description(description),
      m_attached(0),
      m_attachedCount(0),
      m_attachedCapacity(0),
      m_surface(surface),
      m_screen(screen),
      m_memory(0),
      m_textDC(0),
      m_pitch(0),
      m_locked(false),
      m_textLocked(false)
{
    // What the engine reaches: the reference count sits where the engine's UReferenceCounter keeps it.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
    static_assert(__builtin_offsetof(DX7Surface, m_refCount) == __builtin_offsetof(GESurfaceLayout, refCount),
                  "the engine counts references at GESurfaceLayout.refCount");
    static_assert(__builtin_offsetof(DX7Surface, m_description) == sizeof(GESurfaceLayout),
                  "the description follows the engine's part of the object");
#pragma clang diagnostic pop
    m_transparencyColor[0] = 1.0f;
    m_transparencyColor[1] = 1.0f;
    m_transparencyColor[2] = 1.0f;
}

DX7Surface::~DX7Surface()
{
    TextUnlock();
    for (unsigned long i = 0; i < m_attachedCount; ++i) {
        reinterpret_cast<UReferenceCounter*>(m_attached[i])->Dereference();
    }
    // A back buffer of a flip chain (type 7) belongs to its primary surface: only the others are released.
    if (m_description.type != 7) {
        m_surface->vtbl->Release(m_surface);
    }
    ::operator delete(m_attached);
}

void DX7Surface::AddAttached(DX7Surface* surface)
{
    if (m_attachedCount == m_attachedCapacity) {
        const unsigned long capacity = m_attachedCapacity ? m_attachedCapacity * 2 : 4;
        DX7Surface** grown = static_cast<DX7Surface**>(::operator new(capacity * sizeof(DX7Surface*)));
        for (unsigned long i = 0; i < m_attachedCount; ++i) {
            grown[i] = m_attached[i];
        }
        ::operator delete(m_attached);
        m_attached = grown;
        m_attachedCapacity = capacity;
    }
    m_attached[m_attachedCount++] = surface;
}

// ---- attached surfaces ------------------------------------------------------------------------------------

long DX7Surface::AttachSurface(GESurface* surface)
{
    DX7Surface* other = static_cast<DX7Surface*>(surface);
    const long result = m_surface->vtbl->AddAttachedSurface(m_surface, other->m_surface);
    if (result != 0) {
        ESRecordNativeError(static_cast<unsigned long>(result));
        return 1;
    }
    ++other->m_refCount;
    AddAttached(other);
    return 0;
}

long DX7Surface::DetachSurface(unsigned long index)
{
    DX7Surface* other = m_attached[index];
    const long result = m_surface->vtbl->DeleteAttachedSurface(m_surface, 0, other->m_surface);
    if (result != 0) {
        ESRecordNativeError(static_cast<unsigned long>(result));
        return 1;
    }
    for (unsigned long i = index + 1; i < m_attachedCount; ++i) {
        m_attached[i - 1] = m_attached[i];
    }
    --m_attachedCount;
    reinterpret_cast<UReferenceCounter*>(other)->Dereference();
    return 0;
}

GESurface* DX7Surface::GetAttachedSurface(unsigned long index)
{
    return m_attached[index];
}

unsigned long DX7Surface::GetAttachedSurfaceCount()
{
    return m_attachedCount;
}

unsigned long DX7Surface::GetAllocatedSize()
{
    unsigned long size = (m_description.pixelDepth >> 3) * m_description.height * m_description.width;
    for (unsigned long i = 0; i < m_attachedCount; ++i) {
        size += m_attached[i]->GetAllocatedSize();
    }
    return size;
}

GESurfaceDescription const& DX7Surface::GetSurfaceDescription() const
{
    return reinterpret_cast<GESurfaceDescription const&>(m_description);
}

GEColor const& DX7Surface::GetTransparencyColor() const
{
    return reinterpret_cast<GEColor const&>(m_transparencyColor);
}

// ---- drawing ------------------------------------------------------------------------------------------------

long DX7Surface::Clear(GEColor& color, float alpha)
{
    DDBLTFX_ effects;
    __builtin_memset(&effects, 0, sizeof effects);
    effects.words[0] = sizeof effects;
    effects.words[20] = GEPackColor(m_description.pixelFormat, color, alpha); // dwFillColor
    long result;
    EEWEB_UNTIL_DONE_DRAWING(result, m_surface->vtbl->Blt(m_surface, 0, 0, 0, 0x400 /* DDBLT_COLORFILL */, &effects));
    return Status(result);
}

long DX7Surface::Clear(GERectangle& rectangle, GEColor& color, float alpha)
{
    RECT_ rect = ToRect(Layout(rectangle));
    DDBLTFX_ effects;
    __builtin_memset(&effects, 0, sizeof effects);
    effects.words[0] = sizeof effects;
    effects.words[20] = GEPackColor(m_description.pixelFormat, color, alpha);
    long result;
    EEWEB_UNTIL_DONE_DRAWING(result, m_surface->vtbl->Blt(m_surface, &rect, 0, 0, 0x400, &effects));
    return Status(result);
}

// This surface is the source, `destination` receives the pixels.
long DX7Surface::CopyTo(GESurface* destination, GERectangle& sourceRectangle, GERectangle& destinationRectangle, bool useColorKey)
{
    DX7Surface* target = static_cast<DX7Surface*>(destination);
    RECT_ source = ToRect(Layout(sourceRectangle));
    RECT_ rect = ToRect(Layout(destinationRectangle));
    const unsigned long flags = 0x8000000 /* DDBLT_WAIT */ + (useColorKey ? 0x8000 /* DDBLT_KEYSRC */ : 0);
    long result;
    EEWEB_UNTIL_DONE_DRAWING(result, target->m_surface->vtbl->Blt(target->m_surface, &rect, m_surface, &source, flags, 0));
    return Status(result, true);
}

long DX7Surface::Flip(bool wait)
{
    const unsigned long flags = wait ? 0x20 /* DDFLIP_DONOTWAIT */ : 0x28 /* ... | DDFLIP_NOVSYNC */;
    long result;
    EEWEB_UNTIL_DONE_DRAWING(result, m_surface->vtbl->Flip(m_surface, 0, flags));
    return Status(result);
}

long DX7Surface::RestoreSurface()
{
    const long result = m_surface->vtbl->Restore(m_surface);
    if (result != 0) {
        ESRecordNativeError(static_cast<unsigned long>(result));
    }
    return result != 0 ? 1 : 0;
}

void DX7Surface::SetTransparencyColor(GEColor& color)
{
    const GEColorLayout& rgb = reinterpret_cast<const GEColorLayout&>(color);
    m_transparencyColor[0] = rgb.r;
    m_transparencyColor[1] = rgb.g;
    m_transparencyColor[2] = rgb.b;
    const unsigned long packed = GEPackColor(m_description.pixelFormat, color, 1.0f);
    DDCOLORKEY_ key = {packed, packed};
    m_surface->vtbl->SetColorKey(m_surface, 8 /* DDCKEY_SRCBLT */, &key); // like the original, the result is not looked at
}

// ---- locking ----------------------------------------------------------------------------------------------

long DX7Surface::Lock()
{
    DDSURFACEDESC2_ desc;
    __builtin_memset(&desc, 0, sizeof desc);
    desc.size = sizeof desc;
    long result;
    EEWEB_UNTIL_DONE_DRAWING(result, m_surface->vtbl->Lock(m_surface, 0, &desc, 0, 0));
    if (result == 0) {
        m_memory = static_cast<unsigned char*>(desc.surface);
        m_locked = true;
        m_pitch = desc.pitch;
        return 0;
    }
    return Status(result);
}

long DX7Surface::Unlock()
{
    const long result = m_surface->vtbl->Unlock(m_surface, 0);
    if (result == 0 || static_cast<unsigned long>(result) == DDERR_NOTLOCKED_) {
        m_locked = false;
        return 0;
    }
    ESRecordNativeError(static_cast<unsigned long>(result));
    return 1;
}

long DX7Surface::TextLock()
{
    if (m_textLocked) {
        return 2;
    }
    long result;
    EEWEB_UNTIL_DONE_DRAWING(result, m_surface->vtbl->GetDC(m_surface, &m_textDC));
    if (result == 0) {
        m_textLocked = true;
        return 0;
    }
    return Status(result);
}

long DX7Surface::TextUnlock()
{
    if (!m_textLocked) {
        return 2;
    }
    const long result = m_surface->vtbl->ReleaseDC(m_surface, m_textDC);
    if (result == 0) {
        m_textLocked = false;
        return 0;
    }
    ESRecordNativeError(static_cast<unsigned long>(result));
    return 1;
}

// ---- the filtered copy ------------------------------------------------------------------------------------------
// A bilinear stretch from `source` (a rectangle of this surface, in pixels) to `target` (a rectangle of
// `destination`), one destination pixel at a time, through the engine's channel unpack and pack functions of this
// surface's pixel format.  Every value the original stores in memory as a float is a `volatile float` here and
// every one it keeps in an x87 register a long double, so that the arithmetic rounds at the same places.
// Kept from the original: the source is read without any clamping, and a negative fraction (the sample position
// was rounded up) is moved into 0..1 by adding 1 without moving the sample, so a pixel is blended with the
// wrong neighbours; a failed lock of the destination is not an error (nothing is copied, the result is that of
// this surface's Unlock).

template <class Pixel>
long DX7Surface::CopyWithFilterOfSize(DX7Surface* destination, const GERectangleLayout& source, const GERectangleLayout& target)
{
    const unsigned long y0 = static_cast<unsigned long>(static_cast<long>(target.top));
    const unsigned long y1 = static_cast<unsigned long>(static_cast<long>(target.bottom));
    const unsigned long x0 = static_cast<unsigned long>(static_cast<long>(target.left));
    const unsigned long x1 = static_cast<unsigned long>(static_cast<long>(target.right));
    const Register width = static_cast<Register>(target.right) - static_cast<Register>(target.left);
    const Register height = static_cast<Register>(target.bottom) - static_cast<Register>(target.top);
    volatile float stepX = static_cast<float>(Register(1) / width);
    volatile float heightStored = static_cast<float>(height);
    volatile float stepY = static_cast<float>(Register(1) / height);
    volatile float halfStepX = static_cast<float>(Register(1) / (width + width));
    volatile float halfStepY = static_cast<float>(Register(1) / (static_cast<Register>(heightStored) + static_cast<Register>(heightStored)));

    long result = Lock();
    if (result != 0) {
        return result;
    }
    const unsigned char* sourceMemory = m_memory;
    const unsigned long format = m_description.pixelFormat;
    const unsigned long sourcePitch = m_pitch / sizeof(Pixel);
    if (destination->Lock() == 0) {
        const unsigned long destinationPitch = destination->m_pitch / sizeof(Pixel);
        Pixel* row = reinterpret_cast<Pixel*>(destination->m_memory) + destinationPitch * y0 + x0;
        for (unsigned long y = y0; y < y1; ++y, row += destinationPitch) {
            if (x0 >= x1) {
                continue;
            }
            Pixel* out = row;
            volatile float rowPosition = static_cast<float>(static_cast<Register>(y - y0 + 1) * stepY - halfStepY);
            for (unsigned long x = x0; x < x1; ++x) {
                const Register columnPosition = static_cast<Register>(x - x0 + 1) * stepX - halfStepX;
                volatile float sampleX = static_cast<float>(columnPosition * (static_cast<Register>(source.right) - source.left) + source.left - 0.5f);
                volatile float sampleY = static_cast<float>((static_cast<Register>(source.bottom) - source.top) * rowPosition + source.top - 0.5f);
                const long ix = RoundToLong(sampleX);
                const long iy = RoundToLong(sampleY);
                Register fractionXRegister = static_cast<Register>(sampleX) - ix;
                Register fractionYRegister = static_cast<Register>(sampleY) - iy;
                volatile float fractionX = static_cast<float>(fractionXRegister);
                volatile float fractionY = static_cast<float>(fractionYRegister);
                if (fractionXRegister < 0) {
                    fractionXRegister = fractionXRegister + 1.0f;
                    fractionX = static_cast<float>(fractionXRegister);
                }
                if (fractionYRegister < 0) {
                    fractionYRegister = fractionYRegister + 1.0f;
                    fractionY = static_cast<float>(fractionYRegister);
                }
                volatile float restX = static_cast<float>(Register(1) - fractionXRegister);
                volatile float restY = static_cast<float>(Register(1) - fractionYRegister);

                const Pixel* pixels = reinterpret_cast<const Pixel*>(sourceMemory);
                const long index = iy * static_cast<long>(sourcePitch) + ix;
                const unsigned long taps[4] = {pixels[index], pixels[index + 1], pixels[index + static_cast<long>(sourcePitch)],
                                               pixels[index + static_cast<long>(sourcePitch) + 1]};
                unsigned long channel[4][4]; // [tap][red, green, blue, alpha]
                for (int tap = 0; tap < 4; ++tap) {
                    channel[tap][0] = GEUnpackRed(format, taps[tap]);
                    channel[tap][1] = GEUnpackGreen(format, taps[tap]);
                    channel[tap][2] = GEUnpackBlue(format, taps[tap]);
                    channel[tap][3] = GEUnpackAlpha(format, taps[tap]);
                }
                long blended[4];
                for (int c = 0; c < 4; ++c) {
                    volatile float top = static_cast<float>(static_cast<Register>(channel[0][c]) * restX + static_cast<Register>(channel[1][c]) * fractionX);
                    const Register bottom = static_cast<Register>(channel[2][c]) * restX + static_cast<Register>(channel[3][c]) * fractionX;
                    volatile float value = static_cast<float>(bottom * fractionY + static_cast<Register>(top) * restY);
                    blended[c] = RoundToLong(value);
                }
                *out++ = static_cast<Pixel>(GEPackColor(format, static_cast<unsigned long>(blended[0]), static_cast<unsigned long>(blended[1]),
                                                        static_cast<unsigned long>(blended[2]), static_cast<unsigned long>(blended[3])));
            }
        }
        destination->Unlock();
    }
    return Unlock();
}

long DX7Surface::CopyWithFilter(GESurface* destination, GERectangle& sourceRectangle, GERectangle& destinationRectangle)
{
    DX7Surface* target = static_cast<DX7Surface*>(destination);
    if (m_description.pixelDepth == 0x20) {
        return CopyWithFilterOfSize<unsigned long>(target, Layout(sourceRectangle), Layout(destinationRectangle));
    }
    return CopyWithFilterOfSize<unsigned short>(target, Layout(sourceRectangle), Layout(destinationRectangle));
}
