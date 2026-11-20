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
    if(output_w==640 && output_h==240 && canvas_w>0 && canvas_h>0){
        /* 640x240 CRT scanout has 2:1 non-square pixels representing physical 4:3. */
        int w=output_w,h=output_h;
        if((long long)canvas_w*3>(long long)canvas_h*4)
            h=(int)(((long long)output_h*4*canvas_h+(3*canvas_w)/2)/(3*canvas_w));
        else if((long long)canvas_w*3<(long long)canvas_h*4)
            w=(int)(((long long)output_w*3*canvas_w+(4*canvas_h)/2)/(4*canvas_h));
        if(w<1)w=1;
        if(h<1)h=1;
        *left=(output_w-w)/2;*top=(output_h-h)/2;
        *right=*left+w;*bottom=*top+h;
        return;
    }
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
