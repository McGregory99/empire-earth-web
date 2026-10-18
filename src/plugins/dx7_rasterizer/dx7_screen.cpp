#include "dx7_screen.h"

#include "rasterizer_abi.h"

// The engine's error log: every failed DirectDraw call is recorded there, as the original does.
unsigned long ESRecordNativeError(unsigned long error); // ?ESRecordNativeError@@YAKK@Z

extern "C" {
__declspec(dllimport) void __stdcall Sleep(unsigned long milliseconds);
__declspec(dllimport) void* __stdcall GetModuleHandleA(const char* name);
__declspec(dllimport) void* __stdcall GetProcAddress(void* module, const char* name);
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
// The original's table (data at 0x100177a4 of DX7HRTnLDisplay.dll, 28-byte entries): bit count and the
// four channel masks.  A display mode gets the index of the first entry with the same bit count and masks;
// the index is stored in GEVideoMode.pixelFormat.  Entry 7 is the 565 layout (its fourth-from-last field
// marks DXT1 for surfaces, which a display mode never is); entries 8 to 11 are depth buffer formats.
struct PixelFormat {
    unsigned long bitCount, r, g, b, a;
};
const PixelFormat kPixelFormats[] = {
    {32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000}, // 0  A8R8G8B8
    {32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000}, // 1  X8R8G8B8
    {16, 0x00000f00, 0x000000f0, 0x0000000f, 0x0000f000}, // 2  A4R4G4B4
    {16, 0x00007c00, 0x000003e0, 0x0000001f, 0x00008000}, // 3  A1R5G5B5
    {16, 0x0000f800, 0x000007c0, 0x0000003e, 0x00000001}, // 4  R5G5B5A1 (as the original lists it)
    {16, 0x00007c00, 0x000003e0, 0x0000001f, 0x00000000}, // 5  X1R5G5B5
    {16, 0x0000f800, 0x000007c0, 0x0000003e, 0x00000000}, // 6
    {16, 0x0000f800, 0x000007e0, 0x0000001f, 0x00000000}, // 7  R5G6B5
    {0, 0, 0, 0, 0},                                      // 8
    {32, 0, 0xffffffff, 0, 0},                            // 9
    {24, 0, 0x00ffffff, 0, 0},                            // 10
    {16, 0, 0x0000ffff, 0, 0},                            // 11
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

    // The original now probes 32-bit device and texture support by creating a window and a surface
    // (Test32BitSupport); both flags stay false until surfaces exist (P2.6.2, P2.6.3).

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

// ---- surfaces and windows: P2.6.2 ------------------------------------------------------------------------

long DX7Screen::CreateSurface(GESurfaceDescription&, GESurface**)
{
    return EEWEB_NOT_SUPPORTED;
}

long DX7Screen::CreateAWindow(GE2DPoint const&, unsigned long, unsigned long, UString&, UString&, GEWindow*&, unsigned long, GEWindow*)
{
    return EEWEB_NOT_SUPPORTED;
}
