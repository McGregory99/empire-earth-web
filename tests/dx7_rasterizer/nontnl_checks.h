/*
 * The activation of the rasterizer without hardware T&L (task P2.7): the original's DX7Rasterizer::ActivateTheScreen, CreateVertexBuffers and
 * GetIsScreenValid, which the T&L class (DX7TnLRasterizer) replaces.  Included by screen_checks.h after movie_checks.h; the steps run in the
 * non-T&L half of the matrix only (the original DX7HRDisplay.dll and EEWebRasterizerNonTnL.dll).  The differences from the T&L class that the
 * ordinary activation steps already show (the Direct3D device is the HAL device, not the T&L one; the vertex buffer is in system memory;
 * a device that only rasterizes in hardware is valid) are in nontnl_expected.inc next to the literals of these steps.
 *
 * What these steps add is what the non-T&L activation does and the T&L one does not: it asks the screen's DirectDraw object for the display
 * adapter's identity (GetDeviceIdentifier) and the rasterizer for the video memory, and decides from them two flags of the screen's record:
 *   - the record's canModulateTextureAlpha (0x70) is cleared for an adapter whose description (upper-cased) contains both "RAGE" and "PRO" and
 *     that has at most 8 MB (0x800000) of video memory, and set for every other;
 *   - the record's dirtyRectangleSupport (0x71) is cleared for the adapters with vendor 0x1002 and device 0x5046 or vendor 0x121a and device 9,
 *     and set for every other.
 * Each scenario is a fresh rasterizer with one screen, activated once, and a step shows the calls, the return value and every word that changed.
 */

static struct {
    const struct Expect *e;
    const ScreenApi *api;
    void *obj;
} g_nt;

static void nt_scenario(const char *label, const char *description, unsigned long vendor, unsigned long device, unsigned long memory, long identify_result)
{
    char name[160];
    if (!life_begin(g_nt.e, g_nt.api, g_nt.obj, 1, 0, label)) {
        return;
    }
    g_life.dd->ident_vendor = vendor;
    g_life.dd->ident_device = device;
    snprintf(g_life.dd->ident_description, sizeof g_life.dd->ident_description, "%s", description);
    g_life.dd->vid_total = memory;
    g_life.dd->ident_result = identify_result;
    snprintf(name, sizeof name, "nontnl: ActivateScreen: %s", label);
    LS(name, LIFE_DIFF, ret = life_activate(0, 1, 1, 10));
    life_end();
}

static void check_nontnl(const struct Expect *e, HMODULE h, const ScreenApi *api, void *obj)
{
    (void)h;
    if (!g_nontnl) {
        return;
    }
    g_nt.e = e;
    g_nt.api = api;
    g_nt.obj = obj;
    const unsigned long k4MB = 4UL << 20, k8MB = 8UL << 20, k16MB = 16UL << 20;
    /* the description */
    nt_scenario("an adapter that is not a Rage Pro", "NVIDIA GeForce2", 0x10de, 0x28, k4MB, 0);
    nt_scenario("a Rage Pro with 4 MB", "ATI RAGE PRO TURBO AGP 2X", 0x1002, 0x4750, k4MB, 0);
    nt_scenario("a Rage Pro with 8 MB", "ATI RAGE PRO TURBO AGP 2X", 0x1002, 0x4750, k8MB, 0);
    nt_scenario("a Rage Pro with 8 MB and a byte", "ATI RAGE PRO TURBO AGP 2X", 0x1002, 0x4750, k8MB + 1, 0);
    nt_scenario("a Rage Pro with 16 MB", "ATI RAGE PRO TURBO AGP 2X", 0x1002, 0x4750, k16MB, 0);
    nt_scenario("a Rage Pro in lower case with 4 MB", "ati rage pro lt", 0x1002, 0x4750, k4MB, 0);
    nt_scenario("a Rage 128 with 4 MB", "ATI RAGE 128 GL", 0x1002, 0x5245, k4MB, 0);
    nt_scenario("a Pro without Rage with 4 MB", "Matrox Millennium PRO", 0x102b, 0x51a, k4MB, 0);
    nt_scenario("RAGE and PRO in the wrong order with 4 MB", "PRO RAGE", 0x1002, 0x4750, k4MB, 0);
    /* the vendor and device */
    nt_scenario("vendor 0x1002 device 0x5046", "ATI Rage 128 Pro", 0x1002, 0x5046, k16MB, 0);
    nt_scenario("vendor 0x1002 device 0x5047", "ATI Rage 128 Pro", 0x1002, 0x5047, k16MB, 0);
    nt_scenario("vendor 0x121a device 9", "3dfx Voodoo3", 0x121a, 9, k16MB, 0);
    nt_scenario("vendor 0x121a device 5", "3dfx Voodoo Banshee", 0x121a, 5, k16MB, 0);
    nt_scenario("vendor 0x5046 device 0x1002", "Swapped ids", 0x5046, 0x1002, k16MB, 0);
    nt_scenario("vendor 0x10de device 0x5046", "Another vendor, the same device", 0x10de, 0x5046, k16MB, 0);
    /* GetDeviceIdentifier fails: the buffer stays zero */
    nt_scenario("GetDeviceIdentifier fails", "never read", 0x1002, 0x5046, k4MB, (long)0x88760064);
}
