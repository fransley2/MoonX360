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
