/*
 * GEModel, GEViewport, GEText (with GEFont and the strings), GEBitmap, GELine, GEPoint, GERhombus,
 * GEWindow, GETextureManager and the small abstract bases GEScreen, GESurface, GEMovie, GEVertexArray
 * and GEPolygonArray.  Where the engine can build the object (a constructor that needs no running
 * rasterizer) it does; otherwise the layout fills the object and the engine's accessors read it.
 */
#include "oracle.h"

#include <string.h>

/* ---- GEModel --------------------------------------------------------------------------------- */

static void model(void)
{
    GEModelLayout m, sub, parent;
    GEMaterialLayout *materials[2];
    GEModelLayout *submodels[1];
    float radii[3] = {1.5f, 2.5f, 3.5f};
    GETransformationLayout transforms[4];
    GE3DPointLayout tagPoints[2];
    GE3DPointLayout *tagPointLists[2];
    GEBoundingBoxLayout boxes[3];
    UStringLayout names[2];
    struct GEPolygonArrayLayout *polygon_arrays[2] = {(struct GEPolygonArrayLayout *)0x3000, (struct GEPolygonArrayLayout *)0x3100};
    struct GEVertexArrayLayout *vertex_arrays[2] = {(struct GEVertexArrayLayout *)0x4000, (struct GEVertexArrayLayout *)0x4100};
    memset(&m, 0, sizeof(m));
    memset(transforms, 0, sizeof(transforms));
    memset(boxes, 0, sizeof(boxes));
    memset(names, 0, sizeof(names));
    materials[0] = (GEMaterialLayout *)0x1000;
    materials[1] = (GEMaterialLayout *)0x2000;
    submodels[0] = &sub;
    tagPointLists[0] = &tagPoints[0];
    tagPointLists[1] = &tagPoints[1];

    m.graphicsScale = 0.5f;
    m.opacity = 0.25f;
    m.fractionalFramePosition = 0.75f;
    m.currentFrame = 2;
    m.nextFrame = 3;
    m.subModelCount = 5;
    m.frameCount = 6;
    m.materialCount = 7;
    m.polygonCount = 8;
    m.vertexCount = 9;
    m.tagPointCount = 10;
    m.lodCount = 2;
    m.materials = materials;
    m.subModels = submodels;
    m.parentModel = &parent;
    m.boundingRadii = radii;
    m.internalTransforms = transforms;
    m.boundingBoxes = boxes;
    m.graphicsBoundingBox = &boxes[1];
    m.tagPoints = tagPoints;
    m.tagPointLists = tagPointLists;
    m.tagPointNames = names;
    m.polygonArrays = polygon_arrays;
    m.vertexArrays = vertex_arrays;
    m.clipToPlane[2] = 1;

    CHECK_F(((fn_f_v)oracle_export("?GetGraphicsScale@GEModel@@QBEMXZ"))(&m), 0.5f, "GEModel.graphicsScale is at 0x128");
    CHECK_F(((fn_f_v)oracle_export("?GetModelOpacity@GEModel@@QBEMXZ"))(&m), 0.25f, "GEModel.opacity is at 0x12c");
    CHECK_F(((fn_f_v)oracle_export("?GetFractionalFramePosition@GEModel@@QBEMXZ"))(&m), 0.75f, "GEModel.fractionalFramePosition is at 0x130");
    CHECK_F(((fn_u_v)oracle_export("?GetGraphicsCurrentFrame@GEModel@@QBEKXZ"))(&m), 2, "GEModel.currentFrame is at 0x134");
    CHECK_F(((fn_u_v)oracle_export("?GetGraphicsNextFrame@GEModel@@QBEKXZ"))(&m), 3, "GEModel.nextFrame is at 0x138");
    CHECK_F(((fn_u_v)oracle_export("?GetSubModelCount@GEModel@@QBEKXZ"))(&m), 5, "GEModel.subModelCount is at 0x13c");
    CHECK_F(((fn_u_v)oracle_export("?GetFrameCount@GEModel@@QBEKXZ"))(&m), 6, "GEModel.frameCount is at 0x140");
    CHECK_F(((fn_u_v)oracle_export("?GetMaterialCount@GEModel@@QBEKXZ"))(&m), 7, "GEModel.materialCount is at 0x144");
    CHECK_F(((fn_u_v)oracle_export("?GetPolygonCount@GEModel@@QBEKXZ"))(&m), 8, "GEModel.polygonCount is at 0x148");
    CHECK_F(((fn_u_v)oracle_export("?GetTagPointCount@GEModel@@QBEKXZ"))(&m), 10, "GEModel.tagPointCount is at 0x150");
    CHECK_F(((fn_u_v)oracle_export("?GetLODCount@GEModel@@QBEKXZ"))(&m), 2, "GEModel.lodCount is at 0x154");
    CHECK(((fn_p_u)oracle_export("?GetMaterial@GEModel@@QBEPAVGEMaterial@@K@Z"))(&m, 1) == (void *)0x2000, "GEModel.materials is at 0x108");
    CHECK(((fn_p_u)oracle_export("?GetSubModel@GEModel@@QBEPAV1@K@Z"))(&m, 0) == (void *)&sub, "GEModel.subModels is at 0xfc");
    CHECK(((fn_p_v)oracle_export("?GetParentModel@GEModel@@QBEPAV1@XZ"))(&m) == (void *)&parent, "GEModel.parentModel is at 0xf8");
    CHECK(((fn_p_u)oracle_export("?GetPolygonArray@GEModel@@QAEPAVGEPolygonArray@@K@Z"))(&m, 1) == (void *)0x3100, "GEModel.polygonArrays is at 0x100");
    CHECK(((fn_p_u)oracle_export("?GetVertexArray@GEModel@@QAEPAVGEVertexArray@@K@Z"))(&m, 1) == (void *)0x4100, "GEModel.vertexArrays is at 0x104");
    CHECK_F(((fn_f_u)oracle_export("?GetBoundingRadius@GEModel@@QBEMK@Z"))(&m, 2), 3.5f, "GEModel.boundingRadii is at 0x124");
    CHECK(((fn_p_u)oracle_export("?GetBoundingBox@GEModel@@QBEABVGEBoundingBox@@K@Z"))(&m, 2) == (void *)&boxes[2], "GEModel.boundingBoxes is at 0x110 (stride 0x24)");
    CHECK(((fn_p_v)oracle_export("?GetGraphicsBoundingBox@GEModel@@QBEABVGEBoundingBox@@XZ"))(&m) == (void *)&boxes[1], "GEModel.graphicsBoundingBox is at 0x114");
    CHECK(((fn_p_u)oracle_export("?GetInternalTransform@GEModel@@QBEABVGETransformation@@K@Z"))(&m, 3) == (void *)&transforms[3], "GEModel.internalTransforms is at 0xf0 (stride 0x40)");
    CHECK(((fn_p_v)oracle_export("?GetGraphicsInternalTransform@GEModel@@QBEABVGETransformation@@XZ"))(&m) == (void *)&transforms[2], "GEModel.internalTransforms is indexed by currentFrame");
    CHECK(((fn_p_u)oracle_export("?GetTagPoint@GEModel@@QBEABVGE3DPoint@@K@Z"))(&m, 1) == (void *)&tagPoints[1], "GEModel.tagPoints is at 0x118");
    CHECK(((fn_p_u)oracle_export("?GetTagPointName@GEModel@@QBEABVUString@@K@Z"))(&m, 1) == (void *)&names[1], "GEModel.tagPointNames is at 0x120 (stride 0x10)");
    CHECK(((fn_p_v)oracle_export("?GetCenterPoint@GEModel@@QBEABVGE3DPoint@@XZ"))(&m) == (void *)&m.centerPoint, "GEModel.centerPoint is at 0xc8");
    CHECK(((fn_p_v)oracle_export("?GetName@GEModel@@QBEABVUString@@XZ"))(&m) == (void *)&m.name, "GEModel.name (a UString) is at 0xd4");
    CHECK(((fn_p_v)oracle_export("?GetGraphicsTransform@GEModel@@QBEABVGETransformation@@XZ"))(&m) == (void *)&m.graphicsTransform, "GEModel.graphicsTransform is at 0x88");
    CHECK(((fn_p_v)oracle_export("?GetGraphicsPostModelTransform@GEModel@@QBEABVGETransformation@@XZ"))(&m) == (void *)&m.postModelTransform, "GEModel.postModelTransform is at 0x48");
    CHECK(((fn_p_v)oracle_export("?GetGraphicsPreModelTransform@GEModel@@QBEABVGETransformation@@XZ"))(&m) == (void *)&m.preModelTransform, "GEModel.preModelTransform is at 0x8");
    CHECK(((fn_b_u)oracle_export("?GetClipToPlane@GEModel@@QAE_NK@Z"))(&m, 2) == 1 && ((fn_b_u)oracle_export("?GetClipToPlane@GEModel@@QAE_NK@Z"))(&m, 1) == 0,
          "GEModel.clipToPlane is at 0x158");
    ((fn_v_ub)oracle_export("?SetClipToPlane@GEModel@@QAEXK_N@Z"))(&m, 4, 1);
    CHECK(m.clipToPlane[4] == 1, "GEModel::SetClipToPlane writes clipToPlane (0x158)");
}

/* ---- GEViewport ------------------------------------------------------------------------------ */

static void viewport(void)
{
    union {
        GEViewportLayout v;
        uint8_t bytes[sizeof(GEViewportLayout) + 16];
    } buf;
    GEViewportLayout *v = &buf.v;
    GERectangleLayout view = {400.0f, 100.0f, 500.0f, 20.0f};
    GERectangleLayout ortho = {-1.0f, -2.0f, 3.0f, 4.0f};
    GETransformationLayout camera;
    float near_far[2];
    oracle_guard_fill(&buf, sizeof(buf));
    ((fn_v_v)oracle_export("??0GEViewport@@QAE@XZ"))(v);
    CHECK(oracle_guard_intact(&buf, sizeof(GEViewportLayout), sizeof(buf)), "GEViewport's constructor stays inside 0x288 bytes (it has no vptr)");

    ((fn_v_f)oracle_export("?SetFieldOfView@GEViewport@@QAEXM@Z"))(v, 0.5f);
    CHECK_F(v->fieldOfView, 0.5f, "GEViewport::SetFieldOfView writes fieldOfView (0x25c)");
    ((fn_v_f)oracle_export("?SetFrontClippingDistance@GEViewport@@QAEXM@Z"))(v, 2.0f);
    ((fn_v_f)oracle_export("?SetRearClippingDistance@GEViewport@@QAEXM@Z"))(v, 900.0f);
    CHECK_F(v->frontClip, 2.0f, "GEViewport::SetFrontClippingDistance writes frontClip (0x264)");
    CHECK_F(v->rearClip, 900.0f, "GEViewport::SetRearClippingDistance writes rearClip (0x270)");
    ((fn_v_p)oracle_export("?SetViewRectangle@GEViewport@@QAEXABVGERectangle@@@Z"))(v, &view);
    CHECK(FEQ(v->viewRect.bottom, 400.0f) && FEQ(v->viewRect.left, 100.0f) && FEQ(v->viewRect.right, 500.0f) && FEQ(v->viewRect.top, 20.0f),
          "GEViewport::SetViewRectangle writes viewRect (0x10)");
    CHECK_F(v->halfWidth, 200.0f, "GEViewport::SetViewRectangle derives halfWidth (0x26c) from it");
    CHECK_F(v->halfHeight, 190.0f, "GEViewport::SetViewRectangle derives halfHeight (0x268) from it");
    CHECK_F(v->xCenter, 300.0f, "GEViewport::SetViewRectangle derives xCenter (0x274) from it");
    ((fn_v_p)oracle_export("?SetOrthographicRectangle@GEViewport@@QAEXABVGERectangle@@@Z"))(v, &ortho);
    CHECK(FEQ(v->orthographicRect.bottom, -1.0f) && FEQ(v->orthographicRect.top, 4.0f), "GEViewport::SetOrthographicRectangle writes orthographicRect (0x0)");
    ((fn_v_ff)oracle_export("?SetDepthRange@GEViewport@@QAEXMM@Z"))(v, 0.25f, 0.75f);
    CHECK(FEQ(v->depthNear, 0.25f) && FEQ(v->depthFar, 0.75f), "GEViewport::SetDepthRange writes depthNear (0x280) and depthFar (0x284)");
    ((fn_v_pp)oracle_export("?GetDepthRange@GEViewport@@QBEXAAM0@Z"))(v, &near_far[0], &near_far[1]);
    CHECK(FEQ(near_far[0], 0.25f) && FEQ(near_far[1], 0.75f), "GEViewport::GetDepthRange reads 0x280 and 0x284");
    ((fn_v_v)oracle_export("?MakeIdentity@GETransformation@@QAEXXZ"))(&camera);
    camera.m[3] = 5.0f;
    ((fn_v_p)oracle_export("?SetCamera@GEViewport@@QAEXABVGETransformation@@@Z"))(v, &camera);
    CHECK(FEQ(v->camera.m[3], 5.0f), "GEViewport::SetCamera writes camera (0xb0)");

    CHECK(((fn_p_v)oracle_export("?GetCamera@GEViewport@@QBEABVGETransformation@@XZ"))(v) == (void *)&v->camera, "GEViewport.camera is at 0xb0");
    CHECK(((fn_p_v)oracle_export("?GetCameraInverse@GEViewport@@QBEABVGETransformation@@XZ"))(v) == (void *)&v->cameraInverse, "GEViewport.cameraInverse is at 0xf0");
    CHECK(((fn_p_v)oracle_export("?GetOrthographicProjection@GEViewport@@QBEABVGETransformation@@XZ"))(v) == (void *)&v->orthographicProjection, "GEViewport.orthographicProjection is at 0x1b0");
    CHECK(((fn_p_v)oracle_export("?GetPerspectiveProjection@GEViewport@@QBEABVGETransformation@@XZ"))(v) == (void *)&v->perspectiveProjection, "GEViewport.perspectiveProjection is at 0x1f0");
    CHECK(((fn_p_v)oracle_export("?GetCameraLookAtVector@GEViewport@@QBEABVGEVector@@XZ"))(v) == (void *)&v->cameraLookAt, "GEViewport.cameraLookAt is at 0x230");
    CHECK(((fn_p_v)oracle_export("?GetViewRectangle@GEViewport@@QBEABVGERectangle@@XZ"))(v) == (void *)&v->viewRect, "GEViewport.viewRect is at 0x10");
    CHECK(((fn_p_v)oracle_export("?GetOrthographicRectangle@GEViewport@@QBEABVGERectangle@@XZ"))(v) == (void *)&v->orthographicRect, "GEViewport.orthographicRect is at 0x0");
    CHECK(((fn_p_v)oracle_export("?GetFrontPlane@GEViewport@@QBEABVGE3DPlane@@XZ"))(v) == (void *)&v->frontPlane, "GEViewport.frontPlane is at 0x20");
    CHECK(((fn_p_v)oracle_export("?GetRearPlane@GEViewport@@QBEABVGE3DPlane@@XZ"))(v) == (void *)&v->rearPlane, "GEViewport.rearPlane is at 0x38");
    CHECK(((fn_p_v)oracle_export("?GetLeftPlane@GEViewport@@QBEABVGE3DPlane@@XZ"))(v) == (void *)&v->leftPlane, "GEViewport.leftPlane is at 0x50");
    CHECK(((fn_p_v)oracle_export("?GetRightPlane@GEViewport@@QBEABVGE3DPlane@@XZ"))(v) == (void *)&v->rightPlane, "GEViewport.rightPlane is at 0x68");
    CHECK(((fn_p_v)oracle_export("?GetTopPlane@GEViewport@@QBEABVGE3DPlane@@XZ"))(v) == (void *)&v->topPlane, "GEViewport.topPlane is at 0x80");
    CHECK(((fn_p_v)oracle_export("?GetBottomPlane@GEViewport@@QBEABVGE3DPlane@@XZ"))(v) == (void *)&v->bottomPlane, "GEViewport.bottomPlane is at 0x98");
    v->yClippingPlane = 7.0f;
    v->aspectRatioMultiplier = 8.0f;
    v->fieldOfViewMultiplier = 9.0f;
    v->yCenter = 10.0f;
    CHECK_F(((fn_f_v)oracle_export("?GetYClippingPlane@GEViewport@@QBEMXZ"))(v), 7.0f, "GEViewport.yClippingPlane is at 0x27c");
    CHECK_F(((fn_f_v)oracle_export("?GetAspectRatioMultiplier@GEViewport@@QBEMXZ"))(v), 8.0f, "GEViewport.aspectRatioMultiplier is at 0x258");
    CHECK_F(((fn_f_v)oracle_export("?GetFieldOfViewMultiplier@GEViewport@@QBEMXZ"))(v), 9.0f, "GEViewport.fieldOfViewMultiplier is at 0x260");
    CHECK_F(((fn_f_v)oracle_export("?GetYCenter@GEViewport@@QBEMXZ"))(v), 10.0f, "GEViewport.yCenter is at 0x278");
    ((fn_v_v)oracle_export("??1GEViewport@@QAE@XZ"))(v);
}

/* ---- GEText, GEFont and the strings ---------------------------------------------------------- */

static void text(void)
{
    union {
        GETextLayout t;
        uint8_t bytes[sizeof(GETextLayout) + 16];
    } buf;
    GETextLayout *t = &buf.t;
    GEFontLayout font;
    UWideStringLayout str;
    GEColorLayout fg = {0.1f, 0.2f, 0.3f};
    GEColorLayout bg = {0.4f, 0.5f, 0.6f};
    GE2DPointLayout where = {12.0f, 34.0f};

    memset(&font, 0, sizeof(font));
    font.refCount = 5;
    font.osFont = (void *)0x6666;
    font.style = 0x10;
    font.quality = 3;
    font.size = 9;
    font.height = 14;
    CHECK(((fn_p_v)oracle_export("?GetOSFont@GEFont@@QBEPAUHFONT__@@XZ"))(&font) == (void *)0x6666, "GEFont.osFont is at 0x18");
    CHECK_F(((fn_u_v)oracle_export("?GetFontStyle@GEFont@@QBEKXZ"))(&font), 0x10, "GEFont.style is at 0x20");
    CHECK_F(((fn_u_v)oracle_export("?GetFontQuality@GEFont@@QBEKXZ"))(&font), 3, "GEFont.quality is at 0x1c");
    CHECK_F(((fn_u_v)oracle_export("?GetFontSize@GEFont@@QBEKXZ"))(&font), 9, "GEFont.size is at 0x28");
    CHECK_F(((fn_u_v)oracle_export("?GetHeight@GEFont@@QBEJXZ"))(&font), 14, "GEFont.height is at 0x2c");
    CHECK(((fn_p_v)oracle_export("?GetFontName@GEFont@@QBEABVUString@@XZ"))(&font) == (void *)&font.name, "GEFont.name (a UString) is at 0x8");

    ((fn_p_p)oracle_export("??0UWideString@@QAE@PBD@Z"))(&str, "hello");
    CHECK(str.length == 5 && memcmp(str.chars, "hello", 6) == 0, "UWideString: chars (0x4) and length (0x8) hold the text");
    CHECK(sizeof(UWideStringLayout) == 0x14, "UWideString is 0x14 bytes");

    oracle_guard_fill(&buf, sizeof(buf));
    ((fn_p_p)oracle_export("??0GEText@@QAE@PAVGEFont@@@Z"))(t, &font);
    CHECK(oracle_guard_intact(&buf, sizeof(GETextLayout), sizeof(buf)), "GEText's constructor stays inside 0x50 bytes (it has no vptr)");
    CHECK(t->font == &font && font.refCount == 6, "GEText.font is at 0x44 and the constructor takes a reference (font.refCount at 0x4)");
    ((fn_v_p)oracle_export("?SetText@GEText@@QAEXABVUWideString@@@Z"))(t, &str);
    CHECK(t->text.length == 5 && memcmp(t->text.chars, "hello", 6) == 0, "GEText::SetText fills text (0x30): chars at 0x34, length at 0x38");
    CHECK(((fn_p_v)oracle_export("?GetText@GEText@@QBEABVUWideString@@XZ"))(t) == (void *)&t->text, "GEText.text (a UWideString) is at 0x30");
    ((fn_v_p)oracle_export("?SetFgColor@GEText@@QAEXABVGEColor@@@Z"))(t, &fg);
    ((fn_v_p)oracle_export("?SetBgColor@GEText@@QAEXABVGEColor@@@Z"))(t, &bg);
    CHECK(FEQ(t->fgColor.r, 0.1f) && FEQ(t->fgColor.b, 0.3f), "GEText::SetFgColor writes fgColor (0x14)");
    CHECK(FEQ(t->bgColor.r, 0.4f) && FEQ(t->bgColor.b, 0.6f), "GEText::SetBgColor writes bgColor (0x8)");
    ((fn_v_p)oracle_export("?SetLocation@GEText@@QAEXABVGE2DPoint@@@Z"))(t, &where);
    CHECK(FEQ(t->location.x, 12.0f) && FEQ(t->location.y, 34.0f), "GEText::SetLocation writes location (0x0)");
    ((fn_v_u)oracle_export("?SetAlignment@GEText@@QAEXK@Z"))(t, 2);
    CHECK(t->alignment == 2, "GEText::SetAlignment writes alignment (0x48)");
    ((fn_v_ub)oracle_export("?SetIsTransparent@GEText@@QAEX_N@Z"))(t, 1);
    CHECK(t->isTransparent == 1, "GEText::SetIsTransparent writes isTransparent (0x4c)");
    CHECK(((fn_p_v)oracle_export("?GetFont@GEText@@QBEPAVGEFont@@XZ"))(t) == (void *)&font, "GEText.font is at 0x44");
    CHECK(((fn_p_v)oracle_export("?GetTextExtent@GEText@@QBEABVGERectangle@@XZ"))(t) == (void *)&t->textExtent, "GEText.textExtent is at 0x20");
    CHECK_F(((fn_u_v)oracle_export("?GetAlignment@GEText@@QBEKXZ"))(t), 2, "GEText.alignment is at 0x48");
    font.refCount = 5;
    ((fn_v_v)oracle_export("??1GEText@@QAE@XZ"))(t);
    ((fn_v_v)oracle_export("??1UWideString@@UAE@XZ"))(&str);
}

/* ---- GEBitmap and the other 2D objects ------------------------------------------------------- */

static void two_d(void)
{
    union {
        GEBitmapLayout b;
        uint8_t bytes[sizeof(GEBitmapLayout) + 16];
    } bitmap, copy;
    GETextureLayout *textures[2] = {0, 0};
    GELineLayout line;
    GETextureLayout texture;
    struct GETextureLayout *per_screen[1] = {0};
    GERhombusLayout rhombus;

    memset(&bitmap, 0, sizeof(bitmap));
    bitmap.b.vertexArray = (struct GEVertexArrayLayout *)0x7000;
    bitmap.b.textures = textures;
    bitmap.b.textureHeight = 8.0f;
    bitmap.b.textureWidth = 16.0f;
    bitmap.b.rotation = 0.5f;
    bitmap.b.xScale = 2.0f;
    bitmap.b.yScale = 3.0f;
    bitmap.b.screenCount = 2;
    textures[1] = (GETextureLayout *)0x7100;
    CHECK(((fn_p_v)oracle_export("?GetVertexArray@GEBitmap@@QAEPAVGEVertexArray@@XZ"))(&bitmap) == (void *)0x7000, "GEBitmap.vertexArray is at 0x10");
    CHECK(((fn_p_u)oracle_export("?GetTexture@GEBitmap@@QBEPAVGETexture@@K@Z"))(&bitmap, 1) == (void *)0x7100, "GEBitmap.textures is at 0x14");
    CHECK_F(((fn_f_v)oracle_export("?GetWidth@GEBitmap@@QBEMXZ"))(&bitmap), 32.0f, "GEBitmap.xScale (0x24) * textureWidth (0x1c) is its width");
    CHECK_F(((fn_f_v)oracle_export("?GetHeight@GEBitmap@@QBEMXZ"))(&bitmap), 24.0f, "GEBitmap.yScale (0x28) * textureHeight (0x18) is its height");
    CHECK_F(((fn_f_v)oracle_export("?GetRotation@GEBitmap@@QBEMXZ"))(&bitmap), 0.5f, "GEBitmap.rotation is at 0x20");
    CHECK(((fn_p_v)oracle_export("?GetLocation@GEBitmap@@QBEABVGE2DPoint@@XZ"))(&bitmap) == (void *)&bitmap.b.location, "GEBitmap.location is at 0x8");
    oracle_guard_fill(&copy, sizeof(copy));
    ((fn_p_p)oracle_export("??0GEBitmap@@QAE@ABV0@@Z"))(&copy, &bitmap);
    CHECK(oracle_guard_intact(&copy, sizeof(GEBitmapLayout), sizeof(copy)), "GEBitmap's copy constructor stops at 0x30: the object is 0x30 bytes");
    CHECK(memcmp(copy.bytes + 4, bitmap.bytes + 4, 0x2c) == 0, "GEBitmap's copy constructor copies every byte of 0x4..0x2f");

    memset(&line, 0, sizeof(line));
    memset(&texture, 0, sizeof(texture));
    line.textures = per_screen;
    line.screenCount = 1;
    ((fn_v_pu)oracle_export("?SetTexture@GELine@@QAEXPAVGETexture@@K@Z"))(&line, &texture, 0);
    CHECK(per_screen[0] == &texture && texture.refCount == 1, "GELine::SetTexture fills textures (0x4) and takes a reference (GETexture.refCount at 0x4)");

    memset(&rhombus, 0, sizeof(rhombus));
    rhombus.rotation = 0.125f;
    CHECK_F(((fn_f_v)oracle_export("?GetRotation@GERhombus@@QBEMXZ"))(&rhombus), 0.125f, "GERhombus.rotation is at 0x14");
}

/* ---- GEWindow, GETextureManager and the abstract bases --------------------------------------- */

static void window_manager_bases(void)
{
    union {
        GEWindowLayout w;
        uint8_t bytes[sizeof(GEWindowLayout) + 16];
    } window;
    GETextureManagerLayout manager;
    union {
        GEScreenLayout s;
        uint8_t bytes[sizeof(GEScreenLayout) + 16];
    } screen, screen_copy;
    union {
        GESurfaceLayout s;
        uint8_t bytes[sizeof(GESurfaceLayout) + 16];
    } surface;
    union {
        GEMovieLayout m;
        uint8_t bytes[sizeof(GEMovieLayout) + 16];
    } movie;
    union {
        GEVertexArrayLayout v;
        uint8_t bytes[sizeof(GEVertexArrayLayout) + 16];
    } vertices;
    union {
        GEPolygonArrayLayout p;
        uint8_t bytes[sizeof(GEPolygonArrayLayout) + 16];
    } polygons;

    oracle_guard_fill(&window, sizeof(window));
    ((fn_v_puuuu)oracle_export("??0GEWindow@@QAE@PAUHWND__@@KKK_N@Z"))(&window, (void *)0x1234, 640, 480, 7, 1);
    CHECK(oracle_guard_intact(&window, sizeof(GEWindowLayout), sizeof(window)), "GEWindow's constructor stays inside 0x1c bytes (it has no vptr)");
    CHECK(window.w.hwnd == (void *)0x1234 && window.w.width == 640 && window.w.height == 480 && window.w.style == 7 && window.w.ownsWindow == 1,
          "GEWindow's constructor fills hwnd (0x0), width (0xc), height (0x10), style (0x14) and the flag at 0x18");
    CHECK(((fn_p_v)oracle_export("?GetOSRepresentation@GEWindow@@QBEPAUHWND__@@XZ"))(&window) == (void *)0x1234, "GEWindow.hwnd is at 0x0");
    CHECK_F(((fn_u_v)oracle_export("?GetWidth@GEWindow@@QBEKXZ"))(&window), 640, "GEWindow.width is at 0xc");
    CHECK_F(((fn_u_v)oracle_export("?GetHeight@GEWindow@@QBEKXZ"))(&window), 480, "GEWindow.height is at 0x10");

    memset(&manager, 0, sizeof(manager));
    manager.screen = (struct GEScreenLayout *)0x8100;
    manager.memoryFootprint = 4096;
    manager.textureCount = 3;
    manager.path = (void *)0x8200;
    CHECK(((fn_p_v)oracle_export("?GetScreen@GETextureManager@@QBEPAVGEScreen@@XZ"))(&manager) == (void *)0x8100, "GETextureManager.screen is at 0x14");
    CHECK_F(((fn_u_v)oracle_export("?GetMemoryFootprint@GETextureManager@@QAEKXZ"))(&manager), 4096, "GETextureManager.memoryFootprint is at 0x18");
    CHECK_F(((fn_u_v)oracle_export("?GetTextureCount@GETextureManager@@QAEKXZ"))(&manager), 3, "GETextureManager.textureCount is at 0x8");
    CHECK(((fn_p_v)oracle_export("?GetTexturePath@GETextureManager@@QAEPAVFSPath@@XZ"))(&manager) == (void *)0x8200, "GETextureManager.path is at 0x10");
    ((fn_v_p)oracle_export("?SetScreen@GETextureManager@@QAEXPAVGEScreen@@@Z"))(&manager, (void *)0x8300);
    CHECK(manager.screen == (struct GEScreenLayout *)0x8300, "GETextureManager::SetScreen writes screen (0x14)");

    oracle_guard_fill(&screen, sizeof(screen));
    memset(&screen_copy, 0, sizeof(screen_copy));
    ((fn_v_v)oracle_export("??0GEScreen@@IAE@XZ"))(&screen);
    CHECK(screen.s.vptr == oracle_vftable("GEScreen"), "GEScreen.vptr (0x0) is the engine's GEScreen vftable");
    ((fn_v_p)oracle_export("?SetTextureManager@GEScreen@@QAEXPAVGETextureManager@@@Z"))(&screen, &manager);
    ((fn_v_u)oracle_export("?SetRasterizerIndex@GEScreen@@QAEXK@Z"))(&screen, 4);
    CHECK(screen.s.textureManager == &manager && screen.s.rasterizerIndex == 4, "GEScreen::SetTextureManager / SetRasterizerIndex write 0x4 and 0x8");
    CHECK(((fn_p_v)oracle_export("?GetTextureManager@GEScreen@@QBEPAVGETextureManager@@XZ"))(&screen) == (void *)&manager, "GEScreen.textureManager is at 0x4");
    CHECK_F(((fn_u_v)oracle_export("?GetRasterizerIndex@GEScreen@@QBEKXZ"))(&screen), 4, "GEScreen.rasterizerIndex is at 0x8");
    oracle_guard_fill(&screen_copy, sizeof(screen_copy));
    ((fn_p_p)oracle_export("??0GEScreen@@QAE@ABV0@@Z"))(&screen_copy, &screen);
    CHECK(oracle_guard_intact(&screen_copy, sizeof(GEScreenLayout), sizeof(screen_copy)) && screen_copy.s.rasterizerIndex == 4,
          "GEScreen's copy constructor stops at 0xc: the base object is 0xc bytes");

    oracle_guard_fill(&surface, sizeof(surface));
    ((fn_v_v)oracle_export("??0GESurface@@IAE@XZ"))(&surface);
    CHECK(surface.s.refCount == 1 && oracle_guard_intact(&surface, sizeof(GESurfaceLayout), sizeof(surface)), "GESurface: refCount (0x4) starts at 1, the base object is 8 bytes");
    CHECK(surface.s.vptr == oracle_vftable("GESurface"), "GESurface.vptr (0x0) is the engine's GESurface vftable");
    oracle_guard_fill(&movie, sizeof(movie));
    ((fn_v_v)oracle_export("??0GEMovie@@IAE@XZ"))(&movie);
    CHECK(movie.m.refCount == 1 && oracle_guard_intact(&movie, sizeof(GEMovieLayout), sizeof(movie)), "GEMovie: refCount (0x4) starts at 1, the base object is 8 bytes");
    oracle_guard_fill(&vertices, sizeof(vertices));
    ((fn_p_p)oracle_export("??0GEVertexArray@@QAE@ABV0@@Z"))(&vertices, &vertices);
    CHECK(oracle_guard_intact(&vertices, sizeof(GEVertexArrayLayout), sizeof(vertices)), "GEVertexArray: the base object is just the vptr (4 bytes)");
    oracle_guard_fill(&polygons, sizeof(polygons));
    ((fn_p_p)oracle_export("??0GEPolygonArray@@QAE@ABV0@@Z"))(&polygons, &polygons);
    CHECK(oracle_guard_intact(&polygons, sizeof(GEPolygonArrayLayout), sizeof(polygons)), "GEPolygonArray: the base object is just the vptr (4 bytes)");
}

void oracle_model_viewport_text(void)
{
    model();
    viewport();
    text();
    two_d();
    window_manager_bases();
}
