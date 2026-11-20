#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "src/video.h"
#include "src/video_picture.h"
typedef long LONG;
typedef uint32_t DWORD;
static DWORD tick;
static unsigned idr_events;
static video_options options={};
static volatile LONG refresh_active,refresh_state,refresh_requests,refresh_decoded_idrs;
static volatile LONG refresh_request_lock;
static volatile LONG refresh_completed_request=-1;
static volatile LONG refresh_generation;
static volatile LONG refresh_last_idr_ms,refresh_requested_ms,refresh_last_request_ms;
static LONG InterlockedCompareExchange(volatile LONG *p,LONG value,LONG expected){LONG old=*p;if(old==expected)*p=value;return old;}
static LONG InterlockedExchange(volatile LONG *p,LONG value){LONG old=*p;*p=value;return old;}
static LONG InterlockedIncrement(volatile LONG *p){return ++*p;}
static DWORD GetTickCount(){return tick;}
static void LiRequestIdrFrame(){idr_events++;}
static uint32_t idr_received,idr_decoded,idr_present_calls,decode_no_picture;
static unsigned writes;
static char saved[768];
static bool storage_write(const char *name,const void *data,size_t bytes){
    assert(!strcmp(name,"seamless-session.txt") && bytes<sizeof(saved));
    memcpy(saved,data,bytes);saved[bytes]=0;writes++;return true;
}
/* ACTUAL_REFRESH */
/* ACTUAL_REPORT */
int main(){
    video_refresh_snapshot s={};
    tick=1000;
    assert(!video_request_picture_refresh() && !idr_events);
    refresh_active=1;refresh_last_request_ms=tick;
    tick=4000;assert(video_request_picture_refresh() && idr_events==1);
    assert(!video_request_picture_refresh() && idr_events==1);
    video_get_refresh_snapshot(&s);assert(s.state==VIDEO_REFRESH_REQUESTED && s.requests==1);
    tick=4200;video_refresh_tick(tick);video_get_refresh_snapshot(&s);
    assert(s.state==VIDEO_REFRESH_WAITING);
    video_refresh_decoded_idr(4250,(uint32_t)refresh_requests);video_get_refresh_snapshot(&s);
    assert(s.state==VIDEO_REFRESH_DECODED && s.decoded_idrs==1 && s.last_idr_ms==4250);
    tick=4500;assert(!video_request_picture_refresh() && idr_events==1);
    tick=7000;assert(video_request_picture_refresh() && idr_events==2);
    tick=7200;video_refresh_tick(tick);
    tick=11001;video_refresh_tick(tick);video_get_refresh_snapshot(&s);
    assert(s.state==VIDEO_REFRESH_UNAVAILABLE);
    options.refresh_interval_ms=5000;refresh_last_request_ms=11001;
    tick=16001;video_refresh_tick(tick);assert(idr_events==3);
    refresh_state=VIDEO_REFRESH_IDLE;refresh_last_request_ms=100000;
    options.refresh_interval_ms=1000;
    tick=101000;video_refresh_tick(tick);assert(idr_events==4);
    tick=101500;video_refresh_tick(tick);assert(idr_events==4); // outstanding request
    video_refresh_decoded_idr(101600,(uint32_t)refresh_requests);
    tick=101999;video_refresh_tick(tick);assert(idr_events==4);
    tick=102000;video_refresh_tick(tick);assert(idr_events==5);
    video_refresh_decoded_idr(102200,(uint32_t)refresh_requests);
    options.refresh_interval_ms=2000;
    tick=103999;video_refresh_tick(tick);assert(idr_events==5);
    tick=104000;video_refresh_tick(tick);assert(idr_events==6);
    tick=104100;assert(!video_request_picture_refresh() && idr_events==6); // manual cooldown
    video_refresh_decoded_idr(104050,(uint32_t)refresh_requests);
    options.refresh_interval_ms=125;
    tick=104124;video_refresh_tick(tick);assert(idr_events==6);
    tick=104125;video_refresh_tick(tick);assert(idr_events==7);
    tick=104250;video_refresh_tick(tick);assert(idr_events==7); // no duplicate while waiting
    video_refresh_decoded_idr(104300,(uint32_t)refresh_requests);
    options.refresh_interval_ms=250;
    tick=104374;video_refresh_tick(tick);assert(idr_events==7);
    tick=104375;video_refresh_tick(tick);assert(idr_events==8);
    // Output identity survives delay/reordering: the current packet is irrelevant.
    uint64_t old_idr=video_picture_tag(31,7,true);
    uint64_t new_p=video_picture_tag(32,8,false);
    assert(video_picture_idr(old_idr,true) && !video_picture_idr(new_p,true));
    assert(!video_picture_idr(old_idr,false)); // no decoded key picture
    video_refresh_decoded_idr(104400,video_picture_request(old_idr));
    assert(refresh_state==VIDEO_REFRESH_REQUESTED); // older output cannot complete #8
    video_refresh_tick(104401);assert(refresh_state==VIDEO_REFRESH_REQUESTED);
    // A concurrent request admission never makes the decoder wait or loses completion.
    refresh_request_lock=1;
    video_refresh_decoded_idr(104402,8);
    assert(refresh_state==VIDEO_REFRESH_REQUESTED);
    refresh_request_lock=0;video_refresh_tick(104403);
    assert(refresh_state==VIDEO_REFRESH_DECODED && idr_events==8);
    assert(video_picture_request(video_picture_tag(0xffffffffU,0xffffffffU,true))==0xffffffffU);
    // Tick wrap preserves the subsecond cadence.
    refresh_last_request_ms=(LONG)0xfffffff0U;options.refresh_interval_ms=125;
    video_refresh_tick(108);assert(idr_events==8);
    video_refresh_tick(109);assert(idr_events==9);
    // Report remains available without overlay/timing collection.
    idr_received=12;idr_decoded=10;idr_present_calls=9;decode_no_picture=2;
    write_seamless_report();assert(writes==1);
    assert(strstr(saved,"idr_valid_decoded=10\nidr_present_calls=9\ndecode_no_picture=2\n"));
    assert(strstr(saved,"extra_gpu_history_bytes=see_smoothing-session.txt\nextra_gpu_samples=see_smoothing-session.txt\n"));
    // Resize/start resets statistics, but delayed output cannot alias request #1.
    uint32_t before_resize=(uint32_t)refresh_generation;
    refresh_requests=0;refresh_state=VIDEO_REFRESH_IDLE;refresh_completed_request=-1;
    options.refresh_interval_ms=0;refresh_last_request_ms=0;
    video_refresh_decoded_idr(1000,before_resize);
    tick=4000;assert(video_request_picture_refresh() && idr_events==10 && refresh_requests==1);
    video_refresh_tick(tick);assert(refresh_state==VIDEO_REFRESH_REQUESTED);
    video_refresh_decoded_idr(4001,(uint32_t)refresh_generation);
    assert(refresh_state==VIDEO_REFRESH_DECODED);
    refresh_active=0;tick=30000;video_refresh_tick(tick);
    assert(!video_request_picture_refresh() && idr_events==10);
    puts("PASS: actual refresh: manual/periodic/subsecond, delayed picture identity, old request rejection, nonblocking concurrent completion, tick wrap, shutdown and cold report");
}
