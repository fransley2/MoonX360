/*
 * Logical 1280x720 screens: XDK records bounded GPU primitives and cached
 * glyphs/images; the host preview and allocation failure path use a canvas.
 * The platform presents each complete frame with ui_present().
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "canvas.h"
#include "navigation.h"

enum ui_icon {
    ICON_PLAY, ICON_LINK, ICON_UNLINK, ICON_MONITOR, ICON_GEAR, ICON_STOP, ICON_REFRESH,
    ICON_INFO, ICON_ERROR, ICON_CHECK
};

enum ui_host_state { HOST_UNKNOWN, HOST_CONNECTING, HOST_OFFLINE, HOST_UNPAIRED, HOST_PAIRED };

struct ui_host {
    const char *name;           /* host name, or NULL if unknown */
    const char *address;        /* IP address, or "" */
    int state;                  /* enum ui_host_state */
    const char *running_app;    /* NULL when nothing is running */
    const char *detail;         /* error text or server version, may be NULL */
};

struct ui_menu_item {
    const char *title;
    const char *subtitle;
    int icon;
    bool enabled;
};

struct ui_app {
    const char *name;
    const uint32_t *art;        /* ui_art_size() pixels, or NULL for a placeholder */
    bool running;
    bool favorite;
    uint32_t generation;        /* texture cache identity across recycled pointers */
};

struct ui_setting {
    const char *label;
    const char *value;
    const char *help;
};

struct ui_stat {
    const char *label;
    char value[64];
};

bool ui_init(int width, int height);
canvas_t *ui_canvas(void);
/* Size that box art should be scaled to before passing it to ui_draw_apps. */
void ui_art_size(int *w, int *h);
/* Columns in the app grid, for d-pad navigation. */
int ui_apps_columns(void);
int ui_apps_rows(void);

void ui_draw_splash(const char *status, uint32_t t_ms);
void ui_draw_main(const struct ui_host *host, const struct ui_menu_item *items, int n, int sel,
                  const char *console_ip, uint32_t t_ms);
void ui_draw_apps(const struct ui_host *host, const struct ui_app *apps, int n, int sel, uint32_t t_ms);
void ui_draw_settings(const struct ui_setting *rows, int n, int sel);
void ui_draw_settings_page(const char *title, const struct ui_setting *rows, int n, int sel);
void ui_draw_settings_options(const char *title,const struct ui_setting *rows,const int *kinds,int n,int sel);
/* Explicit tab/category ancestry for settings and all nested dialogs. -1 clears. */
void ui_set_settings_context(int page,int category);
/* Fixed-size menu ancestry. Restore the returned token on every exit. */
int ui_location_enter(const char *title);
/* Host/game names never pass through the translation dictionary. */
int ui_location_enter_raw(const char *title);
void ui_location_restore(int token);
void ui_location_path(char *out,int capacity,const char *section,const char *leaf);
void ui_draw_ip_editor(const int octets[4], int sel);
void ui_draw_pair(const struct ui_host *host, const char *pin, uint32_t t_ms);
void ui_draw_busy(const char *title, const char *message, uint32_t t_ms);
void ui_draw_host_failure(const char *title,const char *reason);
void ui_draw_confirm(const char *title,const char *message,const char *confirm);
void ui_draw_profiles(const struct ui_setting *rows,int n,int selected,int factory_count);
void ui_draw_message(int icon, const char *title, const char *text);
void ui_draw_stats(const char *title, const struct ui_stat *stats, int n, const char *note);
void ui_draw_stream_stats(const char *title, const struct ui_stat *stats, int n, const char *note, bool retry);
void ui_set_exit_shortcut(int choice);
/* Release the optional static home-screen cache before allocating stream buffers. */
void ui_drop_main_cache(void);
void ui_draw_log(const char *title, const char *const *lines, int count, int first);

/* Platform: shows the canvas on screen (present_xenon.c / the PC preview). */
void ui_present(void);

struct ui_dashboard {
    const char *profile, *connection;
    const struct ui_app *recent;
    int recent_count;
    const struct ui_app *resume; /* independent validated Continue target, or NULL */
    int resume_id;
};
void ui_draw_dashboard(const struct ui_host *host,const struct ui_menu_item *items,int n,int sel,
    const char *console_ip,const struct ui_dashboard *dashboard);
void ui_draw_library(const struct ui_host *host,const struct ui_app *apps,int n,int sel,const char *filter);
void ui_draw_list(const char *title,const struct ui_setting *rows,int n,int sel);
void ui_draw_advisor(const struct ui_stat *rows,int n,const char *note,bool can_apply);

int ui_dashboard_move(int selected,int dx,int dy,int count);
void ui_set_page(int page);
void ui_set_language(int language);
const char *ui_translate(const char *english);
/* Home: compact Continue + up to eight recents and four quick-access tiles. */
int ui_dashboard_move_tiles(int selected,int dx,int dy,int count,int recents);

void ui_draw_app_details(const struct ui_host*,const struct ui_app*,const char *profile,const char *coverage,int selected);
void ui_draw_performance(const char *grade,const char *mode,float shown,const char *decode,
    const char *latency,const char *cause,const char *note,bool can_apply);

void ui_draw_hosts(const struct ui_setting *rows,int n,int sel,int page,bool searching);

/* Global theme, changed only by the local UI outside a streaming session. */
void ui_set_appearance(int background,int accent);
uint32_t ui_theme_accent(void);
void ui_set_motion(bool enabled);
bool ui_focus_tick(uint32_t now);

void ui_set_stream_hud(int menu_style,int mouse_hud);
int ui_stream_menu_style(void);
int ui_mouse_hud_mode(void);

void ui_launch_begin(const struct ui_app *app,uint32_t now,bool motion);
void ui_launch_pair_pin(const char *pin);
void ui_launch_session(const char *profile,int width,int height,int fps,int bitrate_kbps);

void ui_launch_origin(float *x,float *y,float *w,float *h);
void ui_launch_text(uint32_t *pixels,int width,int height);

/* Tick only while Library owns the menu; returns whether its container moved. */
bool ui_library_tick(uint32_t now);

bool ui_home_tick(uint32_t now);
