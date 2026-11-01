#include "ui.h"
#include "../performance.h"
#include "font.h"
#include "draw.h"
#include "translations.h"
#include "brand.h"
#include "layout.h"
#include "../config.h"
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

/* ------------------------------------------------------------ theme */

#define C_BG_TOP     HEX(0x202629)
#define C_BG_BOTTOM  HEX(0x121619)
#define C_ACCENT     theme_accent
#define C_CARD       HEX(0x22272B)
#define C_CARD_SEL   theme_selection
#define C_BORDER     HEX(0x414A4D)
#define C_TEXT       HEX(0xF2F5F3)
#define C_TEXT_DIM   HEX(0xC0C9C5)
#define C_TEXT_FAINT HEX(0x99A6A0)
#define C_GREEN      HEX(0x72C840)
#define C_YELLOW     HEX(0xF5B94A)
#define C_RED        HEX(0xF0605D)
#define C_BTN_A      HEX(0x5DB82E)
#define C_BTN_B      HEX(0xE0453A)
#define C_BTN_X      HEX(0x2E7FD9)
#define C_BTN_Y      HEX(0xE8B623)
#define C_SHADOW     ARGB(150, 0, 0, 0)

static uint32_t theme_accent=HEX(0x72C840),theme_selection=HEX(0x304535);
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
static float W, H;      /* at least 1280x720; narrower screens gain vertical space */
static int exit_shortcut;
static canvas_t main_cache;
static bool main_cache_valid,main_cache_gpu;
static uint64_t main_cache_key;
static int ui_language;
const char *ui_translate(const char *english)
{
    if(!ui_language || !english)return english;
    int lo=0,hi=(int)(sizeof(ui_translations)/sizeof(ui_translations[0]))-1;
    while(lo<=hi){int m=(lo+hi)/2,c=strcmp(english,ui_translations[m].en);
        if(!c)return ui_language==CONFIG_LANGUAGE_PT_BR?ui_translations[m].pt_br:ui_translations[m].es;
        if(c<0)hi=m-1;else lo=m+1;}
    return english;
}
void ui_set_language(int language)
{
    ui_language=language>=0 && language<CONFIG_LANGUAGE_COUNT?language:CONFIG_LANGUAGE_ENGLISH;
    ui_drop_main_cache();
}
static int active_page;
static int settings_context_page=-1,settings_context_category=-1;
void ui_set_settings_context(int page,int category)
{
    settings_context_page=page;settings_context_category=category;
    if(page>=0)ui_set_page(page);
}
static const char *page_names[]={"Home","Library","Stream","Settings"};
void ui_set_page(int page) {active_page=page>=0&&page<UI_PAGE_COUNT?page:UI_HOME;}

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
    cv_fill(target,0,0,target->w,target->h,C_BG_BOTTOM);
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
    if(background<0 || background>=3)background=0;
    if(accent<0 || accent>=4)accent=0;
    if(background==theme_background_id && accent==theme_accent_id)return;
    if(accent!=theme_accent_id) {
        theme_accent=colors[accent];theme_generation++;
        for(int i=0;i<128*128;i++)theme_icon[i]=(moonx360_icon[i]&0xFF000000u)|(theme_accent&0xFFFFFFu);
    }
    theme_background_id=background;theme_accent_id=accent;
    theme_selection=accent?cv_mix(C_CARD,theme_accent,32):HEX(0x304535);
    ui_drop_main_cache();
    if(bg.px)cv_free(&bg); /* CPU fallback regenerates once on its next redraw. */
}

static void reset_scroll(void);
bool ui_init(int width, int height)
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
    if(width/s<1280.0f)s=width/1280.0f;
    H = height / s;
    W = width / s;
    if(!ui_draw_enabled()) {if(!cv_init(&bg,width,height))return false;draw_background();}
    return true;
}

canvas_t *ui_canvas(void)
{
    return &cv;
}

#define TILE_W 150
#define TILE_H 200

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
    int head = (int)(t_ms / 85) % 10, i;

    for (i = 0; i < 10; i++) {
        float a = (float)i / 10.0f * 6.2831853f - 1.5707963f;
        int age = (head - i + 10) % 10;
        unsigned alpha = (unsigned)(255 - age * 22);
        cv_circle(&cv, S(cx + cosf(a) * r), S(cy + sinf(a) * r), S(r * 0.17f), WITH_ALPHA(C_ACCENT, alpha));
    }
}

/* ------------------------------------------------------------ chrome */

struct hint { const char *button; const char *label; };

static void keycap(float x, float baseline, const char *button, float *out_w)
{
    int bw=strlen(button)>1?42:28;
    if(ui_button(&cv,button,SI(x),SI(baseline-23),SI(bw),SI(28))) {*out_w=(float)bw;return;}
    if(!strcmp(button,"LB/RB")) {
        ui_button(&cv,"LB",SI(x),SI(baseline-23),SI(38),SI(28));
        ui_button(&cv,"RB",SI(x+40),SI(baseline-23),SI(38),SI(28));*out_w=78;return;
    }
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
        rrect(x, cy - 12, w, 24, 6, HEX(0x2C3646));
        text_center(FONT_BOLD, 13, x + w / 2, cy + 4.5f, C_TEXT, button);
        *out_w = w;
    }
}

static void footer(const struct hint *hints, int n, const char *right)
{
    float x = UI_SAFE_X, y = H - 40;
    int i;

    cv_fill(&cv,0,SI(H-70),cv.w,SI(70),HEX(0x171C20));
    cv_fill(&cv, 0, SI(H - 70), cv.w, 1, WITH_ALPHA(C_BORDER, 160));
    for (i = 0; i < n; i++) {
        float kw;
        keycap(x, y, hints[i].button, &kw);
        x += kw + 9;
        text(FONT_REGULAR, 17, x, y, C_TEXT_DIM, ui_translate(hints[i].label));
        x += text_w(FONT_REGULAR, 17, ui_translate(hints[i].label)) + 30;
    }
    if (right)
        text_right(FONT_REGULAR, 15, W - 64, y, C_TEXT_FAINT, right);
}

static void header(const struct ui_host *host)
{
    moon_logo(84, 56, 19);
    text(FONT_BOLD, 30, 116, 67, C_TEXT, ui_translate("MoonX360"));
    text(FONT_REGULAR, 17, 294, 66, C_TEXT_DIM, ui_translate("Streaming for Xbox 360"));

    if (host) {
        char label[160];
        float tw, pw, px;

        if (host->state == HOST_UNKNOWN)
            snprintf(label, sizeof(label), "%s", state_text(host->state));
        else
            snprintf(label, sizeof(label), "%s  \xC2\xB7  %s",
                     host->name && host->name[0] ? host->name : ui_translate("Your streaming PC"), state_text(host->state));
        tw = text_w(FONT_REGULAR, 16, label);
        pw = tw + 46;
        px = W - 64 - pw;
        rrect(px, 37, pw, 38, 19, WITH_ALPHA(C_CARD, 230));
        cv_rrect_outline(&cv, S(px), S(37), S(pw), S(38), S(19), S(1), C_BORDER);
        cv_circle(&cv, S(px + 21), S(56), S(5), state_color(host->state));
        text(FONT_REGULAR, 16, px + 34, 62, C_TEXT, label);
    }
}

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
}

static void tabs(void)
{
    for(int i=0;i<UI_PAGE_COUNT;i++) {
        float x=UI_SAFE_X+(float)i*UI_TAB_STEP;
        text(FONT_BOLD,UI_TEXT_PRIMARY,x,UI_TAB_BASELINE,i==active_page?C_TEXT:C_TEXT_DIM,ui_translate(page_names[i]));
        if(i==active_page)cv_fill(&cv,SI(x),SI(123),SI(text_w(FONT_BOLD,22,ui_translate(page_names[i]))),SI(2),C_ACCENT);
    }
    text_right(FONT_REGULAR,16,W-64,115,C_TEXT_DIM,ui_translate("LB / RB"));
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

/* Centred dialog card; returns its top-left through x/y. */
static void dialog(float w, float h, float *x, float *y)
{
    *x = (W - w) / 2;
    *y = (H - h) / 2 + 10;
    cv_fill(&cv, 0, 0, cv.w, cv.h, ARGB(110, 0, 0, 0));
    card(*x, *y, w, h, 20, false);
}

/* ------------------------------------------------------------ screens */

void ui_draw_splash(const char *status, uint32_t t_ms)
{
    begin_frame();
    moon_logo(W / 2, 270, 52);
    text_center(FONT_BOLD, 60, W / 2, 400, C_TEXT, ui_translate("MoonX360"));
    text_center(FONT_REGULAR, 24, W / 2, 440, C_TEXT_DIM, ui_translate("for Xbox 360"));
    if (status)
        text_center(FONT_REGULAR, 19, W / 2, 530, C_TEXT_DIM, ui_translate(status));
    for(int i=0;i<6;i++) {
        float x=W/2- 92 + i*32;
        cv_fill(&cv,SI(x),SI(580),SI(24),SI(4),i==(int)((t_ms/160)%6)?C_ACCENT:C_BORDER);
    }
}

int ui_dashboard_move(int sel,int dx,int dy,int n)
{
    if(n<=0)return 0;
    int next=sel+(dy?dy:dx);
    return next<0?0:next>=n?n-1:next;
}
static struct {float current,target,origin;uint32_t now,started;bool active;} home_scroll;
bool ui_home_tick(uint32_t now)
{
    home_scroll.now=now;if(!home_scroll.active)return false;
    float t=(now-home_scroll.started)/(float)UI_SCROLL_MS;if(t>1 || !ui_motion_enabled())t=1;
    float r=1-t;home_scroll.current=home_scroll.origin+(home_scroll.target-home_scroll.origin)*(1-r*r*r);
    if(t>=1){home_scroll.current=home_scroll.target;home_scroll.active=false;}return true;
}
/* Fixed logical coordinates: no per-frame layout allocation or measurement. */
static void dashboard_rect(int item,int recents,float *x,float *y,float *w,float *h)
{
    *y=186;*h=258;
    if(!item) {*x=UI_SAFE_X;*w=300;return;}
    if(item<=recents) {*x=400+(float)(item-1)*192-home_scroll.current;*w=174;return;}
    *x=64+(float)(item-recents-1)*290;*y=500;*w=272;*h=128;
}
int ui_dashboard_move_tiles(int selected,int dx,int dy,int count,int recents)
{
    int visible=recents+5;if(count>visible)count=visible;
    if(count<=0)return 0;
    if(selected<0 || selected>=count)selected=0;
    int start=selected<=recents?0:recents+1,end=start?count:recents+1;
    if(end>count)end=count;
    if(dx)return selected+dx>=start && selected+dx<end?selected+dx:selected;
    if(!dy || (dy<0 && !start) || (dy>0 && start))return selected;
    int target=start?0:recents+1;
    if(target>=count)return selected;
    int length=start?recents+1:count-target,column=selected-start;
    return target+(column<length?column:length-1);
}

static void placeholder_art(const char *name,float x,float y,float w,float h);
static void dashboard_art(const struct ui_app *app,float x,float y,float w,float h)
{
    float fit=w/TILE_W;if(h/TILE_H<fit)fit=h/TILE_H;
    float dw=TILE_W*fit,dh=TILE_H*fit;
    x+=(w-dw)/2;y+=(h-dh)/2;
    if(app && app->art) {
        ui_picture_region(&cv,app->art,SI(TILE_W),SI(TILE_H),0,0,SI(TILE_W),SI(TILE_H),
            S(x),S(y),S(dw),S(dh),255,app->generation);
        ui_cover_fade_mark();
    } else placeholder_art(app && app->name?app->name:ui_translate("Your games"),x,y,dw,dh);
}
static void dashboard_shortcut_icon(int item,float x,float y)
{
    uint32_t color=HEX(0xC8D6CF);
    if(!item) {
        for(int i=0;i<4;i++)rrect(x+(float)(i%2)*19,y+(float)(i/2)*19,14,14,0,color);
    } else if(item==1) {
        cv_rrect_outline(&cv,S(x),S(y),S(38),S(27),0,S(2),color);
        rrect(x+17,y+27,4,8,0,color);rrect(x+8,y+35,22,2,0,color);
    } else if(item==2) {
        icon(ICON_PLAY,x+18,y+18,20,color);
    } else {
        for(int i=0;i<3;i++) {float yy=y+(float)i*12;
            rrect(x,yy,36,2,0,color);rrect(x+(i==1?24:8),yy-4,5,10,0,color);}
    }
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
    static const struct hint hints[]={{"A","Play"},{"X","Details"},{"Y","Hosts"},{"B","Back"},{"LB/RB","Tabs"}};
    const char *profile=dashboard && dashboard->profile?dashboard->profile:"Custom";
    const char *mode=dashboard && dashboard->connection?dashboard->connection:"";
    int recent=dashboard?dashboard->recent_count:0;
    if(recent<0)recent=0;if(recent>8)recent=8;if(recent>=n)recent=n>0?n-1:0;
    int visible=n<recent+5?n:recent+5;
    if(sel<0 || sel>=visible)sel=0;
    float rail_target=recent>4 && sel>4 && sel<=recent?(float)(sel-4)*192:0.0f;
    if(sel>0 && sel<=recent && rail_target!=home_scroll.target) {
        home_scroll.origin=home_scroll.current;home_scroll.target=rail_target;home_scroll.started=home_scroll.now;
        home_scroll.active=ui_motion_enabled();if(!home_scroll.active)home_scroll.current=rail_target;
    }
    if(recent<=4){home_scroll.current=home_scroll.target=0;home_scroll.active=false;}
    uint64_t key=main_hash_text(main_hash_text(main_key(host,items,visible,console_ip),profile),mode)^(unsigned)recent;
    key=key*1099511628211ULL+(unsigned)(home_scroll.current*16);
    for(int i=0;dashboard && dashboard->recent && i<recent;i++) {
        const struct ui_app *a=dashboard->recent+i;
        key=main_hash_text(key,a->name);key^=(uint64_t)(uintptr_t)a->art;
        key=key*1099511628211ULL+a->generation;
    }
    if(dashboard && dashboard->resume) {
        key=main_hash_text(key,dashboard->resume->name);key^=(uint64_t)(uintptr_t)dashboard->resume->art;
        key=key*1099511628211ULL+dashboard->resume->generation;
        key=key*1099511628211ULL+(unsigned)dashboard->resume_id+(unsigned)dashboard->resume->running;
    }
    if(!ui_draw_enabled() && !main_cache.px)cv_init(&main_cache,cv.w,cv.h);
    bool reused=false;
    if(main_cache_valid && key==main_cache_key) {
        if(ui_draw_enabled() && main_cache_gpu) {
            cv_unclip(&cv);reused=ui_scene_restore(&cv);font_set_sink(&cv,ui_glyph);
        } else if(!ui_draw_enabled() && !main_cache_gpu && main_cache.px) {
            cv_copy(&cv,&main_cache);reused=true;
        }
    }
    if(!reused) {
        ui_set_page(UI_HOME);begin_frame();header(host);tabs();
        text(FONT_BOLD,UI_TEXT_SECTION,UI_SAFE_X,167,C_TEXT,ui_translate(dashboard && dashboard->resume?"Continue Playing":"Play / Connect"));
        text(FONT_BOLD,UI_TEXT_SECTION,400,167,C_TEXT,ui_translate("Recent Games"));
        text(FONT_REGULAR,17,64,482,C_TEXT_DIM,ui_translate("Quick access"));
        for(int i=0;i<visible;i++) {
            float x,y,w,h;dashboard_rect(i,recent,&x,&y,&w,&h);
            if(!i) {
                rrect(x,y,w,h,UI_CORNER,cv_mix(C_CARD,C_ACCENT,24));
                dashboard_art(dashboard?dashboard->resume:NULL,x+20,y+20,108,144);
                text_fit(FONT_BOLD,23,x+148,y+47,w-168,C_TEXT,profile);
                wrapped(FONT_REGULAR,18,x+148,y+80,w-168,26,C_TEXT_DIM,mode);
                float kw;keycap(x+148,y+147,"A",&kw);text(FONT_REGULAR,18,x+184,y+147,C_TEXT,ui_translate("Play"));
                const char *name=dashboard && dashboard->resume?dashboard->resume->name:ui_translate("Your streaming PC");
                text_fit(FONT_BOLD,24,x+20,y+206,w-40,C_TEXT,name);
                text_fit(FONT_REGULAR,17,x+20,y+236,w-40,C_TEXT_DIM,ui_translate(dashboard && dashboard->resume?"Continue Playing":"Open Library to choose a game"));
            } else if(i<=recent) {
                const struct ui_app *app=dashboard && dashboard->recent?dashboard->recent+i-1:NULL;
                if(x+w<388 || x>W-UI_SAFE_X)continue;
                cv_clip(&cv,SI(388),SI(176),SI(W-UI_SAFE_X-388),SI(278));
                dashboard_art(app,x,y,w,232);
                rrect(x,y+232,w,26,0,HEX(0x20272A));
                text_fit(FONT_REGULAR,16,x+8,y+251,w-16,C_TEXT,items[i].title);
                cv_unclip(&cv);
            } else {
                int shortcut=i-recent-1;
                static const uint32_t colors[]={0x263431,0x28372E,0x26302F,0x292F34};
                rrect(x,y,w,h,6,HEX(colors[shortcut]));
                cv_rrect_outline(&cv,S(x),S(y),S(w),S(h),S(6),S(1),WITH_ALPHA(C_BORDER,100));
                dashboard_shortcut_icon(shortcut,x+22,y+24);
                text_fit(FONT_BOLD,24,x+78,y+51,w-100,C_TEXT,ui_translate(items[i].title));

            }
        }
        if(!recent) {
            text(FONT_BOLD,27,400,279,C_TEXT,ui_translate("Your next game starts here"));
            text(FONT_REGULAR,19,400,314,C_TEXT_DIM,ui_translate("Open Library. Your recent games will appear here."));
        }
        footer(hints,5,NULL);
        main_cache_key=key;main_cache_gpu=ui_draw_enabled();
        if(ui_draw_enabled())main_cache_valid=ui_scene_store();
        else if(main_cache.px){cv_copy(&main_cache,&cv);main_cache_valid=true;}
        else main_cache_valid=false;
    }
    if(visible>0) {
        float x,y,w,h;dashboard_rect(sel,recent,&x,&y,&w,&h);
        if(sel>0 && sel<=recent)cv_clip(&cv,SI(388),SI(176),SI(W-UI_SAFE_X-388),SI(278));
        cv_rrect_outline(&cv,S(x-4),S(y-4),S(w+8),S(h+8),S(6),S(UI_FOCUS_BORDER),C_ACCENT);
        ui_focus_mark(UI_HOME,sel);
        if(sel>0 && sel<=recent && dashboard && dashboard->recent)
            launch_source_tile(dashboard->recent+sel-1,x,y,w,232);
        else if((!sel || sel==recent+3) && dashboard && dashboard->resume)
            launch_source_tile(dashboard->resume,84,206,108,144);
        cv_unclip(&cv);
        if(sel>recent && items[sel].subtitle) {
            text_fit(FONT_REGULAR,17,x+78,y+83,w-100,C_TEXT_DIM,ui_translate(items[sel].subtitle));
        }
    }
}
void ui_draw_main(const struct ui_host *host,const struct ui_menu_item *items,int n,int sel,
                  const char *console_ip,uint32_t t_ms)
{
    (void)t_ms;ui_draw_dashboard(host,items,n,sel,console_ip,NULL);
}

void ui_draw_hosts(const struct ui_setting *rows,int n,int sel,int page,bool searching)
{
    ui_set_page(page);begin_frame();header(NULL);tabs();
    breadcrumb(NULL,ui_translate("Hosts"));
    int first=sel>=6?sel-5:0;
    for(int i=first;i<n && i<first+6;i++) {
        float y=196+(float)(i-first)*62;
        card(64,y,W-128,54,0,i==sel);
        text_fit(FONT_BOLD,21,86,y+24,510,C_TEXT,i>=n-2?ui_translate(rows[i].label):rows[i].label);
        text_fit(FONT_REGULAR,16,86,y+45,510,C_TEXT_DIM,i>=n-2?ui_translate(rows[i].help):rows[i].help);
        text_fit(FONT_REGULAR,18,650,y+33,W-736,C_TEXT_DIM,ui_translate(rows[i].value));
    }
    text(FONT_REGULAR,16,64,615,C_TEXT_FAINT,ui_translate(searching?"Searching network...":"Saved hosts available. Y searches again."));
    static const struct hint hints[]={{"A","Select"},{"X","Details"},{"Y","Search"},{"B","Back"}};
    footer(hints,4,NULL);
}

static uint32_t name_color(const char *name)
{
    uint32_t h = 2166136261u;
    static const uint32_t palette[] = { 0x385547, 0x364D60, 0x4A5A42, 0x4A464F, 0x315952, 0x4F515B };
    while (*name)
        h = (h ^ (unsigned char)*name++) * 16777619u;
    return HEX(palette[h % 6]);
}

/* Placeholder art: vertical gradient in a colour derived from the name. */
static void placeholder_art(const char *name,float x,float y,float w,float h)
{
    uint32_t base = name_color(name);
    uint32_t top = cv_mix(base, HEX(0xFFFFFF), 40), bottom = cv_mix(base, HEX(0x000000), 110);
    cv_vgradient(&cv,SI(x),SI(y),SI(w),SI(h),top,bottom);
    icon(ICON_PLAY,x+w/2,y+h*.32f,18,WITH_ALPHA(C_TEXT,160));
    /* One shared fallback, including small Continue cards. Title uses the same
     * truncation as real artwork captions; no status claim while work is pending. */
    text_fit(FONT_BOLD,UI_TEXT_SECONDARY,x+14,y+h*.62f,w-28,C_TEXT,name);
    cv_rrect_outline(&cv,S(x),S(y),S(w),S(h),0,S(1),WITH_ALPHA(C_BORDER,130));
}

static void app_tile(const struct ui_app *a,float x,float y,bool on,int item)
{
    float w=UI_RAIL_CARD_WIDTH,h=UI_RAIL_CARD_HEIGHT;
    if(on)launch_source_tile(a,x,y,w,h);
    if(a->art) {
        ui_picture_region(&cv,a->art,SI(TILE_W),SI(TILE_H),0,0,SI(TILE_W),SI(TILE_H),S(x),S(y),S(w),S(h),255,a->generation);
        ui_cover_fade_mark();
    } else placeholder_art(a->name,x,y,w,h);
    if(on) {
        cv_rrect_outline(&cv,S(x-5),S(y-5),S(w+10),S(h+10),S(UI_CORNER),S(UI_FOCUS_BORDER),C_ACCENT);
        ui_focus_mark(UI_LIBRARY,item);
    }
    if(a->favorite){rrect(x+w-52,y+10,42,24,4,C_YELLOW);text_center(FONT_BOLD,12,x+w-31,y+27,C_BG_BOTTOM,ui_translate("FAV"));}
    if(a->running){rrect(x+10,y+10,82,24,4,C_GREEN);text_center(FONT_BOLD,12,x+51,y+27,C_BG_BOTTOM,ui_translate("RUNNING"));}
}
static struct {float current,target,origin;uint32_t now,started;uint64_t context;bool active,known;} library_scroll;
static void reset_scroll(void){memset(&home_scroll,0,sizeof(home_scroll));memset(&library_scroll,0,sizeof(library_scroll));}
bool ui_library_tick(uint32_t now)
{
    library_scroll.now=now;if(!library_scroll.active)return false;
    unsigned elapsed=now-library_scroll.started;float t=elapsed/(float)UI_SCROLL_MS;if(t>1 || !ui_motion_enabled())t=1;
    float remaining=1-t;library_scroll.current=library_scroll.origin+(library_scroll.target-library_scroll.origin)*(1-remaining*remaining*remaining);
    if(t>=1){library_scroll.current=library_scroll.target;library_scroll.active=false;}
    return true;
}
void ui_draw_library(const struct ui_host *host,const struct ui_app *apps,int n,int sel,const char *filter)
{
    static const struct hint hints[]={{"A","Play"},{"B","Back"},{"X","Details"},{"Start","Menu"},{"Y","Refresh"}};
    ui_set_page(UI_LIBRARY);begin_frame();header(host);tabs();
    char count[32];snprintf(count,sizeof(count),ui_translate(n==1?"1 app":"%d apps"),n);
    text(FONT_BOLD,UI_TEXT_TITLE,UI_SAFE_X,168,C_TEXT,ui_translate(filter?filter:"All Apps"));
    text_right(FONT_REGULAR,UI_TEXT_META,W-UI_SAFE_X,168,C_TEXT_DIM,count);
    if(n<=0){text(FONT_REGULAR,UI_TEXT_PRIMARY,UI_SAFE_X,330,C_TEXT_DIM,ui_translate("No apps here. Use Start to change view."));footer(hints+1,4,NULL);return;}
    if(sel<0 || sel>=n)sel=0;
    int first=sel>=UI_RAIL_VISIBLE?sel-UI_RAIL_VISIBLE+1:0;
    uint64_t context=main_hash_text(main_hash_text(14695981039346656037ULL,filter),host?host->name:NULL)^(unsigned)n;
    float target=(float)first*(float)UI_RAIL_STEP;
    if(!library_scroll.known || library_scroll.context!=context) {
        library_scroll.current=library_scroll.target=target;library_scroll.active=false;library_scroll.known=true;library_scroll.context=context;
    } else if(target!=library_scroll.target) {
        library_scroll.origin=library_scroll.current;library_scroll.target=target;library_scroll.started=library_scroll.now;
        library_scroll.active=ui_motion_enabled();if(!library_scroll.active)library_scroll.current=target;
    }
    /* Draw just the clipped visible rail and one overscan card either side.
     * All cards share one position; no per-card animation or allocation. */
    float top=212;cv_clip(&cv,SI(UI_SAFE_X-8),SI(top-10),cv.w-SI(UI_SAFE_X)-SI(UI_SAFE_X-8),SI(UI_RAIL_CARD_HEIGHT+66));
    int begin=(int)(library_scroll.current/UI_RAIL_STEP)-1;if(begin<0)begin=0;
    int end=begin+UI_RAIL_VISIBLE+3;if(end>n)end=n;
    for(int i=begin;i<end;i++) {
        float x=UI_SAFE_X+(float)i*UI_RAIL_STEP-library_scroll.current;
        app_tile(apps+i,x,top,i==sel,i);
        text_fit(i==sel?FONT_BOLD:FONT_REGULAR,i==sel?(float)UI_TEXT_PRIMARY:20.0f,x,top+UI_RAIL_CARD_HEIGHT+31,UI_RAIL_CARD_WIDTH,i==sel?C_TEXT:C_TEXT_DIM,apps[i].name);
    }
    cv_unclip(&cv);
    card(UI_SAFE_X,568,W-2*UI_SAFE_X,60,UI_CORNER,false);
    text_fit(FONT_BOLD,UI_TEXT_PRIMARY,UI_SAFE_X+20,606,W-2*UI_SAFE_X-150,C_TEXT,apps[sel].name);
    char position[40];snprintf(position,sizeof(position),"%d / %d",sel+1,n);text_right(FONT_REGULAR,UI_TEXT_META,W-UI_SAFE_X-20,606,C_TEXT_DIM,position);
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

static int option_kind(const char *title,const struct ui_setting *row,int index,bool adjustable)
{
    if(!adjustable || !strcmp(title,"Settings") || !strcmp(title,"Developer / Advanced"))return UI_OPTION_OPEN;
    if(!strcmp(title,"Host details"))return index==0?UI_OPTION_ACTION:index==3?UI_OPTION_OPEN:UI_OPTION_INFO;
    if(!strcmp(title,"Host actions") || !strcmp(title,"Delete profile?") || !strcmp(title,"Rename profile"))return UI_OPTION_ACTION;
    if(!strcmp(title,"Library menu"))return index==0?UI_OPTION_CYCLE:UI_OPTION_OPEN;
    if(!strcmp(title,"Stream Profile"))return UI_OPTION_ACTION;
    if(!strcmp(row->label,"Saved profile") || !strcmp(row->label,"Factory profile"))return UI_OPTION_ACTION;
    if(!strcmp(row->label,"Name"))return UI_OPTION_INFO;
    if(!strcmp(row->value,"On") || !strcmp(row->value,"Off"))return UI_OPTION_TOGGLE;
    if(!strcmp(row->value,"Open") || !strcmp(row->value,"Manage") || !strcmp(row->value,"Press A"))return UI_OPTION_OPEN;
    return UI_OPTION_CYCLE;
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
        float value_width=410.0f;
        float value_x=x+w-value_width-30;
        text_fit(FONT_REGULAR,UI_TEXT_PRIMARY,x+30,ry+35,value_x-x-50,on?C_TEXT:C_TEXT_DIM,(!strcmp(title,"Stream Profile") || !strcmp(title,"Game stream profile"))?rows[i].label:ui_translate(rows[i].label));
        if (kind==UI_OPTION_CYCLE) {
            float vw = text_w(FONT_BOLD, UI_TEXT_PRIMARY, display_value);
            if(vw>value_width-40)vw=value_width-40;
            float vx = x + w - 52 - vw;
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
                bool enabled=!strcmp(rows[i].value,"On");
                rrect(x+w-82,ry+17,48,24,12,enabled?C_ACCENT:C_BORDER);
                cv_circle(&cv,S(x+w-(enabled?46:70)),S(ry+29),S(8),enabled?C_BG_BOTTOM:C_TEXT);
                text_fit(FONT_REGULAR,18,value_x,ry+34,value_width-80,C_TEXT_DIM,literal_value?value:ui_translate(value));
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
        kind==UI_OPTION_TOGGLE?(sel>=0 && sel<n && !strcmp(rows[sel].value,"On")?"Turn off":"Turn on"):
        (sel>=0 && sel<n?rows[sel].label:"Run action");
    struct hint hints[]={{"A",action},{"Left / Right","Change"},{"B","Back"}};
    if(!strcmp(title,"Appearance and display")) {
        struct hint system_hints[]={{"A",action},{"X","Appearance"},{"B","Back"}};
        footer(system_hints,3,NULL);
    }
    else if(!strcmp(title,"History and performance")) {
        struct hint overlay_hints[]={{"A",action},{"X","Streaming interface"},{"B","Back"}};footer(overlay_hints,3,NULL);
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
    cv_unclip(&cv);cv_fill(&cv,0,0,cv.w,cv.h,ARGB(170,0,0,0));
    card(280,246,720,230,UI_CORNER,false);
    text(FONT_BOLD,28,312,294,C_TEXT,ui_translate(title));
    wrapped(FONT_REGULAR,21,312,340,656,28,C_TEXT_DIM,message);
    rrect(312,405,184,48,4,C_RED);float width;keycap(328,436,"A",&width);
    text(FONT_BOLD,20,366,436,C_TEXT,ui_translate(confirm));
    keycap(542,436,"B",&width);text(FONT_REGULAR,20,580,436,C_TEXT,ui_translate("Cancel"));
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
    for(int i=0;i<6;i++) {float y=191+(float)i*58;
        rrect(64,y,W-128,50,2,C_CARD);
        text(FONT_REGULAR,21,84,y+33,C_TEXT_DIM,ui_translate(labels[i]));
        text_fit(FONT_BOLD,22,516,y+33,W-700,C_TEXT,ui_translate(values[i]));
    }
    if(note)wrapped(FONT_REGULAR,17,64,568,W-128,23,C_TEXT_DIM,ui_translate(note));
    static const struct hint hints[]={{"A","Apply safe profile"},{"X","Run analysis / details"},{"B","Back"}};
    footer(can_apply?hints:hints+1,can_apply?3:2,NULL);
}
void ui_draw_advisor(const struct ui_stat *rows,int n,const char *note,bool can_apply)
{
    ui_draw_performance(n?rows[0].value:"Not measured",n>3?rows[3].value:"Standard",-1,
        "See measured details","See measured details",n>1?rows[1].value:"Unknown",note,can_apply);
}
void ui_draw_app_details(const struct ui_host *host,const struct ui_app *app,const char *profile,const char *coverage,int selected)
{
    ui_set_page(UI_LIBRARY);begin_frame();header(host);tabs();
    breadcrumb(NULL,app->name);
    app_tile(app,284,186,false,0);launch_source_tile(app,284,186,UI_RAIL_CARD_WIDTH,UI_RAIL_CARD_HEIGHT);
    text_fit(FONT_BOLD,32,500,207,W-564,C_TEXT,app->name);
    text_fit(FONT_REGULAR,19,500,244,W-564,C_TEXT_DIM,host && host->name?host->name:ui_translate("Your PC"));
    text_fit(FONT_REGULAR,19,500,277,W-564,C_TEXT_DIM,ui_translate(profile));
    const char *options[]={"Play","Stream Profile",app->favorite?"Remove favorite":"Add favorite","Performance history","Stop app","Launch coverage"};
    const char *actions[]={"Start now",profile,app->favorite?"Remove":"Add","Open history  >",app->running?"Stop on PC":"Not running",coverage};
    for(int i=0;i<6;i++) {
        float y=290+(float)i*49;bool enabled=i!=4 || app->running;
        card(500,y,W-564,50,2,i==selected);
        text_fit(FONT_BOLD,21,524,y+33,260,!enabled?C_TEXT_FAINT:i==selected?C_TEXT:C_TEXT_DIM,ui_translate(options[i]));
        text_fit(FONT_REGULAR,18,796,y+32,W-880,!enabled?C_TEXT_FAINT:i==4?C_RED:C_TEXT_DIM,(i==1 || i==5)?actions[i]:ui_translate(actions[i]));
    }
    const char *action=selected==0?"Play now":selected==1?"Open stream settings":selected==2?(app->favorite?"Remove favorite":"Add favorite"):
        selected==3?"Open history":selected==5?"Open settings  >":app->running?"Stop app":"Not running";
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
    dialog(dw, dh, &dx, &dy);
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
    launch_profile[0]=launch_mode[0]=0;
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
        int size=i==0?28:19,face=i==0?FONT_BOLD:FONT_REGULAR;
        int tw=font_text_width(face,(float)size,labels[i]);if(tw>480)tw=480;
        font_draw_fit(&panel,face,(float)size,(width-tw)/2,i==0?34:i==1?66:88,480,i==0?C_TEXT:C_TEXT_DIM,labels[i]);
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
