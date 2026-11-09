/* Single receive-owner metadata. Readers use these records only after teardown. */
#pragma once
#include <Limelight.h>
#include <string.h>
static RTP_VIDEO_FRAME_STATS *rtp_diagnostic_frame(RTP_VIDEO_STATS *s,uint32_t frame,bool enabled)
{
    if(!enabled)return NULL;
    RTP_VIDEO_FRAME_STATS *f=&s->frames[frame%RTP_VIDEO_FRAME_SAMPLES];
    if(!f->valid || f->frame!=frame){memset(f,0,sizeof(*f));f->frame=frame;f->valid=true;}
    return f;
}
static uint32_t rtp_diagnostic_deficit(uint32_t expected,uint32_t received)
{
    return expected>received?expected-received:0;
}
static void rtp_diagnostic_expected(RTP_VIDEO_STATS *s,uint32_t frame,uint32_t data,uint32_t parity,bool trace)
{
    s->packetCountVideo+=data;s->packetCountFec+=parity;
    RTP_VIDEO_FRAME_STATS *f=rtp_diagnostic_frame(s,frame,trace);
    if(f){f->expectedData+=data;f->expectedFec+=parity;}
}
static void rtp_diagnostic_accepted(RTP_VIDEO_STATS *s,uint32_t frame,bool parity,uint32_t bytes,uint64_t now,bool trace)
{
    if(parity)s->acceptedFec++;else s->acceptedData++;
    RTP_VIDEO_FRAME_STATS *f=rtp_diagnostic_frame(s,frame,trace);if(!f)return;
    if(!f->firstUs)f->firstUs=now;
    if(f->lastUs && now>=f->lastUs && now-f->lastUs>f->maxGapUs)
        f->maxGapUs=now-f->lastUs>UINT32_MAX?UINT32_MAX:(uint32_t)(now-f->lastUs);
    f->lastUs=now;f->bytes+=bytes;if(parity)f->acceptedFec++;else f->acceptedData++;
}
