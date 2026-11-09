/* Experimental, bounded presentation-only state. Never contains blended pixels. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
enum smoothing_bypass {SMOOTH_NONE,SMOOTH_NO_HISTORY,SMOOTH_MOTION,SMOOTH_DIRECT};
struct smoothing_signature {uint8_t y[16],u[4],v[4];bool valid;};
struct smoothing_transition {unsigned remaining;bool direct_required;};
struct smoothing_diagnostics {
    uint32_t history_slot_pinned,history_slot_bypassed,idr_transition_started,idr_transition_applied;
    uint32_t actual_blended_present_calls,transition_bypassed_motion,transition_bypassed_invalid_history;
    uint32_t history_allocation_failed;
    uint64_t additional_gpu_history_bytes,additional_history_row_bytes;
    bool enabled,previous_frame_available,history_currently_pinned;
};
static void smoothing_signature_capture(struct smoothing_signature *s,const uint8_t *const p[3],const int pitch[3],int w,int h)
{
    s->valid=false;
    if(w<1 || h<1 || !p[0] || !p[1] || !p[2] || pitch[0]<w || pitch[1]<(w+1)/2 || pitch[2]<(w+1)/2)return;
    for(int y=0;y<4;y++)for(int x=0;x<4;x++)s->y[y*4+x]=p[0][(y*(h-1)/3)*pitch[0]+x*(w-1)/3];
    int cw=(w+1)/2,ch=(h+1)/2;
    for(int y=0;y<2;y++)for(int x=0;x<2;x++) {
        s->u[y*2+x]=p[1][y*(ch-1)*pitch[1]+x*(cw-1)];
        s->v[y*2+x]=p[2][y*(ch-1)*pitch[2]+x*(cw-1)];
    }
    s->valid=true;
}
static unsigned smoothing_difference(unsigned a,unsigned b){return a>b?a-b:b-a;}
/* A sparse scene-change/motion heuristic, NOT proof of clean reference content. */
static bool smoothing_motion_safe(const struct smoothing_signature *a,const struct smoothing_signature *b)
{
    if(!a->valid || !b->valid)return false;
    unsigned sum=0;
    for(int i=0;i<16;i++){unsigned d=smoothing_difference(a->y[i],b->y[i]);if(d>24)return false;sum+=d;}
    if(sum>64)return false;
    sum=0;for(int i=0;i<4;i++){sum+=smoothing_difference(a->u[i],b->u[i]);sum+=smoothing_difference(a->v[i],b->v[i]);}
    return sum<=32;
}
static float smoothing_select(struct smoothing_transition *t,bool idr,bool history,bool motion,int strength,int *reason,bool *started)
{
    *started=false;*reason=SMOOTH_NONE;
    /* Even an all-IDR source cannot restart a continuous crossfade forever. */
    if(t->direct_required){t->direct_required=false;*reason=SMOOTH_DIRECT;return 0;}
    if(idr && !t->remaining){t->remaining=2;*started=true;}
    if(!t->remaining)return 0;
    if(!history){t->remaining=0;*reason=SMOOTH_NO_HISTORY;return 0;}
    if(!motion){t->remaining=0;t->direct_required=true;*reason=SMOOTH_MOTION;return 0;}
    float alpha=t->remaining==2?(strength==1?0.15f:0.10f):(strength==1?0.07f:0.04f);
    if(!--t->remaining)t->direct_required=true;
    return alpha;
}
static void smoothing_constants(float alpha,float c[4])
{
    c[0]=alpha;c[1]=8.0f/255.0f;c[2]=255.0f/7.0f;c[3]=255.0f/6.0f;
}
