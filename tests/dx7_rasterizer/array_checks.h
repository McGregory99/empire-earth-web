/*
 * The polygon and vertex arrays of DX7Rasterizer (task P2.6.7a): CreatePolygonArray (slot 10) and CreateVertexArray (slot 11) and the
 * classes they build (DX7PolygonArray, DX7StandardVertexArray, DX7StdColorVertexArray), driven through the vtables of the objects
 * like the engine does.  Included by screen_checks.h after ring_checks.h and run from the screen probe.
 *
 * Same method as lifecycle_checks.h: every step is a line of observations compared with the literal of array_expected.inc, recorded
 * from the original DLL (EEWEB_LIFE_RECORD=1 prints them instead of checking).  What a step observes: the object's own words (the
 * vtable by kind, the index/vertex block's presence, the counts at 0x8 and 0xc), the size of the heap block behind the data pointer,
 * the raw bytes of the block (the test fills it with 0xa5 first, so a byte nobody wrote is visible), and what the virtual methods
 * answer (floats as their IEEE bits).  No fake Direct3D is involved: the arrays do not touch a device.
 */

typedef unsigned long(THISCALL *ArrGet)(void *self, unsigned long index);
typedef unsigned long(THISCALL *ArrSize)(void *self);
typedef void(THISCALL *ArrSet)(void *self, unsigned long index, unsigned long value);
typedef void(THISCALL *ArrSet4)(void *self, unsigned long index, unsigned long a, unsigned long b, unsigned long c);
typedef void *(THISCALL *ArrCreate1)(void *self, unsigned long count);
typedef void *(THISCALL *ArrCreate2)(void *self, unsigned long kind, unsigned long count);
typedef void(THISCALL *ArrOut)(void *self, float *out, unsigned long index);
typedef void(THISCALL *ArrIn)(void *self, const float *in, unsigned long index);
typedef float(THISCALL *ArrOpacity)(void *self, unsigned long index);
typedef void(THISCALL *ArrSetOpacity)(void *self, float value, unsigned long index);
typedef void(THISCALL *ArrSetVertex)(void *self, const float *color, const float *point, const float *normal, const float *coordinate, float opacity,
                                     unsigned long index);
typedef void(THISCALL *ArrCopy)(void *self, void *source);

/* Slots of GEPolygonArray and GEVertexArray (ge_slots.h). */
#define PA_SLOT(object, name) slot(*(void *const **)(object), EEWEB_SLOT_GEPolygonArray_##name)
#define VA_SLOT(object, name) slot(*(void *const **)(object), EEWEB_SLOT_GEVertexArray_##name)

static struct {
    void *vtable[3]; /* the vtables seen so far: polygon array, standard vertex array, standard colour vertex array */
    void *poly, *vs, *vc;
} g_arr;

static unsigned long arr_heap_size(const void *p)
{
    HANDLE heaps[64];
    DWORD n = GetProcessHeaps(64, heaps);
    for (DWORD i = 0; i < n && i < 64; i++) {
        SIZE_T size = HeapSize(heaps[i], 0, p);
        if (size != (SIZE_T)-1) {
            return (unsigned long)size;
        }
    }
    return 0xffffffffUL;
}

static int arr_live(const void *p)
{
    return p != NULL && arr_heap_size(p) != 0xffffffffUL;
}

static const char *arr_vt_name(void *object, int kind)
{
    void *vtable = *(void **)object;
    if (g_arr.vtable[kind] == NULL) {
        g_arr.vtable[kind] = vtable;
    }
    for (int i = 0; i < 3; i++) {
        if (g_arr.vtable[i] == vtable) {
            return i == 0 ? "poly" : i == 1 ? "std" : "stdcolor";
        }
    }
    return "other";
}

static void arr_words(char *out, size_t size, const void *p, size_t bytes)
{
    size_t used = strlen(out);
    for (size_t i = 0; i + 4 <= bytes && used + 10 < size; i += 4) {
        uint32_t word;
        memcpy(&word, (const char *)p + i, 4);
        used += (size_t)snprintf(out + used, size - used, "%s%x", i ? "," : "", (unsigned)word);
    }
}

/* The object's words and the block behind it.  `block_bytes` is how much of the block the array owns. */
static void arr_object(void *object, int kind, unsigned long block_bytes)
{
    const uint32_t *words = (const uint32_t *)object;
    char bytes[1024] = "";
    const void *data = (const void *)(uintptr_t)words[1];
    if (data != NULL) {
        arr_words(bytes, sizeof bytes, data, block_bytes);
    }
    life_extra("vt=%s data=%s w8=%u wc=%u heap=%lu bytes=%s", arr_vt_name(object, kind), data ? "yes" : "null", (unsigned)words[2],
               kind == 0 ? (unsigned)words[3] : 0u, data ? arr_heap_size(data) : 0, bytes);
}

static void arr_fill(void *object, unsigned long block_bytes)
{
    void *data = (void *)(uintptr_t)((uint32_t *)object)[1];
    if (data) {
        memset(data, 0xa5, block_bytes);
    }
}

static void *arr_new_poly(unsigned long count)
{
    void *object = ((ArrCreate1)slot(g_life.vtable, EEWEB_SLOT_GERasterizer_CreatePolygonArray))(g_life.obj, count);
    if (object) {
        arr_fill(object, count * 6);
    }
    return object;
}

static void *arr_new_vertices(unsigned long kind, unsigned long count)
{
    void *object = ((ArrCreate2)slot(g_life.vtable, EEWEB_SLOT_GERasterizer_CreateVertexArray))(g_life.obj, kind, count);
    if (object) {
        arr_fill(object, count * (kind == 0 ? 32 : 36));
    }
    return object;
}

static void arr_delete(void *object)
{
    ((DeletingDtor)slot(*(void *const **)object, 0))(object, 1);
}

/* ---- polygon arrays ---- */

static void arr_poly_step(const char *name, unsigned long count)
{
    LS(name, LIFE_NODIFF, {
        g_arr.poly = arr_new_poly(count);
        ret = g_arr.poly != NULL;
        if (g_arr.poly) {
            arr_object(g_arr.poly, 0, count * 6);
            life_extra(" size=%lu sizeof=%lu", ((ArrSize)PA_SLOT(g_arr.poly, GetSize))(g_arr.poly), ((ArrSize)PA_SLOT(g_arr.poly, SizeOf))(g_arr.poly));
        }
    });
}

static void arr_poly_get(unsigned long index)
{
    void *p = g_arr.poly;
    life_extra(" get[%lu]=%lx,%lx,%lx", index, ((ArrGet)PA_SLOT(p, GetVertexIndex1))(p, index), ((ArrGet)PA_SLOT(p, GetVertexIndex2))(p, index),
               ((ArrGet)PA_SLOT(p, GetVertexIndex3))(p, index));
}

static void arr_polygons(void)
{
    arr_poly_step("arrays: CreatePolygonArray(5)", 5);
    LS("arrays: SetVertexIndices(2, 0x11111, 0x2222, 0xffff) keeps the low 16 bits", LIFE_NODIFF, {
        ((ArrSet4)PA_SLOT(g_arr.poly, SetVertexIndices))(g_arr.poly, 2, 0x11111, 0x2222, 0xffff);
        arr_object(g_arr.poly, 0, 30);
        arr_poly_get(2);
        arr_poly_get(1);
        arr_poly_get(3);
    });
    LS("arrays: SetVertexIndex1, 2 and 3 write one index each", LIFE_NODIFF, {
        ((ArrSet)PA_SLOT(g_arr.poly, SetVertexIndex1))(g_arr.poly, 0, 0x1234);
        ((ArrSet)PA_SLOT(g_arr.poly, SetVertexIndex2))(g_arr.poly, 1, 0x5678);
        ((ArrSet)PA_SLOT(g_arr.poly, SetVertexIndex3))(g_arr.poly, 4, 0x9abcdef0);
        arr_object(g_arr.poly, 0, 30);
        arr_poly_get(0);
        arr_poly_get(1);
        arr_poly_get(4);
    });
    LS("arrays: the polygon array deletes its index block with the object", LIFE_NODIFF, {
        void *data = (void *)(uintptr_t)((uint32_t *)g_arr.poly)[1];
        void *object = g_arr.poly;
        arr_delete(object);
        g_arr.poly = NULL;
        life_extra("object=%s data=%s", arr_live(object) ? "live" : "freed", arr_live(data) ? "live" : "freed");
    });
    arr_poly_step("arrays: CreatePolygonArray(0)", 0);
    arr_delete(g_arr.poly);
    arr_poly_step("arrays: CreatePolygonArray(1)", 1);
    arr_delete(g_arr.poly);
    arr_poly_step("arrays: CreatePolygonArray(2731)", 2731);
    LS("arrays: the last polygon of a large array", LIFE_NODIFF, {
        ((ArrSet4)PA_SLOT(g_arr.poly, SetVertexIndices))(g_arr.poly, 2730, 1, 2, 3);
        arr_poly_get(2730);
        arr_poly_get(2729);
        char tail[64] = "";
        arr_words(tail, sizeof tail, (const char *)(uintptr_t)((uint32_t *)g_arr.poly)[1] + 16374, 12);
        life_extra(" tail=%s", tail);
    });
    arr_delete(g_arr.poly);
    g_arr.poly = NULL;
}

/* ---- vertex arrays ---- */

static void arr_float(float value, char *out, size_t size)
{
    uint32_t bits;
    memcpy(&bits, &value, 4);
    snprintf(out + strlen(out), size - strlen(out), "%x", (unsigned)bits);
}

static void arr_fill_floats(float *out, int count)
{
    for (int i = 0; i < count; i++) {
        memcpy(&out[i], "\xde\xad\xbe\xef", 4); /* a pattern that is not a result: the getters must overwrite what they write */
    }
}

/* What the getters answer for vertex `index`. */
static void arr_vertex_get(void *v, unsigned long index)
{
    float color[4], normal[4], point[4], coordinate[3];
    arr_fill_floats(color, 4);
    arr_fill_floats(normal, 4);
    arr_fill_floats(point, 4);
    arr_fill_floats(coordinate, 3);
    ((ArrOut)VA_SLOT(v, GetColor))(v, color, index);
    ((ArrOut)VA_SLOT(v, GetNormal))(v, normal, index);
    ((ArrOut)VA_SLOT(v, GetPoint))(v, point, index);
    ((ArrOut)VA_SLOT(v, GetTextureCoordinate))(v, coordinate, index);
    float opacity = ((ArrOpacity)VA_SLOT(v, GetOpacity))(v, index);
    char text[256] = "";
    arr_words(text, sizeof text, color, 16);
    life_extra(" [%lu] color=%s", index, text);
    text[0] = 0;
    arr_words(text, sizeof text, normal, 16);
    life_extra(" normal=%s", text);
    text[0] = 0;
    arr_words(text, sizeof text, point, 16);
    life_extra(" point=%s", text);
    text[0] = 0;
    arr_words(text, sizeof text, coordinate, 12);
    life_extra(" coordinate=%s opacity=", text);
    text[0] = 0;
    arr_float(opacity, text, sizeof text);
    life_extra("%s", text);
}

static void arr_vertex_step(const char *name, unsigned long kind, unsigned long count)
{
    LS(name, LIFE_NODIFF, {
        void *v = arr_new_vertices(kind, count);
        if (kind == 0) {
            g_arr.vs = v;
        } else {
            g_arr.vc = v;
        }
        ret = v != NULL;
        if (v) {
            arr_object(v, kind == 0 ? 1 : 2, count * (kind == 0 ? 32 : 36));
            life_extra(" size=%lu sizeof=%lu type=%lu", ((ArrSize)VA_SLOT(v, GetSize))(v), ((ArrSize)VA_SLOT(v, SizeOf))(v), ((ArrSize)VA_SLOT(v, GetType))(v));
        }
    });
}

static void arr_vertices_setters(const char *label, void *v, unsigned long kind, unsigned long stride)
{
    static const float kColor[3] = {0.5f, 0.25f, 1.0f};
    static const float kPoint[3] = {1.5f, -2.25f, 1e10f};
    static const float kNormal[3] = {0.0f, 1.0f, -0.5f};
    static const float kCoordinate[2] = {0.125f, 7.0f};
    char name[160];
    snprintf(name, sizeof name, "arrays: %s SetPoint, SetNormal and SetTextureCoordinate write one vertex", label);
    LS(name, LIFE_NODIFF, {
        ((ArrIn)VA_SLOT(v, SetPoint))(v, kPoint, 1);
        ((ArrIn)VA_SLOT(v, SetNormal))(v, kNormal, 1);
        ((ArrIn)VA_SLOT(v, SetTextureCoordinate))(v, kCoordinate, 1);
        arr_object(v, kind == 0 ? 1 : 2, 3 * stride);
        arr_vertex_get(v, 1);
        arr_vertex_get(v, 0);
    });
    snprintf(name, sizeof name, "arrays: %s SetColor", label);
    LS(name, LIFE_NODIFF, {
        ((ArrIn)VA_SLOT(v, SetColor))(v, kColor, 2);
        arr_object(v, kind == 0 ? 1 : 2, 3 * stride);
        arr_vertex_get(v, 2);
    });
    snprintf(name, sizeof name, "arrays: %s SetOpacity", label);
    LS(name, LIFE_NODIFF, {
        ((ArrSetOpacity)VA_SLOT(v, SetOpacity))(v, 0.5f, 2);
        arr_object(v, kind == 0 ? 1 : 2, 3 * stride);
        arr_vertex_get(v, 2);
    });
    snprintf(name, sizeof name, "arrays: %s SetVertex", label);
    LS(name, LIFE_NODIFF, {
        ((ArrSetVertex)VA_SLOT(v, SetVertex))(v, kColor, kPoint, kNormal, kCoordinate, 0.75f, 0);
        arr_object(v, kind == 0 ? 1 : 2, 3 * stride);
        arr_vertex_get(v, 0);
    });
}

/* The conversions of a colour channel and the opacity to a byte (rounded to nearest even, not clamped). */
static void arr_vertices_rounding(void)
{
    static const float kChannel[] = {0.0f, 1.0f, 0.5f, 0.25f, 1.5f, -0.5f, 0.001f, 0.002f, 0.0019607844f, 0.9999f, 2.0f, 3.0f};
    for (unsigned i = 0; i < sizeof kChannel / sizeof kChannel[0]; i++) {
        char name[128];
        snprintf(name, sizeof name, "arrays: stdcolor channel rounding %u", i);
        LS(name, LIFE_NODIFF, {
            float color[3] = {kChannel[i], kChannel[(i + 1) % 12], kChannel[(i + 5) % 12]};
            memset((void *)(uintptr_t)((uint32_t *)g_arr.vc)[1], 0, 36 * 3);
            ((ArrIn)VA_SLOT(g_arr.vc, SetColor))(g_arr.vc, color, 0);
            arr_object(g_arr.vc, 2, 36);
            ((ArrSetOpacity)VA_SLOT(g_arr.vc, SetOpacity))(g_arr.vc, kChannel[(i + 3) % 12], 1);
            arr_object(g_arr.vc, 2, 72);
            ((ArrSetVertex)VA_SLOT(g_arr.vc, SetVertex))(g_arr.vc, color, color, color, color, kChannel[(i + 7) % 12], 2);
            arr_object(g_arr.vc, 2, 108);
        });
    }
}

/* Reads of the colour word the test writes itself, so the unpacking is checked apart from the packing. */
static void arr_vertices_unpack(void)
{
    static const uint32_t kWords[] = {0x00000000u, 0xffffffffu, 0x80ff7f01u, 0x01020304u, 0xfe00ff7fu};
    for (unsigned i = 0; i < sizeof kWords / sizeof kWords[0]; i++) {
        char name[128];
        snprintf(name, sizeof name, "arrays: stdcolor reads the colour word %08x", (unsigned)kWords[i]);
        LS(name, LIFE_NODIFF, {
            memcpy((char *)(uintptr_t)((uint32_t *)g_arr.vc)[1] + 24, &kWords[i], 4);
            arr_vertex_get(g_arr.vc, 0);
        });
    }
}

/* Every packed byte value read back, and 1024 channel values packed: the exact floats and bytes the original's x87 code gives, as a hash. */
static void arr_vertices_sweep(void)
{
    LS("arrays: stdcolor sweeps every byte value and 1024 channel values", LIFE_NODIFF, {
        uint32_t hash = 2166136261u;
        uint32_t *word = (uint32_t *)((char *)(uintptr_t)((uint32_t *)g_arr.vc)[1] + 24);
        for (uint32_t v = 0; v < 256; v++) {
            float color[3] = {0, 0, 0};
            *word = v * 0x01010101u;
            ((ArrOut)VA_SLOT(g_arr.vc, GetColor))(g_arr.vc, color, 0);
            float opacity = ((ArrOpacity)VA_SLOT(g_arr.vc, GetOpacity))(g_arr.vc, 0);
            for (int c = 0; c < 4; c++) {
                uint32_t bits;
                memcpy(&bits, c < 3 ? &color[c] : &opacity, 4);
                hash = (hash ^ bits) * 16777619u;
            }
        }
        for (int i = 0; i < 1024; i++) {
            float x = -0.1f + (float)i * 0.0013f;
            float color[3] = {x, x * 0.7f, x * 1.3f};
            ((ArrIn)VA_SLOT(g_arr.vc, SetColor))(g_arr.vc, color, 0);
            hash = (hash ^ *word) * 16777619u;
            ((ArrSetOpacity)VA_SLOT(g_arr.vc, SetOpacity))(g_arr.vc, x, 0);
            hash = (hash ^ *word) * 16777619u;
        }
        life_extra("hash=%08x", (unsigned)hash);
    });
}

static void arr_vertices_copy(void)
{
    void *small = arr_new_vertices(0, 2);
    void *big = arr_new_vertices(0, 5);
    void *colored = arr_new_vertices(1, 4);
    for (int i = 0; i < 2 * 32 / 4; i++) {
        ((uint32_t *)(uintptr_t)((uint32_t *)small)[1])[i] = 0x1000u + (uint32_t)i;
    }
    for (int i = 0; i < 5 * 32 / 4; i++) {
        ((uint32_t *)(uintptr_t)((uint32_t *)big)[1])[i] = 0x2000u + (uint32_t)i;
    }
    for (int i = 0; i < 4 * 36 / 4; i++) {
        ((uint32_t *)(uintptr_t)((uint32_t *)colored)[1])[i] = 0x3000u + (uint32_t)i;
    }
    LS("arrays: CopyFrom a smaller array copies the source's vertices only", LIFE_NODIFF, {
        ((ArrCopy)VA_SLOT(big, CopyFrom))(big, small);
        arr_object(big, 1, 5 * 32);
    });
    LS("arrays: CopyFrom a larger array copies as many vertices as the target holds", LIFE_NODIFF, {
        ((ArrCopy)VA_SLOT(small, CopyFrom))(small, big);
        arr_object(small, 1, 2 * 32);
    });
    LS("arrays: CopyFrom an array of the other kind copies the raw bytes", LIFE_NODIFF, {
        ((ArrCopy)VA_SLOT(small, CopyFrom))(small, colored);
        arr_object(small, 1, 2 * 32);
        ((ArrCopy)VA_SLOT(colored, CopyFrom))(colored, big);
        arr_object(colored, 2, 4 * 36);
    });
    arr_delete(small);
    arr_delete(big);
    arr_delete(colored);
}

static void arr_vertices(void)
{
    arr_vertex_step("arrays: CreateVertexArray(0, 3)", 0, 3);
    arr_vertices_setters("std", g_arr.vs, 0, 32);
    arr_vertex_step("arrays: CreateVertexArray(1, 4)", 1, 4);
    arr_vertices_setters("stdcolor", g_arr.vc, 1, 36);
    arr_vertices_rounding();
    arr_vertices_unpack();
    arr_vertices_sweep();
    LS("arrays: the vertex arrays delete their blocks with the object", LIFE_NODIFF, {
        void *s = (void *)(uintptr_t)((uint32_t *)g_arr.vs)[1];
        void *c = (void *)(uintptr_t)((uint32_t *)g_arr.vc)[1];
        void *so = g_arr.vs;
        void *co = g_arr.vc;
        arr_delete(so);
        arr_delete(co);
        g_arr.vs = g_arr.vc = NULL;
        life_extra("objects=%s,%s blocks=%s,%s", arr_live(so) ? "live" : "freed", arr_live(co) ? "live" : "freed",
                   arr_live(s) ? "live" : "freed", arr_live(c) ? "live" : "freed");
    });
    arr_vertex_step("arrays: CreateVertexArray(0, 0)", 0, 0);
    arr_delete(g_arr.vs);
    arr_vertex_step("arrays: CreateVertexArray(1, 0)", 1, 0);
    arr_delete(g_arr.vc);
    LS("arrays: CreateVertexArray with another kind answers null", LIFE_NODIFF, {
        void *two = ((ArrCreate2)slot(g_life.vtable, EEWEB_SLOT_GERasterizer_CreateVertexArray))(g_life.obj, 2, 3);
        void *big = ((ArrCreate2)slot(g_life.vtable, EEWEB_SLOT_GERasterizer_CreateVertexArray))(g_life.obj, 0xffffffffUL, 3);
        void *three = ((ArrCreate2)slot(g_life.vtable, EEWEB_SLOT_GERasterizer_CreateVertexArray))(g_life.obj, 3, 3);
        ret = (two != NULL) + 2 * (big != NULL) + 4 * (three != NULL);
    });
    arr_vertices_copy();
}

/* Our build only: a count whose byte size does not fit in 32 bits is refused with null (the original asks operator new for the wrapped
 * size and writes past the small block it gets). */
static void arr_ours_only(void)
{
    ArrCreate1 polygons = (ArrCreate1)slot(g_life.vtable, EEWEB_SLOT_GERasterizer_CreatePolygonArray);
    ArrCreate2 vertices = (ArrCreate2)slot(g_life.vtable, EEWEB_SLOT_GERasterizer_CreateVertexArray);
    void *wrapped_polygons = polygons(g_life.obj, 0x2aaaaaabUL);
    void *wrapped_standard = vertices(g_life.obj, 0, 0x08000001UL);
    void *wrapped_colour = vertices(g_life.obj, 1, 0x071c71c8UL);
    CHECK(wrapped_polygons == NULL && wrapped_standard == NULL && wrapped_colour == NULL,
          "%s: arrays: a count whose byte size wraps is refused with null (polygons %p, standard %p, colour %p)", g_life.e->label, wrapped_polygons,
          wrapped_standard, wrapped_colour);
}

static void check_arrays(const struct Expect *e, HMODULE h, const ScreenApi *api, void *obj)
{
    (void)h;
    if (!life_begin(e, api, obj, 0, 0, "the arrays")) {
        return;
    }
    memset(&g_arr, 0, sizeof g_arr);
    arr_polygons();
    arr_vertices();
    if (e->ours) {
        arr_ours_only();
    }
    life_end();
}
