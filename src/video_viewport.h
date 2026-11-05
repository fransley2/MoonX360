#pragma once

/* Fit a source image inside the actual backbuffer without changing its aspect. */
static void video_fit_rect(int source_w,int source_h,int output_w,int output_h,
                           int *left,int *top,int *right,int *bottom)
{
    int w=output_w,h=output_h;
    if(source_w<=0 || source_h<=0 || output_w<=0 || output_h<=0){
        *left=*top=0;*right=output_w;*bottom=output_h;return;
    }
    if((long long)source_w*output_h>(long long)source_h*output_w)
        h=(int)((long long)output_w*source_h/source_w);
    else
        w=(int)((long long)output_h*source_w/source_h);
    *left=(output_w-w)/2;*top=(output_h-h)/2;
    *right=*left+w;*bottom=*top+h;
}
