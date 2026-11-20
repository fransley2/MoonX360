#include "ui.h"
#include "../performance.h"
#include "anim.h"
#include "design_system.h"
#include "smart_keyboard.h"
#include "font.h"
#include "draw.h"
#include "translations.h"
#include "brand.h"
#include "layout.h"
#include "../config.h"
#include "../input.h"
#include "../settings_layout.h"

/* The same layout emits GPU commands on XDK and pixels in the host preview. */
#define cv_fill ui_fill
#define cv_vgradient ui_gradient
#define cv_rrect ui_round
#define cv_rrect_outline ui_outline
#define cv_shadow ui_shadow
#define cv_circle ui_circle
#define cv_ring ui_ring
#define cv_line ui_line
#define cv_triangle ui_triangle
#define cv_image ui_picture

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------ theme (Design System tokens) */

#define C_BG_TOP     DS_BG_TOP
#define C_BG_BOTTOM  DS_BG_BASE
#define C_ACCENT     theme_accent
#define C_CARD       DS_BG_SURFACE
#define C_CARD_SEL   theme_selection
#define C_BORDER     DS_BORDER_SUBTLE
#define C_TEXT       DS_TEXT_PRIMARY
#define C_TEXT_DIM   DS_TEXT_SECONDARY
#define C_TEXT_FAINT DS_TEXT_MUTED
#define C_GREEN      DS_STATUS_OK
#define C_YELLOW     DS_STATUS_WARN
#define C_RED        DS_STATUS_ERROR
#define C_BTN_A      DS_BTN_A
#define C_BTN_B      DS_BTN_B
#define C_BTN_X      DS_BTN_X
#define C_BTN_Y      DS_BTN_Y
#define C_SHADOW     ARGB(150, 0, 0, 0)

static uint32_t theme_accent=HEX(0x72C840),theme_selection=HEX(0x233829);
static int theme_background_id,theme_accent_id;
static int stream_menu_style,mouse_hud_mode;
void ui_set_stream_hud(int style,int hud){stream_menu_style=style==1?1:0;mouse_hud_mode=hud>=0 && hud<3?hud:0;}
int ui_stream_menu_style(void){return stream_menu_style;}
int ui_mouse_hud_mode(void){return mouse_hud_mode;}
static unsigned theme_generation=1;
static uint32_t theme_icon[128*128];
static canvas_t cv;     /* the frame being drawn */
static canvas_t bg;     /* pre-rendered background */
static float s;         /* design px -> screen px */
static float W, H;      /* 1280x720 wide or 960x720 compact design space */
static int exit_shortcut;
static canvas_t main_cache;
static bool main_cache_valid,main_cache_gpu;
static uint64_t main_cache_key;
static int ui_language;
#include "quick_translations.h"
const char *ui_translate(const char *english)
{
    if(!english)return "";
    if(!ui_language)return english;
    int lo=0,hi=(int)(sizeof(ui_translations)/sizeof(ui_translations[0]))-1;
    while(lo<=hi){int m=(lo+hi)/2,c=strcmp(english,ui_translations[m].en);
        if(!c){const char *translated=ui_language==CONFIG_LANGUAGE_PT_BR?ui_translations[m].pt_br:ui_translations[m].es;
            return translated && *translated?translated:english;}
        if(c<0)hi=m-1;else lo=m+1;}
    for(size_t i=0;i<sizeof(quick_translations)/sizeof(quick_translations[0]);i++)
        if(!strcmp(english,quick_translations[i].en))
            return ui_language==CONFIG_LANGUAGE_PT_BR?quick_translations[i].pt_br:quick_translations[i].es;
    return english;
}
void ui_set_language(int language)
{
    ui_language=language>=0 && language<CONFIG_LANGUAGE_COUNT?language:CONFIG_LANGUAGE_ENGLISH;
    ui_drop_main_cache();
}
int ui_get_language(void){return ui_language;}
static int active_page;
static int home_wall_previous=-1;
void ui_home_cancel_prepare(void);
static int settings_context_page=-1,settings_context_category=-1;
void ui_set_settings_context(int page,int category)
{
    settings_context_page=page;settings_context_category=category;
    if(page>=0)ui_set_page(page);
}
void ui_set_page(int page) {int next=page>=0&&page<UI_PAGE_COUNT?page:UI_HOME;
    if(next!=active_page){ui_transition_begin();if(next!=UI_HOME){home_wall_previous=-1;ui_home_cancel_prepare();}}active_page=next;}

static char location_parts[8][80];
static bool location_raw[8];
static int location_depth;
static const char *location_pages[]={"Home","Library","Stream","Settings"};
static int location_enter(const char *title,bool raw)
{
    int token=location_depth;
    if(!location_depth){location_raw[0]=false;snprintf(location_parts[location_depth++],80,"%s",location_pages[active_page]);}
    if(title && *title && (raw || strcmp(title,location_parts[location_depth-1])) && location_depth<8) {
        location_raw[location_depth]=raw;
        snprintf(location_parts[location_depth++],80,"%s",title);
    }
    return token;
}
int ui_location_enter(const char *title){return location_enter(title,false);}
int ui_location_enter_raw(const char *title){return location_enter(title,true);}
void ui_location_restore(int token) {if(token>=0 && token<=location_depth)location_depth=token;}
void ui_location_path(char *out,int capacity,const char *section,const char *leaf)
{
    if(capacity<=0)return;
    out[0]=0;const char *last=NULL;
    for(int i=0;i<location_depth+2;i++) {
        const char *part=i<location_depth?(location_raw[i]?location_parts[i]:ui_translate(location_parts[i])):i==location_depth?section:leaf;
        if(!part || !*part || (last && !strcmp(last,part)))continue;
        size_t used=strlen(out);
        if(used<(size_t)capacity)snprintf(out+used,(size_t)capacity-used,"%s%s",used?"  >  ":"",part);
        last=part;
    }
    if(!out[0])snprintf(out,(size_t)capacity,"%s",ui_translate(location_pages[active_page]));
}

void ui_drop_main_cache(void)
{
    if (main_cache.px) cv_free(&main_cache);
    main_cache_valid = false;
    ui_scene_drop();
}

void ui_set_exit_shortcut(int choice)
{
    exit_shortcut = choice;
}

#define S(v) ((v) * s)
#define SI(v) ((int)((v) * s + 0.5f))

/* ------------------------------------------------------------ setup */

/* Static geometric wallpaper: identical CPU/GPU geometry, no image slot,
 * per-frame animation, blur, or extra full-screen texture. */
static void theme_background(canvas_t *target)
{
    cv_vgradient(target,0,0,target->w,target->h,HEX(0x172B35),C_BG_BOTTOM);
    if(theme_background_id==2)return;
    bool midnight=theme_background_id==1;
    cv_triangle(target,S(W-870),0,S(W),0,S(W),S(510),(midnight?HEX(0x182535):HEX(0x19272A)));
    cv_triangle(target,S(W-870),0,S(W),S(510),S(W-290),S(660),(midnight?HEX(0x16202E):HEX(0x172126)));
    cv_triangle(target,S(W-620),0,S(W),0,S(W),S(390),(midnight?HEX(0x24364C):HEX(0x20332D)));
    cv_triangle(target,S(W-620),0,S(W),S(390),S(W-128),S(404),(midnight?HEX(0x1E2B3D):HEX(0x1C2C28)));
    cv_line(target,S(W-620),0,S(W-128),S(404),S(1),(midnight?HEX(0x3A4D64):HEX(0x30453B)));
    cv_fill(target,0,0,target->w,SI(132),ARGB(100,12,16,19));
}
static void draw_background(void)
{
    theme_background(&bg);
}

uint32_t ui_theme_accent(void){return theme_accent;}
void ui_set_appearance(int background,int accent)
{
    static const uint32_t colors[]={0xFF72C840,0xFF70B8FF,0xFFE1B458,0xFFB69CF0};
    if(background<0 || background>=5)background=0;
    if(accent<0 || accent>=4)accent=0;
    if(background==theme_background_id && accent==theme_accent_id)return;
    if(accent!=theme_accent_id) {
        theme_accent=colors[accent];theme_generation++;
        for(int i=0;i<128*128;i++)theme_icon[i]=(moonx360_icon[i]&0xFF000000u)|(theme_accent&0xFFFFFFu);
    }
    theme_background_id=background;theme_accent_id=accent;
    theme_selection=accent?cv_mix(C_CARD,theme_accent,34):HEX(0x233829);
    ui_drop_main_cache();
    if(bg.px)cv_free(&bg); /* CPU fallback regenerates once on its next redraw. */
}

static void reset_scroll(void);
bool ui_init_layout(int width, int height, int layout_4_3)
{
    reset_scroll();
    ui_drop_main_cache();
    if (!font_init())
        return false;
    if(cv.px)cv_free(&cv);
    if(bg.px)cv_free(&bg);
    if (!cv_init(&cv, width, height))
        return false;
    s = height / 720.0f;
    if(width/s<(layout_4_3?960.0f:1280.0f))s=width/(layout_4_3?960.0f:1280.0f);
    H = height / s;
    W = width / s;
    if(!ui_draw_enabled()) {if(!cv_init(&bg,width,height))return false;draw_background();}
    return true;
}
bool ui_init(int width,int height){return ui_init_layout(width,height,0);}

canvas_t *ui_canvas(void)
{
    return &cv;
}

#define TILE_W 150
#define TILE_H 225

/* Last selected art rectangle, also available for placeholders/software UI. */
static char launch_source_name[64];
static float launch_source[4];
static void launch_source_tile(const struct ui_app *app,float x,float y,float w,float h)
{
    if(!app || !app->name)return;
    snprintf(launch_source_name,sizeof(launch_source_name),"%s",app->name);
    launch_source[0]=x;launch_source[1]=y;launch_source[2]=w;launch_source[3]=h;
}
#define TILE_GAP UI_CARD_GAP
#define LIBRARY_W 174
#define LIBRARY_H 232

void ui_art_size(int *w, int *h)
{
    *w = SI(TILE_W);
    *h = SI(TILE_H);
}

int ui_apps_columns(void)
{
    int cols = (int)((W - 144 + TILE_GAP) / (LIBRARY_W + TILE_GAP));
    return cols < 1 ? 1 : cols;
}

int ui_apps_rows(void)
{
    int rows=(int)((H-170-198)/(LIBRARY_H+52));return rows>0?rows:1;
}

/* ------------------------------------------------------------ helpers */

static void text(int face, float px, float x, float y, uint32_t color, const char *str)
{
    font_draw(&cv, face, S(px), SI(x), SI(y), color, str);
}

static void text_fit(int face, float px, float x, float y, float max_w, uint32_t color, const char *str)
{
    font_draw_fit(&cv, face, S(px), SI(x), SI(y), SI(max_w), color, str);
}

static float text_w(int face, float px, const char *str)
{
    return font_text_width(face, S(px), str) / s;
}

static void text_center(int face, float px, float cx, float y, uint32_t color, const char *str)
{
    text(face, px, cx - text_w(face, px, str) / 2, y, color, str);
}

static void text_right(int face, float px, float rx, float y, uint32_t color, const char *str)
{
    text(face, px, rx - text_w(face, px, str), y, color, str);
}

static int wrapped(int face, float px, float x, float y, float w, float line_h, uint32_t color, const char *str)
{
    return font_draw_wrapped(&cv, face, S(px), SI(x), SI(y), SI(w), S(line_h), color, str);
}

static int wrapped_lines(int face, float px, float w, const char *str)
{
    return font_draw_wrapped(NULL, face, S(px), 0, 0, SI(w), 0, 0, str);
}

static void rrect(float x, float y, float w, float h, float r, uint32_t c)
{
    cv_rrect(&cv, S(x), S(y), S(w), S(h), S(r), c);
}

static void card(float x, float y, float w, float h, float r, bool selected)
{
    rrect(x,y,w,h,r,selected?C_CARD_SEL:C_CARD);
    cv_rrect_outline(&cv,S(x),S(y),S(w),S(h),S(r),S(selected?UI_FOCUS_BORDER:1),selected?C_ACCENT:WITH_ALPHA(C_BORDER,100));
}

static uint32_t state_color(int state)
{
    switch (state) {
    case HOST_PAIRED:   return C_GREEN;
    case HOST_UNPAIRED: return C_YELLOW;
    case HOST_OFFLINE:  return C_RED;
    default:            return C_TEXT_FAINT;
    }
}

static const char *state_text(int state)
{
    switch (state) {
    case HOST_PAIRED:     return ui_translate("Online / Paired");
    case HOST_UNPAIRED:   return ui_translate("Pairing required");
    case HOST_OFFLINE:    return ui_translate("Offline");
    case HOST_CONNECTING: return ui_translate("Connecting...");
    default:              return ui_translate("No host set");
    }
}

/* ------------------------------------------------------------ icons */

struct icon_ctx { float cx, cy, k; };

static void moon_logo(float cx, float cy, float r)
{
    /* Shared 128px brand bitmap: one existing image command, no runtime decoder. */
    ui_picture_region(&cv,theme_accent_id?theme_icon:moonx360_icon,128,128,0,0,128,128,
                      S(cx-r),S(cy-r),S(r*2),S(r*2),255,theme_generation);
}

static float gear_sdf(float x, float y, const void *p)
{
    const struct icon_ctx *c = (const struct icon_ctx *)p;
    float dx = x - c->cx, dy = y - c->cy;
    float len = sqrtf(dx * dx + dy * dy);
    float teeth = cosf(8.0f * atan2f(dy, dx)) * 3.0f;
    float outer;

    teeth = teeth > 1 ? 1 : teeth < -1 ? -1 : teeth;
    outer = len - (c->k * 0.70f + c->k * 0.17f * teeth);
    return fmaxf(outer, -(len - c->k * 0.30f));
}

struct arc_ctx { float cx, cy, r, t, gap0, gap1; };

static float arc_sdf(float x, float y, const void *p)
{
    const struct arc_ctx *c = (const struct arc_ctx *)p;
    float dx = x - c->cx, dy = y - c->cy;
    float a = atan2f(dy, dx);

    if (a > c->gap0 && a < c->gap1) {
        float e0x = c->cx + cosf(c->gap0) * c->r, e0y = c->cy + sinf(c->gap0) * c->r;
        float e1x = c->cx + cosf(c->gap1) * c->r, e1y = c->cy + sinf(c->gap1) * c->r;
        float d0 = sqrtf((x - e0x) * (x - e0x) + (y - e0y) * (y - e0y));
        float d1 = sqrtf((x - e1x) * (x - e1x) + (y - e1y) * (y - e1y));
        return fminf(d0, d1) - c->t * 0.5f;
    }
    return fabsf(sqrtf(dx * dx + dy * dy) - c->r) - c->t * 0.5f;
}

/* Draws an icon centred at (cx, cy); k is its half-size in design px. */
static void icon(int which, float cx, float cy, float k, uint32_t color)
{
    float X = S(cx), Y = S(cy), K = S(k);
    struct icon_ctx ctx = { X, Y, K };

    switch (which) {
    case ICON_PLAY:
        cv_triangle(&cv, X - K * 0.45f, Y - K * 0.65f, X - K * 0.45f, Y + K * 0.65f, X + K * 0.7f, Y, color);
        break;
    case ICON_LINK:
    case ICON_UNLINK:
        cv_ring(&cv, X - K * 0.32f, Y, K * 0.42f, K * 0.17f, color);
        cv_ring(&cv, X + K * 0.32f, Y, K * 0.42f, K * 0.17f, color);
        if (which == ICON_UNLINK)
            cv_line(&cv, X - K * 0.75f, Y + K * 0.75f, X + K * 0.75f, Y - K * 0.75f, K * 0.17f, color);
        break;
    case ICON_MONITOR:
        cv_rrect_outline(&cv, X - K * 0.85f, Y - K * 0.65f, K * 1.7f, K * 1.1f, K * 0.15f, K * 0.16f, color);
        cv_line(&cv, X, Y + K * 0.45f, X, Y + K * 0.75f, K * 0.16f, color);
        cv_line(&cv, X - K * 0.4f, Y + K * 0.8f, X + K * 0.4f, Y + K * 0.8f, K * 0.16f, color);
        break;
    case ICON_GEAR:
        if(ui_draw_enabled()) {cv_ring(&cv,X,Y,K*.65f,K*.25f,color);for(int i=0;i<8;i++) {
            float a=(float)i*3.14159265f/4;cv_line(&cv,X+cosf(a)*K*.55f,Y+sinf(a)*K*.55f,X+cosf(a)*K*.92f,Y+sinf(a)*K*.92f,K*.22f,color);}}
        else cv_sdf(&cv, X - K, Y - K, 2 * K, 2 * K, gear_sdf, &ctx, color);
        break;
    case ICON_STOP:
        cv_rrect(&cv, X - K * 0.55f, Y - K * 0.55f, K * 1.1f, K * 1.1f, K * 0.18f, color);
        break;
    case ICON_REFRESH: {
        struct arc_ctx a = { X, Y, K * 0.6f, K * 0.17f, -0.9f, -0.1f };
        float ex = X + cosf(-0.1f) * K * 0.6f, ey = Y + sinf(-0.1f) * K * 0.6f;
        if(ui_draw_enabled())cv_ring(&cv,X,Y,K*.6f,K*.17f,color);
        else cv_sdf(&cv, X - K, Y - K, 2 * K, 2 * K, arc_sdf, &a, color);
        cv_triangle(&cv, ex - K * 0.3f, ey, ex + K * 0.3f, ey, ex, ey + K * 0.35f, color);
        break;
    }
    case ICON_INFO:
    case ICON_ERROR: {
        const char *g = which == ICON_INFO ? "i" : "!";
        float px = k * 1.3f;
        text_center(FONT_BOLD, px, cx, cy + px * 0.36f, color, g);
        break;
    }
    case ICON_CHECK:
        cv_line(&cv, X - K * 0.5f, Y, X - K * 0.12f, Y + K * 0.4f, K * 0.2f, color);
        cv_line(&cv, X - K * 0.12f, Y + K * 0.4f, X + K * 0.55f, Y - K * 0.4f, K * 0.2f, color);
        break;
    }
}

static void spinner(float cx, float cy, float r, uint32_t t_ms)
{
    /* Unified smooth Xbox-style spinning ring (900ms period, 225 deg sweeping arc).
     * 24 fixed-geometry segments around the ring with smooth per-segment color/alpha
     * sweep: matches the GPU opening shader and batches into 1 solid_ui draw call. */
    const int steps = 24;
    const float two_pi = 6.2831853f;
    float thick = r * 0.22f;
    if (thick < 2.2f) thick = 2.2f;
    float phase = (float)(t_ms % 900u) / 900.0f;

    for (int i = 0; i < steps; i++) {
        float a0 = (float)i / (float)steps * two_pi - 1.5707963f;
        float a1 = (float)(i + 1) / (float)steps * two_pi - 1.5707963f;
        float pos = ((float)i + 0.5f) / (float)steps;
        float ang = phase - pos;
        if (ang < 0.0f) ang += 1.0f;
        float tail = 1.0f - ang / 0.625f;
        if (tail < 0.0f) tail = 0.0f;
        float fade = tail * tail;
        uint32_t rgb = cv_mix(C_BORDER, C_ACCENT, (unsigned)(fade * 255.0f));
        unsigned alpha = (unsigned)(105.0f + 150.0f * fade);
        cv_line(&cv,
                S(cx + cosf(a0) * r), S(cy + sinf(a0) * r),
                S(cx + cosf(a1) * r), S(cy + sinf(a1) * r),
                S(thick), WITH_ALPHA(rgb, alpha));
    }
}

/* ------------------------------------------------------------ chrome */

struct hint { const char *button; const char *label; };

static void keycap(float x, float baseline, const char *button, float *out_w)
{
    if(!strcmp(button,"LB/RB") || !strcmp(button,"LB / RB")) {
        ui_button(&cv,"LB",SI(x),SI(baseline-22),SI(35),SI(28));
        ui_button(&cv,"RB",SI(x+38),SI(baseline-22),SI(35),SI(28));
        *out_w=73.0f;return;
    }
    if(!strcmp(button,"X Y")) {
        ui_button(&cv,"X",SI(x),SI(baseline-22),SI(28),SI(28));
        ui_button(&cv,"Y",SI(x+31),SI(baseline-22),SI(28),SI(28));
        *out_w=59.0f;return;
    }
    if(!strcmp(button,"Back + L3")) {
        ui_button(&cv,"Back",SI(x),SI(baseline-22),SI(28),SI(28));
        text(FONT_BOLD,14,x+31,baseline-2,C_TEXT_DIM,"+");
        ui_button(&cv,"L3",SI(x+44),SI(baseline-22),SI(28),SI(28));
        *out_w=72.0f;return;
    }
    bool shoulder=!strcmp(button,"LB") || !strcmp(button,"RB") || !strcmp(button,"LT") || !strcmp(button,"RT");
    int bw=shoulder?35:28;
    if(ui_button(&cv,button,SI(x),SI(baseline-22),SI(bw),SI(28))) {*out_w=(float)bw;return;}
    uint32_t color = 0;
    float cy = baseline - 6;

    if (!strcmp(button, "A")) color = C_BTN_A;
    else if (!strcmp(button, "B")) color = C_BTN_B;
    else if (!strcmp(button, "X")) color = C_BTN_X;
    else if (!strcmp(button, "Y")) color = C_BTN_Y;

    if (color) {
        cv_circle(&cv, S(x + 13), S(cy), S(13), color);
        text_center(FONT_BOLD, 15, x + 13, cy + 5.5f, HEX(0xFFFFFF), button);
        *out_w = 26;
    } else {
        button=ui_translate(button);
        float w = text_w(FONT_BOLD, 13, button) + 16;
        rrect(x, cy - 12, w, 24, DS_RADIUS_MD, HEX(0x2C3646));
        text_center(FONT_BOLD, 13, x + w / 2, cy + 4.5f, C_TEXT, button);
        *out_w = w;
    }
}

/* ---- Non-blocking Toast notification component ---- */
static struct {
    bool active;
    int kind;
    char text[128];
    uint32_t started, duration_ms, now;
} ui_toast;

void ui_toast_show(int kind, const char *msg, uint32_t now_ms, uint32_t duration_ms)
{
    if (!msg || !*msg) return;
    ui_toast.active = true;
    ui_toast.kind = kind;
    snprintf(ui_toast.text, sizeof(ui_toast.text), "%s", ui_translate(msg));
    ui_toast.started = now_ms;
    ui_toast.now = now_ms;
    ui_toast.duration_ms = duration_ms ? duration_ms : UI_TOAST_DEFAULT_MS;
}

bool ui_toast_tick(uint32_t now_ms)
{
    if (!ui_toast.active) return false;
    ui_toast.now = now_ms;
    uint32_t elapsed = (uint32_t)(now_ms - ui_toast.started);
    if (elapsed >= ui_toast.duration_ms) {
        ui_toast.active = false;
        return true;
    }
    return elapsed < UI_TOAST_FADE_MS || elapsed + UI_TOAST_FADE_MS >= ui_toast.duration_ms;
}

void ui_toast_clear(void) { ui_toast.active = false; }

static void draw_toast(void)
{
    if (!ui_toast.active) return;
    uint32_t elapsed = (uint32_t)(ui_toast.now - ui_toast.started);
    if (elapsed >= ui_toast.duration_ms) { ui_toast.active = false; return; }
    float alpha = 1.0f;
    if (ui_motion_enabled()) {
        if (elapsed < UI_TOAST_FADE_MS)
            alpha = ui_ease_out_cubic((float)elapsed / (float)UI_TOAST_FADE_MS);
        else if (elapsed + UI_TOAST_FADE_MS > ui_toast.duration_ms)
            alpha = ui_ease_out_cubic((float)(ui_toast.duration_ms - elapsed) / (float)UI_TOAST_FADE_MS);
    }
    unsigned a_bg = (unsigned)(236.0f * alpha);
    unsigned a_fg = (unsigned)(255.0f * alpha);
    if (!a_fg) return;
    uint32_t stripe = ui_toast.kind == UI_TOAST_ERROR ? C_RED :
                      ui_toast.kind == UI_TOAST_WARN ? C_YELLOW :
                      ui_toast.kind == UI_TOAST_OK ? C_GREEN : C_ACCENT;
    float tw = text_w(FONT_BOLD, 16, ui_toast.text);
    float bw = tw + 56.0f;
    float max_w = W - 2.0f * UI_SAFE_X;
    if (bw < 220.0f) bw = 220.0f;
    if (bw > max_w) bw = max_w;
    float bh = 44.0f;
    float bx = W - UI_SAFE_X - bw;
    float by = 116.0f - 10.0f * (1.0f - alpha);
    rrect(bx, by, bw, bh, DS_RADIUS_MD, WITH_ALPHA(DS_BG_ELEVATED, a_bg));
    cv_rrect_outline(&cv, S(bx), S(by), S(bw), S(bh), S(DS_RADIUS_MD), S(1), WITH_ALPHA(C_BORDER, a_bg));
    cv_fill(&cv, SI(bx), SI(by + 6), SI(4), SI(bh - 12), WITH_ALPHA(stripe, a_fg));
    text_fit(FONT_BOLD, 16, bx + 18, by + 28, bw - 32, WITH_ALPHA(C_TEXT, a_fg), ui_toast.text);
}

static void footer(const struct hint *hints, int n, const char *right)
{
    float x = UI_SAFE_X, y = H - 27;
    int i;

    cv_fill(&cv,0,SI(H-56),cv.w,SI(56),DS_BG_FOOTER);
    cv_fill(&cv, 0, SI(H - 56), cv.w, 1, WITH_ALPHA(C_BORDER, 140));
    for (i = 0; i < n; i++) {
        float kw;
        keycap(x, y, hints[i].button, &kw);
        x += kw + 9;
        text(FONT_REGULAR, 17, x, y, C_TEXT_DIM, ui_translate(hints[i].label));
        x += text_w(FONT_REGULAR, 17, ui_translate(hints[i].label)) + 28;
    }
    if (right)
        text_right(FONT_REGULAR, 15, W - 64, y, C_TEXT_FAINT, right);
    draw_toast();
}

static void header(const struct ui_host *host)
{
    moon_logo(84, 56, 20);
    text(FONT_BOLD, 23, 116, 58, C_TEXT, ui_translate("MoonX360"));
    if (host) {
        char label[160];
        if (host->state == HOST_UNKNOWN)
            snprintf(label, sizeof(label), "%s", state_text(host->state));
        else
            snprintf(label, sizeof(label), "%s  \xC2\xB7  %s",
                     host->name && host->name[0] ? host->name : ui_translate("Your streaming PC"), state_text(host->state));
        cv_circle(&cv,S(122),S(77),S(4),state_color(host->state));
        text_fit(FONT_REGULAR,15,134,82,250,C_TEXT_DIM,label);
    }
    time_t stamp=time(NULL);
    if(stamp>0){struct tm *clock=localtime(&stamp);if(clock){char label[16];
        snprintf(label,sizeof(label),"%02d:%02d",clock->tm_hour,clock->tm_min);
        text_right(FONT_REGULAR,19,W-64,68,C_TEXT,label);}}
#ifdef _XBOX
    {int pads=input_connected_count();if(pads>0){char label[40];
        snprintf(label,sizeof(label),"%d %s",pads,ui_translate(pads==1?"controller":"controllers"));
        text_right(FONT_REGULAR,15,W-64,89,C_TEXT_DIM,label);}}
#endif
}

static void draw_wallpaper(void);
static void begin_frame(void)
{
    perf_ui_begin();
    cv_unclip(&cv);
    ui_draw_begin(&cv);font_set_sink(&cv,ui_glyph);
    if(ui_draw_recording())theme_background(&cv);
    else {
        if(!bg.px && cv_init(&bg,cv.w,cv.h))draw_background();
        if(bg.px)cv_copy(&cv,&bg);else cv_fill(&cv,0,0,cv.w,cv.h,C_BG_BOTTOM);
    }
    draw_wallpaper();
}

static void top_nav(int focus,int active)
{
    static const char *names[]={"Home","Library","Computers","Settings"};
    rrect(W/2-214,48,428,50,8,ARGB(145,12,16,20));
    cv_rrect_outline(&cv,S(W/2-214),S(48),S(428),S(50),S(8),S(1),WITH_ALPHA(C_BORDER,90));
    for(int i=0;i<4;i++) {
        float cx=W/2-169.5f+(float)i*113;
        uint32_t color=i==focus || i==active?C_TEXT:C_TEXT_DIM;
        if(i==focus)rrect(cx-24,52,48,42,6,ARGB(180,32,44,40));
        else if(i==active)rrect(cx-24,52,48,42,6,ARGB(85,255,255,255));
        if(i==0)moon_logo(cx,73,12);
        else if(i==1){for(int j=0;j<3;j++)rrect(cx-11,63.0f+(float)j*8.0f,22,3,0,color);}
        else if(i==2)icon(ICON_MONITOR,cx,73,14,color);
        else icon(ICON_GEAR,cx,73,14,color);
        if(i==focus)text_center(FONT_BOLD,14,cx,116,C_TEXT,ui_translate(names[i]));
        else if(i==active)cv_fill(&cv,SI(cx-10),SI(101),SI(20),SI(2),C_ACCENT);
    }
}
static void tabs(void)
{
    /* All native screens use Home's compact navigation and one content area. */
    top_nav(-1,active_page==UI_LIBRARY?1:active_page==UI_HOME?2:3);
    ui_button(&cv,"LB",SI(W-140),SI(97),SI(35),SI(26));
    ui_button(&cv,"RB",SI(W-101),SI(97),SI(35),SI(26));
}

static void breadcrumb(const char *section,const char *leaf)
{
    char path[800];ui_location_path(path,sizeof(path),section,leaf);
    const char *shown=path;
    /* Preserve the current page when long game/host names exceed the line. */
    while(text_w(FONT_REGULAR,18,shown)>W-128) {
        const char *next=strstr(shown,"  >  ");if(!next)break;shown=next+5;
    }
    text_fit(FONT_REGULAR,18,64,162,W-128,C_TEXT_DIM,shown);
}

/* Unified centered dialog container with top semantic accent bar and 4:3 clamp. */
static void ds_dialog_frame(float w, float h, uint32_t bar_color, float *x, float *y)
{
    float max_w = W - 2.0f * UI_SAFE_X;
    if (w > max_w) w = max_w;
    *x = (W - w) / 2.0f;
    *y = (H - h) / 2.0f + 10.0f;
    cv_fill(&cv, 0, 0, cv.w, cv.h, DS_BG_SCRIM);
    card(*x, *y, w, h, DS_RADIUS_LG, false);
    if (bar_color) cv_fill(&cv, SI(*x + 16), SI(*y), SI(w - 32), SI(3), bar_color);
}
static void dialog(float w, float h, float *x, float *y)
{
    ds_dialog_frame(w, h, C_ACCENT, x, y);
}

/* ------------------------------------------------------------ screens */

void ui_draw_splash(const char *status, uint32_t t_ms)
{
    begin_frame();
    moon_logo(W / 2, 270, 52);
    text_center(FONT_BOLD, 60, W / 2, 400, C_TEXT, ui_translate("MoonX360"));
    text_center(FONT_REGULAR, 24, W / 2, 440, C_TEXT_DIM, ui_translate("for Xbox 360"));
    if (status)
        text_center(FONT_REGULAR, 19, W / 2, 524, C_TEXT_DIM, ui_translate(status));
    spinner(W / 2, 582, 18, t_ms);
}

int ui_dashboard_move(int sel,int dx,int dy,int n)
{
    if(n<=0)return 0;
    int next=sel+(dy?dy:dx);
    return next<0?0:next>=n?n-1:next;
}
static struct {float current,target,origin;uint32_t now,started;bool active;} home_scroll;
static struct {float current,target,origin;uint32_t started;bool active;} home_focus;
static uint32_t home_wall_started;
static void home_animate(float *value,float *origin,float target,uint32_t *started,bool *active)
{
    if(*value==target && !*active)return;
    *origin=*value;*started=home_scroll.now;*active=ui_motion_enabled();
    if(!*active)*value=target;
}
bool ui_home_tick(uint32_t now)
{
    home_scroll.now=now;bool changed=false;
    if(home_scroll.active){
        float t=ui_anim_progress(now,home_scroll.started,UI_SCROLL_MS);
        if(!ui_motion_enabled())t=1.0f;
        home_scroll.current=ui_anim_lerp(home_scroll.origin,home_scroll.target,ui_ease_out_cubic(t));
        if(t>=1.0f){home_scroll.current=home_scroll.target;home_scroll.active=false;}
        changed=true;
    }
    if(home_focus.active){
        float t=ui_anim_progress(now,home_focus.started,UI_ANIM_SCROLL_MS);
        if(!ui_motion_enabled())t=1.0f;
        home_focus.current=ui_anim_lerp(home_focus.origin,home_focus.target,ui_ease_out_cubic(t));
        if(t>=1.0f){home_focus.current=home_focus.target;home_focus.active=false;}
        changed=true;
    }
    if(theme_background_id==4 && home_wall_previous>=0){
        if((uint32_t)(now-home_wall_started)>=UI_WALL_FADE_MS || !ui_motion_enabled())home_wall_previous=-1;
        changed=true;
    }
    return changed;
}
/* Four destinations, a contextual action, portrait recents and four shortcuts. */
static void dashboard_rect(int item,int recents,float *x,float *y,float *w,float *h)
{
    if(item<4){*x=W/2-169.5f+(float)item*113-20;*y=53;*w=40;*h=40;return;}
    if(item==4){*x=64;*y=152;*w=500;*h=124;return;}
    if(item<5+recents){
        int index=item-5;float pos=64-home_scroll.current;
        for(int i=0;i<index;i++) {float d=fabsf(home_focus.current-(float)i);if(d>1)d=1;pos+=140-28*d+18;}
        float d=fabsf(home_focus.current-(float)index);if(d>1)d=1;
        *w=140-28*d;*h=210-42*d;*x=pos;*y=H-190-*h;return;
    }
    int shortcut=item-5-recents;*w=(W-128-54)/4;*h=104;
    *x=64+(float)shortcut*(*w+18);*y=H-172;
}
int ui_dashboard_move_tiles(int selected,int dx,int dy,int count,int recents)
{
    if(count<=0)return 0;if(selected<0 || selected>=count)selected=0;
    int group=selected<4?0:selected==4?1:selected<5+recents?2:3;
    int start=group==0?0:group==1?4:group==2?5:5+recents;
    int length=group==0?4:group==1?1:group==2?recents:4;
    if(dx){int next=selected+dx;return next>=start && next<start+length && next<count?next:selected;}
    if(dy>0){if(group==0)return count>4?4:selected;
        if(group==1)return count>5?5:selected;
        if(group==2){int col=selected-start;if(col>3)col=3;int dst=5+recents+col;return dst<count?dst:count-1;}}
    if(dy<0){if(group==1)return 0;
        if(group==2)return 4;
        if(group==3)return recents?5+(selected-start<recents?selected-start:recents-1):4;}
    return selected;
}

static void placeholder_art(const char *name,float x,float y,float w,float h);
static void dashboard_art(const struct ui_app *app,float x,float y,float w,float h)
{
    if(app && app->art) {
        int iw=SI(TILE_W),ih=SI(TILE_H),sx=0,sy=0,sw=iw,sh=ih;
        /* Fill the portrait rectangle without stretching the source cover. */
        if((float)iw/(float)ih>w/h){sw=(int)((float)ih*w/h);sx=(iw-sw)/2;}
        else {sh=(int)((float)iw*h/w);sy=(ih-sh)/2;}
        ui_picture_region(&cv,app->art,iw,ih,sx,sy,sw,sh,S(x),S(y),S(w),S(h),255,app->generation);
        ui_cover_fade_mark();
    } else placeholder_art(app && app->name?app->name:ui_translate("Your games"),x,y,w,h);
}
/* Two cover transition slots and one user image. 1280x720 costs 3.52 MiB per
 * slot on CPU and GPU; preparation happens only after focus settles. */
#define HOME_WALL_W 1280
#define HOME_WALL_H 720
static uint32_t home_wall[3][HOME_WALL_W*HOME_WALL_H];
static uint32_t home_wall_generation[3];
static uint32_t home_wall_small[160*90];
static int home_wall_current=-1;
static const uint32_t *home_wall_source;
static uint32_t home_wall_source_generation;
static char home_wall_host[96];
static struct {const uint32_t *source;uint32_t generation;char host[96];int row,slot,w,h;bool active;} home_wall_pending;
void ui_home_cancel_prepare(void){home_wall_pending.active=false;}
static uint32_t wall_sample(const uint32_t *source,int sw,int sh,int x,int y,int dw,int dh,bool cover)
{
    int crop_w=sw,crop_h=sh,ox=0,oy=0;
    if(cover) {
        if((int64_t)sw*dh>(int64_t)sh*dw){crop_w=(int)((int64_t)sh*dw/dh);ox=(sw-crop_w)/2;}
        else {crop_h=(int)((int64_t)sw*dh/dw);oy=(sh-crop_h)/2;}
        if(crop_w<1)crop_w=1;if(crop_h<1)crop_h=1;
    }
    int sx=ox+x*crop_w/dw,sy=oy+y*crop_h/dh;
    int fx=(x*crop_w*256/dw)&255,fy=(y*crop_h*256/dh)&255;
    if(sx>=sw)sx=sw-1;if(sy>=sh)sy=sh-1;
    int sx1=sx+1<sw?sx+1:sx,sy1=sy+1<sh?sy+1:sy;
    uint32_t p[4]={source[sy*sw+sx],source[sy*sw+sx1],source[sy1*sw+sx],source[sy1*sw+sx1]};
    uint32_t color=0xFF000000u;
    for(int shift=0;shift<24;shift+=8){
        int a=(int)((p[0]>>shift)&255),b=(int)((p[1]>>shift)&255);
        int c=(int)((p[2]>>shift)&255),d=(int)((p[3]>>shift)&255);
        int v=((a*(256-fx)+b*fx)*(256-fy)+(c*(256-fx)+d*fx)*fy)/65536;
        color|=(uint32_t)v<<shift;
    }
    return color;
}
static void home_wall_resample_rows(uint32_t *out,const uint32_t *source,int sw,int sh,bool cover,int first,int end)
{
    for(int y=first;y<end;y++)for(int x=0;x<HOME_WALL_W;x++) {
        uint32_t color=cover?wall_sample(home_wall_small,160,90,x,y,HOME_WALL_W,HOME_WALL_H,false):
            wall_sample(source,sw,sh,x,y,HOME_WALL_W,HOME_WALL_H,true);
        int shade=cover?(y<HOME_WALL_H/2?78:61):90;
        for(int shift=0;shift<24;shift+=8){int v=(int)((color>>shift)&255);
            color=(color&~(255u<<shift))|((uint32_t)(v*shade/100)<<shift);}
        out[y*HOME_WALL_W+x]=color;
    }
}
bool ui_home_prepare_cover(const char *host,const struct ui_app *app,uint32_t now)
{
    if(!app || !app->art)return false;
    if(!home_wall_pending.active && home_wall_current>=0 && app->art==home_wall_source && app->generation==home_wall_source_generation &&
       !strcmp(home_wall_host,host?host:""))return false;
    if(!home_wall_pending.active || home_wall_pending.source!=app->art ||
       home_wall_pending.generation!=app->generation || strcmp(home_wall_pending.host,host?host:"")) {
        home_wall_pending.source=app->art;home_wall_pending.generation=app->generation;
        snprintf(home_wall_pending.host,sizeof(home_wall_pending.host),"%s",host?host:"");
        home_wall_pending.slot=home_wall_current==0?1:0;home_wall_pending.row=0;
        ui_art_size(&home_wall_pending.w,&home_wall_pending.h);
        for(int y=0;y<90;y++)for(int x=0;x<160;x++)
            home_wall_small[y*160+x]=wall_sample(app->art,home_wall_pending.w,home_wall_pending.h,x,y,160,90,true);
        home_wall_pending.active=true;
    }
    int next=home_wall_pending.row+24;if(next>HOME_WALL_H)next=HOME_WALL_H;
    home_wall_resample_rows(home_wall[home_wall_pending.slot],home_wall_small,160,90,true,home_wall_pending.row,next);
    home_wall_pending.row=next;
    if(next<HOME_WALL_H)return false;
    home_wall_previous=ui_motion_enabled()?home_wall_current:-1;home_wall_current=home_wall_pending.slot;home_wall_started=now;
    home_wall_source=app->art;home_wall_source_generation=app->generation;
    snprintf(home_wall_host,sizeof(home_wall_host),"%s",host?host:"");
    home_wall_pending.active=false;
    home_wall_generation[home_wall_current]++;ui_drop_main_cache();return true;
}
void ui_home_set_user_wallpaper(const uint32_t *pixels,int width,int height)
{
    if(!pixels || width<=0 || height<=0 || width>2048 || height>2048)return;
    home_wall_resample_rows(home_wall[2],pixels,width,height,false,0,HOME_WALL_H);
    home_wall_generation[2]++;ui_drop_main_cache();
}
static void home_wall_draw(int index,unsigned alpha)
{
    int sx=0,sy=0,sw=HOME_WALL_W,sh=HOME_WALL_H;
    float aspect=W/H;
    if(aspect<(float)HOME_WALL_W/HOME_WALL_H){sw=(int)(HOME_WALL_H*aspect);sx=(HOME_WALL_W-sw)/2;}
    else {sh=(int)(HOME_WALL_W/aspect);sy=(HOME_WALL_H-sh)/2;}
    ui_picture_region(&cv,home_wall[index],HOME_WALL_W,HOME_WALL_H,sx,sy,sw,sh,
        0,0,S(W),S(H),alpha,home_wall_generation[index]);
}
static void draw_wallpaper(void)
{
    if(theme_background_id==3 && home_wall_generation[2])home_wall_draw(2,255);
    else if(theme_background_id==4 && home_wall_current>=0) {
        if(home_wall_previous>=0)home_wall_draw(home_wall_previous,255);
        unsigned alpha=255;
        if(home_wall_previous>=0){
            float t=ui_anim_progress(home_scroll.now,home_wall_started,UI_WALL_FADE_MS);
            alpha=(unsigned)(255.0f*ui_ease_out_cubic(t));
        }
        home_wall_draw(home_wall_current,alpha);
    }
    if(theme_background_id==3 || theme_background_id==4) {
        cv_vgradient(&cv,0,0,cv.w,SI(148),ARGB(155,6,10,13),ARGB(28,6,10,13));
        cv_vgradient(&cv,0,SI(H-260),cv.w,SI(260),ARGB(20,6,10,13),ARGB(165,6,10,13));
    }
}
static void dashboard_shortcut_icon(int item,int which,float x,float y)
{
    uint32_t color=HEX(0xC8D6CF);
    if(!item) {
        for(int i=0;i<4;i++)rrect(x+(float)(i%2)*19,y+(float)(i/2)*19,14,14,0,color);
    } else if(item==1) {
        cv_rrect_outline(&cv,S(x),S(y),S(38),S(27),0,S(2),color);
        rrect(x+17,y+27,4,8,0,color);rrect(x+8,y+35,22,2,0,color);
    } else icon(which,x+18,y+18,20,which==ICON_STOP?C_RED:color);
}

/* Hash the content, not temporary pointers supplied by app.c. */
static uint64_t main_hash_text(uint64_t key, const char *text)
{
    if (text) while (*text) { key ^= (unsigned char)*text++; key *= 1099511628211ULL; }
    return (key ^ 255) * 1099511628211ULL;
}

static uint64_t main_key(const struct ui_host *host, const struct ui_menu_item *items,
                         int n, const char *ip)
{
    uint64_t key = 14695981039346656037ULL;
    int i;
    key = main_hash_text(key, ip);
    key ^= (unsigned)exit_shortcut;
    if (host) {
        key = main_hash_text(key, host->name);
        key = main_hash_text(key, host->address);
        key = main_hash_text(key, host->running_app);
        key = main_hash_text(key, host->detail);
        key ^= (unsigned)host->state;
    }
    key ^= (unsigned)n;
    for (i = 0; i < n; i++) {
        key = main_hash_text(key, items[i].title);
        key = main_hash_text(key, items[i].subtitle);
        key ^= (unsigned)items[i].icon * 17 + (unsigned)items[i].enabled;
    }
    return key;
}

void ui_draw_dashboard(const struct ui_host *host,const struct ui_menu_item *items,int n,int sel,
                       const char *console_ip,const struct ui_dashboard *dashboard)
{
    perf_ui_begin();
    ui_set_page(UI_HOME);
    static const struct hint menu_hints[]={{"A","Open"},{"Y","Search"},{"B","Back"}};
    static const struct hint start_hints[]={{"A","Start playing"},{"Y","Search"},{"B","Back"}};
    int recent=dashboard?dashboard->recent_count:0;if(recent<0)recent=0;if(recent>8)recent=8;
    int visible=5+recent+4;if(visible>n)visible=n;
    if(sel<0 || sel>=visible)sel=0;
    float focus_target=sel>=5 && sel<5+recent?(float)(sel-5):-1.0f;
    if(home_focus.target!=focus_target) {
        home_focus.target=focus_target;home_animate(&home_focus.current,&home_focus.origin,
            home_focus.target,&home_focus.started,&home_focus.active);
    }
    /* Keep the focused recent tile cleanly inside the safe horizontal viewport on 16:9 and 4:3. */
    {
        float scroll_target=0.0f;
        if(sel>=5 && sel<5+recent) {
            int idx=sel-5;
            float left=0.0f;
            for(int i=0;i<idx;i++)left+=112.0f+18.0f;
            float right=left+140.0f;
            float view_w=W-2.0f*UI_SAFE_X;
            if(right-home_scroll.target>view_w)scroll_target=right-view_w;
            else if(left<home_scroll.target)scroll_target=left;
            else scroll_target=home_scroll.target;
            if(scroll_target<0.0f)scroll_target=0.0f;
        }
        if(home_scroll.target!=scroll_target) {
            home_scroll.target=scroll_target;home_animate(&home_scroll.current,&home_scroll.origin,
                home_scroll.target,&home_scroll.started,&home_scroll.active);
        }
    }
    uint64_t key=main_key(host,items,visible,console_ip);
    key=main_hash_text(main_hash_text(key,dashboard?dashboard->profile:NULL),dashboard?dashboard->connection:NULL);
    if(dashboard && dashboard->featured){const struct ui_app *a=dashboard->featured;
        key=main_hash_text(key,a->name);key^=(uint64_t)(uintptr_t)a->art;
        key=key*1099511628211ULL+a->generation+(unsigned)a->running;
        key=key*1099511628211ULL+(unsigned)dashboard->featured_id;}
    key=key*1099511628211ULL+(unsigned)sel;
    key=key*1099511628211ULL+(unsigned)(home_focus.current*256);
    key=key*1099511628211ULL+(unsigned)(home_scroll.current*16);
    key=key*1099511628211ULL+(unsigned)theme_background_id;
#ifdef _XBOX
    key=key*1099511628211ULL+(unsigned)input_connected_count();
#endif
    key=key*1099511628211ULL+(unsigned)(time(NULL)/60);
    key=key*1099511628211ULL+home_wall_generation[theme_background_id==3?2:home_wall_current>=0?home_wall_current:0];
    if(theme_background_id==4 && home_wall_previous>=0)key=key*1099511628211ULL+(home_scroll.now-home_wall_started)/16;
    for(int i=0;dashboard && dashboard->recent && i<recent;i++) {
        const struct ui_app *a=dashboard->recent+i;
        key=main_hash_text(key,a->name);key^=(uint64_t)(uintptr_t)a->art;
        key=key*1099511628211ULL+a->generation;
    }
    if(!ui_draw_enabled() && !main_cache.px)cv_init(&main_cache,cv.w,cv.h);
    bool reused=false;
    if(main_cache_valid && key==main_cache_key) {
        if(ui_draw_enabled() && main_cache_gpu){cv_unclip(&cv);reused=ui_scene_restore(&cv);font_set_sink(&cv,ui_glyph);}
        else if(!ui_draw_enabled() && !main_cache_gpu && main_cache.px){cv_copy(&cv,&main_cache);reused=true;}
    }
    if(!reused) {
        begin_frame();
        /* A precomputed wallpaper stays beneath all text and native tiles. */
        header(host);
        /* The right side remains open for the wallpaper. The small left
         * context shows only real host, profile and application state. */
        top_nav(sel<4?sel:-1,0);
        {
            float x,y,w,h;dashboard_rect(4,recent,&x,&y,&w,&h);
            const struct ui_app *featured=dashboard?dashboard->featured:NULL;
            bool running=featured && featured->running;
            rrect(x,y,w,h,UI_CORNER,ARGB(228,17,22,26));
            cv_rrect_outline(&cv,S(x),S(y),S(w),S(h),S(UI_CORNER),S(1),WITH_ALPHA(C_BORDER,175));
            if(sel==4)cv_fill(&cv,SI(x),SI(y+8),SI(3),SI(h-16),C_ACCENT);
            if(featured)dashboard_art(featured,x+10,y+8,72,108);
            else {rrect(x+10,y+8,72,108,4,HEX(0x1D282E));moon_logo(x+46,y+62,19);}
            char context[160];snprintf(context,sizeof(context),"%s  \xC2\xB7  %s",
                ui_translate(running?"Now playing":featured?"Last played":"Your games"),
                host && host->name && host->name[0]?host->name:ui_translate("Your streaming PC"));
            text_fit(FONT_BOLD,14,x+103,y+23,w-118,running?C_ACCENT:C_TEXT_DIM,context);
            text_fit(FONT_BOLD,23,x+103,y+49,w-118,C_TEXT,
                featured && featured->name?featured->name:ui_translate("My Library"));
            text_fit(FONT_REGULAR,15,x+103,y+71,w-118,C_TEXT_DIM,
                dashboard && dashboard->profile?dashboard->profile:"");
            text_fit(FONT_REGULAR,14,x+103,y+88,w-118,C_TEXT_FAINT,
                dashboard && dashboard->connection?dashboard->connection:"");
            if(sel==4){
                rrect(x+103,y+94,198,26,DS_RADIUS_SM,C_ACCENT);
                ui_button(&cv,"A",SI(x+109),SI(y+96),SI(22),SI(22));
                text_fit(FONT_BOLD,15,x+137,y+113,156,C_BG_BOTTOM,
                    ui_translate(running?"Continue stream":"Start playing"));
                if(featured){rrect(x+309,y+94,122,26,DS_RADIUS_SM,HEX(0x2A353B));
                    ui_button(&cv,"X",SI(x+315),SI(y+96),SI(22),SI(22));
                    text_fit(FONT_REGULAR,15,x+343,y+113,82,C_TEXT,ui_translate("Details"));}
            }
        }
        text(FONT_BOLD,18,64,H-412,C_TEXT,ui_translate("Recent Games"));
        if(recent) {
            cv_clip(&cv,SI(UI_SAFE_X-8),SI(H-406),cv.w-SI(UI_SAFE_X)-SI(UI_SAFE_X-8),SI(224));
            for(int i=0;i<recent;i++) {
                float x,y,w,h;dashboard_rect(5+i,recent,&x,&y,&w,&h);
                if(x+w<UI_SAFE_X-16 || x>W-UI_SAFE_X+16)continue;
                const struct ui_app *app=dashboard->recent+i;
                rrect(x,y,w,h,4,HEX(0x161D22));dashboard_art(app,x,y,w,h);
                if(app->running) {
                    rrect(x+8,y+8,10,10,5,C_ACCENT);
                }
                if(sel==5+i) {
                    cv_vgradient(&cv,SI(x),SI(y+h-54),SI(w),SI(54),ARGB(0,8,12,15),ARGB(225,8,12,15));
                    text_fit(FONT_BOLD,17,x+10,y+h-12,w-20,C_TEXT,app->name?app->name:"");
                }
            }
            cv_unclip(&cv);
        } else {
            text(FONT_REGULAR,18,64,H-200,C_TEXT_DIM,ui_translate("Open Library. Your recent games will appear here."));
        }
        for(int i=5+recent;i<visible;i++) {
            float x,y,w,h;dashboard_rect(i,recent,&x,&y,&w,&h);int shortcut=i-5-recent;
            bool on=sel==i;
            rrect(x,y,w,h,UI_CORNER,on?C_CARD_SEL:ARGB(226,24,30,35));
            cv_rrect_outline(&cv,S(x),S(y),S(w),S(h),S(UI_CORNER),S(1),WITH_ALPHA(C_BORDER,on?210:145));
            if(on)cv_fill(&cv,SI(x+12),SI(y),SI(w-24),SI(3),C_ACCENT);
            dashboard_shortcut_icon(shortcut,items[i].icon,x+20,y+19);
            text_fit(FONT_BOLD,19,x+20,y+h-17,w-40,C_TEXT,ui_translate(items[i].title));
            if(items[i].subtitle)text_fit(FONT_REGULAR,15,x+62,y+37,w-82,C_TEXT_DIM,items[i].subtitle);
        }
        struct hint game_hints[]={{"A",sel==4 && dashboard && dashboard->featured && dashboard->featured->running?"Continue":"Play"},
            {"X","Details"},{"Y","Search"},{"B","Back"}};
        bool game_action=sel==4 && dashboard && dashboard->featured || sel>=5 && sel<5+recent;
        footer(game_action?game_hints:sel==4?start_hints:menu_hints,game_action?4:3,NULL);
        main_cache_key=key;main_cache_gpu=ui_draw_enabled();
        if(ui_draw_enabled())main_cache_valid=ui_scene_store();
        else if(main_cache.px){cv_copy(&main_cache,&cv);main_cache_valid=true;}
        else main_cache_valid=false;
    }
    if(visible>0) {
        float x,y,w,h;dashboard_rect(sel,recent,&x,&y,&w,&h);
        cv_rrect_outline(&cv,S(x-1),S(y-1),S(w+2),S(h+2),S(UI_CORNER),S(1),WITH_ALPHA(C_TEXT,120));
        cv_rrect_outline(&cv,S(x-4),S(y-4),S(w+8),S(h+8),S(UI_CORNER+1),S(UI_FOCUS_BORDER),C_ACCENT);
        ui_focus_mark(UI_HOME,sel);
        if(sel==4 && dashboard && dashboard->featured)
            launch_source_tile(dashboard->featured,x+8,y+9,78,106);
        if(sel>=5 && sel<5+recent && dashboard && dashboard->recent)
            launch_source_tile(dashboard->recent+sel-5,x,y,w,h);
    }
}
void ui_draw_main(const struct ui_host *host,const struct ui_menu_item *items,int n,int sel,
                  const char *console_ip,uint32_t t_ms)
{
    (void)t_ms;ui_draw_dashboard(host,items,n,sel,console_ip,NULL);
}

void ui_draw_hosts(const struct ui_setting *rows,int n,int sel,int page,bool searching)
{
    ui_set_page(page);begin_frame();header(NULL);top_nav(-1,2);
    ui_button(&cv,"LB",SI(W-140),SI(97),SI(35),SI(26));
    ui_button(&cv,"RB",SI(W-101),SI(97),SI(35),SI(26));
    breadcrumb(NULL,ui_translate("Hosts"));
    int first=sel>=6?sel-5:0;
    float card_w=W-2*UI_SAFE_X;
    float value_w=card_w*0.34f;if(value_w>360)value_w=360;
    float value_x=UI_SAFE_X+card_w-value_w-24;
    float label_w=value_x-UI_SAFE_X-38;
    for(int i=first;i<n && i<first+6;i++) {
        float y=196+(float)(i-first)*62;
        card(UI_SAFE_X,y,card_w,54,UI_CORNER,i==sel);
        text_fit(FONT_BOLD,21,UI_SAFE_X+22,y+24,label_w,C_TEXT,i>=n-2?ui_translate(rows[i].label):rows[i].label);
        text_fit(FONT_REGULAR,16,UI_SAFE_X+22,y+45,label_w,C_TEXT_DIM,i>=n-2?ui_translate(rows[i].help):rows[i].help);
        text_fit(FONT_REGULAR,18,value_x,y+33,value_w,C_TEXT_DIM,ui_translate(rows[i].value));
    }
    text(FONT_REGULAR,16,UI_SAFE_X,615,C_TEXT_FAINT,ui_translate(searching?"Searching network...":"Saved hosts available. Y searches again."));
    static const struct hint hints[]={{"A","Select"},{"X","Details"},{"Y","Search"},{"B","Back"}};
    footer(hints,4,NULL);
}

static uint32_t name_color(const char *name)
{
    uint32_t h = 2166136261u;
    static const uint32_t palette[] = { 0x263A32, 0x243342, 0x333F2E, 0x34313C, 0x223E3A, 0x353942 };
    while (*name)
        h = (h ^ (unsigned char)*name++) * 16777619u;
    return HEX(palette[h % 6]);
}

/* Placeholder art: refined dark console card with subtle name-derived tint. */
static void placeholder_art(const char *name,float x,float y,float w,float h)
{
    uint32_t base = name_color(name);
    uint32_t top = cv_mix(base, HEX(0xFFFFFF), 24), bottom = cv_mix(base, HEX(0x0D1114), 140);
    cv_vgradient(&cv,SI(x),SI(y),SI(w),SI(h),top,bottom);
    cv_fill(&cv,SI(x),SI(y),SI(w),SI(3),WITH_ALPHA(C_ACCENT,150));
    icon(ICON_PLAY,x+w/2,y+h*.34f,18,WITH_ALPHA(C_TEXT,145));
    /* One shared fallback, including small Continue cards. Title uses the same
     * truncation as real artwork captions; no status claim while work is pending. */
    text_fit(FONT_BOLD,UI_TEXT_SECONDARY,x+14,y+h*.64f,w-28,C_TEXT,name);
    cv_rrect_outline(&cv,S(x),S(y),S(w),S(h),0,S(1),WITH_ALPHA(C_BORDER,140));
}

static int rail_visible(void)
{
    int fit=(int)((W-2.0f*UI_SAFE_X+UI_CARD_GAP)/UI_RAIL_STEP);
    if(fit<1)fit=1;
    if(fit>UI_RAIL_VISIBLE)fit=UI_RAIL_VISIBLE;
    return fit;
}

static void app_tile(const struct ui_app *a,float x,float y,bool on,int item)
{
    float w=UI_RAIL_CARD_WIDTH,h=UI_RAIL_CARD_HEIGHT;
    float ty=on?y-4.0f:y;
    if(on)launch_source_tile(a,x,ty,w,h);
    if(a->art) {
        ui_picture_region(&cv,a->art,SI(TILE_W),SI(TILE_H),0,0,SI(TILE_W),SI(TILE_H),S(x),S(ty),S(w),S(h),255,a->generation);
        ui_cover_fade_mark();
    } else placeholder_art(a->name,x,ty,w,h);
    if(on) {
        cv_rrect_outline(&cv,S(x-1),S(ty-1),S(w+2),S(h+2),S(UI_CORNER-1),S(1),WITH_ALPHA(C_TEXT,135));
        cv_rrect_outline(&cv,S(x-5),S(ty-5),S(w+10),S(h+10),S(UI_CORNER),S(UI_FOCUS_BORDER),C_ACCENT);
        ui_focus_mark(UI_LIBRARY,item);
    }
    if(a->favorite){rrect(x+w-52,ty+10,42,24,4,C_YELLOW);text_center(FONT_BOLD,12,x+w-31,ty+27,C_BG_BOTTOM,ui_translate("FAV"));}
    if(a->running){rrect(x+10,ty+10,82,24,4,C_GREEN);text_center(FONT_BOLD,12,x+51,ty+27,C_BG_BOTTOM,ui_translate("RUNNING"));}
}
static struct {float current,target,origin;uint32_t now,started;uint64_t context;bool active,known;} library_scroll;
void ui_library_art_window(int count,int selected,int *first,int *end)
{
    int vis=rail_visible();
    int target=selected>=vis?selected-vis+1:0;
    *first=library_scroll.known?(int)(library_scroll.current/UI_RAIL_STEP)-1:target-1;
    if(*first<0)*first=0;if(*first>=count)*first=count>0?count-1:0;
    *end=*first+vis+3;if(*end>count)*end=count;
}
static void reset_scroll(void){memset(&home_scroll,0,sizeof(home_scroll));memset(&home_focus,0,sizeof(home_focus));home_focus.current=home_focus.target=-1;memset(&library_scroll,0,sizeof(library_scroll));}
bool ui_library_tick(uint32_t now)
{
    library_scroll.now=now;if(!library_scroll.active)return false;
    float t=ui_anim_progress(now,library_scroll.started,UI_SCROLL_MS);
    if(!ui_motion_enabled())t=1.0f;
    library_scroll.current=ui_anim_lerp(library_scroll.origin,library_scroll.target,ui_ease_out_cubic(t));
    if(t>=1.0f){library_scroll.current=library_scroll.target;library_scroll.active=false;}
    return true;
}
void ui_draw_library(const struct ui_host *host,const struct ui_app *apps,int n,int sel,const char *filter)
{
    static const struct hint hints[]={{"A","Play"},{"B","Back"},{"X","Details"},{"Start","Menu"},{"Y","Refresh"}};
    ui_set_page(UI_LIBRARY);begin_frame();header(host);tabs();
    char count[48];
    if(n>0 && sel>=0 && sel<n)snprintf(count,sizeof(count),"%d / %d",sel+1,n);
    else snprintf(count,sizeof(count),ui_translate(n==1?"1 app":"%d apps"),n);
    text(FONT_BOLD,UI_TEXT_TITLE,UI_SAFE_X,168,C_TEXT,ui_translate(filter?filter:"All Apps"));
    text_right(FONT_REGULAR,UI_TEXT_META,W-UI_SAFE_X,168,C_TEXT_DIM,count);
    if(n<=0){text(FONT_REGULAR,UI_TEXT_PRIMARY,UI_SAFE_X,330,C_TEXT_DIM,ui_translate("No apps here. Use Start to change view."));footer(hints+1,4,NULL);return;}
    if(sel<0 || sel>=n)sel=0;
    int vis=rail_visible();
    int first=sel>=vis?sel-vis+1:0;
    uint64_t context=main_hash_text(main_hash_text(14695981039346656037ULL,filter),host?host->name:NULL)^(unsigned)n^(unsigned)(vis<<16);
    float target=(float)first*(float)UI_RAIL_STEP;
    if(!library_scroll.known || library_scroll.context!=context) {
        library_scroll.current=library_scroll.target=target;library_scroll.active=false;library_scroll.known=true;library_scroll.context=context;
    } else if(target!=library_scroll.target) {
        library_scroll.origin=library_scroll.current;library_scroll.target=target;library_scroll.started=library_scroll.now;
        library_scroll.active=ui_motion_enabled();if(!library_scroll.active)library_scroll.current=target;
    }
    /* Draw just the clipped visible rail and one overscan card either side.
     * All cards share one position; no per-card animation or allocation. */
    float top=206;cv_clip(&cv,SI(UI_SAFE_X-8),SI(top-14),cv.w-SI(UI_SAFE_X)-SI(UI_SAFE_X-8),SI(UI_RAIL_CARD_HEIGHT+74));
    int begin,end;ui_library_art_window(n,sel,&begin,&end);
    for(int i=begin;i<end;i++) {
        float x=UI_SAFE_X+(float)i*UI_RAIL_STEP-library_scroll.current;
        app_tile(apps+i,x,top,i==sel,i);
        text_fit(i==sel?FONT_BOLD:FONT_REGULAR,i==sel?(float)UI_TEXT_PRIMARY:19.0f,x,top+UI_RAIL_CARD_HEIGHT+31,UI_RAIL_CARD_WIDTH,i==sel?C_TEXT:C_TEXT_DIM,apps[i].name);
    }
    cv_unclip(&cv);
    /* The focused cover already carries its title. Keep the lower wallpaper
     * open instead of repeating that title in a separate old-style bar. */
    footer(hints,5,NULL);
}

void ui_draw_apps(const struct ui_host *host,const struct ui_app *apps,int n,int sel,uint32_t t_ms)
{
    (void)t_ms;ui_draw_library(host,apps,n,sel,"All Apps");
}

void ui_draw_settings(const struct ui_setting *rows, int n, int sel)
{
    ui_draw_settings_page("Settings", rows, n, sel);
}

static bool toggle_enabled(const char *value)
{
    return value && (!strcmp(value,"On") || !strncmp(value,"On (",4) || !strcmp(value,"Limited"));
}
static int option_kind(const char *title,const struct ui_setting *row,int index,bool adjustable)
{
    if(!adjustable || !strcmp(title,"Settings") || !strcmp(title,"Developer / Advanced"))return UI_OPTION_OPEN;
    if(!strcmp(title,"Host details"))return index==0?UI_OPTION_ACTION:index==3?UI_OPTION_OPEN:UI_OPTION_INFO;
    if(!strcmp(title,"Host actions") || !strcmp(title,"Delete profile?") || !strcmp(title,"Rename profile"))return UI_OPTION_ACTION;
    if(!strcmp(title,"Library menu"))return index==0?UI_OPTION_CYCLE:UI_OPTION_OPEN;
    if(!strcmp(title,"Stream Profile"))return UI_OPTION_ACTION;
    if(!strcmp(row->label,"Saved profile") || !strcmp(row->label,"Factory profile"))return UI_OPTION_ACTION;
    if(!strcmp(row->label,"Name"))return UI_OPTION_INFO;
    if(toggle_enabled(row->value) || !strcmp(row->value,"Off"))return UI_OPTION_TOGGLE;
    if(!strcmp(row->value,"Open") || !strcmp(row->value,"Manage") || !strcmp(row->value,"Press A"))return UI_OPTION_OPEN;
    return UI_OPTION_CYCLE;
}
static int settings_row_icon(const char *title,const char *label)
{
    /* Standardized rule: category hub pages (Settings / Stream / Advanced) show
     * a leading icon on EVERY row so the label column stays 100% aligned;
     * leaf option submenus show NO leading icons on any row. */
    if(!label || (strcmp(title,"Settings") && strcmp(title,"Stream") && strcmp(title,"Advanced")))return -1;
    if(strstr(label,"Network") || strstr(label,"PC") || strstr(label,"Host") || strstr(label,"Connection"))return ICON_LINK;
    if(strstr(label,"Video") || strstr(label,"Display") || strstr(label,"Interface") || strstr(label,"Appearance") || strstr(label,"CRT"))return ICON_MONITOR;
    if(strstr(label,"Audio") || strstr(label,"Controller") || strstr(label,"Input") || strstr(label,"Stream Profile") || strstr(label,"Desktop") || strstr(label,"Launch"))return ICON_PLAY;
    if(strstr(label,"Aurora") || strstr(label,"Cache") || strstr(label,"Refresh") || strstr(label,"Restart") || strstr(label,"Reset") || strstr(label,"Save"))return ICON_REFRESH;
    if(strstr(label,"About") || strstr(label,"History") || strstr(label,"Performance") || strstr(label,"Diagnostic") || strstr(label,"Info") || strstr(label,"Log"))return ICON_INFO;
    return ICON_GEAR;
}
static void settings_page(const char *title, const struct ui_setting *rows, int n, int sel, bool adjustable,const int *kinds)
{
    float x = UI_SAFE_X, y = UI_CONTENT_TOP, w = W - 2*UI_SAFE_X, rh = UI_ROW_HEIGHT;
    int i, visible = n < 6 ? n : 6;
    int first = sel - visible / 2;
    if (first < 0) first = 0;
    if (first > n - visible) first = n - visible;

    ui_set_page(settings_context_page>=0?settings_context_page:!strcmp(title,"Stream")?UI_STREAM:!strcmp(title,"Host details")?UI_HOME:!strcmp(title,"Library menu")?UI_LIBRARY:UI_SETTINGS);begin_frame();
    header(NULL);
    tabs();
    breadcrumb(NULL,ui_translate(title));
    float panel_h=rh*visible+16+(n>visible?UI_SETTINGS_PEEK:0);
    card(x,y,w,panel_h,UI_CORNER,false);
    cv_clip(&cv,SI(x+8),SI(y+8),SI(w-16),SI(panel_h-16));
    int last=first+visible+(n>visible?1:0);if(last>n)last=n;
    for (i = first; i < last; i++) {
        float ry = y + 8 + (i - first) * rh;
        bool on = i == sel;

        if (on) {
            rrect(x + 8, ry, w - 16, rh, UI_CORNER, C_CARD_SEL);
            cv_rrect_outline(&cv,S(x+8),S(ry),S(w-16),S(rh),S(UI_CORNER),S(UI_FOCUS_BORDER),C_ACCENT);
        } else if (i > 0) {
            cv_fill(&cv, SI(x + 24), SI(ry), SI(w - 48), 1, WITH_ALPHA(C_BORDER, 200));
        }
        int kind=kinds?kinds[i]:option_kind(title,rows+i,i,adjustable);
        bool literal_value=!strcmp(rows[i].label,"Saved profile") || !strcmp(rows[i].label,"Name") || !strcmp(rows[i].label,"Use profile") || !strcmp(rows[i].label,"Stream Profile");
        const char *display_value=literal_value?rows[i].value:ui_translate(rows[i].value);
        float value_width=w*0.38f;if(value_width>410.0f)value_width=410.0f;
        float value_x=x+w-value_width-30;
        int cat_icon=settings_row_icon(title,rows[i].label);
        float label_x=x+30;
        if(cat_icon>=0){
            icon(cat_icon,x+40,ry+rh*0.5f,11,on?C_ACCENT:C_TEXT_DIM);
            label_x=x+64;
        }
        text_fit(on?FONT_BOLD:FONT_REGULAR,UI_TEXT_PRIMARY,label_x,ry+35,value_x-label_x-20,on?C_TEXT:C_TEXT_DIM,(!strcmp(title,"Stream Profile") || !strcmp(title,"Game stream profile"))?rows[i].label:ui_translate(rows[i].label));
        if (kind==UI_OPTION_CYCLE) {
            float vw = text_w(FONT_BOLD, UI_TEXT_PRIMARY, display_value);
            if(vw>value_width-40)vw=value_width-40;
            float vx = x + w - 52 - vw;
            if(on)rrect(vx-30,ry+10,vw+58,rh-20,5,ARGB(90,10,14,18));
            text_fit(FONT_BOLD, UI_TEXT_PRIMARY, vx, ry + 35, value_width-40, on?C_TEXT:C_TEXT_DIM, display_value);
            /* < value > chevrons */
            cv_line(&cv, S(vx - 16), S(ry + rh / 2 - 6), S(vx - 22), S(ry + rh / 2), S(2.2f), C_ACCENT);
            cv_line(&cv, S(vx - 22), S(ry + rh / 2), S(vx - 16), S(ry + rh / 2 + 6), S(2.2f), C_ACCENT);
            cv_line(&cv, S(x + w - 36), S(ry + rh / 2 - 6), S(x + w - 30), S(ry + rh / 2), S(2.2f), C_ACCENT);
            cv_line(&cv, S(x + w - 30), S(ry + rh / 2), S(x + w - 36), S(ry + rh / 2 + 6), S(2.2f), C_ACCENT);
        } else {
            const char *value=kind==UI_OPTION_OPEN && (!strcmp(rows[i].value,"Open") || !strcmp(rows[i].value,"Manage") || !strcmp(rows[i].value,"Press A"))?"Open  >":rows[i].value;
            bool raw=literal_value || (!strcmp(title,"Host settings") && (i==0 || i==3 || i==4 || i==5)) || (!strcmp(title,"Game stream profile") && i>=CONFIG_PROFILE_COUNT) || (!strcmp(title,"Host details") && i==1) || ((!strcmp(title,"Delete profile?") || !strcmp(title,"Delete profile")) && i==1) || !strcmp(title,"Rename profile");
            if(kind==UI_OPTION_TOGGLE) {
                bool enabled=toggle_enabled(rows[i].value);
                rrect(x+w-82,ry+17,48,24,12,enabled?C_ACCENT:HEX(0x2C353C));
                cv_rrect_outline(&cv,S(x+w-82),S(ry+17),S(48),S(24),S(12),S(1),enabled?C_ACCENT:C_BORDER);
                cv_circle(&cv,S(x+w-(enabled?46:70)),S(ry+29),S(8),enabled?C_BG_BOTTOM:C_TEXT);
                text_fit(enabled?FONT_BOLD:FONT_REGULAR,18,value_x,ry+34,value_width-80,enabled?C_TEXT:C_TEXT_DIM,literal_value?value:ui_translate(value));
            } else text_fit(on?FONT_BOLD:FONT_REGULAR,UI_TEXT_PRIMARY,value_x,ry+35,value_width,on?C_TEXT:C_TEXT_DIM,raw?value:ui_translate(value));
        }
    }

    cv_unclip(&cv);
    if (sel >= 0 && sel < n && ui_translate(rows[sel].help)) {
        cv_clip(&cv,SI(x),SI(y+panel_h+20),SI(w),SI(H-82-(y+panel_h+20)));
        wrapped(FONT_REGULAR, UI_TEXT_SECONDARY, x + 4, (y + panel_h + 44), w - 8, 25, C_TEXT_DIM, ui_translate(rows[sel].help));
        cv_unclip(&cv);
    }

    if (n > visible) {
        char position[32];
        snprintf(position, sizeof(position), "%d / %d", sel + 1, n);
        text_right(FONT_REGULAR, 16, x + w, 168, C_TEXT_DIM, position);
    }

    int kind=sel>=0 && sel<n?(kinds?kinds[sel]:option_kind(title,rows+sel,sel,adjustable)):UI_OPTION_INFO;
    bool details=sel>=0 && sel<n && (!strcmp(rows[sel].label,"Stream Profile") || !strcmp(rows[sel].label,"Launch coverage")) && !strcmp(title,"Stream");
    const char *action=!strcmp(title,"Rename profile")?"Save":!strcmp(title,"Stream Profile")?"Select":kind==UI_OPTION_OPEN?"Open":kind==UI_OPTION_CYCLE?(details?"Details":"Next value"):
        kind==UI_OPTION_TOGGLE?(sel>=0 && sel<n && toggle_enabled(rows[sel].value)?"Turn off":"Turn on"):
        (sel>=0 && sel<n?rows[sel].label:"Run action");
    struct hint hints[]={{"A",action},{"Left / Right","Change"},{"B","Back"}};
    if(!strcmp(title,"Appearance and display")) {
        struct hint system_hints[]={{"A",action},{"X","Appearance"},{"B","Back"}};
        footer(system_hints,3,NULL);
    }
    else if(!strcmp(title,"History and performance")) {
        struct hint overlay_hints[]={{"A",action},{"X","Streaming interface"},{"B","Back"}};footer(overlay_hints,3,NULL);
    }
    else if(!strcmp(title,"Aurora Shortcuts") && sel<n-3) {
        struct hint shortcut_hints[]={{"A","Details"},{"X","Delete"},{"Y","Recreate"},{"B","Back"}};
        footer(shortcut_hints,4,NULL);
    }
    else if(!strcmp(title,"Select MoonX360 XEX") || !strcmp(title,"Select Shortcuts Folder") || !strcmp(title,"Select Aurora Folder") || !strcmp(title,"Select Aurora.xex")) {
        struct hint browser_hints[]={{"A","Open / Select"},{"X","Select folder"},{"Y","Roots"},{"B","Back"}};
        struct hint xex_hints[]={{"A","Open / Select"},{"Y","Roots"},{"B","Back"}};
        if(!strcmp(title,"Select MoonX360 XEX") || !strcmp(title,"Select Aurora.xex"))footer(xex_hints,3,NULL);
        else footer(browser_hints,4,NULL);
    }
    else if(kind==UI_OPTION_INFO)footer(hints+2,1,NULL);
    else if(kind==UI_OPTION_CYCLE)footer(hints,3,NULL);
    else {struct hint simple[]={{"A",action},{"B","Back"}};footer(simple,2,NULL);}
}

void ui_draw_settings_page(const char *title,const struct ui_setting *rows,int n,int sel)
{settings_page(title,rows,n,sel,true,NULL);}
void ui_draw_settings_options(const char *title,const struct ui_setting *rows,const int *kinds,int n,int sel)
{settings_page(title,rows,n,sel,true,kinds);}
void ui_draw_profiles(const struct ui_setting *rows,int n,int sel,int factory_count)
{
    ui_set_page(UI_STREAM);begin_frame();header(NULL);tabs();breadcrumb(NULL,ui_translate("Stream Profile"));
    int first=sel>2?sel-2:0;if(first>n-5)first=n>5?n-5:0;
    float y=222;
    text(FONT_BOLD,18,UI_SAFE_X,198,C_ACCENT,ui_translate(first<factory_count?"Factory":"My Profiles"));
    for(int i=first;i<n && i<first+5;i++) {
        if(i==factory_count && i>first){text(FONT_BOLD,18,UI_SAFE_X,y+18,C_ACCENT,ui_translate("My Profiles"));y+=28;}
        bool on=i==sel;card(UI_SAFE_X,y,W-2*UI_SAFE_X,58,UI_CORNER,on);
        text_fit(on?FONT_BOLD:FONT_REGULAR,22,UI_SAFE_X+24,y+36,600,on?C_TEXT:C_TEXT_DIM,rows[i].label);
        text_right(FONT_REGULAR,19,W-UI_SAFE_X-24,y+36,C_TEXT_DIM,i==n-1?ui_translate(rows[i].value):rows[i].value);y+=66;
    }
    wrapped(FONT_REGULAR,18,UI_SAFE_X,608,W-2*UI_SAFE_X,24,C_TEXT_DIM,ui_translate(rows[sel].help));
    struct hint hints[]={{"A","Use profile"},{"X","Edit"},{"Y","Manage"},{"B","Back"}};
    if(sel==n-1){struct hint save[]={{"A","Save"},{"B","Back"}};footer(save,2,NULL);}
    else footer(hints,4,NULL);
}
void ui_draw_confirm(const char *title,const char *message,const char *confirm)
{
    float dx,dy,dw=W-2.0f*UI_SAFE_X;if(dw>DS_DIALOG_MD)dw=DS_DIALOG_MD;
    ds_dialog_frame(dw,230,C_RED,&dx,&dy);
    text(FONT_BOLD,28,dx+32,dy+48,C_TEXT,ui_translate(title));
    wrapped(FONT_REGULAR,21,dx+32,dy+94,dw-64,28,C_TEXT_DIM,message);
    rrect(dx+32,dy+159,192,48,DS_RADIUS_SM,C_RED);float width;keycap(dx+46,dy+190,"A",&width);
    text(FONT_BOLD,20,dx+84,dy+190,C_TEXT,ui_translate(confirm));
    rrect(dx+244,dy+159,164,48,DS_RADIUS_SM,DS_BG_ELEVATED);
    cv_rrect_outline(&cv,S(dx+244),S(dy+159),S(164),S(48),S(DS_RADIUS_SM),S(1),C_BORDER);
    keycap(dx+258,dy+190,"B",&width);text(FONT_REGULAR,20,dx+296,dy+190,C_TEXT,ui_translate("Cancel"));
}
void ui_draw_switch_confirm(int selected)
{
    int logical_w=cv.h>0?(cv.w*720)/cv.h:1280;
    int dw=logical_w-128;if(dw>800)dw=800;
    int dx=(logical_w-dw)/2,dy=214;
    int btn_w=(dw-96)*42/100,btn2_w=(dw-96)-btn_w;
    begin_frame();header(NULL);tabs();
    cv_unclip(&cv);cv_fill(&cv,0,0,cv.w,cv.h,ARGB(170,0,0,0));
    card((float)dx,(float)dy,(float)dw,300,UI_CORNER,false);
    text(FONT_BOLD,28,(float)(dx+32),(float)(dy+46),C_TEXT,"Switch Application?");
    wrapped(FONT_REGULAR,21,(float)(dx+32),(float)(dy+90),(float)(dw-64),28,C_TEXT_DIM,
        "Are you sure you want to launch this app? This will terminate the currently running app.");
    text(FONT_REGULAR,21,(float)(dx+32),(float)(dy+176),C_TEXT,"Any unsaved progress may be lost.");
    card((float)(dx+32),(float)(dy+214),(float)btn_w,58,4,selected==0);
    card((float)(dx+64+btn_w),(float)(dy+214),(float)btn2_w,58,4,selected==1);
    text(FONT_BOLD,22,(float)(dx+60),(float)(dy+252),C_TEXT,"Cancel");
    text(FONT_BOLD,22,(float)(dx+92+btn_w),(float)(dy+252),C_TEXT,"Switch App");
    static const struct hint hints[]={{"A","Select"},{"B","Cancel"}};footer(hints,2,NULL);
}
void ui_draw_list(const char *title,const struct ui_setting *rows,int n,int sel)
{
    if(strcmp(title,"Recent Sessions")){settings_page(title,rows,n,sel,false,NULL);return;}
    ui_set_page(UI_SETTINGS);begin_frame();header(NULL);tabs();
    breadcrumb(NULL,ui_translate("Recent Sessions"));
    int first=sel/3*3;
    for(int i=first;i<n && i<first+3;i++) {
        float y=194+(float)(i-first)*144;card(64,y,W-128,124,3,i==sel);
        text_fit(FONT_BOLD,26,88,y+39,W-176,C_TEXT,rows[i].label);
        text_fit(FONT_REGULAR,20,88,y+73,W-176,C_TEXT_DIM,rows[i].value);
        if(rows[i].help)text_fit(FONT_REGULAR,16,88,y+102,W-176,C_TEXT_FAINT,rows[i].help);
    }
    static const struct hint hints[]={{"A","Details"},{"B","Back"}};footer(hints,2,NULL);
}
void ui_draw_performance(const char *grade,const char *mode,float shown,const char *decode,
    const char *latency,const char *cause,const char *note,bool can_apply)
{
    ui_set_page(UI_SETTINGS);begin_frame();header(NULL);tabs();
    breadcrumb(NULL,ui_translate("Performance Advisor"));
    const char *labels[]={"Connection","Stream","Shown FPS","Decode","Latency / RTT","Likely bottleneck"};
    char fps[40];snprintf(fps,sizeof(fps),shown>=0?"%.1f FPS":"Not measured",shown);
    const char *values[]={grade,mode,fps,decode,latency,cause};
    float row_w=W-2*UI_SAFE_X;
    float value_x=UI_SAFE_X+row_w*0.38f;
    float value_w=UI_SAFE_X+row_w-value_x-24;
    for(int i=0;i<6;i++) {float y=191+(float)i*58;
        rrect(UI_SAFE_X,y,row_w,50,UI_CORNER,C_CARD);
        text_fit(FONT_REGULAR,21,UI_SAFE_X+20,y+33,value_x-UI_SAFE_X-36,C_TEXT_DIM,ui_translate(labels[i]));
        text_fit(FONT_BOLD,22,value_x,y+33,value_w,C_TEXT,ui_translate(values[i]));
    }
    if(note)wrapped(FONT_REGULAR,17,UI_SAFE_X,568,row_w,23,C_TEXT_DIM,ui_translate(note));
    static const struct hint hints[]={{"A","Apply safe profile"},{"X","Run analysis / details"},{"B","Back"}};
    footer(can_apply?hints:hints+1,can_apply?3:2,NULL);
}
void ui_draw_advisor(const struct ui_stat *rows,int n,const char *note,bool can_apply)
{
    ui_draw_performance(n?rows[0].value:"Not measured",n>3?rows[3].value:"Standard",-1,
        "See measured details","See measured details",n>1?rows[1].value:"Unknown",note,can_apply);
}
static bool aurora_detail_enabled,aurora_detail_exists;
void ui_app_details_aurora(bool enabled,bool exists)
{aurora_detail_enabled=enabled;aurora_detail_exists=exists;}
void ui_draw_app_details(const struct ui_host *host,const struct ui_app *app,const char *profile,const char *coverage,int selected)
{
    ui_set_page(UI_LIBRARY);begin_frame();header(host);tabs();
    breadcrumb(NULL,app->name);
    float cover_x=W<1100?UI_SAFE_X:220.0f;
    float info_x=cover_x+UI_RAIL_CARD_WIDTH+32.0f;
    float info_w=W-info_x-UI_SAFE_X;
    float value_x=info_x+info_w*0.52f;
    float value_w=info_x+info_w-value_x-20.0f;
    app_tile(app,cover_x,186,false,0);launch_source_tile(app,cover_x,186,UI_RAIL_CARD_WIDTH,UI_RAIL_CARD_HEIGHT);
    text_fit(FONT_BOLD,32,info_x,207,info_w,C_TEXT,app->name);
    text_fit(FONT_REGULAR,19,info_x,244,info_w,C_TEXT_DIM,host && host->name?host->name:ui_translate("Your PC"));
    text_fit(FONT_REGULAR,19,info_x,277,info_w,C_TEXT_DIM,ui_translate(profile));
    const char *options[]={"Play","Stream Profile",app->favorite?"Remove favorite":"Add favorite","Performance history","Stop app","Launch coverage"};
    const char *actions[]={"Start now",profile,app->favorite?"Remove":"Add","Open history  >",app->running?"Stop on PC":"Not running",coverage};
    for(int i=0;i<(aurora_detail_enabled?7:6);i++) {
        float y=290+(float)i*49;bool enabled=i!=4 || app->running;
        card(info_x,y,info_w,46,UI_CORNER,i==selected);
        const char *label=i==6?(aurora_detail_exists?"Aurora Shortcut":"Create Aurora Shortcut"):options[i];
        const char *value=i==6?(aurora_detail_exists?"Manage  >":"Create  >"):actions[i];
        text_fit(FONT_BOLD,20,info_x+20,y+30,value_x-info_x-32,!enabled?C_TEXT_FAINT:i==selected?C_TEXT:C_TEXT_DIM,ui_translate(label));
        text_fit(FONT_REGULAR,18,value_x,y+30,value_w,!enabled?C_TEXT_FAINT:i==4?C_RED:C_TEXT_DIM,(i==1 || i==5)?value:ui_translate(value));
    }
    const char *action=selected==0?"Play now":selected==1?"Open stream settings":selected==2?(app->favorite?"Remove favorite":"Add favorite"):
        selected==3?"Open history":selected==5?"Open settings  >":selected==6?"Open Aurora Shortcut":app->running?"Stop app":"Not running";
    struct hint hints[]={{"A",action},{"B","Back"}};
    if(selected==1 || selected==5){struct hint inline_hints[]={{"Left / Right","Change"},{"A","Details"},{"B","Back"}};footer(inline_hints,3,NULL);}
    else footer(selected==4 && !app->running?hints+1:hints,selected==4 && !app->running?1:2,NULL);
}

void ui_draw_ip_editor(const int octets[4], int sel)
{
    static const struct hint hints[] = {
        { "\xE2\x86\x91 \xE2\x86\x93", "\xC2\xB1" "1" }, { "X Y", "\xC2\xB1" "10" },
        { "\xE2\x86\x90 \xE2\x86\x92", "Move" }, { "A", "Save" }, { "B", "Cancel" }
    };
    float dw = 720, dh = 290, dx, dy, bw = 118, bh = 96, gap = 38, bx0;
    int i;

    begin_frame();
    header(NULL);
    if(location_depth)breadcrumb(NULL,ui_translate("Add host"));
    dialog(dw, dh, &dx, &dy);

    text(FONT_BOLD, 26, dx + 40, dy + 58, C_TEXT, ui_translate("Host PC address"));
    text(FONT_REGULAR, 17, dx + 40, dy + 88, C_TEXT_DIM, ui_translate("The IP address of the PC running Vibepollo or Sunshine."));

    bx0 = dx + (dw - (4 * bw + 3 * gap)) / 2;
    for (i = 0; i < 4; i++) {
        float bx = bx0 + i * (bw + gap), by = dy + 140;
        char num[8];
        bool on = i == sel;

        snprintf(num, sizeof(num), "%d", octets[i]);
        rrect(bx, by, bw, bh, 14, on ? C_CARD_SEL : HEX(0x111821));
        cv_rrect_outline(&cv, S(bx), S(by), S(bw), S(bh), S(14), on ? S(3) : S(1), on ? C_ACCENT : C_BORDER);
        text_center(FONT_BOLD, 46, bx + bw / 2, by + bh / 2 + 16, on ? C_TEXT : C_TEXT_DIM, num);
        if (on) {
            cv_triangle(&cv, S(bx + bw / 2 - 9), S(by - 10), S(bx + bw / 2 + 9), S(by - 10), S(bx + bw / 2), S(by - 20), C_ACCENT);
            cv_triangle(&cv, S(bx + bw / 2 - 9), S(by + bh + 10), S(bx + bw / 2 + 9), S(by + bh + 10), S(bx + bw / 2), S(by + bh + 20), C_ACCENT);
        }
        if (i < 3)
            cv_circle(&cv, S(bx + bw + gap / 2), S(by + bh - 18), S(4.5f), C_TEXT_DIM);
    }
    footer(hints, 5, NULL);
}

void ui_draw_pair(const struct ui_host *host, const char *pin, uint32_t t_ms)
{
    float dw = 760, dh = 390, dx, dy, bw = 96, bh = 116, gap = 20, bx0;
    char title[160];
    int i;

    begin_frame();
    header(host);
    dialog(dw, dh, &dx, &dy);

    snprintf(title, sizeof(title), ui_translate("Pair with %s"), host && host->name && host->name[0] ? host->name :
             host ? host->address : ui_translate("host"));
    text_fit(FONT_BOLD, 26, dx + 40, dy + 58, dw - 80, C_TEXT, title);
    wrapped(FONT_REGULAR, 17, dx + 40, dy + 90, dw - 80, 24, C_TEXT_DIM,
            ui_translate("On your PC, open Sunshine, Apollo or Vibepollo and enter this pairing PIN:"));

    bx0 = dx + (dw - (4 * bw + 3 * gap)) / 2;
    for (i = 0; i < 4 && pin[i]; i++) {
        char d[2] = { pin[i], 0 };
        float bx = bx0 + i * (bw + gap), by = dy + 150;
        rrect(bx, by, bw, bh, 16, HEX(0x111821));
        cv_rrect_outline(&cv, S(bx), S(by), S(bw), S(bh), S(16), S(2), C_ACCENT);
        text_center(FONT_BOLD, 64, bx + bw / 2, by + bh / 2 + 23, C_TEXT, d);
    }

    spinner(dx + 60, dy + dh - 50, 11, t_ms);
    text(FONT_REGULAR, 17, dx + 84, dy + dh - 44, C_TEXT_DIM, ui_translate("Waiting for the PIN on the PC. B cancels."));
}

void ui_draw_host_failure(const char *title,const char *reason)
{
    begin_frame();header(NULL);tabs();
    card(UI_SAFE_X,UI_CONTENT_TOP,W-2*UI_SAFE_X,360,UI_CORNER,false);
    icon(ICON_MONITOR,UI_SAFE_X+42,UI_CONTENT_TOP+58,25,C_TEXT_DIM);
    text_fit(FONT_BOLD,UI_TEXT_TITLE,UI_SAFE_X+86,UI_CONTENT_TOP+69,W-2*UI_SAFE_X-116,C_TEXT,title);
    wrapped(FONT_REGULAR,UI_TEXT_PRIMARY,UI_SAFE_X+32,UI_CONTENT_TOP+130,W-2*UI_SAFE_X-64,32,C_TEXT_DIM,ui_translate(reason));
    static const struct hint hints[]={{"A","Try again"},{"X","Host details"},{"Y","Forget host"},{"B","Back"}};
    footer(hints,4,NULL);
}

void ui_draw_busy(const char *title, const char *message, uint32_t t_ms)
{
    message=ui_translate(message);
    float dw = 640, dx, dy;
    int lines = message ? wrapped_lines(FONT_REGULAR, 18, dw - 150, message) : 0;
    float dh = 120.0f + (float)lines * 25.0f;

    begin_frame();
    header(NULL);
    dialog(dw, dh, &dx, &dy);
    spinner(dx + 64, dy + dh / 2, 20, t_ms);
    text_fit(FONT_BOLD, 24, dx + 110, dy + 56, dw - 140, C_TEXT, ui_translate(title));
    if (message)
        wrapped(FONT_REGULAR, 18, dx + 110, dy + 88, dw - 150, 25, C_TEXT_DIM, ui_translate(message));
    static const struct hint hints[]={{"B","Cancel"}};footer(hints,1,NULL);
}

void ui_draw_message(int which, const char *title, const char *body)
{
    body=ui_translate(body);
    static const struct hint hints[] = { { "A", "OK" } };
    float dw = 700, dx, dy;
    int lines = body ? wrapped_lines(FONT_REGULAR, 19, dw - 160, body) : 0;
    float dh = 150.0f + (float)lines * 27.0f;
    uint32_t color = which == ICON_ERROR ? C_RED : which == ICON_CHECK ? C_GREEN : C_ACCENT;

    if (dh < 190)
        dh = 190;
    begin_frame();
    header(NULL);
    ds_dialog_frame(dw, dh, color, &dx, &dy);
    cv_circle(&cv, S(dx + 70), S(dy + 72), S(28), WITH_ALPHA(color, 50));
    icon(which, dx + 70, dy + 72, 15, color);
    text_fit(FONT_BOLD, 26, dx + 120, dy + 72, dw - 160, C_TEXT, ui_translate(title));
    if (body)
        wrapped(FONT_REGULAR, 19, dx + 120, dy + 110, dw - 160, 27, C_TEXT_DIM, ui_translate(body));
    footer(hints, 1, NULL);
}

void ui_draw_stats(const char *title, const struct ui_stat *stats, int n, const char *note)
{
    ui_draw_stream_stats(title, stats, n, note, false);
}

void ui_draw_log(const char *title, const char *const *lines, int count, int first)
{
    static const struct hint hints[] = {
        { "Up Down", "Scroll" }, { "Start", "Page" }, { "Left Right", "Current / previous" }, { "B", "Back" }
    };
    char position[48];
    int i, end = first + 10 < count ? first + 10 : count;
    begin_frame();
    header(NULL);
    tabs();
    breadcrumb(NULL,ui_translate(title));
    card(64,184,W-128,350,16,false);
    for (i = first; i < end; i++)
        text_fit(FONT_REGULAR, 17, 84, 215.0f + (float)(i - first) * 32.0f, W - 168, C_TEXT, lines[i]);
    snprintf(position, sizeof(position), "%d-%d / %d", count ? first + 1 : 0, end, count);
    text_right(FONT_REGULAR, 16, W - 64, 162, C_TEXT_DIM, position);
    wrapped(FONT_REGULAR, 16, 68, 568, W - 136, 22, C_TEXT_DIM,
            ui_translate("Saved checkpoints show where execution stopped, not a crash backtrace. A sudden crash can lose the RAM log. "
            "Enable Diagnostic capture before reproducing a problem."));
    footer(hints, 4, NULL);
}

void ui_draw_stream_stats(const char *title, const struct ui_stat *stats, int n, const char *note, bool retry)
{
    note=ui_translate(note);
    static const struct hint hints[] = { { "A", "OK" } };
    static const struct hint retry_hints[] = { { "A", "Retry connection" }, { "B", "Back" } };
    float dw = 780, rh = 34, dx, dy;
    int note_lines = note ? wrapped_lines(FONT_REGULAR, 16, dw - 80, note) : 0;
    float dh = 120 + n * rh + note_lines * 22 + (note ? 20 : 0);
    int i;

    begin_frame();
    header(NULL);
    if(location_depth) {
        tabs();breadcrumb(NULL,title);
        dx=64;dy=184;dw=W-128;rh=30;
        note_lines=note?wrapped_lines(FONT_REGULAR,16,dw-80,note):0;
        dh=120+(float)n*rh+(float)note_lines*22+(note?20:0);
        card(dx,dy,dw,dh,4,false);
    } else dialog(dw,dh,&dx,&dy);
    text(FONT_BOLD, 26, dx + 40, dy + 58, C_TEXT, title);
    for (i = 0; i < n; i++) {
        float ry = dy + 100 + i * rh;
        if (i > 0)
            cv_fill(&cv, SI(dx + 40), SI(ry - 23), SI(dw - 80), 1, WITH_ALPHA(C_BORDER, 200));
        text(FONT_REGULAR, 17, dx + 40, ry, C_TEXT_DIM, ui_translate(stats[i].label));
        text_right(FONT_BOLD, 17, dx + dw - 40, ry, C_TEXT,
                   !strcmp(stats[i].label,"Host")?stats[i].value:ui_translate(stats[i].value));
    }
    if (note)
        wrapped(FONT_REGULAR, 16, dx + 40, dy + 110 + n * rh + 10, dw - 80, 22, C_TEXT_FAINT, ui_translate(note));
    if(retry)footer(retry_hints,2,NULL);
    else if(location_depth){static const struct hint back[]={{"B","Back"}};footer(back,1,NULL);}
    else footer(hints,1,NULL);
}

/* Capture the last visible tile geometry before replacing the scene. Art stays
 * owned by the menu until the platform has made its immutable launch panel. */
#include "../launch_cover.h"
static float launch_x,launch_y,launch_w,launch_h;
static char launch_name[192],launch_profile[128],launch_mode[96];
static bool launch_crt_note;
void ui_launch_crt_note(bool show){launch_crt_note=show;}
static char launch_pair_label[80];
void ui_launch_pair_pin(const char *pin){snprintf(launch_pair_label,sizeof(launch_pair_label),ui_translate("Pairing PIN: %s"),pin);}
static const char *launch_labels[LAUNCH_STATUS_COUNT]={
    "Starting application...","Connecting to PC...","Finding the PC...","Negotiating stream...",
    "Preparing video...","Preparing audio...","Preparing connection...","Preparing controller...",
    "Waiting for first video frame...","Video ready","Cancelling...","Retrying with stereo...","Connection failed",launch_pair_label
};
void ui_launch_session(const char *profile,int width,int height,int fps,int bitrate_kbps)
{
    snprintf(launch_profile,sizeof(launch_profile),"%s",profile?profile:"");
    snprintf(launch_mode,sizeof(launch_mode),"%d x %d  |  %d FPS  |  %d.%d Mbps",width,height,fps,bitrate_kbps/1000,(bitrate_kbps%1000)/100);
}
void ui_launch_begin(const struct ui_app *app,uint32_t now,bool motion)
{
    launch_x=W/2-126;launch_y=122;launch_w=252;launch_h=336;
    int count=0;const struct ui_draw_command *commands=ui_draw_commands(&count);
    float fx=0,fy=0,fw=0,fh=0,best=1e30f;ui_focus_bounds(&fx,&fy,&fw,&fh);
    if(!strcmp(launch_source_name,app->name)) {
        launch_x=launch_source[0];launch_y=launch_source[1];launch_w=launch_source[2];launch_h=launch_source[3];
    } else if(fw>0 && fh>0){launch_x=fx/s;launch_y=fy/s;launch_w=fw/s;launch_h=fh/s;}
    if(commands && app->art)for(int i=0;i<count;i++)if(commands[i].kind==UI_IMAGE && commands[i].pixels==app->art) {
        float dx=commands[i].x+commands[i].w/2-fx-fw/2,dy=commands[i].y+commands[i].h/2-fy-fh/2;
        float distance=dx*dx+dy*dy;if(distance>=best)continue;best=distance;
        launch_x=commands[i].x/s;launch_y=commands[i].y/s;launch_w=commands[i].w/s;launch_h=commands[i].h/s;
    }
    /* Focus can be a wide action row. Keep the cover's ratio throughout interpolation. */
    if(fabsf(launch_w*TILE_H-launch_h*TILE_W)>0.01f) {
        float fit=launch_w/TILE_W;if(launch_h/TILE_H<fit)fit=launch_h/TILE_H;
        float fitted_w=TILE_W*fit,fitted_h=TILE_H*fit;
        launch_x+=(launch_w-fitted_w)/2;launch_y+=(launch_h-fitted_h)/2;launch_w=fitted_w;launch_h=fitted_h;
    }
    snprintf(launch_name,sizeof(launch_name),"%s",app->name);
    ui_draw_hide_image(app->art,S(launch_x),S(launch_y),S(launch_w),S(launch_h));
    (void)now;(void)motion;
    launch_profile[0]=launch_mode[0]=0;launch_crt_note=false;
}
void ui_launch_origin(float *x,float *y,float *w,float *h)
{*x=launch_x;*y=launch_y;*w=launch_w;*h=launch_h;}
/* Build a straight-alpha text atlas on the menu thread, once per opening.
 * Canvas compositing normally forces alpha opaque, so glyph masks use this
 * explicit sink. Renderer only samples the finished atlas. */
static void launch_text_glyph(canvas_t *c,int x,int y,const uint8_t *mask,int w,int h,int stride,uint32_t color)
{
    for(int yy=0;yy<h;yy++)for(int xx=0;xx<w;xx++) {
        int dx=x+xx,dy=y+yy;if(dx<0 || dx>=c->w || dy<0 || dy>=c->h)continue;
        unsigned alpha=mask[yy*stride+xx]*(color>>24)/255;uint32_t *out=c->px+dy*c->w+dx;
        unsigned old=*out>>24,combined=alpha+old*(255-alpha)/255;
        if(combined)*out=(combined<<24)|(color&0xffffff);
    }
}
void ui_launch_text(uint32_t *pixels,int width,int height)
{
    if(!pixels || width!=LAUNCH_TEXT_W || height!=LAUNCH_TEXT_H)return;
    canvas_t panel;memset(&panel,0,sizeof(panel));panel.px=pixels;panel.w=width;panel.h=height;cv_unclip(&panel);
    memset(pixels,0,(size_t)width*height*4);font_set_sink(&panel,launch_text_glyph);
    const char *labels[]={launch_name,launch_profile,launch_mode};
    for(int i=0;i<3;i++) {
        int size=i==0?28:launch_crt_note?16:19,face=i==0?FONT_BOLD:FONT_REGULAR;
        int tw=font_text_width(face,(float)size,labels[i]);if(tw>480)tw=480;
        font_draw_fit(&panel,face,(float)size,(width-tw)/2,i==0?34:i==1?(launch_crt_note?62:66):(launch_crt_note?78:88),480,i==0?C_TEXT:C_TEXT_DIM,labels[i]);
    }
    if(launch_crt_note) {
        const char *note="Compatible CRT resolution not detected. Using selected settings.";
        int tw=font_text_width(FONT_REGULAR,12,note);if(tw>480)tw=480;
        font_draw_fit(&panel,FONT_REGULAR,12,(width-tw)/2,92,480,C_TEXT_DIM,note);
    }
    for(int i=0;i<LAUNCH_STATUS_COUNT+3;i++) {
        const char *label=i<LAUNCH_STATUS_COUNT?launch_labels[i]:i==LAUNCH_STATUS_COUNT?"B: Cancel":i==LAUNCH_STATUS_COUNT+1?"B: Cancel | X: Reveal video":"MoonX360";
        label=ui_translate(label);int size=i<LAUNCH_STATUS_COUNT?18:i==LAUNCH_STATUS_COUNT+2?24:16;
        int tw=font_text_width(FONT_REGULAR,(float)size,label);if(tw>480)tw=480;
        int row=i<LAUNCH_STATUS_COUNT?LAUNCH_STATUS_Y+i*LAUNCH_STATUS_H:i==LAUNCH_STATUS_COUNT+2?LAUNCH_LOGO_Y:LAUNCH_HINT_Y+(i-LAUNCH_STATUS_COUNT)*24;
        font_draw_fit(&panel,FONT_REGULAR,(float)size,(width-tw)/2,row+(i<LAUNCH_STATUS_COUNT?17:19),480,i==LAUNCH_STATUS_COUNT+2?C_ACCENT:C_TEXT_DIM,label);
    }
    font_set_sink(&cv,ui_glyph);
}

/* Reusable ContextMenu overlay: anchored near the focused item, clamped inside
 * 16:9 / 4:3 safe area, scrollable for long lists, with consistent focus bar. */
void ui_draw_context_menu(const char *title,const struct ui_setting *items,int count,int selected,float anchor_x,float anchor_y)
{
    if(!items || count<=0)return;
    if(selected<0)selected=0;
    if(selected>=count)selected=count-1;
    int visible=count<UI_CONTEXT_MAX_VISIBLE?count:UI_CONTEXT_MAX_VISIBLE;
    int first=selected-visible/2;
    if(first<0)first=0;
    if(first>count-visible)first=count-visible;

    float mw=360.0f;
    float max_w=W-2.0f*UI_SAFE_X;
    if(mw>max_w)mw=max_w;
    float header_h=(title && *title)?46.0f:12.0f;
    float row_h=46.0f;
    float footer_h=38.0f;
    float mh=header_h+(float)visible*row_h+footer_h+8.0f;

    float mx=anchor_x,my=anchor_y;
    if(mx<UI_SAFE_X)mx=UI_SAFE_X;
    if(mx+mw>W-UI_SAFE_X)mx=W-UI_SAFE_X-mw;
    if(my<128.0f)my=128.0f;
    if(my+mh>H-68.0f)my=H-68.0f-mh;
    if(my<64.0f)my=64.0f;

    cv_unclip(&cv);
    cv_fill(&cv,0,0,cv.w,cv.h,ARGB(110,0,0,0));
    rrect(mx,my,mw,mh,DS_RADIUS_LG,DS_BG_SURFACE);
    cv_rrect_outline(&cv,S(mx),S(my),S(mw),S(mh),S(DS_RADIUS_LG),S(1),DS_BORDER_STRONG);
    cv_fill(&cv,SI(mx+12),SI(my),SI(mw-24),SI(3),C_ACCENT);

    if(title && *title) {
        text_fit(FONT_BOLD,18,mx+18,my+31,mw-36,C_TEXT,ui_translate(title));
        cv_fill(&cv,SI(mx+14),SI(my+header_h-2),SI(mw-28),1,WITH_ALPHA(C_BORDER,160));
    }

    for(int i=first;i<first+visible;i++) {
        float ry=my+header_h+4.0f+(float)(i-first)*row_h;
        bool on=i==selected;
        if(on) {
            rrect(mx+8,ry,mw-16,row_h-4,DS_RADIUS_SM,C_CARD_SEL);
            cv_rrect_outline(&cv,S(mx+8),S(ry),S(mw-16),S(row_h-4),S(DS_RADIUS_SM),S(2),C_ACCENT);
            ui_focus_mark(UI_SETTINGS+10,i);
        }
        bool has_val=items[i].value && items[i].value[0];
        float label_w=has_val?(mw-136.0f):(mw-40.0f);
        text_fit(on?FONT_BOLD:FONT_REGULAR,18,mx+20,ry+28,label_w,on?C_TEXT:C_TEXT_DIM,ui_translate(items[i].label));
        if(has_val)text_right(FONT_REGULAR,15,mx+mw-20,ry+28,on?C_TEXT:C_TEXT_FAINT,ui_translate(items[i].value));
    }

    float fy=my+mh-footer_h;
    cv_fill(&cv,SI(mx+14),SI(fy),SI(mw-28),1,WITH_ALPHA(C_BORDER,140));
    float kw=0;
    keycap(mx+16,fy+26,"A",&kw);
    text(FONT_REGULAR,14,mx+22+kw,fy+26,C_TEXT_DIM,ui_translate("Select"));
    float bx=mx+mw-110;
    keycap(bx,fy+26,"B",&kw);
    text(FONT_REGULAR,14,bx+kw+6,fy+26,C_TEXT_DIM,ui_translate("Back"));
}

/* MoonX360 Smart Virtual Keyboard — native Design System renderer (16:9 & 4:3 CRT). */
void ui_draw_smart_keyboard(const struct sk_state *sk)
{
    if(!sk)return;
    begin_frame();
    header(NULL);
    bool compact=W<1100.0f;
    float pw=W-2.0f*UI_SAFE_X;
    if(pw>1020.0f)pw=1020.0f;
    float px=(W-pw)*0.5f;
    float py=104.0f;
    float ph=544.0f;

    /* Main keyboard surface */
    rrect(px,py,pw,ph,DS_RADIUS_LG,DS_BG_SURFACE);
    cv_rrect_outline(&cv,S(px),S(py),S(pw),S(ph),S(DS_RADIUS_LG),S(1),DS_BORDER_STRONG);
    cv_fill(&cv,SI(px+16),SI(py),SI(pw-32),SI(3),C_ACCENT);

    /* 1. Header: title, subtitle, and mode/language/shift badges */
    const char *title=sk->title[0]?ui_translate(sk->title):ui_translate("Text Input");
    text_fit(FONT_BOLD,24,px+24,py+38,pw-290,C_TEXT,title);
    if(sk->subtitle[0])
        text_fit(FONT_REGULAR,15,px+24,py+60,pw-290,C_TEXT_DIM,ui_translate(sk->subtitle));

    static const char *lang_tags[]={"EN","PT-BR","ES"};
    const char *page_tag=sk->page==SK_PAGE_NUMERIC?"IPv4":
                         sk->page==SK_PAGE_SYMBOLS?"123 / #":
                         sk->page==SK_PAGE_ACCENTS?"\xc3\x81\xc3\x89\xc3\x87":
                         sk->shift==SK_SHIFT_CAPS?"CAPS":
                         sk->shift==SK_SHIFT_ONCE?"SHIFT":"ABC";
    char badge[64];
    if(sk->dual_stick)
        snprintf(badge,sizeof(badge),"DUAL-STICK  |  %s  |  %s",page_tag,lang_tags[sk->lang%SK_LANG_COUNT]);
    else
        snprintf(badge,sizeof(badge),"%s  |  %s",page_tag,lang_tags[sk->lang%SK_LANG_COUNT]);
    float bw=text_w(FONT_BOLD,14,badge)+22.0f;
    rrect(px+pw-24-bw,py+18,bw,28,DS_RADIUS_SM,DS_BG_ELEVATED);
    cv_rrect_outline(&cv,S(px+pw-24-bw),S(py+18),S(bw),S(28),S(DS_RADIUS_SM),S(1),C_BORDER);
    text_center(FONT_BOLD,14,px+pw-24-bw*0.5f,py+37,C_ACCENT,badge);

    /* 2. Editor field with UTF-8 horizontal scroll & high-contrast cursor */
    float ex=px+24,ey=py+74,ew=pw-48,eh=56;
    rrect(ex,ey,ew,eh,DS_RADIUS_MD,DS_BG_BASE);
    cv_rrect_outline(&cv,S(ex),S(ey),S(ew),S(eh),S(DS_RADIUS_MD),S(1),
                     sk->error[0]?C_RED:DS_BORDER_STRONG);

    char display[SK_TEXT_MAX],prefix_buf[SK_TEXT_MAX];
    int tlen=(int)strlen(sk->text);
    if(sk->mode==SK_MODE_PASSWORD) {
        int chars=0,c_chars=0;
        for(int i=0;i<tlen;i++)if(!sk_is_utf8_cont((unsigned char)sk->text[i])) {
            if(i<sk->cursor)c_chars++;
            if(chars<SK_TEXT_MAX-1)display[chars++]='*';
        }
        display[chars]='\0';
        memcpy(prefix_buf,display,(size_t)c_chars);
        prefix_buf[c_chars]='\0';
    } else {
        memcpy(display,sk->text,(size_t)(tlen+1));
        int clen=sk->cursor<=tlen?sk->cursor:tlen;
        memcpy(prefix_buf,sk->text,(size_t)clen);
        prefix_buf[clen]='\0';
    }
    float text_area=ew-92.0f;
    float cur_x=text_w(FONT_BOLD,22,prefix_buf);
    float scroll_x=cur_x>text_area-12.0f?cur_x-(text_area-12.0f):0.0f;
    cv_clip(&cv,SI(ex+12),SI(ey+4),SI(text_area+8),SI(eh-8));
    text(FONT_BOLD,22,ex+16-scroll_x,ey+36,C_TEXT,display);
    cv_fill(&cv,SI(ex+16+cur_x-scroll_x),SI(ey+12),SI(2),SI(32),C_ACCENT);
    cv_unclip(&cv);

    char counter[24];
    snprintf(counter,sizeof(counter),"%d/%d",tlen,sk->max_bytes-1);
    text_right(FONT_REGULAR,14,ex+ew-12,ey+34,C_TEXT_FAINT,counter);
    if(sk->error[0])
        text_fit(FONT_BOLD,14,ex+4,ey+eh+17,ew-8,C_RED,ui_translate(sk->error));

    /* 3. Suggestion bar (3 stable slots with real LB/RB icons) */
    float sy=ey+eh+22,sh=42;
    if(sk->mode!=SK_MODE_PASSWORD && sk->mode!=SK_MODE_IP) {
        ui_button(&cv,"LB",SI(ex),SI(sy+7),SI(34),SI(26));
        ui_button(&cv,"RB",SI(ex+ew-34),SI(sy+7),SI(34),SI(26));
        float slot_gap=10.0f;
        float bar_x=ex+44.0f,bar_w=ew-88.0f;
        float slot_w=(bar_w-slot_gap*2.0f)/3.0f;
        for(int i=0;i<SK_MAX_SUGGESTIONS;i++) {
            float sx=bar_x+(float)i*(slot_w+slot_gap);
            bool has=i<sk->suggestions.count;
            bool on=has && sk->zone==SK_ZONE_SUGGESTIONS && sk->sug_index==i;
            rrect(sx,sy,slot_w,sh,DS_RADIUS_SM,on?C_CARD_SEL:DS_BG_ELEVATED);
            cv_rrect_outline(&cv,S(sx),S(sy),S(slot_w),S(sh),S(DS_RADIUS_SM),S(on?2:1),
                             on?C_ACCENT:WITH_ALPHA(C_BORDER,has?180:85));
            if(on)ui_focus_mark(UI_SETTINGS+20,i);
            if(has)
                text_center(on?FONT_BOLD:FONT_REGULAR,17,sx+slot_w*0.5f,sy+27,on?C_TEXT:C_TEXT_DIM,sk->suggestions.items[i]);
            else if(i==1 && !sk->suggestions.count)
                text_center(FONT_REGULAR,14,sx+slot_w*0.5f,sy+26,C_TEXT_FAINT,
                            ui_translate(sk->predictions_enabled?"Type for suggestions":"Suggestions off"));
        }
    }

    /* 4. Key Grid (QWERTY / Symbols / Accents / Numeric IPv4) */
    float gy=sy+sh+16;
    if(sk->page==SK_PAGE_NUMERIC) {
        float kw=128.0f,kh=58.0f,gap=12.0f;
        float gw=kw*3.0f+gap*2.0f;
        float gx=px+(pw-gw)*0.5f;
        for(int r=0;r<SK_NUM_ROWS;r++)for(int c=0;c<SK_NUM_COLS;c++) {
            float kx=gx+(float)c*(kw+gap),ky=gy+(float)r*(kh+gap);
            bool on=sk->zone==SK_ZONE_KEYS && sk->row==r && sk->col==c;
            const char *lbl=sk_key_label(sk->page,sk->shift,r,c);
            rrect(kx,ky,kw,kh,DS_RADIUS_MD,on?C_CARD_SEL:DS_BG_ELEVATED);
            cv_rrect_outline(&cv,S(kx),S(ky),S(kw),S(kh),S(DS_RADIUS_MD),S(on?DS_FOCUS_BORDER:1),on?C_ACCENT:C_BORDER);
            if(on)ui_focus_mark(UI_SETTINGS+21,r*SK_NUM_COLS+c);
            text_center(FONT_BOLD,24,kx+kw*0.5f,ky+38,on?C_TEXT:C_TEXT_DIM,lbl);
        }
    } else {
        float gap=compact?7.0f:10.0f;
        float kw=(ew-gap*9.0f)/10.0f;
        float kh=74.0f;
        for(int r=0;r<SK_COMPACT_ROWS;r++)for(int c=0;c<SK_COMPACT_COLS;c++) {
            float kx=ex+(float)c*(kw+gap),ky=gy+(float)r*(kh+gap);
            bool left_on=sk->zone==SK_ZONE_KEYS && sk->row==r && sk->col==c;
            bool right_on=sk->dual_stick && sk->right_row==r && sk->right_col==c;
            const char *lbl=sk_compact_key_label(sk->page,sk->shift,r,c);
            uint32_t fill=left_on?C_CARD_SEL:right_on?HEX(0x1E3348):DS_BG_ELEVATED;
            uint32_t border=left_on?C_ACCENT:right_on?DS_BTN_X:C_BORDER;
            rrect(kx,ky,kw,kh,DS_RADIUS_MD,fill);
            cv_rrect_outline(&cv,S(kx),S(ky),S(kw),S(kh),S(DS_RADIUS_MD),S((left_on||right_on)?DS_FOCUS_BORDER:1),border);
            if(left_on)ui_focus_mark(UI_SETTINGS+22,r*SK_COMPACT_COLS+c);
            text_center(FONT_BOLD,compact?21.0f:24.0f,kx+kw*0.5f,ky+47,
                        (left_on||right_on)?C_TEXT:C_TEXT_DIM,lbl);
        }
        if(sk->dual_stick) {
            float mid_x=ex+5.0f*kw+4.5f*gap;
            cv_fill(&cv,SI(mid_x),SI(gy+4),SI(1),SI(kh*3.0f+gap*2.0f-8),WITH_ALPHA(C_ACCENT,120));
        }
        text_center(FONT_REGULAR,13,px+pw*0.5f,gy+kh*3.0f+gap*2.0f+24,C_TEXT_FAINT,
                    ui_translate("Right Stick moves cursor  |  Hold X repeats Backspace  |  R3 toggles experimental Dual-Stick"));
    }

    /* 5. Footer with real Xbox 360 graphic icons */
    if(sk->page==SK_PAGE_NUMERIC) {
        static const struct hint num_hints[]={{"A","Type"},{"X","Delete"},{"Start","Confirm"},{"B","Cancel"}};
        footer(num_hints,4,NULL);
    } else if(compact) {
        static const struct hint c_hints[]={{"A","Type"},{"X","Delete"},{"Y","Space"},{"RT","Symbols"},{"Start","Done"},{"B","Cancel"}};
        footer(c_hints,6,NULL);
    } else {
        static const struct hint w_hints[]={{"A","Type"},{"X","Delete"},{"Y","Space"},{"LT","Shift"},{"RT","Symbols"},{"Start","Done"},{"B","Cancel"}};
        footer(w_hints,7,NULL);
    }
}
