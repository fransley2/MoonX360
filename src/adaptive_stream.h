#pragma once
#include <stdint.h>
#include <stdbool.h>
/* One sample per second, no allocations or changes to compressed reference frames.
 * Action 1: reduce bandwidth; action 2: reduce pixel rate. Hysteresis is deliberately
 * slow because GameStream requires reconnecting for a new mode/bitrate. */
struct adaptive_stream {unsigned bad_network,bad_decode;uint32_t started;};
static int adaptive_stream_sample(struct adaptive_stream *s,uint32_t now,bool poor,
    unsigned queue,uint32_t decode_us,int fps,bool suspended)
{
    if(suspended || (uint32_t)(now-s->started)<15000) {s->bad_network=s->bad_decode=0;return 0;}
    uint32_t budget=1000000u/(fps>0?fps:30);
    bool decode=queue>1 && decode_us>budget*95/100;
    s->bad_network=poor?s->bad_network+1:0;
    s->bad_decode=decode?s->bad_decode+1:0;
    if(s->bad_decode>=8)return 2;
    if(s->bad_network>=8)return 1;
    return 0;
}
static bool adaptive_stream_lower(int reason,int *width,int *height,int *bitrate)
{
    if(reason==1 && *bitrate>6000) {
        int next=(*bitrate/1000)*750;
        *bitrate=next<6000?6000:next;return true;
    }
    if(*height>720){*width=1280;*height=720;if(*bitrate>15000)*bitrate=15000;return true;}
    if(*height>480){*width=854;*height=480;if(*bitrate>8000)*bitrate=8000;return true;}
    return false;
}
