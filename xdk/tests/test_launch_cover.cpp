#include <assert.h>
#include <stdio.h>
#include "../../src/launch_cover.h"
int main()
{
    launch_visual v;launch_visual_sample frame;
    assert(LAUNCH_LOGO_Y+48<=LAUNCH_TEXT_H);
    for(int i=0;i<LAUNCH_STATUS_COUNT;i++) {
        assert(launch_status_progress((launch_status)i)<=7);
        assert(launch_display_status((launch_status)i,false,false)==i);
        assert(launch_display_status((launch_status)i,true,false)==LAUNCH_READY);
        assert(launch_display_status((launch_status)i,true,true)==LAUNCH_CANCELLING);
    }
    launch_visual_begin(&v,1000,true,72,200,150,200,true);
    launch_visual_get(&v,1000,&frame);assert(frame.x==72 && frame.w==150 && frame.dim==0 && frame.art==1 && frame.title==0);
    launch_visual_get(&v,1200,&frame);assert(frame.x==293 && frame.w==201 && frame.dim==.5f && frame.title==.8f);
    launch_visual_get(&v,1400,&frame);assert(frame.x==514 && frame.w==252 && frame.dim==1 && !frame.intro);
    launch_visual_begin(&v,2000,true,72,200,150,200,false);
    launch_visual_get(&v,6000,&frame);assert(frame.art==0 && frame.x==514);
    launch_visual_art_ready(&v,6000);launch_visual_get(&v,6000,&frame);assert(frame.art==0 && frame.x==514);
    launch_visual_get(&v,6125,&frame);assert(frame.art==.5f && frame.x==514);
    launch_visual_get(&v,6250,&frame);assert(frame.art==1 && !frame.intro);
    launch_visual_begin(&v,UINT32_MAX-199,true,0,0,150,200,false);
    launch_visual_get(&v,0,&frame);assert(frame.dim==.5f);
    launch_visual_begin(&v,0,false,0,0,150,200,false);launch_visual_art_ready(&v,0);
    launch_visual_get(&v,0,&frame);assert(frame.art==1 && frame.dim==1 && frame.title==1 && frame.x==514);
    puts("PASS: visual clock: real origin, 400ms easing, title 250ms, late art starts at usability, no movement restart, static and wrap");
    launch_cover s;
    for(int motion=0;motion<2;motion++)for(unsigned wait=0;wait<=10000;wait+=1000) {
        launch_cover_begin(&s,wait,motion!=0);
        assert(launch_cover_alpha(&s,50000,false,true)==255 && s.active);
        launch_cover_frame(&s,50001);
        launch_cover_frame(&s,60000);assert(s.first_ms==50001);
        if(wait)assert(launch_cover_alpha(&s,50000+wait,true,false)==255 && !s.revealing);
        assert(launch_cover_alpha(&s,50001+wait,false,true)==255 && !s.revealing);
        unsigned alpha=launch_cover_alpha(&s,50001+wait,true,false);
        assert(alpha==(motion?255u:0u));
        if(motion){assert(launch_cover_alpha(&s,50126+wait,true,false)==128);assert(!launch_cover_alpha(&s,50251+wait,true,false));}
        assert(!s.active);
    }
    launch_cover_begin(&s,5000,true);launch_cover_frame(&s,0);
    assert(launch_cover_alpha(&s,100,true,true)==255 && s.revealing);
    assert(launch_cover_alpha(&s,350,true,false)==0 && !s.active);
    launch_cover_begin(&s,20000,true);assert(s.wait_ms==10000);
    launch_cover_cancel(&s);launch_cover_frame(&s,10);
    assert(launch_cover_alpha(&s,99999,true,true)==255 && s.active);
    launch_cover_begin(&s,5000,true);assert(!s.cancelled && !s.first && !s.revealing);
    launch_cover_frame(&s,UINT32_MAX-1000);
    assert(launch_cover_alpha(&s,3998,true,false)==255 && !s.revealing);
    assert(launch_cover_alpha(&s,3999,true,false)==255 && s.revealing);
    assert(!launch_cover_alpha(&s,4249,true,false));
    uint32_t since=0;bool video=false;
    assert(!launch_cover_timeout(&since,&video,29999,true,false,false));
    assert(launch_cover_timeout(&since,&video,30000,true,false,false));
    assert(!launch_cover_timeout(&since,&video,30001,true,true,false));
    assert(since==30001 && !launch_cover_timeout(&since,&video,40001,true,true,false));
    assert(!launch_cover_timeout(&since,&video,90000,true,true,true));
    assert(!launch_cover_timeout(&since,&video,90001,true,true,false));
    assert(!launch_cover_timeout(&since,&video,150000,false,true,false));
    puts("PASS: controlled first-frame clock, 0..10s, early reveal, no usable frame, cancel, 250ms fade, static motion, reset and clock wrap");
}
