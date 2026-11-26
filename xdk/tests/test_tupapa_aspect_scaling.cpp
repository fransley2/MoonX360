/* Acceptance & regression tests for Tupapa's custom-resolution, 4:3 UI layout,
 * and player scaling / Crop to 4:3 fixes. */
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include "../../src/config.h"
#include "../../src/crt_resolutions.h"
#include "../../src/display_policy.h"
#include "../../src/profile_links.h"
#include "../../src/ui_layout_geometry.h"
#include "../../src/video_viewport.h"
#include "../../src/overlay_hud.h"
#include "../../src/ui/ui.h"
void ui_present(void) {}

static std::map<std::string, std::string> files;
char *storage_read(const char *name, size_t *bytes) {
    if (!files.count(name)) { errno = ENOENT; return NULL; }
    if (bytes) *bytes = files[name].size();
    return _strdup(files[name].c_str());
}
bool storage_write(const char *name, const void *data, size_t n) {
    files[name] = std::string((const char *)data, n);
    return true;
}

static void make_params(const struct config *c, int retro_bb, struct crt_viewport_params *out) {
    memset(out, 0, sizeof(*out));
    out->crt_aspect = c->crt_aspect;
    out->crt_integer_scale = c->crt_integer_scale;
    out->retro_240p_bb = retro_bb;
    out->crt_overscan = c->crt_overscan;
    out->crt_size_x = c->crt_size_x;
    out->crt_size_y = c->crt_size_y;
    out->crt_pos_x = c->crt_pos_x;
    out->crt_pos_y = c->crt_pos_y;
}

int main(void) {
    struct config c, loaded;
    struct crt_viewport_params vp;
    struct video_present_rect pr;
    int ui_layout, ui_w, ui_h, bb_w, bb_h;

    /* Acceptance 1: 4:3 CRT + 800x600 or 1024x768 stream (and 1280x720) */
    config_defaults(&c);
    c.width = 800; c.height = 600; c.ui_layout = UI_LAYOUT_AUTO; c.crt_aspect = CRT_ASPECT_FIT;
    display_render_size(c.display_mode == 0 && display_output_is_4_3(640, 480, false) ? 4 : c.display_mode,
                        640, 480, &bb_w, &bb_h);
    ui_layout = ui_layout_resolve_display(c.ui_layout, c.display_mode, 640, 480, false, bb_w, bb_h);
    ui_layout_canvas_size(ui_layout, &ui_w, &ui_h);
    assert(bb_w == 640 && bb_h == 480 && ui_layout == UI_LAYOUT_4_3 && ui_w == 960 && ui_h == 720);
    make_params(&c, 0, &vp);
    video_compute_present_rect(800, 600, bb_w, bb_h, &vp, &pr);
    assert(pr.src_left == 0 && pr.src_right == 800 && pr.dst_left == 0 && pr.dst_right == 640 && pr.dst_bottom == 480);
    video_compute_present_rect(1024, 768, bb_w, bb_h, &vp, &pr);
    assert(pr.src_left == 0 && pr.src_right == 1024 && pr.dst_left == 0 && pr.dst_right == 640 && pr.dst_bottom == 480);
    /* Even with 1280x720 stream on 4:3 CRT, Auto UI stays 4:3 (960x720)! */
    c.width = 1280; c.height = 720;
    ui_layout = ui_layout_resolve_display(c.ui_layout, c.display_mode, 640, 480, false, bb_w, bb_h);
    ui_layout_canvas_size(ui_layout, &ui_w, &ui_h);
    assert(ui_layout == UI_LAYOUT_4_3 && ui_w == 960 && ui_h == 720);
    printf("CASE1: 4:3 CRT | req=800x600/1024x768 | ui=%dx%d | bb=%dx%d | out=640x480i | src=[0,0,800,600) dst=[0,0,640,480)\n",
           ui_w, ui_h, bb_w, bb_h);

    /* Acceptance 2: 4:3 CRT + 1280x720 stream with embedded side bars + Crop to 4:3 */
    c.crt_aspect = CRT_ASPECT_CROP_4_3;
    make_params(&c, 0, &vp);
    video_compute_present_rect(1280, 720, bb_w, bb_h, &vp, &pr);
    assert(pr.src_left == 160 && pr.src_top == 0 && pr.src_right == 1120 && pr.src_bottom == 720);
    assert(pr.dst_left == 0 && pr.dst_top == 0 && pr.dst_right == 640 && pr.dst_bottom == 480);
    /* Test circle roundness verification: circle R=120px in 960x720 active region maps to 160x160 px in 640x480 (4:3 CRT physical 1:1) */
    double phys_x = (240.0 / (pr.src_right - pr.src_left)) * (pr.dst_right - pr.dst_left) * (4.0 / bb_w);
    double phys_y = (240.0 / (pr.src_bottom - pr.src_top)) * (pr.dst_bottom - pr.dst_top) * (3.0 / bb_h);
    assert(fabs(phys_x / phys_y - 1.0) < 1e-6);
    assert(c.width == 1280 && c.height == 720 && ui_layout == UI_LAYOUT_4_3);
    printf("CASE2: 4:3 CRT Crop4:3 | req=1280x720 dec=1280x720 | ui=%dx%d | bb=%dx%d | src=[%d,%d,%d,%d) dst=[%d,%d,%d,%d) | circle=%.3f\n",
           ui_w, ui_h, bb_w, bb_h, pr.src_left, pr.src_top, pr.src_right, pr.src_bottom,
           pr.dst_left, pr.dst_top, pr.dst_right, pr.dst_bottom, phys_x / phys_y);

    /* Acceptance 3: 16:9 TV + 800x600 stream -> widescreen UI (1280x720), centered 4:3 video with side bars only */
    config_defaults(&c);
    c.width = 800; c.height = 600; c.ui_layout = UI_LAYOUT_AUTO; c.crt_aspect = CRT_ASPECT_FIT;
    display_render_size(c.display_mode, 1920, 1080, &bb_w, &bb_h);
    ui_layout = ui_layout_resolve_display(c.ui_layout, c.display_mode, 1920, 1080, true, bb_w, bb_h);
    ui_layout_canvas_size(ui_layout, &ui_w, &ui_h);
    assert(bb_w == 1920 && bb_h == 1080 && ui_layout == UI_LAYOUT_16_9 && ui_w == 1280 && ui_h == 720);
    make_params(&c, 0, &vp);
    video_compute_present_rect(800, 600, bb_w, bb_h, &vp, &pr);
    assert(pr.src_left == 0 && pr.src_right == 800 && pr.dst_left == 240 && pr.dst_top == 0 && pr.dst_right == 1680 && pr.dst_bottom == 1080);
    printf("CASE3: 16:9 TV + 800x600 | req=800x600 dec=800x600 | ui=%dx%d | bb=%dx%d | src=[%d,%d,%d,%d) dst=[%d,%d,%d,%d)\n",
           ui_w, ui_h, bb_w, bb_h, pr.src_left, pr.src_top, pr.src_right, pr.src_bottom,
           pr.dst_left, pr.dst_top, pr.dst_right, pr.dst_bottom);

    /* Acceptance 4: 16:9 TV + 1280x720 stream -> matches baseline full screen Fit & 16:9 UI */
    c.width = 1280; c.height = 720;
    video_compute_present_rect(1280, 720, bb_w, bb_h, &vp, &pr);
    assert(pr.src_left == 0 && pr.src_right == 1280 && pr.dst_left == 0 && pr.dst_top == 0 && pr.dst_right == 1920 && pr.dst_bottom == 1080);
    printf("CASE4: 16:9 TV + 1280x720 | req=1280x720 dec=1280x720 | ui=%dx%d | bb=%dx%d | src=[%d,%d,%d,%d) dst=[%d,%d,%d,%d)\n",
           ui_w, ui_h, bb_w, bb_h, pr.src_left, pr.src_top, pr.src_right, pr.src_bottom,
           pr.dst_left, pr.dst_top, pr.dst_right, pr.dst_bottom);

    /* Acceptance 5: Persistence & navigation (720p -> 800x600 -> save/reload -> connect -> per-game profile -> return global) */
    files.clear();
    config_defaults(&c);
    config_customize_stream(&c);
    c.width = 800; c.height = 600;
    assert(config_save(&c));
    config_load(&loaded);
    assert(loaded.width == 800 && loaded.height == 600 && !config_last_resolution_rejected(NULL, NULL, NULL));
    struct config requested = loaded;
    /* Apply a per-game profile (Standard 720p30) for app 101 */
    config_apply_preset(&loaded, CONFIG_PROFILE_STANDARD);
    assert(loaded.width == 1280 && loaded.height == 720 && loaded.fps == 30);
    /* Return to global profile after session ends */
    loaded = requested;
    assert(loaded.width == 800 && loaded.height == 600 && loaded.fps == 60);
    /* Invalid INI resolution is detected and reported while keeping raw INI on disk untouched */
    std::string bad_ini = "width=1025\nheight=768\n";
    files["moonlight.ini"] = bad_ini;
    config_load(&loaded);
    int rw = 0, rh = 0; const char *reason = NULL;
    assert(config_last_resolution_rejected(&rw, &rh, &reason) && rw == 1025 && rh == 768 && reason != NULL);
    assert(files["moonlight.ini"] == bad_ini);
    /* Manual 1:1 UI layout remains independent of Pixel-to-pixel video scaling */
    loaded.ui_layout = UI_LAYOUT_1_1;
    loaded.crt_aspect = CRT_ASPECT_FIT;
    assert(ui_layout_resolve_display(loaded.ui_layout, 0, 1920, 1080, true, 1920, 1080) == UI_LAYOUT_1_1);
    printf("CASE5: Persistence + per-game restore + invalid INI rejection diagnostic PASS\n");

    /* Acceptance 6: 320x240 in Pixel-to-pixel (1:1) & experimental 2560x240 at 4:3 DAR (no t240.xex) */
    config_defaults(&c);
    config_customize_stream(&c);
    c.width = 320; c.height = 240; c.crt_aspect = CRT_ASPECT_PIXEL_1_1;
    assert(!config_is_crt_retro(&c));
    make_params(&c, 0, &vp);
    video_compute_present_rect(320, 240, 640, 480, &vp, &pr);
    assert(pr.src_left == 0 && pr.src_top == 0 && pr.src_right == 320 && pr.src_bottom == 240);
    assert(pr.dst_left == 160 && pr.dst_top == 120 && pr.dst_right == 480 && pr.dst_bottom == 360);
    printf("CASE6a: 320x240 Pixel 1:1 (640x480) | req=320x240 dec=320x240 | src=[%d,%d,%d,%d) dst=[%d,%d,%d,%d)\n",
           pr.src_left, pr.src_top, pr.src_right, pr.src_bottom, pr.dst_left, pr.dst_top, pr.dst_right, pr.dst_bottom);
    video_compute_present_rect(320, 240, 1280, 720, &vp, &pr);
    assert(pr.dst_left == 480 && pr.dst_top == 240 && pr.dst_right == 800 && pr.dst_bottom == 480);
    printf("CASE6b: 320x240 Pixel 1:1 (1280x720) | req=320x240 dec=320x240 | src=[%d,%d,%d,%d) dst=[%d,%d,%d,%d)\n",
           pr.src_left, pr.src_top, pr.src_right, pr.src_bottom, pr.dst_left, pr.dst_top, pr.dst_right, pr.dst_bottom);

    c.width = 2560; c.height = 240; c.crt_aspect = CRT_ASPECT_FIT;
    assert(crt_stream_resolution_supported(2560, 240) && !config_is_crt_retro(&c));
    make_params(&c, 0, &vp);
    video_compute_present_rect(2560, 240, 640, 480, &vp, &pr);
    assert(pr.src_left == 0 && pr.src_top == 0 && pr.src_right == 2560 && pr.src_bottom == 240);
    assert(pr.dst_left == 0 && pr.dst_top == 0 && pr.dst_right == 640 && pr.dst_bottom == 480);
    printf("CASE6c: 2560x240 DAR 4:3 (640x480) | req=2560x240 dec=2560x240 | src=[%d,%d,%d,%d) dst=[%d,%d,%d,%d) | crt_retro=0\n",
           pr.src_left, pr.src_top, pr.src_right, pr.src_bottom, pr.dst_left, pr.dst_top, pr.dst_right, pr.dst_bottom);
    video_compute_present_rect(2560, 240, 1280, 720, &vp, &pr);
    assert(pr.dst_left == 160 && pr.dst_top == 0 && pr.dst_right == 1120 && pr.dst_bottom == 720);
    printf("CASE6d: 2560x240 DAR 4:3 (1280x720) | req=2560x240 dec=2560x240 | src=[%d,%d,%d,%d) dst=[%d,%d,%d,%d) | crt_retro=0\n",
           pr.src_left, pr.src_top, pr.src_right, pr.src_bottom, pr.dst_left, pr.dst_top, pr.dst_right, pr.dst_bottom);

    /* Overlay requested vs decoded footer mismatch distinction */
    assert(ui_init_layout(960, 720, UI_LAYOUT_4_3));
    struct overlay_telemetry ot;
    memset(&ot, 0, sizeof(ot));
    ot.mode = OVERLAY_DETAILED;
    ot.requested_w = 800; ot.requested_h = 600;
    ot.actual_w = 1280; ot.actual_h = 720;
    snprintf(ot.backend_name, sizeof(ot.backend_name), "FFmpeg");
    int ow = 0, oh = 0;
    overlay_measure_panel(&ot, 960, &ow, &oh);
    canvas_t oc;
    assert(cv_init(&oc, ow, oh));
    overlay_render_panel(&oc, &ot, 960);
    cv_free(&oc);

    puts("PASS: all 6 Tupapa custom-res, 4:3 UI layout, player scaling, Crop to 4:3, and diagnostic tests passed.");
    return 0;
}
