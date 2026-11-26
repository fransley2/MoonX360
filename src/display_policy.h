#pragma once
#include <stdbool.h>

/* SD video may report 720x480 for either aspect. VGA can report native 4:3. */
static __inline bool display_output_is_4_3(int width,int height,bool widescreen)
{
    return width>0 && height>0 && !widescreen && (height<=576 || width*3==height*4);
}
static __inline bool display_output_is_5_4(int width,int height,bool widescreen)
{
    return width>0 && height>0 && !widescreen && (long long)width*4==(long long)height*5;
}
static __inline bool display_output_aspect_is_narrow_4_3(int width,int height,bool widescreen)
{
    return width>0 && height>0 && !widescreen && (long long)width*3<=(long long)height*4;
}
static void display_render_size(int choice,int physical_w,int physical_h,int *w,int *h)
{
    if(choice==4){*w=640;*h=480;}
    else if(choice==1){*w=848;*h=480;}
    else if(choice==2){*w=1280;*h=720;}
    else if(choice==3){*w=1920;*h=1080;}
    else if(physical_h<=576 && physical_h>0){*w=848;*h=480;}
    else if(physical_w==1920 && physical_h==1080){*w=1920;*h=1080;}
    else {*w=1280;*h=720;}
}
static int display_rate_x100(float hz)
{
    return hz>=20.0f && hz<=240.0f?(int)(hz*100.0f+0.5f):6000;
}
