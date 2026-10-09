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
    HRESULT hr_flip_status, hr_scanline;
    int format_callbacks;
    DWORD vb_fvf, vb_vertices;  /* what GetVertexBufferDesc reported back */
    DWORD tex_pitch;            /* reported by Lock */
    DWORD got_render_state;     /* GetRenderState after SetRenderState */
} ScenarioB;
typedef int(WINAPI *ScenarioBFn)(HWND hwnd, ScenarioB *out);

/* Scenario C: present-equivalent calls and the capture cap. */
typedef struct ScenarioC {
    HRESULT hr_setup, hr_blt[6];
} ScenarioC;
typedef int(WINAPI *ScenarioCFn)(HWND hwnd, ScenarioC *out);

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
