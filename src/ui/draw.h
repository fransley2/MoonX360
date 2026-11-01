#pragma once
#include "canvas.h"
#define UI_DRAW_CAPACITY 8192
enum ui_draw_kind { UI_RECT,UI_GRADIENT,UI_OUTLINE,UI_LINE,UI_TRIANGLE,UI_IMAGE,UI_GLYPH };
struct ui_draw_command {
    int kind, clip[4];
    float x,y,w,h,r,t,x2,y2;
    uint32_t color,bottom,generation;
    const void *pixels;
    int iw,ih,stride,sx,sy,sw,sh;
};
struct ui_memory_info {
    uint32_t gpu_bytes,glyph_bytes,cover_gpu_bytes,button_gpu_bytes,command_bytes;
    uint32_t cover_cpu_bytes,loaded_covers,button_cpu_bytes,canvas_bytes;
    uint32_t gpu_covers,commands,software_fallbacks;
};
void ui_draw_enable(bool enabled);
bool ui_draw_enabled(void);
bool ui_draw_recording(void);
void ui_draw_begin(canvas_t *canvas);
void ui_draw_suspend(void);
/* One retained focus outline; bounded 120 ms settle, no new commands/textures. */
void ui_focus_mark(int context,int item);
bool ui_focus_tick(uint32_t now);
void ui_set_motion(bool enabled);
void ui_focus_presented(void);
void ui_focus_finish(void);
/* Call immediately after a cover image. Retained alpha edits only, bounded. */
void ui_cover_fade_mark(void);
/* Bounded retained menu scene. Glyph/cover owners invalidate before releasing data. */
bool ui_scene_store(void);
bool ui_scene_restore(canvas_t *canvas);
void ui_scene_drop(void);
const struct ui_draw_command *ui_draw_commands(int *count);
void ui_draw_software(void);
void ui_draw_gpu_memory(uint32_t glyph,uint32_t covers,uint32_t buttons,unsigned count);
void ui_draw_cover_memory(unsigned count,unsigned bytes);
void ui_draw_memory(struct ui_memory_info *out);
void ui_fill(canvas_t*,int,int,int,int,uint32_t);
void ui_gradient(canvas_t*,int,int,int,int,uint32_t,uint32_t);
void ui_round(canvas_t*,float,float,float,float,float,uint32_t);
void ui_outline(canvas_t*,float,float,float,float,float,float,uint32_t);
void ui_shadow(canvas_t*,float,float,float,float,float,float,uint32_t);
void ui_circle(canvas_t*,float,float,float,uint32_t);
void ui_ring(canvas_t*,float,float,float,float,uint32_t);
void ui_line(canvas_t*,float,float,float,float,float,uint32_t);
void ui_triangle(canvas_t*,float,float,float,float,float,float,uint32_t);
void ui_picture(canvas_t*,const uint32_t*,int,int,int,int,float,unsigned);
void ui_picture_region(canvas_t*,const uint32_t*,int,int,int,int,int,int,float,float,float,float,unsigned,uint32_t);
void ui_glyph(canvas_t*,int,int,const uint8_t*,int,int,int,uint32_t);
/* Single original button atlas; generated locally using the embedded OFL font. */
bool ui_button(canvas_t*,const char*,int,int,int,int);

bool ui_focus_bounds(float *x,float *y,float *w,float *h);

void ui_draw_hide_image(const void *pixels,float x,float y,float w,float h);

bool ui_motion_enabled(void);
