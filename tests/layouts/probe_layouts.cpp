// Compile-only probe: ge_layouts.h must be valid C++ for the plugin's target (clang, i686-pc-windows-msvc,
// the flags of RAST_CXXFLAGS) as well as C for the mingw-built oracle.  The sizes below are the ones the
// engine's own constructors and allocations give these objects (see src/engine_abi/LAYOUTS.md).
#include "ge_layouts.h"

static_assert(sizeof(GEColorLayout) == 0xc, "GEColor is three floats");
static_assert(sizeof(GE2DPointLayout) == 0x8, "GE2DPoint is two floats");
static_assert(sizeof(GE3DPointLayout) == 0xc, "GE3DPoint is three floats");
static_assert(sizeof(GEVectorLayout) == 0xc, "GEVector is three floats");
static_assert(sizeof(GERectangleLayout) == 0x10, "GERectangle is four floats");
static_assert(sizeof(GETransformationLayout) == 0x40, "GETransformation is a 4x4 float matrix");
static_assert(sizeof(GETextureCoordinateLayout) == 0x8, "GETextureCoordinate is two floats");
static_assert(sizeof(GEBoundingBoxLayout) == 0x24, "GEBoundingBox is two corners and three extents");
static_assert(sizeof(GE3DPlaneLayout) == 0x18, "GE3DPlane is a point and a normal");

static_assert(sizeof(GETextureLayout) == 0x6c, "operator new(0x6c) in GETextureManager::LoadTexture");
static_assert(sizeof(GETStageLayout) == 0x20, "GETexture::GetStage returns the 8-dword block at 0xc");
static_assert(sizeof(GEMaterialLayout) == 0x48, "operator new(0x48) in GEModel::AllocateInternalData");
static_assert(sizeof(GELightLayout) == 0x74, "GELight's copy constructor copies 0x0..0x73");
static_assert(sizeof(GEModelLayout) == 0x188, "operator new(0x188) in GEModelManager::LoadModel");
static_assert(sizeof(GEViewportLayout) == 0x288, "GEViewport's copy constructor copies 0x0..0x287");
static_assert(sizeof(GETextLayout) == 0x50, "operator new(0x50) in GERasterizer::GERasterizer");
static_assert(sizeof(GEBitmapLayout) == 0x30, "operator new(0x30) in UIManager::UIManager");
static_assert(sizeof(GEWindowLayout) == 0x1c, "GEWindow ends at 0x1b");
static_assert(sizeof(GETextureManagerLayout) == 0x1c, "push 0x1c before GETextureManager's constructor");
static_assert(sizeof(GERasterizerLayout) == 0x68, "GERasterizer's copy constructor ends with screenId at 0x64");
static_assert(sizeof(GERScreenStateLayout) == 0x340, "operator new(0x340) in GERasterizer::AddValidScreen");
static_assert(sizeof(GETMRenderVertexLayout) == 0x24, "the terrain render vertex arrays have a 0x24 stride");
static_assert(sizeof(GETMMaterialLayout) == 0x34, "GetTerrainMaterialCount divides by 0x34");
static_assert(sizeof(DX7ScreenRecordLayout) == 0x74, "operator new(0x74) in DX7Rasterizer::AddValidScreen");
