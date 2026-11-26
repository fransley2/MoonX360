#pragma once
/* Unified MoonX360 Performance Overlay layout, alert semantics, and rasterizer.
 * Platform-independent (uses canvas.h, font.h, design_system.h); compiled into
 * both Xbox 360 XDK and PC host regression fixtures.
 *
 * Developer Metric Glossary (all values are real, instrumented measurements):
 * - FPS R/D/P   : 1-second stage rates — Received (callback) / Decoded / Presented FPS.
 * - Dec avg     : Exponential moving average (EMA) of H.264 decode time (ms).
 * - p50/95/99(N): Median, 95th and 99th percentile decode time (ms) over last N samples (up to 256).
 * - RTT / Jit   : Estimated network round-trip time and variance/jitter (ms).
 * - Bitrate     : 1-second measured video payload throughput (Mbps).
 * - Queue       : Pending video frames in queue (fr) and queue age (ms).
 * - Frame age   : Measured assembly + queue + decode latency (ms).
 * - Dropped     : Frames skipped/dropped before display (s:N = superseded in render slot).
 * - RTP / FEC   : L: lost frames, R: FEC-recovered packets, F: FEC failures (G:N = frame gaps).
 * - Up / Pres   : Texture upload/copy EMA and Present wait EMA (ms).
 * - Recovery    : Decoder deadline recovery count and active loop-filter state (Full/Fast/Rec).
 * - Errors      : Decoder errors (ovf:N = receive queue overflows).
 * - Workers/Slc : Active FFmpeg worker threads (T) and H.264 slices (S).
 * - Pipeline    : Active GPU upload path (Cached/Legacy) and YUV shader (Fast/Qual).
 * - GPU Slots   : Cached texture slots ready for render (rdy) and GPU in-flight (flt).
 * - GPU Busy    : GPU slot starvation events where producer waited on free slot.
 * - Feed->Acq   : System GPU feed-to-acquire latency (ms).
 * - OutWait     : System GPU output wait latency (ms).
 * - InFlight    : System GPU frames currently in flight.
 * - Buffers     : System GPU buffer occupancy (used / capacity).
 * - Timeouts    : System GPU wait timeout count.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "ui/canvas.h"
#include "ui/font.h"
#include "ui/design_system.h"

enum overlay_mode {
    OVERLAY_OFF = 0,
    OVERLAY_SIMPLE = 1,
    OVERLAY_DETAILED = 2,
    OVERLAY_DEVELOPER = 3,
    OVERLAY_MODE_COUNT = 4
};

enum overlay_alert_kind {
    OVERLAY_ALERT_NONE = 0,
    OVERLAY_ALERT_VIDEO_STALL,     /* No incoming video frames */
    OVERLAY_ALERT_SYSGPU_WAIT,     /* System GPU output wait / timeout sustained */
    OVERLAY_ALERT_DECODE_OVERLOAD, /* Active decode consistently above frame budget */
    OVERLAY_ALERT_PACKET_LOSS,     /* RTP loss, FEC failure, or queue overflow */
    OVERLAY_ALERT_QUEUE_BACKLOG,   /* Sustained video queue depth / age buildup */
    OVERLAY_ALERT_RECOVERY,        /* Decoder deadline recovery active */
    OVERLAY_ALERT_STALE            /* Network telemetry sample is stale */
};

struct overlay_sysgpu_telemetry {
    bool valid;
    uint32_t feed_acquire_us;
    uint32_t output_wait_us;
    uint32_t in_flight;
    uint32_t buffer_used;
    uint32_t buffer_capacity;
    uint32_t timeouts;
    char recovery_state[24];
};

struct overlay_telemetry {
    int mode;       /* enum overlay_mode */
    int dev_page;   /* 0 or 1 when Developer mode paginates */

    /* Rates & timing (1-second window) */
    bool fps_valid;
    double fps_received;
    double fps_decoded;
    double fps_presented;
    double payload_mbps;
    int target_fps;
    double budget_ms;

    /* Sample freshness flags (prevent stale green/false overload) */
    bool video_stale;
    bool network_stale;
    bool decode_stale;

    /* Decode latency (EMA + windowed percentiles) */
    uint32_t avg_decode_us;
    bool percentiles_valid;
    uint32_t decode_window_samples;
    uint32_t decode_p50_us;
    uint32_t decode_p95_us;
    uint32_t decode_p99_us;

    /* Network */
    bool network_valid;
    uint32_t rtt_ms;
    uint32_t jitter_ms;

    /* Queue & frame age */
    int video_queue;
    int audio_queue_ms;
    bool queue_age_valid;
    uint32_t queue_age_us;
    bool frame_age_valid;
    uint32_t frame_age_us;

    /* Loss, drops & recovery */
    uint32_t dropped;
    uint32_t render_superseded;
    uint32_t decode_errors;
    uint32_t frame_gaps;
    bool pressure_valid;
    uint32_t rtp_lost;
    uint32_t fec_recovered;
    uint32_t fec_failed;
    uint32_t queue_overflows;
    uint32_t recent_loss_delta;
    uint32_t recent_overflow_delta;

    /* Upload / Present timing (EMA) */
    bool timing_valid;
    uint32_t upload_us;
    uint32_t gpu_submit_us;
    uint32_t present_wait_us;

    /* Decoder & pipeline state */
    uint32_t recoveries;
    bool in_recovery;
    bool fast_decode;
    int decode_threads;
    int slices;
    bool pipeline_cached;
    bool shader_fast;
    uint32_t slots_ready;
    uint32_t slots_in_flight;
    uint32_t gpu_starvation;

    /* Hysteresis-filtered alert state (populated by overlay_alert_update) */
    int alert_kind; /* enum overlay_alert_kind */
    bool alert_filtered;

    /* Footer identity (actual decoded stream + requested comparison) */
    int requested_w;
    int requested_h;
    int actual_w;
    int actual_h;
    float display_hz;
    char backend_name[24]; /* "FFmpeg" or "System GPU" */

    /* Optional System GPU specific telemetry */
    struct overlay_sysgpu_telemetry sysgpu;
};

/* Sustained-window alert tracker with hysteresis (updated at 1 Hz). */
struct overlay_alert_tracker {
    uint8_t stall_ticks;
    uint8_t decode_over_ticks;
    uint8_t decode_clear_ticks;
    uint8_t queue_over_ticks;
    uint8_t queue_clear_ticks;
    uint8_t sysgpu_wait_ticks;
    uint8_t sysgpu_clear_ticks;
    uint8_t loss_hold_ticks;
    uint8_t overflow_hold_ticks;
    uint32_t last_sysgpu_timeouts;
    bool decode_active;
    bool queue_active;
    bool sysgpu_active;
};

static inline const char *overlay_mode_name(int mode)
{
    switch (mode) {
    case OVERLAY_SIMPLE:    return "Simple";
    case OVERLAY_DETAILED:  return "Detailed";
    case OVERLAY_DEVELOPER: return "Developer";
    default:                return "Off";
    }
}

static inline int overlay_alert_update(struct overlay_alert_tracker *tr, const struct overlay_telemetry *t)
{
    if (!tr || !t) return OVERLAY_ALERT_NONE;

    /* 1. Video input interruption (never misattributed to decoder overload). */
    bool no_video = t->video_stale || (t->fps_valid && t->fps_received < 0.5 && t->fps_decoded < 0.5);
    if (no_video) {
        if (tr->stall_ticks < 10) tr->stall_ticks++;
    } else {
        tr->stall_ticks = 0;
    }

    /* 2. System GPU output wait / timeout pressure (sustained 2s, hysteresis clear). */
    bool sysgpu_high = false, sysgpu_low = true;
    if (t->sysgpu.valid && !no_video && t->budget_ms > 0.0) {
        double wait_ms = t->sysgpu.output_wait_us / 1000.0;
        bool timeout_inc = t->sysgpu.timeouts > tr->last_sysgpu_timeouts;
        tr->last_sysgpu_timeouts = t->sysgpu.timeouts;
        sysgpu_high = (wait_ms > t->budget_ms) || timeout_inc;
        sysgpu_low = (wait_ms < t->budget_ms * 0.85) && !timeout_inc;
    }
    if (sysgpu_high) {
        tr->sysgpu_clear_ticks = 0;
        if (tr->sysgpu_wait_ticks < 10) tr->sysgpu_wait_ticks++;
        if (tr->sysgpu_wait_ticks >= 2) tr->sysgpu_active = true;
    } else if (tr->sysgpu_active && sysgpu_low) {
        tr->sysgpu_wait_ticks = 0;
        if (++tr->sysgpu_clear_ticks >= 2) tr->sysgpu_active = false;
    } else {
        tr->sysgpu_wait_ticks = 0;
    }

    /* 3. Genuine decode overload: requires fresh active decoding > budget for 2s. */
    bool dec_active_stream = !no_video && !t->decode_stale && t->avg_decode_us > 0 && t->budget_ms > 0.0 &&
                             (!t->fps_valid || t->fps_decoded >= 1.0);
    double dec_ms = t->avg_decode_us / 1000.0;
    if (dec_active_stream && dec_ms > t->budget_ms) {
        tr->decode_clear_ticks = 0;
        if (tr->decode_over_ticks < 10) tr->decode_over_ticks++;
        if (tr->decode_over_ticks >= 2) tr->decode_active = true;
    } else if (no_video || t->decode_stale) {
        /* Input stalled or decode sample stale: clear decode overload immediately. */
        tr->decode_over_ticks = 0;
        tr->decode_clear_ticks = 0;
        tr->decode_active = false;
    } else if (tr->decode_active && dec_ms < t->budget_ms * 0.92) {
        tr->decode_over_ticks = 0;
        if (++tr->decode_clear_ticks >= 2) tr->decode_active = false;
    } else {
        tr->decode_over_ticks = 0;
    }

    /* 4. Packet loss hold (2s hold prevents single-tick flicker). */
    if (t->recent_loss_delta > 0) {
        tr->loss_hold_ticks = 2;
    } else if (tr->loss_hold_ticks > 0) {
        tr->loss_hold_ticks--;
    }
    if (t->recent_overflow_delta > 0) {
        tr->overflow_hold_ticks = 2;
    } else if (tr->overflow_hold_ticks > 0) {
        tr->overflow_hold_ticks--;
    }

    /* 5. Queue backlog (sustained 2s, hysteresis clear at <= 1 frame & < 80% budget). */
    bool q_high = !no_video && (t->video_queue >= 3 ||
                  (t->queue_age_valid && t->budget_ms > 0.0 && t->queue_age_us > (uint32_t)(t->budget_ms * 1000.0)));
    bool q_low = no_video || (t->video_queue <= 1 &&
                 (!t->queue_age_valid || t->budget_ms <= 0.0 || t->queue_age_us < (uint32_t)(t->budget_ms * 800.0)));
    if (q_high) {
        tr->queue_clear_ticks = 0;
        if (tr->queue_over_ticks < 10) tr->queue_over_ticks++;
        if (tr->queue_over_ticks >= 2) tr->queue_active = true;
    } else if (tr->queue_active && q_low) {
        tr->queue_over_ticks = 0;
        if (++tr->queue_clear_ticks >= 2) tr->queue_active = false;
    } else {
        tr->queue_over_ticks = 0;
    }

    /* Priority order: input stall -> System GPU wait -> decode overload -> packet loss -> queue -> recovery -> stale */
    if (tr->stall_ticks >= 2) return OVERLAY_ALERT_VIDEO_STALL;
    if (tr->sysgpu_active) return OVERLAY_ALERT_SYSGPU_WAIT;
    if (tr->decode_active) return OVERLAY_ALERT_DECODE_OVERLOAD;
    if (tr->loss_hold_ticks > 0) return OVERLAY_ALERT_PACKET_LOSS;
    if (tr->queue_active || tr->overflow_hold_ticks > 0) return OVERLAY_ALERT_QUEUE_BACKLOG;
    if (t->in_recovery && !no_video) return OVERLAY_ALERT_RECOVERY;
    if (t->network_stale && t->network_valid) return OVERLAY_ALERT_STALE;
    return OVERLAY_ALERT_NONE;
}

static inline int overlay_effective_alert_kind(const struct overlay_telemetry *t)
{
    if (!t) return OVERLAY_ALERT_NONE;
    if (t->alert_filtered) return t->alert_kind;
    /* Stateless fallback for callers that do not maintain a tracker. */
    if (t->video_stale || (t->fps_valid && t->fps_received < 0.5 && t->fps_decoded < 0.5))
        return OVERLAY_ALERT_VIDEO_STALL;
    if (t->sysgpu.valid && t->budget_ms > 0.0 && (t->sysgpu.output_wait_us / 1000.0) > t->budget_ms)
        return OVERLAY_ALERT_SYSGPU_WAIT;
    if (!t->decode_stale && t->avg_decode_us > 0 && t->budget_ms > 0.0 &&
        (!t->fps_valid || t->fps_decoded >= 1.0) && (t->avg_decode_us / 1000.0) > t->budget_ms)
        return OVERLAY_ALERT_DECODE_OVERLOAD;
    if (t->recent_loss_delta > 0)
        return OVERLAY_ALERT_PACKET_LOSS;
    if (t->recent_overflow_delta > 0 || t->video_queue >= 3 || (t->queue_age_valid && t->budget_ms > 0.0 && t->queue_age_us > (uint32_t)(t->budget_ms * 1000.0)))
        return OVERLAY_ALERT_QUEUE_BACKLOG;
    if (t->in_recovery) return OVERLAY_ALERT_RECOVERY;
    if (t->network_stale && t->network_valid) return OVERLAY_ALERT_STALE;
    return OVERLAY_ALERT_NONE;
}

static inline const char *overlay_active_alert(const struct overlay_telemetry *t, uint32_t *color)
{
    int kind = overlay_effective_alert_kind(t);
    switch (kind) {
    case OVERLAY_ALERT_VIDEO_STALL:
        if (color) *color = DS_STATUS_WARN;
        return "No video input";
    case OVERLAY_ALERT_SYSGPU_WAIT:
        if (color) *color = DS_STATUS_WARN;
        return "GPU output wait";
    case OVERLAY_ALERT_DECODE_OVERLOAD:
        if (color) *color = DS_STATUS_ERROR;
        return "Decode overload";
    case OVERLAY_ALERT_PACKET_LOSS:
        if (color) *color = DS_STATUS_ERROR;
        return "Packet loss";
    case OVERLAY_ALERT_QUEUE_BACKLOG:
        if (color) *color = DS_STATUS_WARN;
        return "Queue backlog";
    case OVERLAY_ALERT_RECOVERY:
        if (color) *color = DS_STATUS_WARN;
        return "Recovery active";
    case OVERLAY_ALERT_STALE:
        if (color) *color = DS_TEXT_MUTED;
        return "Stale metrics";
    default:
        return NULL;
    }
}

/* Semantic color helpers: stale samples NEVER show healthy green. */
static inline uint32_t overlay_fps_color(const struct overlay_telemetry *t)
{
    if (!t->fps_valid || t->target_fps <= 0) return DS_TEXT_MUTED;
    if (t->video_stale || (t->fps_received < 0.5 && t->fps_decoded < 0.5)) return DS_STATUS_WARN;
    double ratio = t->fps_presented / (double)t->target_fps;
    if (ratio < 0.75) return DS_STATUS_ERROR;
    if (ratio < 0.92) return DS_STATUS_WARN;
    if (ratio >= 0.97) return DS_STATUS_OK;
    return DS_TEXT_PRIMARY;
}

static inline uint32_t overlay_decode_color(const struct overlay_telemetry *t)
{
    if (!t->avg_decode_us || t->budget_ms <= 0.0 || t->decode_stale || t->video_stale)
        return DS_TEXT_MUTED;
    double ms = t->avg_decode_us / 1000.0;
    if (ms > t->budget_ms) return DS_STATUS_ERROR;
    if (ms > t->budget_ms * 0.85) return DS_STATUS_WARN;
    if (ms <= t->budget_ms * 0.75) return DS_STATUS_OK;
    return DS_TEXT_PRIMARY;
}

static inline uint32_t overlay_rtt_color(const struct overlay_telemetry *t)
{
    if (!t->network_valid || t->network_stale) return DS_TEXT_MUTED;
    if (t->rtt_ms >= 60) return DS_STATUS_ERROR;
    if (t->rtt_ms >= 25) return DS_STATUS_WARN;
    if (t->rtt_ms <= 10) return DS_STATUS_OK;
    return DS_TEXT_PRIMARY;
}

static inline uint32_t overlay_jitter_color(const struct overlay_telemetry *t)
{
    if (!t->network_valid || t->network_stale) return DS_TEXT_MUTED;
    if (t->jitter_ms >= 20) return DS_STATUS_ERROR;
    if (t->jitter_ms >= 8) return DS_STATUS_WARN;
    return DS_TEXT_PRIMARY;
}

static inline uint32_t overlay_bitrate_color(const struct overlay_telemetry *t)
{
    if (!t->fps_valid || t->video_stale) return DS_TEXT_MUTED;
    return DS_TEXT_PRIMARY;
}

static inline uint32_t overlay_queue_color(const struct overlay_telemetry *t)
{
    if (t->video_stale) return DS_TEXT_MUTED;
    if (t->video_queue >= 4 || (t->queue_age_valid && t->budget_ms > 0.0 && t->queue_age_us > (uint32_t)(t->budget_ms * 1500.0)))
        return DS_STATUS_ERROR;
    if (t->video_queue >= 2 || (t->queue_age_valid && t->budget_ms > 0.0 && t->queue_age_us > (uint32_t)(t->budget_ms * 1000.0)))
        return DS_STATUS_WARN;
    return DS_TEXT_PRIMARY;
}

static inline uint32_t overlay_loss_color(const struct overlay_telemetry *t)
{
    if (t->decode_errors > 0 || t->recent_loss_delta > 0)
        return DS_STATUS_ERROR;
    if (t->dropped > 0 || t->rtp_lost > 0 || t->frame_gaps > 0 || t->fec_failed > 0 || t->queue_overflows > 0)
        return DS_STATUS_WARN;
    return DS_TEXT_PRIMARY;
}

/* Straight-alpha graphite HUD conversion with 1px antialiased rounded corners.
 * Only evaluates SDF in the four r x r corner regions for minimal CPU cost. */
static inline void overlay_finalize_hud_alpha(canvas_t *c, uint32_t bg_rgb, float radius, unsigned panel_alpha)
{
    const int w = c->w, h = c->h;
    const unsigned bg_r = (bg_rgb >> 16) & 255u;
    const unsigned bg_g = (bg_rgb >> 8) & 255u;
    const unsigned bg_b = bg_rgb & 255u;
    const float cx = w * 0.5f, cy = h * 0.5f, hw = w * 0.5f, hh = h * 0.5f;
    const int ir = (int)ceilf(radius) + 1;

    for (int y = 0; y < h; y++) {
        uint32_t *row = &c->px[y * w];
        bool y_corner = (y < ir) || (y >= h - ir);
        for (int x = 0; x < w; x++) {
            uint32_t px = row[x];
            unsigned r = (px >> 16) & 255u;
            unsigned g = (px >> 8) & 255u;
            unsigned b = px & 255u;
            unsigned dr = r > bg_r ? r - bg_r : bg_r - r;
            unsigned dg = g > bg_g ? g - bg_g : bg_g - g;
            unsigned db = b > bg_b ? b - bg_b : bg_b - b;
            unsigned diff = dr > dg ? dr : dg;
            if (db > diff) diff = db;
            unsigned boost = diff * 3u;
            if (boost > 255u) boost = 255u;
            unsigned alpha = panel_alpha + ((255u - panel_alpha) * boost + 127u) / 255u;

            if (y_corner && (x < ir || x >= w - ir)) {
                float d = sd_round_box(x + 0.5f, y + 0.5f, cx, cy, hw, hh, radius);
                if (d >= 0.5f) {
                    row[x] = 0u;
                    continue;
                }
                if (d > -0.5f) {
                    float cov = 0.5f - d;
                    alpha = (unsigned)(alpha * cov + 0.5f);
                }
            }
            row[x] = (alpha << 24) | (px & 0x00FFFFFFu);
        }
    }
}

static inline int overlay_align32(int v) { return ((v + 31) / 32) * 32; }

static inline bool overlay_dev_needs_pagination(const struct overlay_telemetry *t, int layout_w)
{
    return (layout_w <= 960) || t->sysgpu.valid;
}

static inline void overlay_format_simple(const struct overlay_telemetry *t,
                                         char *fps_v, size_t fps_sz,
                                         char *dec_v, size_t dec_sz,
                                         char *rtt_v, size_t rtt_sz)
{
    if (t->fps_valid && !t->video_stale) snprintf(fps_v, fps_sz, "%.1f", t->fps_presented);
    else snprintf(fps_v, fps_sz, "--");
    if (t->avg_decode_us > 0 && !t->decode_stale && !t->video_stale)
        snprintf(dec_v, dec_sz, "%.1f ms", t->avg_decode_us / 1000.0);
    else snprintf(dec_v, dec_sz, "--");
    if (t->network_valid) snprintf(rtt_v, rtt_sz, "%u ms", t->rtt_ms);
    else rtt_v[0] = '\0';
}

/* Computes exact Developer row count for the active page so panel height fits
 * content tightly with zero empty region below the footer. */
static inline int overlay_dev_page_rows(const struct overlay_telemetry *t, int layout_w, int *backend_split)
{
    bool crt = layout_w <= 960;
    bool paged = overlay_dev_needs_pagination(t, layout_w);
    int page = paged ? (t->dev_page & 1) : 0;
    if (backend_split) *backend_split = 0;

    if (!paged) {
        /* 16:9 FFmpeg: 6 core rows + divider + 2 FFmpeg rows (or 3 if audio queue shown) */
        if (backend_split) *backend_split = 1;
        return 6 + (t->audio_queue_ms > 0 ? 3 : 2);
    }
    if (page == 0) {
        /* Page 1: 6 core rows on 16:9, 4 spacious rows on CRT 4:3 */
        return crt ? 4 : 6;
    }
    /* Page 2: System GPU = 3 rows; FFmpeg CRT = 4 rows */
    if (t->sysgpu.valid) return 3;
    return crt ? 4 : 3;
}

static inline void overlay_measure_panel(const struct overlay_telemetry *t, int layout_w, int *out_w, int *out_h)
{
    if (!t || t->mode <= OVERLAY_OFF || t->mode >= OVERLAY_MODE_COUNT) {
        if (out_w) *out_w = 0;
        if (out_h) *out_h = 0;
        return;
    }
    bool crt = layout_w <= 960;
    int max_w = layout_w - 32;
    if (max_w < 320) max_w = 320;

    if (t->mode == OVERLAY_SIMPLE) {
        /* CRT 4:3 uses larger logical font (20px -> ~13.3px physical at 480p) */
        float f_lbl = crt ? 16.0f : 13.0f;
        float f_val = crt ? 20.0f : 15.0f;
        int pad = crt ? 11 : 9;
        int div_gap = crt ? 16 : 14;
        char fps_v[24], dec_v[24], rtt_v[24];
        overlay_format_simple(t, fps_v, sizeof(fps_v), dec_v, sizeof(dec_v), rtt_v, sizeof(rtt_v));
        int w = pad * 2;
        w += font_text_width(FONT_REGULAR, f_lbl, "FPS") + 5 + font_text_width_tabular(FONT_BOLD, f_val, fps_v);
        w += div_gap;
        w += font_text_width(FONT_REGULAR, f_lbl, "Decode") + 5 + font_text_width_tabular(FONT_BOLD, f_val, dec_v);
        if (rtt_v[0]) {
            w += div_gap;
            w += font_text_width(FONT_REGULAR, f_lbl, "RTT") + 5 + font_text_width_tabular(FONT_BOLD, f_val, rtt_v);
        }
        w = overlay_align32(w);
        int cap = crt ? 448 : 352;
        if (cap > max_w) cap = max_w;
        if (w < 192) w = 192;
        if (w > cap) w = cap;
        int h = crt ? 42 : 32;
        if (out_w) *out_w = w;
        if (out_h) *out_h = h;
        return;
    }

    if (t->mode == OVERLAY_DETAILED) {
        uint32_t dummy = 0;
        bool has_alert = overlay_active_alert(t, &dummy) != NULL;
        if (crt) {
            /* CRT 4:3 dedicated 2-column layout with readable 480p typography */
            int w = 352;
            if (w > max_w) w = overlay_align32(max_w - 31);
            int h = 198 + (has_alert ? 28 : 0);
            if (out_w) *out_w = w;
            if (out_h) *out_h = h;
            return;
        }
        int w = 320;
        if (w > max_w) w = overlay_align32(max_w - 31);
        int h = 144 + (has_alert ? 20 : 0);
        if (out_w) *out_w = w;
        if (out_h) *out_h = h;
        return;
    }

    /* OVERLAY_DEVELOPER: exact content-fitted height and tight horizontal footprint */
    {
        int w = crt ? 480 : 416;
        if (w > max_w) w = overlay_align32(max_w - 31);
        int pad = crt ? 12 : 10;
        float f_val = crt ? 17.5f : 13.0f;
        int step = crt ? 26 : 19;
        int split = 0;
        int rows = overlay_dev_page_rows(t, layout_w, &split);
        uint32_t dummy = 0;
        bool has_alert = overlay_active_alert(t, &dummy) != NULL;

        /* Top padding + ascent + (rows-1)*step + split divider + bottom/alert/footer */
        int y = pad + (int)font_ascent(FONT_BOLD, f_val) + rows * step;
        if (split) y += 11;
        int cursor_y = y - step + 8;
        if (has_alert) cursor_y += crt ? 26 : 19;
        int footer_h = crt ? 28 : 22;
        int h = cursor_y + footer_h;
        if (h > 688) h = 688;
        if (out_w) *out_w = w;
        if (out_h) *out_h = h;
    }
}

static inline void overlay_draw_kv(canvas_t *c, int x, int w, int baseline_y,
                                   float lbl_px, float val_px,
                                   const char *label, const char *value, uint32_t val_color)
{
    font_draw(c, FONT_REGULAR, lbl_px, x, baseline_y, DS_TEXT_MUTED, label);
    int vw = font_text_width_tabular(FONT_BOLD, val_px, value);
    int vx = x + w - vw;
    int lw = font_text_width(FONT_REGULAR, lbl_px, label);
    if (vx < x + lw + 6) vx = x + lw + 6;
    font_draw_tabular(c, FONT_BOLD, val_px, vx, baseline_y, val_color, value);
}

static inline void overlay_render_panel(canvas_t *c, const struct overlay_telemetry *t, int layout_w)
{
    if (!c || !c->px || !t || t->mode <= OVERLAY_OFF) return;

    const uint32_t BG = 0xFF12171C;
    const uint32_t BORDER = DS_BORDER_SUBTLE;
    const uint32_t DIV = 0xFF263038;
    const int w = c->w, h = c->h;
    const bool crt = layout_w <= 960;

    cv_fill(c, 0, 0, w, h, BG);
    cv_rrect_outline(c, 0.5f, 0.5f, (float)w - 1.0f, (float)h - 1.0f, (float)DS_RADIUS_MD, 1.0f, BORDER);

    if (t->mode == OVERLAY_SIMPLE) {
        float f_lbl = crt ? 16.0f : 13.0f;
        float f_val = crt ? 20.0f : 15.0f;
        int pad = crt ? 11 : 9;
        int div_gap = crt ? 16 : 14;
        char fps_v[24], dec_v[24], rtt_v[24];
        overlay_format_simple(t, fps_v, sizeof(fps_v), dec_v, sizeof(dec_v), rtt_v, sizeof(rtt_v));
        int base_y = (h + (int)font_ascent(FONT_BOLD, f_val)) / 2 - 1;
        int pen = pad;

        pen = font_draw(c, FONT_REGULAR, f_lbl, pen, base_y, DS_TEXT_MUTED, "FPS") + 5;
        pen = font_draw_tabular(c, FONT_BOLD, f_val, pen, base_y, overlay_fps_color(t), fps_v);

        cv_fill(c, pen + div_gap / 2, 8, 1, h - 16, DIV);
        pen += div_gap;

        pen = font_draw(c, FONT_REGULAR, f_lbl, pen, base_y, DS_TEXT_MUTED, "Decode") + 5;
        pen = font_draw_tabular(c, FONT_BOLD, f_val, pen, base_y, overlay_decode_color(t), dec_v);

        if (rtt_v[0]) {
            cv_fill(c, pen + div_gap / 2, 8, 1, h - 16, DIV);
            pen += div_gap;
            pen = font_draw(c, FONT_REGULAR, f_lbl, pen, base_y, DS_TEXT_MUTED, "RTT") + 5;
            font_draw_tabular(c, FONT_BOLD, f_val, pen, base_y, overlay_rtt_color(t), rtt_v);
        }

        overlay_finalize_hud_alpha(c, BG, (float)DS_RADIUS_MD, 214u);
        return;
    }

    if (t->mode == OVERLAY_DETAILED) {
        if (crt) {
            /* Dedicated 2-column CRT 4:3 layout: large crisp fonts, zero squishing */
            const int pad = 12;
            const int inner_w = w - pad * 2;
            const int col_w = inner_w / 2;
            const float f_lbl = 15.0f;
            const float f_pri = 21.0f;
            const float f_sec = 19.0f;
            const float f_ftr = 15.0f;
            char v0[32], v1[32];

            /* Row 1: Primary (FPS | Decode) */
            int l1 = pad + (int)font_ascent(FONT_REGULAR, f_lbl);
            int v1y = l1 + 22;
            if (t->fps_valid && !t->video_stale) snprintf(v0, sizeof(v0), "%.1f", t->fps_presented);
            else snprintf(v0, sizeof(v0), "--");
            if (t->avg_decode_us > 0 && !t->decode_stale && !t->video_stale)
                snprintf(v1, sizeof(v1), "%.1f ms", t->avg_decode_us / 1000.0);
            else snprintf(v1, sizeof(v1), "--");

            font_draw(c, FONT_REGULAR, f_lbl, pad, l1, DS_TEXT_MUTED, "FPS");
            font_draw_tabular(c, FONT_BOLD, f_pri, pad, v1y, overlay_fps_color(t), v0);
            font_draw(c, FONT_REGULAR, f_lbl, pad + col_w, l1, DS_TEXT_MUTED, "Decode");
            font_draw_tabular(c, FONT_BOLD, f_pri, pad + col_w, v1y, overlay_decode_color(t), v1);

            int div1 = v1y + 10;
            cv_fill(c, pad, div1, inner_w, 1, DIV);

            /* Row 2: Network & Bandwidth (RTT | Bitrate) */
            int l2 = div1 + 19;
            int v2y = l2 + 21;
            if (t->network_valid) snprintf(v0, sizeof(v0), "%u ms", t->rtt_ms);
            else snprintf(v0, sizeof(v0), "--");
            if (t->fps_valid && !t->video_stale) snprintf(v1, sizeof(v1), "%.1f Mbps", t->payload_mbps);
            else snprintf(v1, sizeof(v1), "--");

            font_draw(c, FONT_REGULAR, f_lbl, pad, l2, DS_TEXT_MUTED, "RTT");
            font_draw_tabular(c, FONT_BOLD, f_sec, pad, v2y, overlay_rtt_color(t), v0);
            font_draw(c, FONT_REGULAR, f_lbl, pad + col_w, l2, DS_TEXT_MUTED, "Bitrate");
            font_draw_tabular(c, FONT_BOLD, f_sec, pad + col_w, v2y, overlay_bitrate_color(t), v1);

            /* Row 3: Diagnostics (Lost/Dropped | Queue) */
            int l3 = v2y + 22;
            int v3y = l3 + 21;
            uint32_t lost_total = t->pressure_valid ? (t->rtp_lost + t->frame_gaps) : t->dropped;
            const char *lost_lbl = t->pressure_valid ? "Lost" : "Dropped";
            snprintf(v0, sizeof(v0), "%u", lost_total);
            if (t->queue_age_valid) snprintf(v1, sizeof(v1), "%d (%.0f ms)", t->video_queue, t->queue_age_us / 1000.0);
            else snprintf(v1, sizeof(v1), "%d fr", t->video_queue);

            font_draw(c, FONT_REGULAR, f_lbl, pad, l3, DS_TEXT_MUTED, lost_lbl);
            font_draw_tabular(c, FONT_BOLD, f_sec, pad, v3y, overlay_loss_color(t), v0);
            font_draw(c, FONT_REGULAR, f_lbl, pad + col_w, l3, DS_TEXT_MUTED, "Queue");
            font_draw_tabular(c, FONT_BOLD, f_sec, pad + col_w, v3y, overlay_queue_color(t), v1);

            int cursor_y = v3y + 10;
            uint32_t alert_col = DS_STATUS_WARN;
            const char *alert = overlay_active_alert(t, &alert_col);
            if (alert) {
                cv_fill(c, pad, cursor_y, inner_w, 1, DIV);
                int ay = cursor_y + 20;
                cv_circle(c, (float)(pad + 5), (float)(ay - 5), 3.5f, alert_col);
                font_draw(c, FONT_BOLD, 16.0f, pad + 14, ay, alert_col, alert);
                cursor_y = ay + 8;
            }

            cv_fill(c, pad, cursor_y, inner_w, 1, DIV);
            int fy = cursor_y + 20;
            char footer[96];
            const char *backend = t->backend_name[0] ? t->backend_name : "FFmpeg";
            if (t->actual_w > 0 && t->actual_h > 0 && t->requested_w > 0 && t->requested_h > 0 &&
                (t->requested_w != t->actual_w || t->requested_h != t->actual_h))
                snprintf(footer, sizeof(footer), "Req %dx%d->%dx%d | %s",
                         t->requested_w, t->requested_h, t->actual_w, t->actual_h, backend);
            else if (t->actual_w > 0 && t->actual_h > 0)
                snprintf(footer, sizeof(footer), "%dx%d  |  %s", t->actual_w, t->actual_h, backend);
            else
                snprintf(footer, sizeof(footer), "%s", backend);
            font_draw_tabular(c, FONT_REGULAR, f_ftr, pad, fy, DS_TEXT_MUTED, footer);

            overlay_finalize_hud_alpha(c, BG, (float)DS_RADIUS_MD, 214u);
            return;
        }

        /* 16:9 720p compact 3-column Detailed layout */
        const int pad = 10;
        const int inner_w = w - pad * 2;
        const int col_w = inner_w / 3;
        float f_lbl = 11.0f, f_pri = 16.0f, f_sec = 14.0f, f_ftr = 11.0f;
        char v0[32], v1[32], v2[32];

        /* Row 1: Primary (Presented FPS | Decode | RTT) */
        int l1 = pad + (int)font_ascent(FONT_REGULAR, f_lbl);
        int v1y = l1 + 16;
        if (t->fps_valid && !t->video_stale) snprintf(v0, sizeof(v0), "%.1f", t->fps_presented);
        else snprintf(v0, sizeof(v0), "--");
        if (t->avg_decode_us > 0 && !t->decode_stale && !t->video_stale)
            snprintf(v1, sizeof(v1), "%.1f ms", t->avg_decode_us / 1000.0);
        else snprintf(v1, sizeof(v1), "--");

        font_draw(c, FONT_REGULAR, f_lbl, pad, l1, DS_TEXT_MUTED, "FPS");
        font_draw_tabular(c, FONT_BOLD, f_pri, pad, v1y, overlay_fps_color(t), v0);
        font_draw(c, FONT_REGULAR, f_lbl, pad + col_w, l1, DS_TEXT_MUTED, "Decode");
        font_draw_tabular(c, FONT_BOLD, f_pri, pad + col_w, v1y, overlay_decode_color(t), v1);
        if (t->network_valid) {
            snprintf(v2, sizeof(v2), "%u ms", t->rtt_ms);
            font_draw(c, FONT_REGULAR, f_lbl, pad + col_w * 2, l1, DS_TEXT_MUTED, "RTT");
            font_draw_tabular(c, FONT_BOLD, f_pri, pad + col_w * 2, v1y, overlay_rtt_color(t), v2);
        }

        int div1 = v1y + 8;
        cv_fill(c, pad, div1, inner_w, 1, DIV);

        /* Row 2: Secondary (Bitrate | Jitter | Frame age) */
        int l2 = div1 + 14;
        int v2y = l2 + 15;
        if (t->fps_valid && !t->video_stale) snprintf(v0, sizeof(v0), "%.1f Mbps", t->payload_mbps);
        else snprintf(v0, sizeof(v0), "--");
        font_draw(c, FONT_REGULAR, f_lbl, pad, l2, DS_TEXT_MUTED, "Bitrate");
        font_draw_tabular(c, FONT_BOLD, f_sec, pad, v2y, overlay_bitrate_color(t), v0);

        if (t->network_valid) {
            snprintf(v1, sizeof(v1), "%u ms", t->jitter_ms);
            font_draw(c, FONT_REGULAR, f_lbl, pad + col_w, l2, DS_TEXT_MUTED, "Jitter");
            font_draw_tabular(c, FONT_BOLD, f_sec, pad + col_w, v2y, overlay_jitter_color(t), v1);
        }
        if (t->frame_age_valid && !t->video_stale) {
            snprintf(v2, sizeof(v2), "%.1f ms", t->frame_age_us / 1000.0);
            font_draw(c, FONT_REGULAR, f_lbl, pad + col_w * 2, l2, DS_TEXT_MUTED, "Frame age");
            font_draw_tabular(c, FONT_BOLD, f_sec, pad + col_w * 2, v2y, DS_TEXT_PRIMARY, v2);
        }

        /* Row 3: Diagnostics (Lost/Dropped | Queue | Decoded FPS) */
        int l3 = v2y + 16;
        int v3y = l3 + 15;
        uint32_t lost_total = t->pressure_valid ? (t->rtp_lost + t->frame_gaps) : t->dropped;
        const char *lost_lbl = t->pressure_valid ? "Lost" : "Dropped";
        snprintf(v0, sizeof(v0), "%u", lost_total);
        font_draw(c, FONT_REGULAR, f_lbl, pad, l3, DS_TEXT_MUTED, lost_lbl);
        font_draw_tabular(c, FONT_BOLD, f_sec, pad, v3y, overlay_loss_color(t), v0);

        if (t->queue_age_valid) snprintf(v1, sizeof(v1), "%d (%.0f ms)", t->video_queue, t->queue_age_us / 1000.0);
        else snprintf(v1, sizeof(v1), "%d fr", t->video_queue);
        font_draw(c, FONT_REGULAR, f_lbl, pad + col_w, l3, DS_TEXT_MUTED, "Queue");
        font_draw_tabular(c, FONT_BOLD, f_sec, pad + col_w, v3y, overlay_queue_color(t), v1);

        if (t->fps_valid && !t->video_stale) snprintf(v2, sizeof(v2), "%.1f", t->fps_decoded);
        else snprintf(v2, sizeof(v2), "--");
        font_draw(c, FONT_REGULAR, f_lbl, pad + col_w * 2, l3, DS_TEXT_MUTED, "Dec FPS");
        font_draw_tabular(c, FONT_BOLD, f_sec, pad + col_w * 2, v3y,
                          t->video_stale ? DS_TEXT_MUTED : DS_TEXT_PRIMARY, v2);

        int cursor_y = v3y + 8;
        uint32_t alert_col = DS_STATUS_WARN;
        const char *alert = overlay_active_alert(t, &alert_col);
        if (alert) {
            cv_fill(c, pad, cursor_y, inner_w, 1, DIV);
            int ay = cursor_y + 15;
            cv_circle(c, (float)(pad + 4), (float)(ay - 4), 3.0f, alert_col);
            font_draw(c, FONT_BOLD, f_ftr + 1.0f, pad + 12, ay, alert_col, alert);
            cursor_y = ay + 6;
        }

        cv_fill(c, pad, cursor_y, inner_w, 1, DIV);
        int fy = cursor_y + 14;
        char footer[96];
        const char *backend = t->backend_name[0] ? t->backend_name : "FFmpeg";
        if (t->actual_w > 0 && t->actual_h > 0 && t->requested_w > 0 && t->requested_h > 0 &&
            (t->requested_w != t->actual_w || t->requested_h != t->actual_h))
            snprintf(footer, sizeof(footer), "Req %dx%d->%dx%d | %s",
                     t->requested_w, t->requested_h, t->actual_w, t->actual_h, backend);
        else if (t->actual_w > 0 && t->actual_h > 0)
            snprintf(footer, sizeof(footer), "%dx%d  |  %s", t->actual_w, t->actual_h, backend);
        else
            snprintf(footer, sizeof(footer), "%s", backend);
        font_draw_tabular(c, FONT_REGULAR, f_ftr, pad, fy, DS_TEXT_MUTED, footer);

        overlay_finalize_hud_alpha(c, BG, (float)DS_RADIUS_MD, 214u);
        return;
    }

    /* OVERLAY_DEVELOPER */
    {
        const int pad = crt ? 12 : 10;
        const int inner_w = w - pad * 2;
        const int col_gap = crt ? 16 : 14;
        const int col_w = (inner_w - col_gap) / 2;
        const int xL = pad;
        const int xR = pad + col_w + col_gap;
        const float f_lbl = crt ? 15.0f : 11.5f;
        const float f_val = crt ? 17.5f : 13.0f;
        const float f_ftr = crt ? 14.5f : 11.0f;
        const int step = crt ? 26 : 19;
        const bool paged = overlay_dev_needs_pagination(t, layout_w);
        const int page = paged ? (t->dev_page & 1) : 0;
        char buf[64], p_lbl[32];

        int y = pad + (int)font_ascent(FONT_BOLD, f_val);
        int section_top = pad + 2;

        if (!paged || page == 0) {
            /* Row 1: FPS R/D/P (1s) | RTT / Jit */
            if (t->fps_valid && !t->video_stale)
                snprintf(buf, sizeof(buf), "%.1f/%.1f/%.1f", t->fps_received, t->fps_decoded, t->fps_presented);
            else
                snprintf(buf, sizeof(buf), "--");
            overlay_draw_kv(c, xL, col_w, y, f_lbl, f_val, "FPS R/D/P", buf, overlay_fps_color(t));

            if (t->network_valid)
                snprintf(buf, sizeof(buf), "%u / %u ms", t->rtt_ms, t->jitter_ms);
            else
                snprintf(buf, sizeof(buf), "--");
            overlay_draw_kv(c, xR, col_w, y, f_lbl, f_val, "RTT / Jit", buf, overlay_rtt_color(t));
            y += step;

            /* Row 2: Dec avg (EMA) | Bitrate (1s) */
            if (t->avg_decode_us > 0 && !t->decode_stale && !t->video_stale)
                snprintf(buf, sizeof(buf), "%.1f ms", t->avg_decode_us / 1000.0);
            else
                snprintf(buf, sizeof(buf), "--");
            overlay_draw_kv(c, xL, col_w, y, f_lbl, f_val, "Dec avg", buf, overlay_decode_color(t));

            if (t->fps_valid && !t->video_stale) snprintf(buf, sizeof(buf), "%.1f Mbps", t->payload_mbps);
            else snprintf(buf, sizeof(buf), "--");
            overlay_draw_kv(c, xR, col_w, y, f_lbl, f_val, "Bitrate", buf, overlay_bitrate_color(t));
            y += step;

            /* Row 3: p50/95/99 (window N) | RTP / FEC */
            if (t->percentiles_valid && !t->decode_stale) {
                snprintf(p_lbl, sizeof(p_lbl), "p50/95/99(%u)", t->decode_window_samples ? t->decode_window_samples : 256u);
                snprintf(buf, sizeof(buf), "%.1f/%.1f/%.1f", t->decode_p50_us / 1000.0, t->decode_p95_us / 1000.0, t->decode_p99_us / 1000.0);
            } else {
                snprintf(p_lbl, sizeof(p_lbl), "p50/95/99");
                snprintf(buf, sizeof(buf), "--");
            }
            overlay_draw_kv(c, xL, col_w, y, f_lbl, f_val, p_lbl, buf,
                            t->decode_stale ? DS_TEXT_MUTED : DS_TEXT_PRIMARY);

            if (t->pressure_valid)
                snprintf(buf, sizeof(buf), "L:%u R:%u F:%u", t->rtp_lost, t->fec_recovered, t->fec_failed);
            else
                snprintf(buf, sizeof(buf), "G:%u", t->frame_gaps);
            overlay_draw_kv(c, xR, col_w, y, f_lbl, f_val, "RTP / FEC", buf, overlay_loss_color(t));
            y += step;

            /* Row 4: Queue (depth / age) | Frame age */
            if (t->queue_age_valid)
                snprintf(buf, sizeof(buf), "%d fr / %.1f ms", t->video_queue, t->queue_age_us / 1000.0);
            else
                snprintf(buf, sizeof(buf), "%d fr", t->video_queue);
            overlay_draw_kv(c, xL, col_w, y, f_lbl, f_val, "Queue", buf, overlay_queue_color(t));

            if (t->frame_age_valid && !t->video_stale)
                snprintf(buf, sizeof(buf), "%.1f ms", t->frame_age_us / 1000.0);
            else
                snprintf(buf, sizeof(buf), "--");
            overlay_draw_kv(c, xR, col_w, y, f_lbl, f_val, "Frame age", buf,
                            t->video_stale ? DS_TEXT_MUTED : DS_TEXT_PRIMARY);
            y += step;

            if (!crt) {
                /* Rows 5-6 on 16:9 (on CRT 4:3 these move cleanly to Page 2 for larger text) */
                if (t->render_superseded > 0)
                    snprintf(buf, sizeof(buf), "%u (s:%u)", t->dropped, t->render_superseded);
                else
                    snprintf(buf, sizeof(buf), "%u", t->dropped);
                overlay_draw_kv(c, xL, col_w, y, f_lbl, f_val, "Dropped", buf,
                                t->dropped > 0 ? DS_STATUS_WARN : DS_TEXT_PRIMARY);

                if (t->timing_valid && !t->video_stale)
                    snprintf(buf, sizeof(buf), "%.1f / %.1f ms", t->upload_us / 1000.0, t->present_wait_us / 1000.0);
                else
                    snprintf(buf, sizeof(buf), "--");
                overlay_draw_kv(c, xR, col_w, y, f_lbl, f_val, "Up / Pres", buf, DS_TEXT_PRIMARY);
                y += step;

                const char *fstate = t->fast_decode ? "Fast" : t->in_recovery ? "Rec" : "Full";
                snprintf(buf, sizeof(buf), "%u (%s)", t->recoveries, fstate);
                overlay_draw_kv(c, xL, col_w, y, f_lbl, f_val, "Recovery", buf,
                                t->in_recovery ? DS_STATUS_WARN : DS_TEXT_PRIMARY);

                if (t->queue_overflows > 0)
                    snprintf(buf, sizeof(buf), "%u (ovf:%u)", t->decode_errors, t->queue_overflows);
                else
                    snprintf(buf, sizeof(buf), "%u", t->decode_errors);
                overlay_draw_kv(c, xR, col_w, y, f_lbl, f_val, "Errors", buf,
                                (t->decode_errors > 0 || t->queue_overflows > 0) ? DS_STATUS_ERROR : DS_TEXT_PRIMARY);
                y += step;
            }

            /* Center vertical divider for top section */
            cv_fill(c, xL + col_w + col_gap / 2, section_top, 1, y - step - section_top + 4, DIV);
        }

        if (!paged) {
            int div_y = y - step + 8;
            cv_fill(c, pad, div_y, inner_w, 1, DIV);
            y = div_y + (int)font_ascent(FONT_BOLD, f_val) + 6;
            section_top = div_y + 4;
        }

        if (!paged || page == 1) {
            int p2_top = paged ? section_top : (y - (int)font_ascent(FONT_BOLD, f_val) - 2);
            if (t->sysgpu.valid) {
                snprintf(buf, sizeof(buf), "%.1f ms", t->sysgpu.feed_acquire_us / 1000.0);
                overlay_draw_kv(c, xL, col_w, y, f_lbl, f_val, "Feed->Acq", buf, DS_TEXT_PRIMARY);
                snprintf(buf, sizeof(buf), "%.1f ms", t->sysgpu.output_wait_us / 1000.0);
                overlay_draw_kv(c, xR, col_w, y, f_lbl, f_val, "OutWait", buf,
                                (t->budget_ms > 0.0 && t->sysgpu.output_wait_us / 1000.0 > t->budget_ms) ? DS_STATUS_WARN : DS_TEXT_PRIMARY);
                y += step;

                snprintf(buf, sizeof(buf), "%u", t->sysgpu.in_flight);
                overlay_draw_kv(c, xL, col_w, y, f_lbl, f_val, "InFlight", buf, DS_TEXT_PRIMARY);
                snprintf(buf, sizeof(buf), "%u / %u", t->sysgpu.buffer_used, t->sysgpu.buffer_capacity);
                overlay_draw_kv(c, xR, col_w, y, f_lbl, f_val, "Buffers", buf, DS_TEXT_PRIMARY);
                y += step;

                snprintf(buf, sizeof(buf), "%u", t->sysgpu.timeouts);
                overlay_draw_kv(c, xL, col_w, y, f_lbl, f_val, "Timeouts", buf,
                                t->sysgpu.timeouts > 0 ? DS_STATUS_WARN : DS_TEXT_PRIMARY);
                overlay_draw_kv(c, xR, col_w, y, f_lbl, f_val, "Recovery",
                                t->sysgpu.recovery_state[0] ? t->sysgpu.recovery_state : "Idle",
                                DS_TEXT_PRIMARY);
                y += step;
            } else {
                if (crt) {
                    /* CRT Page 2 starts with Dropped/Up-Pres + Recovery/Errors + FFmpeg */
                    snprintf(buf, sizeof(buf), "%u", t->dropped);
                    overlay_draw_kv(c, xL, col_w, y, f_lbl, f_val, "Dropped", buf,
                                    t->dropped > 0 ? DS_STATUS_WARN : DS_TEXT_PRIMARY);
                    if (t->timing_valid && !t->video_stale)
                        snprintf(buf, sizeof(buf), "%.1f / %.1f ms", t->upload_us / 1000.0, t->present_wait_us / 1000.0);
                    else snprintf(buf, sizeof(buf), "--");
                    overlay_draw_kv(c, xR, col_w, y, f_lbl, f_val, "Up / Pres", buf, DS_TEXT_PRIMARY);
                    y += step;

                    const char *fstate = t->fast_decode ? "Fast" : t->in_recovery ? "Rec" : "Full";
                    snprintf(buf, sizeof(buf), "%u (%s)", t->recoveries, fstate);
                    overlay_draw_kv(c, xL, col_w, y, f_lbl, f_val, "Recovery", buf,
                                    t->in_recovery ? DS_STATUS_WARN : DS_TEXT_PRIMARY);
                    snprintf(buf, sizeof(buf), "%u", t->decode_errors);
                    overlay_draw_kv(c, xR, col_w, y, f_lbl, f_val, "Errors", buf,
                                    t->decode_errors > 0 ? DS_STATUS_ERROR : DS_TEXT_PRIMARY);
                    y += step;
                }

                snprintf(buf, sizeof(buf), "%dT / %dS", t->decode_threads, t->slices);
                overlay_draw_kv(c, xL, col_w, y, f_lbl, f_val, "Workers/Slc", buf, DS_TEXT_PRIMARY);
                snprintf(buf, sizeof(buf), "%s / %s",
                         t->pipeline_cached ? "Cached" : "Legacy",
                         t->shader_fast ? "Fast" : "Qual");
                overlay_draw_kv(c, xR, col_w, y, f_lbl, f_val, "Pipeline", buf, DS_TEXT_PRIMARY);
                y += step;

                snprintf(buf, sizeof(buf), "%u rdy / %u flt", t->slots_ready, t->slots_in_flight);
                overlay_draw_kv(c, xL, col_w, y, f_lbl, f_val, "GPU Slots", buf, DS_TEXT_PRIMARY);
                snprintf(buf, sizeof(buf), "%u", t->gpu_starvation);
                overlay_draw_kv(c, xR, col_w, y, f_lbl, f_val, "GPU Busy", buf,
                                t->gpu_starvation > 0 ? DS_STATUS_WARN : DS_TEXT_PRIMARY);
                y += step;

                if (!crt && t->audio_queue_ms > 0) {
                    snprintf(buf, sizeof(buf), "%d ms", t->audio_queue_ms);
                    overlay_draw_kv(c, xL, col_w, y, f_lbl, f_val, "Audio Q", buf, DS_TEXT_PRIMARY);
                    if (t->timing_valid && !t->video_stale)
                        snprintf(buf, sizeof(buf), "%.1f ms", t->gpu_submit_us / 1000.0);
                    else snprintf(buf, sizeof(buf), "--");
                    overlay_draw_kv(c, xR, col_w, y, f_lbl, f_val, "GPU Sub", buf, DS_TEXT_PRIMARY);
                    y += step;
                }
            }
            if (paged) {
                cv_fill(c, xL + col_w + col_gap / 2, p2_top, 1, y - step - p2_top + 4, DIV);
            }
        }

        int cursor_y = y - step + 8;

        /* Conditional alert line (filtered with hysteresis) */
        uint32_t alert_col = DS_STATUS_WARN;
        const char *alert = overlay_active_alert(t, &alert_col);
        if (alert) {
            cv_fill(c, pad, cursor_y, inner_w, 1, DIV);
            int ay = cursor_y + (crt ? 19 : 14);
            cv_circle(c, (float)(pad + 4), (float)(ay - (crt ? 5 : 4)), crt ? 3.5f : 3.0f, alert_col);
            font_draw(c, FONT_BOLD, f_ftr + 1.0f, pad + (crt ? 13 : 12), ay, alert_col, alert);
            cursor_y = ay + (crt ? 7 : 5);
        }

        /* Discrete footer */
        cv_fill(c, pad, cursor_y, inner_w, 1, DIV);
        int fy = cursor_y + (crt ? 19 : 14);
        char footer[128];
        const char *backend = t->backend_name[0] ? t->backend_name : "FFmpeg";
        if (t->actual_w > 0 && t->actual_h > 0 && t->requested_w > 0 && t->requested_h > 0 &&
            (t->requested_w != t->actual_w || t->requested_h != t->actual_h) && t->display_hz >= 20.0f)
            snprintf(footer, sizeof(footer), "Req %dx%d->%dx%d | %s | %.0f Hz",
                     t->requested_w, t->requested_h, t->actual_w, t->actual_h, backend, t->display_hz);
        else if (t->actual_w > 0 && t->actual_h > 0 && t->requested_w > 0 && t->requested_h > 0 &&
                 (t->requested_w != t->actual_w || t->requested_h != t->actual_h))
            snprintf(footer, sizeof(footer), "Req %dx%d->%dx%d | %s",
                     t->requested_w, t->requested_h, t->actual_w, t->actual_h, backend);
        else if (t->actual_w > 0 && t->actual_h > 0 && t->display_hz >= 20.0f)
            snprintf(footer, sizeof(footer), "%dx%d  |  %s  |  %.0f Hz", t->actual_w, t->actual_h, backend, t->display_hz);
        else if (t->actual_w > 0 && t->actual_h > 0)
            snprintf(footer, sizeof(footer), "%dx%d  |  %s", t->actual_w, t->actual_h, backend);
        else
            snprintf(footer, sizeof(footer), "%s", backend);
        font_draw_tabular(c, FONT_REGULAR, f_ftr, pad, fy, DS_TEXT_MUTED, footer);

        if (paged) {
            char pg[16];
            snprintf(pg, sizeof(pg), "Pg %d/2", page + 1);
            int pgw = font_text_width_tabular(FONT_REGULAR, f_ftr, pg);
            font_draw_tabular(c, FONT_REGULAR, f_ftr, w - pad - pgw, fy, DS_TEXT_MUTED, pg);
        }

        overlay_finalize_hud_alpha(c, BG, (float)DS_RADIUS_MD, 214u);
    }
}
