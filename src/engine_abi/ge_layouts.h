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

/* @class UString size=0x10 size-confidence=confirmed | Phase 1 (TCP plugin) layout, low_level_engine.h; GEMaterial, GEModel and GEFont embed one every 0x10 bytes */
typedef struct UStringLayout {
    void * vptr; /* @0x0 confirmed | L UString constructors; virtual destructor */
    uint8_t *chars; /* @0x4 confirmed | P Draw(GEText) reads the text through the UWideString twin; Phase 1 plugin reads it inline */
    uint32_t length; /* @0x8 confirmed | characters without the terminator; Phase 1 plugin */
    uint32_t capacity; /* @0xc confirmed | L GEModel::SizeOf reads it at GEModel+0xe0 */
} UStringLayout;
EEL_AT(UStringLayout, vptr, 0x0);
EEL_AT(UStringLayout, chars, 0x4);
EEL_AT(UStringLayout, length, 0x8);
EEL_AT(UStringLayout, capacity, 0xc);
EEL_SIZE(UStringLayout, 0x10);

/* @class UWideString size=0x14 size-confidence=confirmed | Phase 1 layout (single-byte characters despite the name); GEText.text spans 0x30..0x43 */
typedef struct UWideStringLayout {
    void * vptr; /* @0x0 confirmed | L UWideString constructors */
    uint8_t *chars; /* @0x4 confirmed | P Draw(GEText) passes it to ExtTextOutA and GetTextExtentPoint32A */
    uint32_t length; /* @0x8 confirmed | P Draw(GEText) passes it as the ExtTextOutA count */
    uint32_t capacity; /* @0xc confirmed | P Draw(GEText) passes it as the GetTextExtentPoint32A count (an original quirk) */
    uint32_t reserved; /* @0x10 plugin-only | Phase 1 layout; unused */
} UWideStringLayout;
EEL_AT(UWideStringLayout, vptr, 0x0);
EEL_AT(UWideStringLayout, chars, 0x4);
EEL_AT(UWideStringLayout, length, 0x8);
EEL_AT(UWideStringLayout, capacity, 0xc);
EEL_AT(UWideStringLayout, reserved, 0x10);
EEL_SIZE(UWideStringLayout, 0x14);

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
    struct UStringLayout name; /* @0x8 lle-only | L GEMaterial::GetName, SetName */
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

/* @class GEModel size=0x188 size-confidence=confirmed | operator new(0x188) in GEModel::LoadCurrentVersion, LoadLegacyVersion, GEModelManager::LoadModel */
typedef struct GEModelLayout {
    void * vptr; /* @0x0 lle-only | L ctor sets the GEModel vftable (UReferenceCounter derived) */
    uint32_t refCount; /* @0x4 lle-only | L ctor starts it at 1; GEModel::Dereference */
    struct GETransformationLayout preModelTransform; /* @0x8 lle-only | L GEModel::GetGraphicsPreModelTransform */
    struct GETransformationLayout postModelTransform; /* @0x48 lle-only | L GEModel::GetGraphicsPostModelTransform */
    struct GETransformationLayout graphicsTransform; /* @0x88 confirmed | P DrawModelHierarchy applies it to the stack and the camera; L GEModel::GetGraphicsTransform */
    struct GE3DPointLayout centerPoint; /* @0xc8 lle-only | L GEModel::GetCenterPoint */
    struct UStringLayout name; /* @0xd4 lle-only | L GEModel::GetName */
    uint8_t flagE4; /* @0xe4 guess | L ctor sets it; meaning unknown */
    uint8_t flagE5; /* @0xe5 guess | L ctor sets it; meaning unknown */
    uint8_t pad_0xe6[0x2];
    uint32_t unknownE8; /* @0xe8 guess | L ConvertLegacyModel reads it */
    uint32_t unknownEC; /* @0xec guess | L GEModel::SizeOf subtracts it from frameCount */
    struct GETransformationLayout *internalTransforms; /* @0xf0 lle-only | L GEModel::GetInternalTransform (stride 0x40), GetGraphicsInternalTransform */
    void * manager; /* @0xf4 lle-only | a GEModelManager; L ctor, Dereference */
    struct GEModelLayout *parentModel; /* @0xf8 lle-only | L GEModel::GetParentModel, SetParentModel */
    struct GEModelLayout **subModels; /* @0xfc confirmed | P DrawModelHierarchy recurses over it; L GEModel::GetSubModel */
    struct GEPolygonArrayLayout **polygonArrays; /* @0x100 confirmed | P DrawModelHierarchy indexes it by level of detail; L GEModel::GetPolygonArray */
    struct GEVertexArrayLayout **vertexArrays; /* @0x104 confirmed | P DrawModelHierarchy and CopyStdVertices call through its first entry; L GEModel::GetVertexArray */
    struct GEMaterialLayout **materials; /* @0x108 confirmed | P DrawModelHierarchy iterates it; L GEModel::GetMaterial */
    struct GE3DPointLayout **tagPointLists; /* @0x10c lle-only | L GEModel::GetTagPoint(index, frame) */
    struct GEBoundingBoxLayout *boundingBoxes; /* @0x110 lle-only | L GEModel::GetBoundingBox (stride 0x24) */
    struct GEBoundingBoxLayout *graphicsBoundingBox; /* @0x114 lle-only | L GEModel::GetGraphicsBoundingBox, SetGraphicsCurrentFrame */
    struct GE3DPointLayout *tagPoints; /* @0x118 lle-only | L GEModel::GetTagPoint, GetTagPointList */
    void * textureFilenames; /* @0x11c lle-only | an array of UString; L GEModel::SetTextureFilename */
    struct UStringLayout *tagPointNames; /* @0x120 lle-only | L GEModel::GetTagPointName (stride 0x10) */
    float *boundingRadii; /* @0x124 lle-only | L GEModel::GetBoundingRadius */
    float graphicsScale; /* @0x128 confirmed | P DrawModelHierarchy scales the radius by it; L GEModel::GetGraphicsScale */
    float opacity; /* @0x12c confirmed | P DrawModelHierarchy multiplies the material opacity by it; L GEModel::GetModelOpacity */
    float fractionalFramePosition; /* @0x130 confirmed | P CopyAndSmoothStdVertices; L GEModel::GetFractionalFramePosition */
    uint32_t currentFrame; /* @0x134 confirmed | P CopyAndSmoothStdVertices; L GEModel::GetGraphicsCurrentFrame */
    uint32_t nextFrame; /* @0x138 confirmed | P CopyAndSmoothStdVertices; L GEModel::GetGraphicsNextFrame */
    uint32_t subModelCount; /* @0x13c confirmed | P DrawModelHierarchy; L GEModel::GetSubModelCount */
    uint32_t frameCount; /* @0x140 lle-only | L GEModel::GetFrameCount */
    uint32_t materialCount; /* @0x144 confirmed | P DrawModelHierarchy; L GEModel::GetMaterialCount */
    uint32_t polygonCount; /* @0x148 lle-only | L GEModel::GetPolygonCount */
    uint32_t vertexCount; /* @0x14c lle-only | L ctor argument */
    uint32_t tagPointCount; /* @0x150 lle-only | L GEModel::GetTagPointCount */
    uint32_t lodCount; /* @0x154 confirmed | P DrawModelHierarchy clamps the level of detail with it; L GEModel::GetLODCount */
    uint8_t clipToPlane[6]; /* @0x158 lle-only | L GEModel::GetClipToPlane, SetClipToPlane (one byte per frustum plane) */
    uint8_t pad_0x15e[0x2];
    uint32_t legacyLoadState[10]; /* @0x160 guess | L ConvertLegacyModel and the legacy chunk loaders use these dwords */
} GEModelLayout;
EEL_AT(GEModelLayout, vptr, 0x0);
EEL_AT(GEModelLayout, refCount, 0x4);
EEL_AT(GEModelLayout, preModelTransform, 0x8);
EEL_AT(GEModelLayout, postModelTransform, 0x48);
EEL_AT(GEModelLayout, graphicsTransform, 0x88);
EEL_AT(GEModelLayout, centerPoint, 0xc8);
EEL_AT(GEModelLayout, name, 0xd4);
EEL_AT(GEModelLayout, flagE4, 0xe4);
EEL_AT(GEModelLayout, flagE5, 0xe5);
EEL_AT(GEModelLayout, unknownE8, 0xe8);
EEL_AT(GEModelLayout, unknownEC, 0xec);
EEL_AT(GEModelLayout, internalTransforms, 0xf0);
EEL_AT(GEModelLayout, manager, 0xf4);
EEL_AT(GEModelLayout, parentModel, 0xf8);
EEL_AT(GEModelLayout, subModels, 0xfc);
EEL_AT(GEModelLayout, polygonArrays, 0x100);
EEL_AT(GEModelLayout, vertexArrays, 0x104);
EEL_AT(GEModelLayout, materials, 0x108);
EEL_AT(GEModelLayout, tagPointLists, 0x10c);
EEL_AT(GEModelLayout, boundingBoxes, 0x110);
EEL_AT(GEModelLayout, graphicsBoundingBox, 0x114);
EEL_AT(GEModelLayout, tagPoints, 0x118);
EEL_AT(GEModelLayout, textureFilenames, 0x11c);
EEL_AT(GEModelLayout, tagPointNames, 0x120);
EEL_AT(GEModelLayout, boundingRadii, 0x124);
EEL_AT(GEModelLayout, graphicsScale, 0x128);
EEL_AT(GEModelLayout, opacity, 0x12c);
EEL_AT(GEModelLayout, fractionalFramePosition, 0x130);
EEL_AT(GEModelLayout, currentFrame, 0x134);
EEL_AT(GEModelLayout, nextFrame, 0x138);
EEL_AT(GEModelLayout, subModelCount, 0x13c);
EEL_AT(GEModelLayout, frameCount, 0x140);
EEL_AT(GEModelLayout, materialCount, 0x144);
EEL_AT(GEModelLayout, polygonCount, 0x148);
EEL_AT(GEModelLayout, vertexCount, 0x14c);
EEL_AT(GEModelLayout, tagPointCount, 0x150);
EEL_AT(GEModelLayout, lodCount, 0x154);
EEL_AT(GEModelLayout, clipToPlane, 0x158);
EEL_AT(GEModelLayout, legacyLoadState, 0x160);
EEL_SIZE(GEModelLayout, 0x188);

/* @class GEViewport size=0x288 size-confidence=confirmed | copy constructor and operator= copy 0x0..0x287; GERasterizer screen records derive from it (their own fields start at 0x288); the class has no vptr */
typedef struct GEViewportLayout {
    struct GERectangleLayout orthographicRect; /* @0x0 confirmed | P TransformToCamera reads its four floats; L GEViewport::GetOrthographicRectangle, SetOrthographicRectangle */
    struct GERectangleLayout viewRect; /* @0x10 confirmed | P SetViewport reads its four floats; L GEViewport::GetViewRectangle, SetViewRectangle */
    struct GE3DPlaneLayout frontPlane; /* @0x20 lle-only | L GEViewport::GetFrontPlane */
    struct GE3DPlaneLayout rearPlane; /* @0x38 lle-only | L GEViewport::GetRearPlane */
    struct GE3DPlaneLayout leftPlane; /* @0x50 lle-only | L GEViewport::GetLeftPlane */
    struct GE3DPlaneLayout rightPlane; /* @0x68 lle-only | L GEViewport::GetRightPlane */
    struct GE3DPlaneLayout topPlane; /* @0x80 lle-only | L GEViewport::GetTopPlane */
    struct GE3DPlaneLayout bottomPlane; /* @0x98 lle-only | L GEViewport::GetBottomPlane */
    struct GETransformationLayout camera; /* @0xb0 lle-only | L GEViewport::GetCamera, SetCamera */
    struct GETransformationLayout cameraInverse; /* @0xf0 lle-only | L GEViewport::GetCameraInverse */
    struct GETransformationLayout orthographicInverse; /* @0x130 guess | L DenormalizeOrthographicPoint applies it; name inferred */
    struct GETransformationLayout perspectiveInverse; /* @0x170 guess | L DenormalizePerspectivePoint applies it; name inferred */
    struct GETransformationLayout orthographicProjection; /* @0x1b0 confirmed | P Draw(GETerrainMesh), DrawWater convert it for D3D transform state 3; L GEViewport::GetOrthographicProjection */
    struct GETransformationLayout perspectiveProjection; /* @0x1f0 confirmed | P Draw(GETerrainMesh), DrawWater convert it for D3D transform state 3; L GEViewport::GetPerspectiveProjection */
    struct GEVectorLayout cameraLookAt; /* @0x230 lle-only | L GEViewport::GetCameraLookAtVector */
    struct GEVectorLayout orthographicNormalize; /* @0x23c guess | L NormalizeOrthographicCameraPoint reads three floats here */
    struct GEVectorLayout perspectiveNormalize; /* @0x248 guess | L NormalizePerspectiveCameraPoint reads three floats here */
    float unknown254; /* @0x254 guess | L SetFieldOfView and SetViewRectangle write it */
    float aspectRatioMultiplier; /* @0x258 lle-only | L GEViewport::GetAspectRatioMultiplier */
    float fieldOfView; /* @0x25c confirmed | P TransformToCamera takes its tangent; L GEViewport::GetFieldOfView */
    float fieldOfViewMultiplier; /* @0x260 lle-only | L GEViewport::GetFieldOfViewMultiplier */
    float frontClip; /* @0x264 confirmed | P TransformToCamera; L GEViewport::GetFrontClippingDistance */
    float halfHeight; /* @0x268 confirmed | P TransformToCamera; L GEViewport::GetHalfHeight */
    float halfWidth; /* @0x26c confirmed | P TransformToCamera; L GEViewport::GetHalfWidth */
    float rearClip; /* @0x270 confirmed | P TransformToCamera; L GEViewport::GetRearClippingDistance */
    float xCenter; /* @0x274 lle-only | L GEViewport::GetXCenter */
    float yCenter; /* @0x278 lle-only | L GEViewport::GetYCenter */
    float yClippingPlane; /* @0x27c confirmed | P TransformToCamera; L GEViewport::GetYClippingPlane */
    float depthNear; /* @0x280 confirmed | P SetViewport (D3DVIEWPORT7 dvMinZ); L GEViewport::GetDepthRange, SetDepthRange */
    float depthFar; /* @0x284 confirmed | P SetViewport (D3DVIEWPORT7 dvMaxZ); L GEViewport::GetDepthRange, SetDepthRange */
} GEViewportLayout;
EEL_AT(GEViewportLayout, orthographicRect, 0x0);
EEL_AT(GEViewportLayout, viewRect, 0x10);
EEL_AT(GEViewportLayout, frontPlane, 0x20);
EEL_AT(GEViewportLayout, rearPlane, 0x38);
EEL_AT(GEViewportLayout, leftPlane, 0x50);
EEL_AT(GEViewportLayout, rightPlane, 0x68);
EEL_AT(GEViewportLayout, topPlane, 0x80);
EEL_AT(GEViewportLayout, bottomPlane, 0x98);
EEL_AT(GEViewportLayout, camera, 0xb0);
EEL_AT(GEViewportLayout, cameraInverse, 0xf0);
EEL_AT(GEViewportLayout, orthographicInverse, 0x130);
EEL_AT(GEViewportLayout, perspectiveInverse, 0x170);
EEL_AT(GEViewportLayout, orthographicProjection, 0x1b0);
EEL_AT(GEViewportLayout, perspectiveProjection, 0x1f0);
EEL_AT(GEViewportLayout, cameraLookAt, 0x230);
EEL_AT(GEViewportLayout, orthographicNormalize, 0x23c);
EEL_AT(GEViewportLayout, perspectiveNormalize, 0x248);
EEL_AT(GEViewportLayout, unknown254, 0x254);
EEL_AT(GEViewportLayout, aspectRatioMultiplier, 0x258);
EEL_AT(GEViewportLayout, fieldOfView, 0x25c);
EEL_AT(GEViewportLayout, fieldOfViewMultiplier, 0x260);
EEL_AT(GEViewportLayout, frontClip, 0x264);
EEL_AT(GEViewportLayout, halfHeight, 0x268);
EEL_AT(GEViewportLayout, halfWidth, 0x26c);
EEL_AT(GEViewportLayout, rearClip, 0x270);
EEL_AT(GEViewportLayout, xCenter, 0x274);
EEL_AT(GEViewportLayout, yCenter, 0x278);
EEL_AT(GEViewportLayout, yClippingPlane, 0x27c);
EEL_AT(GEViewportLayout, depthNear, 0x280);
EEL_AT(GEViewportLayout, depthFar, 0x284);
EEL_SIZE(GEViewportLayout, 0x288);

/* @class GEFont size=0x30 size-confidence=confirmed | operator new(0x30) in GERasterizer::GERasterizer */
typedef struct GEFontLayout {
    void * vptr; /* @0x0 lle-only | L ctor (UReferenceCounter derived) */
    uint32_t refCount; /* @0x4 confirmed | P Draw(GEText) increments it; L GEText::SetFont, GEFont::SetFontBitmap */
    struct UStringLayout name; /* @0x8 lle-only | L GEFont::GetFontName */
    void * osFont; /* @0x18 confirmed | P Draw(GEText) selects it into the DC (an HFONT); L GEFont::GetOSFont */
    uint32_t quality; /* @0x1c lle-only | L GEFont::GetFontQuality */
    uint32_t style; /* @0x20 confirmed | P Draw(GEText) tests bit 0x10 (draw a shadow); L GEFont::GetFontStyle */
    void * fontBitmap; /* @0x24 lle-only | a GEFontBitmap; L GEFont::GetFontBitmap */
    uint32_t size; /* @0x28 lle-only | L GEFont::GetFontSize */
    uint32_t height; /* @0x2c lle-only | L GEFont::GetHeight */
} GEFontLayout;
EEL_AT(GEFontLayout, vptr, 0x0);
EEL_AT(GEFontLayout, refCount, 0x4);
EEL_AT(GEFontLayout, name, 0x8);
EEL_AT(GEFontLayout, osFont, 0x18);
EEL_AT(GEFontLayout, quality, 0x1c);
EEL_AT(GEFontLayout, style, 0x20);
EEL_AT(GEFontLayout, fontBitmap, 0x24);
EEL_AT(GEFontLayout, size, 0x28);
EEL_AT(GEFontLayout, height, 0x2c);
EEL_SIZE(GEFontLayout, 0x30);

/* @class GEText size=0x50 size-confidence=confirmed | operator new(0x50) in GERasterizer::GERasterizer, UIButton::SetText and others; the class has no vptr */
typedef struct GETextLayout {
    struct GE2DPointLayout location; /* @0x0 confirmed | P Draw(GEText) reads x,y; L GEText::GetLocation, SetLocation */
    struct GEColorLayout bgColor; /* @0x8 lle-only | L GEText::GetBgColor, SetBgColor (P Draw(GEText) loads it through an x87 helper) */
    struct GEColorLayout fgColor; /* @0x14 confirmed | P Draw(GEText) sums r,g,b to pick the shadow color; L GEText::GetFgColor, SetFgColor */
    struct GERectangleLayout textExtent; /* @0x20 lle-only | L GEText::GetTextExtent, ComputeTextExtent */
    struct UWideStringLayout text; /* @0x30 confirmed | P Draw(GEText) reads chars, length, capacity; L GEText::GetText, SetText */
    struct GEFontLayout *font; /* @0x44 confirmed | P Draw(GEText); L GEText::GetFont, SetFont */
    uint32_t alignment; /* @0x48 confirmed | P Draw(GEText): 1 centers, 2 right-aligns; L GEText::GetAlignment, SetAlignment */
    uint8_t isTransparent; /* @0x4c confirmed | P Draw(GEText) picks the DC background mode; L GEText::GetIsTransparent */
    uint8_t pad_0x4d[0x3];
} GETextLayout;
EEL_AT(GETextLayout, location, 0x0);
EEL_AT(GETextLayout, bgColor, 0x8);
EEL_AT(GETextLayout, fgColor, 0x14);
EEL_AT(GETextLayout, textExtent, 0x20);
EEL_AT(GETextLayout, text, 0x30);
EEL_AT(GETextLayout, font, 0x44);
EEL_AT(GETextLayout, alignment, 0x48);
EEL_AT(GETextLayout, isTransparent, 0x4c);
EEL_SIZE(GETextLayout, 0x50);

/* @class GEBitmap size=0x30 size-confidence=confirmed | operator new(0x30) in UIManager::UIManager and others */
typedef struct GEBitmapLayout {
    void * vptr; /* @0x0 lle-only | L ctor (UReferenceCounter derived) */
    uint32_t refCount; /* @0x4 lle-only | L ctor starts it at 1 */
    struct GE2DPointLayout location; /* @0x8 lle-only | L GEBitmap::GetLocation, SetLocation */
    struct GEVertexArrayLayout *vertexArray; /* @0x10 confirmed | P Draw(GEBitmap) reads and rewrites its four vertices; L GEBitmap::GetVertexArray */
    struct GETextureLayout **textures; /* @0x14 confirmed | P Draw(GEBitmap) indexes it by screen; L GEBitmap::GetTexture, SetTexture */
    float textureHeight; /* @0x18 lle-only | L GEBitmap::SetTexture, GetHeight */
    float textureWidth; /* @0x1c lle-only | L GEBitmap::SetTexture, GetWidth */
    float rotation; /* @0x20 lle-only | L GEBitmap::GetRotation, SetRotation */
    float xScale; /* @0x24 lle-only | L GEBitmap::GetXScale */
    float yScale; /* @0x28 lle-only | L GEBitmap::GetYScale */
    uint32_t screenCount; /* @0x2c lle-only | L GEBitmap ctor: length of textures */
} GEBitmapLayout;
EEL_AT(GEBitmapLayout, vptr, 0x0);
EEL_AT(GEBitmapLayout, refCount, 0x4);
EEL_AT(GEBitmapLayout, location, 0x8);
EEL_AT(GEBitmapLayout, vertexArray, 0x10);
EEL_AT(GEBitmapLayout, textures, 0x14);
EEL_AT(GEBitmapLayout, textureHeight, 0x18);
EEL_AT(GEBitmapLayout, textureWidth, 0x1c);
EEL_AT(GEBitmapLayout, rotation, 0x20);
EEL_AT(GEBitmapLayout, xScale, 0x24);
EEL_AT(GEBitmapLayout, yScale, 0x28);
EEL_AT(GEBitmapLayout, screenCount, 0x2c);
EEL_SIZE(GEBitmapLayout, 0x30);

/* @class GELine size=0xc size-confidence=confirmed | operator= and the constructors cover exactly 0x0..0xb */
typedef struct GELineLayout {
    struct GEVertexArrayLayout *vertexArray; /* @0x0 confirmed | P Draw(GELine) reads it; L GELine ctor */
    struct GETextureLayout **textures; /* @0x4 confirmed | P Draw(GELine) indexes it by screen; L GELine::SetTexture */
    uint32_t screenCount; /* @0x8 lle-only | L GELine ctor: length of textures */
} GELineLayout;
EEL_AT(GELineLayout, vertexArray, 0x0);
EEL_AT(GELineLayout, textures, 0x4);
EEL_AT(GELineLayout, screenCount, 0x8);
EEL_SIZE(GELineLayout, 0xc);

/* @class GEPoint size=0xc size-confidence=confirmed | the constructors cover exactly 0x0..0xb */
typedef struct GEPointLayout {
    struct GEVertexArrayLayout *vertexArray; /* @0x0 confirmed | P Draw(GEPoint) reads it; L GEPoint ctor */
    struct GETextureLayout **textures; /* @0x4 confirmed | P Draw(GEPoint) indexes it by screen; L GEPoint ctor */
    uint32_t screenCount; /* @0x8 lle-only | L GEPoint ctor: length of textures */
} GEPointLayout;
EEL_AT(GEPointLayout, vertexArray, 0x0);
EEL_AT(GEPointLayout, textures, 0x4);
EEL_AT(GEPointLayout, screenCount, 0x8);
EEL_SIZE(GEPointLayout, 0xc);

/* @class GERhombus size=0x18 size-confidence=confirmed | operator new(0x18) in UIListBox constructors */
typedef struct GERhombusLayout {
    struct GEVertexArrayLayout *vertexArray; /* @0x0 confirmed | P Draw(GERhombus) reads it; L GERhombus ctor */
    struct GERectangleLayout rectangle; /* @0x4 lle-only | L GERhombus::SetRectangle */
    float rotation; /* @0x14 lle-only | L GERhombus::GetRotation, SetRotation */
} GERhombusLayout;
EEL_AT(GERhombusLayout, vertexArray, 0x0);
EEL_AT(GERhombusLayout, rectangle, 0x4);
EEL_AT(GERhombusLayout, rotation, 0x14);
EEL_SIZE(GERhombusLayout, 0x18);

/* @class GEWindow size=0x1c size-confidence=confirmed | the constructor covers 0x0..0x18 and the next object member starts at 0x1c */
typedef struct GEWindowLayout {
    void * hwnd; /* @0x0 confirmed | P Capture, CreateAWindow; L GEWindow::GetOSRepresentation */
    struct GE2DPointLayout clientOrigin; /* @0x4 confirmed | P CreateAWindow writes it; L GEWindow::GetClientOrigin */
    uint32_t width; /* @0xc confirmed | P ActivateScreen; L GEWindow::GetWidth, Resize */
    uint32_t height; /* @0x10 confirmed | P ActivateScreen; L GEWindow::GetHeight, Resize */
    uint32_t style; /* @0x14 confirmed | P CreateAWindow writes it; L GEWindow ctor (fourth argument), Resize tests it against 1 */
    uint8_t ownsWindow; /* @0x18 guess | P CreateAWindow writes it; L ctor (fifth argument), ~GEWindow reads it; meaning inferred */
    uint8_t pad_0x19[0x3];
} GEWindowLayout;
EEL_AT(GEWindowLayout, hwnd, 0x0);
EEL_AT(GEWindowLayout, clientOrigin, 0x4);
EEL_AT(GEWindowLayout, width, 0xc);
EEL_AT(GEWindowLayout, height, 0x10);
EEL_AT(GEWindowLayout, style, 0x14);
EEL_AT(GEWindowLayout, ownsWindow, 0x18);
EEL_SIZE(GEWindowLayout, 0x1c);

/* @class GETextureManager size=0x1c size-confidence=confirmed | operator new(0x1c) in the executable (push 0x1c before the constructor call) */
typedef struct GETextureManagerLayout {
    uint8_t flags[2]; /* @0x0 guess | L ctor writes two bytes */
    uint8_t pad_0x2[0x2];
    void * textureListHead; /* @0x4 lle-only | MSVC 6 std::list: head at 0x4, size at 0x8; L LoadTexture, RemoveAllTextures */
    uint32_t textureCount; /* @0x8 lle-only | L GETextureManager::GetTextureCount */
    void * criticalSection; /* @0xc lle-only | a TSCriticalSection; L LoadTexture */
    void * path; /* @0x10 lle-only | an FSPath; L GETextureManager::GetTexturePath */
    struct GEScreenLayout *screen; /* @0x14 confirmed | P ActivateRenderingSurface writes it; L GETextureManager::GetScreen, SetScreen */
    uint32_t memoryFootprint; /* @0x18 lle-only | L GETextureManager::GetMemoryFootprint */
} GETextureManagerLayout;
EEL_AT(GETextureManagerLayout, flags, 0x0);
EEL_AT(GETextureManagerLayout, textureListHead, 0x4);
EEL_AT(GETextureManagerLayout, textureCount, 0x8);
EEL_AT(GETextureManagerLayout, criticalSection, 0xc);
EEL_AT(GETextureManagerLayout, path, 0x10);
EEL_AT(GETextureManagerLayout, screen, 0x14);
EEL_AT(GETextureManagerLayout, memoryFootprint, 0x18);
EEL_SIZE(GETextureManagerLayout, 0x1c);

/* @class GEScreen size=0xc size-confidence=confirmed | the base-class copy constructor copies 0x4 and 0x8; DX7Screen adds its own fields behind */
typedef struct GEScreenLayout {
    void * vptr; /* @0x0 lle-only | L ctor sets the GEScreen vftable */
    struct GETextureManagerLayout *textureManager; /* @0x4 confirmed | P DX7Screen constructor writes it; L GEScreen::GetTextureManager, SetTextureManager */
    uint32_t rasterizerIndex; /* @0x8 confirmed | P DX7Screen constructor writes it; L GEScreen::GetRasterizerIndex, SetRasterizerIndex */
} GEScreenLayout;
EEL_AT(GEScreenLayout, vptr, 0x0);
EEL_AT(GEScreenLayout, textureManager, 0x4);
EEL_AT(GEScreenLayout, rasterizerIndex, 0x8);
EEL_SIZE(GEScreenLayout, 0xc);

/* @class GESurface size=0x8 size-confidence=confirmed | the base constructor writes 0x0 and 0x4; DX7Surface adds its own fields from 0x8 */
typedef struct GESurfaceLayout {
    void * vptr; /* @0x0 lle-only | L ctor sets the GESurface vftable */
    uint32_t refCount; /* @0x4 confirmed | P DX7Surface constructor writes it; L GESurface ctor starts it at 1 */
} GESurfaceLayout;
EEL_AT(GESurfaceLayout, vptr, 0x0);
EEL_AT(GESurfaceLayout, refCount, 0x4);
EEL_SIZE(GESurfaceLayout, 0x8);

/* @class GEMovie size=0x8 size-confidence=confirmed | the base constructor writes 0x0 and 0x4 */
typedef struct GEMovieLayout {
    void * vptr; /* @0x0 lle-only | L ctor sets the GEMovie vftable */
    uint32_t refCount; /* @0x4 confirmed | P DX7Movie constructor writes it; L GEMovie ctor starts it at 1 */
} GEMovieLayout;
EEL_AT(GEMovieLayout, vptr, 0x0);
EEL_AT(GEMovieLayout, refCount, 0x4);
EEL_SIZE(GEMovieLayout, 0x8);

/* @class GEVertexArray size=0x4 size-confidence=confirmed | the base constructors write only the vptr */
typedef struct GEVertexArrayLayout {
    void * vptr; /* @0x0 lle-only | L GEVertexArray ctor */
} GEVertexArrayLayout;
EEL_AT(GEVertexArrayLayout, vptr, 0x0);
EEL_SIZE(GEVertexArrayLayout, 0x4);

/* @class GEPolygonArray size=0x4 size-confidence=confirmed | the base constructors write only the vptr */
typedef struct GEPolygonArrayLayout {
    void * vptr; /* @0x0 lle-only | L GEPolygonArray ctor */
} GEPolygonArrayLayout;
EEL_AT(GEPolygonArrayLayout, vptr, 0x0);
EEL_SIZE(GEPolygonArrayLayout, 0x4);
#endif
