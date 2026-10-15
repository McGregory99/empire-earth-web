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

struct GEScreenLayout;
struct GESurfaceLayout;
struct GETextureManagerLayout;

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

/* @class GETStage size=0x20 size-confidence=confirmed | GETexture::GetStage returns this + 0xc; the copy constructor copies this block as eight dwords */
typedef struct GETStageLayout {
    struct GETextureLayout *texture; /* @0x0 confirmed | P SetTexture reads the stage texture; L GETexture::SetStageTexture, GetStageTexture */
    uint32_t colorMode; /* @0x4 confirmed | P SetTexture: 0..3 maps to a D3D color op; L GETexture::SetStageColorMode */
    uint32_t alphaMode; /* @0x8 confirmed | P SetTexture: 0..3 maps to a D3D alpha op; L GETexture::SetStageAlphaMode */
    uint32_t addressingMode; /* @0xc confirmed | P SetTexture; L GETexture::SetStageTextureAddressingMode */
    uint32_t minFilter; /* @0x10 confirmed | P SetTexture; L GETexture::SetStageMinFilterMode */
    uint32_t magFilter; /* @0x14 confirmed | P SetTexture; L GETexture::SetStageMagFilterMode */
    uint32_t mipFilter; /* @0x18 confirmed | P SetTexture; L GETexture::SetStageMipFilterMode */
    float mipMapBias; /* @0x1c confirmed | P SetTexture; L GETexture::SetStageMipMapBias */
} GETStageLayout;
EEL_AT(GETStageLayout, texture, 0x0);
EEL_AT(GETStageLayout, colorMode, 0x4);
EEL_AT(GETStageLayout, alphaMode, 0x8);
EEL_AT(GETStageLayout, addressingMode, 0xc);
EEL_AT(GETStageLayout, minFilter, 0x10);
EEL_AT(GETStageLayout, magFilter, 0x14);
EEL_AT(GETStageLayout, mipFilter, 0x18);
EEL_AT(GETStageLayout, mipMapBias, 0x1c);
EEL_SIZE(GETStageLayout, 0x20);

/* @class GETexture size=0x6c size-confidence=confirmed | operator new(0x6c) in GETextureManager::LoadTexture and MakeTexture; the copy constructor ends at 0x6b */
typedef struct GETextureLayout {
    void * vptr; /* @0x0 lle-only | L ctor sets the GETexture vftable (UReferenceCounter derived) */
    uint32_t refCount; /* @0x4 confirmed | P SetTexture increments it; L GETexture::Dereference, ctor starts it at 1 */
    uint8_t refFlag; /* @0x8 lle-only | L ctor clears it; UReferenceCounter flag byte */
    uint8_t pad_0x9[0x3];
    struct GETStageLayout stage; /* @0xc confirmed | P SetTexture reads the eight stage words; L GETexture::GetStage */
    struct GETextureCoordinateLayout frameTopLeft; /* @0x2c confirmed | P Draw(GEBitmap) reads u,v; L GETexture::GetFrameTopLeftCorner */
    struct GEScreenLayout *screen; /* @0x34 lle-only | L GETexture::GetScreen, Recreate */
    struct GESurfaceLayout *surface; /* @0x38 confirmed | P SetTexture reaches the DirectDraw surface through it; L GETexture::GetSurface, GetWidth */
    struct GETextureLayout *baseTexture; /* @0x3c confirmed | P SetTexture follows stage.texture -> baseTexture -> surface; L GETexture::GetData, GetFrameSurface, GetIsCompressed; ctor points it at the texture itself */
    struct GETextureLayout **frames; /* @0x40 lle-only | L GETexture::GetFrameTexture */
    struct GETextureManagerLayout *manager; /* @0x44 lle-only | L GETexture::Promote, Dereference */
    void * userData; /* @0x48 lle-only | L GETexture::GetData, SetData (through baseTexture) */
    float frameUVSize; /* @0x4c confirmed | P Draw(GEBitmap) scales the corners by it; L GETexture::GetFrameUVSize */
    uint32_t frameFormat; /* @0x50 lle-only | L GETexture::GetFrameFormat; 0 means one texture per frame, else a single frame */
    uint32_t frame; /* @0x54 lle-only | L GETexture::GetFrame, SetFrame */
    uint32_t frameCount; /* @0x58 lle-only | L GETexture::GetFrameCount, GetAllocatedSize */
    uint32_t framesPerRow; /* @0x5c guess | L GETexture::Load multiplies the frame width by it; meaning inferred */
    uint32_t mipLevelCount; /* @0x60 lle-only | L GETexture::GetMipLevelCount */
    uint32_t numMipLevelsToDrop; /* @0x64 lle-only | L GETexture::GetNumMipLevelsToDrop */
    uint8_t isCompressed; /* @0x68 lle-only | L GETexture::GetIsCompressed (read through baseTexture) */
    uint8_t isPromoted; /* @0x69 lle-only | L GETexture::GetIsPromoted, SetIsPromoted */
    uint8_t hasChanged; /* @0x6a confirmed | P SetTexture tests it and clears it; L GETexture::HasChanged, SetHasChanged, ValidateChanges */
    uint8_t createMIPMaps; /* @0x6b lle-only | L GETexture::GetCreateMIPMaps */
} GETextureLayout;
EEL_AT(GETextureLayout, vptr, 0x0);
EEL_AT(GETextureLayout, refCount, 0x4);
EEL_AT(GETextureLayout, refFlag, 0x8);
EEL_AT(GETextureLayout, stage, 0xc);
EEL_AT(GETextureLayout, frameTopLeft, 0x2c);
EEL_AT(GETextureLayout, screen, 0x34);
EEL_AT(GETextureLayout, surface, 0x38);
EEL_AT(GETextureLayout, baseTexture, 0x3c);
EEL_AT(GETextureLayout, frames, 0x40);
EEL_AT(GETextureLayout, manager, 0x44);
EEL_AT(GETextureLayout, userData, 0x48);
EEL_AT(GETextureLayout, frameUVSize, 0x4c);
EEL_AT(GETextureLayout, frameFormat, 0x50);
EEL_AT(GETextureLayout, frame, 0x54);
EEL_AT(GETextureLayout, frameCount, 0x58);
EEL_AT(GETextureLayout, framesPerRow, 0x5c);
EEL_AT(GETextureLayout, mipLevelCount, 0x60);
EEL_AT(GETextureLayout, numMipLevelsToDrop, 0x64);
EEL_AT(GETextureLayout, isCompressed, 0x68);
EEL_AT(GETextureLayout, isPromoted, 0x69);
EEL_AT(GETextureLayout, hasChanged, 0x6a);
EEL_AT(GETextureLayout, createMIPMaps, 0x6b);
EEL_SIZE(GETextureLayout, 0x6c);

/* @class GEMaterial size=0x48 size-confidence=confirmed | operator new(0x48) in GEModel::AllocateInternalData, ConvertLegacyModel, ComputeLODReduction */
typedef struct GEMaterialLayout {
    void * vptr; /* @0x0 lle-only | L ctor sets the GEMaterial vftable (UReferenceCounter derived) */
    uint32_t refCount; /* @0x4 lle-only | L ctor starts it at 1 */
    uint8_t name[16]; /* @0x8 lle-only | a UString (16 bytes); L GEMaterial::GetName, SetName */
    struct GEColorLayout color; /* @0x18 confirmed | P DrawModelHierarchy reads r,g,b into the material; L GEMaterial::GetColor, SetColor */
    struct GETextureLayout **textures; /* @0x24 confirmed | P DrawModelHierarchy indexes it by screen; L GEMaterial::GetTexture, SetTexture */
    uint32_t textureCount; /* @0x28 lle-only | L GEMaterial ctor (first argument), ~GEMaterial */
    uint32_t *polygonStart; /* @0x2c confirmed | P DrawModelHierarchy indexes it by level of detail; L GEMaterial::GetPolygonStartingIndex */
    uint32_t *polygonLength; /* @0x30 confirmed | P DrawModelHierarchy indexes it by level of detail; L GEMaterial::GetPolygonRangeLength */
    uint32_t vertexStart; /* @0x34 confirmed | P CopyStdVertices and variants; L GEMaterial::GetVertexStartingIndex */
    uint32_t vertexLength; /* @0x38 confirmed | P DrawModelHierarchy (vertex count to lock), CopyStdVertices; L GEMaterial::GetVertexRangeLength */
    uint32_t textureIndex; /* @0x3c lle-only | L GEMaterial::GetTextureIndex, SetTextureIndex */
    uint32_t lodCount; /* @0x40 lle-only | L GEMaterial::GetLODCount, ctor (second argument) */
    float opacity; /* @0x44 confirmed | P DrawModelHierarchy multiplies the model opacity by it; L GEMaterial::GetOpacity, SetOpacity */
} GEMaterialLayout;
EEL_AT(GEMaterialLayout, vptr, 0x0);
EEL_AT(GEMaterialLayout, refCount, 0x4);
EEL_AT(GEMaterialLayout, name, 0x8);
EEL_AT(GEMaterialLayout, color, 0x18);
EEL_AT(GEMaterialLayout, textures, 0x24);
EEL_AT(GEMaterialLayout, textureCount, 0x28);
EEL_AT(GEMaterialLayout, polygonStart, 0x2c);
EEL_AT(GEMaterialLayout, polygonLength, 0x30);
EEL_AT(GEMaterialLayout, vertexStart, 0x34);
EEL_AT(GEMaterialLayout, vertexLength, 0x38);
EEL_AT(GEMaterialLayout, textureIndex, 0x3c);
EEL_AT(GEMaterialLayout, lodCount, 0x40);
EEL_AT(GEMaterialLayout, opacity, 0x44);
EEL_SIZE(GEMaterialLayout, 0x48);

/* @class GELight size=0x74 size-confidence=confirmed | copy constructor and operator= copy 0x0..0x73; the class has no vptr */
typedef struct GELightLayout {
    struct GEColorLayout color; /* @0x0 lle-only | L GELight::GetColor returns this, SetColor normalizes it */
    struct GEColorLayout computedColor; /* @0xc confirmed | P UpdateLight reads it as the D3D light color; L GELight::GetComputedColor (given color * intensity) */
    struct GE3DPointLayout location; /* @0x18 confirmed | P UpdateLight reads the position; L GELight::GetLocation, SetLocation */
    struct GE3DPointLayout modelSpaceLocation; /* @0x24 lle-only | L GELight::GetModelSpaceLocation */
    struct GEVectorLayout direction; /* @0x30 confirmed | P UpdateLight negates it into the D3D direction; L GELight::GetDirection; SetDirection stores the negated, normalized argument */
    struct GEVectorLayout modelSpaceDirection; /* @0x3c lle-only | L GELight::GetModelSpaceDirection */
    float attenuation0; /* @0x48 confirmed | P UpdateLight; L GELight::GetAttenuation0 */
    float attenuation1; /* @0x4c confirmed | P UpdateLight; L GELight::GetAttenuation1, SetAttenuation1 */
    float attenuation2; /* @0x50 confirmed | P UpdateLight; L GELight::GetAttenuation2, SetAttenuation2 */
    float intensity; /* @0x54 lle-only | L GELight::GetIntensity, SetIntensity */
    float range; /* @0x58 confirmed | P UpdateLight; L GELight::GetRange, SetRange */
    float rangeInverse; /* @0x5c lle-only | L GELight::GetRangeInverse */
    float rangeSquared; /* @0x60 lle-only | L GELight::GetRangeSquared */
    float modelSpaceRange; /* @0x64 lle-only | L GELight::GetModelSpaceRange */
    float modelSpaceRangeInverse; /* @0x68 lle-only | L GELight::GetModelSpaceRangeInverse */
    float modelSpaceRangeSquared; /* @0x6c lle-only | L GELight::GetModelSpaceRangeSquared */
    uint32_t style; /* @0x70 confirmed | P UpdateLight: non-zero selects D3DLIGHT_DIRECTIONAL (3), zero D3DLIGHT_POINT (1); L GELight::GetStyle, SetStyle */
} GELightLayout;
EEL_AT(GELightLayout, color, 0x0);
EEL_AT(GELightLayout, computedColor, 0xc);
EEL_AT(GELightLayout, location, 0x18);
EEL_AT(GELightLayout, modelSpaceLocation, 0x24);
EEL_AT(GELightLayout, direction, 0x30);
EEL_AT(GELightLayout, modelSpaceDirection, 0x3c);
EEL_AT(GELightLayout, attenuation0, 0x48);
EEL_AT(GELightLayout, attenuation1, 0x4c);
EEL_AT(GELightLayout, attenuation2, 0x50);
EEL_AT(GELightLayout, intensity, 0x54);
EEL_AT(GELightLayout, range, 0x58);
EEL_AT(GELightLayout, rangeInverse, 0x5c);
EEL_AT(GELightLayout, rangeSquared, 0x60);
EEL_AT(GELightLayout, modelSpaceRange, 0x64);
EEL_AT(GELightLayout, modelSpaceRangeInverse, 0x68);
EEL_AT(GELightLayout, modelSpaceRangeSquared, 0x6c);
EEL_AT(GELightLayout, style, 0x70);
EEL_SIZE(GELightLayout, 0x74);
#endif
