#pragma once
#include "config.h"
#include "display_policy.h"
#include "video_viewport.h"

static const int ui_layout_widths[UI_LAYOUT_COUNT]={1280,960,1280,720,900,1080,1152,1680};

static __inline const char *ui_layout_name(int layout)
{
    static const char *names[UI_LAYOUT_COUNT]={"Auto","4:3","16:9","1:1","5:4","3:2","16:10","21:9"};
    return names[layout>=0 && layout<UI_LAYOUT_COUNT?layout:UI_LAYOUT_AUTO];
}

/* Compare display aspects at a common height, accounting for CRT super resolutions. */
static __inline int ui_layout_resolve(int layout,int source_w,int source_h)
{
    if(layout>UI_LAYOUT_AUTO && layout<UI_LAYOUT_COUNT)return layout;
    if(source_w<=0 || source_h<=0)return UI_LAYOUT_16_9;
    int num,den;
    crt_source_dar(source_w,source_h,&num,&den);
    long long width=(720LL*num+den/2)/den;
    int best=UI_LAYOUT_16_9;
    for(int candidate=UI_LAYOUT_4_3;candidate<UI_LAYOUT_COUNT;candidate++) {
        long long distance=width-ui_layout_widths[candidate];
        long long best_distance=width-ui_layout_widths[best];
        if(distance<0)distance=-distance;
        if(best_distance<0)best_distance=-best_distance;
        if(distance<best_distance)best=candidate;
    }
    return best;
}

/* Auto UI layout follows the effective Xbox display configuration, never stream size. */
static __inline int ui_layout_resolve_display(int layout,int display_mode,int physical_w,int physical_h,
                                              bool widescreen,int backbuffer_w,int backbuffer_h)
{
    if(layout>UI_LAYOUT_AUTO && layout<UI_LAYOUT_COUNT)return layout;
    if(display_mode==4)return UI_LAYOUT_4_3;
    if(display_mode>=1 && display_mode<=3)return UI_LAYOUT_16_9;
    if(display_output_is_5_4(physical_w,physical_h,widescreen))return UI_LAYOUT_5_4;
    if(display_output_is_4_3(physical_w,physical_h,widescreen))return UI_LAYOUT_4_3;
    if(display_output_aspect_is_narrow_4_3(physical_w,physical_h,widescreen))return UI_LAYOUT_4_3;
    if(backbuffer_w==640 && (backbuffer_h==480 || backbuffer_h==240))return UI_LAYOUT_4_3;
    return UI_LAYOUT_16_9;
}

static __inline void ui_layout_canvas_size(int layout,int *width,int *height)
{
    *width=ui_layout_widths[layout>=0 && layout<UI_LAYOUT_COUNT?layout:UI_LAYOUT_AUTO];
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
