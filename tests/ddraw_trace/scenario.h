/* Shared between the harness (an exe, whose calls the proxy must NOT trace) and the fake plugin DLL
 * (named in DDT_MODULES, whose calls the proxy must trace). */
#ifndef SCENARIO_H
#define SCENARIO_H

#include <windows.h>

/* Results of the calls the plugin made, so that the harness can check the proxy passed them through. */
typedef struct ScenarioA {
    HRESULT hr_enum_ex, hr_enum, hr_create, hr_coop, hr_qi, hr_qi_again;
    HRESULT hr_primary, hr_target, hr_device, hr_begin, hr_clear, hr_end;
    int enum_ex_callbacks, enum_callbacks;
    void *d3d_first, *d3d_again; /* both queries must give the same interface pointer */
    ULONG device_release;
} ScenarioA;

typedef int(WINAPI *ScenarioAFn)(HWND hwnd, ScenarioA *out);

/* Scenario B: device state, structures, textures, vertex buffers, Lock/Unlock, unwrapped interfaces. */
typedef struct ScenarioB {
    HRESULT hr_setup;          /* device and surfaces created */
    HRESULT hr_caps, hr_enum_formats, hr_texture, hr_set_texture, hr_render_state, hr_stage_state;
    HRESULT hr_transform, hr_viewport, hr_material, hr_light, hr_light_enable;
    HRESULT hr_vb_create, hr_vb_lock, hr_vb_unlock, hr_draw_vb, hr_vb_desc;
    HRESULT hr_tex_lock, hr_tex_unlock, hr_clipper, hr_clipper_use, hr_clipper_raw, hr_qi_dd4, hr_dd4_caps;
    HRESULT hr_flip_status, hr_scanline, hr_draw_indexed, hr_draw_prim;
    int format_callbacks;
    DWORD vb_fvf, vb_vertices;  /* what GetVertexBufferDesc reported back */
    DWORD tex_pitch;            /* reported by Lock */
    DWORD got_render_state;     /* GetRenderState after SetRenderState */
} ScenarioB;
typedef int(WINAPI *ScenarioBFn)(HWND hwnd, ScenarioB *out);

/* Scenario C: present-equivalent calls and the capture cap. */
typedef struct ScenarioC {
    HRESULT hr_setup, hr_blt[6], hr_partial, hr_fill;
} ScenarioC;
typedef int(WINAPI *ScenarioCFn)(HWND hwnd, ScenarioC *out);

/* Scenario R: the capture is armed by a file.  Two whole-surface blits (presents) before the arm file exists, then the plugin
 * creates DDT_ARM_FILE, then six more presents: the trace starts after the present that first sees the file. */
typedef struct ScenarioR {
    HRESULT hr_setup, hr_blt[8];
    int armed_file_made;
} ScenarioR;
typedef int(WINAPI *ScenarioRFn)(HWND hwnd, ScenarioR *out);

/* Scenario D: the wrapped-surface paths that the other scenarios do not reach. */
typedef struct ScenarioD {
    HRESULT hr_setup, hr_srt, hr_get_rt, hr_get_tex, hr_att1, hr_att2, hr_enum_att, hr_not_found, hr_bltfast;
    HRESULT hr_bad_create, hr_lock1, hr_lock2_busy, hr_unlock, hr_vidmem;
    void *target2, *got_rt, *tex, *got_tex, *mip_level1, *att1, *att2, *enum_seen, *bad_surface;
    int enum_callbacks;
} ScenarioD;
typedef int(WINAPI *ScenarioDFn)(HWND hwnd, ScenarioD *out);

/* The rectangle that scenario D locks in its 8x8 texture, and what it writes there. */
#define LOCK_RECT_L 2
#define LOCK_RECT_T 2
#define LOCK_RECT_R 6
#define LOCK_RECT_B 6
static __inline unsigned rect_pixel(unsigned x, unsigned y) { return 0xff000000u | (y << 8) | x; }

/* Scenario T: two traced threads interleave their calls; polling collapse must be per thread. */
typedef struct ScenarioT {
    HRESULT hr_create;
    HRESULT hr_flip_status[3];
    int thread_calls;
} ScenarioT;
typedef int(WINAPI *ScenarioTFn)(HWND hwnd, ScenarioT *out);

/* Scenario M (against the scripted mock ddraw): failed presents, retries and unwrapping of surface arguments. */
typedef struct ScenarioM {
    HRESULT hr_create, hr_primary, hr_back, hr_attached;
    HRESULT hr_flip_first[3];  /* DDERR_WASSTILLDRAWING, DDERR_WASSTILLDRAWING, DD_OK */
    HRESULT hr_flip_override;  /* the back buffer as override: only valid if the proxy unwrapped it */
    HRESULT hr_blt_busy, hr_blt_ok, hr_bltfast, hr_flip_after_cap;
    void *attached, *back;     /* GetAttachedSurface must hand back the back buffer's own wrapper */
} ScenarioM;
typedef int(WINAPI *ScenarioMFn)(HWND hwnd, ScenarioM *out);

/* Scenario V (against the scripted mock ddraw, DDT_DATA set or not): vertex and index data captured through a vertex-buffer
 * ring (DISCARD / NOOVERWRITE locks), plus draws that read application memory.  All content is synthetic. */
typedef struct ScenarioV {
    HRESULT hr_setup, hr_vb_create, hr_lock[3], hr_unlock[3], hr_draw[8], hr_blt[3];
} ScenarioV;
typedef int(WINAPI *ScenarioVFn)(HWND hwnd, ScenarioV *out);

#define V_STRIDE 36       /* FVF 0x152 */
#define V_TOTAL 16        /* vertices in the buffer */
/* Synthetic vertex bytes: vertices first .. first+count-1 of pattern `seed`. */
static __inline void fill_pattern(unsigned char *dst, unsigned first, unsigned count, unsigned seed) {
    unsigned i, j;
    for (i = 0; i < count; i++)
        for (j = 0; j < V_STRIDE; j++)
            dst[i * V_STRIDE + j] = (unsigned char)(seed * 31u + (first + i) * 7u + j);
}

/* Deterministic content the plugin writes into locked buffers, so that the harness can compute the expected hash. */
#define VB_VERTICES 4
#define VB_FVF 0x152 /* XYZ | NORMAL | DIFFUSE | TEX1 */
#define VB_VERTEX_SIZE 36
#define TEX_SIZE 8

static __inline void fill_vertices(unsigned char *dst) {
    float *f = (float *)dst;
    unsigned i;
    for (i = 0; i < VB_VERTICES; i++) {
        float *v = f + i * 9; /* x y z nx ny nz (diffuse as bits) u v */
        v[0] = (float)(i & 1);
        v[1] = (float)(i >> 1);
        v[2] = 0.5f;
        v[3] = 0.0f;
        v[4] = 0.0f;
        v[5] = 1.0f;
        ((unsigned *)v)[6] = 0xff000000u | (i * 0x111111u);
        v[7] = (float)(i & 1);
        v[8] = (float)(i >> 1);
    }
}

static __inline void fill_texture(unsigned char *base, unsigned pitch) {
    unsigned y, x;
    for (y = 0; y < TEX_SIZE; y++)
        for (x = 0; x < TEX_SIZE; x++)
            ((unsigned *)(base + y * pitch))[x] = 0xff000000u | (y * 0x10000u) | (x * 0x100u) | (x + y);
}

static __inline unsigned long long fnv1a(const void *p, unsigned n, unsigned long long h) {
    const unsigned char *b = (const unsigned char *)p;
    while (n--) {
        h ^= *b++;
        h *= 0x100000001b3ull;
    }
    return h;
}

#endif
