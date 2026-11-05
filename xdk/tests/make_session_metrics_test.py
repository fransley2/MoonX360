"""Check the actual stopped-session snapshot after periodic log sum resets."""
from pathlib import Path
import sys
r=Path(__file__).resolve().parents[2];source=(r/'xdk/src/video_xdk.cpp').read_text()
start=source.index('void video_get_session_performance(');end=source.index('{',start)+1;depth=1
while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
fixture=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/performance_advisor.h"
#include "../src/video_gpu_policy.h"
#include "../src/decode_deadline.h"
#include "../src/stream_metrics.h"
#include "../src/performance.h"
enum {TIME_DECODE,TIME_CALLBACK_TOTAL,TIME_CALLBACK_GAP,TIME_FENCE_WAIT,TIME_COUNT};
static struct {uint32_t received,decoded,dropped,rendered,avg_decode_us;} stats;
static struct {int target_fps;bool stats_overlay;} options;
static struct {uint32_t frame_gaps,payload_bytes;} observations;
static decode_deadline deadline;
static gpu_time_window pipeline_times[TIME_COUNT];
static uint32_t pipeline_calls[TIME_COUNT],network_video_queue,network_rtt,gpu_slot_starvation,cached_failovers;
static uint64_t pipeline_total[TIME_COUNT],streaming_start_us,streaming_end_us,decode_us_sum;
static uint64_t last_decoded_us,last_presented_us;
static uint64_t now_us(){return 0;}
static uint32_t last_packet_age_stop_ms,last_decode_age_stop_ms,last_present_age_stop_ms;
static bool network_valid;
static int setup_fps_argument;
struct RTP_VIDEO_STATS {uint32_t packetCountVideo,packetCountFec,packetCountFecRecovered,packetCountFecFailed,packetCountOOS,packetCountInvalid;};
static RTP_VIDEO_STATS last_rtp_stats;
static uint32_t last_complete_frames,last_video_packets,last_video_bytes,last_late_packets,last_lost_frames;
static uint32_t last_data_packets,last_fec_packets,present_duplicates;
/* CODE */
int main(){
 assert(video_stage_age_ms(2000000,1000000)==1000);
 assert(video_stage_age_ms(1,2)==UINT32_MAX && video_stage_age_ms(1,0)==UINT32_MAX);
 assert(video_stage_age_ms((uint64_t)UINT32_MAX*1000+1,1)==UINT32_MAX);
 performance_sample s;stats.received=1200;stats.decoded=1000;stats.rendered=980;stats.avg_decode_us=18000;
 last_packet_age_stop_ms=25;last_decode_age_stop_ms=30;last_present_age_stop_ms=40;
 options.target_fps=60;options.stats_overlay=true;decode_us_sum=0;
 setup_fps_argument=60;
 last_complete_frames=1100;last_video_packets=8000;last_rtp_stats.packetCountFecRecovered=12;
 last_rtp_stats.packetCountVideo=7000;last_rtp_stats.packetCountFec=1000;last_data_packets=6800;last_fec_packets=950;
 pipeline_calls[TIME_DECODE]=1000;pipeline_total[TIME_DECODE]=17000000;
 streaming_start_us=100;streaming_end_us=20000100;
 for(int i=0;i<32;i++)gpu_time_add(&pipeline_times[TIME_DECODE],17000+i);
 video_get_session_performance(&s);assert(s.decode_avg_us==17000 && s.shown_fps==49 && s.tail_valid);
 assert(s.last_video_packet_age_ms==25 && s.last_decoded_age_ms==30 && s.last_presented_age_ms==40);
 assert(s.requested_fps==60 && s.setup_fps_argument==60 && !s.negotiated_fps_known && !s.negotiated_fps);
 assert(s.callback_fps==60 && s.decoded_fps==50 && s.presented_fps==49);
 assert(s.complete_frame_fps==55 && s.complete_frames==1100 && s.video_packets==8000 && s.fec_recovered==12);
 assert(s.expected_video_data_packets==7000 && s.expected_fec_packets==1000 && s.accepted_data_packets==6800 && s.accepted_fec_packets==950);
 setup_fps_argument=30;video_get_session_performance(&s);
 assert(s.requested_fps==60 && s.setup_fps_argument==30 && !s.negotiated_fps_known);
 assert(s.decode_p99_us==17031);
 decode_us_sum=500;video_get_session_performance(&s);assert(s.decode_avg_us==17000);
 pipeline_calls[TIME_DECODE]=0;options.stats_overlay=false;video_get_session_performance(&s);assert(s.decode_avg_us==18000);
 memset(pipeline_times,0,sizeof(pipeline_times));streaming_end_us=0;video_get_session_performance(&s);
 assert(!s.shown_fps && !s.tail_valid && !s.callback_fps && !s.decoded_fps && !s.presented_fps);
 puts("PASS: actual session decode mean uses the whole-session sum/count after periodic reset; EMA fallback, rolling tails and invalid duration preserved.");
}
'''
helper_start=source.index('static uint32_t video_stage_age_ms(')
helper=source[helper_start:source.index('}',helper_start)+1]
Path(sys.argv[1]).write_text(fixture.replace('/* CODE */',helper+source[start:end]))
