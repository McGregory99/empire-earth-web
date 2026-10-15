/*
 * Byte layouts of the engine objects the renderer plugin reads inline (task P2.5).
 *
 * The original plugin, DX7HRTnLDisplay.dll, reads and writes private data members of about a dozen
 * engine classes directly by offset instead of through virtual calls; a reimplementation that reads the
 * same offsets in memory that really holds an engine object must find the same fields there, or it reads
 * garbage silently.  This header is the contract: each struct mirrors one engine class (plain C, no
 * vptr member declared as a virtual: the vptr is an explicit field), with explicit padding, and every
 * offset and size is asserted at compile time.  `make check-layouts` also proves the offsets against
 * the real "Low-Level Engine.dll" (tests/layouts/) and src/engine_abi/LAYOUTS.md is generated from the
 * comments below by tools/re/gen_layouts_md.py.
 *
 * It states interface facts only: class and field names, offsets, sizes and types.  Names and types are
 * inferred from usage (the engine's exported accessors, constructors and setters, and the plugin).
 *
 * Reading the comments (also what the table generator parses):
 *   a comment "@class <Name> size=<bytes> size-confidence=<level> | <evidence>" above each struct
 *   a trailing comment "@<offset> <level> | <evidence>" on each field
 * Confidence levels:
 *   confirmed   the plugin and the engine (LLE) agree on the offset
 *   plugin-only only the plugin reads it (or the object is plugin-owned); the engine never names it
 *   lle-only    the engine names it (accessor, constructor, setter) but the plugin does not touch it
 *   guess       the offset is certain from some access, but the type or the meaning is inferred
 * Evidence notation: P = plugin function (DX7Rasterizer:: etc. omitted), L = engine function, "ctor" =
 * constructor.  Container members use the MSVC 6 / Dinkumware STL layout of that compiler.
 *
 * Mirror structs are named <Class>Layout so they never clash with the class declarations of
 * ge_interfaces.h.  The header is valid C (mingw oracle) and C++ (the clang plugin build); it assumes
 * a 32-bit target.
 */
#ifndef EEWEB_GE_LAYOUTS_H
#define EEWEB_GE_LAYOUTS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#define EEL_CHECK(name, cond) static_assert(cond, #name)
#else
#define EEL_CHECK(name, cond) typedef char eel_check_##name[(cond) ? 1 : -1]
#endif

/* The offset of a plain member, and the size of a whole struct. */
#define EEL_AT(type, member, offset) EEL_CHECK(type##_##member##_at_##offset, offsetof(type, member) == (offset))
#define EEL_SIZE(type, size) EEL_CHECK(type##_has_size_##size, sizeof(type) == (size))

EEL_CHECK(pointers_are_32_bit, sizeof(void *) == 4);
EEL_CHECK(float_is_32_bit, sizeof(float) == 4);

/* @class GEColor size=0xc size-confidence=confirmed | three floats; GELight.color and GEMaterial.color sit 0xc apart from their neighbours */
typedef struct GEColorLayout {
    float r; /* @0x0 confirmed | P Clear, DrawTerrainHighlight; L GEColor::GetRed */
    float g; /* @0x4 confirmed | P DrawTerrainHighlight; L GEColor::GetGreen */
    float b; /* @0x8 confirmed | P DrawTerrainHighlight; L GEColor::GetBlue, SetBlue */
} GEColorLayout;
EEL_AT(GEColorLayout, r, 0x0);
EEL_AT(GEColorLayout, g, 0x4);
EEL_AT(GEColorLayout, b, 0x8);
EEL_SIZE(GEColorLayout, 0xc);

/* @class GE2DPoint size=0x8 size-confidence=confirmed | two floats; GE2DPoint::SetXY, Clear */
typedef struct GE2DPointLayout {
    float x; /* @0x0 confirmed | P CreateAWindow; L GE2DPoint::SetXY, GetX */
    float y; /* @0x4 confirmed | P CreateAWindow; L GE2DPoint::SetXY, GetY */
} GE2DPointLayout;
EEL_AT(GE2DPointLayout, x, 0x0);
EEL_AT(GE2DPointLayout, y, 0x4);
EEL_SIZE(GE2DPointLayout, 0x8);

/* @class GE3DPoint size=0xc size-confidence=confirmed | three floats; GE3DPoint::SetXYZ, Clear */
typedef struct GE3DPointLayout {
    float x; /* @0x0 confirmed | P GetPoint; L GE3DPoint::SetXYZ, GetX */
    float y; /* @0x4 confirmed | P GetPoint; L GE3DPoint::SetXYZ, GetY */
    float z; /* @0x8 confirmed | P GetPoint; L GE3DPoint::SetXYZ, GetZ */
} GE3DPointLayout;
EEL_AT(GE3DPointLayout, x, 0x0);
EEL_AT(GE3DPointLayout, y, 0x4);
EEL_AT(GE3DPointLayout, z, 0x8);
EEL_SIZE(GE3DPointLayout, 0xc);

/* @class GEVector size=0xc size-confidence=confirmed | three floats; GEVector::GetMagnitudeSquared */
typedef struct GEVectorLayout {
    float x; /* @0x0 confirmed | P GetNormal; L GEVector::GetX */
    float y; /* @0x4 confirmed | P GetNormal; L GEVector::GetY */
    float z; /* @0x8 confirmed | P GetNormal; L GEVector::GetZ, SetZ */
} GEVectorLayout;
EEL_AT(GEVectorLayout, x, 0x0);
EEL_AT(GEVectorLayout, y, 0x4);
EEL_AT(GEVectorLayout, z, 0x8);
EEL_SIZE(GEVectorLayout, 0xc);

/* @class GERectangle size=0x10 size-confidence=confirmed | four floats; GERectangle::operator= */
typedef struct GERectangleLayout {
    float bottom; /* @0x0 confirmed | P Clear, ClearZBuffer; L GERectangle::GetBottom, GetHeight (bottom - top) */
    float left; /* @0x4 confirmed | P Clear; L GERectangle::GetLeft, GetWidth (right - left) */
    float right; /* @0x8 confirmed | P Clear; L GERectangle::GetRight */
    float top; /* @0xc confirmed | P Clear; L GERectangle::GetTop */
} GERectangleLayout;
EEL_AT(GERectangleLayout, bottom, 0x0);
EEL_AT(GERectangleLayout, left, 0x4);
EEL_AT(GERectangleLayout, right, 0x8);
EEL_AT(GERectangleLayout, top, 0xc);
EEL_SIZE(GERectangleLayout, 0x10);

/* @class GETextureCoordinate size=0x8 size-confidence=confirmed | two floats; GEBitmap and GELine hand them to the vertex arrays as two dwords */
typedef struct GETextureCoordinateLayout {
    float u; /* @0x0 confirmed | P GetTextureCoordinate; L GETextureCoordinate::GetU */
    float v; /* @0x4 confirmed | P GetTextureCoordinate; L GETextureCoordinate::GetV */
} GETextureCoordinateLayout;
EEL_AT(GETextureCoordinateLayout, u, 0x0);
EEL_AT(GETextureCoordinateLayout, v, 0x4);
EEL_SIZE(GETextureCoordinateLayout, 0x8);

/* @class GETransformation size=0x40 size-confidence=confirmed | sixteen floats; GETransformation::GETransformation, MakeIdentity write 0x0..0x3c */
typedef struct GETransformationLayout {
    float m[16]; /* @0x0 confirmed | row-major 4x4, element (row, col) at m[row*4+col], translation in m[3] m[7] m[11]; P ConvertToD3DMatrix reads all sixteen; L GETransformation::GetElement, GetPosition, Apply */
} GETransformationLayout;
EEL_AT(GETransformationLayout, m, 0x0);
EEL_SIZE(GETransformationLayout, 0x40);

/* @class GE3DPlane size=0x18 size-confidence=confirmed | a point and a normal; the six GEViewport planes sit 0x18 apart */
typedef struct GE3DPlaneLayout {
    struct GE3DPointLayout point; /* @0x0 lle-only | L GE3DPlane::GetPoint returns this */
    struct GEVectorLayout normal; /* @0xc lle-only | L GE3DPlane::GetNormal returns this + 0xc */
} GE3DPlaneLayout;
EEL_AT(GE3DPlaneLayout, point, 0x0);
EEL_AT(GE3DPlaneLayout, normal, 0xc);
EEL_SIZE(GE3DPlaneLayout, 0x18);

/* @class GEBoundingBox size=0x24 size-confidence=confirmed | two corners and three extents; GEModel.boundingBoxes has a 0x24 stride */
typedef struct GEBoundingBoxLayout {
    struct GE3DPointLayout bottomLeftFront; /* @0x0 lle-only | L GEBoundingBox::GetBottomLeftFront, SetBottomLeftFront */
    struct GE3DPointLayout topRightBack; /* @0xc lle-only | L GEBoundingBox::GetTopRightBack, SetTopRightBack */
    float width; /* @0x18 lle-only | L GEBoundingBox::GetWidth */
    float height; /* @0x1c lle-only | L GEBoundingBox::GetHeight */
    float depth; /* @0x20 lle-only | L GEBoundingBox::GetDepth */
} GEBoundingBoxLayout;
EEL_AT(GEBoundingBoxLayout, bottomLeftFront, 0x0);
EEL_AT(GEBoundingBoxLayout, topRightBack, 0xc);
EEL_AT(GEBoundingBoxLayout, width, 0x18);
EEL_AT(GEBoundingBoxLayout, height, 0x1c);
EEL_AT(GEBoundingBoxLayout, depth, 0x20);
EEL_SIZE(GEBoundingBoxLayout, 0x24);
#endif
