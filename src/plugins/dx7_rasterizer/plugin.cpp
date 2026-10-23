// EEWebRasterizer.dll: the engine-facing surface of a renderer ("rasterizer") plugin.
//
// The engine finds plugins by enumerating *.dll in the game directory (PIMModule::GetModule in
// Low-Level Engine.dll), resolves PIMGetName / PIMGetType / PIMIsValid by name, and keeps only
// the modules whose PIMGetType() is 'rast'.  GEInitialize then calls PIMIsValid(); when that is
// true it calls RPIMGetRasterizer() and registers the returned object, otherwise the module is
// unloaded.  This build identifies itself and activates by creating a DX7Screen for each DirectDraw device
// with hardware T&L (task P2.6.1), so on such a machine it says "valid"; the drawing operations are still null.
//
// All four exports are cdecl.  PIMGetName returns `const UString&` (a pointer to an engine
// UString that outlives the call), PIMGetType an unsigned long, and PIMIsValid a bool, which the
// engine tests through AL.

#include "engine_ustring.h"
#include "rasterizer_variant.h"

typedef unsigned long ulong;

namespace {

// The module's display name.  The original keeps a static UString built by its CRT start-up;
// this DLL has no CRT start-up, so DllMain builds and destroys it.
alignas(4) char g_nameStorage[sizeof(UString)];

UString& Name()
{
    return *reinterpret_cast<UString*>(g_nameStorage);
}

// The one rasterizer object, built in DllMain for the same reason.  The original returns a
// static object too: the engine never deletes what RPIMGetRasterizer hands out.
alignas(4) char g_rasterizerStorage[sizeof(EEWebVariantRasterizer)];

EEWebVariantRasterizer& Rasterizer()
{
    return *reinterpret_cast<EEWebVariantRasterizer*>(g_rasterizerStorage);
}

} // namespace

extern "C" {

const void* PIMGetName(void)
{
    return &Name();
}

ulong PIMGetType(void)
{
    return 0x72617374; // 'rast'
}

// Mirrors the original: activate the rasterizer (which creates its screens), ask how many it has,
// deactivate it again (which destroys them).  Valid means at least one screen could render.
// Each call also runs the class self-check so the build keeps proving its C++ ABI.
bool PIMIsValid(void)
{
    EEWebVariantRasterizer& rasterizer = Rasterizer();
    RunClassSelfCheck(rasterizer);
    bool valid = false;
    if (rasterizer.Activate() == 0 && rasterizer.GetScreenCount() != 0) {
        valid = true;
    }
    rasterizer.Deactivate();
    return valid;
}

// Only called by the engine when PIMIsValid() is true.  The engine sees a GERasterizer.
void* RPIMGetRasterizer(void)
{
    GERasterizer* rasterizer = &Rasterizer();
    return rasterizer;
}

// DLL entry point.  The loader has already initialized Low-Level Engine.dll (our import), so its
// string constructor can run here, the same moment the original's static UString is built.
// On unload through FreeLibrary (lpReserved == 0) the name is destroyed; at process exit the
// engine's heap is going away anyway.
int __stdcall DllMain(void*, ulong reason, void* reserved)
{
    if (reason == 1) { // DLL_PROCESS_ATTACH
        new (g_nameStorage) UString(kEEWebRasterizerName);
        new (g_rasterizerStorage) EEWebVariantRasterizer;
    } else if (reason == 0 && reserved == 0) { // DLL_PROCESS_DETACH, FreeLibrary
        // The base part only: the harness may have destroyed the object through its vtable already, and only the base destructor knows that.
    static_cast<EEWebNullRasterizer&>(Rasterizer()).EEWebNullRasterizer::~EEWebNullRasterizer();
        Name().UString::~UString();
    }
    return 1;
}

} // extern "C"
