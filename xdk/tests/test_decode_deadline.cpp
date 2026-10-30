#include "../../src/decode_deadline.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint64_t tick=1000000;
static bool sample(decode_deadline *s,int fps,uint32_t age,uint32_t ema,bool forced=false){
    tick+=1000000/(fps?fps:30);
    bool fast=decode_deadline_before(s,forced,fps,tick-age,tick,ema);
    decode_deadline_after(s,fast,true,ema);return fast;
}
int main(){
    decode_deadline s={};
    assert(!sample(&s,60,15000,17000));assert(!sample(&s,60,0,8000)); // transient spike
    assert(!sample(&s,60,15000,17000));assert(sample(&s,60,16000,17000));
    for(int i=0;i<28;i++)assert(sample(&s,60,0,8000));
    assert(sample(&s,60,0,8000));assert(!sample(&s,60,0,8000));
    // Long decode pressure WITHOUT growing queue must NEVER generate a periodic IDR.
    memset(&s,0,sizeof(s));
    for(int i=0;i<3600;i++){sample(&s,60,0,18000);assert(!decode_deadline_needs_refresh(&s));}
    assert(s.recovery && s.recoveries==1);
    // Reset only for a persistently growing six-frame backlog, once per episode.
    for(int i=0;i<32;i++)sample(&s,60,110000+i*100,18000);
    assert(decode_deadline_needs_refresh(&s));assert(!decode_deadline_needs_refresh(&s));
    uint64_t last=s.last_refresh;
    for(int i=0;i<32;i++)sample(&s,60,0,8000);
    for(int i=0;i<32;i++)sample(&s,60,120000+i*100,18000);
    assert(!decode_deadline_needs_refresh(&s) && s.last_refresh==last); // cooldown across episodes
    for(int i=0;i<600;i++)sample(&s,60,150000+i,18000);
    assert(decode_deadline_needs_refresh(&s));
    for(int i=0;i<20;i++)sample(&s,60,0,8000,true);
    assert(!s.recovery && !decode_deadline_needs_refresh(&s));
    // Missing/future timestamps cannot authorize recovery exit.
    s.recovery=true;s.fast_duration=30;
    for(int i=0;i<40;i++){assert(decode_deadline_before(&s,false,60,0,tick,1000));assert(!s.queue_valid);}
    assert(s.healthy==0);
    assert(decode_deadline_before(&s,false,60,tick+1,tick,1000));
    int fps[]={24,30,40,50,60};
    for(int i=0;i<5;i++){memset(&s,0,sizeof(s));sample(&s,fps[i],0,1000);assert(s.budget_us==1000000u/fps[i]);}
    puts("PASS: recovery hysteresis, no timed IDR, bounded backlog reset, cooldown, invalid clocks, forced fast");
}
