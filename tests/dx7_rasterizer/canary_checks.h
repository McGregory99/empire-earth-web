/*
 * Layout canaries (task P2.5), driven through the null rasterizer's vtable with fake engine objects.
 * Included by harness.c, after CHECK, counter(), slot() and THISCALL.  Every fake object is built from
 * ../../src/engine_abi/ge_layouts.h; the class vftables are the REAL ones of the engine, so the RTTI
 * the canaries read is the engine's own.
 *
 * Operations that return a status answer EEWEB_LAYOUT_VIOLATION for a refused object (the normal
 * answer is EEWEB_NOT_SUPPORTED); the void ones are observed through the violation counter.  In both
 * cases EEWEB_COUNTER_LAST_VIOLATION says why.
 */
#include "ge_layouts.h"

#include <stdlib.h>

typedef long (THISCALL *CanaryOp3)(void *self, void *object, unsigned long a, unsigned long b);
typedef long (THISCALL *CanaryModelOp)(void *self, void *object, unsigned long a, float b, unsigned char c);
typedef long (THISCALL *CanaryTerrainOp)(void *self, void *object, unsigned long a, unsigned char b);
typedef long (THISCALL *CanaryAddLight)(void *self, void *light, unsigned long a, unsigned long *id);
typedef void (THISCALL *CanaryVoidOp)(void *self, void *object, unsigned long a);
typedef long (THISCALL *CanaryTextOp)(void *self, void *text, void *surface);
typedef void (THISCALL *CanaryCameraOp)(void *self, void *mesh, void *viewport, float a, unsigned char b, unsigned char c);

static void *vftable(const char *class_name)
{
    char name[96];
    snprintf(name, sizeof name, "??_7%s@@6B@", class_name);
    return (void *)GetProcAddress(GetModuleHandleA("Low-Level Engine.dll"), name);
}

static GEMaterialLayout *fake_material(void)
{
    GEMaterialLayout *m = (GEMaterialLayout *)calloc(1, sizeof *m);
    m->vptr = vftable("GEMaterial");
    m->refCount = 1;
    m->textureCount = 1;
    m->lodCount = 1;
    m->textures = (struct GETextureLayout **)calloc(1, sizeof(void *));
    m->polygonStart = (uint32_t *)calloc(1, sizeof(uint32_t));
    m->polygonLength = (uint32_t *)calloc(1, sizeof(uint32_t));
    m->opacity = 1.0f;
    return m;
}

static GETextureLayout *fake_texture(void)
{
    GETextureLayout *t = (GETextureLayout *)calloc(1, sizeof *t);
    t->vptr = vftable("GETexture");
    t->refCount = 1;
    t->stage.texture = t;
    t->baseTexture = t;
    t->surface = (struct GESurfaceLayout *)calloc(1, 0x100);
    return t;
}

static GEModelLayout *fake_model(void)
{
    GEModelLayout *m = (GEModelLayout *)calloc(1, sizeof *m);
    m->vptr = vftable("GEModel");
    m->refCount = 1;
    m->lodCount = 1;
    m->materialCount = 1;
    m->materials = (struct GEMaterialLayout **)calloc(1, sizeof(void *));
    m->materials[0] = fake_material();
    m->polygonArrays = (struct GEPolygonArrayLayout **)calloc(1, sizeof(void *));
    m->vertexArrays = (struct GEVertexArrayLayout **)calloc(1, sizeof(void *));
    m->graphicsScale = 1.0f;
    m->opacity = 1.0f;
    return m;
}

static GETerrainMeshLayout *fake_mesh(void)
{
    GETerrainMeshLayout *m = (GETerrainMeshLayout *)calloc(1, sizeof *m + 64);
    m->vptr = vftable("GETerrainMesh");
    m->pointRows = (struct GETMPointRowLayout *)calloc(1, sizeof(GETMPointRowLayout));
    m->pointRowCount = 1;
    return m;
}

static GETextLayout *fake_text(void)
{
    GETextLayout *t = (GETextLayout *)calloc(1, sizeof *t);
    GEFontLayout *font = (GEFontLayout *)calloc(1, sizeof *font);
    font->refCount = 1;
    font->osFont = (void *)font;
    t->font = font;
    t->text.chars = (uint8_t *)"abc";
    t->text.length = 3;
    t->text.capacity = 16;
    return t;
}

static void check_layout_canaries(const char *label, void *obj, void *const *vtable)
{
    CanaryModelOp draw_model;
    CanaryTerrainOp draw_terrain;
    CanaryAddLight add_light;
    CanaryVoidOp set_texture, light_terrain;
    CanaryTextOp draw_text;
    CanaryCameraOp transform_to_camera;
    void *fn;

    fn = slot(vtable, EEWEB_SLOT_GERasterizer_Draw_7);
    memcpy(&draw_model, &fn, sizeof fn);
    fn = slot(vtable, EEWEB_SLOT_GERasterizer_Draw_2);
    memcpy(&draw_terrain, &fn, sizeof fn);
    fn = slot(vtable, EEWEB_SLOT_GERasterizer_AddLight);
    memcpy(&add_light, &fn, sizeof fn);
    fn = slot(vtable, EEWEB_SLOT_GERasterizer_SetTexture);
    memcpy(&set_texture, &fn, sizeof fn);
    fn = slot(vtable, EEWEB_SLOT_GERasterizer_Light);
    memcpy(&light_terrain, &fn, sizeof fn);
    fn = slot(vtable, EEWEB_SLOT_GERasterizer_Draw_4);
    memcpy(&draw_text, &fn, sizeof fn);
    fn = slot(vtable, EEWEB_SLOT_GERasterizer_TransformToCamera);
    memcpy(&transform_to_camera, &fn, sizeof fn);

    /* ---- GEModel: Draw(GEModel *, ...) ----------------------------------------------------------- */
    GEModelLayout *model = fake_model();
    long before = counter(obj, EEWEB_COUNTER_LAYOUT_VIOLATIONS);
    CHECK(draw_model(obj, model, 0, 1.0f, 0) == EEWEB_NOT_SUPPORTED, "%s: canary: a well-formed GEModel is accepted", label);
    CHECK(counter(obj, EEWEB_COUNTER_LAYOUT_VIOLATIONS) == before, "%s: canary: ... and counts no violation", label);

    CHECK(draw_model(obj, NULL, 0, 1.0f, 0) == EEWEB_LAYOUT_VIOLATION && counter(obj, EEWEB_COUNTER_LAST_VIOLATION) == EEWEB_LAYOUT_NULL,
          "%s: canary: a null GEModel is refused (null)", label);
    CHECK(draw_model(obj, (char *)model + 1, 0, 1.0f, 0) == EEWEB_LAYOUT_VIOLATION && counter(obj, EEWEB_COUNTER_LAST_VIOLATION) == EEWEB_LAYOUT_MISALIGNED,
          "%s: canary: a misaligned GEModel is refused (misaligned)", label);
    CHECK(draw_model(obj, (void *)0x1000, 0, 1.0f, 0) == EEWEB_LAYOUT_VIOLATION && counter(obj, EEWEB_COUNTER_LAST_VIOLATION) == EEWEB_LAYOUT_UNREADABLE,
          "%s: canary: an unreadable GEModel is refused (unreadable)", label);
    void *good_vptr = model->vptr;
    model->vptr = vftable("GETexture");
    CHECK(draw_model(obj, model, 0, 1.0f, 0) == EEWEB_LAYOUT_VIOLATION && counter(obj, EEWEB_COUNTER_LAST_VIOLATION) == EEWEB_LAYOUT_WRONG_CLASS,
          "%s: canary: a GETexture where a GEModel is expected is refused (wrong class)", label);
    model->vptr = good_vptr;
    model->lodCount = 0;
    CHECK(draw_model(obj, model, 0, 1.0f, 0) == EEWEB_LAYOUT_VIOLATION && counter(obj, EEWEB_COUNTER_LAST_VIOLATION) == EEWEB_LAYOUT_BAD_FIELD,
          "%s: canary: a GEModel without a level of detail is refused (bad field)", label);
    model->lodCount = 1;
    struct GEMaterialLayout **good_materials = model->materials;
    model->materials = (struct GEMaterialLayout **)0x10; /* a shifted field reads as a small integer */
    CHECK(draw_model(obj, model, 0, 1.0f, 0) == EEWEB_LAYOUT_VIOLATION && counter(obj, EEWEB_COUNTER_LAST_VIOLATION) == EEWEB_LAYOUT_BAD_CHAIN,
          "%s: canary: a GEModel whose material table is garbage is refused (bad chain)", label);
    model->materials = good_materials;
    good_materials[0]->vptr = vftable("GETexture");
    CHECK(draw_model(obj, model, 0, 1.0f, 0) == EEWEB_LAYOUT_VIOLATION && counter(obj, EEWEB_COUNTER_LAST_VIOLATION) == EEWEB_LAYOUT_WRONG_CLASS,
          "%s: canary: a GEModel holding a non-material is refused (wrong class)", label);
    good_materials[0]->vptr = vftable("GEMaterial");
    CHECK(draw_model(obj, model, 0, 1.0f, 0) == EEWEB_NOT_SUPPORTED, "%s: canary: the repaired GEModel is accepted again", label);

    /* ---- GETexture: SetTexture(GETexture *, screen), void --------------------------------------- */
    GETextureLayout *texture = fake_texture();
    before = counter(obj, EEWEB_COUNTER_LAYOUT_VIOLATIONS);
    set_texture(obj, texture, 0);
    set_texture(obj, NULL, 0);
    CHECK(counter(obj, EEWEB_COUNTER_LAYOUT_VIOLATIONS) == before, "%s: canary: a well-formed GETexture, and a null texture (unbinding), are accepted", label);
    texture->vptr = vftable("GEMaterial");
    set_texture(obj, texture, 0);
    CHECK(counter(obj, EEWEB_COUNTER_LAYOUT_VIOLATIONS) == before + 1 && counter(obj, EEWEB_COUNTER_LAST_VIOLATION) == EEWEB_LAYOUT_WRONG_CLASS,
          "%s: canary: a GEMaterial where a GETexture is expected is refused", label);
    texture->vptr = vftable("GETexture");
    texture->baseTexture = (struct GETextureLayout *)0x10;
    set_texture(obj, texture, 0);
    CHECK(counter(obj, EEWEB_COUNTER_LAYOUT_VIOLATIONS) == before + 2 && counter(obj, EEWEB_COUNTER_LAST_VIOLATION) == EEWEB_LAYOUT_BAD_CHAIN,
          "%s: canary: a GETexture whose baseTexture is garbage is refused (bad chain)", label);
    texture->baseTexture = texture;
    texture->hasChanged = 7;
    set_texture(obj, texture, 0);
    CHECK(counter(obj, EEWEB_COUNTER_LAYOUT_VIOLATIONS) == before + 3 && counter(obj, EEWEB_COUNTER_LAST_VIOLATION) == EEWEB_LAYOUT_BAD_FIELD,
          "%s: canary: a GETexture whose hasChanged byte is not a flag is refused (bad field)", label);

    /* ---- GETerrainMesh: Draw(GETerrainMesh &, ...) ---------------------------------------------- */
    GETerrainMeshLayout *mesh = fake_mesh();
    CHECK(draw_terrain(obj, mesh, 0, 0) == EEWEB_NOT_SUPPORTED, "%s: canary: a well-formed GETerrainMesh is accepted", label);
    mesh->pointRows = (struct GETMPointRowLayout *)0x20;
    CHECK(draw_terrain(obj, mesh, 0, 0) == EEWEB_LAYOUT_VIOLATION && counter(obj, EEWEB_COUNTER_LAST_VIOLATION) == EEWEB_LAYOUT_BAD_CHAIN,
          "%s: canary: a GETerrainMesh whose point rows are garbage is refused (bad chain)", label);
    mesh->pointRows = NULL;
    mesh->pointRowCount = 0;
    mesh->vptr = vftable("GEModel");
    CHECK(draw_terrain(obj, mesh, 0, 0) == EEWEB_LAYOUT_VIOLATION && counter(obj, EEWEB_COUNTER_LAST_VIOLATION) == EEWEB_LAYOUT_WRONG_CLASS,
          "%s: canary: a GEModel where a GETerrainMesh is expected is refused (wrong class)", label);
    mesh->vptr = vftable("GETerrainMesh");

    /* ---- GELight: AddLight(GELight &, ...) ------------------------------------------------------ */
    GELightLayout light;
    unsigned long id = 0;
    memset(&light, 0, sizeof light);
    CHECK(add_light(obj, &light, 0, &id) == EEWEB_NOT_SUPPORTED, "%s: canary: a well-formed GELight is accepted", label);
    uint32_t nan = 0x7fc00000u;
    memcpy(&light.computedColor.r, &nan, 4);
    CHECK(add_light(obj, &light, 0, &id) == EEWEB_LAYOUT_VIOLATION && counter(obj, EEWEB_COUNTER_LAST_VIOLATION) == EEWEB_LAYOUT_BAD_FIELD,
          "%s: canary: a GELight with a NaN color is refused (bad field)", label);

    /* ---- GEText: Draw(GEText *, GESurface *) ---------------------------------------------------- */
    GETextLayout *text = fake_text();
    CHECK(draw_text(obj, text, NULL) == EEWEB_NOT_SUPPORTED, "%s: canary: a well-formed GEText is accepted", label);
    text->text.length = 99; /* longer than the capacity */
    CHECK(draw_text(obj, text, NULL) == EEWEB_LAYOUT_VIOLATION && counter(obj, EEWEB_COUNTER_LAST_VIOLATION) == EEWEB_LAYOUT_BAD_FIELD,
          "%s: canary: a GEText whose length exceeds its capacity is refused (bad field)", label);
    text->text.length = 3;
    text->font = (struct GEFontLayout *)0x30;
    CHECK(draw_text(obj, text, NULL) == EEWEB_LAYOUT_VIOLATION && counter(obj, EEWEB_COUNTER_LAST_VIOLATION) == EEWEB_LAYOUT_BAD_CHAIN,
          "%s: canary: a GEText with a garbage font is refused (bad chain)", label);

    /* ---- GETerrainMesh + GEViewport: TransformToCamera, void ------------------------------------ */
    GEViewportLayout viewport;
    memset(&viewport, 0, sizeof viewport);
    before = counter(obj, EEWEB_COUNTER_LAYOUT_VIOLATIONS);
    transform_to_camera(obj, mesh, &viewport, 1.0f, 0, 0);
    CHECK(counter(obj, EEWEB_COUNTER_LAYOUT_VIOLATIONS) == before, "%s: canary: a well-formed mesh and viewport are accepted", label);
    memcpy(&viewport.fieldOfView, &nan, 4);
    transform_to_camera(obj, mesh, &viewport, 1.0f, 0, 0);
    CHECK(counter(obj, EEWEB_COUNTER_LAYOUT_VIOLATIONS) == before + 1 && counter(obj, EEWEB_COUNTER_LAST_VIOLATION) == EEWEB_LAYOUT_BAD_FIELD,
          "%s: canary: a GEViewport with a NaN field of view is refused (bad field)", label);
    (void)light_terrain;
}
