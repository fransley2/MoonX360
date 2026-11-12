#pragma once

#include <stdbool.h>
#include "fast_decode.h"
#include "video_decoder.h"
#include <stdint.h>
#include <Limelight.h>
#include "performance_advisor.h"
struct video_progress_snapshot;

enum video_renderer {
    RENDERER_GPU,          /* Xenos shader does YUV->RGB and scaling (default) */
    RENDERER_CPU,          /* lookup-table conversion on hardware thread 4 */
    RENDERER_CPU_HWCOLOR,  /* experimental: display colour matrix converts */
    RENDERER_COUNT
};

struct video_options {
    bool fast_decode;   /* On/Off; fixed for this streaming session */
    int renderer;       /* enum video_renderer */
    bool vsync;         /* GPU renderer: flip on vertical blank (no tearing) */
    bool stats_overlay; /* SDK panel; updated at 1 Hz, disabled by default */
    int target_fps;
    int bitrate_kbps;
    int width, height;  /* requested geometry, available before callback negotiation */
    int slice_count;    /* 0 Auto; diagnostic override 8, 12, 16 */
    int color_space;    /* same COLORSPACE_* requested from the host */
    int overlay_mode;  /* 0 preserves legacy boolean behavior, 1 Basic, 2 Advanced, 3 Developer */
    int shader_mode;   /* existing shader policy: 0 Auto, 1 Fast, 2 Quality */
    int decode_workers;
    int thread_layout;
    int refresh_interval_ms; /* 0 Off; 125, 250, 1000..60000 ms */
    int packet_size; /* negotiated request, diagnostic identity */
    int refresh_smoothing,smoothing_strength;
    bool crt_clarity; /* opt-in spatial interlace filter, hardware/output-gated */
    char host_name[64]; /* selected host label, copied before connection */
    uint32_t connection_started_ms; /* cold trace origin, before host launch */
    int decoder_backend; /* enum video_decoder; frozen before connection setup */
};

const char *video_renderer_name(int renderer);
void video_set_options(const struct video_options *opts);
/* Backend-supported YUV matrix, shared by host request and renderer setup. */
int video_colorspace(int width, int height);
void video_init_callbacks(DECODER_RENDERER_CALLBACKS *cb);

/* Display geometry of the Xenos scanout surface (valid after console_init). */
void video_get_display_size(int *w, int *h);

struct video_stats {
    uint32_t received, decoded, rendered, dropped, decode_errors;
    uint32_t avg_decode_us, avg_render_us;
    /* SDK-only diagnostic EMAs; zero on backends that do not collect them. */
    uint32_t avg_publish_us, avg_upload_us, avg_gpu_wait_us;
    uint32_t render_superseded; /* already-decoded pictures replaced before upload */
    int display_w, display_h;
    int actual_w, actual_h; /* valid decoded picture, never requested geometry */
    int decode_threads;     /* FFmpeg threads actually active */
    int renderer;           /* renderer actually used */
};
void video_get_stats(struct video_stats *out);
struct video_quick_info {
    int requested_w,requested_h,requested_fps,bitrate_kbps,slices;
    int renderer,fast_decode,shader_mode,decode_workers;
    char host_name[64];
    char pipeline[32]; /* active GPU upload path, not requested renderer */
    bool network_valid;
    uint32_t rtt_ms,variance_ms,queue_age_ms;
};
void video_get_quick_info(struct video_quick_info *out);
enum video_refresh_state { VIDEO_REFRESH_IDLE, VIDEO_REFRESH_REQUESTED,
    VIDEO_REFRESH_WAITING, VIDEO_REFRESH_DECODED, VIDEO_REFRESH_UNAVAILABLE };
struct video_refresh_snapshot {
    int state;
    uint32_t requests, decoded_idrs, last_idr_ms;
};
/* Input thread only requests transport IDRs. Decoder callback owns completion. */
bool video_request_picture_refresh(void);
void video_refresh_tick(uint32_t now_ms);
void video_get_refresh_snapshot(struct video_refresh_snapshot *out);
enum video_stream_error { VIDEO_STREAM_OK, VIDEO_STREAM_UNSUPPORTED_SIZE, VIDEO_STREAM_RESOURCE_ERROR };
/* Polled by the app; decoder callbacks must never stop their own transport. */
int video_get_stream_error(void);
void video_get_progress(struct video_progress_snapshot *out);
unsigned video_recover_frame_queue(void);
void video_request_decoder_reset(void);
/* Sampled by app.c only while LiStartConnection has completed and before stop. */
void video_set_network_stats(uint32_t rtt, uint32_t variance, bool valid,
                             int video_queue, int audio_queue_ms);

/* After stop only: no new locks/clocks are added to the decoder for this snapshot.
 * Tail metrics remain unavailable when the existing diagnostic collector was OFF. */
void video_get_session_performance(struct performance_sample *out);
