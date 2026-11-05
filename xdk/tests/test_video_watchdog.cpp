#include <assert.h>
#include <stdio.h>
#include "../../src/video_watchdog.h"

static video_progress_snapshot first(void)
{
    video_progress_snapshot p={};
    for(int i=VP_RX;i<=VP_PRESENT_COMPLETED;i++)p.count[i]=1;
    p.frame_serial=1;p.count[VP_AUDIO]=p.count[VP_INPUT]=1;
    return p;
}
int main(void)
{
    video_progress_snapshot p=first();
    video_watchdog w={};
    assert(video_watchdog_tick(&w,&p,0)==0);

    // A: audio/input progress, UDP video stops.
    p.count[VP_AUDIO]+=50;p.count[VP_INPUT]+=250;
    assert(video_watchdog_tick(&w,&p,1200)==0 && w.kind==VS_RX);
    assert(video_watchdog_tick(&w,&p,3000)==1);
    assert(video_watchdog_tick(&w,&p,5000)==2);
    assert(video_watchdog_tick(&w,&p,8000)==3);
    assert(video_watchdog_tick(&w,&p,11000)==4);
    assert(video_watchdog_tick(&w,&p,14000)==5);
    assert(video_watchdog_tick(&w,&p,17000)==0);
    assert(video_watchdog_tick(&w,&p,30000)==6);

    // Packets arrive but RTP/FEC never completes a frame.
    p=first();w=video_watchdog();video_watchdog_tick(&w,&p,0);
    p.count[VP_RX]+=400;p.count[VP_AUDIO]+=50;
    video_watchdog_tick(&w,&p,1200);assert(w.kind==VS_ASSEMBLY);

    // B: complete access units arrive, FFmpeg produces no frames.
    p=first();w=video_watchdog();video_watchdog_tick(&w,&p,0);
    p.count[VP_RX]+=400;p.count[VP_ASSEMBLED]+=120;p.count[VP_SUBMITTED]+=120;
    p.count[VP_AUDIO]+=50;
    video_watchdog_tick(&w,&p,1200);assert(w.kind==VS_DECODER);

    // C: decode continues, no consumer selection.
    p=first();w=video_watchdog();video_watchdog_tick(&w,&p,0);
    p.count[VP_RX]+=400;p.count[VP_ASSEMBLED]+=120;p.count[VP_SUBMITTED]+=120;
    p.count[VP_DECODED]+=120;p.count[VP_QUEUED]+=120;
    video_watchdog_tick(&w,&p,1200);assert(w.kind==VS_QUEUE);

    // D: selected frames cannot be uploaded.
    p.count[VP_SELECTED]+=120;
    video_watchdog_tick(&w,&p,1300);assert(w.kind==VS_UPLOAD);

    // A cached upload that never returns must not be blamed on FFmpeg.
    p=first();w=video_watchdog();video_watchdog_tick(&w,&p,0);
    p.count[VP_RX]+=400;p.count[VP_ASSEMBLED]+=120;p.count[VP_SUBMITTED]++;
    p.count[VP_DECODED]++;p.count[VP_UPLOAD_STARTED]++;
    video_watchdog_tick(&w,&p,1200);assert(w.kind==VS_UPLOAD);

    // An older failed upload must not mask a later successful upload followed
    // by a blocked queue consumer.
    p=first();w=video_watchdog();video_watchdog_tick(&w,&p,0);
    p.count[VP_RX]+=400;p.count[VP_ASSEMBLED]+=120;p.count[VP_SUBMITTED]+=120;
    p.count[VP_CALLBACK_ENTERED]+=120;p.count[VP_CALLBACK_RETURNED]+=120;
    p.count[VP_DECODED]+=120;p.count[VP_QUEUED]+=120;
    p.count[VP_UPLOAD_STARTED]+=2;p.count[VP_UPLOADED]++;
    p.last_ms[VP_UPLOAD_STARTED]=200;p.last_ms[VP_UPLOADED]=210;
    video_watchdog_tick(&w,&p,1200);assert(w.kind==VS_QUEUE);

    // E: GPU submit and Present enter, but Present never returns.
    p=first();w=video_watchdog();video_watchdog_tick(&w,&p,0);
    p.count[VP_RX]+=400;p.count[VP_ASSEMBLED]+=120;p.count[VP_SUBMITTED]+=120;
    p.count[VP_DECODED]+=120;p.count[VP_QUEUED]+=120;p.count[VP_SELECTED]+=120;
    p.count[VP_UPLOADED]+=120;p.count[VP_GPU_SUBMITTED]+=120;p.count[VP_PRESENT_CALLED]++;
    video_watchdog_tick(&w,&p,1400);assert(w.kind==VS_PRESENTATION);

    // A cached texture fence never clears: all slots are in flight.
    p=first();w=video_watchdog();video_watchdog_tick(&w,&p,0);
    p.count[VP_RX]+=400;p.count[VP_ASSEMBLED]+=120;p.count[VP_SUBMITTED]+=120;
    p.count[VP_DECODED]+=120;p.cached_in_flight=3;
    p.gpu_slot_capacity=3;
    video_watchdog_tick(&w,&p,1200);assert(w.kind==VS_GPU);

    // Legacy's two texture sets can both remain fenced after frame one/two.
    p=first();w=video_watchdog();video_watchdog_tick(&w,&p,0);
    p.count[VP_RX]+=400;p.count[VP_ASSEMBLED]+=120;p.count[VP_SUBMITTED]+=120;
    p.count[VP_DECODED]+=120;p.count[VP_QUEUED]+=120;p.count[VP_SELECTED]++;
    p.cached_in_flight=p.gpu_slot_capacity=2;
    video_watchdog_tick(&w,&p,1200);assert(w.kind==VS_GPU);

    // F: 200-300 ms jitter does not warn or recover.
    p=first();w=video_watchdog();video_watchdog_tick(&w,&p,0);
    p.count[VP_AUDIO]+=15;p.count[VP_INPUT]+=50;
    assert(video_watchdog_tick(&w,&p,300)==0 && !w.warned);

    // G: natural recovery after a warning clears without a reset.
    video_watchdog_tick(&w,&p,1200);assert(w.warned && w.level==0);
    p.count[VP_PRESENT_COMPLETED]++;p.frame_serial++;
    assert(video_watchdog_tick(&w,&p,1500)==0 && !w.warned && w.level==0);

    // H: IDR restores progress; no decoder reset follows.
    p=first();w=video_watchdog();video_watchdog_tick(&w,&p,0);
    p.count[VP_RX]+=400;p.count[VP_ASSEMBLED]+=120;p.count[VP_SUBMITTED]+=120;
    assert(video_watchdog_tick(&w,&p,3000)==1);
    assert(video_watchdog_tick(&w,&p,5000)==2);
    p.count[VP_DECODED]++;p.count[VP_QUEUED]++;p.count[VP_SELECTED]++;
    p.count[VP_UPLOADED]++;p.count[VP_GPU_SUBMITTED]++;
    p.count[VP_PRESENT_CALLED]++;p.count[VP_PRESENT_COMPLETED]++;p.frame_serial++;
    assert(video_watchdog_tick(&w,&p,5500)==0 && w.successes==1 && !w.attempts[3]);

    // I: IDR is insufficient, callback-thread decoder reset is requested.
    p=first();w=video_watchdog();video_watchdog_tick(&w,&p,0);
    p.count[VP_RX]+=400;p.count[VP_ASSEMBLED]+=120;p.count[VP_SUBMITTED]+=120;
    p.count[VP_AUDIO]+=100;p.count[VP_INPUT]+=1000;
    assert(video_watchdog_tick(&w,&p,3000)==1);
    assert(video_watchdog_tick(&w,&p,5000)==2);
    assert(video_watchdog_tick(&w,&p,8000)==3);
    assert(p.count[VP_AUDIO]>1 && p.count[VP_INPUT]>1);

    // J: bounded escalation, never a second automatic reconnect decision.
    assert(video_watchdog_tick(&w,&p,11000)==4);
    assert(video_watchdog_tick(&w,&p,14000)==5);
    assert(video_watchdog_tick(&w,&p,17000)==6);
    assert(video_watchdog_tick(&w,&p,20000)==0 && w.level==6);
    p.count[VP_CALLBACK_ENTERED]+=120;p.count[VP_CALLBACK_RETURNED]+=120;
    p.count[VP_RENDER_ITERATION]++;p.last_ms[VP_RENDER_ITERATION]=17000;
    assert(video_watchdog_reconnect_safe(&w,&p,17000,0));
    assert(!video_watchdog_reconnect_safe(&w,&p,17000,1));
    p.cached_in_flight=1;assert(!video_watchdog_reconnect_safe(&w,&p,17000,0));
    p.cached_in_flight=0;p.count[VP_CALLBACK_ENTERED]++;
    assert(!video_watchdog_reconnect_safe(&w,&p,17000,0)); // blocked decoder callback

    // The 32-bit millisecond clock wraps without suppressing a real stall.
    p=first();w=video_watchdog();
    video_watchdog_tick(&w,&p,0xffffff00u);
    p.count[VP_AUDIO]++;p.count[VP_INPUT]++;
    assert(video_watchdog_tick(&w,&p,0xffffff00u+1200u)==0 && w.kind==VS_RX);
    assert(video_watchdog_tick(&w,&p,0xffffff00u+3000u)==1);
    puts("PASS: video watchdog A-J");
}
