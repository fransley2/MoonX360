#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include "../../src/config.h"
#include "../../src/crt_output.h"
#include "../../src/video_viewport.h"
#include "../../src/product_features.h"

static std::map<std::string, std::string> files;
char *storage_read(const char *name, size_t *bytes) {
    if (!files.count(name)) { errno = ENOENT; return NULL; }
    if (bytes) *bytes = files[name].size();
    return _strdup(files[name].c_str());
}
bool storage_write(const char *name, const void *p, size_t n) {
    files[name] = std::string((const char *)p, n);
    return true;
}

static void test_beacon_and_evaluation(void) {
    assert(sizeof(struct crt_s240_beacon) == 36);
    assert(CRT_S240_MAGIC == 0x53323430u);
    assert(CRT_S240_VERSION == 3u);

    struct crt_output_status st;
    memset(&st, 0, sizeof(st));
    crt_output_evaluate(&st, 0);
    assert(!st.retro_240p_available && !st.active_240p && st.reason == CRT_REASON_HOST_ONLY);

    /* HiDef console output */
    memset(&st, 0, sizeof(st));
    st.display_width = 1280; st.display_height = 720; st.hidef = 1; st.refresh_hz_x100 = 6000; st.plugin_ver = 0x00021700u;
    crt_output_evaluate(&st, 1);
    assert(st.console_output_kind == CRT_OUTPUT_HIDEF && !st.retro_240p_available && st.reason == CRT_REASON_HIDEF_MODE);

    /* PAL 50Hz output */
    memset(&st, 0, sizeof(st));
    st.display_width = 640; st.display_height = 576; st.interlaced = 1; st.refresh_hz_x100 = 5000; st.plugin_ver = 0x00021700u;
    crt_output_evaluate(&st, 1);
    assert(st.console_output_kind == CRT_OUTPUT_PAL50_OR_OTHER && !st.retro_240p_available && st.reason == CRT_REASON_NOT_60HZ);

    /* Progressive 480p without active 240p */
    memset(&st, 0, sizeof(st));
    st.display_width = 640; st.display_height = 480; st.interlaced = 0; st.refresh_hz_x100 = 5994; st.plugin_ver = 0x00021700u;
    crt_output_evaluate(&st, 1);
    assert(st.console_output_kind == CRT_OUTPUT_480P && !st.retro_240p_available && st.reason == CRT_REASON_NOT_INTERLACED);

    /* 480i without t240.xex */
    memset(&st, 0, sizeof(st));
    st.display_width = 640; st.display_height = 480; st.interlaced = 1; st.refresh_hz_x100 = 5994; st.plugin_ver = 0u;
    crt_output_evaluate(&st, 1);
    assert(st.console_output_kind == CRT_OUTPUT_480I && !st.plugin_detected && !st.retro_240p_available && st.reason == CRT_REASON_NO_PLUGIN);

    /* 480i with too-old plugin (< 2.0) */
    st.plugin_ver = 0x00010500u;
    crt_output_evaluate(&st, 1);
    assert(st.plugin_detected && !st.retro_240p_available && st.reason == CRT_REASON_PLUGIN_VERSION);

    /* 480i + verified v2.23 ready (not yet active) */
    st.plugin_ver = 0x00021700u; st.plugin_mode = CRT_PLUGIN_MODE_480I; st.backbuffer_240p = 0;
    crt_output_evaluate(&st, 1);
    assert(st.retro_240p_available && !st.active_240p && st.reason == CRT_REASON_READY);

    /* 640x240 backbuffer alone must NOT fake 240p active without plugin confirmation */
    st.backbuffer_240p = 1; st.plugin_mode = CRT_PLUGIN_MODE_480I;
    crt_output_evaluate(&st, 1);
    assert(!st.active_240p && st.reason == CRT_REASON_READY);

    /* Active 240p confirmed only when both backbuffer 640x240 AND plugin_mode == 1 match */
    st.backbuffer_240p = 1; st.plugin_mode = CRT_PLUGIN_MODE_240P;
    crt_output_evaluate(&st, 1);
    assert(st.retro_240p_available && st.active_240p && st.reason == CRT_REASON_ACTIVE_240P);
    assert(!strcmp(crt_output_mode_short(&st), "240p"));

    char ver[24];
    crt_output_format_version(0x00021700u, ver, sizeof(ver));
    assert(!strcmp(ver, "v2.23"));
    crt_output_format_version(0u, ver, sizeof(ver));
    assert(!strcmp(ver, "None"));
    assert(strstr(crt_output_reason_text(CRT_REASON_NO_PLUGIN), "t240.xex"));
}

static void test_crt_viewport_geometry(void) {
    int l, t, r, b;
    /* 640x480 into 640x480 */
    video_crt_fit_rect(640, 480, 640, 480, CRT_ASPECT_4_3, 0, 0, 0, 0, &l, &t, &r, &b);
    assert(l == 0 && t == 0 && r == 640 && b == 480);

    /* 640x240 native 240p scanout into 640x240 backbuffer */
    video_crt_fit_rect(640, 240, 640, 240, CRT_ASPECT_4_3, 1, 0, 0, 0, &l, &t, &r, &b);
    assert(l == 0 && t == 0 && r == 640 && b == 240);

    /* Widescreen 16:9 MUST letterbox on 4:3 (never stretch) */
    video_crt_fit_rect(1280, 720, 640, 480, CRT_ASPECT_4_3, 0, 0, 0, 0, &l, &t, &r, &b);
    assert(l == 0 && r == 640 && t == 60 && b == 420);

    /* Widescreen 16:9 MUST letterbox into 640x240 physical 4:3 screen (640x180 centered vertically) */
    video_crt_fit_rect(1280, 720, 640, 240, CRT_ASPECT_4_3, 1, 0, 0, 0, &l, &t, &r, &b);
    assert(l == 0 && r == 640 && t == 30 && b == 210);

    /* 640x480 on 1280x720 stays pillarboxed cleanly in both 4:3 and Original modes */
    video_crt_fit_rect(640, 480, 1280, 720, CRT_ASPECT_4_3, 0, 0, 0, 0, &l, &t, &r, &b);
    assert(l == 160 && t == 0 && r == 1120 && b == 720);
    video_crt_fit_rect(640, 480, 1280, 720, CRT_ASPECT_ORIGINAL, 0, 0, 0, 0, &l, &t, &r, &b);
    assert(l == 160 && t == 0 && r == 1120 && b == 720);

    /* 1280x720 on 1280x720: 4:3 mode pillarboxes 4:3 box then letterboxes 16:9; Original uses full 1280x720 */
    video_crt_fit_rect(1280, 720, 1280, 720, CRT_ASPECT_4_3, 0, 0, 0, 0, &l, &t, &r, &b);
    assert(l == 160 && r == 1120 && t == 90 && b == 630);
    video_crt_fit_rect(1280, 720, 1280, 720, CRT_ASPECT_ORIGINAL, 0, 0, 0, 0, &l, &t, &r, &b);
    assert(l == 0 && t == 0 && r == 1280 && b == 720);

    /* Bounded shift (-20..+20) + optional safe area never crop source dimensions proportionally */
    video_crt_fit_rect(640, 480, 640, 480, CRT_ASPECT_4_3, 0, 5, 50, -50, &l, &t, &r, &b);
    assert((r - l) == 576 && (b - t) == 432);
    assert(l == (640 - 576) / 2 + 20 && t == (480 - 432) / 2 - 20);
}

static void test_consolidated_profiles_and_v6_persistence(void) {
    int visible[CONFIG_PROFILE_COUNT];
    int vcount = config_visible_factory_profiles(visible, CONFIG_PROFILE_COUNT);
    assert(vcount == 8);
    assert(visible[6] == CONFIG_PROFILE_CRT_DISPLAY && visible[7] == CONFIG_PROFILE_CRT_RETRO_240P);
    for (int i = 0; i < vcount; i++) assert(!config_profile_is_legacy(visible[i]));
    for (int id = CONFIG_PROFILE_CRT; id <= CONFIG_PROFILE_CRT_QUALITY; id++) {
        assert(config_profile_is_legacy(id) && !config_profile_is_visible(id));
    }

    struct config c;
    config_defaults(&c);
    config_select_factory(&c, CONFIG_PROFILE_CRT_DISPLAY);
    assert(config_crt_profile(&c) && !config_is_crt_retro(&c));
    assert(c.width == 640 && c.height == 480 && c.fps == 60 && c.bitrate_kbps == 3000);
    assert(!c.fast_decode && !c.vsync && c.renderer == 0 && c.packet_size == 1024);
    assert(c.image_quality == 0 && c.crt_scaling == CRT_SCALE_AUTO && c.crt_aspect == CRT_ASPECT_4_3);
    assert(c.refresh_interval_ms == 0 && c.refresh_smoothing == 0 && !c.sops && !c.crt_clarity);
    assert(config_match_factory(&c) == CONFIG_PROFILE_CRT_DISPLAY);

    config_select_factory(&c, CONFIG_PROFILE_CRT_RETRO_240P);
    assert(config_crt_profile(&c) && config_is_crt_retro(&c));
    assert(c.width == 640 && c.height == 240 && c.fps == 60 && c.bitrate_kbps == 3000);
    assert(!c.fast_decode && c.vsync && c.renderer == 0 && c.packet_size == 1024);
    assert(c.image_quality == 0 && c.crt_scaling == CRT_SCALE_POINT && c.crt_aspect == CRT_ASPECT_4_3);
    assert(c.refresh_interval_ms == 0 && c.refresh_smoothing == 0 && !c.sops && !c.crt_clarity);
    assert(config_match_factory(&c) == CONFIG_PROFILE_CRT_RETRO_240P);

    /* Custom derivation preserves CRT context and 640x240 survives save/load + XENON_PROFILES 6 with negative offsets */
    files.clear();
    c.crt_scaling = CRT_SCALE_SHARP_BILINEAR;
    c.crt_aspect = CRT_ASPECT_ORIGINAL;
    c.crt_pos_x = -12;
    c.crt_pos_y = 7;
    config_customize_stream(&c);
    assert(config_crt_profile(&c) && config_is_crt_retro(&c));
    assert(config_save(&c));

    struct config loaded;
    config_load(&loaded);
    assert(loaded.width == 640 && loaded.height == 240);
    assert(loaded.crt_scaling == CRT_SCALE_SHARP_BILINEAR && loaded.crt_aspect == CRT_ASPECT_ORIGINAL);
    assert(loaded.crt_pos_x == -12 && loaded.crt_pos_y == 7);

    struct user_profiles up = {}; up.slot = -1;
    assert(config_profiles_create(&up, &loaded, "Retro 240p Offset"));
    struct user_profiles up_loaded = {};
    assert(config_profiles_load(&up_loaded) && up_loaded.count == 1);
    struct config applied;
    config_defaults(&applied);
    assert(config_profiles_apply(&up_loaded, 0, &applied));
    assert(applied.width == 640 && applied.height == 240);
    assert(applied.crt_scaling == CRT_SCALE_SHARP_BILINEAR && applied.crt_aspect == CRT_ASPECT_ORIGINAL);
    assert(applied.crt_pos_x == -12 && applied.crt_pos_y == 7);
    assert(config_profiles_selected(&up_loaded, &applied) == 0);
}

int main(void) {
    test_beacon_and_evaluation();
    test_crt_viewport_geometry();
    test_consolidated_profiles_and_v6_persistence();
    puts("PASS: S240 v3 validation, 240p state guards, non-stretching CRT viewport geometry, 2 visible CRT profiles + hidden legacy IDs, 640x240 & v6 persistence.");
    return 0;
}
