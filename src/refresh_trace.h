/* Bounded diagnostic metadata, never a CPU image filter or persistent pixel copy. */
#pragma once
#include <stdint.h>
#include <string.h>
#include "video_gpu_policy.h"
#define REFRESH_TRACE_FRAMES 64
struct refresh_trace_frame {
    uint32_t input,output,bytes,queue_us,decode_us;
    uint64_t start_us;
    int got,ret,flags;
    unsigned input_idr,valid,mean_y,min_y,max_y;
};
struct refresh_trace {
    struct refresh_trace_frame frame[REFRESH_TRACE_FRAMES];
    unsigned next,count,idrs,frames;
    uint64_t all_bytes,idr_bytes;
    struct gpu_time_window idr_sizes,frame_sizes;
};
static struct refresh_trace_frame *refresh_trace_begin(struct refresh_trace *t,uint32_t input,
    uint32_t bytes,bool idr,uint64_t start,uint32_t age)
{
    struct refresh_trace_frame *f=&t->frame[t->next];memset(f,0,sizeof(*f));
    t->next=(t->next+1)%REFRESH_TRACE_FRAMES;if(t->count<REFRESH_TRACE_FRAMES)t->count++;
    f->input=input;f->bytes=bytes;f->input_idr=idr;f->start_us=start;f->queue_us=age;
    t->frames++;t->all_bytes+=bytes;gpu_time_add(&t->frame_sizes,bytes);
    if(idr){t->idrs++;t->idr_bytes+=bytes;gpu_time_add(&t->idr_sizes,bytes);}return f;
}
static void refresh_trace_luma(struct refresh_trace_frame *f,const uint8_t *y,int pitch,int w,int h)
{
    unsigned sum=0;f->min_y=255;f->max_y=0;
    for(int row=0;row<8;row++)for(int col=0;col<8;col++) {
        unsigned v=y[(row*(h-1)/7)*pitch+col*(w-1)/7];sum+=v;
        if(v<f->min_y)f->min_y=v;if(v>f->max_y)f->max_y=v;
    }
    f->mean_y=sum/64;f->valid=1;
}
