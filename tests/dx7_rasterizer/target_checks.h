/*
 * ActivateRenderingSurface(screen, textureManager, window, width, height) of DX7Rasterizer (task P2.6.7j), slot 1.  Included by
 * screen_checks.h after terrain_draw_checks.h and run from the screen probe; same method as lifecycle_checks.h (the calls of the fake
 * DirectDraw and Direct3D, the answer, the engine's native error and the words changed in the engine's per-screen state, the screen
 * record, the screen and the texture manager; literals of target_expected.inc recorded from the original DLL).
 *
 * The function activates a screen on an off-screen surface: it takes the first video mode with as many bits per pixel as the current
 * mode, makes a video-memory surface (type 1) of the given size in that mode's pixel format, and activates the screen with it as
 * primary surface and render target (ActivateTheScreen).  When the surface cannot be made or the screen cannot be activated it
 * tries again with the first 16-bit mode.
 */

typedef long(THISCALL *ActivateTargetFn)(void *self, unsigned long screen, void *textureManager, void *window, unsigned long width, unsigned long height);

#define TG_STEP(name, screen, width, height, ...)                                                                                           \
    do {                                                                                                                                    \
        g_life.cur = screen;                                                                                                                \
        LS(name, LIFE_DIFF, {                                                                                                               \
            __VA_ARGS__;                                                                                                                    \
            ret = ((ActivateTargetFn)LIFE_SLOT(ActivateRenderingSurface))(g_life.obj, (unsigned long)(screen), g_life.tm[screen], g_life.win[screen], (width), (height)); \
            life_depth_report();                                                                                                            \
        });                                                                                                                                 \
    } while (0)

static void check_target(const struct Expect *e, HMODULE h, const ScreenApi *api, void *obj)
{
    (void)h;
    if (!life_begin(e, api, obj, 2, 0, "the rendering surfaces")) {
        return;
    }
    TG_STEP("target: ActivateRenderingSurface of 256 x 128", 0, 256, 128);
    TG_STEP("target: ActivateRenderingSurface of another screen, 640 x 480", 1, 640, 480);
    TG_STEP("target: ActivateRenderingSurface of a screen that is active already", 0, 128, 64);
    /* the first surface cannot be made: the first 16-bit mode is tried */
    TG_STEP("target: ActivateRenderingSurface when the first CreateSurface fails", 1, 320, 200, g_life.dd->create_results[0] = LIFE_ERR_OTHER; g_life.dd->create_result_count = 1;
            g_life.dd->create_result_pos = 0);
    TG_STEP("target: ActivateRenderingSurface when both CreateSurface calls fail", 0, 320, 200, g_life.dd->create_results[0] = LIFE_ERR_OTHER;
            g_life.dd->create_results[1] = LIFE_ERR_LOST; g_life.dd->create_result_count = 2; g_life.dd->create_result_pos = 0);
    g_life.dd->create_result_count = 0;
    /* the screen cannot be activated: the surface is given back and the first 16-bit mode is tried */
    TG_STEP("target: ActivateRenderingSurface when CreateDevice is refused", 1, 100, 100, g_life.dd->create_device_result = (long)0x887600a2);
    g_life.dd->create_device_result = 0;
    life_end();
}
