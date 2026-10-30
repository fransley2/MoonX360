/* Bounded presentation state and session-only shader A/B policy. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
/* Optional diagnostic file. 0=Auto; pipeline 1=cached/2=legacy;
 * shader 1=FAST/2=QUALITY. Comments never select an override. */
static void gpu_options_parse(const char *text,int *pipeline,int *shader)
{
    while (text && *text) {
        char line[128],value[32];unsigned n=0;
        while (*text && *text!='\n') { if(n+1<sizeof(line))line[n++]=*text;text++; }
        if(*text=='\n')text++;
        line[n]=0;
        if(sscanf(line," pipeline = %31s",value)==1) {
            if(!strcmp(value,"auto"))*pipeline=0;
            else if(!strcmp(value,"cpu_cached_texture"))*pipeline=1;
            else if(!strcmp(value,"legacy"))*pipeline=2;
        } else if(sscanf(line," shader = %31s",value)==1) {
            if(!strcmp(value,"auto"))*shader=0;
            else if(!strcmp(value,"fast"))*shader=1;
            else if(!strcmp(value,"quality"))*shader=2;
        }
    }
}
enum gpu_slot_state { GPU_FREE, GPU_WRITING, GPU_READY, GPU_IN_FLIGHT };
struct gpu_slot { int state; uint32_t serial, fence; };
static int gpu_slot_write(struct gpu_slot slots[3])
{
    int i;
    for (i=0;i<3;i++) if (slots[i].state==GPU_FREE) { slots[i].state=GPU_WRITING; return i; }
    return -1;
}
static int gpu_slot_latest(struct gpu_slot slots[3], unsigned *discarded)
{
    int i, newest=-1;
    *discarded=0;
    for (i=0;i<3;i++) if (slots[i].state==GPU_READY &&
        (newest<0 || (int32_t)(slots[i].serial-slots[newest].serial)>0)) newest=i;
    for (i=0;i<3;i++) if (slots[i].state==GPU_READY && i!=newest) {
        slots[i].state=GPU_FREE; (*discarded)++;
    }
    if (newest>=0) { slots[newest].state=GPU_IN_FLIGHT; slots[newest].fence=0; }
    return newest;
}
#define GPU_TIME_SAMPLES 256
struct gpu_time_window { uint32_t us[GPU_TIME_SAMPLES]; unsigned count, next; };
static void gpu_time_add(struct gpu_time_window *w, uint32_t us)
{
    w->us[w->next]=us;w->next=(w->next+1)%GPU_TIME_SAMPLES;
    if (w->count<GPU_TIME_SAMPLES) w->count++;
}
static int gpu_time_compare(const void *a,const void *b)
{
    uint32_t x=*(const uint32_t*)a,y=*(const uint32_t*)b;
    return x>y?1:x<y?-1:0;
}
static uint32_t gpu_time_percentile(const struct gpu_time_window *w, unsigned percentile)
{
    uint32_t copy[GPU_TIME_SAMPLES];unsigned rank;
    if (!w->count) return 0;
    memcpy(copy,w->us,w->count*sizeof(copy[0]));
    qsort(copy,w->count,sizeof(copy[0]),gpu_time_compare);
    rank=(w->count*percentile+99)/100;
    return copy[(rank?rank:1)-1];
}
struct gpu_quality_policy {
    struct gpu_time_window window;
    unsigned bad_windows, phase;
    uint32_t quality_p99, fast_p99;
    bool fast, tested;
};
/* UI choices always win over legacy diagnostic files unless the file explicitly
 * opts in. Pipeline overrides remain independent of image quality. */
static bool gpu_options_resolve(const char *text,int selected,int *pipeline,int *shader)
{
    bool allow=false;
    int diagnostic=selected>=0 && selected<=2?selected:0;
    gpu_options_parse(text,pipeline,&diagnostic);
    for(const char *p=text;p && *p;) {
        char line[128];unsigned n=0;int value;
        while(*p && *p!='\n'){if(n+1<sizeof(line))line[n++]=*p;p++;}
        if(*p=='\n')p++;line[n]=0;
        if(sscanf(line," allow_shader_override = %d",&value)==1)allow=value==1;
    }
    *shader=allow?diagnostic:selected>=0 && selected<=2?selected:0;
    return allow;
}
/* Shared by rendering, HUD and reports: Fast Decode is deliberately not an
 * input. It controls the H.264 loop filter, not the image shader. */
static bool gpu_shader_fast(int requested,bool available,const struct gpu_quality_policy *policy)
{
    return requested==1 || !available || (requested==0 && policy->fast);
}
/* This is CPU command-submission wall time, NOT a GPU timestamp. Two slow
 * QUALITY windows start a FAST trial; an A/B/A confirmation is required to keep FAST.
 * Present/VSync and upload/lock are outside the sample. No persistent change. */
static void gpu_quality_sample(struct gpu_quality_policy *p, uint32_t submit_us)
{
    uint32_t tail;
    if (p->tested) return;
    gpu_time_add(&p->window,submit_us);
    if (p->window.count<GPU_TIME_SAMPLES) return;
    tail=gpu_time_percentile(&p->window,99);
    memset(&p->window,0,sizeof(p->window));
    if (!p->phase) {
        if (tail>2500) p->bad_windows++; else p->bad_windows=0;
        if (p->bad_windows>=2) { p->quality_p99=tail;p->phase=1;p->fast=true; }
    } else if (p->phase==1) {
        p->fast_p99=tail;
        bool better=(uint64_t)tail+250<p->quality_p99 && (uint64_t)tail*100<(uint64_t)p->quality_p99*80;
        p->fast=false;
        if (better) p->phase=2; /* return to QUALITY to confirm the A/B result */
        else p->tested=true;
    } else {
        p->fast=(uint64_t)p->fast_p99+250<tail && (uint64_t)p->fast_p99*100<(uint64_t)tail*80;
        p->tested=true;
    }
}
