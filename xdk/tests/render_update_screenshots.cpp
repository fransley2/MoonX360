/* Render visual verification screenshots of MoonX360 App Updates UI & Home badge
 * at 1280x720 (HD) and 640x480 (CRT) into evidencias/auto-update/. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/config.h"
extern "C" {
#include "../../src/ui/ui.h"
}

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../third_party/stb/stb_image_write.h"

static const char *g_out_dir;
static const char *g_shot_name;

void ui_present(void)
{
    canvas_t *c = ui_canvas();
    unsigned char *rgb = (unsigned char *)malloc((size_t)c->w * (size_t)c->h * 3u);
    char path[512];
    for (int i = 0; i < c->w * c->h; i++) {
        rgb[i * 3 + 0] = (unsigned char)((c->px[i] >> 16) & 255);
        rgb[i * 3 + 1] = (unsigned char)((c->px[i] >> 8) & 255);
        rgb[i * 3 + 2] = (unsigned char)(c->px[i] & 255);
    }
    snprintf(path, sizeof(path), "%s/%s.png", g_out_dir, g_shot_name);
    stbi_write_png(path, c->w, c->h, 3, rgb, c->w * 3);
    free(rgb);
    printf("wrote %s\n", path);
}

static void render_set(int w, int h, int layout, int lang, const char *suffix)
{
    ui_set_language(lang);
    ui_init_layout(w, h, layout);
    ui_set_appearance(0, 0);

    struct ui_update_view v;
    memset(&v, 0, sizeof(v));
    v.installed_version = "0.2.7";
    v.installed_build = 100;
    v.available_version = "0.2.8";
    v.available_build = 101;
    v.channel = 0;
    v.auto_check = true;
    v.has_previous = true;
    v.download_total = 13107200ull;

    /* 1. Update Available */
    v.state = 3;
    v.status_text = "Update available.";
    v.notes = lang == CONFIG_LANGUAGE_PT_BR
        ? "Otimizacoes na decodificacao de video e correcao de estabilidade no modo CRT."
        : lang == CONFIG_LANGUAGE_SPANISH
        ? "Optimizaciones de decodificacion de video y correccion de estabilidad en modo CRT."
        : "Video decode optimizations and CRT stability improvements.";
    char name[96];
    snprintf(name, sizeof(name), "updates_available_%s", suffix);
    g_shot_name = name;
    ui_draw_app_updates(&v, 0, 500);
    ui_present();

    /* 2. Downloading (64%, 1.85 MB/s) */
    v.state = 4;
    v.status_text = "Downloading.";
    v.download_done = 8388608ull;
    v.progress_pct = 64;
    v.speed_bps = 1940000u;
    snprintf(name, sizeof(name), "updates_downloading_%s", suffix);
    g_shot_name = name;
    ui_draw_app_updates(&v, 0, 800);
    ui_present();

    /* 3. Update Ready / Restart required */
    v.state = 6;
    v.status_text = "Restart required.";
    v.download_done = v.download_total;
    v.progress_pct = 100;
    v.speed_bps = 0;
    snprintf(name, sizeof(name), "updates_ready_%s", suffix);
    g_shot_name = name;
    ui_draw_app_updates(&v, 0, 1100);
    ui_present();

    /* 4. Home Dashboard with compact update badge */
    struct ui_host host = { "DESKTOP-GAMING", "192.168.15.20", HOST_PAIRED, NULL, "Sunshine 2026" };
    struct ui_menu_item items[9];
    memset(items, 0, sizeof(items));
    items[0].title = "Home"; items[0].enabled = true;
    items[1].title = "Library"; items[1].enabled = true;
    items[2].title = "Computers"; items[2].enabled = true;
    items[3].title = "Settings"; items[3].enabled = true;
    items[4].title = "Start playing"; items[4].icon = ICON_PLAY; items[4].enabled = true;
    items[5].title = "My Library"; items[5].icon = ICON_PLAY; items[5].enabled = true;
    items[6].title = "Stream Settings"; items[6].icon = ICON_MONITOR; items[6].enabled = true;
    items[7].title = "Connect to PC"; items[7].icon = ICON_LINK; items[7].enabled = true;
    items[8].title = "My Computers"; items[8].icon = ICON_MONITOR; items[8].enabled = true;
    struct ui_dashboard dash;
    memset(&dash, 0, sizeof(dash));
    dash.profile = "Smooth (720p60)";
    dash.connection = "720p60  \xE2\x80\xA2  10 Mbps";
    dash.update_badge = "Update available";
    snprintf(name, sizeof(name), "home_badge_%s", suffix);
    g_shot_name = name;
    ui_draw_dashboard(&host, items, 9, 4, "192.168.15.9", &dash);
    ui_present();
}

int main(int argc, char **argv)
{
    g_out_dir = argc > 1 ? argv[1] : ".";
    render_set(1280, 720, UI_LAYOUT_16_9, CONFIG_LANGUAGE_ENGLISH, "hd_en");
    render_set(640, 480, UI_LAYOUT_4_3, CONFIG_LANGUAGE_PT_BR, "crt_ptbr");
    render_set(1280, 720, UI_LAYOUT_16_9, CONFIG_LANGUAGE_SPANISH, "hd_es");
    return 0;
}
