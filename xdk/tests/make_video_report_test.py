"""Compile the actual cold decoder-session report with distinct stage counts."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
source = (root / 'xdk/src/video_xdk.cpp').read_text()
start = source.index('    if (options.stats_overlay && storage_dir())', source.index('static void video_cleanup('))
end = source.index('\n    free_resources();', start)
fixture = r'''
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "../src/video.h"
#include "../src/video_gpu_policy.h"
#include "../src/stream_metrics.h"
#include "../src/decode_deadline.h"
#define XDK_BUILD_PROFILE_LABEL "test"
#define XDK_WORKER_MAP_LABEL "current"
enum {TIME_DECODE,TIME_FRAME_COPY,TIME_TEXTURE_LOCK,TIME_TEXTURE_COPY,TIME_GPU_SUBMIT,TIME_FENCE_WAIT,
      TIME_PRESENT_WAIT,TIME_PACKET_PREPARE,TIME_CALLBACK_TOTAL,TIME_CALLBACK_GAP,TIME_COUNT};
static video_options options;
static video_stats stats;
static decode_deadline deadline;
static stream_observations observations;
static gpu_quality_policy quality_policy;
static gpu_time_window pipeline_times[TIME_COUNT];
static uint32_t pipeline_calls[TIME_COUNT],pipeline_peak[TIME_COUNT];
static uint64_t pipeline_total[TIME_COUNT],streaming_start_us,streaming_end_us;
static int vid_w=1280,vid_h=720,setup_fps_argument=60,session_slices=8,requested_shader;
static unsigned cap_frames,gpu_slot_starvation,cached_failovers,ff_xbox_vmx_flags,ff_xbox_loopfilter_mask;
static unsigned network_rtt,network_variance;
static int network_video_queue,network_video_queue_peak;
static bool network_valid,shader_diagnostic_override;
static unsigned writes;
static char saved[8192];
static const char *storage_dir(){return "test";}
static bool storage_write(const char *name,const void *text,size_t n){
    assert(!strcmp(name,"decoder-session.txt") && n<sizeof(saved));
    memcpy(saved,text,n);saved[n]=0;writes++;return true;
}
static void gfx_display_size(int *w,int *h){*w=1920;*h=1080;}
static void gfx_backbuffer_size(int *w,int *h){*w=1280;*h=720;}
static float gfx_display_hz(){return 60;}
static bool gfx_display_interlaced(){return false;}
static int current_pipeline(){return 1;}
static const char *pipeline_name(int){return "CPU_CACHED_TEXTURE";}
static uint64_t cached_video_memory(){return 5000000;}
static bool gfx_quality_available(){return true;}
static void actual_report(){
/* ACTUAL_REPORT */
}
int main(){
    options.stats_overlay=true;options.target_fps=60;options.vsync=true;options.color_space=COLORSPACE_REC_709;
    stats.received=1200;stats.decoded=1000;stats.rendered=980;
    streaming_start_us=100;streaming_end_us=20000100;
    actual_report();assert(writes==1);
    assert(strstr(saved,"requested_fps=60\n") && strstr(saved,"setup_fps_argument=60\n"));
    assert(strstr(saved,"negotiated_fps=unknown\nnegotiated_fps_known=0\n"));
    assert(strstr(saved,"callback_fps=60.000\n") && strstr(saved,"decoded_fps=50.000\n") && strstr(saved,"presented_fps=49.000\n"));
    assert(strstr(saved,"vsync_enabled=1\n") && strstr(saved,"not_scanout_or_visible_frames"));
    assert(strstr(saved,"callback_gap_p99_us=0\n")); // report tail was not truncated
    setup_fps_argument=30;streaming_end_us=0;actual_report();
    assert(writes==2 && strstr(saved,"setup_fps_argument=30\n") && strstr(saved,"negotiated_fps=unknown\n"));
    assert(strstr(saved,"callback_fps=0.000\n") && strstr(saved,"decoded_fps=0.000\n") && strstr(saved,"presented_fps=0.000\n"));
    options.stats_overlay=false;actual_report();assert(writes==2);
    options.stats_overlay=true;streaming_end_us=20000100;
    memset(&stats,0xff,sizeof(stats));memset(&deadline,0xff,sizeof(deadline));
    memset(&observations,0xff,sizeof(observations));memset(&quality_policy,0xff,sizeof(quality_policy));
    // Bounded full windows and large session counters must still leave report space.
    memset(&quality_policy,0,sizeof(quality_policy));
    for(int metric=0;metric<TIME_COUNT;metric++){
        pipeline_calls[metric]=pipeline_peak[metric]=UINT_MAX;pipeline_total[metric]=0xffffffffffffffffull;
        for(int i=0;i<GPU_TIME_SAMPLES;i++)gpu_time_add(&pipeline_times[metric],UINT_MAX);
    }
    actual_report();assert(writes==3 && strstr(saved,"callback_gap_p99_us=4294967295\n"));
    puts("PASS: actual decoder report: requested/setup/unknown negotiation, distinct stage rates, VSync, OFF, zero duration and bounded report tail.");
}
'''
Path(sys.argv[1]).write_text(fixture.replace('/* ACTUAL_REPORT */', source[start:end]))
