#pragma once
#include "video_viewport.h"

/* Canvas pixels stay square at either aspect. The backbuffer may have another
 * aspect, so fit the complete interface without stretching its text or art. */
static void ui_layout_canvas_size(int layout,int *width,int *height)
{
    *width=layout==1?960:1280;
    *height=720;
}
static void ui_layout_viewport(int canvas_w,int canvas_h,int output_w,int output_h,
                               int *left,int *top,int *right,int *bottom)
{
    video_fit_rect(canvas_w,canvas_h,output_w,output_h,left,top,right,bottom);
}
/* UI projection only. Gameplay retains video_fit_rect() without overscan crop. */
static void ui_layout_safe_viewport(int cw,int ch,int ow,int oh,int percent,int *l,int *t,int *r,int *b)
{
    ui_layout_viewport(cw,ch,ow,oh,l,t,r,b);
    if(percent<0 || percent>10)percent=0;
    int dx=(*r-*l)*percent/100,dy=(*b-*t)*percent/100;
    *l+=dx;*r-=dx;*t+=dy;*b-=dy;
}
