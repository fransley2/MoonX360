#pragma once
#include <stdint.h>
#include <stdbool.h>
/* The existing controller is advisory. No live renegotiation or reconnect.
 * One sample/second, 15s warmup, sustained evidence and 30s recommendation cooldown. */
enum adaptive_reason {ADAPT_NONE,ADAPT_NETWORK,ADAPT_DECODER,ADAPT_REFRESH};
struct adaptive_stream {unsigned bad_network,bad_decode,bad_refresh;uint32_t started,last_notice;bool noticed;};
struct adaptive_evidence {
    bool network_valid,suspended;
    unsigned recovered,missing,lost,overflows,queue;
    uint32_t decode_us,queue_us;
    int fps,refresh_ms;
};
static unsigned adaptive_streak(unsigned n,bool bad){return bad?(n<30?n+1:n):(n>1?n-2:0);}
static int adaptive_stream_observe(struct adaptive_stream *s,uint32_t now,const struct adaptive_evidence *e)
{
    if(e->suspended || (uint32_t)(now-s->started)<15000) {
        s->bad_network=s->bad_decode=s->bad_refresh=0;return ADAPT_NONE;
    }
    uint32_t budget=1000000u/(e->fps>0?e->fps:30);
    bool queued=e->queue>1 || e->queue_us>budget*2;
    bool decode=queued && e->decode_us>budget*95/100;
    /* MCC's poor-connection hint alone and unknown loss are not network evidence.
     * A decode-queue overflow is not a missing UDP packet. */
    bool network=e->network_valid && (e->missing || e->lost || e->recovered>=4);
    bool refresh=e->refresh_ms>0 && e->refresh_ms<=1000 && queued;
    s->bad_network=adaptive_streak(s->bad_network,network);
    s->bad_decode=adaptive_streak(s->bad_decode,decode || (e->overflows && e->decode_us>budget*9/10));
    s->bad_refresh=adaptive_streak(s->bad_refresh,refresh);
    if(s->noticed && (uint32_t)(now-s->last_notice)<30000)return ADAPT_NONE;
    int reason=s->bad_decode>=8?ADAPT_DECODER:s->bad_network>=8?ADAPT_NETWORK:s->bad_refresh>=8?ADAPT_REFRESH:ADAPT_NONE;
    if(reason){s->last_notice=now;s->noticed=true;}return reason;
}
/* Legacy caller contract retained, but an uncorroborated poor hint cannot act. */
static int adaptive_stream_sample(struct adaptive_stream *s,uint32_t now,bool poor,unsigned queue,uint32_t us,int fps,bool suspended)
{
    struct adaptive_evidence e={0};(void)poor;e.queue=queue;e.decode_us=us;e.fps=fps;e.suspended=suspended;
    return adaptive_stream_observe(s,now,&e);
}
static bool adaptive_stream_lower(int reason,int *width,int *height,int *bitrate)
{
    if(reason==ADAPT_NETWORK) {
        if(*bitrate<=6000)return false;
        int next=(*bitrate/1000)*750;*bitrate=next<6000?6000:next;return true;
    }
    if(reason!=ADAPT_DECODER)return false;
    if(*height>720){*width=1280;*height=720;return true;}
    if(*height>480){*width=854;*height=480;return true;}
    return false;
}
static const char *adaptive_stream_advice(int reason)
{
    return reason==ADAPT_NETWORK?"Sustained RTP/FEC reconstruction pressure. Try a lower bitrate next session; the local link does not identify the complete host path.":
        reason==ADAPT_DECODER?"Decode pressure with a growing queue. Try 30 FPS or a lower resolution next session. Frequent picture refresh may add stuttering.":
        "Frequent picture refresh with a backed-up queue may add stuttering. Try a longer refresh interval (2 or 5 seconds). This does not establish the cause.";
}
