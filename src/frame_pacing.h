#ifndef MOONX_FRAME_PACING_H
#define MOONX_FRAME_PACING_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

enum frame_pacing_mode {
    FRAME_PACING_BASELINE = 0,          /* A: Current MoonX360 pipeline unchanged (default) */
    FRAME_PACING_LATEST_WINS = 1,       /* B: Latest complete valid frame wins immediately */
    FRAME_PACING_BOUNDED_QUEUE = 2,     /* C: Bounded queue depth (<=1) + max age cap */
    FRAME_PACING_ADAPTIVE_PRESENT = 3,  /* D: Age & refresh-period adaptive present/drop */
    FRAME_PACING_TIMESTAMP_PACED = 4,   /* E: Monotonic arrival/RTP delta pacing */
    FRAME_PACING_REFRESH_ALIGNED = 5,   /* F: 30/50/59.94/60 Hz refresh cadence alignment */
    FRAME_PACING_COUNT
};

enum frame_pacing_flag {
    FP_FLAG_IDR = 1u << 0,
    FP_FLAG_DROPPED = 1u << 1,
    FP_FLAG_SUPERSEDED = 1u << 2,
    FP_FLAG_LATE = 1u << 3,
    FP_FLAG_REPEATED = 1u << 4,
    FP_FLAG_STALL_RECOVERY = 1u << 5
};

#define FRAME_PACING_RING_CAP 256

/* All *_us fields except host_proc_us are monotonic local client microseconds.
 * Never fabricate host capture timestamps when the host does not provide them. */
struct frame_pacing_record {
    uint32_t frame_id;
    uint32_t rtp_timestamp;
    uint32_t host_proc_us;       /* host-reported processing latency (0 = unavailable) */
    uint64_t arrival_us;         /* first RTP packet local receive time */
    uint64_t enqueue_us;         /* depacketizer enqueue time */
    uint64_t decode_start_us;
    uint64_t decode_end_us;
    uint64_t upload_start_us;
    uint64_t upload_end_us;
    uint64_t present_start_us;
    uint64_t present_end_us;
    uint32_t frame_age_us;       /* present_start_us - arrival_us (or enqueue_us) */
    uint32_t present_interval_us;
    uint8_t queue_depth;
    uint8_t flags;
};

struct frame_pacing_summary {
    uint64_t duration_us;
    uint32_t total_records;
    uint32_t presented_count;
    uint32_t dropped_count;
    uint32_t superseded_count;
    uint32_t repeated_count;
    uint32_t late_count;
    uint32_t stall_recoveries;
    uint32_t max_stall_recovery_us;
    uint32_t decode_p50_us, decode_p95_us, decode_p99_us;
    uint32_t frame_age_p50_us, frame_age_p95_us, frame_age_p99_us;
    uint32_t admit_to_vblank_p50_us, admit_to_vblank_p95_us, admit_to_vblank_p99_us;
    uint32_t present_interval_p50_us, present_interval_p95_us, present_interval_p99_us;
    double fps_presented;
};

struct frame_pacing_state {
    int mode;
    int target_fps;
    int display_hz_x100;
    uint32_t refresh_us;
    uint32_t target_frame_us;
    uint64_t last_present_start_us;
    uint64_t last_frame_origin_us;
    uint64_t last_pts_us;
    uint64_t pace_anchor_us;
    uint64_t stall_started_us;
    uint32_t max_age_us;
    uint32_t dropped_count;
    uint32_t superseded_count;
    uint32_t repeated_count;
    uint32_t late_count;
    uint32_t stall_recoveries;
    uint32_t max_stall_recovery_us;
    uint32_t count;
    uint32_t next;
    struct frame_pacing_record ring[FRAME_PACING_RING_CAP];
};

static inline const char *frame_pacing_mode_name(int mode) {
    static const char *names[FRAME_PACING_COUNT] = {
        "Baseline (Default)",
        "Latest Frame Wins",
        "Bounded Queue",
        "Adaptive Presentation",
        "Timestamp Paced",
        "Refresh Aligned"
    };
    return names[(mode >= 0 && mode < FRAME_PACING_COUNT) ? mode : 0];
}

static inline const char *frame_pacing_mode_key(int mode) {
    static const char *keys[FRAME_PACING_COUNT] = {
        "baseline",
        "latest_wins",
        "bounded_queue",
        "adaptive_present",
        "timestamp_paced",
        "refresh_aligned"
    };
    return keys[(mode >= 0 && mode < FRAME_PACING_COUNT) ? mode : 0];
}

static inline int frame_pacing_cycle_mode(int mode, int direction) {
    int cur = (mode >= 0 && mode < FRAME_PACING_COUNT) ? mode : 0;
    return (cur + (direction < 0 ? FRAME_PACING_COUNT - 1 : 1)) % FRAME_PACING_COUNT;
}

static inline void frame_pacing_init(struct frame_pacing_state *s, int mode, int target_fps, int display_hz_x100) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->mode = (mode >= 0 && mode < FRAME_PACING_COUNT) ? mode : FRAME_PACING_BASELINE;
    s->target_fps = (target_fps >= 20 && target_fps <= 120) ? target_fps : 60;
    s->display_hz_x100 = (display_hz_x100 >= 2000 && display_hz_x100 <= 24000) ? display_hz_x100 : 6000;
    s->refresh_us = (uint32_t)(100000000ULL / (uint64_t)s->display_hz_x100);
    s->target_frame_us = (uint32_t)(1000000u / (uint32_t)s->target_fps);
    /* Bounded queue caps age at 2x target frame period (clamped 20..50 ms). */
    s->max_age_us = s->target_frame_us * 2u;
    if (s->max_age_us < 20000u) s->max_age_us = 20000u;
    if (s->max_age_us > 50000u) s->max_age_us = 50000u;
}

/* Experiment F: use D3DPRESENT_INTERVAL_TWO for 30 FPS on ~60 Hz display to lock 2:2 cadence. */
static inline int frame_pacing_present_interval(int mode, bool vsync, int target_fps, int display_hz_x100) {
    if (!vsync) return 0; /* D3DPRESENT_INTERVAL_IMMEDIATE */
    if (mode == FRAME_PACING_REFRESH_ALIGNED && target_fps == 30 &&
        display_hz_x100 >= 5800 && display_hz_x100 <= 6200) {
        return 2; /* D3DPRESENT_INTERVAL_TWO */
    }
    return 1; /* D3DPRESENT_INTERVAL_ONE */
}

/* Decide whether an older ready slot should be pruned before presentation. */
static inline bool frame_pacing_should_prune_ready(const struct frame_pacing_state *s,
                                                   uint64_t now_us, uint64_t slot_ready_us,
                                                   unsigned ready_count) {
    if (!s || s->mode == FRAME_PACING_BASELINE) return false;
    if (ready_count > 1) return true;
    if ((s->mode == FRAME_PACING_BOUNDED_QUEUE || s->mode == FRAME_PACING_ADAPTIVE_PRESENT) &&
        slot_ready_us > 0 && now_us > slot_ready_us) {
        uint64_t age = now_us - slot_ready_us;
        if (age > s->max_age_us) return true;
    }
    return false;
}

/* Compute bounded pre-present pacing wait (us). Always returns 0 when backlog exists. */
static inline uint32_t frame_pacing_compute_wait_us(struct frame_pacing_state *s,
                                                    uint64_t now_us,
                                                    uint64_t frame_origin_us,
                                                    uint32_t rtp_timestamp,
                                                    unsigned ready_count) {
    if (!s) return 0;
    if (s->mode == FRAME_PACING_BASELINE || s->mode == FRAME_PACING_LATEST_WINS ||
        s->mode == FRAME_PACING_BOUNDED_QUEUE) {
        return 0;
    }
    /* Never delay presentation if another decoded frame is already waiting. */
    if (ready_count > 1) return 0;

    if (s->mode == FRAME_PACING_ADAPTIVE_PRESENT) {
        if (s->last_present_start_us && now_us >= s->last_present_start_us) {
            uint64_t since_present = now_us - s->last_present_start_us;
            uint32_t min_gap = (s->target_frame_us > s->refresh_us) ? s->target_frame_us : s->refresh_us;
            uint32_t early_guard = min_gap * 80u / 100u;
            if (since_present < early_guard) {
                uint32_t wait = (uint32_t)(early_guard - since_present);
                uint32_t cap = s->refresh_us / 2u;
                return wait > cap ? cap : wait;
            }
        }
        return 0;
    }

    if (s->mode == FRAME_PACING_TIMESTAMP_PACED || s->mode == FRAME_PACING_REFRESH_ALIGNED) {
        uint64_t pts_us = ((uint64_t)rtp_timestamp * 1000ULL) / 90ULL;
        uint32_t target_step = s->target_frame_us;
        if (s->mode == FRAME_PACING_REFRESH_ALIGNED && s->target_fps == 60 &&
            s->display_hz_x100 >= 5980 && s->display_hz_x100 <= 6020) {
            target_step = s->refresh_us;
        }
        if (s->last_present_start_us && now_us >= s->last_present_start_us) {
            uint64_t delta_pts = (pts_us > s->last_pts_us && (pts_us - s->last_pts_us) <= 200000ULL)
                ? (pts_us - s->last_pts_us) : target_step;
            if (s->mode == FRAME_PACING_REFRESH_ALIGNED && delta_pts > target_step * 90u / 100u &&
                delta_pts < target_step * 110u / 100u) {
                delta_pts = target_step;
            }
            uint64_t desired = s->last_present_start_us + delta_pts;
            if (frame_origin_us && now_us >= frame_origin_us && (now_us - frame_origin_us) > s->max_age_us) {
                return 0;
            }
            if (desired > now_us) {
                uint64_t wait = desired - now_us;
                uint32_t cap = s->refresh_us / 2u;
                return wait > cap ? cap : (uint32_t)wait;
            }
        }
    }
    return 0;
}

static inline void frame_pacing_record_sample(struct frame_pacing_state *s,
                                              const struct frame_pacing_record *rec) {
    struct frame_pacing_record r;
    if (!s || !rec) return;
    r = *rec;
    if (!(r.flags & FP_FLAG_DROPPED) && r.present_start_us > 0) {
        if (s->last_present_start_us && r.present_start_us >= s->last_present_start_us) {
            uint64_t iv = r.present_start_us - s->last_present_start_us;
            r.present_interval_us = iv > 0xFFFFFFFFULL ? 0xFFFFFFFFu : (uint32_t)iv;
            if (r.present_interval_us > (s->target_frame_us * 3u) / 2u) {
                r.flags |= FP_FLAG_LATE;
                s->late_count++;
                if (!s->stall_started_us) s->stall_started_us = s->last_present_start_us;
            } else if (s->stall_started_us) {
                uint64_t rec_us = r.present_start_us - s->stall_started_us;
                s->stall_recoveries++;
                if (rec_us > s->max_stall_recovery_us) {
                    s->max_stall_recovery_us = rec_us > 0xFFFFFFFFULL ? 0xFFFFFFFFu : (uint32_t)rec_us;
                }
                s->stall_started_us = 0;
                r.flags |= FP_FLAG_STALL_RECOVERY;
            }
        }
        s->last_present_start_us = r.present_start_us;
        if (r.rtp_timestamp) s->last_pts_us = ((uint64_t)r.rtp_timestamp * 1000ULL) / 90ULL;
    }
    if (r.flags & FP_FLAG_DROPPED) s->dropped_count++;
    if (r.flags & FP_FLAG_SUPERSEDED) s->superseded_count++;
    if (r.flags & FP_FLAG_REPEATED) s->repeated_count++;

    s->ring[s->next] = r;
    s->next = (s->next + 1u) % FRAME_PACING_RING_CAP;
    if (s->count < FRAME_PACING_RING_CAP) s->count++;
}

static int frame_pacing_cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x > y ? 1 : (x < y ? -1 : 0);
}

static inline uint32_t frame_pacing_pct(uint32_t *vals, uint32_t n, unsigned pct) {
    if (!n) return 0;
    uint32_t rank = (n * pct + 99u) / 100u;
    if (rank < 1u) rank = 1u;
    if (rank > n) rank = n;
    return vals[rank - 1u];
}

static inline void frame_pacing_summarize(const struct frame_pacing_state *s,
                                          struct frame_pacing_summary *out) {
    uint32_t dec[FRAME_PACING_RING_CAP];
    uint32_t age[FRAME_PACING_RING_CAP];
    uint32_t a2v[FRAME_PACING_RING_CAP];
    uint32_t piv[FRAME_PACING_RING_CAP];
    uint32_t nd = 0, na = 0, nv = 0, np = 0;
    uint64_t first_p = 0, last_p = 0;
    uint32_t presented = 0;
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!s || !s->count) return;

    uint32_t start = (s->next + FRAME_PACING_RING_CAP - s->count) % FRAME_PACING_RING_CAP;
    for (uint32_t i = 0; i < s->count; i++) {
        const struct frame_pacing_record *r = &s->ring[(start + i) % FRAME_PACING_RING_CAP];
        if (r->decode_end_us > r->decode_start_us) {
            dec[nd++] = (uint32_t)(r->decode_end_us - r->decode_start_us);
        }
        if (!(r->flags & FP_FLAG_DROPPED) && r->present_start_us > 0) {
            presented++;
            if (!first_p) first_p = r->present_start_us;
            last_p = r->present_start_us;
            if (r->frame_age_us > 0) age[na++] = r->frame_age_us;
            if (r->present_end_us > r->decode_start_us && r->decode_start_us > 0 &&
                (r->present_end_us - r->decode_start_us) <= 10000000ULL) {
                a2v[nv++] = (uint32_t)(r->present_end_us - r->decode_start_us);
            }
            if (r->present_interval_us > 0) piv[np++] = r->present_interval_us;
        }
    }

    if (nd) qsort(dec, nd, sizeof(uint32_t), frame_pacing_cmp_u32);
    if (na) qsort(age, na, sizeof(uint32_t), frame_pacing_cmp_u32);
    if (nv) qsort(a2v, nv, sizeof(uint32_t), frame_pacing_cmp_u32);
    if (np) qsort(piv, np, sizeof(uint32_t), frame_pacing_cmp_u32);

    out->total_records = s->count;
    out->presented_count = presented;
    out->dropped_count = s->dropped_count;
    out->superseded_count = s->superseded_count;
    out->repeated_count = s->repeated_count;
    out->late_count = s->late_count;
    out->stall_recoveries = s->stall_recoveries;
    out->max_stall_recovery_us = s->max_stall_recovery_us;
    out->duration_us = (last_p > first_p) ? (last_p - first_p) : 0;
    out->fps_presented = (out->duration_us > 0 && presented > 1)
        ? ((double)(presented - 1u) * 1000000.0 / (double)out->duration_us) : 0.0;

    out->decode_p50_us = frame_pacing_pct(dec, nd, 50);
    out->decode_p95_us = frame_pacing_pct(dec, nd, 95);
    out->decode_p99_us = frame_pacing_pct(dec, nd, 99);
    out->frame_age_p50_us = frame_pacing_pct(age, na, 50);
    out->frame_age_p95_us = frame_pacing_pct(age, na, 95);
    out->frame_age_p99_us = frame_pacing_pct(age, na, 99);
    out->admit_to_vblank_p50_us = frame_pacing_pct(a2v, nv, 50);
    out->admit_to_vblank_p95_us = frame_pacing_pct(a2v, nv, 95);
    out->admit_to_vblank_p99_us = frame_pacing_pct(a2v, nv, 99);
    out->present_interval_p50_us = frame_pacing_pct(piv, np, 50);
    out->present_interval_p95_us = frame_pacing_pct(piv, np, 95);
    out->present_interval_p99_us = frame_pacing_pct(piv, np, 99);
}

#ifdef __cplusplus
}
#endif

#endif /* MOONX_FRAME_PACING_H */
