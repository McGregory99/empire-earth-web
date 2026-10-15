/*
 * Value types: GEColor, GE2DPoint, GE3DPoint, GEVector, GERectangle, GETextureCoordinate,
 * GEBoundingBox, GE3DPlane and GETransformation.  The plugin reads their floats inline.
 */
#include "oracle.h"

static void color(void)
{
    GEColorLayout c = {0.25f, 0.5f, 0.75f};
    CHECK_F(((fn_f_v)oracle_export("?GetRed@GEColor@@QBEMXZ"))(&c), 0.25f, "GEColor.r is the engine's red (offset 0x0)");
    CHECK_F(((fn_f_v)oracle_export("?GetGreen@GEColor@@QBEMXZ"))(&c), 0.5f, "GEColor.g is the engine's green (offset 0x4)");
    CHECK_F(((fn_f_v)oracle_export("?GetBlue@GEColor@@QBEMXZ"))(&c), 0.75f, "GEColor.b is the engine's blue (offset 0x8)");
    ((fn_v_f)oracle_export("?SetBlue@GEColor@@QAEXM@Z"))(&c, 4.0f);
    CHECK_F(c.b, 4.0f, "GEColor::SetBlue writes GEColor.b");
    CHECK(sizeof(c) == 0xc, "GEColor is three floats");
}

static void point2(void)
{
    GE2DPointLayout p = {0.0f, 0.0f};
    ((fn_v_ff)oracle_export("?SetXY@GE2DPoint@@QAEXMM@Z"))(&p, 3.0f, 5.0f);
    CHECK(p.x == 3.0f && p.y == 5.0f, "GE2DPoint::SetXY writes x@0x0 and y@0x4");
    CHECK_F(((fn_f_v)oracle_export("?GetY@GE2DPoint@@QBEMXZ"))(&p), 5.0f, "GE2DPoint::GetY reads y");
}

static void point3(void)
{
    GE3DPointLayout p = {0.0f, 0.0f, 0.0f};
    ((fn_v_fff)oracle_export("?SetXYZ@GE3DPoint@@QAEXMMM@Z"))(&p, 1.0f, 2.0f, 3.0f);
    CHECK(p.x == 1.0f && p.y == 2.0f && p.z == 3.0f, "GE3DPoint::SetXYZ writes x,y,z at 0x0,0x4,0x8");
    CHECK_F(((fn_f_v)oracle_export("?GetZ@GE3DPoint@@QBEMXZ"))(&p), 3.0f, "GE3DPoint::GetZ reads z");

    GEVectorLayout v = {3.0f, 4.0f, 12.0f};
    CHECK_F(((fn_f_v)oracle_export("?GetMagnitudeSquared@GEVector@@QBEMXZ"))(&v), 169.0f, "GEVector::GetMagnitudeSquared reads x,y,z");
    CHECK_F(((fn_f_v)oracle_export("?GetMagnitude@GEVector@@QBEMXZ"))(&v), 13.0f, "GEVector::GetMagnitude reads x,y,z");
}

static void rectangle(void)
{
    /* bottom@0 left@4 right@8 top@0xc: y grows downwards, so the bottom is the larger y. */
    GERectangleLayout r = {40.0f, 10.0f, 30.0f, 5.0f};
    CHECK_F(((fn_f_v)oracle_export("?GetBottom@GERectangle@@QBEMXZ"))(&r), 40.0f, "GERectangle.bottom is at 0x0");
    CHECK_F(((fn_f_v)oracle_export("?GetLeft@GERectangle@@QBEMXZ"))(&r), 10.0f, "GERectangle.left is at 0x4");
    CHECK_F(((fn_f_v)oracle_export("?GetRight@GERectangle@@QBEMXZ"))(&r), 30.0f, "GERectangle.right is at 0x8");
    CHECK_F(((fn_f_v)oracle_export("?GetTop@GERectangle@@QBEMXZ"))(&r), 5.0f, "GERectangle.top is at 0xc");
    CHECK_F(((fn_f_v)oracle_export("?GetWidth@GERectangle@@QBEMXZ"))(&r), 20.0f, "GERectangle::GetWidth is right - left");
    CHECK_F(((fn_f_v)oracle_export("?GetHeight@GERectangle@@QBEMXZ"))(&r), 35.0f, "GERectangle::GetHeight is bottom - top");
}

static void texture_coordinate(void)
{
    GETextureCoordinateLayout t = {0.125f, 0.875f};
    CHECK_F(((fn_f_v)oracle_export("?GetU@GETextureCoordinate@@QBEMXZ"))(&t), 0.125f, "GETextureCoordinate.u is at 0x0");
    CHECK_F(((fn_f_v)oracle_export("?GetV@GETextureCoordinate@@QBEMXZ"))(&t), 0.875f, "GETextureCoordinate.v is at 0x4");
}

static void bounding_box_and_plane(void)
{
    GEBoundingBoxLayout b;
    GE3DPointLayout lo = {1.0f, 2.0f, 3.0f};
    GE3DPointLayout hi = {4.0f, 5.0f, 6.0f};
    memset(&b, 0, sizeof(b));
    ((fn_v_p)oracle_export("?SetBottomLeftFront@GEBoundingBox@@QAEXABVGE3DPoint@@@Z"))(&b, &lo);
    ((fn_v_p)oracle_export("?SetTopRightBack@GEBoundingBox@@QAEXABVGE3DPoint@@@Z"))(&b, &hi);
    CHECK(b.bottomLeftFront.x == 1.0f && b.bottomLeftFront.z == 3.0f, "GEBoundingBox::SetBottomLeftFront writes the corner at 0x0");
    CHECK(b.topRightBack.x == 4.0f && b.topRightBack.z == 6.0f, "GEBoundingBox::SetTopRightBack writes the corner at 0xc");
    b.width = 7.0f;
    b.height = 8.0f;
    b.depth = 9.0f;
    CHECK_F(((fn_f_v)oracle_export("?GetWidth@GEBoundingBox@@QBEMXZ"))(&b), 7.0f, "GEBoundingBox.width is at 0x18");
    CHECK_F(((fn_f_v)oracle_export("?GetHeight@GEBoundingBox@@QBEMXZ"))(&b), 8.0f, "GEBoundingBox.height is at 0x1c");
    CHECK_F(((fn_f_v)oracle_export("?GetDepth@GEBoundingBox@@QBEMXZ"))(&b), 9.0f, "GEBoundingBox.depth is at 0x20");

    GE3DPlaneLayout plane;
    memset(&plane, 0, sizeof(plane));
    CHECK(((fn_p_v)oracle_export("?GetPoint@GE3DPlane@@QBEABVGE3DPoint@@XZ"))(&plane) == (void *)&plane.point, "GE3DPlane.point is at 0x0");
    CHECK(((fn_p_v)oracle_export("?GetNormal@GE3DPlane@@QBEABVGEVector@@XZ"))(&plane) == (void *)&plane.normal, "GE3DPlane.normal is at 0xc");
}

static void transformation(void)
{
    GETransformationLayout t;
    GE3DPointLayout position;
    GE3DPointLayout in = {1.0f, 2.0f, 3.0f};
    GE3DPointLayout out = {0.0f, 0.0f, 0.0f};
    int i;
    for (i = 0; i < 16; i++) {
        t.m[i] = (float)(i + 1);
    }
    /* Row-major 4x4: element (row, col) is m[row * 4 + col]; the translation is the last column. */
    CHECK_F(((fn_f_uu)oracle_export("?GetElement@GETransformation@@QBEMKK@Z"))(&t, 1, 2), t.m[6], "GETransformation::GetElement(row, col) reads m[row * 4 + col]");
    ((fn_v_p)oracle_export("?GetPosition@GETransformation@@QBEXAAVGE3DPoint@@@Z"))(&t, &position);
    CHECK(position.x == t.m[3] && position.y == t.m[7] && position.z == t.m[11], "GETransformation::GetPosition reads m[3], m[7], m[11] (offsets 0xc, 0x1c, 0x2c)");
    ((fn_v_pp)oracle_export("?Apply@GETransformation@@QBEXABVGE3DPoint@@AAV2@@Z"))(&t, &in, &out);
    CHECK(out.x == 1.0f * t.m[0] + 2.0f * t.m[1] + 3.0f * t.m[2] + t.m[3], "GETransformation::Apply: x' = row 0 . (x,y,z,1)");
    CHECK(out.y == 1.0f * t.m[4] + 2.0f * t.m[5] + 3.0f * t.m[6] + t.m[7], "GETransformation::Apply: y' = row 1 . (x,y,z,1)");
    CHECK(out.z == 1.0f * t.m[8] + 2.0f * t.m[9] + 3.0f * t.m[10] + t.m[11], "GETransformation::Apply: z' = row 2 . (x,y,z,1)");
    ((fn_v_v)oracle_export("?MakeIdentity@GETransformation@@QAEXXZ"))(&t);
    CHECK(t.m[0] == 1.0f && t.m[5] == 1.0f && t.m[10] == 1.0f && t.m[15] == 1.0f && t.m[1] == 0.0f && t.m[3] == 0.0f,
          "GETransformation::MakeIdentity writes all sixteen floats (0x0..0x3c)");
    CHECK(sizeof(t) == 0x40, "GETransformation is a 4x4 float matrix");
}

void oracle_value_types(void)
{
    color();
    point2();
    point3();
    rectangle();
    texture_coordinate();
    bounding_box_and_plane();
    transformation();
}
