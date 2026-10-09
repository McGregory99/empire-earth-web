// EEWebRasterizer.dll: the engine-facing surface of a renderer ("rasterizer") plugin.
//
// The engine finds plugins by enumerating *.dll in the game directory (PIMModule::GetModule in
// Low-Level Engine.dll), resolves PIMGetName / PIMGetType / PIMIsValid by name, and keeps only
// the modules whose PIMGetType() is 'rast'.  GEInitialize then calls PIMIsValid(); when that is
// true it calls RPIMGetRasterizer() and registers the returned object, otherwise the module is
// unloaded.  This build is a null rasterizer: it identifies itself, and always says "not valid".
//
// All four exports are cdecl.  PIMGetName returns `const UString&` (a pointer to an engine
// UString that outlives the call), PIMGetType an unsigned long, and PIMIsValid a bool, which the
// engine tests through AL.

#include "engine_ustring.h"
#include "null_rasterizer.h"

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
alignas(4) char g_rasterizerStorage[sizeof(EEWebNullRasterizer)];

EEWebRasterizerBase& Rasterizer()
{
    return *reinterpret_cast<EEWebRasterizerBase*>(g_rasterizerStorage);
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

// Mirrors the original: activate the rasterizer, ask for its screens, deactivate it again.  A
// null rasterizer cannot activate, so it is never valid and the engine unloads the module.
// Each call also runs the class self-check so the build keeps proving its C++ ABI.
bool PIMIsValid(void)
{
    EEWebRasterizerBase& rasterizer = Rasterizer();
    RunClassSelfCheck(rasterizer);
    bool valid = false;
    if (rasterizer.Activate() == 0 && rasterizer.GetScreenCount() != 0) {
        valid = true;
    }
    rasterizer.Deactivate();
    return valid;
}

// Only called by the engine when PIMIsValid() is true, so never for this build.
void* RPIMGetRasterizer(void)
{
    return &Rasterizer();
}

// DLL entry point.  The loader has already initialized Low-Level Engine.dll (our import), so its
// string constructor can run here, the same moment the original's static UString is built.
// On unload through FreeLibrary (lpReserved == 0) the name is destroyed; at process exit the
// engine's heap is going away anyway.
int __stdcall DllMain(void*, ulong reason, void* reserved)
{
    if (reason == 1) { // DLL_PROCESS_ATTACH
        new (g_nameStorage) UString("EEWeb Null Rasterizer");
        new (g_rasterizerStorage) EEWebNullRasterizer;
    } else if (reason == 0 && reserved == 0) { // DLL_PROCESS_DETACH, FreeLibrary
        reinterpret_cast<EEWebNullRasterizer*>(g_rasterizerStorage)->EEWebNullRasterizer::~EEWebNullRasterizer();
        Name().UString::~UString();
    }
    return 1;
}

} // extern "C"
