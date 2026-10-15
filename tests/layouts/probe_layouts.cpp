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
