#include "null_rasterizer.h"

#include "rasterizer_abi.h"

unsigned long ESRecordNativeError(unsigned long error); // ?ESRecordNativeError@@YAKK@Z

namespace {

// The rasterizer that is enumerating devices (the original's DAT_10017d28); the enumeration callback has no other way to reach it.
EEWebNullRasterizer* g_activating;

const GUID_ kIidDirectDraw7 = {0x15e65ec0, 0x3b9c, 0x11d2, {0xb9, 0x2f, 0x00, 0x60, 0x97, 0x97, 0xea, 0x5b}};
const GUID_ kIidDirect3D7 = {0xf5049e77, 0x4861, 0x11d2, {0xa4, 0x07, 0x00, 0xa0, 0xc9, 0x06, 0x29, 0xa8}};

long g_destroyed; // destructor runs of any EEWebNullRasterizer in this DLL (read by the self-check only)

DX7RasterizerAbi* MakeNullRasterizer()
{
    return new EEWebNullRasterizer;
}

// Called through a volatile pointer so the optimizer sees an unknown dynamic type.
DX7RasterizerAbi* (*volatile g_factory)() = &MakeNullRasterizer;

} // namespace

EEWebNullRasterizer::EEWebNullRasterizer()
{
    // The harness reads the counters at this offset, behind the vptr.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
    static_assert(__builtin_offsetof(EEWebNullRasterizer, m_counters) == EEWEB_COUNTERS_OFFSET,
                  "the counters must sit right behind the vptr");
#pragma clang diagnostic pop
    for (int k = 0; k < EEWEB_COUNTER_COUNT; ++k) {
        m_counters[k] = 0;
    }
    m_screens = 0;
    m_screenCount = 0;
    m_screenCapacity = 0;
}

EEWebNullRasterizer::~EEWebNullRasterizer()
{
    ::operator delete(m_screens); // the screens themselves belong to Deactivate
    ++g_destroyed;
    ++m_counters[EEWEB_COUNTER_DESTROYED];
}

long EEWebNullRasterizer::Note(long canaryStatus)
{
    if (canaryStatus != EEWEB_LAYOUT_OK) {
        ++m_counters[EEWEB_COUNTER_LAYOUT_VIOLATIONS];
        m_counters[EEWEB_COUNTER_LAST_VIOLATION] = canaryStatus;
    }
    return canaryStatus;
}

long EEWebNullRasterizer::Verdict(long canaryStatus, long result)
{
    return Note(canaryStatus) == EEWEB_LAYOUT_OK ? result : EEWEB_LAYOUT_VIOLATION;
}

long EEWebNullRasterizer::Activate()
{
    return ActivateRasterizer();
}

unsigned long EEWebNullRasterizer::GetScreenCount() const
{
    return m_screenCount;
}

void EEWebNullRasterizer::NoteSelfCheck(bool passed)
{
    ++m_counters[EEWEB_COUNTER_SELF_CHECKS];
    if (!passed) {
        ++m_counters[EEWEB_COUNTER_SELF_CHECK_FAILURES];
    }
}

long __stdcall FindTnLDevice(char*, char*, void* deviceDesc7, void* found);

// ---- activation: one screen per DirectDraw device that can render -------------------------------------------

// DirectDrawEnumerateEx callback (the original's FUN_10008f04): create the device, make a screen of it when it
// has 3D hardware, keep the screen when the rasterizer finds it valid, and go on with the next device.
int __attribute__((stdcall)) EEWebNullRasterizer::EnumerateDevice(GUID_* guid, char* description, char*, void*, void* monitor)
{
    // A secondary device without a monitor is not attached to the desktop; everything else is.
    const bool attachedToDesktop = !(guid != 0 && monitor == 0);
    unsigned long monitorFlags = 0;
    const bool sharesPrimary = EEWebQueryMonitorFlags(monitor, monitorFlags) ? (monitorFlags & 1) != 0 : monitor == 0;

    IDirectDraw7* directDraw = 0;
    long error = DirectDrawCreateEx(guid, reinterpret_cast<void**>(&directDraw), &kIidDirectDraw7, 0);
    if (error != 0) {
        ESRecordNativeError(static_cast<unsigned long>(error));
        return 1;
    }
    DDCAPS_ caps;
    __builtin_memset(&caps, 0, sizeof caps);
    caps.size = sizeof caps;
    error = directDraw->vtbl->GetCaps(directDraw, &caps, 0);
    if (error != 0) {
        ESRecordNativeError(static_cast<unsigned long>(error));
        directDraw->vtbl->Release(directDraw);
        return 1;
    }
    if ((caps.caps & DDCAPS_3D) != DDCAPS_3D) {
        directDraw->vtbl->Release(directDraw);
        return 1;
    }
    UString name(description);
    DX7Screen* screen = new DX7Screen(0, directDraw, name, attachedToDesktop, sharesPrimary);
    if (screen == 0) {
        directDraw->vtbl->Release(directDraw);
    } else if (g_activating->GetIsScreenValid(screen)) {
        g_activating->AddValidScreen(screen);
    } else {
        delete screen; // releases the DirectDraw object
    }
    return 1;
}

long EEWebNullRasterizer::ActivateRasterizer()
{
    ++m_counters[EEWEB_COUNTER_ACTIVATIONS];
    g_activating = this;
    const long error = DirectDrawEnumerateExA(&EEWebNullRasterizer::EnumerateDevice, 0, 7); // all devices, attached or not
    g_activating = 0;
    if (error != 0) {
        ESRecordNativeError(static_cast<unsigned long>(error));
        return 1;
    }
    return 0;
}

long EEWebNullRasterizer::AddValidScreen(GEScreen* screen)
{
    if (m_screenCount == m_screenCapacity) {
        const unsigned long capacity = m_screenCapacity ? m_screenCapacity * 2 : 4;
        DX7Screen** grown = static_cast<DX7Screen**>(::operator new(capacity * sizeof(DX7Screen*)));
        for (unsigned long i = 0; i < m_screenCount; ++i) {
            grown[i] = m_screens[i];
        }
        ::operator delete(m_screens);
        m_screens = grown;
        m_screenCapacity = capacity;
    }
    m_screens[m_screenCount++] = static_cast<DX7Screen*>(screen);
    m_counters[EEWEB_COUNTER_SCREENS] = static_cast<long>(m_screenCount);
    return 0;
}

// A screen is valid for the hardware T&L rasterizer when its Direct3D 7 offers a hardware-rasterizing device
// with hardware transform and lighting (D3DDEVCAPS_HWRASTERIZATION | D3DDEVCAPS_HWTRANSFORMANDLIGHT).
long __stdcall FindTnLDevice(char*, char*, void* deviceDesc7, void* found)
{
    const unsigned long devCaps = *static_cast<unsigned long*>(deviceDesc7);
    const unsigned long wanted = D3DDEVCAPS_HWRASTERIZATION | D3DDEVCAPS_HWTRANSFORMANDLIGHT;
    if ((devCaps & wanted) == wanted) {
        *static_cast<bool*>(found) = true;
        return 0; // DDENUMRET_CANCEL: found one
    }
    return 1;
}

bool EEWebNullRasterizer::GetIsScreenValid(DX7Screen* screen)
{
    if (screen == 0) {
        return false;
    }
    IDirectDraw7* directDraw = screen->GetDirectDraw();
    IDirect3D7* direct3d = 0;
    long error = directDraw->vtbl->QueryInterface(directDraw, &kIidDirect3D7, reinterpret_cast<void**>(&direct3d));
    if (error != 0) {
        ESRecordNativeError(static_cast<unsigned long>(error));
        return false;
    }
    bool found = false;
    error = direct3d->vtbl->EnumDevices(direct3d, &FindTnLDevice, &found);
    if (error != 0) {
        ESRecordNativeError(static_cast<unsigned long>(error));
        found = false;
    }
    direct3d->vtbl->Release(direct3d);
    return found;
}

void EEWebNullRasterizer::ClearScreens()
{
    for (unsigned long i = 0; i < m_screenCount; ++i) {
        delete m_screens[i]; // releases the screen's DirectDraw object and unregisters it
    }
    m_screenCount = 0;
    m_counters[EEWEB_COUNTER_SCREENS] = 0;
}

long EEWebNullRasterizer::Deactivate()
{
    ++m_counters[EEWEB_COUNTER_DEACTIVATIONS];
    ClearScreens();
    return 0;
}

// ---- video memory of a screen's DirectDraw device ------------------------------------------------------------

long EEWebNullRasterizer::GetVideoMemory(unsigned long screen, unsigned long& total, unsigned long& available)
{
    DDSCAPS2_ caps = {DDSCAPS_VIDEOMEMORY, 0, 0, 0};
    unsigned long totalMemory = 0;
    unsigned long freeMemory = 0;
    IDirectDraw7* directDraw = m_screens[screen]->GetDirectDraw();
    const long error = directDraw->vtbl->GetAvailableVidMem(directDraw, &caps, &totalMemory, &freeMemory);
    if (error == 0 && totalMemory != 0) {
        total = totalMemory;
        available = freeMemory;
        return 0;
    }
    return 0xd;
}

long EEWebNullRasterizer::GetTotalVideoMemory(unsigned long screen, unsigned long& total)
{
    unsigned long totalMemory = 0;
    unsigned long freeMemory = 0;
    const long result = GetVideoMemory(screen, totalMemory, freeMemory);
    if (result == 0) {
        total = totalMemory;
    }
    return result;
}

long EEWebNullRasterizer::GetAvailableVideoMemory(unsigned long screen, unsigned long& available)
{
    unsigned long totalMemory = 0;
    unsigned long freeMemory = 0;
    const long result = GetVideoMemory(screen, totalMemory, freeMemory);
    if (result == 0) {
        available = freeMemory;
    }
    return result;
}

long EEWebNullRasterizer::Draw(GEBitmap*, unsigned long mode)
{
    return EEWEB_DRAW_BITMAP_BASE + static_cast<long>(mode);
}

long EEWebNullRasterizer::Draw(GELine*, unsigned long first, unsigned long second)
{
    return EEWEB_DRAW_LINE_BASE + static_cast<long>(first + second);
}

void RunClassSelfCheck(EEWebNullRasterizer& shared)
{
    DX7RasterizerAbi* made = g_factory();
    EEWebNullRasterizer* temp = dynamic_cast<EEWebNullRasterizer*>(made);
    bool ok = temp != 0;
    if (temp) {
        ok = ok && temp->GetScreenCount() == 0;
        ok = ok && made->Draw(static_cast<GEBitmap*>(0), 3UL) == EEWEB_DRAW_BITMAP_BASE + 3;
        ok = ok && made->Draw(static_cast<GELine*>(0), 2UL, 3UL) == EEWEB_DRAW_LINE_BASE + 5;
        ok = ok && !made->GetIsScreenValid(0);
        ok = ok && dynamic_cast<EEWebNullRasterizer*>(static_cast<DX7RasterizerAbi*>(&shared)) != 0;
        const long destroyedBefore = g_destroyed;
        delete temp; // virtual call through the deleting-destructor slot, then operator delete
        ok = ok && g_destroyed == destroyedBefore + 1;
    }
    shared.NoteSelfCheck(ok);
}
