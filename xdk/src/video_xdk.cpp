/*
 * H.264 decoding with XBMC-360's FFmpeg 1.2 and presentation with Direct3D.
 *
 * Moonlight's decoder thread calls video_submit() for each frame, which
 * decodes it (slices in parallel on hardware threads 1-4) and copies the
 * picture directly into validated cached Y/U/V textures, or staging RAM in
 * the legacy fallback. The render thread (hardware thread 5) submits the newest slot and
 * draws them with the YUV shader, so waiting for the GPU or v-sync never
 * delays decoding.
 *
 * Also here: the VMX self-test at startup, an on-demand recording of the
 * first frames of a stream (moonlight\capture.bin), and the decoder benchmark
 * that replays that recording with and without the VMX code.
 */
#include "xdk.h"
#include "../../src/video_viewport.h"
#include "../../src/video_h264_diagnostic.h"
#include "../../src/h264_reference_policy.h"
#include "../../src/performance.h"

#include <malloc.h>
#include <limits.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/config.h"
#include "../../src/input.h"
#include "../../src/platform.h"
#include "../../src/video.h"
#include "video_decoder_backend.h"
#include "../../src/launch_cover.h"
#include "../../src/video_session.h"
#include "../../src/video_gpu_policy.h"
#include "../../src/video_picture.h"
#include "../../src/video_smoothing.h"
#include "../../src/refresh_trace.h"
#include "../../src/ui/ui.h"
#include "../ffmpeg_xbox/xbox_dsp.h"
#include "../stream_checks.h"
#include "../../src/stream_metrics.h"
#include "../../src/decode_deadline.h"
#include "../../src/timing.h"
#include "build_identity.h"
#ifndef DIAGNOSTIC_LEGACY_UPLOAD
#define DIAGNOSTIC_LEGACY_UPLOAD 0
#endif
#ifndef XDK_H264_REFERENCE_MODE
#define XDK_H264_REFERENCE_MODE 0
#endif
#ifndef XDK_H264_SINGLE_SLICE_REQUEST
#define XDK_H264_SINGLE_SLICE_REQUEST 0
#endif
#ifndef XDK_H264_NO_RFI
#define XDK_H264_NO_RFI 0
#endif

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/log.h>
#include "../ffmpeg_xbox/h264_parallel_diagnostic.h"
extern AVCodec ff_h264_decoder;
#if XDK_SLICE_DISPATCH_BATCHED
int ff_xbox_slice_dispatch_version(void);
#endif

/* XBMC-360's static pthreads-win32 (FFmpeg's worker threads) doesn't set
 * itself up: until this runs, its global critical sections are zero-filled
 * (which reads as "locked", so the first pthread_create waits forever). */
int ptw32_processInitialize(void);
/* This static pthreads build omits DLL attach. Its CAS dispatch is otherwise
 * NULL, first reached by mutex_trylock during slice-worker join/cleanup. */
extern LONG (WINAPI *ptw32_interlocked_compare_exchange)(LONG *, LONG, LONG);
}

#define FRAME_SLOTS      3
#define DECODE_THREADS   4   /* xb_thread.c puts worker i on hardware thread i + 1 */
#define STATS_INTERVAL_MS 2000

#define CAPTURE_FILE     "capture.bin"
#define CAPTURE_FRAMES   600                 /* 20 s at 30 FPS */
#define CAPTURE_MAX      (40 * 1024 * 1024)

struct staging {
    uint8_t *plane[3];
};

static struct video_options options = { true, RENDERER_GPU, true, false, 60, 30000, 1280, 720, 0, COLORSPACE_REC_709 };
static bool first_present_logged,last_present_shown,launch_decode_notified;
static bool freeze_first_present;
static int session_slices = 8;
static struct video_h264_diagnostic h264_diagnostic = {1,0,1,0};
static bool yuv_captured;
static bool yuv_upload_compared;
static int h264_workers(void) { return XDK_H264_SINGLE_SLICE_REQUEST || h264_diagnostic.workers == 1 ? 1 : options.decode_workers == 3 ? 3 : 4; }
/* MCC passes StreamConfig.fps to setup, not an observed/negotiated rate. */
static int setup_fps_argument;
static struct video_stats stats;
static FFH264ParallelDiagnostic parallel_totals;
static uint32_t parallel_serial_frames,parallel_parallel_frames,parallel_mixed_frames;
static uint32_t parallel_forced_serial_frames,parallel_reference_frames,parallel_nonreference_frames;
struct parallel_trace_entry {uint32_t frame,decode_us,idr;FFH264ParallelDiagnostic d;};
static parallel_trace_entry parallel_trace[64];
static uint32_t parallel_trace_next,parallel_trace_count;
static struct decode_deadline deadline, deadline_overlay;
static CRITICAL_SECTION telemetry_lock;
static struct stream_observations observations;
static struct stream_counters overlay_previous;
static DWORD overlay_last_ms;
static bool overlay_has_text, overlay_failed;
static uint32_t network_rtt, network_variance;
static int network_video_queue, network_video_queue_peak, network_audio_ms;
static bool network_valid;
static RTP_VIDEO_STATS last_rtp_stats;
static uint64_t rtp_counter_duration_us;
static uint32_t last_complete_frames,last_video_packets,last_video_bytes,last_late_packets,last_lost_frames;
static uint32_t last_data_packets,last_fec_packets;
static volatile bool telemetry_ready;

static AVCodecContext *codec_ctx;
static AVFrame *dec_frame;
static uint8_t *pkt_buf;
static int pkt_cap;

static int vid_w, vid_h, chroma_w, chroma_h;
static struct staging slots[FRAME_SLOTS];
static uint64_t legacy_ready_us[FRAME_SLOTS];
static uint32_t legacy_ready_ms[FRAME_SLOTS];
static uint32_t legacy_serial[FRAME_SLOTS],publish_serial,last_present_serial,present_duplicates;
static uint64_t legacy_picture[FRAME_SLOTS];
static uint32_t idr_present_calls,decode_no_picture;
static uint32_t smoothing_last_input;static bool smoothing_have_input;
static struct refresh_trace refresh_trace;
static struct perf_pressure pressure_at_setup;
static bool last_present_cached;
static uint64_t previous_present_start_us,last_present_start_us,last_present_return_us;
static uint64_t last_decoded_us,last_presented_us;
static uint32_t present_age_sample_ordinal;
static uint32_t last_packet_age_stop_ms,last_decode_age_stop_ms,last_present_age_stop_ms;
struct present_trace_entry {uint64_t start_us,return_us;uint32_t serial,frame;uint8_t cached,idr;};
static present_trace_entry present_trace[64];
static uint32_t present_trace_next,present_trace_count;
static CRITICAL_SECTION mailbox_lock;
static int slot_pending = -1, slot_rendering = -1;
static HANDLE frame_event, render_thread;
static volatile bool running;
static bool need_idr;
static volatile LONG refresh_active,refresh_state,refresh_requests,refresh_decoded_idrs;
static volatile LONG refresh_request_lock;
static volatile LONG refresh_completed_request;
/* Never reset at resize/start: delayed pictures must not alias a new session's
 * request counter after video_start() resets the user-facing statistics. */
static volatile LONG refresh_generation;
static uint32_t idr_received,idr_decoded;
static volatile LONG refresh_last_idr_ms,refresh_requested_ms,refresh_last_request_ms;

static bool request_picture_refresh(DWORD now,DWORD cooldown_ms)
{
    if(!InterlockedCompareExchange(&refresh_active,0,0))return false;
    if(InterlockedCompareExchange(&refresh_request_lock,1,0)!=0)return false;
    LONG state=InterlockedCompareExchange(&refresh_state,0,0);
    DWORD last=(DWORD)InterlockedCompareExchange(&refresh_last_request_ms,0,0);
    if(state==VIDEO_REFRESH_REQUESTED || state==VIDEO_REFRESH_WAITING ||
       (last && (DWORD)(now-last)<cooldown_ms)){
        InterlockedExchange(&refresh_request_lock,0);
        return false;
    }
    InterlockedExchange(&refresh_requested_ms,(LONG)now);
    InterlockedExchange(&refresh_last_request_ms,(LONG)now);
    InterlockedIncrement(&refresh_generation);
    InterlockedExchange(&refresh_state,VIDEO_REFRESH_REQUESTED);
    InterlockedIncrement(&refresh_requests);
    InterlockedExchange(&refresh_request_lock,0);
    /* MCC signals its control stream; no FFmpeg state is touched here. */
    LiRequestIdrFrame();
    return true;
}
bool video_request_picture_refresh(void)
{
    return request_picture_refresh(GetTickCount(),3000);
}
void video_refresh_tick(uint32_t now_ms)
{
    if(!InterlockedCompareExchange(&refresh_active,0,0))return;
    LONG state=InterlockedCompareExchange(&refresh_state,0,0);
    DWORD requested=(DWORD)InterlockedCompareExchange(&refresh_requested_ms,0,0);
    /* Decoder never waits for the request lock. Finish a deferred observation
     * here on the input owner if request admission overlapped decode output. */
    if((state==VIDEO_REFRESH_REQUESTED || state==VIDEO_REFRESH_WAITING) &&
       InterlockedCompareExchange(&refresh_completed_request,0,0)==
       InterlockedCompareExchange(&refresh_generation,0,0)) {
        InterlockedCompareExchange(&refresh_state,VIDEO_REFRESH_DECODED,state);
        state=InterlockedCompareExchange(&refresh_state,0,0);
    }
    if(state==VIDEO_REFRESH_REQUESTED && (DWORD)(now_ms-requested)>=150)
        InterlockedCompareExchange(&refresh_state,VIDEO_REFRESH_WAITING,VIDEO_REFRESH_REQUESTED);
    else if(state==VIDEO_REFRESH_WAITING && (DWORD)(now_ms-requested)>=4000)
        InterlockedCompareExchange(&refresh_state,VIDEO_REFRESH_UNAVAILABLE,VIDEO_REFRESH_WAITING);
    DWORD last=(DWORD)InterlockedCompareExchange(&refresh_last_request_ms,0,0);
    if(options.refresh_interval_ms>0 && state!=VIDEO_REFRESH_REQUESTED &&
       state!=VIDEO_REFRESH_WAITING &&
       (DWORD)(now_ms-last)>=(DWORD)options.refresh_interval_ms)
        request_picture_refresh(now_ms,0);
}
void video_get_refresh_snapshot(struct video_refresh_snapshot *out)
{
    out->state=(int)InterlockedCompareExchange(&refresh_state,0,0);
    out->requests=(uint32_t)InterlockedCompareExchange(&refresh_requests,0,0);
    out->decoded_idrs=(uint32_t)InterlockedCompareExchange(&refresh_decoded_idrs,0,0);
    out->last_idr_ms=(uint32_t)InterlockedCompareExchange(&refresh_last_idr_ms,0,0);
}
static void video_refresh_decoded_idr(uint32_t now_ms,uint32_t request)
{
    InterlockedIncrement(&refresh_decoded_idrs);
    InterlockedExchange(&refresh_last_idr_ms,(LONG)now_ms);
    InterlockedExchange(&refresh_completed_request,(LONG)request);
    /* A delayed pre-request picture must not complete a newer refresh. Never
     * wait on the input thread: if it is admitting a request, leave it pending. */
    if(InterlockedCompareExchange(&refresh_request_lock,1,0)!=0)return;
    LONG state=InterlockedCompareExchange(&refresh_state,0,0);
    if(request==(uint32_t)InterlockedCompareExchange(&refresh_generation,0,0) &&
       (state==VIDEO_REFRESH_REQUESTED || state==VIDEO_REFRESH_WAITING))
        InterlockedCompareExchange(&refresh_state,VIDEO_REFRESH_DECODED,state);
    InterlockedExchange(&refresh_request_lock,0);
}
static volatile LONG decoder_reset_requested;
static volatile LONG stream_error;
int video_get_stream_error(void) { return InterlockedCompareExchange(&stream_error,0,0); }

/* Two texture sets: the GPU reads one while the next frame fills the other. */
static D3DTexture *tex[2][3];
static DWORD tex_fence[2];
static uint32_t legacy_fence_submitted_ms[2];
static int tex_next;

enum { PIPE_AUTO, PIPE_CACHED, PIPE_LEGACY, PIPE_SWITCHING, PIPE_DISABLED };
static volatile LONG active_pipeline=PIPE_LEGACY;
static int requested_pipeline, requested_shader;
static bool shader_diagnostic_override;
static const char *pipeline_name(LONG mode)
{
    switch(mode) {
    case PIPE_CACHED:return "CPU_CACHED_TEXTURE";
    case PIPE_LEGACY:return "LEGACY_UPLOAD";
    case PIPE_SWITCHING:return "SWITCHING_TO_LEGACY";
    case PIPE_DISABLED:return "DISABLED_AFTER_ALLOCATION_FAILURE";
    default:return "AUTO";
    }
}
static struct gpu_quality_policy quality_policy;
enum { TIME_DECODE, TIME_FRAME_COPY, TIME_TEXTURE_LOCK, TIME_TEXTURE_COPY,
       TIME_GPU_SUBMIT, TIME_FENCE_WAIT, TIME_PRESENT_WAIT,
       TIME_PACKET_PREPARE, TIME_CALLBACK_TOTAL, TIME_CALLBACK_GAP, TIME_COUNT };
static struct gpu_time_window pipeline_times[TIME_COUNT];
static uint32_t pipeline_ema[TIME_COUNT],pipeline_peak[TIME_COUNT],pipeline_calls[TIME_COUNT];
static uint64_t pipeline_total[TIME_COUNT];
static uint64_t streaming_start_us,streaming_end_us;
static uint64_t submit_last_end_us;
static unsigned gpu_slot_starvation, cached_failovers;
static LONG current_pipeline(void) { return InterlockedCompareExchange(&active_pipeline,0,0); }
void video_get_progress(struct video_progress_snapshot *out)
{
    LONG mode=current_pipeline();
    if(mode==PIPE_LEGACY) {
        EnterCriticalSection(&mailbox_lock);
        if(slot_pending>=0)video_progress_queue_age(GetTickCount()-legacy_ready_ms[slot_pending]);
        LeaveCriticalSection(&mailbox_lock);
    }
    unsigned ready=0,in_flight=0;
    if(mode==PIPE_CACHED)cached_video_snapshot(&ready,&in_flight);
    else if(mode==PIPE_LEGACY)for(int i=0;i<2;i++)if(gfx_fence_pending(tex_fence[i])) {
        in_flight++;
        if(legacy_fence_submitted_ms[i])
            video_progress_gpu_wait(GetTickCount()-legacy_fence_submitted_ms[i]);
    }
    video_progress_snapshot_get(out);
    out->cached_ready=ready;out->cached_in_flight=in_flight;
    out->gpu_slot_capacity=mode==PIPE_CACHED?3u:mode==PIPE_LEGACY?2u:0u;
}
unsigned video_recover_frame_queue(void)
{
    /* READY slots only. IN_FLIGHT and WRITING remain under their owners. */
    unsigned dropped=current_pipeline()==PIPE_CACHED?cached_video_prune_ready():0;
    if(dropped)InterlockedExchangeAdd((volatile LONG*)&stats.dropped,dropped);
    if(frame_event)SetEvent(frame_event);
    return dropped;
}
void video_request_decoder_reset(void){InterlockedExchange(&decoder_reset_requested,1);}
static void pipeline_sample(int metric,uint64_t us) {
    if (!options.stats_overlay) return;
    uint32_t value=us>UINT_MAX?UINT_MAX:(uint32_t)us;
    EnterCriticalSection(&telemetry_lock);
    gpu_time_add(&pipeline_times[metric],value);
    pipeline_ema[metric]=pipeline_calls[metric]?stream_smooth(pipeline_ema[metric],value):value;
    pipeline_calls[metric]++;pipeline_total[metric]+=value;
    if(value>pipeline_peak[metric])pipeline_peak[metric]=value;
    LeaveCriticalSection(&telemetry_lock);
}

static LARGE_INTEGER qpc_freq;

/* Periodic stats in the debug log. */
static DWORD stats_last_ms;
static struct video_stats stats_last;
static uint64_t decode_us_sum;

/* Recording of the stream for the benchmark: "MLC1", width, height, count,
 * then per frame a 32-bit length and the H.264 access unit. Only armed when
 * requested from the benchmark screen so normal streaming pays zero cost. */
static uint8_t *cap_buf;
static uint32_t cap_len, cap_frames;
static bool cap_armed;

bool video_xdk_is_streaming(void)
{
    return running;
}

static void vlog(const char *fmt, ...)
{
    char line[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    plat_log(line);
}

static uint64_t now_us(void)
{
    LARGE_INTEGER t;

    QueryPerformanceCounter(&t);
    return moonx_ticks_to_us((uint64_t)t.QuadPart,(uint64_t)qpc_freq.QuadPart);
}

/* Overlay-only wall times. Gap includes MCC cleanup/waiting, scheduling and
 * unavailable input; it must NOT be labeled network latency or GPU time.
 * Scope exit records every ordinary return, including DR_NEED_IDR. */
class submit_timing_scope {
    bool enabled;
    uint64_t start;
public:
    submit_timing_scope() : enabled(options.stats_overlay), start(0) {
        video_progress_note(VP_CALLBACK_ENTERED);
        if (!enabled) return;
        start=now_us();
        if (submit_last_end_us && start>=submit_last_end_us)
            pipeline_sample(TIME_CALLBACK_GAP,start-submit_last_end_us);
    }
    ~submit_timing_scope() {
        if (enabled) {
            uint64_t end=now_us();
            if (end>=start) pipeline_sample(TIME_CALLBACK_TOTAL,end-start);
            /* Exclude our trailing aggregation cost from the NEXT callback gap. */
            submit_last_end_us=now_us();
        }
        video_progress_note(VP_CALLBACK_RETURNED);
    }
};

const char *video_renderer_name(int renderer)
{
    (void)renderer;
    return "GPU";
}

void video_set_options(const struct video_options *opts)
{
    /* app.c configures before video_init_callbacks / connection. A connected
     * decoder must not change its advertised slices or renderer matrix. */
    if (running) return;
    options = *opts;
    xdk_set_thread_layout(opts->thread_layout);
    session_slices = video_session_slices(opts);
    /* Isolated reference-safe experiment. The current host emits IDC 0, so
     * this decoder has already been forced to serial reconstruction. Request
     * one encoded slice and avoid unused slice workers without changing the
     * normative filter. The host may ignore the request; reports record the
     * actual parsed slice count. */
#if XDK_H264_SINGLE_SLICE_REQUEST
    session_slices = 1;
#endif
}

int video_colorspace(int width, int height)
{
    return video_hd_colorspace(width, height);
}

void video_get_display_size(int *w, int *h)
{
    gfx_display_size(w,h);
}

void video_get_stats(struct video_stats *out)
{
    *out = stats;
}
void video_get_quick_info(struct video_quick_info *out)
{
    memset(out,0,sizeof(*out));
    out->requested_w=options.width;out->requested_h=options.height;
    out->requested_fps=options.target_fps;out->bitrate_kbps=options.bitrate_kbps;
    out->slices=session_slices;out->renderer=options.renderer;
    out->fast_decode=options.fast_decode;
    out->shader_mode=gpu_shader_fast(requested_shader,gfx_quality_available(),&quality_policy)?1:2;
    out->decode_workers=stats.decode_threads;
    snprintf(out->host_name,sizeof(out->host_name),"%s",options.host_name);
    snprintf(out->pipeline,sizeof(out->pipeline),"%s",pipeline_name(current_pipeline()));
    EnterCriticalSection(&telemetry_lock);
    out->network_valid=network_valid;out->rtt_ms=network_rtt;
    out->variance_ms=network_variance;
    out->queue_age_ms=deadline_overlay.queue_valid?deadline_overlay.queue_us/1000:0;
    LeaveCriticalSection(&telemetry_lock);
}

void video_set_network_stats(uint32_t rtt, uint32_t variance, bool valid,
                             int video_queue, int audio_queue_ms)
{
    EnterCriticalSection(&telemetry_lock);
    network_rtt = rtt;
    network_variance = variance;
    network_valid = valid;
    network_video_queue = video_queue;
    if (video_queue>network_video_queue_peak) network_video_queue_peak=video_queue;
    network_audio_ms = audio_queue_ms;
    /* app.c only calls this after its connecting UI task has finished. This
     * prevents the optional overlay and busy screen sharing the font cache. */
    telemetry_ready = true;
    LeaveCriticalSection(&telemetry_lock);
}

#define SELFTEST_REPORT_BYTES 16384
static char *selftest_report;
static size_t selftest_report_used;

static LONG WINAPI pthread_compare_exchange(LONG *target, LONG value, LONG comparand)
{
    return InterlockedCompareExchange(target, value, comparand);
}

static void selftest_log(const char *line)
{
    /* Startup only. Keep a completed checkpoint if the next native kernel
     * faults before the accumulated final report can be saved. */
    if (storage_dir()) storage_write("vmx-progress.txt", line, strlen(line));
    if (selftest_report) {
        size_t n = strlen(line), available = SELFTEST_REPORT_BYTES - 1 - selftest_report_used;
        if (n > available) n = available;
        memcpy(selftest_report + selftest_report_used, line, n);
        selftest_report_used += n;
        selftest_report[selftest_report_used] = 0;
    }
    plat_log(line);
}

void video_xdk_init(void)
{
    /* Install the Xenon atomic before any pthread initialization or worker.
     * The legacy fallback in this tree only implements x86 instructions. */
    ptw32_interlocked_compare_exchange = pthread_compare_exchange;
    selftest_log("video init: pthread process initialization begin\n");
    ptw32_processInitialize();
    selftest_log("video init: codec registration begin\n");
    avcodec_register(&ff_h264_decoder);
    av_log_set_level(AV_LOG_QUIET);
    InitializeCriticalSection(&mailbox_lock);
    InitializeCriticalSection(&telemetry_lock);
    frame_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    QueryPerformanceFrequency(&qpc_freq);

    /* Only VMX functions that match FFmpeg's C code exactly get installed. */
    selftest_report = (char *)malloc(SELFTEST_REPORT_BYTES);
    selftest_report_used = 0;
    selftest_log("video init: native DSP selftest begin\n");
    ff_xbox_dsp_selftest(selftest_log);
    /* Native replay: LF SIMD matched C but increased p99/max. Keep all
     * candidates available to the benchmark, and preserve the original C
     * full-filter path in streaming until a tail-latency win is measured. */
    ff_xbox_vmx_flags = XVMX_ALL & ~XVMX_LOOPFILTER;
    ff_xbox_loopfilter_mask = 0;
    selftest_log("streaming: C loop filter selected by native latency results; VMX LF candidates remain benchmarkable\n");
    /* Startup only: diagnostics must survive enabling the RAM log later in
     * app_main. No report allocation or disk I/O is added to streaming. */
    if (selftest_report && selftest_report_used && storage_dir())
        if (!storage_write("vmx-selftest.txt", selftest_report, selftest_report_used))
            plat_log("VMX selftest report could not be saved\n");
    free(selftest_report);
    selftest_report = NULL;
}

static AVCodecContext *open_h264(int width, int height, int threads, bool fast_decode)
{
#if XDK_SLICE_DISPATCH_BATCHED
    if (ff_xbox_slice_dispatch_version()!=2) return NULL;
#endif
    AVCodecContext *c = avcodec_alloc_context3(&ff_h264_decoder);

    if (c == NULL)
        return NULL;
    c->flags |= CODEC_FLAG_LOW_DELAY;
    if (h264_reference_fast_flag(XDK_H264_REFERENCE_MODE,h264_diagnostic.fast_flag,fast_decode))
        c->flags2 |= CODEC_FLAG2_FAST;
    else c->flags2 &= ~CODEC_FLAG2_FAST;
    /* This fork only tracks incomplete macroblocks when error recognition is on. */
    c->err_recognition = AV_EF_CAREFUL;
    c->skip_frame = AVDISCARD_DEFAULT;
    c->skip_idct = AVDISCARD_DEFAULT;
    /* The host receives a session-specific slice request (see capabilities),
     * which the worker threads decode in parallel. No added latency. */
    c->thread_type = FF_THREAD_SLICE;
    c->thread_count = threads;
    c->width = width;
    c->height = height;
    c->pix_fmt = AV_PIX_FMT_YUV420P;
    c->skip_loop_filter = (enum AVDiscard)h264_reference_filter(XDK_H264_REFERENCE_MODE,fast_decode,false,
                                                AVDISCARD_DEFAULT,AVDISCARD_NONREF,AVDISCARD_ALL);
    if (avcodec_open2(c, &ff_h264_decoder, NULL) < 0) {
        av_free(c);
        return NULL;
    }
    return c;
}

static void close_h264(AVCodecContext **c)
{
    if (*c) {
        plat_shutdown_stage("H.264 avcodec_close begin\n");
        avcodec_close(*c);
        plat_shutdown_stage("H.264 avcodec_close done; freeing context\n");
        av_free(*c);
        *c = NULL;
        plat_shutdown_stage("H.264 context freed\n");
    }
}

/* ---------------------------------------------------- render thread */

static void overlay_line(char *out,size_t capacity,const char *format,...)
{
    size_t used=strlen(out);
    if(used+2>=capacity)return;
    if(used)out[used++]='\n';
    va_list args;va_start(args,format);
    vsnprintf(out+used,capacity-used,ui_translate(format),args);
    va_end(args);out[capacity-1]=0;
}
static bool overlay_text(char *out,size_t capacity)
{
    if(!options.stats_overlay || !out || !capacity)return false;
    struct stream_observations m;struct decode_deadline d;
    struct stream_counters current;struct stream_rates rates;
    struct gpu_time_window decode_window;
    uint32_t ema[TIME_COUNT],rtt,variance;
    DWORD now=GetTickCount(),elapsed=now-overlay_last_ms;
    int video_queue,audio_ms,bb_w,bb_h;
    bool valid;char queue_age[32],host[32],network[32];
    double budget=1000.0/(options.target_fps>0?options.target_fps:30);
    if(overlay_has_text && elapsed<1000)return false;
    EnterCriticalSection(&telemetry_lock);
    m=observations;d=deadline_overlay;rtt=network_rtt;variance=network_variance;valid=network_valid;
    video_queue=network_video_queue;audio_ms=network_audio_ms;
    decode_window=pipeline_times[TIME_DECODE];memcpy(ema,pipeline_ema,sizeof(ema));
    LeaveCriticalSection(&telemetry_lock);
    current.received=stats.received;current.decoded=stats.decoded;current.shown=stats.rendered;current.payload_bytes=m.payload_bytes;
    rates=stream_rates_between(&overlay_previous,&current,elapsed);
    int mode=options.overlay_mode>=1 && options.overlay_mode<=3?options.overlay_mode:3;
    /* One snapshot/sort at 1 Hz, outside the producer lock. Basic needs no sort. */
    double p95=mode>1?gpu_time_percentile(&decode_window,95)/1000.0:0;
    double p99=mode>1?gpu_time_percentile(&decode_window,99)/1000.0:0;
    if(d.queue_valid)snprintf(queue_age,sizeof(queue_age),"%.1f ms",d.queue_us/1000.0);else snprintf(queue_age,sizeof(queue_age),"%s",ui_translate("N/A"));
    if(m.have_host)snprintf(host,sizeof(host),"%.1f ms",m.host_us/1000.0);else snprintf(host,sizeof(host),"%s",ui_translate("N/A"));
    if(valid)snprintf(network,sizeof(network),"%u ms",rtt);else snprintf(network,sizeof(network),"%s",ui_translate("N/A"));
    const char *status=stats.decoded<30?"Measuring...":
        stats.avg_decode_us/1000.0>budget?"Decoder overloaded":
        (d.queue_valid && d.queue_us>budget*1000.0) || video_queue>1?"Video is waiting":
        rates.shown+1.0<options.target_fps?"Below target FPS":"Keeping up";
    const char *filter;
    const char *fast_status;
    filter=options.fast_decode ? "Fast" : d.recovery ? "Reduced (recovery)" : "Full";
    fast_status=fast_decode_name(options.fast_decode);
    out[0]=0;
    overlay_line(out,capacity,"Stream: %dx%d | %.1f / %d FPS",vid_w,vid_h,rates.shown,options.target_fps);
    overlay_line(out,capacity,"Bitrate: %.2f / %.2f Mbps (actual / set)",rates.payload_mbps,options.bitrate_kbps/1000.0);
    overlay_line(out,capacity,"Status: %s",ui_translate(status));
    overlay_line(out,capacity,"H.264 filter: %s | Fast Decode: %s",ui_translate(filter),ui_translate(fast_status));
    overlay_line(out,capacity,"GPU image: %s | Active: %s%s",
        ui_translate(options.shader_mode==1?"Fast":options.shader_mode==2?"Quality":"Auto Quality"),
        ui_translate(gpu_shader_fast(requested_shader,gfx_quality_available(),&quality_policy)?"Fast":"Quality"),
        shader_diagnostic_override?ui_translate(" (diagnostic)"):"");
    if(mode>1) {
        overlay_line(out,capacity,"FPS requested / negotiated: %d / unknown",options.target_fps);
        overlay_line(out,capacity,"FPS callback / decoded / presented: %.1f / %.1f / %.1f",rates.received,rates.decoded,rates.shown);
        overlay_line(out,capacity,"Decode: %.2f ms | Budget: %.2f ms",stats.avg_decode_us/1000.0,budget);
        overlay_line(out,capacity,"Network RTT: %s | Video queue: %s",network,queue_age);
        overlay_line(out,capacity,"Skipped display: %u | Decode errors: %u",stats.dropped,stats.decode_errors);
        overlay_line(out,capacity,"Audio queue: %d ms | Host: %s",audio_ms,host);
    }
    if(mode==3) {
        gfx_backbuffer_size(&bb_w,&bb_h);
        overlay_line(out,capacity,"Setup FPS argument: %d | V-Sync: %s",setup_fps_argument,options.vsync?"On":"Off");
        overlay_line(out,capacity,"Decode p95 / p99 (last 256): %.2f / %.2f ms",p95,p99);
        overlay_line(out,capacity,"Workers: %d | Slices: %d | Recovery: %u",stats.decode_threads,session_slices,d.recoveries);
        overlay_line(out,capacity,"Full / reduced frames: %u / %u",d.full_frames,d.fast_frames);
        overlay_line(out,capacity,"Pipeline %s | Copy: %.2f ms",pipeline_name(current_pipeline()),ema[TIME_FRAME_COPY]/1000.0);
        overlay_line(out,capacity,"CPU submit / fence check / Present call: %.2f / %.2f / %.2f ms",ema[TIME_GPU_SUBMIT]/1000.0,ema[TIME_FENCE_WAIT]/1000.0,ema[TIME_PRESENT_WAIT]/1000.0);
        overlay_line(out,capacity,"Display: %dx%d / %.2f Hz",bb_w,bb_h,gfx_display_hz());
        overlay_line(out,capacity,"Video backlog: %d frames | GPU busy: %u",video_queue,gpu_slot_starvation);
    }
    overlay_previous=current;overlay_last_ms=now;overlay_has_text=true;return true;
}

static bool create_legacy_video(void)
{
    bool ok=true;int i,k;
    for (i=0;i<FRAME_SLOTS;i++) for (k=0;k<3;k++) {
        int w=k?chroma_w:vid_w,h=k?chroma_h:vid_h;
        slots[i].plane[k]=(uint8_t*)_aligned_malloc((size_t)w*h,128);
        ok=ok && slots[i].plane[k]!=NULL;
    }
    D3DDevice *d=gfx_begin();
    for (i=0;i<2 && ok;i++) for (k=0;k<3 && ok;k++)
        ok=SUCCEEDED(d->CreateTexture(k?chroma_w:vid_w,k?chroma_h:vid_h,1,0,D3DFMT_LIN_L8,0,&tex[i][k],NULL));
    gfx_end();return ok;
}

static void activate_legacy_fallback(void)
{
    D3DDevice *d=gfx_begin();cached_video_cleanup(d);gfx_end();
    bool fallback_ok=create_legacy_video();
    InterlockedExchange(&active_pipeline,fallback_ok?PIPE_LEGACY:PIPE_DISABLED);
    vlog(fallback_ok?"video: switched to legacy upload\n":"video: both presentation allocations failed\n");
}

static DWORD present_video(D3DDevice *d,D3DTexture **planes,int cached_slot,
                           const char *text,bool update,uint64_t ready_us,uint32_t serial,uint64_t picture=0)
{
    last_present_shown=false;
    bool available=gfx_quality_available();
    bool clarity=options.crt_clarity && vid_h<=480 && gfx_crt_clarity_available();
    bool control=requested_shader==0 && available && !quality_policy.tested && !clarity && !options.refresh_smoothing;
    bool measure=options.stats_overlay || control || perf_enabled();
    uint64_t t=measure?now_us():0;
    int i;D3DTexture *history[3]={NULL,NULL,NULL};float previous_weight=0;int smoothing_history=-1;
    if(options.refresh_smoothing && (launch_active() || gfx_launch_pending()))cached_video_invalidate_history();
    if(options.refresh_smoothing && cached_slot>=0 && gfx_smoothing_available(clarity))
        smoothing_history=cached_video_prepare_smoothing(cached_slot,options.smoothing_strength,history,&previous_weight);
    bool temporal=previous_weight>0 && history[0] && history[0]!=planes[0];
    bool fast=gpu_shader_fast(requested_shader,available,&quality_policy);
    for (i=0;i<3;i++) d->SetTexture(i,planes[i]);
    if(temporal) {
        for(i=0;i<3;i++)d->SetTexture(i+3,history[i]);
        float transition[4];smoothing_constants(previous_weight,transition);d->SetPixelShaderConstantF(2,transition,1);
    }
    /* Rebind constants with the shader: RGB UI/overlay shares this device. */
    float coefficients[4],texel[4]={1.0f/vid_w,1.0f/vid_h,0,0};
    video_yuv_coefficients(options.color_space,coefficients);
    d->SetPixelShaderConstantF(0,coefficients,1);d->SetPixelShaderConstantF(1,texel,1);
    int bw,bh,left,top,right,bottom;
    gfx_backbuffer_size(&bw,&bh);
    video_fit_rect(vid_w,vid_h,bw,bh,&left,&top,&right,&bottom);
    if(left || top || right!=bw || bottom!=bh)
        d->Clear(0,NULL,D3DCLEAR_TARGET,0xff000000,1.0f,0);
    int shader=clarity?GFX_SHADER_CRT_CLARITY:fast?GFX_SHADER_YUV_FAST:GFX_SHADER_YUV_QUALITY;
    if(options.refresh_smoothing)shader=temporal?(clarity?GFX_SHADER_YUV_SMOOTH_CRT:GFX_SHADER_YUV_SMOOTH):
        smoothing_history==-2 && clarity?GFX_SHADER_CRT_CLARITY:GFX_SHADER_YUV_FAST;
    gfx_draw_rect(d,shader,
                  (float)left*GFX_WIDTH/bw,(float)top*GFX_HEIGHT/bh,
                  (float)right*GFX_WIDTH/bw,(float)bottom*GFX_HEIGHT/bh);
    if(temporal)cached_video_smoothing_drawn();
    for (i=0;i<(temporal?6:3);i++) d->SetTexture(i,NULL);
    if (measure) {
        uint64_t submit=now_us()-t;
        pipeline_sample(TIME_GPU_SUBMIT,submit);
        perf_record(PERF_CPU_SUBMIT,submit);
        if (control) gpu_quality_sample(&quality_policy,(uint32_t)submit);
    }
    if (options.stats_overlay && input_stats_visible() && telemetry_ready && !overlay_failed) {
        if (update && !gfx_overlay_set(text)) { gfx_overlay_clear(d);overlay_failed=true; }
        if (!overlay_failed) gfx_overlay_draw(d);
    }
    gfx_mouse_draw(d);
    gfx_stream_menu_draw(d,stats.decoded,stats.rendered,stats.dropped,stats.avg_decode_us,options.stats_overlay);
    if(gfx_launch_pending())gfx_launch_draw(d,true);
    DWORD fence=d->InsertFence();
    video_progress_note(VP_GPU_SUBMITTED);
    gfx_stream_menu_submitted(fence); /* AFTER all three textures have been unbound */
    if (cached_slot>=0) cached_video_submitted(cached_slot,fence);
    if (options.stats_overlay) gfx_overlay_mark_used(fence);
    t=options.stats_overlay?now_us():0;
    if(!gfx_system_ui_active()) {
        uint64_t present_started=perf_enabled()?perf_clock():0;
        if(present_started) {
            if(previous_present_start_us && present_started>=previous_present_start_us)
                perf_record(PERF_PRESENT_INTERVAL,present_started-previous_present_start_us);
            if(ready_us && present_started>=ready_us)
                perf_record(PERF_FRAME_READY_PRESENT,present_started-ready_us);
            previous_present_start_us=last_present_start_us=present_started;
        }
        if(last_present_serial==serial && serial && last_present_cached==(cached_slot>=0))present_duplicates++;
        last_present_serial=serial;
        last_present_cached=cached_slot>=0;
        if(!freeze_first_present)
            plat_diagnostics_checkpoint("[FREEZE] first video Present entered\n");
        video_progress_note(VP_PRESENT_CALLED);
        d->Present(NULL,NULL,NULL,NULL);
        if(picture & 1)idr_present_calls++; /* CPU Present call, not scanout proof */
        /* Every call consumed a newly selected slot, even when a resize or
         * cached-to-legacy switch restarted that slot's local serial. */
        video_progress_new_present();
        video_progress_note(VP_PRESENT_COMPLETED);
        if(perf_video_tracking_enabled() && (!last_presented_us || (++present_age_sample_ordinal&63)==0))
            last_presented_us=now_us();
        if(!freeze_first_present) {
            freeze_first_present=true;
            plat_diagnostics_checkpoint("[FREEZE] first video Present returned\n");
        }
        if(present_started){
            last_present_return_us=perf_clock();
            perf_record(PERF_PRESENT_CALL,last_present_return_us-present_started);
            present_trace_entry *entry=&present_trace[present_trace_next];
            entry->start_us=present_started;entry->return_us=last_present_return_us;
            entry->serial=serial;entry->cached=cached_slot>=0;entry->frame=(uint32_t)picture>>1;entry->idr=(uint8_t)(picture&1);
            present_trace_next=(present_trace_next+1)%64;
            if(present_trace_count<64)present_trace_count++;
        }
        last_present_shown=true;
        if(!first_present_logged && !launch_active()){first_present_logged=true;perf_mark(PERF_FIRST_PRESENTED);vlog("[NET] total start-to-video: %u ms\n",GetTickCount()-options.connection_started_ms);}
    }
    if(cached_slot>=0)cached_video_presented(cached_slot,last_present_shown);
    if (options.stats_overlay) pipeline_sample(TIME_PRESENT_WAIT,now_us()-t);
    return fence;
}

static int draw_cached_frame(void)
{
    if(gfx_system_ui_active())return 0;
    /* A timeout is not a frame. Avoid taking D3D ownership (and preparing the
     * overlay) when nothing can be drawn. A later publish leaves frame_event
     * signalled for the next wait; latest-frame selection still happens only
     * after acquiring D3D, so this peek never pins an older frame. */
    if(!cached_video_has_ready())return 0;
    char text[1536];
    bool update=options.stats_overlay && telemetry_ready && !overlay_failed && overlay_text(text,sizeof(text));
    uint64_t acquire_start=perf_enabled()?perf_clock():0;
    D3DDevice *d=gfx_begin();
    if(acquire_start)perf_record(PERF_D3D_ACQUIRE,perf_clock()-acquire_start);
    D3DTexture *planes[3];unsigned discarded=0;
    int slot=cached_video_take(planes,&discarded);
    if (discarded) { InterlockedExchangeAdd((volatile LONG*)&stats.dropped,discarded);stats.render_superseded+=discarded; }
    if (slot<0) { gfx_end();return 0; }
    video_progress_note(VP_SELECTED);
    present_video(d,planes,slot,text,update,cached_video_ready_time(slot),cached_video_frame_serial(slot),cached_video_picture(slot));gfx_end();return last_present_shown?1:0;
}

static bool draw_frame(int slot)
{
    char text[1536];
    bool update = options.stats_overlay && telemetry_ready && !overlay_failed && overlay_text(text, sizeof(text));
    D3DDevice *d = gfx_begin();
    int k = tex_next;
    bool uploaded;
    const struct staging *s;
    uint64_t timing = options.stats_overlay ? now_us() : 0;

    if(gfx_fence_pending(tex_fence[k]) && !gfx_fence_pending(tex_fence[k^1]))k^=1;
    tex_next = k ^ 1;
    if(gfx_system_ui_active() || gfx_fence_pending(tex_fence[k])) {
        EnterCriticalSection(&mailbox_lock);slot_rendering=-1;LeaveCriticalSection(&mailbox_lock);
        gfx_end();return false;
    }
    if (options.stats_overlay) {
        uint64_t finished = now_us();
        stats.avg_gpu_wait_us = stream_smooth(stats.avg_gpu_wait_us, (uint32_t)(finished - timing));
        pipeline_sample(TIME_FENCE_WAIT,finished-timing);
        timing = finished;
    }
    /* A newer decoded picture may have arrived while acquiring the device or
     * waiting for this texture set. Select it BEFORE upload, under the same
     * ownership lock as publish. The old rendering slot is now free; neither
     * slot has been read by the GPU (only independent textures are sampled).
     * This bounds display age without dropping compressed reference pictures. */
    EnterCriticalSection(&mailbox_lock);
    if (slot_pending >= 0) {
        slot = slot_pending;
        slot_pending = -1;
        slot_rendering = slot;
        InterlockedIncrement((volatile LONG *)&stats.dropped);
        stats.render_superseded++;
    }
    LeaveCriticalSection(&mailbox_lock);
    s = &slots[slot];
    video_progress_queue_age(GetTickCount()-legacy_ready_ms[slot]);
    struct gfx_copy_cost cost={0,0};
    uint64_t upload_started=perf_enabled()?perf_clock():0;
    video_progress_note(VP_UPLOAD_STARTED);
    uploaded = gfx_upload_timed(tex[k][0], s->plane[0], vid_w, vid_w, vid_h, options.stats_overlay?&cost:NULL) &&
               gfx_upload_timed(tex[k][1], s->plane[1], chroma_w, chroma_w, chroma_h, options.stats_overlay?&cost:NULL) &&
               gfx_upload_timed(tex[k][2], s->plane[2], chroma_w, chroma_w, chroma_h, options.stats_overlay?&cost:NULL);
    if(upload_started)perf_record(PERF_LEGACY_TEXTURE_UPLOAD,perf_clock()-upload_started);
    pipeline_sample(TIME_TEXTURE_LOCK,cost.lock_us);pipeline_sample(TIME_TEXTURE_COPY,cost.copy_us);

    if (options.stats_overlay)
        stats.avg_upload_us = stream_smooth(stats.avg_upload_us, (uint32_t)(now_us() - timing));

    /* Snapshot metadata before releasing producer ownership. The producer may
     * immediately reuse this staging slot while we submit independent textures. */
    uint64_t ready_us=legacy_ready_us[slot],picture=legacy_picture[slot];
    uint32_t serial=legacy_serial[slot];
    /* The staging slot can be refilled now. */
    EnterCriticalSection(&mailbox_lock);
    slot_rendering = -1;
    LeaveCriticalSection(&mailbox_lock);

    if (!uploaded) {
        gfx_end();
        return false;
    }
    video_progress_note(VP_UPLOADED);

    legacy_fence_submitted_ms[k]=GetTickCount();
    tex_fence[k]=present_video(d,tex[k],-1,text,update,ready_us,serial,picture);
    gfx_end();
    return last_present_shown;
}

/* One wake can present video OR the idle opening, never both. A decoded frame
 * can become READY after a timeout, including while the opening is drawing. */
static void render_tick(DWORD wake)
{
    video_progress_note(VP_RENDER_ITERATION);
    if(gfx_system_ui_active()) {
        if(options.refresh_smoothing)cached_video_invalidate_history();
        /* Guide heartbeat stays on the established D3D owner. */
        gfx_present_system_ui();return;
    }
    if(current_pipeline()==PIPE_SWITCHING)activate_legacy_fallback();
    bool presented=false;
    if(current_pipeline()==PIPE_CACHED) {
        uint64_t t0=now_us();
        presented=draw_cached_frame()>0;
        if(presented) {
            stats.rendered++;
            stats.avg_render_us=stream_smooth(stats.avg_render_us,(uint32_t)(now_us()-t0));
        }
    } else if(current_pipeline()==PIPE_LEGACY) {
        EnterCriticalSection(&mailbox_lock);
        int s=slot_pending;
        if(s>=0){slot_pending=-1;slot_rendering=s;}
        LeaveCriticalSection(&mailbox_lock);
        if(s>=0) {
            video_progress_note(VP_SELECTED);
            uint64_t t0=now_us();
            presented=draw_frame(s);
            if(presented)stats.rendered++;
            else InterlockedIncrement((volatile LONG *)&stats.dropped);
            stats.avg_render_us=stream_smooth(stats.avg_render_us,(uint32_t)(now_us()-t0));
        }
    }
    if(!presented && wake==WAIT_TIMEOUT && gfx_launch_pending()) {
        D3DDevice *d=gfx_begin();gfx_launch_idle(d);gfx_end();
    }
}

static DWORD WINAPI render_main(LPVOID arg)
{
    xdk_audit_thread("Render");
    D3DDevice *d;
    (void)arg;

    d = gfx_begin();
    float coefficients[4];
    video_yuv_coefficients(options.color_space, coefficients);
    d->SetPixelShaderConstantF(0, coefficients, 1);
    d->SetRenderState(D3DRS_PRESENTINTERVAL, options.vsync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE);
    gfx_end();

    while (running) {
        DWORD wake=WaitForSingleObject(frame_event, gfx_system_ui_active()?16:32);
        if (!running)
            break;
        render_tick(wake);
    }

    d = gfx_begin();
    d->SetRenderState(D3DRS_PRESENTINTERVAL, D3DPRESENT_INTERVAL_ONE);
    gfx_end();
    return 0;
}

/* ---------------------------------------------------- decoding */

static void copy_plane(uint8_t *dst, int w, int h, const uint8_t *src, int pitch)
{
    int y;

    if (pitch == w) {
        XMemCpy(dst, src, (size_t)w * h);
        return;
    }
    for (y = 0; y < h; y++)
        XMemCpy(dst + y * w, src + y * pitch, w);
}

/* Opt-in, one raw planar YUV420 picture per stream for PC reconstruction.
 * Copy before publishing so it can identify decoder output independently
 * from the GPU path. The 4 MiB limit bounds both RAM and disk use. */
static void capture_decoded_yuv_once(const AVFrame *f, int frame_number)
{
    if (!h264_diagnostic.capture_yuv || yuv_captured || !storage_dir()) return;
    yuv_captured = true;
    size_t y=(size_t)vid_w*vid_h, uv=(size_t)chroma_w*chroma_h;
    size_t bytes=y+2*uv;
    if (bytes>4u*1024u*1024u) return;
    uint8_t *raw=(uint8_t *)malloc(bytes);
    if (!raw) return;
    copy_plane(raw,vid_w,vid_h,f->data[0],f->linesize[0]);
    copy_plane(raw+y,chroma_w,chroma_h,f->data[1],f->linesize[1]);
    copy_plane(raw+y+uv,chroma_w,chroma_h,f->data[2],f->linesize[2]);
    bool saved=storage_write("video-decoded.yuv",raw,bytes);
    free(raw);
    if (saved) {
        char info[160];
        int n=snprintf(info,sizeof(info),"format=yuv420p\nwidth=%d\nheight=%d\nframe=%d\nbytes=%u\n",
                       vid_w,vid_h,frame_number,(unsigned)bytes);
        if (n>0 && (size_t)n<sizeof(info))storage_write("video-decoded.txt",info,(size_t)n);
    }
    vlog("[H264 DIAG] decoded YUV capture %s frame=%d size=%u\n",
         saved?"saved":"failed",frame_number,(unsigned)bytes);
}

static void publish(const AVFrame *f)
{
    int s;
    uint64_t timing = options.stats_overlay ? now_us() : 0;

    EnterCriticalSection(&mailbox_lock);
    /* Pick a slot that is neither queued nor being uploaded. */
    for (s = 0; s < FRAME_SLOTS; s++)
        if (s != slot_pending && s != slot_rendering)
            break;
    LeaveCriticalSection(&mailbox_lock);

    uint64_t transfer_start=perf_enabled()?perf_clock():0,plane_start=transfer_start;
    copy_plane(slots[s].plane[0], vid_w, vid_h, f->data[0], f->linesize[0]);
    if(plane_start){uint64_t end=perf_clock();perf_record(PERF_Y_TRANSFER,end-plane_start);plane_start=end;}
    copy_plane(slots[s].plane[1], chroma_w, chroma_h, f->data[1], f->linesize[1]);
    if(plane_start){uint64_t end=perf_clock();perf_record(PERF_U_TRANSFER,end-plane_start);plane_start=end;}
    copy_plane(slots[s].plane[2], chroma_w, chroma_h, f->data[2], f->linesize[2]);
    if(plane_start){uint64_t end=perf_clock();perf_record(PERF_V_TRANSFER,end-plane_start);perf_record(PERF_YUV_TRANSFER,end-transfer_start);}

    EnterCriticalSection(&mailbox_lock);
    /* Count only a picture actually replaced here. The renderer may have
     * consumed the previous pending picture while the planes were copied. */
    if (slot_pending >= 0) {
        InterlockedIncrement((volatile LONG *)&stats.dropped);
        stats.render_superseded++;
    }
    slot_pending = s;
    video_progress_note(VP_QUEUED);
    legacy_ready_us[s]=perf_enabled()?perf_clock():0;
    legacy_ready_ms[s]=GetTickCount();
    legacy_serial[s]=++publish_serial;
    legacy_picture[s]=(uint64_t)f->reordered_opaque;
    if(!video_picture_idr(legacy_picture[s],f->key_frame!=0))legacy_picture[s]&=~(uint64_t)1;
    LeaveCriticalSection(&mailbox_lock);
    SetEvent(frame_event);
    if (options.stats_overlay)
        stats.avg_publish_us = stream_smooth(stats.avg_publish_us, (uint32_t)(now_us() - timing));
}

static void publish_frame(const AVFrame *f)
{
    LONG mode=current_pipeline();
    if (mode==PIPE_LEGACY) {
        uint64_t t=options.stats_overlay?now_us():0;
        publish(f);if (options.stats_overlay) pipeline_sample(TIME_FRAME_COPY,now_us()-t);
        return;
    }
    if (mode!=PIPE_CACHED) { InterlockedIncrement((volatile LONG*)&stats.dropped);return; }
    const BYTE *planes[3]={f->data[0],f->data[1],f->data[2]};
    int pitches[3]={f->linesize[0],f->linesize[1],f->linesize[2]};
    struct gfx_copy_cost cost={0,0};
    uint64_t t=options.stats_overlay?now_us():0;
    cost.verify=h264_diagnostic.capture_yuv && !yuv_upload_compared;
    uint64_t picture=(uint64_t)f->reordered_opaque;
    if(!video_picture_idr(picture,f->key_frame!=0))picture&=~(uint64_t)1;
    int result=cached_video_publish_picture(planes,pitches,(options.stats_overlay || cost.verify)?&cost:NULL,picture);
    if (cost.verify && result!=0) {
        yuv_upload_compared=true;
        vlog("[H264 DIAG] cached YUV upload comparison %s mismatched_rows=%u callback=%u\n",
             result>0?"equal":"failed",cost.mismatch_rows,stats.received);
    }
    if (result>0) {
        video_progress_note(VP_QUEUED);
        if (options.stats_overlay) {
            pipeline_sample(TIME_FRAME_COPY,now_us()-t);pipeline_sample(TIME_TEXTURE_LOCK,cost.lock_us);
            pipeline_sample(TIME_TEXTURE_COPY,cost.copy_us);
        }
        SetEvent(frame_event);return;
    }
    InterlockedIncrement((volatile LONG*)&stats.dropped);
    if (!result) { gpu_slot_starvation++;return; }
    cached_failovers++;
    InterlockedExchange(&active_pipeline,PIPE_SWITCHING);SetEvent(frame_event);
}

static void log_stats(void)
{
    DWORD now = GetTickCount(), dt = now - stats_last_ms;
    uint32_t decoded = stats.decoded - stats_last.decoded;

    if (dt < STATS_INTERVAL_MS)
        return;
    vlog("stats: %.1f fps in, %.1f decoded, %.1f shown; decode %.2f ms avg; render %.2f ms; "
         "dropped %u, errors %u\n",
         (stats.received - stats_last.received) * 1000.0 / dt, decoded * 1000.0 / dt,
         (stats.rendered - stats_last.rendered) * 1000.0 / dt,
         decoded ? decode_us_sum / 1000.0 / decoded : 0.0, stats.avg_render_us / 1000.0,
         stats.dropped - stats_last.dropped, stats.decode_errors - stats_last.decode_errors);
    stats_last = stats;
    stats_last_ms = now;
    decode_us_sum = 0;
    deadline.max_decode_us = deadline.max_queue_us = 0;
}

/* ---------------------------------------------------- recording */

static bool capture_file_exists(void)
{
    char path[160];

    if (storage_dir() == NULL)
        return false;
    snprintf(path, sizeof(path), "%s\\%s", storage_dir(), CAPTURE_FILE);
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

static void capture_start(void)
{
    /* Only allocate 40 MB and record when explicitly armed from the benchmark menu. */
    if (!cap_armed || storage_dir() == NULL || capture_file_exists())
        return;
    cap_armed = false;
    cap_buf = (uint8_t *)malloc(CAPTURE_MAX);
    cap_len = 16;
    cap_frames = 0;
}

static void capture_frame(const uint8_t *data, int len)
{
    if (cap_buf == NULL || cap_frames >= CAPTURE_FRAMES || cap_len + 4 + len > CAPTURE_MAX)
        return;
    memcpy(cap_buf + cap_len, &len, 4);   /* big-endian, as everything here */
    memcpy(cap_buf + cap_len + 4, data, len);
    cap_len += 4 + len;
    cap_frames++;
}

static void capture_finish(void)
{
    uint32_t hdr[4];

    if (cap_buf == NULL)
        return;
    if (cap_frames >= 60) {
        memcpy(&hdr[0], "MLC1", 4);
        hdr[1] = vid_w;
        hdr[2] = vid_h;
        hdr[3] = cap_frames;
        memcpy(cap_buf, hdr, sizeof(hdr));
        if (storage_write(CAPTURE_FILE, cap_buf, cap_len)) {
            char metadata[320];
            int n=snprintf(metadata,sizeof(metadata),"capture_bytes=%u\nfingerprint=%08x\nwidth=%d\nheight=%d\n"
                "target_fps=%d\nbitrate_kbps=%d\nslices_requested=%d\nencoded_slices=NOT_ANALYZED\n",
                cap_len,xdk_capture_fingerprint(cap_buf,cap_len),vid_w,vid_h,options.target_fps,options.bitrate_kbps,session_slices);
            storage_write("capture-info.txt",metadata,n);
            vlog("capture: saved %u frames (%u KB), requested slices %d\n",cap_frames,cap_len/1024,session_slices);
        }
        else
            vlog("capture: couldn't save\n");
    }
    free(cap_buf);
    cap_buf = NULL;
}

/* ---------------------------------------------------- Moonlight callbacks */

/* Producer is between decode calls; renderer must already be joined. Keep the
 * decoder/AVFrame alive when replacing buffers after an H.264 SPS size change. */
static void free_video_buffers(void)
{
    int i, k;
    plat_shutdown_stage("Freeing staging planes\n");
    for (i = 0; i < FRAME_SLOTS; i++)
        for (k = 0; k < 3; k++) {
            if (slots[i].plane[k]) {
                _aligned_free(slots[i].plane[k]);
                slots[i].plane[k] = NULL;
            }
        }
    plat_shutdown_stage("Waiting for GPU before releasing video textures\n");
    D3DDevice *d = gfx_begin();
    cached_video_cleanup(d);
    /* Joining the CPU render thread does not finish queued GPU draws. */
    for (k = 0; k < 3; k++)
        d->SetTexture(k, NULL);
    for (i = 0; i < 2; i++)
        if (tex_fence[i])
            gfx_wait_fence(d,tex_fence[i]);
    d->BlockUntilIdle();
    gfx_overlay_clear(d);
    for (i = 0; i < 2; i++) {
        for (k = 0; k < 3; k++) {
            if (tex[i][k])
                tex[i][k]->Release();
            tex[i][k] = NULL;
        }
        tex_fence[i] = 0;
    }
    gfx_end();
    EnterCriticalSection(&mailbox_lock);
    slot_pending = slot_rendering = -1;
    LeaveCriticalSection(&mailbox_lock);
    plat_shutdown_stage("Video resources released\n");
}

static void free_resources(void)
{
    plat_shutdown_stage("Closing H.264 decoder and joining slice workers\n");
    close_h264(&codec_ctx);
    plat_shutdown_stage("Freeing decoded AVFrame\n");
    if (dec_frame) avcodec_free_frame(&dec_frame);
    plat_shutdown_stage("Freeing compressed packet buffer\n");
    if (pkt_buf) { _aligned_free(pkt_buf);pkt_buf=NULL; }
    free_video_buffers();
}

static void configure_smoothing_history(bool cached,int height)
{
    bool crt=options.crt_clarity && height<=480 && gfx_crt_clarity_available();
    if(cached && options.refresh_smoothing && gfx_smoothing_available(crt))cached_video_enable_history();
}

static int video_setup(int format, int width, int height, int fps, void *ctx, int dr_flags)
{
    bool ok = true;
    setup_fps_argument=fps;
    cached_video_reset_smoothing_diagnostics();
    (void)ctx;
    (void)dr_flags;

    if (format != VIDEO_FORMAT_H264 || width <= 0 || height <= 0 || width > 1920 || height > 1080)
        return -1;
    if (!frame_event || qpc_freq.QuadPart<=0) {
        InterlockedExchange(&stream_error,VIDEO_STREAM_RESOURCE_ERROR);
        return -1;
    }

    char freeze_stage[160];
    snprintf(freeze_stage,sizeof(freeze_stage),
             "[FREEZE] video setup %dx%d fps_argument=%d format=%d\n",width,height,fps,format);
    plat_diagnostics_checkpoint(freeze_stage);

    memset(&stats, 0, sizeof(stats));
    memset(&parallel_totals,0,sizeof(parallel_totals));
    parallel_serial_frames=parallel_parallel_frames=parallel_mixed_frames=0;
    parallel_forced_serial_frames=parallel_reference_frames=parallel_nonreference_frames=0;
    parallel_trace_next=parallel_trace_count=0;
    idr_received=idr_decoded=0;
    idr_present_calls=decode_no_picture=0;smoothing_have_input=false;smoothing_last_input=0;memset(&refresh_trace,0,sizeof(refresh_trace));
    perf_pressure_read(&pressure_at_setup);
    yuv_captured = false;
    yuv_upload_compared = false;
    memset(&last_rtp_stats,0,sizeof(last_rtp_stats));rtp_counter_duration_us=0;
    last_complete_frames=last_video_packets=last_video_bytes=last_late_packets=last_lost_frames=0;
    last_data_packets=last_fec_packets=0;
    memset(legacy_ready_us,0,sizeof(legacy_ready_us));memset(legacy_serial,0,sizeof(legacy_serial));
    publish_serial=last_present_serial=present_duplicates=0;last_present_cached=false;
    previous_present_start_us=last_present_start_us=last_present_return_us=0;
    last_decoded_us=last_presented_us=0;present_age_sample_ordinal=0;
    last_packet_age_stop_ms=last_decode_age_stop_ms=last_present_age_stop_ms=UINT32_MAX;
    present_trace_next=present_trace_count=0;
    InterlockedExchange(&stream_error,VIDEO_STREAM_OK);
    first_present_logged=false;launch_decode_notified=false;freeze_first_present=false;
    memset(&deadline, 0, sizeof(deadline));
    memset(&deadline_overlay, 0, sizeof(deadline_overlay));
    EnterCriticalSection(&telemetry_lock);
    memset(&observations, 0, sizeof(observations));
    network_rtt = network_variance = 0;
    network_video_queue = network_audio_ms = 0;
    network_video_queue_peak = 0;
    network_valid = false;
    telemetry_ready = false;
    LeaveCriticalSection(&telemetry_lock);
    memset(&overlay_previous, 0, sizeof(overlay_previous));
    overlay_last_ms = GetTickCount();
    overlay_has_text = overlay_failed = false;
    gfx_display_size(&stats.display_w,&stats.display_h);
    stats.renderer = RENDERER_GPU;
    stats_last = stats;
    stats_last_ms = GetTickCount();
    decode_us_sum = 0;
    vid_w = width;
    vid_h = height;
    chroma_w = (width + 1) / 2;
    chroma_h = (height + 1) / 2;

    vlog("video: setup %dx%d, format 0x%x, vmx flags %d, loopfilter selection %02x\n",
         width, height, format, ff_xbox_vmx_flags, ff_xbox_loopfilter_mask);
    plat_diagnostics_checkpoint("[FREEZE] H264 decoder initialization entered\n");
    codec_ctx = open_h264(width, height, h264_workers(), options.fast_decode);
    dec_frame = avcodec_alloc_frame();
    if (codec_ctx == NULL || dec_frame == NULL) {
        vlog("video: couldn't open the decoder\n");
        free_resources();
        return -1;
    }
    stats.decode_threads = (codec_ctx->active_thread_type & FF_THREAD_SLICE) ? codec_ctx->thread_count : 1;

    pkt_cap = 1024 * 1024;
    pkt_buf = (uint8_t *)_aligned_malloc(pkt_cap + FF_INPUT_BUFFER_PADDING_SIZE, 128);
    memset(pipeline_times,0,sizeof(pipeline_times));memset(&quality_policy,0,sizeof(quality_policy));
    memset(pipeline_ema,0,sizeof(pipeline_ema));memset(pipeline_peak,0,sizeof(pipeline_peak));
    memset(pipeline_calls,0,sizeof(pipeline_calls));memset(pipeline_total,0,sizeof(pipeline_total));
    streaming_start_us=streaming_end_us=0;
    submit_last_end_us=0;
    cap_frames=0; /* capture_finish must not report a previous session's capture */
    gpu_slot_starvation=cached_failovers=0;requested_pipeline=PIPE_AUTO;
    requested_shader=options.shader_mode>=0 && options.shader_mode<=2?options.shader_mode:0;
    char *gpu_config=storage_read("video-gpu.ini",NULL);
    shader_diagnostic_override=gpu_options_resolve(gpu_config,options.shader_mode,&requested_pipeline,&requested_shader);
    if (gpu_config) {
        free(gpu_config);
    }
    if (DIAGNOSTIC_LEGACY_UPLOAD) requested_pipeline=PIPE_LEGACY;
    plat_diagnostics_checkpoint("[FREEZE] texture creation entered\n");
    bool use_cached=requested_pipeline!=PIPE_LEGACY && cached_video_init(width,height);
    configure_smoothing_history(use_cached,height);
    if (!use_cached) {
        if (requested_pipeline!=PIPE_LEGACY) cached_failovers++;
        ok=create_legacy_video();
    }
    InterlockedExchange(&active_pipeline,use_cached?PIPE_CACHED:PIPE_LEGACY);
    vlog("video: pipeline %s, shader %s, cached row footprint %I64u bytes\n",use_cached?"CPU_CACHED_TEXTURE":"LEGACY_UPLOAD",
         requested_shader==1?"FAST":requested_shader==2?"QUALITY":"AUTO QUALITY",(unsigned long long)cached_video_memory());
    if (DIAGNOSTIC_LEGACY_UPLOAD) vlog("[H264 DIAG] Legacy Upload forced by diagnostic build\n");
    vlog("video: image menu=%d shader request=%d diagnostic override=%d; independent of Fast Decode=%d\n",
         options.shader_mode,requested_shader,shader_diagnostic_override,options.fast_decode);
    if (!ok || pkt_buf == NULL) {
        vlog("video: out of memory for buffers/textures\n");
        free_resources();
        return -1;
    }
    vlog("video: ready, %d decode threads, %d requested slices, Rec.%s limited\n",
         stats.decode_threads, session_slices, options.color_space == COLORSPACE_REC_709 ? "709" : "601");
    snprintf(freeze_stage,sizeof(freeze_stage),
             "[FREEZE] video ready %dx%d pipeline=%d display=%dx%d\n",
             vid_w,vid_h,(int)current_pipeline(),stats.display_w,stats.display_h);
    plat_diagnostics_checkpoint(freeze_stage);

    slot_pending = slot_rendering = -1;
    tex_next = 0;
    need_idr = false;
    InterlockedExchange(&decoder_reset_requested,0);
    capture_start();
    return 0;
}

static void video_start(void)
{
    plat_diagnostics_checkpoint("[FREEZE] video renderer start entered\n");
    streaming_start_us=now_us();streaming_end_us=0;
    InterlockedExchange(&refresh_active,1);
    InterlockedExchange(&refresh_state,VIDEO_REFRESH_IDLE);
    InterlockedExchange(&refresh_requests,0);
    InterlockedExchange(&refresh_decoded_idrs,0);
    InterlockedExchange(&refresh_completed_request,-1);
    InterlockedExchange(&refresh_last_idr_ms,0);
    InterlockedExchange(&refresh_last_request_ms,(LONG)GetTickCount());
    running = true;
    render_thread = CreateThread(NULL, 512 * 1024, render_main, NULL, CREATE_SUSPENDED, NULL);
    if (render_thread) {
        XSetThreadProcessor(render_thread, HW_THREAD_RENDER);
        ResumeThread(render_thread);
    } else InterlockedExchange(&stream_error,VIDEO_STREAM_RESOURCE_ERROR);
}

static void video_stop(void)
{
    InterlockedExchange(&refresh_active,0);
    InterlockedExchange(&refresh_state,VIDEO_REFRESH_UNAVAILABLE);
    running = false;
    if (render_thread) {
        SetEvent(frame_event);
        WaitForSingleObject(render_thread, INFINITE);
        CloseHandle(render_thread);
        render_thread = NULL;
    }
    streaming_end_us=now_us();
}

static bool resize_video(int width,int height)
{
    if(width<=0 || height<=0 || width>1920 || height>1080) {
        vlog("video: unsupported stream size %dx%d\n",width,height);
        InterlockedExchange(&stream_error,VIDEO_STREAM_UNSUPPORTED_SIZE);
        return false;
    }
    vlog("video: stream size changed %dx%d -> %dx%d\n",vid_w,vid_h,width,height);
    char freeze_stage[128];
    snprintf(freeze_stage,sizeof(freeze_stage),"[FREEZE] video resize %dx%d -> %dx%d entered\n",vid_w,vid_h,width,height);
    plat_diagnostics_checkpoint(freeze_stage);
    /* MLC1 benchmark files have a single geometry header. The current packet
     * was captured before decode revealed the change; discard this mixed-size
     * recording rather than save it with misleading dimensions. */
    if(cap_buf) {
        free(cap_buf);cap_buf=NULL;cap_len=cap_frames=0;
        vlog("capture: discarded after stream resolution changed\n");
    }
    uint64_t session_start=streaming_start_us;
    video_stop(); /* Never free textures/staging while the renderer reads them. */
    free_video_buffers();
    vid_w=width;vid_h=height;chroma_w=(width+1)/2;chroma_h=(height+1)/2;
    bool cached=requested_pipeline!=PIPE_LEGACY && cached_video_init(width,height);
    configure_smoothing_history(cached,height);
    bool ok=cached || create_legacy_video();
    InterlockedExchange(&active_pipeline,cached?PIPE_CACHED:ok?PIPE_LEGACY:PIPE_DISABLED);
    tex_next=0;
    if(!ok) {
        InterlockedExchange(&stream_error,VIDEO_STREAM_RESOURCE_ERROR);
        return false;
    }
    video_start();
    streaming_start_us=session_start;
    plat_diagnostics_checkpoint("[FREEZE] video resize resources ready\n");
    return video_get_stream_error()==VIDEO_STREAM_OK;
}

static uint32_t video_stage_age_ms(uint64_t now,uint64_t last)
{
    if(!last || now<last)return UINT32_MAX;
    uint64_t age=(now-last)/1000;
    return age<UINT32_MAX?(uint32_t)age:UINT32_MAX;
}
/* Cold, bounded report, also available when per-frame timing is disabled. */
static void write_seamless_report(void)
{
    char text[768];
    int n=snprintf(text,sizeof(text),
        "schema=1\npresentation=seamless_direct\ntemporal_blend=see_smoothing-session.txt\n"
        "extra_gpu_history_bytes=see_smoothing-session.txt\nextra_gpu_samples=see_smoothing-session.txt\nrefresh_requests=%u\n"
        "idr_received=%u\nidr_valid_decoded=%u\nidr_present_calls=%u\ndecode_no_picture=%u\n"
        "present_scope=CPU_calls_not_scanout\nnewest_frame_selection=unchanged\n",
        (uint32_t)refresh_requests,idr_received,idr_decoded,idr_present_calls,decode_no_picture);
    if(n>0 && (size_t)n<sizeof(text))storage_write("seamless-session.txt",text,(size_t)n);
}
static void write_smoothing_report(void)
{
    struct smoothing_diagnostics m;cached_video_smoothing_snapshot(&m);
    bool crt=options.crt_clarity && vid_h<=480 && gfx_crt_clarity_available();
    bool created=gfx_smoothing_available(crt);
    char text[2048];int n=snprintf(text,sizeof(text),
        "schema=1\nmode=GPU_Smooth_Experimental\ngpu_smoothing_requested=%d\ngpu_smoothing_enabled=%d\n"
        "smoothing_shader_created=%d\nsmoothing_strength=%s\nprevious_frame_available=%d\n"
        "history_slot_pinned=%u\nhistory_currently_pinned=%d\nhistory_slot_bypassed=%u\n"
        "idr_transition_started=%u\nidr_transition_applied=%u\nactual_blended_present_calls=%u\n"
        "transition_bypassed_motion=%u\ntransition_bypassed_invalid_history=%u\nhistory_allocation_failed=%u\n"
        "additional_gpu_history_bytes=%I64u\nadditional_history_row_bytes=%I64u\ngpu_slot_starvation=%u\n"
        "normal_cached_producer_slot_capacity=3\nhistory_pool_slots=%d\n"
        "blend_counter_scope=Present_calls_with_two_distinct_raw_images_and_temporal_shader_not_scanout_or_nonzero_pixel_proof\n"
        "motion_counter_scope=sparse_CPU_gate;per_pixel_shader_gate_hits_unmeasured\n"
        "history_memory_scope=last_successful_XGGetTextureLayout_base_plus_mips;allocator_header_overhead_unmeasured\n"
        "gpu_execution_time=unmeasured;CPU_submit_and_Present_times_are_not_GPU_timestamps\n",
        options.refresh_smoothing,options.refresh_smoothing && m.enabled && created,created,options.smoothing_strength==1?"Medium":"Low",
        m.previous_frame_available,m.history_slot_pinned,m.history_currently_pinned,m.history_slot_bypassed,
        m.idr_transition_started,m.idr_transition_applied,m.actual_blended_present_calls,m.transition_bypassed_motion,
        m.transition_bypassed_invalid_history,m.history_allocation_failed,
        (unsigned long long)m.additional_gpu_history_bytes,(unsigned long long)m.additional_history_row_bytes,gpu_slot_starvation,m.enabled?4:current_pipeline()==PIPE_CACHED?3:0);
    if(n>0 && (size_t)n<sizeof(text))storage_write("smoothing-session.txt",text,(size_t)n);
}
static void write_refresh_network_reports(void)
{
    char text[16384];
    const RTP_VIDEO_STATS *net=LiGetRTPVideoStats();
    struct perf_pressure pressure_end;perf_pressure_read(&pressure_end);
    uint64_t duration=streaming_end_us>streaming_start_us?streaming_end_us-streaming_start_us:0;
    int n=snprintf(text,sizeof(text),
        "schema=1\nlink=%s\nlink_scope=console_only_host_path_unknown\npacket_size=%d\npacket_loss_exact=unavailable\nIP_fragmentation=unavailable\n"
        "duration_us=%I64u\nudp_datagrams=%u\nudp_bytes=%I64u\nudp_packets_per_second=%.3f\n"
        "expected_data_shards=%u\nexpected_parity_shards=%u\naccepted_data_packets=%u\naccepted_parity_packets=%u\n"
        "fec_recovered_data_shards=%u\nincomplete_block_data_deficit=%u\nrtp_oos=%u\nrtp_invalid=%u\ndecode_queue_overflows=%I64u\n"
        "physical_crt_output_status=%d\ncrt_clarity_requested=%d\ncrt_clarity_active=%d\n"
        "frame_samples=diagnostic_only_modulo_64;accepted_before_FEC_not_all_wire_packets\n"
        "columns=frame,expected_data,expected_fec,accepted_data,accepted_fec,accepted_bytes,first_us,last_us,max_gap_us\n",
        console_link_name(plat_network_link()),options.packet_size,(unsigned long long)duration,
        net?net->datagrams:0,(unsigned long long)(net?net->datagramBytes:0),duration && net?net->datagrams*1000000.0/duration:0,
        net?net->packetCountVideo:0,net?net->packetCountFec:0,net?net->acceptedData:0,net?net->acceptedFec:0,
        net?net->packetCountFecRecovered:0,net?net->packetCountFecFailed:0,net?net->packetCountOOS:0,net?net->packetCountInvalid:0,
        (unsigned long long)(pressure_end.overflows-pressure_at_setup.overflows),plat_crt_output_status(),options.crt_clarity,
        options.crt_clarity && vid_h<=480 && gfx_crt_clarity_available());
    for(unsigned i=0;net && i<RTP_VIDEO_FRAME_SAMPLES && n>0 && (size_t)n<sizeof(text);i++) {
        const RTP_VIDEO_FRAME_STATS *f=&net->frames[i];if(!f->valid)continue;
        n+=snprintf(text+n,sizeof(text)-n,"%u,%u,%u,%u,%u,%u,%I64u,%I64u,%u\n",f->frame,f->expectedData,f->expectedFec,
            f->acceptedData,f->acceptedFec,f->bytes,(unsigned long long)f->firstUs,(unsigned long long)f->lastUs,f->maxGapUs);
    }
    if(n>0 && (size_t)n<sizeof(text))storage_write("network-session.txt",text,(size_t)n);
    n=snprintf(text,sizeof(text),
        "schema=1\nmeasurement=diagnostic_only;last_64_callbacks;size_percentiles_last_256\n"
        "frames_sampled=%u\nidr_inputs_sampled=%u\nidr_callbacks_received=%u\nall_payload_bytes=%I64u\nidr_payload_bytes=%I64u\nidr_inputs_per_second=%.3f\n"
        "frame_bytes_p50_p95_p99=%u,%u,%u\nidr_bytes_p50_p95_p99=%u,%u,%u\n"
        "luma=8x8_point_grid_not_full_frame_or_quantization_proof\n"
        "columns=input_frame,output_frame,input_idr,bytes,start_us,predecode_queue_age_us,decode_us,got,ret,decode_flags,validated,mean_y,min_y,max_y\n",
        refresh_trace.frames,refresh_trace.idrs,idr_received,(unsigned long long)refresh_trace.all_bytes,(unsigned long long)refresh_trace.idr_bytes,
        duration?idr_received*1000000.0/duration:0,
        gpu_time_percentile(&refresh_trace.frame_sizes,50),gpu_time_percentile(&refresh_trace.frame_sizes,95),gpu_time_percentile(&refresh_trace.frame_sizes,99),
        gpu_time_percentile(&refresh_trace.idr_sizes,50),gpu_time_percentile(&refresh_trace.idr_sizes,95),gpu_time_percentile(&refresh_trace.idr_sizes,99));
    for(unsigned i=0;i<refresh_trace.count && n>0 && (size_t)n<sizeof(text);i++) {
        const refresh_trace_frame *f=&refresh_trace.frame[(refresh_trace.next+REFRESH_TRACE_FRAMES-refresh_trace.count+i)%REFRESH_TRACE_FRAMES];
        n+=snprintf(text+n,sizeof(text)-n,"%u,%u,%u,%u,%I64u,%u,%u,%d,%d,%d,%u,%u,%u,%u\n",
            f->input,f->output,f->input_idr,f->bytes,(unsigned long long)f->start_us,f->queue_us,f->decode_us,f->got,f->ret,
            f->flags,f->valid,f->mean_y,f->min_y,f->max_y);
    }
    if(n>0 && (size_t)n<sizeof(text))storage_write("refresh-frame-trace.txt",text,(size_t)n);
}
static void video_cleanup(void)
{
    write_seamless_report();
    write_smoothing_report();
    write_refresh_network_reports();
    uint64_t stopped_at=now_us();
    last_packet_age_stop_ms=perf_last_video_packet_age_ms();
    last_decode_age_stop_ms=video_stage_age_ms(stopped_at,last_decoded_us);
    last_present_age_stop_ms=video_stage_age_ms(stopped_at,last_presented_us);
    vlog("video: stream ended: %u received, %u decoded, %u shown, %u dropped, %u errors\n",
         stats.received, stats.decoded, stats.rendered, stats.dropped, stats.decode_errors);
    capture_finish();
    const RTP_VIDEO_STATS *rtp=LiGetRTPVideoStats();
    if(rtp)last_rtp_stats=*rtp;
    rtp_counter_duration_us=perf_window_duration_us();
    last_complete_frames=(uint32_t)perf_window_counter_read(PERF_C_VIDEO_FRAMES_COMPLETE);
    last_video_packets=(uint32_t)perf_window_counter_read(PERF_C_VIDEO_PACKETS);
    last_video_bytes=(uint32_t)perf_window_counter_read(PERF_C_VIDEO_BYTES);
    last_late_packets=(uint32_t)perf_window_counter_read(PERF_C_VIDEO_LATE);
    last_lost_frames=(uint32_t)perf_window_counter_read(PERF_C_VIDEO_FRAMES_LOST);
    last_data_packets=(uint32_t)perf_window_counter_read(PERF_C_VIDEO_DATA_ACCEPTED);
    last_fec_packets=(uint32_t)perf_window_counter_read(PERF_C_VIDEO_FEC_ACCEPTED);
    if(present_trace_count && storage_dir()) {
        char trace[8192];size_t used=0;
        int n=snprintf(trace,sizeof(trace),"local_present_trace_last=%u\nclock=qpc_us_local_only\ncolumns=serial,cached,start_us,return_us,input_frame,idr\n",present_trace_count);
        if(n>0 && (size_t)n<sizeof(trace))used=(size_t)n;
        for(uint32_t i=0;i<present_trace_count && used<sizeof(trace);i++) {
            const present_trace_entry *e=&present_trace[(present_trace_next+64-present_trace_count+i)%64];
            n=snprintf(trace+used,sizeof(trace)-used,"%u,%u,%I64u,%I64u,%u,%u\n",e->serial,(unsigned)e->cached,
                       (unsigned long long)e->start_us,(unsigned long long)e->return_us,e->frame,(unsigned)e->idr);
            if(n<0 || (size_t)n>=sizeof(trace)-used)break;
            used+=(size_t)n;
        }
        if(used)storage_write("present-trace.txt",trace,used);
    }
    if (parallel_trace_count && storage_dir()) {
        uint64_t duration=streaming_end_us>streaming_start_us?streaming_end_us-streaming_start_us:0;
        char summary[2048];
        int n=snprintf(summary,sizeof(summary),
            "reference_mode=%d\nconfigured_slice_workers=%d\n"
            "frames_serial_only=%u\nframes_parallel_only=%u\nframes_mixed=%u\n"
            "frames_forced_serial_by_deblock=%u\nreference_frames=%u\nnonreference_frames=%u\nfast_decode_active=%s\n"
            "parsed_slices=%u\nraw_deblocking_idc_0_1_2=%u,%u,%u\n"
            "effective_filter_off_across_within=%u,%u,%u\n"
            "reference_slices=%u\ncabac_slices=%u\ncavlc_slices=%u\n"
            "serial_dispatches=%u\nparallel_dispatches=%u\n"
            "max_dispatch_contexts=%u\nmax_worker_participants=%u\nmax_concurrent_workers=%u\n"
            "decode_avg_us=%.3f\ndecode_p95_us=%u\ndecode_p99_us=%u\n"
            "completed_counter=%u\ndecoded_counter=%u\npresented_counter=%u\n"
            "completed_counter_scope=performance_window\ncompleted_counter_duration_us=%I64u\n"
            "completed_fps=%.3f\ndecoded_fps=%.3f\npresented_fps=%.3f\n"
            "percentiles=rolling_256; worker_participants=distinct_workers_per_dispatch; concurrent=peak_simultaneous_jobs\n",
            XDK_H264_REFERENCE_MODE,stats.decode_threads,
            parallel_serial_frames,parallel_parallel_frames,parallel_mixed_frames,
            parallel_forced_serial_frames,parallel_reference_frames,parallel_nonreference_frames,fast_decode_name(options.fast_decode),
            parallel_totals.slices,parallel_totals.raw_idc[0],parallel_totals.raw_idc[1],parallel_totals.raw_idc[2],
            parallel_totals.effective_filter[0],parallel_totals.effective_filter[1],parallel_totals.effective_filter[2],
            parallel_totals.reference_slices,parallel_totals.cabac_slices,
            parallel_totals.slices-parallel_totals.cabac_slices,
            parallel_totals.serial_dispatches,parallel_totals.parallel_dispatches,
            parallel_totals.max_dispatch_contexts,parallel_totals.max_worker_participants,
            parallel_totals.max_concurrent_workers,
            pipeline_calls[TIME_DECODE]?(double)pipeline_total[TIME_DECODE]/pipeline_calls[TIME_DECODE]:0.0,
            gpu_time_percentile(&pipeline_times[TIME_DECODE],95),
            gpu_time_percentile(&pipeline_times[TIME_DECODE],99),
            last_complete_frames,stats.decoded,stats.rendered,
            (unsigned long long)rtp_counter_duration_us,
            stream_rate_per_second(last_complete_frames,rtp_counter_duration_us),
            stream_rate_per_second(stats.decoded,duration),
            stream_rate_per_second(stats.rendered,duration));
        if(n>0 && (size_t)n<sizeof(summary))storage_write("h264-parallel-session.txt",summary,(size_t)n);
        if (parallel_totals.slice_calls) {
            LARGE_INTEGER frequency;
            QueryPerformanceFrequency(&frequency);
            double scale=frequency.QuadPart>0?1000000.0/(double)frequency.QuadPart:0.0;
            char stage[1024];
            int sn=snprintf(stage,sizeof(stage),
                "schema=1\nclock=qpc_ticks\nqpc_frequency=%I64u\n"
                "slice_calls=%u\nslice_total_us=%.3f\n"
                "loopfilter_calls=%u\nloopfilter_total_us=%.3f\n"
                "entropy_samples=%u\nentropy_sampled_total_us=%.3f\n"
                "entropy_sample_mean_us=%.3f\n"
                "scope=profiled_decoder_only; entropy_sample_1_in_32_macroblocks; timings_include_probe_cost\n",
                (unsigned long long)frequency.QuadPart,
                parallel_totals.slice_calls,parallel_totals.slice_ticks*scale,
                parallel_totals.loopfilter_calls,parallel_totals.loopfilter_ticks*scale,
                parallel_totals.entropy_samples,parallel_totals.entropy_sample_ticks*scale,
                parallel_totals.entropy_samples?parallel_totals.entropy_sample_ticks*scale/parallel_totals.entropy_samples:0.0);
            if(sn>0 && (size_t)sn<sizeof(stage))storage_write("h264-stage-session.txt",stage,(size_t)sn);
        }
        char trace[16384];size_t used=0;
        n=snprintf(trace,sizeof(trace),"last_frames=%u\ncolumns=frame,decode_us,idr,slices,idc0,idc1,idc2,filter_off,filter_across,filter_within,ref_slices,cabac_slices,contexts_requested,contexts_effective,serial_dispatches,parallel_dispatches,max_dispatch_contexts,worker_participants,concurrent_workers,forced_serial,slice_ticks,loopfilter_ticks,entropy_sample_ticks,entropy_samples\n",parallel_trace_count);
        if(n>0 && (size_t)n<sizeof(trace))used=(size_t)n;
        for(uint32_t i=0;i<parallel_trace_count;i++) {
            const parallel_trace_entry *e=&parallel_trace[(parallel_trace_next+64-parallel_trace_count+i)%64];
            const FFH264ParallelDiagnostic *d=&e->d;
            n=snprintf(trace+used,sizeof(trace)-used,
                "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%I64u,%I64u,%I64u,%u\n",
                e->frame,e->decode_us,e->idr,d->slices,d->raw_idc[0],d->raw_idc[1],d->raw_idc[2],
                d->effective_filter[0],d->effective_filter[1],d->effective_filter[2],
                d->reference_slices,d->cabac_slices,d->contexts_requested,d->contexts_effective,
                d->serial_dispatches,d->parallel_dispatches,d->max_dispatch_contexts,
                d->max_worker_participants,d->max_concurrent_workers,d->forced_serial,
                (unsigned long long)d->slice_ticks,(unsigned long long)d->loopfilter_ticks,
                (unsigned long long)d->entropy_sample_ticks,d->entropy_samples);
            if(n<0 || (size_t)n>=sizeof(trace)-used)break;
            used+=(size_t)n;
        }
        if(used)storage_write("h264-parallel-trace.txt",trace,used);
    }
    if (options.stats_overlay && storage_dir()) {
        char report[8192];
        int n = snprintf(report, sizeof(report),
            "telemetry_schema=4\nbuild_profile=%s\nworker_map=%s\nworkers_requested=%d\nworkers_active=%d\nthread_layout=%s\n"
            "decoder_requested=%s\ndecoder_active=ffmpeg\nsystem_gpu_compiled=0\n"
            "video=%dx%d@%d\nbitrate_kbps=%d\nfast_decode_config=%d\nfast_decode_active=%s\nfast_decode_change=next_session\n"
            "h264_fast_flag=%d\nh264_rfi=%d\nh264_workers_effective=%d\nh264_reference_mode=%d\n"
            "frame_budget_us=%u\ndecode_current_us=%u\ndecode_ema_us=%u\n"
            "decode_max_session_us=%u\nqueue_age_valid=%d\nqueue_age_us=%u\n"
            "queue_age_max_session_us=%u\nfilter=%s\nrecovery_count=%u\n"
            "full_frames=%u\nfast_frames=%u\nrender_present_ema_us=%u\n"
            "received=%u\ndecoded=%u\nshown=%u\ndropped=%u\ndecode_errors=%u\n"
            "vmx_groups=%x\nloopfilter_selection_mask=%x\n"
            "session_slices=%d\ncolor_space=%s\ncolor_range=limited\n"
            "staging_publish_ema_us=%u\ntexture_upload_ema_us=%u\ntexture_fence_wait_ema_us=%u\n"
            "render_superseded=%u\n"
            "pipeline_percentiles=rolling_256_samples; CPU_wall_times; overlay_required\n",
            XDK_BUILD_PROFILE_LABEL,XDK_WORKER_MAP_LABEL,options.decode_workers==3?3:4,stats.decode_threads,
            options.thread_layout?"balanced":"legacy_aux",
            video_decoder_key(options.decoder_backend),
            vid_w, vid_h, options.target_fps, options.bitrate_kbps, options.fast_decode,fast_decode_name(options.fast_decode),
            h264_reference_fast_flag(XDK_H264_REFERENCE_MODE,h264_diagnostic.fast_flag,options.fast_decode),
            h264_diagnostic.rfi && !XDK_H264_NO_RFI,h264_workers(),XDK_H264_REFERENCE_MODE,
            deadline.budget_us, deadline.decode_us, stats.avg_decode_us,
            deadline.session_max_decode_us, deadline.queue_valid, deadline.queue_us,
            deadline.session_max_queue_us,
            XDK_H264_REFERENCE_MODE==1 ? "FULL_REFERENCE_SAFE" :
            XDK_H264_REFERENCE_MODE==2 ? (options.fast_decode || deadline.recovery ? "NONREF_ONLY" : "FULL") :
            options.fast_decode ? "FAST" : deadline.recovery ? "RECOVERY" : "FULL",
            deadline.recoveries, deadline.full_frames, deadline.fast_frames, stats.avg_render_us,
            stats.received, stats.decoded, stats.rendered, stats.dropped, stats.decode_errors,
            ff_xbox_vmx_flags, ff_xbox_loopfilter_mask, session_slices,
            options.color_space == COLORSPACE_REC_709 ? "Rec.709" : "Rec.601",
            stats.avg_publish_us, stats.avg_upload_us, stats.avg_gpu_wait_us, stats.render_superseded);
        if (n>0 && (size_t)n<sizeof(report)) {
            int physical_w,physical_h,bb_w,bb_h;
            gfx_display_size(&physical_w,&physical_h);gfx_backbuffer_size(&bb_w,&bb_h);
            n+=snprintf(report+n,sizeof(report)-n,
                "pipeline=%s\ngpu_slot_starvation=%u\ncached_failovers=%u\ncached_row_bytes=%I64u\n"
                "backbuffer=%dx%d\ndisplay=%dx%d\ndisplay_hz=%.3f\ndisplay_interlaced=%d\nshader=%s\nquality_trial=%u\n"
                "quality_submit_p99_us=%u\nfast_submit_p99_us=%u\n"
                "image_quality_config=%d\nshader_requested=%d\nshader_diagnostic_override=%d\n"
                "gpu_execution_timestamp=unavailable; gpu_submit_is_CPU_wall_time\n",
                pipeline_name(current_pipeline()),gpu_slot_starvation,cached_failovers,
                (unsigned long long)cached_video_memory(),bb_w,bb_h,physical_w,physical_h,
                gfx_display_hz(),gfx_display_interlaced(),
                gpu_shader_fast(requested_shader,gfx_quality_available(),&quality_policy)?"FAST":"QUALITY",
                quality_policy.tested,quality_policy.quality_p99,quality_policy.fast_p99,
                options.shader_mode,requested_shader,shader_diagnostic_override);
            n+=snprintf(report+n,sizeof(report)-n,
#if XDK_SLICE_DISPATCH_BATCHED
                "slice_dispatch=batched\nslice_dispatch_version=2\n"
#else
                "slice_dispatch=legacy\nslice_dispatch_version=0\n"
#endif
                "received_counter=backend_decode_callbacks_not_network_arrivals\n"
                "rtp_completed_frames=%u\nrtp_counter_scope=performance_window\nrtp_counter_duration_us=%I64u\nvideo_udp_packets=%u\nvideo_udp_bytes=%u\n"
                "rtp_expected_video_data_packets=%u\nrtp_expected_fec_packets=%u\n"
                "rtp_accepted_video_data_packets=%u\nrtp_accepted_fec_packets=%u\n"
                "rtp_oos_packets=%u\nrtp_late_packets=%u\nrtp_invalid_packets=%u\n"
                "fec_recovered_packets=%u\nfec_failed_data_deficit=%u\nframe_loss_notifications=%u\n"
                "present_last_local_sequence=%u\npresent_duplicate_local_sequence=%u\n"
                "last_present_start_qpc_us=%I64u\nlast_present_return_qpc_us=%I64u\n"
                "rtp_queue_delay_define_ms=10_unused_in_this_checkout\n"
                "callback_gap=outside_backend_includes_MCC_wait_cleanup_and_scheduling\n"
                "packet_prepare=validation_realloc_fragment_copy_padding_and_optional_capture\n"
                "capture_frames=%u\nframe_gaps_observed=%u\nlast_frame_number=%u\n"
                "backend_payload_bytes_mod32=%u\nhost_processing_ema_us=%u\n"
                "packet_assembly_ema_us=%u\ndu_queue_ema_us=%u\n"
                "network_queue_last_sample=%d\nnetwork_queue_peak_sampled_1Hz=%d\n"
                "network_rtt_valid=%d\nnetwork_rtt_last_ms=%u\nnetwork_variance_last_ms=%u\n",
                last_complete_frames,(unsigned long long)rtp_counter_duration_us,last_video_packets,last_video_bytes,
                last_rtp_stats.packetCountVideo,last_rtp_stats.packetCountFec,last_data_packets,last_fec_packets,
                last_rtp_stats.packetCountOOS,last_late_packets,last_rtp_stats.packetCountInvalid,
                last_rtp_stats.packetCountFecRecovered,last_rtp_stats.packetCountFecFailed,last_lost_frames,
                last_present_serial,present_duplicates,
                (unsigned long long)last_present_start_us,(unsigned long long)last_present_return_us,
                cap_frames,observations.frame_gaps,observations.last_frame,observations.payload_bytes,
                observations.host_us,observations.assembly_us,observations.queue_us,
                network_video_queue,network_video_queue_peak,network_valid,network_rtt,network_variance);
            static const char *names[TIME_COUNT]={"decode","frame_copy","texture_lock","texture_copy","cpu_gpu_submit","fence_check_cpu","present_call_cpu",
                "packet_prepare","callback_total","callback_gap"};
            uint64_t duration=streaming_end_us>streaming_start_us?streaming_end_us-streaming_start_us:0;
            n+=snprintf(report+n,sizeof(report)-n,
                "requested_fps=%d\nsetup_fps_argument=%d\nnegotiated_fps=unknown\nnegotiated_fps_known=0\n"
                "negotiated_fps_source=not_exposed; MCC_setup_copies_requested_StreamConfig_fps\n"
                "vsync_enabled=%d\npresented_counter=video_Present_calls_not_scanout_or_visible_frames\n"
                "callback_fps=%.3f\npresented_fps=%.3f\n"
                "stream_duration_us=%I64u\nshown_fps=%.3f\nreceived_callback_fps=%.3f\ndecoded_fps=%.3f\n"
                "rtp_complete_fps=see_latency-capture_for_bounded_window\nidr_received=%u\nidr_decoded=%u\nperiodic_refresh_requests=%u\nidr_requests_total=unavailable\n"
                "packet_loss_exact=unavailable\n"
                "audio_input_latency_ms=unavailable\ndecode_average_scope=all_timed_decode_calls_in_session\n",
                options.target_fps,setup_fps_argument,options.vsync,
                stream_rate_per_second(stats.received,duration),stream_rate_per_second(stats.rendered,duration),
                (unsigned long long)duration,duration?stats.rendered*1000000.0/duration:0.0,
                duration?stats.received*1000000.0/duration:0.0,duration?stats.decoded*1000000.0/duration:0.0,
                idr_received,idr_decoded,(uint32_t)refresh_requests);
            for (int metric=0;metric<TIME_COUNT && n>0 && (size_t)n<sizeof(report);metric++) {
                const struct gpu_time_window *w=&pipeline_times[metric];
                n+=snprintf(report+n,sizeof(report)-n,"%s_avg_us=%.3f\n%s_max_session_us=%u\n",
                    names[metric],pipeline_calls[metric]?(double)pipeline_total[metric]/pipeline_calls[metric]:0.0,
                    names[metric],pipeline_peak[metric]);
                if(n<0 || (size_t)n>=sizeof(report)) break;
                uint32_t current=w->count?w->us[(w->next+GPU_TIME_SAMPLES-1)%GPU_TIME_SAMPLES]:0;
                n+=snprintf(report+n,sizeof(report)-n,"%s_us=%u\n%s_samples=%u\n%s_p95_us=%u\n%s_p99_us=%u\n%s_max_us=%u\n",
                    names[metric],current,names[metric],w->count,names[metric],gpu_time_percentile(w,95),
                    names[metric],gpu_time_percentile(w,99),names[metric],gpu_time_percentile(w,100));
            }
            if (n>0 && (size_t)n<sizeof(report)) storage_write("decoder-session.txt",report,(size_t)n);
        }
    }
    free_resources();
}

static void parallel_observe(uint32_t frame_number,uint64_t decode_us,bool idr)
{
    FFH264ParallelDiagnostic d;
    if (!options.stats_overlay || !ff_h264_xbox_parallel_diagnostic(codec_ctx,&d) || !d.slices)
        return;
    parallel_trace_entry *entry=&parallel_trace[parallel_trace_next];
    entry->frame=frame_number;
    entry->decode_us=decode_us>UINT_MAX?UINT_MAX:(uint32_t)decode_us;
    entry->idr=idr;
    entry->d=d;
    parallel_trace_next=(parallel_trace_next+1)%64;
    if(parallel_trace_count<64)parallel_trace_count++;
    parallel_totals.slices+=d.slices;
    for(int i=0;i<3;i++) {
        parallel_totals.raw_idc[i]+=d.raw_idc[i];
        parallel_totals.effective_filter[i]+=d.effective_filter[i];
    }
    parallel_totals.reference_slices+=d.reference_slices;
    parallel_totals.cabac_slices+=d.cabac_slices;
    parallel_totals.serial_dispatches+=d.serial_dispatches;
    parallel_totals.parallel_dispatches+=d.parallel_dispatches;
    parallel_totals.slice_ticks+=d.slice_ticks;
    parallel_totals.loopfilter_ticks+=d.loopfilter_ticks;
    parallel_totals.entropy_sample_ticks+=d.entropy_sample_ticks;
    parallel_totals.slice_calls+=d.slice_calls;
    parallel_totals.loopfilter_calls+=d.loopfilter_calls;
    parallel_totals.entropy_samples+=d.entropy_samples;
    if(d.forced_serial)parallel_forced_serial_frames++;
    if(d.reference_slices)parallel_reference_frames++;else parallel_nonreference_frames++;
    if(d.serial_dispatches && d.parallel_dispatches)parallel_mixed_frames++;
    else if(d.parallel_dispatches)parallel_parallel_frames++;
    else if(d.serial_dispatches)parallel_serial_frames++;
    if(d.max_dispatch_contexts>parallel_totals.max_dispatch_contexts)
        parallel_totals.max_dispatch_contexts=d.max_dispatch_contexts;
    if(d.max_worker_participants>parallel_totals.max_worker_participants)
        parallel_totals.max_worker_participants=d.max_worker_participants;
    if(d.max_concurrent_workers>parallel_totals.max_concurrent_workers)
        parallel_totals.max_concurrent_workers=d.max_concurrent_workers;
}

static int video_submit(PDECODE_UNIT du)
{
    submit_timing_scope timing;
    AVPacket pkt;
    PLENTRY e;
    uint64_t t0, dt;
    int off = 0, got = 0, ret;
    struct refresh_trace_frame *trace=NULL;

    if(video_get_stream_error()!=VIDEO_STREAM_OK)return DR_OK;

    if(InterlockedExchange(&decoder_reset_requested,0)) {
        /* Only this callback owns FFmpeg; slice workers have completed the
         * previous synchronous decode. Do not touch the renderer or audio. */
        close_h264(&codec_ctx);
        codec_ctx=open_h264(vid_w,vid_h,h264_workers(),options.fast_decode);
        if(!codec_ctx) {
            InterlockedExchange(&stream_error,VIDEO_STREAM_RESOURCE_ERROR);
            return DR_OK;
        }
        avcodec_free_frame(&dec_frame);
        dec_frame=avcodec_alloc_frame();
        if(!dec_frame) {
            InterlockedExchange(&stream_error,VIDEO_STREAM_RESOURCE_ERROR);
            return DR_OK;
        }
        need_idr=true;if(options.refresh_smoothing)cached_video_invalidate_history();
        video_progress_note(VP_DECODER_RESET);
        plat_log("[VIDEO] FFmpeg decoder reset on callback thread; requesting IDR\n");
        return DR_NEED_IDR;
    }

    if (!stats.received) {
        xdk_audit_thread("VideoDec/dispatch");
        plat_diagnostics_checkpoint("[FREEZE] first decoder callback\n");
    }
    if(options.refresh_smoothing) {
        uint32_t frame=(uint32_t)du->frameNumber;
        if(smoothing_have_input && frame-smoothing_last_input!=1)cached_video_invalidate_history();
        smoothing_last_input=frame;smoothing_have_input=true;
    }
    stats.received++;
    if(du->frameType==FRAME_TYPE_IDR)idr_received++;
    if(du->frameHostProcessingLatency)perf_record(PERF_HOST_PROCESSING,(uint32_t)du->frameHostProcessingLatency*100);
    if (options.stats_overlay) {
        uint64_t submitted = LiGetMicroseconds();
        EnterCriticalSection(&telemetry_lock);
        stream_observe(&observations, (uint32_t)du->frameNumber, du->fullLength,
                       du->frameHostProcessingLatency, du->receiveTimeUs, du->enqueueTimeUs, submitted);
        LeaveCriticalSection(&telemetry_lock);
    }

    if (du->frameType == FRAME_TYPE_IDR) {
        if (need_idr) {
            avcodec_flush_buffers(codec_ctx);
            need_idr = false;
        }
    } else if (need_idr) {
        /* Skip broken P-frames after a decode error so we never draw corrupted
         * macroblocks or waste CPU while waiting for the recovery frame. */
        InterlockedIncrement((volatile LONG *)&stats.dropped);
        return DR_OK; /* requestDecoderRefresh() already requested an IDR */
    }

    uint64_t prepare_start=options.stats_overlay?now_us():0;
    if (du->fullLength <= 0 || du->fullLength > INT_MAX - FF_INPUT_BUFFER_PADDING_SIZE) {
        stats.decode_errors++;
        need_idr=true;if(options.refresh_smoothing)cached_video_invalidate_history();
        return DR_NEED_IDR;
    }
    if (du->fullLength > pkt_cap) {
        uint8_t *n = (uint8_t *)_aligned_realloc(pkt_buf, du->fullLength + FF_INPUT_BUFFER_PADDING_SIZE, 128);
        if (n == NULL) {
            need_idr=true;if(options.refresh_smoothing)cached_video_invalidate_history();
            return DR_NEED_IDR;
        }
        pkt_buf = n;
        pkt_cap = du->fullLength;
    }
    for (e = du->bufferList; e != NULL; e = e->next) {
        if (!e->data || !xdk_fragment_fits(du->fullLength, off, e->length)) {
            stats.decode_errors++;
            need_idr=true;if(options.refresh_smoothing)cached_video_invalidate_history();
            return DR_NEED_IDR;
        }
        /* Network fragments need not be aligned; only decoded planes use XMemCpy. */
        memcpy(pkt_buf + off, e->data, e->length);
        off += e->length;
    }
    if (off != du->fullLength) {
        stats.decode_errors++;
        need_idr=true;if(options.refresh_smoothing)cached_video_invalidate_history();
        return DR_NEED_IDR;
    }
    memset(pkt_buf + off, 0, FF_INPUT_BUFFER_PADDING_SIZE);
    if (cap_buf != NULL)
        capture_frame(pkt_buf, off);

    av_init_packet(&pkt);
    pkt.data = pkt_buf;
    pkt.size = off;
    pkt.flags = du->frameType == FRAME_TYPE_IDR ? AV_PKT_FLAG_KEY : 0;
    /* FFmpeg get_buffer() stores this on the picture; output may belong to an
     * earlier call. Do not infer output identity from the current decode unit. */
    codec_ctx->reordered_opaque=(int64_t)video_picture_tag((uint32_t)du->frameNumber,
        (uint32_t)InterlockedCompareExchange(&refresh_generation,0,0),du->frameType==FRAME_TYPE_IDR);
    video_progress_note(VP_SUBMITTED);
    if (options.stats_overlay) {
        uint64_t prepared=now_us();
        if (prepared>=prepare_start) pipeline_sample(TIME_PACKET_PREPARE,prepared-prepare_start);
    }

    bool skipped=decode_deadline_before(&deadline, options.fast_decode!=0, options.target_fps,
                                     du->enqueueTimeUs, LiGetMicroseconds(), stats.avg_decode_us);
    /* Restore the original decoder-thread-only overload policy between calls. */
    codec_ctx->skip_loop_filter=(enum AVDiscard)h264_reference_filter(XDK_H264_REFERENCE_MODE,
        options.fast_decode!=0,skipped,AVDISCARD_DEFAULT,AVDISCARD_NONREF,AVDISCARD_ALL);
    if(options.stats_overlay || perf_enabled()) {
        uint64_t now=LiGetMicroseconds(),age=now>=du->enqueueTimeUs?now-du->enqueueTimeUs:0;
        trace=refresh_trace_begin(&refresh_trace,(uint32_t)du->frameNumber,(uint32_t)off,
            du->frameType==FRAME_TYPE_IDR,now,age>UINT32_MAX?UINT32_MAX:(uint32_t)age);
    }
    t0 = now_us();
    ret = avcodec_decode_video2(codec_ctx, dec_frame, &got, &pkt);
    dt = now_us() - t0;
    if(trace){trace->decode_us=(uint32_t)dt;trace->got=got;trace->ret=ret;
        trace->flags=got?av_frame_get_decode_error_flags(dec_frame):0;trace->output=got?(uint32_t)dec_frame->reordered_opaque>>1:0;}
    parallel_observe((uint32_t)du->frameNumber,dt,du->frameType==FRAME_TYPE_IDR);
    perf_record(PERF_H264_DECODE,dt);
    uint64_t post_decode_start=perf_enabled()?perf_clock():0;
    pipeline_sample(TIME_DECODE,dt);
    decode_deadline_after(&deadline, codec_ctx->skip_loop_filter != AVDISCARD_DEFAULT,
                          ret >= 0 && got, dt > UINT_MAX ? UINT_MAX : (uint32_t)dt);
    stats.avg_decode_us = deadline.decode_samples == 1 ? deadline.decode_us :
                         stream_smooth(stats.avg_decode_us, deadline.decode_us);
    if (options.stats_overlay) {
        EnterCriticalSection(&telemetry_lock);
        deadline_overlay = deadline;
        LeaveCriticalSection(&telemetry_lock);
    }
    if (ret < 0) {
        if (stats.decode_errors++ < 5)
            vlog("video: decode error %d on frame %d\n", ret, du->frameNumber);
        need_idr=true;if(options.refresh_smoothing)cached_video_invalidate_history();
        return DR_NEED_IDR;
    }
    if (got) {
        int decode_flags = av_frame_get_decode_error_flags(dec_frame);
        if (decode_flags) {
            if (stats.decode_errors++ < 20)
                vlog("[H264 CORRUPT] frame=%d flags=0x%x bytes=%d got=%d ret=%d workers=%d fast=%d\n",
                     du->frameNumber,decode_flags,off,got,ret,stats.decode_threads,h264_diagnostic.fast_flag);
            need_idr=true;if(options.refresh_smoothing)cached_video_invalidate_history();
            return DR_NEED_IDR;
        }
        if (!dec_frame->data[0] || !dec_frame->data[1] || !dec_frame->data[2]) {
            stats.decode_errors++;
            need_idr=true;if(options.refresh_smoothing)cached_video_invalidate_history();
            return DR_NEED_IDR;
        }
        if ((dec_frame->width!=vid_w || dec_frame->height!=vid_h) &&
            !resize_video(dec_frame->width,dec_frame->height))return DR_OK;
        /* Both upload paths read these many bytes per row. Reject malformed
         * output before either path copies from the decoder's allocation. */
        if(dec_frame->linesize[0]<vid_w || dec_frame->linesize[1]<chroma_w || dec_frame->linesize[2]<chroma_w) {
            stats.decode_errors++;need_idr=true;if(options.refresh_smoothing)cached_video_invalidate_history();return DR_NEED_IDR;
        }
        if (dec_frame->format != AV_PIX_FMT_YUV420P && dec_frame->format != AV_PIX_FMT_YUVJ420P) {
            stats.decode_errors++;need_idr=true;if(options.refresh_smoothing)cached_video_invalidate_history();return DR_NEED_IDR;
        }
        stats.decoded++;
        if(trace)refresh_trace_luma(trace,dec_frame->data[0],dec_frame->linesize[0],vid_w,vid_h);
        bool decoded_idr=video_picture_idr((uint64_t)dec_frame->reordered_opaque,dec_frame->key_frame!=0);
        if(decoded_idr)idr_decoded++;
        stats.actual_w=dec_frame->width;stats.actual_h=dec_frame->height;
        if(decoded_idr)
            video_refresh_decoded_idr(GetTickCount(),video_picture_request((uint64_t)dec_frame->reordered_opaque));
        video_progress_note(VP_DECODED);
        last_decoded_us=t0+dt;
        if(stats.decoded==1) {
            perf_mark(PERF_FIRST_DECODED);
            char freeze_frame[160];
            snprintf(freeze_frame,sizeof(freeze_frame),
                     "[FREEZE] first decoded %dx%d format=%d pitch=%d,%d,%d\n",
                     dec_frame->width,dec_frame->height,dec_frame->format,
                     dec_frame->linesize[0],dec_frame->linesize[1],dec_frame->linesize[2]);
            plat_diagnostics_checkpoint(freeze_frame);
        }
        decode_us_sum += dt;
        if(!launch_decode_notified){launch_decoded();launch_decode_notified=true;}
        if(stats.decoded==1)plat_diagnostics_checkpoint("[FREEZE] first frame upload entered\n");
        capture_decoded_yuv_once(dec_frame,du->frameNumber);
        publish_frame(dec_frame);
        if(stats.decoded==1)plat_diagnostics_checkpoint("[FREEZE] first frame upload returned\n");
    } else decode_no_picture++;
    if(post_decode_start)perf_record(PERF_POST_DECODE,perf_clock()-post_decode_start);
    log_stats();
    if (decode_deadline_needs_refresh(&deadline)) {
        need_idr=true;if(options.refresh_smoothing)cached_video_invalidate_history();
        return DR_NEED_IDR;
    }
    return DR_OK;
}

static void video_ffmpeg_init_callbacks(DECODER_RENDERER_CALLBACKS *cb)
{
    LiInitializeVideoCallbacks(cb);
    cb->setup = video_setup;
    cb->start = video_start;
    cb->stop = video_stop;
    cb->cleanup = video_cleanup;
    cb->submitDecodeUnit = video_submit;
    /* No direct submit: Moonlight's own decoder thread calls video_submit.
     * Reference frame invalidation recovers from lost packets without huge IDR bursts. */
    char *diagnostic_text = storage_read("video-h264.ini",NULL);
    video_h264_diagnostic_parse(diagnostic_text,&h264_diagnostic);
    free(diagnostic_text);
    cb->capabilities = CAPABILITY_SLICES_PER_FRAME(session_slices) |
                       (h264_diagnostic.rfi && !XDK_H264_NO_RFI ? CAPABILITY_REFERENCE_FRAME_INVALIDATION_AVC:0);
    vlog("[H264 DIAG] reference_mode=%d fast_flag=%d workers=%d rfi=%d capture_yuv=%d requested_slices=%d\n",
         XDK_H264_REFERENCE_MODE,h264_reference_fast_flag(XDK_H264_REFERENCE_MODE,h264_diagnostic.fast_flag,options.fast_decode),
         h264_workers(),h264_diagnostic.rfi && !XDK_H264_NO_RFI,
         h264_diagnostic.capture_yuv,session_slices);
}

static const struct video_decoder_backend *video_find_decoder_backend(int requested)
{
    static const struct video_decoder_backend backends[] = {
        {VIDEO_DECODER_FFMPEG, video_ffmpeg_init_callbacks}
    };
    int selected=video_decoder_resolve(requested);
    for(unsigned i=0;i<sizeof(backends)/sizeof(backends[0]);i++)
        if(backends[i].decoder==selected)return backends+i;
    return NULL;
}

static int video_decoder_unavailable_setup(int format,int width,int height,int fps,void *ctx,int flags)
{
    (void)format;(void)width;(void)height;(void)fps;(void)ctx;(void)flags;
    vlog("[DECODER] setup rejected: %s\n",video_decoder_unavailable_reason());
    return -1;
}

void video_init_callbacks(DECODER_RENDERER_CALLBACKS *cb)
{
    const struct video_decoder_backend *backend=video_find_decoder_backend(options.decoder_backend);
    if(!backend) {
        /* A second boundary for non-UI callers: reject setup, advertise no
         * capabilities, and never install callbacks for a nonexistent backend. */
        LiInitializeVideoCallbacks(cb);
        cb->setup=video_decoder_unavailable_setup;
        return;
    }
    if(options.decoder_backend==VIDEO_DECODER_AUTO)
        vlog("[DECODER] Auto selected FFmpeg: system GPU ABI unavailable in this build\n");
    backend->init_callbacks(cb);
}

/* ---------------------------------------------------- benchmark */

static const struct {
    const char *name;
    int flags;
    bool fast_decode;
    int reference_group;
    unsigned loopfilter_mask;
    int threads;
    bool thread_matrix;
} bench_configs[] = {
    { "C full", 0, false, 0, XVMX_LF_ALL, 4, false },
    { "QPEL full", XVMX_QPEL, false, 0, XVMX_LF_ALL, 4, false },
    { "Chroma full", XVMX_CHROMA, false, 0, XVMX_LF_ALL, 4, false },
    { "IDCT full", XVMX_IDCT, false, 0, XVMX_LF_ALL, 4, false },
    { "Loop Filter full", XVMX_LOOPFILTER, false, 0, XVMX_LF_ALL, 4, false },
    { "All VMX full", XVMX_ALL, false, 0, XVMX_LF_ALL, 4, false },
    { "All VMX C LF full", XVMX_ALL & ~XVMX_LOOPFILTER, false, 0, XVMX_LF_ALL, 4, false },
    { "Tuned C LF full", XVMX_QPEL | XVMX_IDCT | XVMX_PREFETCH, false, 0, 0, 4, false },
    { "Tuned V normal full", XVMX_QPEL | XVMX_IDCT | XVMX_PREFETCH | XVMX_LOOPFILTER,
      false, 0, XVMX_LF_VERTICAL & XVMX_LF_NORMAL, 4, false },
    { "Threads 3 FULL", XVMX_ALL & ~XVMX_LOOPFILTER, false, 0, 0, 3, true },
    { "Threads 4 FULL", XVMX_ALL & ~XVMX_LOOPFILTER, false, 0, 0, 4, true },
    { "Threads 5 FULL", XVMX_ALL & ~XVMX_LOOPFILTER, false, 0, 0, 5, true },
    { "C fast (reference)", 0, true, 1, XVMX_LF_ALL, 4, false },
    { "All VMX fast", XVMX_ALL, true, 1, XVMX_LF_ALL, 4, false },
    { "Threads 3 FAST", XVMX_ALL & ~XVMX_LOOPFILTER, true, 1, 0, 3, true },
    { "Threads 4 FAST", XVMX_ALL & ~XVMX_LOOPFILTER, true, 1, 0, 4, true },
    { "Threads 5 FAST", XVMX_ALL & ~XVMX_LOOPFILTER, true, 1, 0, 5, true },
};
#define BENCH_RUNS (sizeof(bench_configs) / sizeof(bench_configs[0]))

struct bench {
    uint8_t *data;              /* capture file */
    uint32_t w, h, frames;
    int captured_fps,captured_bitrate,captured_slices; /* 0=unknown, sidecar validated */
    size_t size;
    uint32_t *ref_hash;         /* reused by each filtering group's C reference */
    uint32_t plane_hash[600][3]; /* C full, stable bytewise Y/U/V for PC comparison */
    volatile int run;           /* configuration in progress */
    volatile int frame;
    double ms[BENCH_RUNS], worst_ms[BENCH_RUNS];
    uint32_t mismatches[BENCH_RUNS], decoded[BENCH_RUNS];
    uint32_t reference_count;
    bool reference_valid;
    double p50[BENCH_RUNS], p95[BENCH_RUNS], p99[BENCH_RUNS];
    bool failed[BENCH_RUNS];
    volatile bool done;
};

/* FNV-1a over the visible picture, 32 bits at a time. */
static uint32_t frame_hash(const AVFrame *f, int w, int h)
{
    uint32_t hash = 2166136261u;
    int p, y, x;

    for (p = 0; p < 3; p++) {
        int pw = p ? (w + 1) / 2 : w, ph = p ? (h + 1) / 2 : h;
        for (y = 0; y < ph; y++) {
            const uint8_t *row = f->data[p] + y * f->linesize[p];
            for (x = 0; x + 4 <= pw; x += 4)
                hash = (hash ^ *(const uint32_t *)(row + x)) * 16777619u;
            for (; x < pw; x++)
                hash = (hash ^ row[x]) * 16777619u;
        }
    }
    return hash;
}

static uint32_t frame_plane_hash(const AVFrame *f, int w, int h, int plane)
{
    uint32_t hash=2166136261u;
    int pw=plane?(w+1)/2:w,ph=plane?(h+1)/2:h;
    for(int y=0;y<ph;y++) {
        const uint8_t *row=f->data[plane]+y*f->linesize[plane];
        for(int x=0;x<pw;x++)hash=(hash^row[x])*16777619u;
    }
    return hash;
}

static int compare_times(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

static void bench_run(struct bench *b, int run)
{
    AVCodecContext *c;
    AVFrame *frame = avcodec_alloc_frame();
    uint8_t *buf = (uint8_t *)malloc(2 * 1024 * 1024 + FF_INPUT_BUFFER_PADDING_SIZE);
    const uint8_t *p = b->data + 16;
    uint64_t total = 0, worst = 0;
    uint32_t i, out = 0, calls = 0;
    uint32_t *times = (uint32_t *)malloc(b->frames * sizeof(uint32_t));
    bool reference = run == 0 || bench_configs[run].reference_group != bench_configs[run - 1].reference_group;

    if (!reference && !b->reference_valid) b->failed[run] = true;
    ff_xbox_vmx_flags = bench_configs[run].flags;
    ff_xbox_loopfilter_mask = bench_configs[run].loopfilter_mask;
    c = open_h264(b->w, b->h, bench_configs[run].threads, bench_configs[run].fast_decode);
    if (c == NULL || frame == NULL || buf == NULL || times == NULL) {
        b->failed[run] = true;
        if (reference) { b->reference_count = 0; b->reference_valid = false; }
        free(times);
        close_h264(&c);
        if (frame)
            avcodec_free_frame(&frame);
        free(buf);
        return;
    }

    for (i = 0; i < b->frames; i++) {
        AVPacket pkt;
        uint32_t len;
        uint64_t t0, dt;
        int got = 0, ret;

        b->frame = i;
        if ((size_t)(p - b->data) > b->size || b->size - (size_t)(p - b->data) < 4)
            break;
        memcpy(&len, p, 4);
        if (len == 0 || len > 2 * 1024 * 1024 || len > b->size - (size_t)(p - b->data) - 4)
            break;
        memcpy(buf, p + 4, len);
        memset(buf + len, 0, FF_INPUT_BUFFER_PADDING_SIZE);
        p += 4 + len;

        av_init_packet(&pkt);
        pkt.data = buf;
        pkt.size = len;
        t0 = now_us();
        ret = avcodec_decode_video2(c, frame, &got, &pkt);
        dt = now_us() - t0;
        times[calls++] = (uint32_t)dt;
        total += dt;
        if (dt > worst)
            worst = dt;
        if (ret < 0) { b->failed[run] = true; break; }
        if (got) {
            uint32_t hsh = frame_hash(frame, b->w, b->h);
            if (run==0 && out<600)
                for(int plane=0;plane<3;plane++)
                    b->plane_hash[out][plane]=frame_plane_hash(frame,b->w,b->h,plane);
            if (reference)
                b->ref_hash[out] = hsh;
            else if (out >= b->reference_count || hsh != b->ref_hash[out])
                b->mismatches[run]++;
            out++;
        }
    }
    if (calls != b->frames || !out) b->failed[run] = true;
    if (reference) {
        b->reference_valid = !b->failed[run];
        b->reference_count = b->failed[run] ? 0 : out;
    }
    else if (out != b->reference_count) b->mismatches[run]++;
    qsort(times, calls, sizeof(uint32_t), compare_times);
    b->p50[run] = calls ? times[(calls * 50 + 99) / 100 - 1] / 1000.0 : 0;
    b->p95[run] = calls ? times[(calls * 95 + 99) / 100 - 1] / 1000.0 : 0;
    b->p99[run] = calls ? times[(calls * 99 + 99) / 100 - 1] / 1000.0 : 0;
    b->decoded[run] = out;
    b->ms[run] = calls ? total / 1000.0 / calls : 0;
    b->worst_ms[run] = worst / 1000.0;
    vlog("bench: %s T%d filter=%s avg %.2f worst %.2f p50 %.2f p95 %.2f p99 %.2f ms, decoded %u, mismatches %u, failed %d\n",
         bench_configs[run].name, bench_configs[run].threads,
         XDK_H264_REFERENCE_MODE==1 ? "FULL_REFERENCE_SAFE" :
         XDK_H264_REFERENCE_MODE==2 ? (bench_configs[run].fast_decode ? "NONREF_ONLY" : "FULL") :
         bench_configs[run].fast_decode ? "FAST" : "FULL",
         b->ms[run], b->worst_ms[run], b->p50[run], b->p95[run], b->p99[run], out, b->mismatches[run], b->failed[run]);
    free(times);

    close_h264(&c);
    avcodec_free_frame(&frame);
    free(buf);
}

static int bench_best_threads(const struct bench *b,bool fast)
{
    int best=-1;
    for(int i=0;i<(int)BENCH_RUNS;i++) {
        if(!bench_configs[i].thread_matrix || bench_configs[i].fast_decode!=fast ||
           b->failed[i] || b->mismatches[i] || !b->decoded[i]) continue;
        if(best<0 || b->p99[i]<b->p99[best] ||
           (b->p99[i]==b->p99[best] && bench_configs[i].threads==DECODE_THREADS)) best=i;
    }
    return best<0?0:bench_configs[best].threads;
}

static DWORD WINAPI bench_main(LPVOID arg)
{
    struct bench *b = (struct bench *)arg;
    xdk_audit_thread("DecoderBenchmark");
    int saved = ff_xbox_vmx_flags, run;
    unsigned saved_lf_mask = ff_xbox_loopfilter_mask;

    for (run = 0; run < (int)BENCH_RUNS; run++) {
        b->run = run;
        bench_run(b, run);
    }
    ff_xbox_vmx_flags = saved;
    ff_xbox_loopfilter_mask = saved_lf_mask;
    b->done = true;
    return 0;
}

bool plat_benchmark_available(void)
{
    return storage_dir() != NULL;
}

void plat_run_benchmark(void)
{
    struct bench b;
    struct ui_stat rows[BENCH_RUNS + 1];
    HANDLE th;
    size_t len = 0;
    char msg[320];
    char report[8192];
    size_t report_used = 0;
    int i, n = 0;

    memset(&b, 0, sizeof(b));
    b.data = (uint8_t *)storage_read(CAPTURE_FILE, &len);
    if (b.data == NULL || len < 16 || memcmp(b.data, "MLC1", 4) != 0) {
        free(b.data);
        cap_armed = true;
        ui_draw_message(ICON_INFO, "Recording armed",
                        "The next stream records up to 600 frames / 40 MiB (about 10 seconds at 60 FPS). "
                        "Record each requested slice count separately. Return here after streaming.");
        ui_present();
        while (!(input_menu_pressed() & (A_FLAG | B_FLAG)))
            Sleep(16);
        return;
    }
    memcpy(&b.w, b.data + 4, 4);
    memcpy(&b.h, b.data + 8, 4);
    memcpy(&b.frames, b.data + 12, 4);
    b.size = len;
    if (!xdk_capture_valid(b.data, b.size)) {
        free(b.data);
        ui_draw_message(ICON_ERROR, "Invalid recording", "Delete moonlight/capture.bin and record again.");
        ui_present();
        while (!(input_menu_pressed() & (A_FLAG | B_FLAG)))
            Sleep(16);
        return;
    }
    char *metadata=storage_read("capture-info.txt",NULL);
    if(metadata) {
        struct xdk_capture_info info;
        if(xdk_capture_info_parse(metadata,b.data,len,&info)) {
            b.captured_fps=info.fps;b.captured_bitrate=info.bitrate;b.captured_slices=info.slices;
        }
        free(metadata);
    }
    b.ref_hash = (uint32_t *)calloc(b.frames + 1, sizeof(uint32_t));
    if (b.ref_hash == NULL) {
        free(b.data);
        return;
    }
    vlog("bench: %ux%u, %u frames, threads %d\n", b.w, b.h, b.frames, DECODE_THREADS);

    th = CreateThread(NULL, 256 * 1024, bench_main, &b, CREATE_SUSPENDED, NULL);
    if (th == NULL) {
        free(b.ref_hash);
        free(b.data);
        return;
    }
    XSetThreadProcessor(th, 0);
    ResumeThread(th);
    while (!b.done) {
        _snprintf(msg, sizeof(msg), "%s: frame %d of %u", bench_configs[b.run].name, b.frame + 1, b.frames);
        msg[sizeof(msg) - 1] = '\0';
        ui_draw_busy("Decoder benchmark", msg, GetTickCount());
        ui_present();
        Sleep(100);
    }
    WaitForSingleObject(th, INFINITE);
    CloseHandle(th);

    report_used=(size_t)snprintf(report,sizeof(report),"capture=%ux%u frames=%u requested_fps=%d bitrate_kbps=%d slices_requested=%d\n"
        "reference_mode=%d fast_flag_effective=%d\n"
        "provenance=%s; encoded_slice_count=NOT_ANALYZED; thread tests reuse identical bitstream\n"
        "streaming_default_threads=4; selection_does_not_change_configuration\n",
        b.w,b.h,b.frames,b.captured_fps,b.captured_bitrate,b.captured_slices,
        XDK_H264_REFERENCE_MODE,h264_reference_fast_flag(XDK_H264_REFERENCE_MODE,h264_diagnostic.fast_flag,options.fast_decode),
        b.captured_slices?"bound capture-info sidecar":"UNKNOWN (legacy/missing/stale sidecar)");
    /* Offline benchmark only. Keep actual native results outside the short
     * RAM log ring, and never write a report per streaming frame. */
    for (i = 0; i < (int)BENCH_RUNS; i++) {
        int written = snprintf(report + report_used, sizeof(report) - report_used,
            "%s threads=%d filter=%s avg=%.3f p50=%.3f p95=%.3f p99=%.3f max=%.3f ms frames=%u mismatches=%u failed=%d\n",
            bench_configs[i].name, bench_configs[i].threads,
            XDK_H264_REFERENCE_MODE==1 ? "FULL_REFERENCE_SAFE" :
            XDK_H264_REFERENCE_MODE==2 ? (bench_configs[i].fast_decode ? "NONREF_ONLY" : "FULL") :
            bench_configs[i].fast_decode ? "FAST" : "FULL",
            b.ms[i], b.p50[i], b.p95[i], b.p99[i], b.worst_ms[i], b.decoded[i], b.mismatches[i], b.failed[i]);
        if (written < 0 || (size_t)written >= sizeof(report) - report_used) break;
        report_used += written;
    }
    int tail=snprintf(report+report_used,sizeof(report)-report_used,"offline_best_p99_full_threads=%d\noffline_best_p99_fast_threads=%d\n"
        "recommendation=diagnostic_only; live render/audio/input contention not reproduced; default stays 4\n",bench_best_threads(&b,false),bench_best_threads(&b,true));
    if(tail>0 && (size_t)tail<sizeof(report)-report_used) report_used+=tail;
    if (!storage_write("decoder-benchmark.txt", report, report_used))
        vlog("Benchmark report could not be saved\n");
    if(!b.failed[0] && b.decoded[0]==b.frames) {
        char *hashes=(char*)malloc(32768);
        if(hashes) {
            size_t used=0;
            int written=snprintf(hashes,32768,"capture_bytes=%u\ncapture_fingerprint=%08x\nwidth=%u\nheight=%u\nframes=%u\ncolumns=frame,Y,U,V (FNV1a32 visible bytes)\n",
                (unsigned)len,xdk_capture_fingerprint(b.data,len),b.w,b.h,b.frames);
            if(written>0 && written<32768)used=(size_t)written;
            for(uint32_t frame=0;frame<b.frames && used<32768;frame++) {
                written=snprintf(hashes+used,32768-used,"%u,%08x,%08x,%08x\n",frame,
                    b.plane_hash[frame][0],b.plane_hash[frame][1],b.plane_hash[frame][2]);
                if(written<0 || (size_t)written>=32768-used)break;
                used+=(size_t)written;
            }
            if(used)storage_write("decoder-frame-hashes.txt",hashes,used);
            free(hashes);
        }
    }

    for (i = 0; i < (int)BENCH_RUNS; i++) {
        rows[n].label = bench_configs[i].name;
        _snprintf(rows[n].value, sizeof(rows[n].value), "avg%.1f 50/95/99 %.1f/%.1f/%.1f max%.1f n%u !%u%s",
                  b.ms[i], b.p50[i], b.p95[i], b.p99[i], b.worst_ms[i], b.decoded[i], b.mismatches[i],
                  b.failed[i] ? " FAIL" : "");
        rows[n].value[sizeof(rows[n].value) - 1] = '\0';
        n++;
    }
    int page=0;const int per_page=6;
    while(true) {
        int first=page*per_page,count=n-first;if(count>per_page)count=per_page;
        _snprintf(msg,sizeof(msg),"Page %d/%d; LB/RB browse, A/B exit. ms; 60 FPS budget 16.67. Slices requested %d (0 unknown), capture %ux%u. "
            "Best p99 FULL T%d / FAST T%d; offline only, default remains 4.",page+1,(n+per_page-1)/per_page,b.captured_slices,b.w,b.h,
            bench_best_threads(&b,false),bench_best_threads(&b,true));
        msg[sizeof(msg)-1]=0;ui_draw_stats("Decoder benchmark",rows+first,count,msg);ui_present();
        int key=input_menu_pressed();if(key&(A_FLAG|B_FLAG))break;
        if(key&(RB_FLAG|RIGHT_FLAG))page=(page+1)%((n+per_page-1)/per_page);
        if(key&(LB_FLAG|LEFT_FLAG))page=(page+(n+per_page-1)/per_page-1)%((n+per_page-1)/per_page);
        Sleep(16); /* offline benchmark UI only, never streaming */
    }

    free(b.ref_hash);
    free(b.data);
}

void video_get_session_performance(struct performance_sample *out)
{
    /* Called after LiStopConnection joined the producer and renderer. */
    memset(out,0,sizeof(*out));out->frames=stats.decoded;out->drops=stats.dropped;
    out->last_video_packet_age_ms=last_packet_age_stop_ms;
    out->last_decoded_age_ms=last_decode_age_stop_ms;
    out->last_presented_age_ms=last_present_age_stop_ms;
    out->requested_fps=options.target_fps;out->setup_fps_argument=setup_fps_argument;
    /* No host-confirmed FPS exists in this MCC callback contract. */
    out->negotiated_fps_known=false;
    out->budget_us=deadline.budget_us;out->target_fps=options.target_fps;
    /* log_stats resets decode_us_sum periodically. Use the telemetry's whole-
     * session sum/count, or explicitly fall back to the EMA when it is OFF. */
    out->decode_avg_us=pipeline_calls[TIME_DECODE]?
        (uint32_t)(pipeline_total[TIME_DECODE]/pipeline_calls[TIME_DECODE]):stats.avg_decode_us;
    out->callback_us=pipeline_calls[TIME_CALLBACK_TOTAL]?
        (uint32_t)(pipeline_total[TIME_CALLBACK_TOTAL]/pipeline_calls[TIME_CALLBACK_TOTAL]):0;
    out->tail_valid=pipeline_times[TIME_DECODE].count>=30;
    if(out->tail_valid) {
        out->decode_p95_us=gpu_time_percentile(&pipeline_times[TIME_DECODE],95);
        out->decode_p99_us=gpu_time_percentile(&pipeline_times[TIME_DECODE],99);
        out->callback_p99_us=gpu_time_percentile(&pipeline_times[TIME_CALLBACK_TOTAL],99);
        out->gap_p99_us=gpu_time_percentile(&pipeline_times[TIME_CALLBACK_GAP],99);
        out->fence_p99_us=gpu_time_percentile(&pipeline_times[TIME_FENCE_WAIT],99);
    }
    out->queue_us=deadline.queue_valid?deadline.queue_us:0;out->queue_peak_us=deadline.session_max_queue_us;
    out->mcc_queue=network_video_queue;out->rtt_valid=network_valid;out->rtt_ms=network_rtt;
    out->starvation=gpu_slot_starvation;out->failovers=cached_failovers;out->frame_gaps=observations.frame_gaps;
    uint64_t duration=streaming_end_us>streaming_start_us?streaming_end_us-streaming_start_us:0;
    out->callback_fps=stream_rate_per_second(stats.received,duration);
    out->decoded_fps=stream_rate_per_second(stats.decoded,duration);
    out->presented_fps=stream_rate_per_second(stats.rendered,duration);
    out->complete_frames=last_complete_frames;
    out->complete_frame_fps=stream_rate_per_second(last_complete_frames,rtp_counter_duration_us);
    out->video_packets=last_video_packets;out->video_bytes=last_video_bytes;
    out->expected_video_data_packets=last_rtp_stats.packetCountVideo;
    out->expected_fec_packets=last_rtp_stats.packetCountFec;
    out->accepted_data_packets=last_data_packets;out->accepted_fec_packets=last_fec_packets;
    out->present_duplicate_local_sequence=present_duplicates;
    out->fec_recovered=last_rtp_stats.packetCountFecRecovered;
    out->fec_failed=last_rtp_stats.packetCountFecFailed;
    out->oos=last_rtp_stats.packetCountOOS;out->invalid_packets=last_rtp_stats.packetCountInvalid;
    out->late_packets=last_late_packets;out->lost_frames=last_lost_frames;
    if(duration) {out->shown_fps=stats.rendered*1000000.0/duration;
        if(options.stats_overlay)out->payload_mbps=observations.payload_bytes*8.0/duration;}
}
