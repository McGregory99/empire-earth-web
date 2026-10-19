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
//   ??0GERasterizer@@IAE@ABVUString@@@Z           the engine's rasterizer base class: its protected constructor and destructor
//   ??1GERasterizer@@MAE@XZ                       (run on the first 0x68 bytes of the plugin's object), and the virtual methods
//   ?SetX@GERasterizer@@UAE...                    the plugin hands on to (GERasterizer::SetAlphaTesting(...) etc., which the
//                                                 abstract class of ge_interfaces.h declares pure, so a qualified call links to them)
//   ?TMGetTime@@YAKXZ                             the engine's millisecond clock
//   ?GetClientOrigin@GEWindow@@QAEABVGE2DPoint@@XZ the origin of a window's client area on the desktop
//   ?RoundToNearest@GERectangle@@QAEXXZ           rounds the four floats of a rectangle in place
//   ??0GETransformation@@QAE@XZ                   builds an identity transformation (16 floats)
//   ?Apply@GETransformation@@QBEXABV1@AAV1@@Z     output = this applied to input (the product of two transformations)
#ifndef EEWEB_ENGINE_IMPORTS_H
#define EEWEB_ENGINE_IMPORTS_H

#include "engine_ustring.h"
#include "ge_interfaces.h"

unsigned long ESRecordNativeError(unsigned long error);
unsigned long TMGetTime();
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
    GE2DPoint const& GetClientOrigin(); // the window's client area origin on the desktop (two floats)

private:
    char m_engineOwned[0x1c];
};

// Bare declarations of the engine entry points a class cannot name (constructors of an incomplete class, a method of a
// class the plugin does not define); the assembler label is the engine's exported decorated name.
extern "C" {
void __attribute__((thiscall)) EEWebGERasterizerConstruct(void* self, const UString& name) __asm__("??0GERasterizer@@IAE@ABVUString@@@Z");
void __attribute__((thiscall)) EEWebGERasterizerDestruct(void* self) __asm__("??1GERasterizer@@MAE@XZ");
void __attribute__((thiscall)) EEWebRoundRectangle(float* rectangle) __asm__("?RoundToNearest@GERectangle@@QAEXXZ");
void __attribute__((thiscall)) EEWebTransformationConstruct(void* self) __asm__("??0GETransformation@@QAE@XZ");
void __attribute__((thiscall)) EEWebTransformationApply(const void* self, const void* input, void* output) __asm__("?Apply@GETransformation@@QBEXABV1@AAV1@@Z");
}

#endif
