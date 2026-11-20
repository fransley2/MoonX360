#include <assert.h>
#include <stdio.h>
#include "../../src/video_gpu_policy.h"
static void window(gpu_quality_policy *p,unsigned value) {
    for(int i=0;i<GPU_TIME_SAMPLES;i++)gpu_quality_sample(p,value);
}
int main() {
    int pipeline=0,shader=0;
    gpu_options_parse("# pipeline=legacy\n; shader=fast\n pipeline = cpu_cached_texture\r\n shader = quality\n",&pipeline,&shader);
    assert(pipeline==1 && shader==2);
    gpu_options_parse("pipeline=legacy\nshader=fast\npipeline=auto\nshader=auto\n",&pipeline,&shader);
    assert(!pipeline && !shader);
    // Menu requests (including Auto) win over an old diagnostic shader setting.
    for(int selected=0;selected<3;selected++)for(int legacy=0;legacy<3;legacy++){
        char file[160];snprintf(file,sizeof(file),"pipeline=legacy\nshader=%s\n",legacy==0?"auto":legacy==1?"fast":"quality");
        pipeline=0;assert(!gpu_options_resolve(file,selected,&pipeline,&shader));assert(pipeline==2 && shader==selected);
        strcat(file,"allow_shader_override=1\n");assert(gpu_options_resolve(file,selected,&pipeline,&shader) && shader==legacy);
        strcat(file,"allow_shader_override=0\n");assert(!gpu_options_resolve(file,selected,&pipeline,&shader) && shader==selected);
    }
    pipeline=0;assert(!gpu_options_resolve(NULL,2,&pipeline,&shader) && shader==2 && !pipeline);
    assert(!gpu_options_resolve("# allow_shader_override=1\n; allow_shader_override=1\nshader=fast\n",2,&pipeline,&shader) && shader==2);
    assert(!gpu_options_resolve("allow_shader_override=garbage\nshader=fast\n",2,&pipeline,&shader) && shader==2);
    assert(!gpu_options_resolve(NULL,99,&pipeline,&shader) && shader==0);
    for(int request=0;request<3;request++)for(int available=0;available<2;available++)for(int fast=0;fast<2;fast++){
        gpu_quality_policy state={};state.fast=fast!=0;
        assert(gpu_shader_fast(request,available!=0,&state)==(request==1 || !available || (request==0 && fast)));
    }
    gpu_options_parse("pipeline=legacy_invalid\nshader=quality_invalid\n",&pipeline,&shader);
    assert(!pipeline && !shader);
    gpu_slot s[3]={{GPU_READY,0,0},{GPU_READY,UINT32_MAX-1,0},{GPU_READY,UINT32_MAX,0}};
    unsigned drops;assert(gpu_slot_latest(s,&drops)==0 && drops==2 && s[0].state==GPU_IN_FLIGHT);
    assert(gpu_slot_write(s)==1 && s[1].state==GPU_WRITING);
    assert(gpu_slot_latest(s,&drops)==-1); // no reads of WRITING or IN_FLIGHT
    gpu_time_window t={};assert(gpu_time_percentile(&t,99)==0);
    for(unsigned i=0;i<512;i++)gpu_time_add(&t,i);
    assert(t.count==256 && gpu_time_percentile(&t,95)==499 && gpu_time_percentile(&t,99)==509);
    assert(gpu_time_percentile(&t,100)==511);
    gpu_quality_policy p={};window(&p,4000);assert(!p.fast);
    window(&p,1000);assert(p.bad_windows==0 && !p.fast);
    window(&p,4000);window(&p,4000);assert(p.fast && !p.tested && p.quality_p99==4000);
    window(&p,3500);assert(!p.fast && p.tested); // not enough benefit, restore QUALITY
    window(&p,9000);assert(!p.fast); // no oscillation or persistent setting change
    p=gpu_quality_policy();window(&p,4000);window(&p,4000);window(&p,2000);
    assert(!p.fast && !p.tested && p.phase==2);
    window(&p,4000);assert(p.fast && p.tested && p.fast_p99==2000);
    p=gpu_quality_policy();window(&p,4000);window(&p,4000);window(&p,2000);window(&p,1000);
    assert(!p.fast && p.tested); // scene/load improved: A/B/A rejects a false downgrade
    p=gpu_quality_policy();for(int i=0;i<1024;i++)gpu_quality_sample(&p,i%256==0?9000:1000);
    assert(!p.fast); // isolated spikes do not dominate p99
    puts("PASS: newest selection through serial wrap; protected slot states; bounded rolling p95/p99; sustained pressure and reversible measured FAST trial.");
}
