// EEWebRasterizer.dll: the engine-facing surface of a renderer ("rasterizer") plugin.
//
// The engine finds plugins by enumerating *.dll in the game directory (PIMModule::GetModule in
// Low-Level Engine.dll), resolves PIMGetName / PIMGetType / PIMIsValid by name, and keeps only
// the modules whose PIMGetType() is 'rast'.  GEInitialize then calls PIMIsValid(); when that is
// true it calls RPIMGetRasterizer() and registers the returned object, otherwise the module is
// unloaded.  This build is a null rasterizer: it identifies itself, and always says "not valid".
//
// All four exports are cdecl.  PIMIsValid returns a bool, which the engine tests through AL.

typedef unsigned long ulong;

extern "C" {

const void* PIMGetName(void)
{
    return 0;
}

ulong PIMGetType(void)
{
    return 0x72617374; // 'rast'
}

bool PIMIsValid(void)
{
    return false;
}

void* RPIMGetRasterizer(void)
{
    return 0;
}

// DLL entry point.  There is no CRT start-up code in this DLL (it links the import library of
// msvcrt.dll only), so nothing needs initializing here yet.
int __stdcall DllMain(void*, ulong, void*)
{
    return 1;
}

} // extern "C"
