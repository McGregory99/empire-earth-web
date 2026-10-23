#include "null_rasterizer.h"

#include "engine_imports.h"
#include "rasterizer_abi.h"
#include "rasterizer_variant.h"

unsigned long ESRecordNativeError(unsigned long error); // ?ESRecordNativeError@@YAKK@Z

namespace {

// The rasterizer that is enumerating devices (the original's DAT_10017d28); the enumeration callback has no other way to reach it.
EEWebNullRasterizer* g_activating;

const GUID_ kIidDirectDraw7 = {0x15e65ec0, 0x3b9c, 0x11d2, {0xb9, 0x2f, 0x00, 0x60, 0x97, 0x97, 0xea, 0x5b}};
const GUID_ kIidDirect3D7 = {0xf5049e77, 0x4861, 0x11d2, {0xa4, 0x07, 0x00, 0xa0, 0xc9, 0x06, 0x29, 0xa8}};

long g_destroyed; // destructor runs of any EEWebNullRasterizer in this DLL (read by the self-check only)

DX7RasterizerAbi* MakeNullRasterizer()
{
    return new EEWebVariantRasterizer;
}

// Called through a volatile pointer so the optimizer sees an unknown dynamic type.
DX7RasterizerAbi* (*volatile g_factory)() = &MakeNullRasterizer;

} // namespace

EEWebNullRasterizer::EEWebNullRasterizer()
{
    // The harness reads the counters at this offset, behind the original's DX7Rasterizer fields.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
    static_assert(__builtin_offsetof(EEWebNullRasterizer, m_engineFields) == 4, "the engine's fields start behind the vptr");
    static_assert(__builtin_offsetof(EEWebNullRasterizer, m_counters) == EEWEB_COUNTERS_OFFSET,
                  "the counters must sit right behind the fields of DX7Rasterizer");
    static_assert(EEWEB_COUNTERS_OFFSET == sizeof(DX7RasterizerLayout), "... which end at the size of DX7RasterizerLayout");
#pragma clang diagnostic pop
    for (int k = 0; k < EEWEB_COUNTER_COUNT; ++k) {
        m_counters[k] = 0;
    }
    // The original builds the engine's base class with the rasterizer's name, then its own fields.  The engine's
    // constructor sets the vptr of GERasterizer: put ours back.
    void* const vptr = *reinterpret_cast<void* volatile*>(this);
    UString name(kEEWebRasterizerName);
    EEWebGERasterizerConstruct(this, name);
    *reinterpret_cast<void* volatile*>(this) = vptr;
    m_engineBaseAlive = 1;
    DX7RasterizerLayout* own = Own();
    own->screenRecords.allocator = 0;
    own->screenRecords.first = 0;
    own->screenRecords.last = 0;
    own->screenRecords.end = 0;
    own->debugColor = 0xff000040;
}

EEWebNullRasterizer::~EEWebNullRasterizer()
{
    ::operator delete(Own()->screenRecords.first); // the records and screens themselves belong to Deactivate
    Own()->screenRecords.first = Own()->screenRecords.last = Own()->screenRecords.end = 0;
    // The engine's destructor frees what its constructor made, once: the harness destroys the shared object through the
    // vtable and the DLL's unload destroys it again.
    if (m_engineBaseAlive) {
        m_engineBaseAlive = 0;
        EEWebGERasterizerDestruct(this);
    }
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
    return const_cast<EEWebNullRasterizer*>(this)->RecordCount();
}

// ---- the lists the original keeps: the plugin's screen records (0x68) and the engine's screen states (0x1c) ----

unsigned long EEWebNullRasterizer::RecordCount()
{
    const StdVectorLayout& records = Own()->screenRecords;
    return records.first ? static_cast<unsigned long>((static_cast<char*>(records.last) - static_cast<char*>(records.first)) / sizeof(void*)) : 0;
}

DX7ScreenRecordLayout* EEWebNullRasterizer::Record(unsigned long screen)
{
    return screen < RecordCount() ? static_cast<DX7ScreenRecordLayout**>(Own()->screenRecords.first)[screen] : 0;
}

GERScreenStateLayout* EEWebNullRasterizer::State(unsigned long screen)
{
    const StdVectorLayout& states = Base()->screens;
    if (!states.first || screen >= static_cast<unsigned long>((static_cast<char*>(states.last) - static_cast<char*>(states.first)) / sizeof(void*))) {
        return 0;
    }
    return static_cast<GERScreenStateLayout**>(states.first)[screen];
}

IDirect3DDevice7* EEWebNullRasterizer::Device(unsigned long screen)
{
    DX7ScreenRecordLayout* record = Record(screen);
    return record ? static_cast<IDirect3DDevice7*>(record->device) : 0;
}

DX7Screen* EEWebNullRasterizer::ScreenOf(unsigned long screen)
{
    DX7ScreenRecordLayout* record = Record(screen);
    return record ? reinterpret_cast<DX7Screen*>(record->screen) : 0;
}

// std::vector<DX7ScreenRecord*>::push_back on the MSVC 6 layout: grows the array by doubling.
void EEWebNullRasterizer::AddRecord(DX7ScreenRecordLayout* record)
{
    StdVectorLayout& records = Own()->screenRecords;
    const unsigned long count = RecordCount();
    const unsigned long capacity = records.first ? static_cast<unsigned long>((static_cast<char*>(records.end) - static_cast<char*>(records.first)) / sizeof(void*)) : 0;
    if (count == capacity) {
        const unsigned long grown = capacity ? capacity * 2 : 4;
        DX7ScreenRecordLayout** array = static_cast<DX7ScreenRecordLayout**>(::operator new(grown * sizeof(void*)));
        for (unsigned long i = 0; i < count; ++i) {
            array[i] = static_cast<DX7ScreenRecordLayout**>(records.first)[i];
        }
        ::operator delete(records.first);
        records.first = array;
        records.end = array + grown;
    }
    static_cast<DX7ScreenRecordLayout**>(records.first)[count] = record;
    records.last = static_cast<DX7ScreenRecordLayout**>(records.first) + count + 1;
}

void EEWebNullRasterizer::NoteSelfCheck(bool passed)
{
    ++m_counters[EEWEB_COUNTER_SELF_CHECKS];
    if (!passed) {
        ++m_counters[EEWEB_COUNTER_SELF_CHECK_FAILURES];
    }
}

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
    // The original allocates a 0x74-byte record per screen and keeps only the screen, a null device and the scene flag
    // in it; the other fields are the screen's activation to set (and are zero here).
    DX7ScreenRecordLayout* record = static_cast<DX7ScreenRecordLayout*>(::operator new(sizeof(DX7ScreenRecordLayout)));
    if (record) {
        __builtin_memset(record, 0, sizeof *record);
        record->screen = reinterpret_cast<GEScreenLayout*>(screen);
        record->sceneDrawn = 1;
        AddRecord(record);
    }
    m_counters[EEWEB_COUNTER_SCREENS] = static_cast<long>(RecordCount());
    return GERasterizer::AddValidScreen(screen); // the engine's record of the screen (GERScreenState)
}

// A screen is valid when its Direct3D 7 offers a device the callback accepts: for the rasterizer without T&L a hardware-rasterizing device
// (D3DDEVCAPS_HWRASTERIZATION), for the T&L one also hardware transform and lighting (tnl_rasterizer.cpp).  The callback answers 0 to stop
// the enumeration at the first device it likes.
namespace {

long __stdcall FindHardwareDevice(char*, char*, void* deviceDesc7, void* found)
{
    const unsigned long devCaps = *static_cast<unsigned long*>(deviceDesc7);
    if ((devCaps & D3DDEVCAPS_HWRASTERIZATION) == D3DDEVCAPS_HWRASTERIZATION) {
        *static_cast<bool*>(found) = true;
        return 0;
    }
    return 1;
}

} // namespace

bool EEWebNullRasterizer::GetIsScreenValid(DX7Screen* screen)
{
    return HasDevice(screen, &FindHardwareDevice);
}

bool EEWebNullRasterizer::HasDevice(DX7Screen* screen, EnumDevicesCallback callback)
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
    error = direct3d->vtbl->EnumDevices(direct3d, callback, &found);
    if (error != 0) {
        ESRecordNativeError(static_cast<unsigned long>(error));
        found = false;
    }
    direct3d->vtbl->Release(direct3d);
    return found;
}

// What the original's Deactivate does for each screen record, in its order: unbind the texture, release the vertex buffer, take
// the lights off the device and release it, release Direct3D, delete the screen (which releases its DirectDraw object and
// unregisters it) and the record.  Whatever the activation did not create is skipped.
void EEWebNullRasterizer::ReleaseDeviceObjects(DX7ScreenRecordLayout* record, unsigned long index)
{
    SetTexture(static_cast<GETexture*>(0), index);
    if (record->vertexBuffer) {
        IDirect3DVertexBuffer7* buffer = static_cast<IDirect3DVertexBuffer7*>(record->vertexBuffer);
        buffer->vtbl->Release(buffer);
        record->vertexBuffer = 0;
    }
    if (record->device) {
        RemoveAllLights(index);
        IDirect3DDevice7* device = static_cast<IDirect3DDevice7*>(record->device);
        device->vtbl->Release(device);
        record->device = 0;
    }
    if (record->direct3d) {
        IDirect3D7* direct3d = static_cast<IDirect3D7*>(record->direct3d);
        direct3d->vtbl->Release(direct3d);
        record->direct3d = 0;
    }
}

void EEWebNullRasterizer::ClearScreens()
{
    StdVectorLayout& records = Own()->screenRecords;
    for (unsigned long i = 0; i < RecordCount(); ++i) {
        DX7ScreenRecordLayout* record = static_cast<DX7ScreenRecordLayout**>(records.first)[i];
        ReleaseDeviceObjects(record, i);
        delete reinterpret_cast<DX7Screen*>(record->screen);
        ::operator delete(record);
    }
    records.last = records.first;
    m_counters[EEWEB_COUNTER_SCREENS] = 0;
}

long EEWebNullRasterizer::Deactivate()
{
    ++m_counters[EEWEB_COUNTER_DEACTIVATIONS];
    ClearScreens();
    return GERasterizer::Deactivate(); // the engine forgets its screen states
}

// ---- video memory of a screen's DirectDraw device ------------------------------------------------------------

long EEWebNullRasterizer::GetVideoMemory(unsigned long screen, unsigned long& total, unsigned long& available)
{
    DDSCAPS2_ caps = {DDSCAPS_VIDEOMEMORY, 0, 0, 0};
    unsigned long totalMemory = 0;
    unsigned long freeMemory = 0;
    DX7Screen* owner = ScreenOf(screen);
    if (owner == 0) {
        return 0xd;
    }
    IDirectDraw7* directDraw = owner->GetDirectDraw();
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
