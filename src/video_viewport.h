#pragma once
#include "crt_resolutions.h"

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

/* Legacy CRT v1 helper preserved for source compatibility. */
static void video_crt_fit_rect(int source_w,int source_h,int output_w,int output_h,
                               int crt_aspect,int retro_240p_bb,
                               int safe_pct,int pos_x,int pos_y,
                               int *left,int *top,int *right,int *bottom)
{
    long long disp_num,disp_den,box_dar_num,box_dar_den,src_dar_num,src_dar_den;
    int box_w,box_h,w,h,px,py;
    if(source_w<=0 || source_h<=0 || output_w<=0 || output_h<=0){
        *left=*top=0;*right=output_w;*bottom=output_h;return;
    }
    if(retro_240p_bb || (output_w==640 && output_h==240)){disp_num=4;disp_den=3;}
    else{disp_num=output_w;disp_den=output_h;}

    box_w=output_w;box_h=output_h;
    box_dar_num=disp_num;box_dar_den=disp_den;
    if(crt_aspect==0){
        if(disp_num*3>disp_den*4){
            box_w=(int)((long long)output_w*4*disp_den/(3*disp_num));
            box_dar_num=4;box_dar_den=3;
        }else if(disp_num*3<disp_den*4){
            box_h=(int)((long long)output_h*3*disp_num/(4*disp_den));
            box_dar_num=4;box_dar_den=3;
        }
    }

    if(source_w==640 && source_h==240){src_dar_num=4;src_dar_den=3;}
    else{src_dar_num=source_w;src_dar_den=source_h;}

    if(src_dar_num*box_dar_den>src_dar_den*box_dar_num){
        w=box_w;
        h=(int)(((long long)box_h*box_dar_num*src_dar_den+(box_dar_den*src_dar_num)/2)/(box_dar_den*src_dar_num));
    }else{
        h=box_h;
        w=(int)(((long long)box_w*box_dar_den*src_dar_num+(box_dar_num*src_dar_den)/2)/(box_dar_num*src_dar_den));
    }
    if(w<1)w=1;
    if(h<1)h=1;

    if(safe_pct>0){
        int sp=safe_pct>10?10:safe_pct;
        w=(w*(100-2*sp)+50)/100;
        h=(h*(100-2*sp)+50)/100;
        if(w<1)w=1;
        if(h<1)h=1;
    }
    px=pos_x<-20?-20:pos_x>20?20:pos_x;
    py=pos_y<-20?-20:pos_y>20?20:pos_y;
    *left=(output_w-w)/2+px;
    *top=(output_h-h)/2+py;
    *right=*left+w;
    *bottom=*top+h;
}

struct crt_viewport_params {
    int crt_aspect;        /* 0=Fit to screen, 1=Original(legacy fit), 2=16:9, 3=Stretch, 4=Custom, 5=Pixel-to-pixel(1:1), 6=Crop to 4:3 */
    int crt_integer_scale; /* 0=Off, 1=On */
    int retro_240p_bb;     /* non-zero for physical 240p 4:3 non-square scanout */
    int crt_overscan;      /* 0..10 percent per edge (game picture only) */
    int crt_size_x;        /* 80..110 percent (100 = default) */
    int crt_size_y;        /* 80..110 percent (100 = default) */
    int crt_pos_x;         /* -20..+20 px */
    int crt_pos_y;         /* -20..+20 px */
};

struct video_present_rect {
    int src_left, src_top, src_right, src_bottom;
    int dst_left, dst_top, dst_right, dst_bottom;
};

/*
 * Player presentation geometry (zero allocations, source-crop + destination viewport):
 * - Fit to screen (0) / Original (1): Full visible source preserving its DAR (crt_source_dar).
 * - Pixel-to-pixel 1:1 (5): Center source texels 1:1 into backbuffer without enlarging.
 *   When source_w > output_w or source_h > output_h, scales down preserving DAR without cropping.
 *   On CRT non-square pixel scanout (e.g. 640x240 2:1 PAR or 720x480 SD), 1:1 backbuffer texels
 *   preserve pixel exactness at the backbuffer's physical pixel aspect ratio rather than correcting to 4:3.
 * - Crop to 4:3 (6): For widescreen sources (DAR > 4:3, e.g. 1280x720 with embedded side bars),
 *   crops horizontally centered to [160, 1120) x [0, 720) first, then fits 4:3 to destination.
 *   Sources already <= 4:3 retain the full frame.
 */
static void video_compute_present_rect(int source_w,int source_h,int output_w,int output_h,
                                       const struct crt_viewport_params *p,
                                       struct video_present_rect *out)
{
    long long disp_num,disp_den,box_dar_num,box_dar_den;
    int s_num,s_den;
    int box_w,box_h,w,h,px,py,os,sx_pct,sy_pct;
    int aspect=p?p->crt_aspect:0;
    int integer=p?p->crt_integer_scale:0;
    int retro_bb=p?p->retro_240p_bb:0;

    if(!out)return;
    if(source_w<=0 || source_h<=0 || output_w<=0 || output_h<=0){
        out->src_left=out->src_top=0;
        out->src_right=source_w>0?source_w:0;
        out->src_bottom=source_h>0?source_h:0;
        out->dst_left=out->dst_top=0;
        out->dst_right=output_w>0?output_w:0;
        out->dst_bottom=output_h>0?output_h:0;
        return;
    }

    out->src_left=0;out->src_top=0;
    out->src_right=source_w;out->src_bottom=source_h;

    if(retro_bb || (output_h==240 && (output_w==640 || output_w==1280 || output_w==1920))){
        disp_num=4;disp_den=3;
    } else {
        disp_num=output_w;disp_den=output_h;
    }

    crt_source_dar(source_w,source_h,&s_num,&s_den);

    if(aspect==6){ /* CRT_ASPECT_CROP_4_3 */
        if((long long)s_num*3>(long long)s_den*4){
            int crop_w=(int)(((long long)source_h*4+1)/3);
            if(crop_w<1)crop_w=1;
            if(crop_w>source_w)crop_w=source_w;
            out->src_left=(source_w-crop_w)/2;
            out->src_right=out->src_left+crop_w;
            s_num=4;s_den=3;
        }
        aspect=0; /* Fit cropped 4:3 region cleanly to output display */
    }

    if(aspect==5){ /* CRT_ASPECT_PIXEL_1_1 */
        px=p?p->crt_pos_x:0;
        py=p?p->crt_pos_y:0;
        if(px<-20)px=-20;if(px>20)px=20;
        if(py<-20)py=-20;if(py>20)py=20;
        if(source_w<=output_w && source_h<=output_h){
            w=source_w;
            h=source_h;
            out->dst_left=(output_w-w)/2+px;
            out->dst_top=(output_h-h)/2+py;
            out->dst_right=out->dst_left+w;
            out->dst_bottom=out->dst_top+h;
            return;
        }
        /* Source exceeds backbuffer: never enlarge, fit full frame down preserving DAR. */
        aspect=0;
        integer=0;
    }

    if(aspect==1)aspect=0; /* CRT_ASPECT_ORIGINAL behaves as Fit to screen */

    box_w=output_w;box_h=output_h;
    box_dar_num=disp_num;box_dar_den=disp_den;

    if(aspect==2){ /* CRT_ASPECT_16_9 */
        if(disp_num*9>disp_den*16){
            box_w=(int)((long long)output_w*16*disp_den/(9*disp_num));
            box_dar_num=16;box_dar_den=9;
        }else if(disp_num*9<disp_den*16){
            box_h=(int)((long long)output_h*9*disp_num/(16*disp_den));
            box_dar_num=16;box_dar_den=9;
        }
    }

    if(aspect==3){ /* CRT_ASPECT_STRETCH */
        w=output_w;
        h=output_h;
    } else if(integer && source_w<=box_w && source_h<=box_h){
        bool non_sq_out=(disp_num*output_h!=disp_den*output_w);
        bool non_sq_src=(s_num*source_h!=s_den*source_w);
        if(non_sq_out){
            int kx=box_w/source_w;
            int ky=box_h/source_h;
            if(kx<1)kx=1;
            if(ky<1)ky=1;
            w=source_w*kx;
            h=source_h*ky;
        } else if(non_sq_src){
            /* Non-square 4:3 source (e.g. 640x240) on square-pixel output needs ky = 2*kx. */
            int kx=box_w/source_w;
            int ky=box_h/(source_h*2);
            int k=kx<ky?kx:ky;
            if(k>=1){
                w=source_w*k;
                h=source_h*(k*2);
            } else {
                w=source_w;
                h=source_h*2<=box_h?source_h*2:source_h;
            }
        } else {
            int kx=box_w/source_w;
            int ky=box_h/source_h;
            int k=kx<ky?kx:ky;
            if(k<1)k=1;
            w=source_w*k;
            h=source_h*k;
        }
    } else if(aspect==2){ /* 16:9 target frame */
        w=box_w;
        h=box_h;
    } else {
        long long src_dar_num=s_num,src_dar_den=s_den;
        if(src_dar_num*box_dar_den>src_dar_den*box_dar_num){
            w=box_w;
            h=(int)(((long long)box_h*box_dar_num*src_dar_den+(box_dar_den*src_dar_num)/2)/(box_dar_den*src_dar_num));
        }else{
            h=box_h;
            w=(int)(((long long)box_w*box_dar_den*src_dar_num+(box_dar_num*src_dar_den)/2)/(box_dar_num*src_dar_den));
        }
    }
    if(w<1)w=1;
    if(h<1)h=1;

    os=p?p->crt_overscan:0;
    if(os>0){
        if(os>10)os=10;
        w=(w*(100-2*os)+50)/100;
        h=(h*(100-2*os)+50)/100;
        if(w<1)w=1;
        if(h<1)h=1;
    }

    sx_pct=p&&p->crt_size_x>0?p->crt_size_x:100;
    sy_pct=p&&p->crt_size_y>0?p->crt_size_y:100;
    if(sx_pct<80)sx_pct=80;if(sx_pct>110)sx_pct=110;
    if(sy_pct<80)sy_pct=80;if(sy_pct>110)sy_pct=110;
    if(!integer && (sx_pct!=100 || sy_pct!=100)){
        w=(w*sx_pct+50)/100;
        h=(h*sy_pct+50)/100;
        if(w<1)w=1;
        if(h<1)h=1;
    }

    px=p?p->crt_pos_x:0;
    py=p?p->crt_pos_y:0;
    if(px<-20)px=-20;if(px>20)px=20;
    if(py<-20)py=-20;if(py>20)py=20;
    out->dst_left=(output_w-w)/2+px;
    out->dst_top=(output_h-h)/2+py;
    out->dst_right=out->dst_left+w;
    out->dst_bottom=out->dst_top+h;
}

/* CRT Retro 2.0 viewport geometry helper sharing video_compute_present_rect(). */
static void video_crt_fit_rect_v2(int source_w,int source_h,int output_w,int output_h,
                                  const struct crt_viewport_params *p,
                                  int *left,int *top,int *right,int *bottom)
{
    struct video_present_rect r;
    video_compute_present_rect(source_w,source_h,output_w,output_h,p,&r);
    *left=r.dst_left;*top=r.dst_top;*right=r.dst_right;*bottom=r.dst_bottom;
}
