#include "xdk.h"
#include "../../src/performance.h"
#include "../../src/timing.h"
#include "../../src/config.h"
#include <stdio.h>
#include <stdlib.h>
volatile int perf_active;
struct __declspec(align(128)) perf_slot {struct perf_window window;};
static struct perf_slot slots[PERF_COUNT];
static struct perf_slot last_slots[PERF_COUNT];
static uint64_t marks[PERF_EVENT_COUNT],frequency,origin,ui_started,input_sample_us[4];
static uint64_t counters[PERF_C_COUNT];
static volatile LONG pressure[4];
void perf_pressure_read(struct perf_pressure *out)
{
    out->recovered=(uint32_t)InterlockedCompareExchange(&pressure[0],0,0);
    out->missing=(uint32_t)InterlockedCompareExchange(&pressure[1],0,0);
    out->lost=(uint32_t)InterlockedCompareExchange(&pressure[2],0,0);
    out->overflows=(uint32_t)InterlockedCompareExchange(&pressure[3],0,0);
    out->valid=true; /* event counters; exact packet loss is still unavailable */
}
static uint64_t last_video_packet_us;
static uint32_t video_packet_ordinal;
static volatile LONG video_tracking;
static volatile LONG progress_count[VP_STAGE_COUNT],progress_last[VP_STAGE_COUNT];
static volatile LONG progress_serial,progress_queue_age,progress_gpu_wait;
static void progress_max(volatile LONG *value,uint32_t sample)
{
    LONG old=InterlockedCompareExchange(value,0,0);
    while(sample>(uint32_t)old && InterlockedCompareExchange(value,(LONG)sample,old)!=old)
        old=InterlockedCompareExchange(value,0,0);
}
void video_progress_note(int stage)
{
    if(stage<0 || stage>=VP_STAGE_COUNT)return;
    InterlockedIncrement(&progress_count[stage]);
    InterlockedExchange(&progress_last[stage],(LONG)GetTickCount());
}
void video_progress_new_present(void){InterlockedIncrement(&progress_serial);}
void video_progress_queue_age(uint32_t age_ms){progress_max(&progress_queue_age,age_ms);}
void video_progress_gpu_wait(uint32_t wait_ms){progress_max(&progress_gpu_wait,wait_ms);}
void video_progress_snapshot_get(struct video_progress_snapshot *out)
{
    memset(out,0,sizeof(*out));
    for(int i=0;i<VP_STAGE_COUNT;i++) {
        out->count[i]=(uint32_t)InterlockedCompareExchange(&progress_count[i],0,0);
        out->last_ms[i]=(uint32_t)InterlockedCompareExchange(&progress_last[i],0,0);
    }
    out->frame_serial=(uint32_t)InterlockedCompareExchange(&progress_serial,0,0);
    out->max_queue_age_ms=(uint32_t)InterlockedCompareExchange(&progress_queue_age,0,0);
    out->max_gpu_wait_ms=(uint32_t)InterlockedCompareExchange(&progress_gpu_wait,0,0);
}
static uint64_t last_counters[PERF_C_COUNT];
static uint64_t capture_counter_base[PERF_C_COUNT];
static uint64_t capture_started_us,capture_duration_us,last_capture_duration_us;
static bool capture_bounded,capture_closed;
static struct {int fps,refresh_x100;unsigned observations;} fps_stages[PERF_FPS_STAGE_COUNT];
static struct {uint32_t frame_number;uint64_t completed_us;} frame_trace[PERF_SAMPLES];
static unsigned frame_trace_next,frame_trace_count;
static volatile LONG frame_trace_active;
uint64_t perf_clock(void)
{
    LARGE_INTEGER now;QueryPerformanceCounter(&now);
    uint64_t ticks=(uint64_t)now.QuadPart;
    return moonx_ticks_to_us(ticks,frequency);
}
void perf_begin(bool enabled)
{
    /* Called before connection workers start; no resetting a live producer. */
    InterlockedExchange((volatile LONG*)&perf_active,0);
    for(int i=0;i<VP_STAGE_COUNT;i++) {
        InterlockedExchange(&progress_count[i],0);InterlockedExchange(&progress_last[i],0);
    }
    InterlockedExchange(&progress_serial,0);
    InterlockedExchange(&progress_queue_age,0);InterlockedExchange(&progress_gpu_wait,0);
    memset(slots,0,sizeof(slots));memset(marks,0,sizeof(marks));memset(counters,0,sizeof(counters));memset(capture_counter_base,0,sizeof(capture_counter_base));memset(input_sample_us,0,sizeof(input_sample_us));ui_started=0;
    last_video_packet_us=0;video_packet_ordinal=0;InterlockedExchange(&video_tracking,enabled?1:0);
    memset(fps_stages,0,sizeof(fps_stages));frame_trace_next=frame_trace_count=0;
    InterlockedExchange(&frame_trace_active,0);
    capture_started_us=capture_duration_us=0;capture_bounded=capture_closed=false;
    LARGE_INTEGER value;QueryPerformanceFrequency(&value);frequency=(uint64_t)value.QuadPart;
    origin=enabled?perf_clock():0;
    InterlockedExchange((volatile LONG*)&perf_active,enabled?1:0);
}
void perf_record(int metric,uint64_t us)
{if(perf_enabled() && metric>=0 && metric<PERF_COUNT)perf_window_add(&slots[metric].window,us);}
void perf_count(int counter,uint32_t amount)
{
    int index=counter==PERF_C_VIDEO_FEC_RECOVERED?0:counter==PERF_C_VIDEO_FEC_FAILED?1:
        counter==PERF_C_VIDEO_FRAMES_LOST?2:counter==PERF_C_VIDEO_QUEUE_OVERFLOW?3:-1;
    if(index>=0)InterlockedExchangeAdd(&pressure[index],(LONG)amount);
    if(perf_enabled() && counter>=0 && counter<PERF_C_COUNT)counters[counter]+=amount;
}
void perf_video_packet_seen(uint32_t bytes)
{
    video_progress_note(VP_RX);
    if(!video_tracking)return;
    if(perf_enabled()) {perf_count(PERF_C_VIDEO_PACKETS,1);perf_count(PERF_C_VIDEO_BYTES,bytes);}
    if(++video_packet_ordinal==1 || (video_packet_ordinal&63)==0)last_video_packet_us=perf_clock();
}
bool perf_video_tracking_enabled(void){return video_tracking!=0;}
uint32_t perf_last_video_packet_age_ms(void)
{
    if(!last_video_packet_us)return UINT32_MAX;
    uint64_t now=perf_clock();
    return now>=last_video_packet_us && (now-last_video_packet_us)/1000<UINT32_MAX?
        (uint32_t)((now-last_video_packet_us)/1000):UINT32_MAX;
}
void perf_request_fps(int stage,int fps,int client_refresh_x100)
{
    if(stage<0 || stage>=PERF_FPS_STAGE_COUNT)return;
    fps_stages[stage].fps=fps;
    fps_stages[stage].refresh_x100=client_refresh_x100;
    fps_stages[stage].observations++;
}
void perf_frame_complete(uint32_t frame_number)
{
    if(!perf_enabled() || !frame_trace_active)return;
    frame_trace[frame_trace_next].frame_number=frame_number;
    frame_trace[frame_trace_next].completed_us=perf_clock();
    frame_trace_next=(frame_trace_next+1)%PERF_SAMPLES;
    if(frame_trace_count<PERF_SAMPLES)frame_trace_count++;
}
uint64_t perf_counter_read(int counter)
{return counter>=0 && counter<PERF_C_COUNT?counters[counter]:0;}
uint64_t perf_window_counter_read(int counter)
{
    if(counter<0 || counter>=PERF_C_COUNT)return 0;
    return counters[counter]-(capture_started_us?capture_counter_base[counter]:0);
}
uint64_t perf_window_duration_us(void)
{
    if(capture_started_us)return capture_closed?capture_duration_us:perf_clock()-capture_started_us;
    return origin?perf_clock()-origin:0;
}
uint64_t perf_last_counter_read(int counter)
{return counter>=0 && counter<PERF_C_COUNT?last_counters[counter]:0;}
void perf_input_sample(int controller,uint64_t detected_us)
{if(controller>=0 && controller<4)input_sample_us[controller]=detected_us;}
uint64_t perf_input_sample_time(int controller)
{return controller>=0 && controller<4?input_sample_us[controller]:0;}
void perf_capture_start(bool bounded)
{
    capture_started_us=perf_enabled()?perf_clock():0;capture_bounded=bounded;capture_closed=false;
    for(int i=0;i<PERF_C_COUNT;i++)capture_counter_base[i]=counters[i];
    frame_trace_next=frame_trace_count=0;
    InterlockedExchange(&frame_trace_active,capture_started_us?1:0);
}
void perf_capture_close(void)
{
    if(!capture_started_us || capture_closed)return;
    InterlockedExchange(&frame_trace_active,0);
    uint64_t now=perf_clock();
    capture_duration_us=now-capture_started_us;
    capture_closed=true;
    InterlockedExchange((volatile LONG*)&perf_active,0);
}
bool perf_capture_tick(void)
{
    if(!capture_bounded || capture_closed || !capture_started_us)return false;
    if(perf_clock()-capture_started_us<45000000)return false;
    perf_capture_close();
    return true;
}
uint64_t perf_capture_duration_us(void){return last_capture_duration_us;}
void perf_mark(int event)
{if(perf_enabled() && event>=0 && event<PERF_EVENT_COUNT && !marks[event])marks[event]=perf_clock()-origin+1;}
void perf_ui_begin(void){if(perf_enabled() && !ui_started)ui_started=perf_clock();}
void perf_ui_end(void){if(ui_started){perf_record(PERF_UI_BUILD,perf_clock()-ui_started);ui_started=0;}}
static int compare(const void *a,const void *b)
{uint32_t x=*(const uint32_t*)a,y=*(const uint32_t*)b;return x<y?-1:x>y;}
void perf_get_summary(int metric,struct perf_summary *out)
{
    memset(out,0,sizeof(*out));
    if(metric<0 || metric>=PERF_COUNT)return;
    const perf_window *w=&last_slots[metric].window;
    out->current=w->current;out->ema=w->ema;out->max=w->maximum;out->count=w->count;out->total=w->total;
    if(!w->filled)return;
    uint32_t sorted[PERF_SAMPLES];memcpy(sorted,w->samples,w->filled*sizeof(uint32_t));
    qsort(sorted,w->filled,sizeof(uint32_t),compare);
    out->p50=sorted[(w->filled*50+99)/100-1];
    out->p95=sorted[(w->filled*95+99)/100-1];
    out->p99=sorted[(w->filled*99+99)/100-1];
}
static void flush_report(const char *path)
{
    /* All network/audio/input/video producers are stopped; sorting/I/O are cold. */
    if(!origin)return;
    static const char *names[]={"tcp_setup","tls_setup","rtsp_setup","moonlight_negotiation","cover_http","cover_decode",
        "ui_screen_build","ui_texture_upload","home_cache_read","input_interval","input_poll_work","input_send_enqueue",
        "audio_callback","opus_decode","audio_glitches","audio_queue_full","audio_submit_failure",
        "controller_sample_enqueue","controller_queue_send","controller_local_latency",
        "input_queue_depth_sampled","input_holder_age_at_dequeue","input_oldest_holder_age_sampled","input_send_work",
        "video_recv_syscall_sampled_1of64","video_decrypt_sampled_1of64","video_rtp_cpu_sampled_1of64",
        "video_first_packet_to_fec_complete","video_observed_gap_wait","video_depacketize","video_decode_queue_depth",
        "video_decode_queue_age","host_reported_processing","audio_buffers","audio_queued_ms",
        "y_plane_transfer","u_plane_transfer","v_plane_transfer","yuv_transfer_total",
        "cached_free_texture_slots","legacy_texture_upload","frame_ready_to_present","present_call_interval",
        "present_call_cpu","h264_avcodec_call","post_decode_handling","cpu_gpu_submit","d3d_thread_ownership"};
    static const char *counter_names[]={"input_coalesced","input_queue_full","video_udp_packets",
        "video_udp_bytes","video_fec_complete_frames","video_late_packets","video_oos_packets",
        "video_fec_recovered_packets","video_incomplete_data_deficit","video_lost_frames","video_invalid_packets",
        "video_gap_filled_frames","video_gap_unresolved_frames","video_data_packets_accepted","video_fec_packets_accepted","video_decode_queue_overflows"};
    static const char *events[]={"connect_start","app_launch_response","moonlight_connected","first_video_packet","first_decoded","first_presented","stop_start","return_menu"};
    char report[16384];int n=snprintf(report,sizeof(report),"schema=2\nscope=single-producer CPU wall time; p50/p95/p99 rolling 128; no per-frame logs\n");
    for(int i=0;i<PERF_EVENT_COUNT && n>0 && (size_t)n<sizeof(report);i++)
        n+=snprintf(report+n,sizeof(report)-n,"%s_observed=%d\n%s_us=%I64u\n",events[i],marks[i]!=0,events[i],(unsigned long long)(marks[i]?marks[i]-1:0));
    for(int i=0;i<PERF_COUNT && n>0 && (size_t)n<sizeof(report);i++) {
        const perf_window *w=&slots[i].window;uint32_t sorted[PERF_SAMPLES];memcpy(sorted,w->samples,w->filled*sizeof(uint32_t));
        if(i>=PERF_AUDIO_GLITCHES && i<=PERF_AUDIO_SUBMIT_FAIL){n+=snprintf(report+n,sizeof(report)-n,"%s_observations=%u\n%s_total=%I64u\n",names[i],w->count,names[i],(unsigned long long)w->total);continue;}
        qsort(sorted,w->filled,sizeof(uint32_t),compare);
        const char *unit=i==PERF_INPUT_QUEUE_DEPTH || i==PERF_VIDEO_DU_DEPTH || i==PERF_AUDIO_BUFFERS || i==PERF_CACHED_FREE_SLOTS?"_count":
                         i==PERF_AUDIO_QUEUED_MS?"_ms":"_us";
        n+=snprintf(report+n,sizeof(report)-n,"%s_count=%u\n%s_current%s=%u\n%s_ema%s=%u\n%s_avg%s=%.3f\n%s_p50%s=%u\n%s_p95%s=%u\n%s_p99%s=%u\n%s_max%s=%u\n",
            names[i],w->count,names[i],unit,w->current,names[i],unit,w->ema,names[i],unit,w->count?(double)w->total/w->count:0.0,
            names[i],unit,w->filled?sorted[(w->filled*50+99)/100-1]:0,
            names[i],unit,w->filled?sorted[(w->filled*95+99)/100-1]:0,names[i],unit,w->filled?sorted[(w->filled*99+99)/100-1]:0,names[i],unit,w->maximum);
    }
    for(int i=0;i<PERF_C_COUNT && n>0 && (size_t)n<sizeof(report);i++)
        n+=snprintf(report+n,sizeof(report)-n,"%s=%I64u\n",counter_names[i],(unsigned long long)counters[i]);
    if(n>0 && (size_t)n<sizeof(report)){
        n+=snprintf(report+n,sizeof(report)-n,"units=us_except_depth_and_audio_ms; percentiles=last_128; maxima=session; sampled_network_cpu=1_of_64_packets\ncontroller_local_latency=sample_to_InputSend_handoff; not_input_to_photon\n");
        if(n>0 && (size_t)n<sizeof(report))storage_write(path,report,(size_t)n);
    }
}
static void flush_fps_request(void)
{
    static const char *stage_names[]={"stream_profile","stream_configuration","gamestream_launch_mode",
        "video_options","moonlight_streamconfig_copy","rtsp_announce_sdp_generated_maxfps"};
    char report[1024];int n=snprintf(report,sizeof(report),
        "schema=1\nmeaning=client_requested_values_only\nhost_confirmed_fps=unavailable\n");
    for(int i=0;i<PERF_FPS_STAGE_COUNT && n>0 && (size_t)n<sizeof(report);i++)
        n+=snprintf(report+n,sizeof(report)-n,
            "%s_observations=%u\n%s_fps=%d\n%s_client_refresh_x100=%d\n",
            stage_names[i],fps_stages[i].observations,stage_names[i],fps_stages[i].fps,
            stage_names[i],fps_stages[i].refresh_x100);
    if(n>0 && (size_t)n<sizeof(report))storage_write("fps-request-path.txt",report,(size_t)n);
}
static void flush_frame_trace(void)
{
    if(!capture_started_us)return;
    char report[8192];size_t used=0;
    int n=snprintf(report,sizeof(report),
        "schema=1\nstage=rtp_last_fec_block_complete_before_depacketizer\nclock=monotonic_microseconds\nwindow_us=%I64u\ncount=%u\nframe_number,complete_us,interval_us,skipped_ids\n",
        (unsigned long long)capture_duration_us,frame_trace_count);
    if(n<0 || (size_t)n>=sizeof(report))return;
    used=(size_t)n;
    uint64_t previous_time=0;uint32_t previous_frame=0;
    unsigned first=(frame_trace_next+PERF_SAMPLES-frame_trace_count)%PERF_SAMPLES;
    for(unsigned i=0;i<frame_trace_count;i++){
        unsigned at=(first+i)%PERF_SAMPLES;
        uint32_t frame=frame_trace[at].frame_number;
        uint64_t completed=frame_trace[at].completed_us;
        uint64_t interval=previous_time && completed>=previous_time?completed-previous_time:0;
        uint32_t skipped=previous_time && frame>previous_frame?frame-previous_frame-1:0;
        n=snprintf(report+used,sizeof(report)-used,"%u,%I64u,%I64u,%u\n",frame,
            (unsigned long long)completed,(unsigned long long)interval,skipped);
        if(n<0 || (size_t)n>=sizeof(report)-used)return;
        used+=(size_t)n;previous_time=completed;previous_frame=frame;
    }
    storage_write("frame-cadence.txt",report,used);
}
void perf_flush(void){
    if(capture_started_us && !capture_closed)capture_duration_us=perf_clock()-capture_started_us;
    last_capture_duration_us=capture_duration_us;
    flush_report("performance-session.txt");
    if(origin){flush_fps_request();flush_frame_trace();}
    memcpy(last_slots,slots,sizeof(last_slots));
    for(int i=0;i<PERF_C_COUNT;i++)last_counters[i]=counters[i]-(capture_started_us?capture_counter_base[i]:0);
}
void perf_flush_menu(void){flush_report("performance-menu.txt");}
