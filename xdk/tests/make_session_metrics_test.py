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
enum {TIME_DECODE,TIME_CALLBACK_TOTAL,TIME_CALLBACK_GAP,TIME_FENCE_WAIT,TIME_COUNT};
static struct {uint32_t decoded,dropped,rendered,avg_decode_us;} stats;
static struct {int target_fps;bool stats_overlay;} options;
static struct {uint32_t frame_gaps,payload_bytes;} observations;
static decode_deadline deadline;
static gpu_time_window pipeline_times[TIME_COUNT];
static uint32_t pipeline_calls[TIME_COUNT],network_video_queue,network_rtt,gpu_slot_starvation,cached_failovers;
static uint64_t pipeline_total[TIME_COUNT],streaming_start_us,streaming_end_us,decode_us_sum;
static bool network_valid;
/* CODE */
int main(){
 performance_sample s;stats.decoded=1000;stats.rendered=980;stats.avg_decode_us=18000;
 options.target_fps=60;options.stats_overlay=true;decode_us_sum=0;
 pipeline_calls[TIME_DECODE]=1000;pipeline_total[TIME_DECODE]=17000000;
 streaming_start_us=100;streaming_end_us=20000100;
 for(int i=0;i<32;i++)gpu_time_add(&pipeline_times[TIME_DECODE],17000+i);
 video_get_session_performance(&s);assert(s.decode_avg_us==17000 && s.shown_fps==49 && s.tail_valid);
 assert(s.decode_p99_us==17031);
 decode_us_sum=500;video_get_session_performance(&s);assert(s.decode_avg_us==17000);
 pipeline_calls[TIME_DECODE]=0;options.stats_overlay=false;video_get_session_performance(&s);assert(s.decode_avg_us==18000);
 memset(pipeline_times,0,sizeof(pipeline_times));streaming_end_us=0;video_get_session_performance(&s);assert(!s.shown_fps && !s.tail_valid);
 puts("PASS: actual session decode mean uses the whole-session sum/count after periodic reset; EMA fallback, rolling tails and invalid duration preserved.");
}
'''
Path(sys.argv[1]).write_text(fixture.replace('/* CODE */',source[start:end]))
