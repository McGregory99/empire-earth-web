#include "dx7_screen.h"

#include "dx7_surface.h"
#include "engine_imports.h"
#include "rasterizer_abi.h"

extern "C" {
__declspec(dllimport) void __stdcall Sleep(unsigned long milliseconds);
__declspec(dllimport) void* __stdcall GetModuleHandleA(const char* name);
__declspec(dllimport) void* __stdcall GetProcAddress(void* module, const char* name);
__declspec(dllimport) unsigned long __stdcall GetLastError(void);
// user32 and gdi32: the window class and the window of the 32-bit probe, and CreateAWindow.
}

namespace {
struct WndClassEx {
    unsigned long size, style;
    long(__stdcall* windowProc)(void*, unsigned int, unsigned int, long);
    int classExtra, windowExtra;
    void *instance, *icon, *cursor, *background;
    const char *menuName, *className;
    void* smallIcon;
};
static_assert(sizeof(WndClassEx) == 0x30, "WNDCLASSEXA is 48 bytes");
struct Rect {
    long left, top, right, bottom;
};
} // namespace

extern "C" {
__declspec(dllimport) unsigned short __stdcall RegisterClassExA(const WndClassEx*);
__declspec(dllimport) int __stdcall UnregisterClassA(const char* className, void* instance);
__declspec(dllimport) int __stdcall GetClassInfoExA(void* instance, const char* className, WndClassEx* out);
__declspec(dllimport) int __stdcall AdjustWindowRect(Rect*, unsigned long style, int menu);
__declspec(dllimport) void* __stdcall CreateWindowExA(unsigned long exStyle, const char* className, const char* windowName, unsigned long style,
                                                       int x, int y, int width, int height, void* parent, void* menu, void* instance, void* param);
__declspec(dllimport) void* __stdcall LoadCursorA(void* instance, const char* name);
__declspec(dllimport) long __stdcall DefWindowProcA(void*, unsigned int, unsigned int, long);
__declspec(dllimport) void* __stdcall GetStockObject(int object);
}

namespace {

// ---- the registry of live screens (the original's static DX7Screen::sScreens) ------------------------
DX7Screen** g_screens;
unsigned long g_screenCount;
unsigned long g_screenCapacity;

// Captures are counted across all screens: only the first one decides whether later ones may be full
// screen (DX7Screen::sScreensCaptured, sFirstCaptureWasAttachedToDesktop).
unsigned long g_capturedScreens;
bool g_firstCaptureWasAttachedToDesktop;

// ---- pixel formats of display modes --------------------------------------------------------------------
// The original's table (data at 0x1001779c of DX7HRTnLDisplay.dll, 28-byte entries): the DDPIXELFORMAT flags and
// four-character code, the bit count and the four channel masks.  A display mode gets the index of the first
// entry with the same bit count and masks; the index is stored in GEVideoMode.pixelFormat.  A surface sends the
// whole entry to DirectDraw.  Entry 8 is DXT1, entries 9 to 11 are depth buffer formats (the "bit count" is the
// depth, the "green" mask the depth mask).
struct PixelFormat {
    unsigned long flags, fourCC, bitCount, r, g, b, a; // DDPIXELFORMAT: dwFlags, dwFourCC, dwRGBBitCount and the four masks
};
const unsigned long kDXT1 = 0x31545844; // 'DXT1'
const PixelFormat kPixelFormats[] = {
    {0x41, 0, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000}, // 0  A8R8G8B8
    {0x40, 0, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000}, // 1  X8R8G8B8
    {0x41, 0, 16, 0x00000f00, 0x000000f0, 0x0000000f, 0x0000f000}, // 2  A4R4G4B4
    {0x41, 0, 16, 0x00007c00, 0x000003e0, 0x0000001f, 0x00008000}, // 3  A1R5G5B5
    {0x41, 0, 16, 0x0000f800, 0x000007c0, 0x0000003e, 0x00000001}, // 4  R5G5B5A1 (as the original lists it)
    {0x40, 0, 16, 0x00007c00, 0x000003e0, 0x0000001f, 0x00000000}, // 5  X1R5G5B5
    {0x40, 0, 16, 0x0000f800, 0x000007c0, 0x0000003e, 0x00000000}, // 6
    {0x40, 0, 16, 0x0000f800, 0x000007e0, 0x0000001f, 0x00000000}, // 7  R5G6B5
    {0x04, kDXT1, 0, 0, 0, 0, 0},                                  // 8  DXT1 (DDPF_FOURCC)
    {0x400, 0, 32, 0, 0xffffffff, 0, 0},                           // 9  32-bit depth buffer (DDPF_ZBUFFER: bit count, z mask)
    {0x400, 0, 24, 0, 0x00ffffff, 0, 0},                           // 10 24-bit depth buffer
    {0x400, 0, 16, 0, 0x0000ffff, 0, 0},                           // 11 16-bit depth buffer
};
const unsigned long kPixelFormatCount = sizeof(kPixelFormats) / sizeof(kPixelFormats[0]);
const unsigned long kNoPixelFormat = 14; // the original's "not found" value

// ---- cooperative levels, as the original passes them to SetCooperativeLevel ---------------------------------
const unsigned long kCoopWindowed = 0x408;           // DDSCL_NORMAL | DDSCL_MULTITHREADED
const unsigned long kCoopFullScreen = 0x413;         // FULLSCREEN | ALLOWREBOOT | EXCLUSIVE | MULTITHREADED
const unsigned long kCoopFocusOnly = 0x80;           // DDSCL_SETFOCUSWINDOW
const unsigned long kCoopFullScreenCreateWindow = 0x513; // ... | CREATEDEVICEWINDOW
const unsigned long kCoopFullScreenSecond = 0x691;   // NOWINDOWCHANGES | ... for a second full screen capture
const unsigned long kCoopNormal = 0x8;               // DDSCL_NORMAL

struct MonitorInfo {
    unsigned long size;
    long monitor[4];
    long work[4];
    unsigned long flags;
};
typedef int(__stdcall* GetMonitorInfoProc)(void*, MonitorInfo*);

// user32's GetMonitorInfoA, resolved at run time like the original does.  Returns false for no monitor.
bool QueryMonitor(void* monitor, MonitorInfo& info)
{
    if (!monitor) {
        return false;
    }
    void* user32 = GetModuleHandleA("USER32.DLL");
    void* raw = user32 ? GetProcAddress(user32, "GetMonitorInfoA") : 0;
    if (!raw) {
        return false;
    }
    GetMonitorInfoProc proc = reinterpret_cast<GetMonitorInfoProc>(raw);
    info.size = sizeof(MonitorInfo);
    return proc(monitor, &info) == 1;
}

} // namespace

bool EEWebQueryMonitorFlags(void* monitor, unsigned long& flags)
{
    MonitorInfo info;
    if (!QueryMonitor(monitor, info)) {
        return false;
    }
    flags = info.flags;
    return true;
}

// ---- the registry -----------------------------------------------------------------------------------------

unsigned long DX7Screen::GetScreenCount()
{
    return g_screenCount;
}

DX7Screen* DX7Screen::GetScreen(unsigned long index)
{
    return g_screens[index];
}

// ---- construction ----------------------------------------------------------------------------------------

DX7Screen::DX7Screen(HMONITOR__* monitor, IDirectDraw7* directDraw, UString const& name, bool attachedToDesktop, bool sharesPrimary)
    : m_textureManager(0),
      m_rasterizerIndex(0),
      m_modes(0),
      m_modeCount(0),
      m_modeCapacity(0),
      m_name(name),
      m_directDraw(directDraw),
      m_monitor(monitor),
      m_currentMode(0),
      m_attachedToDesktop(attachedToDesktop),
      m_fullScreen(false),
      m_sharesPrimary(sharesPrimary),
      m_canMagFilter(false),
      m_canMinFilter(false),
      m_device32(false),
      m_texture32(false)
{
    // What the engine reaches: the texture manager and the rasterizer index sit where GEScreenLayout puts them.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
    static_assert(__builtin_offsetof(DX7Screen, m_textureManager) == __builtin_offsetof(GEScreenLayout, textureManager),
                  "the engine writes the texture manager at GEScreenLayout.textureManager");
    static_assert(__builtin_offsetof(DX7Screen, m_rasterizerIndex) == __builtin_offsetof(GEScreenLayout, rasterizerIndex),
                  "the engine writes the rasterizer index at GEScreenLayout.rasterizerIndex");
#pragma clang diagnostic pop

    for (int i = 0; i < 4; ++i) {
        m_desktopRectangle[i] = 0.0f;
    }

    DDCAPS_ caps;
    __builtin_memset(&caps, 0, sizeof caps);
    caps.size = sizeof caps;
    m_directDraw->vtbl->GetCaps(m_directDraw, &caps, 0);
    // The original tests the filter capabilities as `(caps | A) != 0 && (caps | B) != 0`, which is always
    // true, so every screen reports that it can magnify and minify.  Kept for fidelity.
    m_canMinFilter = true;
    m_canMagFilter = true;

    m_directDraw->vtbl->EnumDisplayModes(m_directDraw, 0, 0, this, &DX7Screen::EnumVideoModeCallback);

    DDSURFACEDESC2_ current;
    __builtin_memset(&current, 0, sizeof current);
    current.size = sizeof current;
    m_directDraw->vtbl->GetDisplayMode(m_directDraw, &current);
    for (unsigned long i = 0; i < m_modeCount; ++i) {
        const EEWebVideoMode& mode = m_modes[i];
        if (mode.width == current.width && mode.height == current.height && mode.bitsPerPixel == current.pixelFormat.rgbBitCount) {
            m_currentMode = i;
            break;
        }
    }

    Test32BitSupport();

    if (g_screenCount == g_screenCapacity) {
        const unsigned long capacity = g_screenCapacity ? g_screenCapacity * 2 : 4;
        DX7Screen** grown = static_cast<DX7Screen**>(::operator new(capacity * sizeof(DX7Screen*)));
        for (unsigned long i = 0; i < g_screenCount; ++i) {
            grown[i] = g_screens[i];
        }
        ::operator delete(g_screens);
        g_screens = grown;
        g_screenCapacity = capacity;
    }
    g_screens[g_screenCount++] = this;
}

DX7Screen::~DX7Screen()
{
    for (unsigned long i = 0; i < g_screenCount; ++i) {
        if (g_screens[i] == this) {
            for (unsigned long k = i + 1; k < g_screenCount; ++k) {
                g_screens[k - 1] = g_screens[k];
            }
            --g_screenCount;
            break;
        }
    }
    m_directDraw->vtbl->Release(m_directDraw);
    ::operator delete(m_modes);
    if (g_screenCount == 0) { // the last screen: give the registry back
        ::operator delete(g_screens);
        g_screens = 0;
        g_screenCapacity = 0;
    }
}

void DX7Screen::AddMode(const EEWebVideoMode& mode)
{
    if (m_modeCount == m_modeCapacity) {
        const unsigned long capacity = m_modeCapacity ? m_modeCapacity * 2 : 16;
        EEWebVideoMode* grown = static_cast<EEWebVideoMode*>(::operator new(capacity * sizeof(EEWebVideoMode)));
        for (unsigned long i = 0; i < m_modeCount; ++i) {
            grown[i] = m_modes[i];
        }
        ::operator delete(m_modes);
        m_modes = grown;
        m_modeCapacity = capacity;
    }
    m_modes[m_modeCount++] = mode;
}

int __attribute__((stdcall)) DX7Screen::EnumVideoModeCallback(DDSURFACEDESC2_* desc, void* context)
{
    EEWebVideoMode mode;
    mode.width = desc->width;
    mode.height = desc->height;
    mode.refreshRate = desc->refreshRate;
    mode.bitsPerPixel = desc->pixelFormat.rgbBitCount;
    mode.pixelFormat = kNoPixelFormat;
    for (unsigned long i = 0; i < kPixelFormatCount; ++i) {
        const PixelFormat& format = kPixelFormats[i];
        if (format.bitCount == desc->pixelFormat.rgbBitCount && format.r == desc->pixelFormat.rMask &&
            format.g == desc->pixelFormat.gMask && format.b == desc->pixelFormat.bMask && format.a == desc->pixelFormat.aMask) {
            mode.pixelFormat = i;
            break;
        }
    }
    static_cast<DX7Screen*>(context)->AddMode(mode);
    return 1; // DDENUMRET_OK: go on
}

// ---- getters -----------------------------------------------------------------------------------------------

GEVideoMode const& DX7Screen::GetMode(unsigned long index) const
{
    return reinterpret_cast<GEVideoMode const&>(m_modes[index]);
}

GEVideoMode const& DX7Screen::GetCurrentMode() const
{
    return reinterpret_cast<GEVideoMode const&>(m_modes[m_currentMode]);
}

GERectangle const& DX7Screen::GetDesktopRectangle()
{
    MonitorInfo info;
    if (QueryMonitor(m_monitor, info)) {
        m_desktopRectangle[0] = static_cast<float>(info.monitor[3]); // bottom
        m_desktopRectangle[1] = static_cast<float>(info.monitor[0]); // left
        m_desktopRectangle[2] = static_cast<float>(info.monitor[2]); // right
        m_desktopRectangle[3] = static_cast<float>(info.monitor[1]); // top
    } else {
        // Without a monitor (every screen the rasterizer creates has none) the desktop is the current mode.
        m_desktopRectangle[0] = static_cast<float>(m_modes[m_currentMode].height);
        m_desktopRectangle[1] = 0.0f;
        m_desktopRectangle[2] = static_cast<float>(m_modes[m_currentMode].width);
        m_desktopRectangle[3] = 0.0f;
    }
    return reinterpret_cast<GERectangle const&>(m_desktopRectangle);
}

// ---- capturing the screen ----------------------------------------------------------------------------------

namespace {
void* WindowHandle(GEWindow const* window)
{
    return *reinterpret_cast<void* const*>(window); // the HWND is the first field of a GEWindow
}
} // namespace

long DX7Screen::Capture(GEWindow const* window, bool fullScreen)
{
    void* hwnd = WindowHandle(window);
    long result;
    if (fullScreen) {
        if (g_capturedScreens == 0) {
            if (!GetAttachedToDesktop()) {
                result = m_directDraw->vtbl->SetCooperativeLevel(m_directDraw, hwnd, kCoopFullScreen);
                if (result == 0) {
                    m_fullScreen = true;
                    g_firstCaptureWasAttachedToDesktop = false;
                }
            } else {
                result = m_directDraw->vtbl->SetCooperativeLevel(m_directDraw, hwnd, kCoopFocusOnly);
                if (result == 0) {
                    result = m_directDraw->vtbl->SetCooperativeLevel(m_directDraw, hwnd, kCoopFullScreenCreateWindow);
                }
                if (result == 0) {
                    m_fullScreen = true;
                    g_firstCaptureWasAttachedToDesktop = true;
                }
            }
        } else {
            // Another screen is already captured: only a second desktop-attached screen may follow a first one.
            if (!g_firstCaptureWasAttachedToDesktop || !GetAttachedToDesktop()) {
                return 0xd;
            }
            result = m_directDraw->vtbl->SetCooperativeLevel(m_directDraw, hwnd, kCoopFullScreenSecond);
            if (result == 0) {
                m_fullScreen = true;
            }
        }
        if (result != 0) {
            ESRecordNativeError(static_cast<unsigned long>(result));
            m_fullScreen = false;
            return 1;
        }
    } else if (g_capturedScreens == 0) {
        m_fullScreen = false;
        result = m_directDraw->vtbl->SetCooperativeLevel(m_directDraw, hwnd, kCoopWindowed);
        if (result != 0) {
            ESRecordNativeError(static_cast<unsigned long>(result));
            return 1;
        }
        g_firstCaptureWasAttachedToDesktop = GetAttachedToDesktop();
    } else {
        if (!g_firstCaptureWasAttachedToDesktop || !GetAttachedToDesktop()) {
            return 0xd;
        }
        m_fullScreen = false;
        result = m_directDraw->vtbl->SetCooperativeLevel(m_directDraw, hwnd, kCoopWindowed);
        if (result != 0) {
            ESRecordNativeError(static_cast<unsigned long>(result));
            return 1;
        }
    }
    ++g_capturedScreens;
    return 0;
}

long DX7Screen::Release(GEWindow const* window)
{
    const long result = m_directDraw->vtbl->SetCooperativeLevel(m_directDraw, WindowHandle(window), kCoopNormal);
    if (result != 0) {
        ESRecordNativeError(static_cast<unsigned long>(result));
        return 1;
    }
    m_fullScreen = false;
    --g_capturedScreens;
    return 0;
}

long DX7Screen::SetMode(unsigned long index)
{
    if (!m_fullScreen) {
        return 0; // a windowed screen keeps the desktop's mode
    }
    const EEWebVideoMode& mode = m_modes[index];
    // The original reads the bit count from its format table by the mode's format index, which for a mode
    // without a table entry (index 14) reads past the table; the mode's own bit count is used for those.
    const unsigned long bits = mode.pixelFormat < kPixelFormatCount ? kPixelFormats[mode.pixelFormat].bitCount : mode.bitsPerPixel;
    const long result = m_directDraw->vtbl->SetDisplayMode(m_directDraw, mode.width, mode.height, bits, mode.refreshRate, 0);
    if (result != 0) {
        ESRecordNativeError(static_cast<unsigned long>(result));
    }
    m_currentMode = index; // like the original, also when the call failed
    return result != 0 ? 1 : 0;
}

long DX7Screen::RestoreAllSurfaces()
{
    if (m_directDraw->vtbl->TestCooperativeLevel(m_directDraw) != 0) {
        Sleep(100); // the level is lost (another application has the display): give it time
        return 1;
    }
    const long result = m_directDraw->vtbl->RestoreAllSurfaces(m_directDraw);
    if (result != 0) {
        ESRecordNativeError(static_cast<unsigned long>(result));
        return 1;
    }
    return 0;
}

// ---- surfaces and windows ---------------------------------------------------------------------------------

namespace {

const unsigned long kTypeBackBuffer = 7; // a flip chain's back buffer: the mark the plugin puts on the description

long ToLong(float value)
{
    long result;
    __asm__("fistpl %0" : "=m"(result) : "t"(value) : "st");
    return result;
}

// GEScreen::sInstance, the HINSTANCE the engine registers its window classes with.
void* EngineInstance()
{
    void* engine = GetModuleHandleA("Low-Level Engine.dll");
    void* variable = engine ? GetProcAddress(engine, "?sInstance@GEScreen@@0PAUHINSTANCE__@@A") : 0;
    return variable ? *static_cast<void**>(variable) : 0;
}

const GUID_ kIidDirect3D7 = {0xf5049e77, 0x4861, 0x11d2, {0xa4, 0x07, 0x00, 0xa0, 0xc9, 0x06, 0x29, 0xa8}};
const GUID_ kIidDirect3DHalDevice = {0x84e63de0, 0x46aa, 0x11cf, {0x81, 0x6f, 0x00, 0x00, 0xc0, 0x20, 0x15, 0x6e}};
const char kTestWindowClass[] = "TestWindowClass";

} // namespace

// What DirectDraw is asked for, by surface type:
//   0 the primary surface            1 an offscreen surface in video memory   2 the primary surface with a flip chain
//   3 a depth buffer                 4 an offscreen surface in system memory  5 a texture (managed, or a render target
//   6 a texture with mipmap levels   when 3D renderable)                       any other type: no flags and no caps
// Depth buffers asked for with pixel format 12 or 13 ("whatever depth the driver has") try the 16, 24 and 32-bit
// formats (13: 32, 24, 16) one after the other, and the description handed in ends up with the one that worked.
// A flip chain gets one DX7Surface per back buffer attached to its predecessor, a mipmapped texture one per level
// (half the size of the previous one).
long DX7Screen::CreateSurface(GESurfaceDescription& description, GESurface** out)
{
    GESurfaceDescriptionLayout& d = reinterpret_cast<GESurfaceDescriptionLayout&>(description);
    GESurfaceDescriptionLayout levelDescription = d; // the mipmap chain starts from the description as it came in
    *out = 0;
    long result = 0;

    if (d.type == 3 && (d.pixelFormat == 0xc || d.pixelFormat == 0xd)) {
        static const unsigned long kDepthOrder[2][3] = {{0xb, 10, 9}, {9, 10, 0xb}};
        static const unsigned long kDepthBits[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0x20, 0x18, 0x10};
        const unsigned long* order = kDepthOrder[d.pixelFormat == 0xd ? 1 : 0];
        for (int i = 0; i < 3; ++i) {
            d.pixelFormat = order[i];
            result = CreateSurface(description, out);
            if (result == 0 || i == 2) { // the depth is recorded for the format that worked, or for the last one tried
                d.pixelDepth = kDepthBits[order[i]];
            }
            if (result == 0) {
                break;
            }
        }
        if (result != 0) {
            return result;
        }
    }

    if (*out == 0) {
        DDSURFACEDESC2_ ddesc;
        __builtin_memset(&ddesc, 0, sizeof ddesc);
        const unsigned long colorKey = d.isTransparencyEnabled ? 0x10000 /* DDSD_CKSRCBLT */ : 0;
        const unsigned long videoCaps3D = d.is3DRenderable ? 0x2000 /* DDSCAPS_3DDEVICE */ : 0;
        ddesc.size = sizeof ddesc;
        ddesc.width = d.width;
        ddesc.height = d.height;
        if (d.pixelFormat < kPixelFormatCount) { // the original reads past its table for an unknown index
            const PixelFormat& format = kPixelFormats[d.pixelFormat];
            ddesc.pixelFormat.flags = format.flags;
            ddesc.pixelFormat.fourCC = format.fourCC;
            ddesc.pixelFormat.rgbBitCount = format.bitCount;
            ddesc.pixelFormat.rMask = format.r;
            ddesc.pixelFormat.gMask = format.g;
            ddesc.pixelFormat.bMask = format.b;
            ddesc.pixelFormat.aMask = format.a;
        }
        ddesc.pixelFormat.size = 0x20;
        switch (d.type) {
        case 0:
            ddesc.flags = colorKey | 1;
            ddesc.caps[0] = videoCaps3D + 0x10004200;
            break;
        case 1:
            ddesc.flags = colorKey | 0x1007;
            ddesc.caps[0] = videoCaps3D + 0x10004040;
            break;
        case 2:
            ddesc.flags = colorKey | 0x21;
            ddesc.caps[0] = videoCaps3D + 0x10004218;
            ddesc.backBufferCount = d.backBufferCount;
            break;
        case 3:
            ddesc.flags = colorKey | 0x1007;
            ddesc.caps[0] = 0x10024000;
            break;
        case 4:
            ddesc.flags = colorKey | 0x1007;
            ddesc.caps[0] = videoCaps3D + 0x840;
            break;
        case 5:
            ddesc.flags = colorKey | 0x1007;
            if (!d.is3DRenderable) {
                ddesc.caps[0] = 0x1000;
                ddesc.caps[1] = (d.volatility != 0 ? 4 : 0) + 0x14;
            } else {
                ddesc.caps[0] = 0x10007000;
                ddesc.caps[1] = (d.volatility != 0 ? 4 : 0) + 4;
            }
            break;
        case 6:
            ddesc.flags = colorKey | 0x21007;
            ddesc.caps[0] = videoCaps3D + 0x401008;
            ddesc.caps[1] = (d.volatility != 0 ? 4 : 0) + 0x14;
            ddesc.refreshRate = d.mipMapCount; // dwMipMapCount shares the field
            break;
        default:
            break;
        }

        IDirectDrawSurface7* surface = 0;
        const long status = m_directDraw->vtbl->CreateSurface(m_directDraw, &ddesc, &surface, 0);
        if (status != 0) {
            ESRecordNativeError(static_cast<unsigned long>(status));
            return 1;
        }
        DX7Surface* root = new DX7Surface(surface, d, this);
        *out = root;
        if (!root) {
            return 0;
        }
        if (d.type == 2) {
            d.type = kTypeBackBuffer; // the caller's description is changed, as in the original
            DDSCAPS2_ caps = {0x10 /* DDSCAPS_FLIP */, 0, 0, 0};
            IDirectDrawSurface7* back = 0;
            DX7Surface* current = root;
            for (unsigned long i = 0; i < d.backBufferCount; ++i) {
                current->m_surface->vtbl->GetAttachedSurface(current->m_surface, &caps, &back); // a failure leaves `back` as it was
                DX7Surface* child = new DX7Surface(back, d, this);
                if (!child) {
                    break;
                }
                current->AddAttached(child);
                current = child;
            }
        } else if (d.type == 6 && d.mipMapCount > 1) {
            DDSCAPS2_ caps = {0x400000 /* DDSCAPS_MIPMAP */, 0, 0, 0};
            IDirectDrawSurface7* level = 0;
            DX7Surface* current = root;
            for (unsigned long i = 0; i < d.mipMapCount - 1; ++i) {
                current->m_surface->vtbl->GetAttachedSurface(current->m_surface, &caps, &level);
                levelDescription.width >>= 1;
                levelDescription.height >>= 1;
                DX7Surface* child = new DX7Surface(level, levelDescription, this);
                if (!child) {
                    break;
                }
                current->AddAttached(child);
                current = child;
            }
        }
    }
    return result;
}

// A window of the given size at the given position.  `style` 1 is an ordinary window whose client area is the
// size asked for (but the position is that of the client area, as the original passes it), anything else a
// topmost popup (a child of `parent` when one is given).  The GEWindow the engine gets owns the window.
long DX7Screen::CreateAWindow(GE2DPoint const& position, unsigned long width, unsigned long height, UString& className, UString& windowName,
                              GEWindow*& window, unsigned long style, GEWindow* parent)
{
    const GE2DPointLayout& origin = reinterpret_cast<const GE2DPointLayout&>(position);
    const char* classText = *reinterpret_cast<char* const*>(reinterpret_cast<const char*>(&className) + 4); // UStringLayout.chars
    const char* titleText = *reinterpret_cast<char* const*>(reinterpret_cast<const char*>(&windowName) + 4);
    void* parentWindow = 0;
    unsigned long windowStyle = 0x90000000; // WS_POPUP | WS_VISIBLE
    unsigned long exStyle = 8;              // WS_EX_TOPMOST
    int w = static_cast<int>(width), h = static_cast<int>(height);
    const int x = static_cast<int>(ToLong(origin.x));
    const int y = static_cast<int>(ToLong(origin.y));
    void* instance = EngineInstance();

    if (style == 1) {
        bool hasMenu = false;
        WndClassEx info;
        __builtin_memset(&info, 0, sizeof info);
        info.size = sizeof info;
        GetClassInfoExA(instance, classText, &info);
        if (info.menuName != 0) {
            hasMenu = true;
        }
        Rect rect;
        rect.left = ToLong(origin.x);
        rect.top = ToLong(origin.y);
        rect.right = ToLong(origin.x + static_cast<float>(static_cast<long>(width)));
        rect.bottom = ToLong(origin.y + static_cast<float>(static_cast<long>(height)));
        AdjustWindowRect(&rect, 0xcf0000 /* WS_OVERLAPPEDWINDOW */, hasMenu ? 1 : 0);
        windowStyle = 0x10cf0000;
        exStyle = 0;
        w = rect.right - rect.left;
        h = rect.bottom - rect.top;
    } else if (parent != 0) {
        parentWindow = WindowHandle(parent);
        windowStyle = 0xd0000000;
    }

    void* hwnd = CreateWindowExA(exStyle, classText, titleText, windowStyle, x, y, w, h, parentWindow, 0, instance, 0);
    if (hwnd == 0) {
        ESRecordNativeError(GetLastError());
        return 1;
    }
    GEWindowLayout* created = static_cast<GEWindowLayout*>(::operator new(sizeof(GEWindowLayout)));
    if (created != 0) {
        __builtin_memset(created, 0, sizeof *created);
        created->hwnd = hwnd;
        created->width = width;
        created->height = height;
        created->style = style;
        created->ownsWindow = 1;
    }
    window = reinterpret_cast<GEWindow*>(created);
    return 0;
}

// ---- the 32-bit probe -----------------------------------------------------------------------------------------

long __stdcall DX7Screen::TestWindowProc(void* window, unsigned int message, unsigned int wparam, long lparam)
{
    return DefWindowProcA(window, message, wparam, lparam);
}

long __stdcall DX7Screen::EnumTextureFormatCallback(DDPIXELFORMAT_* format, void* found)
{
    if (format->rgbBitCount == 0x20 && (format->aMask & 0xff000000) != 0 && (format->rMask & 0xff0000) != 0 && (format->gMask & 0xff00) != 0 &&
        (format->bMask & 0xff) != 0) {
        *static_cast<bool*>(found) = true;
        return 0; // enough: stop the enumeration
    }
    return 1;
}

// Decides Get32BitDeviceSupport (the Direct3D device on a 2x2 offscreen surface of the 800x600x16 mode's format
// renders into 32-bit surfaces) and Get32BitTextureSupport (the device lists a 32-bit texture format with alpha, and
// a 16x16 ARGB texture can really be created), by building a small window, capturing it and creating the surface.
// Both stay false on the first thing that fails.  Without an 800x600x16 mode the original reads one past its mode
// list; the current mode is used here.
void DX7Screen::Test32BitSupport()
{
    m_device32 = false;
    m_texture32 = false;

    void* instance = EngineInstance();
    WndClassEx windowClass;
    __builtin_memset(&windowClass, 0, sizeof windowClass);
    windowClass.size = sizeof windowClass;
    windowClass.style = 3; // CS_HREDRAW | CS_VREDRAW
    windowClass.windowProc = &DX7Screen::TestWindowProc;
    windowClass.instance = instance;
    windowClass.cursor = LoadCursorA(0, reinterpret_cast<const char*>(0x7f00)); // IDC_ARROW
    windowClass.background = GetStockObject(4);                               // BLACK_BRUSH
    windowClass.className = kTestWindowClass;
    RegisterClassExA(&windowClass);

    GEWindow* window = 0;
    {
        UString title("");
        UString className(kTestWindowClass);
        float origin[2] = {0.0f, 0.0f};
        const long created = CreateAWindow(*reinterpret_cast<GE2DPoint const*>(origin), 0x10, 0x10, className, title, window, 0, 0);
        if (created == 0) {
            if (Capture(window, false) == 0) {
                unsigned long mode = 0;
                const unsigned long count = GetModeCount();
                while (mode < count) {
                    const EEWebVideoMode& candidate = m_modes[mode];
                    if (candidate.width == 800 && candidate.height == 600 && candidate.bitsPerPixel == 16) {
                        break;
                    }
                    ++mode;
                }
                if (mode >= count) {
                    mode = m_currentMode;
                }
                GESurfaceDescriptionLayout probe;
                __builtin_memset(&probe, 0, sizeof probe);
                probe.type = 1;
                probe.pixelFormat = m_modes[mode].pixelFormat;
                probe.width = 2;
                probe.height = 2;
                probe.pixelDepth = m_modes[mode].bitsPerPixel;
                probe.is3DRenderable = 1;
                GESurface* surface = 0;
                if (CreateSurface(reinterpret_cast<GESurfaceDescription&>(probe), &surface) == 0 && surface != 0) {
                    DX7Surface* probeSurface = dynamic_cast<DX7Surface*>(surface);
                    IDirect3D7* direct3d = 0;
                    if (probeSurface && m_directDraw->vtbl->QueryInterface(m_directDraw, &kIidDirect3D7, reinterpret_cast<void**>(&direct3d)) == 0) {
                        IDirect3DDevice7* device = 0;
                        if (direct3d->vtbl->CreateDevice(direct3d, &kIidDirect3DHalDevice, probeSurface->m_surface, &device) == 0) {
                            unsigned long caps[0xec / 4]; // D3DDEVICEDESC7 is 236 bytes
                            device->vtbl->GetCaps(device, caps);
                            m_device32 = (caps[0x74 / 4] & 0x100 /* DDBD_32 */) != 0; // dwDeviceRenderBitDepth
                            m_texture32 = false;
                            device->vtbl->EnumTextureFormats(device, &DX7Screen::EnumTextureFormatCallback, &m_texture32);
                            if (m_texture32) {
                                GESurfaceDescriptionLayout texture;
                                __builtin_memset(&texture, 0, sizeof texture);
                                texture.type = 5;
                                texture.pixelFormat = 0;
                                texture.volatility = 1;
                                texture.width = 0x10;
                                texture.height = 0x10;
                                texture.pixelDepth = 0x20;
                                GESurface* textureSurface = 0;
                                if (CreateSurface(reinterpret_cast<GESurfaceDescription&>(texture), &textureSurface) == 0 && textureSurface != 0) {
                                    reinterpret_cast<UReferenceCounter*>(textureSurface)->Dereference();
                                } else {
                                    m_texture32 = false;
                                }
                            }
                            device->vtbl->Release(device);
                        } else {
                            m_device32 = false;
                            m_texture32 = false;
                        }
                        direct3d->vtbl->Release(direct3d);
                    } else {
                        m_device32 = false;
                        m_texture32 = false;
                    }
                    reinterpret_cast<UReferenceCounter*>(surface)->Dereference();
                } else {
                    m_device32 = false;
                    m_texture32 = false;
                }
                Release(window);
            }
            if (window != 0) {
                window->~GEWindow();
                ::operator delete(window);
            }
        }
    }
    UnregisterClassA(kTestWindowClass, instance);
}
