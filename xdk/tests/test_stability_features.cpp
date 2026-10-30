#include "../../src/stick_calibration.h"
#include "../../src/adaptive_stream.h"
#include "../../src/display_policy.h"
#include <assert.h>
#include <stdio.h>
int main(){
    for(int v=-32768;v<=32767;v++)assert(stick_calibrate((int16_t)v,0,100)==v);
    int prev=-32768;
    for(int v=-32768;v<=32767;v++){int next=stick_calibrate((int16_t)v,12,88);assert(next>=prev);prev=next;}
    assert(stick_calibrate(2000,12,88)==0 && stick_calibrate(-2000,12,88)==0);
    assert(stick_calibrate(30000,12,88)==32767 && stick_calibrate(-30000,12,88)==-32768);
    assert(stick_calibrate(6000,0,100)!=stick_calibrate(6000,25,100));
    adaptive_stream a={};
    for(int i=0;i<15;i++)assert(!adaptive_stream_sample(&a,i*1000,true,5,20000,60,false));
    for(int i=15;i<22;i++)assert(!adaptive_stream_sample(&a,i*1000,false,5,20000,60,false));
    assert(adaptive_stream_sample(&a,22000,false,5,20000,60,false)==2);
    assert(!adaptive_stream_sample(&a,23000,true,5,20000,60,true)); // Guide suppresses adaptation
    for(int i=24;i<31;i++)assert(!adaptive_stream_sample(&a,i*1000,true,0,6000,60,false));
    assert(adaptive_stream_sample(&a,31000,true,0,6000,60,false)==1);
    assert(!adaptive_stream_sample(&a,32000,false,0,6000,60,false));
    int w=1920,h=1080,b=20000;assert(adaptive_stream_lower(1,&w,&h,&b));assert(w==1920 && h==1080 && b==15000);
    assert(adaptive_stream_lower(2,&w,&h,&b));assert(w==1280 && h==720);
    assert(adaptive_stream_lower(2,&w,&h,&b));assert(w==854 && h==480 && b==8000);
    b=6000;assert(!adaptive_stream_lower(1,&w,&h,&b));
    display_render_size(0,720,480,&w,&h);assert(w==848 && h==480);
    display_render_size(0,1920,1080,&w,&h);assert(w==1920 && h==1080);
    display_render_size(2,1920,1080,&w,&h);assert(w==1280 && h==720);
    display_render_size(3,1280,720,&w,&h);assert(w==1920 && h==1080);
    assert(display_rate_x100(59.94f)==5994 && display_rate_x100(50)==5000 && display_rate_x100(0)==6000);
    puts("PASS: exhaustive stick identity/monotonicity, adaptive warmup/persistence/floors, render/output separation, refresh");
}
