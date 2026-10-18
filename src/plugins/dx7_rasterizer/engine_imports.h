// The parts of "Low-Level Engine.dll" that the surfaces and the 32-bit probe call (imports: lle_imports.def),
// declared so that the compiler emits the engine's own MSVC-decorated names:
//
//   ?ESRecordNativeError@@YAKK@Z                  the engine's error log (every failed DirectDraw call lands there)
//   ?GEPackColor@@YAKKABVGEColor@@M@Z             a colour (and alpha) packed for pixel format index `format`
//   ?GEPackColor@@YAKKKKKK@Z                      four channels (0..255) packed for pixel format index `format`
//   ?GEUnpackRed/Green/Blue/Alpha@@YAEKK@Z        one channel of a pixel of pixel format index `format`
//   ?Dereference@UReferenceCounter@@QAEXXZ        drops one reference and deletes the object (through its virtual
//                                                 destructor) at zero: the way the engine lets go of a surface
//   ??1GEWindow@@QAE@XZ                           the destructor of the engine's window object
//   ?sInstance@GEScreen@@0PAUHINSTANCE__@@A       the HINSTANCE of the executable, a private static data member of GEScreen:
//                                                 looked up at run time (dx7_screen.cpp), a class cannot name it
#ifndef EEWEB_ENGINE_IMPORTS_H
#define EEWEB_ENGINE_IMPORTS_H

#include "ge_interfaces.h"

unsigned long ESRecordNativeError(unsigned long error);
unsigned long GEPackColor(unsigned long format, GEColor const& color, float alpha);
unsigned long GEPackColor(unsigned long format, unsigned long red, unsigned long green, unsigned long blue, unsigned long alpha);
unsigned char GEUnpackRed(unsigned long format, unsigned long pixel);
unsigned char GEUnpackGreen(unsigned long format, unsigned long pixel);
unsigned char GEUnpackBlue(unsigned long format, unsigned long pixel);
unsigned char GEUnpackAlpha(unsigned long format, unsigned long pixel);

// Only the members the plugin calls.  The engine's surfaces derive from it (the reference count sits at 0x4);
// DX7Surface does not model that base, it keeps the field where the engine expects it (GESurfaceLayout.refCount).
class UReferenceCounter {
public:
    void Dereference();
};

// The engine's window object: 0x1c bytes, built by DX7Screen::CreateAWindow field by field (GEWindowLayout) and
// destroyed with the engine's own destructor.
class GEWindow {
public:
    ~GEWindow();

private:
    char m_engineOwned[0x1c];
};

#endif
