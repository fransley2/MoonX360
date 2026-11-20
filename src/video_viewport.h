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

/* Fit a stream inside CRT output without ever stretching widescreen into 4:3.
 * crt_aspect: 0 constrains to 4:3 frame, 1 preserves full output frame; neither stretches src aspect.
 * retro_240p_bb: non-zero when output_w x output_h is 640x240 non-square physical 4:3 CRT scanout. */
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
