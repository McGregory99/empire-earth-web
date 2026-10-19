# Engine class layouts the renderer plugin reads inline (P2.5)

Generated from `ge_layouts.h` by `tools/re/gen_layouts_md.py` (`make check-layouts` fails if this file is stale). Facts only: names and types are
inferred from usage; offsets and sizes are asserted at compile time and checked against the real `Low-Level Engine.dll` by `tests/layouts/`.

Confidence: `confirmed` = plugin and engine agree; `plugin-only` = only the plugin reads it (or the object is plugin-owned); `lle-only` = the engine
names it but the plugin does not touch it; `guess` = offset certain, type or meaning inferred. Evidence: `P` = plugin function, `L` = engine function.

## Summary

46 classes and structs, 397 fields.

| confidence | fields |
|---|---|
| confirmed | 191 |
| plugin-only | 36 |
| lle-only | 142 |
| guess | 28 |

### UString (0x10 bytes, size confirmed)

Size evidence: Phase 1 (TCP plugin) layout, low_level_engine.h; GEMaterial, GEModel and GEFont embed one every 0x10 bytes.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 4 | `void *` | `vptr` | confirmed | L UString constructors; virtual destructor |
| 0x4 | 4 | `uint8_t *` | `chars` | confirmed | P Draw(GEText) reads the text through the UWideString twin; Phase 1 plugin reads it inline |
| 0x8 | 4 | `uint32_t` | `length` | confirmed | characters without the terminator; Phase 1 plugin |
| 0xc | 4 | `uint32_t` | `capacity` | confirmed | L GEModel::SizeOf reads it at GEModel+0xe0 |

### UWideString (0x14 bytes, size confirmed)

Size evidence: Phase 1 layout (single-byte characters despite the name); GEText.text spans 0x30..0x43.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 4 | `void *` | `vptr` | confirmed | L UWideString constructors |
| 0x4 | 4 | `uint8_t *` | `chars` | confirmed | P Draw(GEText) passes it to ExtTextOutA and GetTextExtentPoint32A |
| 0x8 | 4 | `uint32_t` | `length` | confirmed | P Draw(GEText) passes it as the ExtTextOutA count |
| 0xc | 4 | `uint32_t` | `capacity` | confirmed | P Draw(GEText) passes it as the GetTextExtentPoint32A count (an original quirk) |
| 0x10 | 4 | `uint32_t` | `reserved` | plugin-only | Phase 1 layout; unused |

### GEColor (0xc bytes, size confirmed)

Size evidence: three floats; GELight.color and GEMaterial.color sit 0xc apart from their neighbours.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 4 | `float` | `r` | confirmed | P Clear, DrawTerrainHighlight; L GEColor::GetRed |
| 0x4 | 4 | `float` | `g` | confirmed | P DrawTerrainHighlight; L GEColor::GetGreen |
| 0x8 | 4 | `float` | `b` | confirmed | P DrawTerrainHighlight; L GEColor::GetBlue, SetBlue |

### GE2DPoint (0x8 bytes, size confirmed)

Size evidence: two floats; GE2DPoint::SetXY, Clear.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 4 | `float` | `x` | confirmed | P CreateAWindow; L GE2DPoint::SetXY, GetX |
| 0x4 | 4 | `float` | `y` | confirmed | P CreateAWindow; L GE2DPoint::SetXY, GetY |

### GE3DPoint (0xc bytes, size confirmed)

Size evidence: three floats; GE3DPoint::SetXYZ, Clear.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 4 | `float` | `x` | confirmed | P GetPoint; L GE3DPoint::SetXYZ, GetX |
| 0x4 | 4 | `float` | `y` | confirmed | P GetPoint; L GE3DPoint::SetXYZ, GetY |
| 0x8 | 4 | `float` | `z` | confirmed | P GetPoint; L GE3DPoint::SetXYZ, GetZ |

### GEVector (0xc bytes, size confirmed)

Size evidence: three floats; GEVector::GetMagnitudeSquared.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 4 | `float` | `x` | confirmed | P GetNormal; L GEVector::GetX |
| 0x4 | 4 | `float` | `y` | confirmed | P GetNormal; L GEVector::GetY |
| 0x8 | 4 | `float` | `z` | confirmed | P GetNormal; L GEVector::GetZ, SetZ |

### GERectangle (0x10 bytes, size confirmed)

Size evidence: four floats; GERectangle::operator=.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 4 | `float` | `bottom` | confirmed | P Clear, ClearZBuffer; L GERectangle::GetBottom, GetHeight (bottom - top) |
| 0x4 | 4 | `float` | `left` | confirmed | P Clear; L GERectangle::GetLeft, GetWidth (right - left) |
| 0x8 | 4 | `float` | `right` | confirmed | P Clear; L GERectangle::GetRight |
| 0xc | 4 | `float` | `top` | confirmed | P Clear; L GERectangle::GetTop |

### GETextureCoordinate (0x8 bytes, size confirmed)

Size evidence: two floats; GEBitmap and GELine hand them to the vertex arrays as two dwords.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 4 | `float` | `u` | confirmed | P GetTextureCoordinate; L GETextureCoordinate::GetU |
| 0x4 | 4 | `float` | `v` | confirmed | P GetTextureCoordinate; L GETextureCoordinate::GetV |

### GETransformation (0x40 bytes, size confirmed)

Size evidence: sixteen floats; GETransformation::GETransformation, MakeIdentity write 0x0..0x3c.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 64 | `float[16]` | `m` | confirmed | row-major 4x4, element (row, col) at m[row*4+col], translation in m[3] m[7] m[11]; P ConvertToD3DMatrix reads all sixteen; L GETransformation::GetElement, GetPosition, Apply |

### GE3DPlane (0x18 bytes, size confirmed)

Size evidence: a point and a normal; the six GEViewport planes sit 0x18 apart.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 12 | `struct GE3DPointLayout` | `point` | lle-only | L GE3DPlane::GetPoint returns this |
| 0xc | 12 | `struct GEVectorLayout` | `normal` | lle-only | L GE3DPlane::GetNormal returns this + 0xc |

### GEBoundingBox (0x24 bytes, size confirmed)

Size evidence: two corners and three extents; GEModel.boundingBoxes has a 0x24 stride.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 12 | `struct GE3DPointLayout` | `bottomLeftFront` | lle-only | L GEBoundingBox::GetBottomLeftFront, SetBottomLeftFront |
| 0xc | 12 | `struct GE3DPointLayout` | `topRightBack` | lle-only | L GEBoundingBox::GetTopRightBack, SetTopRightBack |
| 0x18 | 4 | `float` | `width` | lle-only | L GEBoundingBox::GetWidth |
| 0x1c | 4 | `float` | `height` | lle-only | L GEBoundingBox::GetHeight |
| 0x20 | 4 | `float` | `depth` | lle-only | L GEBoundingBox::GetDepth |

### GETStage (0x20 bytes, size confirmed)

Size evidence: GETexture::GetStage returns this + 0xc; the copy constructor copies this block as eight dwords.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 4 | `struct GETextureLayout *` | `texture` | confirmed | P SetTexture reads the stage texture; L GETexture::SetStageTexture, GetStageTexture |
| 0x4 | 4 | `uint32_t` | `colorMode` | confirmed | P SetTexture: 0..3 maps to a D3D color op; L GETexture::SetStageColorMode |
| 0x8 | 4 | `uint32_t` | `alphaMode` | confirmed | P SetTexture: 0..3 maps to a D3D alpha op; L GETexture::SetStageAlphaMode |
| 0xc | 4 | `uint32_t` | `addressingMode` | confirmed | P SetTexture; L GETexture::SetStageTextureAddressingMode |
| 0x10 | 4 | `uint32_t` | `minFilter` | confirmed | P SetTexture; L GETexture::SetStageMinFilterMode |
| 0x14 | 4 | `uint32_t` | `magFilter` | confirmed | P SetTexture; L GETexture::SetStageMagFilterMode |
| 0x18 | 4 | `uint32_t` | `mipFilter` | confirmed | P SetTexture; L GETexture::SetStageMipFilterMode |
| 0x1c | 4 | `float` | `mipMapBias` | confirmed | P SetTexture; L GETexture::SetStageMipMapBias |

### GETexture (0x6c bytes, size confirmed)

Size evidence: operator new(0x6c) in GETextureManager::LoadTexture and MakeTexture; the copy constructor ends at 0x6b.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 4 | `void *` | `vptr` | lle-only | L ctor sets the GETexture vftable (UReferenceCounter derived) |
| 0x4 | 4 | `uint32_t` | `refCount` | confirmed | P SetTexture increments it; L GETexture::Dereference, ctor starts it at 1 |
| 0x8 | 1 | `uint8_t` | `refFlag` | lle-only | L ctor clears it; UReferenceCounter flag byte |
| 0xc | 32 | `struct GETStageLayout` | `stage` | confirmed | P SetTexture reads the eight stage words; L GETexture::GetStage |
| 0x2c | 8 | `struct GETextureCoordinateLayout` | `frameTopLeft` | confirmed | P Draw(GEBitmap) reads u,v; L GETexture::GetFrameTopLeftCorner |
| 0x34 | 4 | `struct GEScreenLayout *` | `screen` | lle-only | L GETexture::GetScreen, Recreate |
| 0x38 | 4 | `struct GESurfaceLayout *` | `surface` | confirmed | P SetTexture reaches the DirectDraw surface through it; L GETexture::GetSurface, GetWidth |
| 0x3c | 4 | `struct GETextureLayout *` | `baseTexture` | confirmed | P SetTexture follows stage.texture -> baseTexture -> surface; L GETexture::GetData, GetFrameSurface, GetIsCompressed; ctor points it at the texture itself |
| 0x40 | 4 | `struct GETextureLayout **` | `frames` | lle-only | L GETexture::GetFrameTexture |
| 0x44 | 4 | `struct GETextureManagerLayout *` | `manager` | lle-only | L GETexture::Promote, Dereference |
| 0x48 | 4 | `void *` | `userData` | lle-only | L GETexture::GetData, SetData (through baseTexture) |
| 0x4c | 4 | `float` | `frameUVSize` | confirmed | P Draw(GEBitmap) scales the corners by it; L GETexture::GetFrameUVSize |
| 0x50 | 4 | `uint32_t` | `frameFormat` | lle-only | L GETexture::GetFrameFormat; 0 means one texture per frame, else a single frame |
| 0x54 | 4 | `uint32_t` | `frame` | lle-only | L GETexture::GetFrame, SetFrame |
| 0x58 | 4 | `uint32_t` | `frameCount` | lle-only | L GETexture::GetFrameCount, GetAllocatedSize |
| 0x5c | 4 | `uint32_t` | `framesPerRow` | guess | L GETexture::Load multiplies the frame width by it; meaning inferred |
| 0x60 | 4 | `uint32_t` | `mipLevelCount` | lle-only | L GETexture::GetMipLevelCount |
| 0x64 | 4 | `uint32_t` | `numMipLevelsToDrop` | lle-only | L GETexture::GetNumMipLevelsToDrop |
| 0x68 | 1 | `uint8_t` | `isCompressed` | lle-only | L GETexture::GetIsCompressed (read through baseTexture) |
| 0x69 | 1 | `uint8_t` | `isPromoted` | lle-only | L GETexture::GetIsPromoted, SetIsPromoted |
| 0x6a | 1 | `uint8_t` | `hasChanged` | confirmed | P SetTexture tests it and clears it; L GETexture::HasChanged, SetHasChanged, ValidateChanges |
| 0x6b | 1 | `uint8_t` | `createMIPMaps` | lle-only | L GETexture::GetCreateMIPMaps |

### GEMaterial (0x48 bytes, size confirmed)

Size evidence: operator new(0x48) in GEModel::AllocateInternalData, ConvertLegacyModel, ComputeLODReduction.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 4 | `void *` | `vptr` | lle-only | L ctor sets the GEMaterial vftable (UReferenceCounter derived) |
| 0x4 | 4 | `uint32_t` | `refCount` | lle-only | L ctor starts it at 1 |
| 0x8 | 16 | `struct UStringLayout` | `name` | lle-only | L GEMaterial::GetName, SetName |
| 0x18 | 12 | `struct GEColorLayout` | `color` | confirmed | P DrawModelHierarchy reads r,g,b into the material; L GEMaterial::GetColor, SetColor |
| 0x24 | 4 | `struct GETextureLayout **` | `textures` | confirmed | P DrawModelHierarchy indexes it by screen; L GEMaterial::GetTexture, SetTexture |
| 0x28 | 4 | `uint32_t` | `textureCount` | lle-only | L GEMaterial ctor (first argument), ~GEMaterial |
| 0x2c | 4 | `uint32_t *` | `polygonStart` | confirmed | P DrawModelHierarchy indexes it by level of detail; L GEMaterial::GetPolygonStartingIndex |
| 0x30 | 4 | `uint32_t *` | `polygonLength` | confirmed | P DrawModelHierarchy indexes it by level of detail; L GEMaterial::GetPolygonRangeLength |
| 0x34 | 4 | `uint32_t` | `vertexStart` | confirmed | P CopyStdVertices and variants; L GEMaterial::GetVertexStartingIndex |
| 0x38 | 4 | `uint32_t` | `vertexLength` | confirmed | P DrawModelHierarchy (vertex count to lock), CopyStdVertices; L GEMaterial::GetVertexRangeLength |
| 0x3c | 4 | `uint32_t` | `textureIndex` | lle-only | L GEMaterial::GetTextureIndex, SetTextureIndex |
| 0x40 | 4 | `uint32_t` | `lodCount` | lle-only | L GEMaterial::GetLODCount, ctor (second argument) |
| 0x44 | 4 | `float` | `opacity` | confirmed | P DrawModelHierarchy multiplies the model opacity by it; L GEMaterial::GetOpacity, SetOpacity |

### GELight (0x74 bytes, size confirmed)

Size evidence: copy constructor and operator= copy 0x0..0x73; the class has no vptr.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 12 | `struct GEColorLayout` | `color` | lle-only | L GELight::GetColor returns this, SetColor normalizes it |
| 0xc | 12 | `struct GEColorLayout` | `computedColor` | confirmed | P UpdateLight reads it as the D3D light color; L GELight::GetComputedColor (given color * intensity) |
| 0x18 | 12 | `struct GE3DPointLayout` | `location` | confirmed | P UpdateLight reads the position; L GELight::GetLocation, SetLocation |
| 0x24 | 12 | `struct GE3DPointLayout` | `modelSpaceLocation` | lle-only | L GELight::GetModelSpaceLocation |
| 0x30 | 12 | `struct GEVectorLayout` | `direction` | confirmed | P UpdateLight negates it into the D3D direction; L GELight::GetDirection; SetDirection stores the negated, normalized argument |
| 0x3c | 12 | `struct GEVectorLayout` | `modelSpaceDirection` | lle-only | L GELight::GetModelSpaceDirection |
| 0x48 | 4 | `float` | `attenuation0` | confirmed | P UpdateLight; L GELight::GetAttenuation0 |
| 0x4c | 4 | `float` | `attenuation1` | confirmed | P UpdateLight; L GELight::GetAttenuation1, SetAttenuation1 |
| 0x50 | 4 | `float` | `attenuation2` | confirmed | P UpdateLight; L GELight::GetAttenuation2, SetAttenuation2 |
| 0x54 | 4 | `float` | `intensity` | lle-only | L GELight::GetIntensity, SetIntensity |
| 0x58 | 4 | `float` | `range` | confirmed | P UpdateLight; L GELight::GetRange, SetRange |
| 0x5c | 4 | `float` | `rangeInverse` | lle-only | L GELight::GetRangeInverse |
| 0x60 | 4 | `float` | `rangeSquared` | lle-only | L GELight::GetRangeSquared |
| 0x64 | 4 | `float` | `modelSpaceRange` | lle-only | L GELight::GetModelSpaceRange |
| 0x68 | 4 | `float` | `modelSpaceRangeInverse` | lle-only | L GELight::GetModelSpaceRangeInverse |
| 0x6c | 4 | `float` | `modelSpaceRangeSquared` | lle-only | L GELight::GetModelSpaceRangeSquared |
| 0x70 | 4 | `uint32_t` | `style` | confirmed | P UpdateLight: non-zero selects D3DLIGHT_DIRECTIONAL (3), zero D3DLIGHT_POINT (1); L GELight::GetStyle, SetStyle |

### GEModel (0x188 bytes, size confirmed)

Size evidence: operator new(0x188) in GEModel::LoadCurrentVersion, LoadLegacyVersion, GEModelManager::LoadModel.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 4 | `void *` | `vptr` | lle-only | L ctor sets the GEModel vftable (UReferenceCounter derived) |
| 0x4 | 4 | `uint32_t` | `refCount` | lle-only | L ctor starts it at 1; GEModel::Dereference |
| 0x8 | 64 | `struct GETransformationLayout` | `preModelTransform` | lle-only | L GEModel::GetGraphicsPreModelTransform |
| 0x48 | 64 | `struct GETransformationLayout` | `postModelTransform` | lle-only | L GEModel::GetGraphicsPostModelTransform |
| 0x88 | 64 | `struct GETransformationLayout` | `graphicsTransform` | confirmed | P DrawModelHierarchy applies it to the stack and the camera; L GEModel::GetGraphicsTransform |
| 0xc8 | 12 | `struct GE3DPointLayout` | `centerPoint` | lle-only | L GEModel::GetCenterPoint |
| 0xd4 | 16 | `struct UStringLayout` | `name` | lle-only | L GEModel::GetName |
| 0xe4 | 1 | `uint8_t` | `flagE4` | guess | L ctor sets it; meaning unknown |
| 0xe5 | 1 | `uint8_t` | `flagE5` | guess | L ctor sets it; meaning unknown |
| 0xe8 | 4 | `uint32_t` | `unknownE8` | guess | L ConvertLegacyModel reads it |
| 0xec | 4 | `uint32_t` | `unknownEC` | guess | L GEModel::SizeOf subtracts it from frameCount |
| 0xf0 | 4 | `struct GETransformationLayout *` | `internalTransforms` | lle-only | L GEModel::GetInternalTransform (stride 0x40), GetGraphicsInternalTransform |
| 0xf4 | 4 | `void *` | `manager` | lle-only | a GEModelManager; L ctor, Dereference |
| 0xf8 | 4 | `struct GEModelLayout *` | `parentModel` | lle-only | L GEModel::GetParentModel, SetParentModel |
| 0xfc | 4 | `struct GEModelLayout **` | `subModels` | confirmed | P DrawModelHierarchy recurses over it; L GEModel::GetSubModel |
| 0x100 | 4 | `struct GEPolygonArrayLayout **` | `polygonArrays` | confirmed | P DrawModelHierarchy indexes it by level of detail; L GEModel::GetPolygonArray |
| 0x104 | 4 | `struct GEVertexArrayLayout **` | `vertexArrays` | confirmed | P DrawModelHierarchy and CopyStdVertices call through its first entry; L GEModel::GetVertexArray |
| 0x108 | 4 | `struct GEMaterialLayout **` | `materials` | confirmed | P DrawModelHierarchy iterates it; L GEModel::GetMaterial |
| 0x10c | 4 | `struct GE3DPointLayout **` | `tagPointLists` | lle-only | L GEModel::GetTagPoint(index, frame) |
| 0x110 | 4 | `struct GEBoundingBoxLayout *` | `boundingBoxes` | lle-only | L GEModel::GetBoundingBox (stride 0x24) |
| 0x114 | 4 | `struct GEBoundingBoxLayout *` | `graphicsBoundingBox` | lle-only | L GEModel::GetGraphicsBoundingBox, SetGraphicsCurrentFrame |
| 0x118 | 4 | `struct GE3DPointLayout *` | `tagPoints` | lle-only | L GEModel::GetTagPoint, GetTagPointList |
| 0x11c | 4 | `void *` | `textureFilenames` | lle-only | an array of UString; L GEModel::SetTextureFilename |
| 0x120 | 4 | `struct UStringLayout *` | `tagPointNames` | lle-only | L GEModel::GetTagPointName (stride 0x10) |
| 0x124 | 4 | `float *` | `boundingRadii` | lle-only | L GEModel::GetBoundingRadius |
| 0x128 | 4 | `float` | `graphicsScale` | confirmed | P DrawModelHierarchy scales the radius by it; L GEModel::GetGraphicsScale |
| 0x12c | 4 | `float` | `opacity` | confirmed | P DrawModelHierarchy multiplies the material opacity by it; L GEModel::GetModelOpacity |
| 0x130 | 4 | `float` | `fractionalFramePosition` | confirmed | P CopyAndSmoothStdVertices; L GEModel::GetFractionalFramePosition |
| 0x134 | 4 | `uint32_t` | `currentFrame` | confirmed | P CopyAndSmoothStdVertices; L GEModel::GetGraphicsCurrentFrame |
| 0x138 | 4 | `uint32_t` | `nextFrame` | confirmed | P CopyAndSmoothStdVertices; L GEModel::GetGraphicsNextFrame |
| 0x13c | 4 | `uint32_t` | `subModelCount` | confirmed | P DrawModelHierarchy; L GEModel::GetSubModelCount |
| 0x140 | 4 | `uint32_t` | `frameCount` | lle-only | L GEModel::GetFrameCount |
| 0x144 | 4 | `uint32_t` | `materialCount` | confirmed | P DrawModelHierarchy; L GEModel::GetMaterialCount |
| 0x148 | 4 | `uint32_t` | `polygonCount` | lle-only | L GEModel::GetPolygonCount |
| 0x14c | 4 | `uint32_t` | `vertexCount` | lle-only | L ctor argument |
| 0x150 | 4 | `uint32_t` | `tagPointCount` | lle-only | L GEModel::GetTagPointCount |
| 0x154 | 4 | `uint32_t` | `lodCount` | confirmed | P DrawModelHierarchy clamps the level of detail with it; L GEModel::GetLODCount |
| 0x158 | 6 | `uint8_t[6]` | `clipToPlane` | lle-only | L GEModel::GetClipToPlane, SetClipToPlane (one byte per frustum plane) |
| 0x160 | 40 | `uint32_t[10]` | `legacyLoadState` | guess | L ConvertLegacyModel and the legacy chunk loaders use these dwords |

### GEViewport (0x288 bytes, size confirmed)

Size evidence: copy constructor and operator= copy 0x0..0x287; GERasterizer screen records derive from it (their own fields start at 0x288); the class has no vptr.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 16 | `struct GERectangleLayout` | `orthographicRect` | confirmed | P TransformToCamera reads its four floats; L GEViewport::GetOrthographicRectangle, SetOrthographicRectangle |
| 0x10 | 16 | `struct GERectangleLayout` | `viewRect` | confirmed | P SetViewport reads its four floats; L GEViewport::GetViewRectangle, SetViewRectangle |
| 0x20 | 24 | `struct GE3DPlaneLayout` | `frontPlane` | lle-only | L GEViewport::GetFrontPlane |
| 0x38 | 24 | `struct GE3DPlaneLayout` | `rearPlane` | lle-only | L GEViewport::GetRearPlane |
| 0x50 | 24 | `struct GE3DPlaneLayout` | `leftPlane` | lle-only | L GEViewport::GetLeftPlane |
| 0x68 | 24 | `struct GE3DPlaneLayout` | `rightPlane` | lle-only | L GEViewport::GetRightPlane |
| 0x80 | 24 | `struct GE3DPlaneLayout` | `topPlane` | lle-only | L GEViewport::GetTopPlane |
| 0x98 | 24 | `struct GE3DPlaneLayout` | `bottomPlane` | lle-only | L GEViewport::GetBottomPlane |
| 0xb0 | 64 | `struct GETransformationLayout` | `camera` | lle-only | L GEViewport::GetCamera, SetCamera |
| 0xf0 | 64 | `struct GETransformationLayout` | `cameraInverse` | lle-only | L GEViewport::GetCameraInverse |
| 0x130 | 64 | `struct GETransformationLayout` | `orthographicInverse` | guess | L DenormalizeOrthographicPoint applies it; name inferred |
| 0x170 | 64 | `struct GETransformationLayout` | `perspectiveInverse` | guess | L DenormalizePerspectivePoint applies it; name inferred |
| 0x1b0 | 64 | `struct GETransformationLayout` | `orthographicProjection` | confirmed | P Draw(GETerrainMesh), DrawWater convert it for D3D transform state 3; L GEViewport::GetOrthographicProjection |
| 0x1f0 | 64 | `struct GETransformationLayout` | `perspectiveProjection` | confirmed | P Draw(GETerrainMesh), DrawWater convert it for D3D transform state 3; L GEViewport::GetPerspectiveProjection |
| 0x230 | 12 | `struct GEVectorLayout` | `cameraLookAt` | lle-only | L GEViewport::GetCameraLookAtVector |
| 0x23c | 12 | `struct GEVectorLayout` | `orthographicNormalize` | guess | L NormalizeOrthographicCameraPoint reads three floats here |
| 0x248 | 12 | `struct GEVectorLayout` | `perspectiveNormalize` | guess | L NormalizePerspectiveCameraPoint reads three floats here |
| 0x254 | 4 | `float` | `unknown254` | guess | L SetFieldOfView and SetViewRectangle write it |
| 0x258 | 4 | `float` | `aspectRatioMultiplier` | lle-only | L GEViewport::GetAspectRatioMultiplier |
| 0x25c | 4 | `float` | `fieldOfView` | confirmed | P TransformToCamera takes its tangent; L GEViewport::GetFieldOfView |
| 0x260 | 4 | `float` | `fieldOfViewMultiplier` | lle-only | L GEViewport::GetFieldOfViewMultiplier |
| 0x264 | 4 | `float` | `frontClip` | confirmed | P TransformToCamera; L GEViewport::GetFrontClippingDistance |
| 0x268 | 4 | `float` | `halfHeight` | confirmed | P TransformToCamera; L GEViewport::GetHalfHeight |
| 0x26c | 4 | `float` | `halfWidth` | confirmed | P TransformToCamera; L GEViewport::GetHalfWidth |
| 0x270 | 4 | `float` | `rearClip` | confirmed | P TransformToCamera; L GEViewport::GetRearClippingDistance |
| 0x274 | 4 | `float` | `xCenter` | lle-only | L GEViewport::GetXCenter |
| 0x278 | 4 | `float` | `yCenter` | lle-only | L GEViewport::GetYCenter |
| 0x27c | 4 | `float` | `yClippingPlane` | confirmed | P TransformToCamera; L GEViewport::GetYClippingPlane |
| 0x280 | 4 | `float` | `depthNear` | confirmed | P SetViewport (D3DVIEWPORT7 dvMinZ); L GEViewport::GetDepthRange, SetDepthRange |
| 0x284 | 4 | `float` | `depthFar` | confirmed | P SetViewport (D3DVIEWPORT7 dvMaxZ); L GEViewport::GetDepthRange, SetDepthRange |

### GEFont (0x30 bytes, size confirmed)

Size evidence: operator new(0x30) in GERasterizer::GERasterizer.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 4 | `void *` | `vptr` | lle-only | L ctor (UReferenceCounter derived) |
| 0x4 | 4 | `uint32_t` | `refCount` | confirmed | P Draw(GEText) increments it; L GEText::SetFont, GEFont::SetFontBitmap |
| 0x8 | 16 | `struct UStringLayout` | `name` | lle-only | L GEFont::GetFontName |
| 0x18 | 4 | `void *` | `osFont` | confirmed | P Draw(GEText) selects it into the DC (an HFONT); L GEFont::GetOSFont |
| 0x1c | 4 | `uint32_t` | `quality` | lle-only | L GEFont::GetFontQuality |
| 0x20 | 4 | `uint32_t` | `style` | confirmed | P Draw(GEText) tests bit 0x10 (draw a shadow); L GEFont::GetFontStyle |
| 0x24 | 4 | `void *` | `fontBitmap` | lle-only | a GEFontBitmap; L GEFont::GetFontBitmap |
| 0x28 | 4 | `uint32_t` | `size` | lle-only | L GEFont::GetFontSize |
| 0x2c | 4 | `uint32_t` | `height` | lle-only | L GEFont::GetHeight |

### GEText (0x50 bytes, size confirmed)

Size evidence: operator new(0x50) in GERasterizer::GERasterizer, UIButton::SetText and others; the class has no vptr.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 8 | `struct GE2DPointLayout` | `location` | confirmed | P Draw(GEText) reads x,y; L GEText::GetLocation, SetLocation |
| 0x8 | 12 | `struct GEColorLayout` | `bgColor` | lle-only | L GEText::GetBgColor, SetBgColor (P Draw(GEText) loads it through an x87 helper) |
| 0x14 | 12 | `struct GEColorLayout` | `fgColor` | confirmed | P Draw(GEText) sums r,g,b to pick the shadow color; L GEText::GetFgColor, SetFgColor |
| 0x20 | 16 | `struct GERectangleLayout` | `textExtent` | lle-only | L GEText::GetTextExtent, ComputeTextExtent |
| 0x30 | 20 | `struct UWideStringLayout` | `text` | confirmed | P Draw(GEText) reads chars, length, capacity; L GEText::GetText, SetText |
| 0x44 | 4 | `struct GEFontLayout *` | `font` | confirmed | P Draw(GEText); L GEText::GetFont, SetFont |
| 0x48 | 4 | `uint32_t` | `alignment` | confirmed | P Draw(GEText): 1 centers, 2 right-aligns; L GEText::GetAlignment, SetAlignment |
| 0x4c | 1 | `uint8_t` | `isTransparent` | confirmed | P Draw(GEText) picks the DC background mode; L GEText::GetIsTransparent |

### GEBitmap (0x30 bytes, size confirmed)

Size evidence: operator new(0x30) in UIManager::UIManager and others.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 4 | `void *` | `vptr` | lle-only | L ctor (UReferenceCounter derived) |
| 0x4 | 4 | `uint32_t` | `refCount` | lle-only | L ctor starts it at 1 |
| 0x8 | 8 | `struct GE2DPointLayout` | `location` | lle-only | L GEBitmap::GetLocation, SetLocation |
| 0x10 | 4 | `struct GEVertexArrayLayout *` | `vertexArray` | confirmed | P Draw(GEBitmap) reads and rewrites its four vertices; L GEBitmap::GetVertexArray |
| 0x14 | 4 | `struct GETextureLayout **` | `textures` | confirmed | P Draw(GEBitmap) indexes it by screen; L GEBitmap::GetTexture, SetTexture |
| 0x18 | 4 | `float` | `textureHeight` | lle-only | L GEBitmap::SetTexture, GetHeight |
| 0x1c | 4 | `float` | `textureWidth` | lle-only | L GEBitmap::SetTexture, GetWidth |
| 0x20 | 4 | `float` | `rotation` | lle-only | L GEBitmap::GetRotation, SetRotation |
| 0x24 | 4 | `float` | `xScale` | lle-only | L GEBitmap::GetXScale |
| 0x28 | 4 | `float` | `yScale` | lle-only | L GEBitmap::GetYScale |
| 0x2c | 4 | `uint32_t` | `screenCount` | lle-only | L GEBitmap ctor: length of textures |

### GELine (0xc bytes, size confirmed)

Size evidence: operator= and the constructors cover exactly 0x0..0xb.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 4 | `struct GEVertexArrayLayout *` | `vertexArray` | confirmed | P Draw(GELine) reads it; L GELine ctor |
| 0x4 | 4 | `struct GETextureLayout **` | `textures` | confirmed | P Draw(GELine) indexes it by screen; L GELine::SetTexture |
| 0x8 | 4 | `uint32_t` | `screenCount` | lle-only | L GELine ctor: length of textures |

### GEPoint (0xc bytes, size confirmed)

Size evidence: the constructors cover exactly 0x0..0xb.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 4 | `struct GEVertexArrayLayout *` | `vertexArray` | confirmed | P Draw(GEPoint) reads it; L GEPoint ctor |
| 0x4 | 4 | `struct GETextureLayout **` | `textures` | confirmed | P Draw(GEPoint) indexes it by screen; L GEPoint ctor |
| 0x8 | 4 | `uint32_t` | `screenCount` | lle-only | L GEPoint ctor: length of textures |

### GERhombus (0x18 bytes, size confirmed)

Size evidence: operator new(0x18) in UIListBox constructors.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 4 | `struct GEVertexArrayLayout *` | `vertexArray` | confirmed | P Draw(GERhombus) reads it; L GERhombus ctor |
| 0x4 | 16 | `struct GERectangleLayout` | `rectangle` | lle-only | L GERhombus::SetRectangle |
| 0x14 | 4 | `float` | `rotation` | lle-only | L GERhombus::GetRotation, SetRotation |

### GEWindow (0x1c bytes, size confirmed)

Size evidence: the constructor covers 0x0..0x18 and the next object member starts at 0x1c.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 4 | `void *` | `hwnd` | confirmed | P Capture, CreateAWindow; L GEWindow::GetOSRepresentation |
| 0x4 | 8 | `struct GE2DPointLayout` | `clientOrigin` | confirmed | P CreateAWindow writes it; L GEWindow::GetClientOrigin |
| 0xc | 4 | `uint32_t` | `width` | confirmed | P ActivateScreen; L GEWindow::GetWidth, Resize |
| 0x10 | 4 | `uint32_t` | `height` | confirmed | P ActivateScreen; L GEWindow::GetHeight, Resize |
| 0x14 | 4 | `uint32_t` | `style` | confirmed | P CreateAWindow writes it; L GEWindow ctor (fourth argument), Resize tests it against 1 |
| 0x18 | 1 | `uint8_t` | `ownsWindow` | guess | P CreateAWindow writes it; L ctor (fifth argument), ~GEWindow reads it; meaning inferred |

### GETextureManager (0x1c bytes, size confirmed)

Size evidence: operator new(0x1c) in the executable (push 0x1c before the constructor call).

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 2 | `uint8_t[2]` | `flags` | guess | L ctor writes two bytes |
| 0x4 | 4 | `void *` | `textureListHead` | lle-only | MSVC 6 std::list: head at 0x4, size at 0x8; L LoadTexture, RemoveAllTextures |
| 0x8 | 4 | `uint32_t` | `textureCount` | lle-only | L GETextureManager::GetTextureCount |
| 0xc | 4 | `void *` | `criticalSection` | lle-only | a TSCriticalSection; L LoadTexture |
| 0x10 | 4 | `void *` | `path` | lle-only | an FSPath; L GETextureManager::GetTexturePath |
| 0x14 | 4 | `struct GEScreenLayout *` | `screen` | confirmed | P ActivateRenderingSurface writes it; L GETextureManager::GetScreen, SetScreen |
| 0x18 | 4 | `uint32_t` | `memoryFootprint` | lle-only | L GETextureManager::GetMemoryFootprint |

### GEScreen (0xc bytes, size confirmed)

Size evidence: the base-class copy constructor copies 0x4 and 0x8; DX7Screen adds its own fields behind.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 4 | `void *` | `vptr` | lle-only | L ctor sets the GEScreen vftable |
| 0x4 | 4 | `struct GETextureManagerLayout *` | `textureManager` | confirmed | P DX7Screen constructor writes it; L GEScreen::GetTextureManager, SetTextureManager |
| 0x8 | 4 | `uint32_t` | `rasterizerIndex` | confirmed | P DX7Screen constructor writes it; L GEScreen::GetRasterizerIndex, SetRasterizerIndex |

### GEVideoMode (0x14 bytes, size confirmed)

Size evidence: the plugin's mode vector divides by 0x14 (GetModeCount); DX7Screen's constructor and EnumVideoModeCallback fill the five fields; the engine receives a reference through GEScreen::GetMode and GetCurrentMode.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 4 | `uint32_t` | `width` | plugin-only | P EnumVideoModeCallback (dwWidth); the constructor and Test32BitSupport compare it |
| 0x4 | 4 | `uint32_t` | `height` | plugin-only | P EnumVideoModeCallback (dwHeight) |
| 0x8 | 4 | `uint32_t` | `refreshRate` | plugin-only | P EnumVideoModeCallback (dwRefreshRate, 0 when the driver does not say) |
| 0xc | 4 | `uint32_t` | `bitsPerPixel` | plugin-only | P EnumVideoModeCallback (RGB bit count); the constructor matches the current mode by it |
| 0x10 | 4 | `uint32_t` | `pixelFormat` | plugin-only | P EnumVideoModeCallback (index into the plugin's pixel format table, 14 when none matches); SetMode reads the bit count through it |

### GESurface (0x8 bytes, size confirmed)

Size evidence: the base constructor writes 0x0 and 0x4; DX7Surface adds its own fields from 0x8.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 4 | `void *` | `vptr` | lle-only | L ctor sets the GESurface vftable |
| 0x4 | 4 | `uint32_t` | `refCount` | confirmed | P DX7Surface constructor writes it; L GESurface ctor starts it at 1 |

### GESurfaceDescription (0x24 bytes, size plugin-only)

Size evidence: the engine builds one and passes it to GEScreen::CreateSurface; DX7Screen::CreateSurface copies nine dwords of it and DX7Surface's constructor keeps that copy at 0x8.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 4 | `uint32_t` | `type` | plugin-only | P CreateSurface selects the DirectDraw caps by it (0 primary, 1 video memory, 2 flipping primary, 3 depth buffer, 4 system memory, 5 texture, 6 mipmapped texture; 7 is the plugin's mark of a flip chain's back buffer) |
| 0x4 | 4 | `uint32_t` | `pixelFormat` | plugin-only | P CreateSurface indexes its pixel format table with it (types 3: 12 and 13 ask for any depth buffer format) |
| 0x8 | 4 | `uint32_t` | `volatility` | plugin-only | P CreateSurface (types 5 and 6: non-zero adds the dynamic hint); GetSurfaceVolatility |
| 0xc | 4 | `uint32_t` | `width` | plugin-only | P CreateSurface (dwWidth), GetWidth |
| 0x10 | 4 | `uint32_t` | `height` | plugin-only | P CreateSurface (dwHeight), GetHeight |
| 0x14 | 4 | `uint32_t` | `backBufferCount` | plugin-only | P CreateSurface (type 2: dwBackBufferCount and the length of the flip chain) |
| 0x18 | 4 | `uint32_t` | `mipMapCount` | plugin-only | P CreateSurface (type 6: dwMipMapCount and the length of the mipmap chain) |
| 0x1c | 4 | `uint32_t` | `pixelDepth` | plugin-only | P CreateSurface writes it for fallback depth buffer formats; GetPixelDepth, GetAllocatedSize, CopyWithFilter |
| 0x20 | 1 | `uint8_t` | `is3DRenderable` | plugin-only | P CreateSurface (adds DDSCAPS_3DDEVICE); GetIs3DRenderable |
| 0x21 | 1 | `uint8_t` | `isTransparencyEnabled` | plugin-only | P CreateSurface (adds DDSD_CKSRCBLT); GetIsTransparencyEnabled |
| 0x22 | 2 | `uint8_t[2]` | `pad22` | guess | padding to the dword boundary; copied with the rest |

### GEMovie (0x8 bytes, size confirmed)

Size evidence: the base constructor writes 0x0 and 0x4.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 4 | `void *` | `vptr` | lle-only | L ctor sets the GEMovie vftable |
| 0x4 | 4 | `uint32_t` | `refCount` | confirmed | P DX7Movie constructor writes it; L GEMovie ctor starts it at 1 |

### GEVertexArray (0x4 bytes, size confirmed)

Size evidence: the base constructors write only the vptr.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 4 | `void *` | `vptr` | lle-only | L GEVertexArray ctor |

### GEPolygonArray (0x4 bytes, size confirmed)

Size evidence: the base constructors write only the vptr.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 4 | `void *` | `vptr` | lle-only | L GEPolygonArray ctor |

### StdVector (0x10 bytes, size confirmed)

Size evidence: MSVC 6 std::vector: an allocator word, then first, last, end; GERasterizer.screens, DX7Rasterizer.screenRecords and the GETerrainMesh vectors all use it.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 4 | `uint32_t` | `allocator` | guess | the empty allocator occupies a word |
| 0x4 | 4 | `void *` | `first` | confirmed | P Deactivate, Draw(GETerrainMesh), TransformToCamera iterate it; L GERasterizer::GetScreenCount, GETerrainMesh::GetNumVisibleTileRows |
| 0x8 | 4 | `void *` | `last` | confirmed | same: (last - first) / element size is the count |
| 0xc | 4 | `void *` | `end` | lle-only | capacity end |

### StdMap (0xc bytes, size confirmed)

Size evidence: MSVC 6 std::map as a member of GETerrainMesh: a comparator word, the head node, the node count.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 4 | `uint32_t` | `comparator` | guess | the empty comparator occupies a word |
| 0x4 | 4 | `void *` | `head` | confirmed | P Draw(GETerrainMesh) walks the nodes from head->left (node: left, parent, right, key at 0xc, value at 0x10) |
| 0x8 | 4 | `uint32_t` | `size` | lle-only | node count |

### GERasterizer (0x68 bytes, size confirmed)

Size evidence: the copy constructor ends with screenId at 0x64; DX7Rasterizer adds its fields from 0x68.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 4 | `void *` | `vptr` | lle-only | L ctor sets the GERasterizer vftable |
| 0x4 | 12 | `struct GEColorLayout` | `ambientColor` | confirmed | P SetAmbientColor writes it; L GERasterizer::GetAmbientColor |
| 0x10 | 12 | `struct GEColorLayout` | `computedAmbientColor` | confirmed | P Light reads it as the base color of the terrain lighting; L GERasterizer::GetComputedAmbientColor (ambientColor * ambientIntensity) |
| 0x1c | 16 | `struct StdVectorLayout` | `screens` | confirmed | P everywhere: this->screens.first[screen] is the screen record; L GERasterizer::GetScreenCount, AddValidScreen, Deactivate |
| 0x2c | 4 | `struct GEFontLayout *` | `font` | lle-only | L GERasterizer ctor (Arial Bold 9) |
| 0x30 | 4 | `struct GETextLayout *` | `text` | lle-only | L GERasterizer ctor |
| 0x34 | 4 | `uint32_t` | `unknown34` | guess | L copy constructor copies it |
| 0x38 | 4 | `void *` | `criticalSection` | lle-only | a TSCriticalSection; L ctor |
| 0x3c | 4 | `float` | `ambientIntensity` | confirmed | P SetAmbientColor; L GERasterizer::GetAmbientIntensity, SetAmbientIntensity |
| 0x40 | 12 | `float[3]` | `waterRipple` | confirmed | P TransformToCamera reads the three floats; L GERasterizer::SetWaterRippleEffect |
| 0x4c | 4 | `uint32_t` | `culling` | confirmed | P ActivateScreen; L GERasterizer::GetCulling, SetCulling |
| 0x50 | 1 | `uint8_t` | `polyAreaProfiling` | lle-only | L GERasterizer::GetIsPolyAreaProfiling |
| 0x51 | 1 | `uint8_t` | `activated` | lle-only | L GERasterizer::GetIsActivated, Activate, Deactivate |
| 0x54 | 16 | `struct UStringLayout` | `name` | lle-only | L GERasterizer::GetName |
| 0x64 | 4 | `uint32_t` | `screenId` | lle-only | L GERasterizer::GetScreenID, SetScreenID |

### GERScreenState (0x340 bytes, size confirmed)

Size evidence: operator new(0x340) in GERasterizer::AddValidScreen; the record derives from GEViewport (name inferred, the engine has no symbol for it).

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 648 | `struct GEViewportLayout` | `viewport` | confirmed | P Draw(GETerrainMesh), DrawWater, SetViewport read the viewport through the record; L GERasterizer::GetViewport returns the record |
| 0x288 | 12 | `struct GEColorLayout` | `fogColor` | confirmed | P SetFogColor; L GERasterizer::SetFogColor, GetFogAttributes |
| 0x294 | 4 | `uint32_t` | `fogType` | lle-only | L GERasterizer::SetFogType, GetFogAttributes |
| 0x298 | 4 | `float` | `fogValue` | lle-only | L GERasterizer::SetFogValue, GetFogAttributes |
| 0x29c | 4 | `float` | `fogStart` | confirmed | P SetFogRange; L GERasterizer::SetFogRange |
| 0x2a0 | 4 | `float` | `fogEnd` | confirmed | P SetFogRange; L GERasterizer::SetFogRange |
| 0x2a4 | 4 | `float` | `fogInverseRange` | lle-only | L GERasterizer::SetFogRange |
| 0x2a8 | 4 | `struct GEScreenLayout *` | `screen` | confirmed | P ActivateScreen, ShowScene; L GERasterizer::GetScreen, GetTextureManager (through it) |
| 0x2ac | 4 | `struct GESurfaceLayout *` | `primarySurface` | confirmed | P ActivateRenderingSurface, ShowScene; L GERasterizer::GetPrimarySurface |
| 0x2b0 | 4 | `struct GESurfaceLayout *` | `secondarySurface` | confirmed | P ActivateScreen, ShowScene; L GERasterizer::GetSecondarySurface |
| 0x2b4 | 4 | `struct GESurfaceLayout *` | `renderTarget` | confirmed | P SetRenderTarget; L GERasterizer::GetRenderTarget |
| 0x2b8 | 4 | `struct GETextureLayout *` | `currentTexture` | confirmed | P SetTexture caches the bound texture and its reference here |
| 0x2bc | 4 | `struct GEWindowLayout *` | `window` | confirmed | P ActivateScreen; L GERasterizer::GetWindow |
| 0x2c0 | 4 | `float` | `alphaTestRefValue` | confirmed | P SetAlphaTestingRefValue; L GERasterizer::GetAlphaTestingRefValue |
| 0x2c4 | 4 | `float` | `polyAreaAccumulator` | confirmed | P ShowScene; L GERasterizer::AddToDrawnPolyArea |
| 0x2c8 | 4 | `float` | `polyAreaPerFrame` | confirmed | P ShowScene; L GERasterizer::GetDrawnPolyAreaPerFrame |
| 0x2cc | 4 | `float` | `polyAreaPerSecond` | confirmed | P ShowScene; L GERasterizer::GetDrawnPolyAreaPerSecond |
| 0x2d0 | 4 | `float` | `polysPerFrame` | confirmed | P ShowScene; L GERasterizer::GetDrawnPolysPerFrame |
| 0x2d4 | 4 | `float` | `polysPerSecond` | confirmed | P ShowScene; L GERasterizer::GetDrawnPolysPerSecond |
| 0x2d8 | 4 | `float` | `framesPerSecond` | confirmed | P ShowScene; L GERasterizer::GetFramesPerSecond |
| 0x2dc | 4 | `float` | `scenePolysPerFrame` | confirmed | P ShowScene; L GERasterizer::GetScenePolysPerFrame |
| 0x2e0 | 4 | `float` | `scenePolysPerSecond` | confirmed | P ShowScene; L GERasterizer::GetScenePolysPerSecond |
| 0x2e4 | 4 | `float` | `textureChangesPerFrame` | confirmed | P ShowScene; L GERasterizer::GetTotalTextureChangesPerFrame |
| 0x2e8 | 4 | `uint32_t` | `alphaTesting` | confirmed | P SetAlphaTesting; L GERasterizer::GetAlphaTesting |
| 0x2ec | 4 | `uint32_t` | `alphaTestCompare` | confirmed | P SetAlphaTestingCompareState; L GERasterizer::GetAlphaTestingCompareState |
| 0x2f0 | 4 | `uint32_t` | `antiAliasing` | confirmed | P SetAntiAliasing; L GERasterizer::GetAntiAliasing |
| 0x2f4 | 4 | `uint32_t` | `blendSource` | confirmed | P SetBlending; L GERasterizer::GetBlending |
| 0x2f8 | 4 | `uint32_t` | `blendDest` | confirmed | P SetBlending; L GERasterizer::GetBlending |
| 0x2fc | 4 | `uint32_t` | `colorKeying` | confirmed | P SetColorKeying; L GERasterizer::GetColorKeying |
| 0x300 | 4 | `uint32_t` | `dithering` | confirmed | P SetDithering; L GERasterizer::GetDithering |
| 0x304 | 4 | `uint32_t` | `drawStyle` | confirmed | P SetDrawStyle; L GERasterizer::GetDrawStyle |
| 0x308 | 4 | `uint32_t` | `fog` | confirmed | P SetFog; L GERasterizer::GetFog |
| 0x30c | 4 | `uint32_t` | `lighting` | confirmed | P SetLighting; L GERasterizer::GetLighting |
| 0x310 | 4 | `uint32_t` | `projectionMode` | confirmed | P Set3DMode; L GERasterizer::SetProjectionMode |
| 0x314 | 4 | `uint32_t` | `shading` | confirmed | P SetShading; L GERasterizer::GetShading |
| 0x318 | 4 | `uint32_t` | `shadingColorSource` | confirmed | P SetShadingColorSource; L GERasterizer::SetShadingColorSource |
| 0x31c | 4 | `uint32_t` | `specular` | confirmed | P SetSpecular; L GERasterizer::GetSpecular |
| 0x320 | 4 | `uint32_t` | `zBuffer` | confirmed | P SetZBuffer; L GERasterizer::GetZBuffer |
| 0x324 | 4 | `uint32_t` | `zCompare` | confirmed | P SetZCompare; L GERasterizer::GetZCompare |
| 0x328 | 4 | `uint32_t` | `drawnPolyCount` | confirmed | P ShowScene; L GERasterizer::AddToDrawnPolyCount |
| 0x32c | 4 | `uint32_t` | `frameCounter` | confirmed | P ShowScene counts frames and recomputes the statistics every 12; L ctor clears it |
| 0x330 | 4 | `uint32_t` | `scenePolyCount` | confirmed | P Draw(GEPolygonArray), DrawWater add the polygon count; ShowScene |
| 0x334 | 4 | `uint32_t` | `lastStatisticsTime` | confirmed | P ShowScene stores TMGetTime here |
| 0x338 | 4 | `uint32_t` | `textureChangeCount` | confirmed | P SetTexture increments it; ShowScene |
| 0x33c | 1 | `uint8_t` | `hasTexture` | confirmed | P SetTexture; L ctor clears it |
| 0x33d | 1 | `uint8_t` | `activated` | confirmed | P ActivateScreen, Deactivate; L GERasterizer::GetIsScreenActivated |
| 0x33e | 1 | `uint8_t` | `presentFlag` | confirmed | P ShowScene passes it to the primary surface flip; L ctor sets it to 1 |

### GETMRenderVertex (0x24 bytes, size confirmed)

Size evidence: GETerrainMesh builds 10201 of them per array with a 0x24 stride; the constructor initializes position and color.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 12 | `struct GE3DPointLayout` | `position` | confirmed | P Draw(GETerrainMesh), DrawTerrainMaterial copy it into the D3D vertex; L constructor zeroes it |
| 0xc | 12 | `struct GEColorLayout` | `color` | confirmed | P Light writes it, DrawTerrainMaterial packs it; L constructor sets it to 1.0 |
| 0x18 | 4 | `float` | `intensity` | plugin-only | P DrawTerrainMaterial multiplies the color by it; the constructor leaves it unset |
| 0x1c | 4 | `float` | `unknown1c` | guess | P TransformToCamera reads it in the water pass |
| 0x20 | 4 | `uint32_t` | `clipFlags` | plugin-only | P TransformToCamera stores the frustum outcode bits (1,2,4,8,0x10,0x20) here |

### GETMPolygon (0x30 bytes, size confirmed)

Size evidence: GETerrainMesh builds 12000 + 5000 of them with a 0x30 stride; the constructor initializes the three uv points at 0x18.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 12 | `void *[3]` | `vertices` | confirmed | P DrawTerrainMaterial follows the three GETMRenderVertex pointers |
| 0xc | 12 | `float[3]` | `alpha` | plugin-only | P DrawTerrainMaterial turns each into the vertex alpha |
| 0x18 | 24 | `struct GE2DPointLayout[3]` | `uv` | confirmed | P DrawTerrainMaterial copies them to the vertex texture coordinates; L the constructor initializes them |

### GETMMaterial (0x34 bytes, size confirmed)

Size evidence: GetTerrainMaterialCount divides the byte size of the vector by 0x34.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 4 | `struct GETextureLayout *` | `texture` | confirmed | P Draw(GETerrainMesh), DrawWater bind it with SetTexture |
| 0x4 | 16 | `struct StdVectorLayout` | `unknownVector` | guess | P TransformToCamera resets it |
| 0x14 | 16 | `struct StdVectorLayout` | `polygons` | confirmed | P Draw(GETerrainMesh), DrawWater pass its address to DrawTerrainMaterial (a vector of GETMPolygon pointers) |
| 0x24 | 16 | `struct StdVectorLayout` | `overlayPolygons` | guess | P DrawWater, Draw(GETerrainMesh) draw it as a second pass; name inferred |

### GETMPoint (0xc bytes, size confirmed)

Size evidence: GETerrainMesh::GetZ indexes the point row with a 0xc stride.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 4 | `float` | `z` | confirmed | P Light, TransformToCamera; L GETerrainMesh::GetZ |
| 0x4 | 4 | `struct GEVectorLayout *` | `normal` | confirmed | P Light dots the light directions with it; L GETerrainMesh::GetVertexNormal |
| 0x8 | 1 | `uint8_t` | `colorIndex` | confirmed | P Light indexes GETerrainMesh.colorTable; L GETerrainMesh::GetColorTableIndex |

### GETMPointRow (0xc bytes, size confirmed)

Size evidence: GETerrainMesh::GETMPointRow constructor and Initialize cover 0x0..0xb; rows are 0xc apart.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 4 | `uint32_t` | `firstIndex` | confirmed | P Light subtracts it from x; L GETerrainMesh::GetIsValidPoint |
| 0x4 | 4 | `uint32_t` | `count` | lle-only | L GETerrainMesh::GetIsValidPoint, GETMPointRow::Initialize |
| 0x8 | 4 | `struct GETMPointLayout *` | `points` | confirmed | P Light, TransformToCamera; L GETerrainMesh::GetZ |

### GETMTile (0x4 bytes, size confirmed)

Size evidence: GETerrainMesh::GetIsWaterTile indexes a tile row with a 4-byte stride.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x2 | 1 | `uint8_t` | `textureSetId` | lle-only | L GETerrainMesh::GetTextureSetID |
| 0x3 | 1 | `uint8_t` | `waterInfo` | lle-only | L GETerrainMesh::GetWaterInfo, GetIsWaterTile |

### GETMTileRow (0xc bytes, size confirmed)

Size evidence: GETerrainMesh::GetIsValidTile indexes rows with a 0xc stride.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 4 | `uint32_t` | `firstIndex` | lle-only | L GETerrainMesh::GetIsValidTile |
| 0x4 | 4 | `uint32_t` | `count` | lle-only | L GETerrainMesh::GetIsValidTile |
| 0x8 | 4 | `struct GETMTileLayout *` | `tiles` | lle-only | L GETerrainMesh::GetTextureSetID |

### GETerrainMesh (0x181694 bytes, size guess)

Size evidence: at least 0x181692 (the last byte of waterPointVisibility); rounded up to a dword. It is embedded in another class, so no operator new(size) gives it directly.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 4 | `void *` | `vptr` | lle-only | L ctor sets the GETerrainMesh vftable |
| 0x4 | 12 | `struct StdMapLayout` | `materialMap` | confirmed | P Draw(GETerrainMesh) iterates it (node key = texture at 0xc, value = material index at 0x10); L GETerrainMesh::GetTerrainMaterialMap |
| 0x10 | 16 | `struct StdVectorLayout` | `materials` | confirmed | P TransformToCamera; L GETerrainMesh::GetTerrainMaterialList, GetTerrainMaterialCount (elements: GETMMaterial) |
| 0x20 | 16 | `struct StdVectorLayout` | `visibleRows` | confirmed | P Light, TransformToCamera; L GETerrainMesh::GetNumVisibleTileRows, GetVisibleTileRow (12-byte elements) |
| 0x30 | 16 | `struct StdVectorLayout` | `waterMaterials` | confirmed | P DrawWater, TransformToCamera; L GETerrainMesh::GetWaterMaterialList (elements: GETMMaterial) |
| 0x40 | 16 | `struct StdVectorLayout` | `registeredTextureSets` | lle-only | L GETerrainMesh::GetRegisteredTextureSet, RegisterTextureSet |
| 0x50 | 16 | `struct StdVectorLayout` | `colorSets` | lle-only | L GETerrainMesh::RegisterColorSet |
| 0x60 | 16 | `struct StdVectorLayout` | `edgeRenderVertices` | lle-only | L GETerrainMesh::GetEdgeRenderVertices |
| 0x70 | 16 | `struct StdVectorLayout` | `edgeWorldPoints` | confirmed | P TransformToCamera tests its element count; L GETerrainMesh::GetEdgeWorldPoints |
| 0x80 | 12 | `struct GEColorLayout` | `waterColor` | confirmed | P Light multiplies it by the light; L GETerrainMesh::GetWaterColor |
| 0x8c | 3072 | `struct GEColorLayout[256]` | `colorTable` | confirmed | P Light indexes it by GETMPoint.colorIndex; L GETerrainMesh::GetVertexColor |
| 0xc8c | 24 | `struct GE3DPlaneLayout` | `frontPlane` | lle-only | L GETerrainMesh::GetFrontPlane |
| 0xca4 | 24 | `struct GE3DPlaneLayout` | `backPlane` | lle-only | L GETerrainMesh::GetBackPlane |
| 0xcbc | 24 | `struct GE3DPlaneLayout` | `leftPlane` | lle-only | L GETerrainMesh::GetLeftPlane |
| 0xcd4 | 24 | `struct GE3DPlaneLayout` | `rightPlane` | lle-only | L GETerrainMesh::GetRightPlane |
| 0xcec | 576000 | `struct GETMPolygonLayout[12000]` | `polygons` | lle-only | L constructor initializes the uv points of 12000 polygons; the vectors in materials point into it |
| 0x8d6ec | 367236 | `struct GETMRenderVertexLayout[10201]` | `renderVertices` | confirmed | P Draw(GETerrainMesh), Light, TransformToCamera index it [x][y] with strides 0xe34 and 0x24; L GETerrainMesh::GetTerrainRenderVertexArray (101 x 101) |
| 0xe7170 | 240000 | `struct GETMPolygonLayout[5000]` | `waterPolygons` | lle-only | L constructor initializes the uv points of 5000 water polygons |
| 0x121af0 | 367236 | `struct GETMRenderVertexLayout[10201]` | `waterRenderVertices` | confirmed | P TransformToCamera, Light; L GETerrainMesh::GetWaterRenderVertexArray (101 x 101) |
| 0x17b574 | 4 | `void *` | `fractal` | lle-only | a U2DFractal; L constructor |
| 0x17b578 | 4 | `struct GETMPointRowLayout *` | `pointRows` | confirmed | P Light, TransformToCamera; L GETerrainMesh::GetZ, GetVertexNormal, GetColorTableIndex |
| 0x17b57c | 4 | `struct GETMTileRowLayout *` | `tileRows` | confirmed | P DrawTerrainHighlight; L GETerrainMesh::GetIsWaterTile, GetTextureSetID, ReferenceTile |
| 0x17b580 | 4 | `float` | `unknown17b580` | guess | L constructor sets -0.714; SetGlobalWaterState writes it |
| 0x17b584 | 4 | `float` | `waterVisiblePercentage` | confirmed | P TransformToCamera; L GETerrainMesh::GetVisibileWaterTilePercentage |
| 0x17b588 | 4 | `float` | `minimumZ` | lle-only | L GETerrainMesh::GetMinimumZ, SetZ |
| 0x17b58c | 4 | `float` | `maximumZ` | lle-only | L GETerrainMesh::GetMaximumZ, SetZ |
| 0x17b590 | 4 | `uint32_t` | `unknown17b590` | guess | L CreateFlatMesh, Save write it (mesh width?) |
| 0x17b594 | 4 | `uint32_t` | `unknown17b594` | guess | L CreateFlatMesh, Save write it (mesh height?) |
| 0x17b598 | 4 | `uint32_t` | `edgeLeft` | lle-only | L GETerrainMesh::GetIsLeftEdgeTile |
| 0x17b59c | 4 | `uint32_t` | `edgeTop` | lle-only | L GETerrainMesh::GetIsTopEdgeTile |
| 0x17b5a0 | 4 | `uint32_t` | `edgeRight` | lle-only | L GETerrainMesh::GetIsRightEdgeTile |
| 0x17b5a4 | 4 | `uint32_t` | `edgeBottom` | lle-only | L GETerrainMesh::GetIsBottomEdgeTile |
| 0x17c6ac | 4 | `uint32_t` | `visibleTileWidth` | confirmed | P DrawGridLines; L GETerrainMesh::GetTerrainVisibleTileDimensions |
| 0x17c6b0 | 4 | `uint32_t` | `visibleTileHeight` | confirmed | P DrawGridLines; L GETerrainMesh::GetTerrainVisibleTileDimensions |
| 0x17c6b4 | 4 | `uint32_t` | `visibleBottomLeftX` | confirmed | P DrawGridLines, Light; L GETerrainMesh::GetTerrainVisibleBottomLeftIndices |
| 0x17c6b8 | 4 | `uint32_t` | `visibleBottomLeftY` | confirmed | P DrawGridLines, Light; L GETerrainMesh::GetTerrainVisibleBottomLeftIndices |
| 0x17c6bc | 4 | `uint32_t` | `unknown17c6bc` | guess | L UpdateWaterAnimation writes it |
| 0x17c6c0 | 4 | `uint32_t` | `globalWaterSetId` | lle-only | L GETerrainMesh::GetGlobalWaterSetID |
| 0x17c6c4 | 4 | `uint32_t` | `unknown17c6c4` | guess | L SetWaterColorSet, SetGlobalWaterState |
| 0x17c6c8 | 4 | `uint32_t` | `unknown17c6c8` | guess | L constructor sets 0x50 |
| 0x17c6cc | 4 | `uint32_t` | `pointRowCount` | lle-only | L GETerrainMesh::GetIsValidPoint |
| 0x17c6d0 | 4 | `uint32_t` | `tileRowCount` | confirmed | P DrawTerrainHighlight; L GETerrainMesh::GetIsValidTile |
| 0x17c6d4 | 4 | `uint32_t` | `visiblePolyCount` | lle-only | L GETerrainMesh::GetVisiblePolyCount |
| 0x17c6d8 | 4 | `uint32_t` | `numVisibleTiles` | lle-only | L GETerrainMesh::GetNumVisibleTiles |
| 0x17c6dc | 4 | `uint32_t` | `createTime` | lle-only | L constructor stores TMGetTime |
| 0x17c6e0 | 10201 | `uint8_t[10201]` | `pointVisibility` | confirmed | P Light, TransformToCamera test it [x * 101 + y]; L GETerrainMesh::GetTerrainPointVisibilityArray |
| 0x17eeb9 | 10201 | `uint8_t[10201]` | `waterPointVisibility` | confirmed | P Light, TransformToCamera; L GETerrainMesh::GetWaterPointVisibilityArray |

### DX7ScreenRecord (0x74 bytes, size confirmed)

Size evidence: plugin-owned: operator new(0x74) in DX7Rasterizer::AddValidScreen (which leaves everything but 0x18-0x28 and 0x6d unset).

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 24 | `uint32_t[6]` | `viewport` | plugin-only | a D3DVIEWPORT7 (x, y, width, height, minZ, maxZ); P SetViewport fills it and passes the record itself to IDirect3DDevice7::SetViewport, Set2DMode/Set3DMode read the width and height |
| 0x18 | 4 | `void *` | `direct3d` | plugin-only | an IDirect3D7; P ActivateTheScreen stores it (a refused CreateDevice leaves it unreleased and unstored), Deactivate releases it |
| 0x1c | 4 | `void *` | `device` | plugin-only | an IDirect3DDevice7; P ActivateTheScreen creates it on the render target, Deactivate releases it, everywhere else through DX7Rasterizer.screenRecords |
| 0x20 | 4 | `struct GEScreenLayout *` | `screen` | plugin-only | P AddValidScreen stores the GEScreen; Deactivate calls its virtual slot 19 |
| 0x24 | 4 | `void *` | `vertexBuffer` | plugin-only | an IDirect3DVertexBuffer7 (write-only, FVF 0x152, 0x98a vertices); P CreateVertexBuffers (TnL), Deactivate releases it |
| 0x28 | 4 | `uint32_t` | `vertexCursor` | plugin-only | P LockVB/UnlockVB advance it by the vertices written |
| 0x2c | 4 | `uint32_t` | `textureStateDirty` | plugin-only | P SetTexture sets it |
| 0x30 | 48 | `uint32_t[12]` | `stageCache` | plugin-only | P SetTexture mirrors the GETStage words it last sent to the device |
| 0x60 | 4 | `uint32_t` | `blendSourceCache` | plugin-only | P SetBlending: the D3D source blend factor last sent is remembered by the engine's value (0..12), compared before every send |
| 0x64 | 4 | `uint32_t` | `blendDestCache` | plugin-only | P SetBlending: the same for the destination factor |
| 0x68 | 4 | `uint32_t` | `zEnableCache` | plugin-only | P SetZBuffer: the D3DRENDERSTATE_ZENABLE value last sent (0 or 1) |
| 0x6c | 1 | `uint8_t` | `unknown6c` | guess | P Set2DMode/Set3DMode test it, the DX7Rasterizer constructor path clears it |
| 0x6d | 1 | `uint8_t` | `sceneDrawn` | guess | P AddValidScreen and SetViewport set it, Draw(GETerrainMesh), DrawWater set it after drawing; Set2DMode/Set3DMode clear it |
| 0x6e | 1 | `uint8_t` | `zWriteCache` | plugin-only | P SetZBuffer: the D3DRENDERSTATE_ZWRITEENABLE value last sent |
| 0x6f | 1 | `uint8_t` | `blendEnabledCache` | plugin-only | P SetBlending: the D3DRENDERSTATE_ALPHABLENDENABLE value last sent |
| 0x70 | 1 | `uint8_t` | `canModulateTextureAlpha` | plugin-only | P GetCanModulateTextureAlpha returns it |
| 0x71 | 1 | `uint8_t` | `dirtyRectangleSupport` | plugin-only | P GetDirtyRectangleSupport returns it |

### DX7Rasterizer (0x7c bytes, size guess)

Size evidence: plugin-owned: base GERasterizer (0x68) plus a vector and a dword; the copy constructor ends at 0x78.

| offset | width | type | field | confidence | evidence |
|---|---|---|---|---|---|
| 0x0 | 104 | `struct GERasterizerLayout` | `base` | confirmed | the engine base class |
| 0x68 | 16 | `struct StdVectorLayout` | `screenRecords` | plugin-only | P AddValidScreen, Deactivate, DX7Rasterizer copy constructor; elements are DX7ScreenRecord pointers |
| 0x78 | 4 | `uint32_t` | `debugColor` | plugin-only | P SetDebugColor packs a colour into it (0xAARRGGBB, alpha 255); the constructor starts it at 0xff000040 |

## Not established

- `GETerrainMesh` total size: it is embedded in another class, so no `operator new(size)` gives it; at least 0x181692 bytes. The 0x34bc0 bytes at 0x58b2c and the 0x1104 bytes at 0x17b5a8 are not named by any accessor the plugin or the engine's exports use.
- `GEText.bgColor`, `GEViewport` plane contents and the internals of `GETMMaterial` (fields 0x4 and 0x24): the plugin reaches them only through x87 helper calls or whole-object passes.
- Plugin-owned objects whose layout the original chose and the engine never reads (`DX7Screen`, `DX7Surface`, `DX7Movie`, `DX7PolygonArray`, `DX7StandardVertexArray`, `DX7StdColorVertexArray`): a reimplementation may lay them out differently, so they are not part of this contract. `DX7Rasterizer` and `DX7ScreenRecord` are listed because the vtable shape and the engine-side base class make their first fields visible.
- Classes of the `GEFontBitmap`/Bink path (`DX7Movie`, `BINK`) are not covered (the three intro movies only).

