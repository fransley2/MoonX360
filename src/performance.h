#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "video_watchdog.h"
#define PERF_SAMPLES 128
/* Each metric has one producer. Read/reset only after its worker is joined. */
struct perf_window {uint32_t samples[PERF_SAMPLES],count,filled,next,maximum,current,ema;uint64_t total;};
static void perf_window_add(struct perf_window *w,uint64_t us)
{
    uint32_t value=us>0xFFFFFFFFu?0xFFFFFFFFu:(uint32_t)us;
    w->samples[w->next]=value;w->next=(w->next+1)%PERF_SAMPLES;
    if(w->filled<PERF_SAMPLES)w->filled++;
    w->ema=w->count?(uint32_t)(((uint64_t)w->ema*15+value)/16):value;
    w->current=value;w->count++;w->total+=value;if(value>w->maximum)w->maximum=value;
}
enum perf_metric {PERF_TCP,PERF_TLS,PERF_RTSP,PERF_NEGOTIATION,PERF_COVER_HTTP,PERF_COVER_DECODE,
    PERF_UI_BUILD,PERF_UI_UPLOAD,PERF_HOME_READ,PERF_INPUT_INTERVAL,PERF_INPUT_WORK,PERF_INPUT_SEND,
    PERF_AUDIO_CALLBACK,PERF_AUDIO_DECODE,PERF_AUDIO_GLITCHES,PERF_AUDIO_QUEUE_FULL,PERF_AUDIO_SUBMIT_FAIL,
    PERF_CONTROLLER_SAMPLE_ENQUEUE,PERF_CONTROLLER_QUEUE_SEND,PERF_CONTROLLER_LOCAL,
    PERF_INPUT_QUEUE_DEPTH,PERF_INPUT_QUEUE_AGE,PERF_INPUT_OLDEST_AGE,PERF_INPUT_SEND_WORK,
    PERF_VIDEO_RECV_SAMPLED,PERF_VIDEO_DECRYPT_SAMPLED,PERF_VIDEO_RTP_SAMPLED,
    PERF_VIDEO_RTP_FRAME_SPAN,PERF_VIDEO_RTP_WAIT,PERF_VIDEO_DEPACKETIZE,PERF_VIDEO_DU_DEPTH,
    PERF_VIDEO_DU_AGE,PERF_HOST_PROCESSING,PERF_AUDIO_BUFFERS,PERF_AUDIO_QUEUED_MS,
    PERF_Y_TRANSFER,PERF_U_TRANSFER,PERF_V_TRANSFER,PERF_YUV_TRANSFER,
    PERF_CACHED_FREE_SLOTS,PERF_LEGACY_TEXTURE_UPLOAD,PERF_FRAME_READY_PRESENT,PERF_PRESENT_INTERVAL,
    PERF_PRESENT_CALL,PERF_H264_DECODE,PERF_POST_DECODE,PERF_CPU_SUBMIT,PERF_D3D_ACQUIRE,PERF_COUNT};
enum perf_counter {PERF_C_INPUT_COALESCED,PERF_C_INPUT_QUEUE_FULL,PERF_C_VIDEO_PACKETS,
    PERF_C_VIDEO_BYTES,PERF_C_VIDEO_FRAMES_COMPLETE,PERF_C_VIDEO_LATE,
    PERF_C_VIDEO_OOS,PERF_C_VIDEO_FEC_RECOVERED,PERF_C_VIDEO_FEC_FAILED,
    PERF_C_VIDEO_FRAMES_LOST,PERF_C_VIDEO_INVALID,PERF_C_VIDEO_WAIT_RECOVERED,
    PERF_C_VIDEO_WAIT_LOST,PERF_C_VIDEO_DATA_ACCEPTED,PERF_C_VIDEO_FEC_ACCEPTED,PERF_C_VIDEO_QUEUE_OVERFLOW,PERF_C_COUNT};
struct perf_pressure {uint32_t recovered,missing,lost,overflows;bool valid;};
struct perf_summary {uint32_t current,ema,p50,p95,p99,max,count;uint64_t total;};
enum perf_event {PERF_CONNECT_BEGIN,PERF_LAUNCH_RESPONSE,PERF_CONNECTED,PERF_FIRST_PACKET,PERF_FIRST_DECODED,PERF_FIRST_PRESENTED,PERF_STOP_BEGIN,PERF_RETURN_MENU,PERF_EVENT_COUNT};
enum perf_fps_stage {PERF_FPS_PROFILE,PERF_FPS_STREAM_CONFIG,PERF_FPS_LAUNCH_REQUEST,
    PERF_FPS_VIDEO_OPTIONS,PERF_FPS_MOONLIGHT_COPY,PERF_FPS_SDP,PERF_FPS_STAGE_COUNT};
#ifdef __cplusplus
extern "C" {
#endif
#ifdef _XBOX
extern volatile int perf_active;
void perf_pressure_read(struct perf_pressure *out);
static inline bool perf_enabled(void){return perf_active!=0;}
uint64_t perf_clock(void);
void perf_begin(bool enabled);
void perf_record(int metric,uint64_t us);
void perf_count(int counter,uint32_t amount);
void perf_video_packet_seen(uint32_t bytes);
void video_progress_note(int stage);
void video_progress_new_present(void);
void video_progress_snapshot_get(struct video_progress_snapshot *out);
void video_progress_queue_age(uint32_t age_ms);
void video_progress_gpu_wait(uint32_t wait_ms);
uint32_t perf_last_video_packet_age_ms(void); /* after video receive worker join; UINT32_MAX if absent */
bool perf_video_tracking_enabled(void);
void perf_request_fps(int stage,int fps,int client_refresh_x100);
void perf_frame_complete(uint32_t frame_number);
uint64_t perf_counter_read(int counter); /* after worker join */
/* Current window, before perf_flush publishes last-session snapshots. Cold only. */
uint64_t perf_window_counter_read(int counter);
uint64_t perf_window_duration_us(void);
uint64_t perf_last_counter_read(int counter); /* capture window, or full session when unbounded */
void perf_get_summary(int metric,struct perf_summary *out); /* after worker join */
void perf_input_sample(int controller,uint64_t detected_us);
uint64_t perf_input_sample_time(int controller);
void perf_capture_start(bool bounded);
bool perf_capture_tick(void); /* true once when the 45 s capture closes */
void perf_capture_close(void); /* early stream stop; no teardown samples */
uint64_t perf_capture_duration_us(void);
void perf_mark(int event);
void perf_flush(void);
void perf_flush_menu(void);
void perf_ui_begin(void);
void perf_ui_end(void);
#else
static inline void perf_pressure_read(struct perf_pressure *out){memset(out,0,sizeof(*out));}
static inline bool perf_enabled(void){return false;}
static inline uint64_t perf_clock(void){return 0;}
static inline void perf_begin(bool enabled){(void)enabled;}
static inline void perf_record(int metric,uint64_t us){(void)metric;(void)us;}
static inline void perf_count(int counter,uint32_t amount){(void)counter;(void)amount;}
static inline void perf_video_packet_seen(uint32_t bytes){(void)bytes;}
static inline void video_progress_note(int stage){(void)stage;}
static inline void video_progress_new_present(void){}
static inline void video_progress_snapshot_get(struct video_progress_snapshot *out){memset(out,0,sizeof(*out));}
static inline void video_progress_queue_age(uint32_t age_ms){(void)age_ms;}
static inline void video_progress_gpu_wait(uint32_t wait_ms){(void)wait_ms;}
static inline uint32_t perf_last_video_packet_age_ms(void){return UINT32_MAX;}
static inline bool perf_video_tracking_enabled(void){return false;}
static inline void perf_request_fps(int stage,int fps,int client_refresh_x100){(void)stage;(void)fps;(void)client_refresh_x100;}
static inline void perf_frame_complete(uint32_t frame_number){(void)frame_number;}
static inline uint64_t perf_counter_read(int counter){(void)counter;return 0;}
static inline uint64_t perf_window_counter_read(int counter){(void)counter;return 0;}
static inline uint64_t perf_window_duration_us(void){return 0;}
static inline uint64_t perf_last_counter_read(int counter){(void)counter;return 0;}
static inline void perf_get_summary(int metric,struct perf_summary *out){(void)metric;memset(out,0,sizeof(*out));}
static inline void perf_input_sample(int controller,uint64_t detected_us){(void)controller;(void)detected_us;}
static inline uint64_t perf_input_sample_time(int controller){(void)controller;return 0;}
static inline void perf_capture_start(bool bounded){(void)bounded;}
static inline bool perf_capture_tick(void){return false;}
static inline void perf_capture_close(void){}
static inline uint64_t perf_capture_duration_us(void){return 0;}
static inline void perf_mark(int event){(void)event;}
static inline void perf_flush(void){}
static inline void perf_flush_menu(void){}
static inline void perf_ui_begin(void){}
static inline void perf_ui_end(void){}
#endif
#ifdef __cplusplus
}
class PerfScope {
    int metric;uint64_t started;
public:
    explicit PerfScope(int value):metric(value),started(perf_enabled()?perf_clock():0){}
    ~PerfScope(){if(started)perf_record(metric,perf_clock()-started);}
};
#endif
