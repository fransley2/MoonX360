/* Decoder-thread-only deadline protection. No queue API, allocation or I/O. */
#pragma once
#include <stdint.h>
#include <stdbool.h>

/* Percent of one frame budget. Require repeated evidence, never one spike.
 * Recovery lasts at least thirty calls and requires thirty healthy samples to leave. */
#define DEADLINE_QUEUE_ENTER 75
#define DEADLINE_EMA_ENTER   95
#define DEADLINE_QUEUE_EXIT  25
#define DEADLINE_EMA_EXIT    85
#define DEADLINE_ENTER_FRAMES 2
#define DEADLINE_MIN_FAST     30
#define DEADLINE_EXIT_FRAMES  30
#define DEADLINE_BACKLOG_BUDGETS 6  /* far below common-c's 15-unit bound */

struct decode_deadline {
    bool recovery, queue_valid, previous_valid;
    unsigned overload, healthy, fast_duration;
    unsigned recovery_frames;
    unsigned backlog;
    bool refresh_requested;
    uint64_t sample_now, last_refresh;
    uint32_t budget_us, queue_us, previous_queue_us;
    uint32_t recoveries, full_frames, fast_frames;
    uint32_t decode_samples;
    uint32_t decode_us, max_decode_us, max_queue_us; /* statistics-interval maxima */
    uint32_t session_max_decode_us, session_max_queue_us;
};

static bool decode_deadline_before(struct decode_deadline *s, bool forced_fast,
                                   int fps, uint64_t enqueue, uint64_t now,
                                   uint32_t decode_ema)
{
    bool queue_pressure, slow;
    s->sample_now=now;
    s->budget_us = 1000000u / (fps >= 1 && fps <= 240 ? fps : 30);
    /* Same LiGetMicroseconds clock; zero, future and implausibly stale stamps
     * are unknown, not a zero-age sample that would authorize leaving FAST. */
    s->queue_valid = enqueue != 0 && now >= enqueue && now - enqueue <= 10000000;
    s->queue_us = s->queue_valid ? (uint32_t)(now - enqueue) : 0;
    if (s->queue_valid && s->queue_us > s->max_queue_us) s->max_queue_us = s->queue_us;
    if (s->queue_valid && s->queue_us > s->session_max_queue_us) s->session_max_queue_us = s->queue_us;
    queue_pressure = s->queue_valid && s->queue_us > s->budget_us * DEADLINE_QUEUE_ENTER / 100 &&
                     (!s->previous_valid || s->queue_us >= s->previous_queue_us);
    slow = decode_ema > s->budget_us * DEADLINE_EMA_ENTER / 100;
    if (forced_fast) {
        s->recovery = false;
        s->overload = s->healthy = s->fast_duration = 0;
    } else if (!s->recovery) {
        s->overload = queue_pressure || slow ? s->overload + 1 : 0;
        if (s->overload >= DEADLINE_ENTER_FRAMES) {
            s->recovery = true;
            s->recoveries++;
            s->fast_duration = s->healthy = s->overload = 0;
            s->recovery_frames = 0;
            s->refresh_requested = false;
            s->backlog = 0;
        }
    } else {
        bool healthy = s->queue_valid && s->queue_us < s->budget_us * DEADLINE_QUEUE_EXIT / 100 &&
                       decode_ema < s->budget_us * DEADLINE_EMA_EXIT / 100;
        bool backlog = s->queue_valid && s->previous_valid &&
                       s->queue_us > s->budget_us * DEADLINE_BACKLOG_BUDGETS &&
                       s->queue_us >= s->previous_queue_us;
        s->backlog = backlog ? s->backlog + (s->backlog < DEADLINE_ENTER_FRAMES) : 0;
        s->healthy = healthy ? s->healthy + 1 : 0;
        if (s->fast_duration >= DEADLINE_MIN_FAST && s->healthy >= DEADLINE_EXIT_FRAMES) {
            s->recovery = false;
            s->healthy = s->overload = 0;
        }
    }
    s->previous_valid = s->queue_valid;
    s->previous_queue_us = s->queue_us;
    return forced_fast || s->recovery;
}

static void decode_deadline_after(struct decode_deadline *s, bool skipped, bool decoded, uint32_t us)
{
    s->decode_samples++;
    s->decode_us = us;
    if (us > s->max_decode_us) s->max_decode_us = us;
    if (us > s->session_max_decode_us) s->session_max_decode_us = us;
    if (decoded) {
        if (skipped) s->fast_frames++; else s->full_frames++;
    }
    /* Saturate: a prolonged overload must not wrap the minimum-duration gate. */
    if (s->recovery && s->fast_duration < DEADLINE_MIN_FAST) s->fast_duration++;
    if (s->recovery && s->recovery_frames < 1000000) s->recovery_frames++;
}

static bool decode_deadline_needs_refresh(struct decode_deadline *s)
{
    /* NONREF recovery keeps reference pictures intact. Only a measured, growing
     * backlog warrants a reset. Cooldown survives leaving/re-entering recovery. */
    if (s->recovery && !s->refresh_requested && s->budget_us &&
        s->fast_duration>=DEADLINE_MIN_FAST && s->backlog>=DEADLINE_ENTER_FRAMES &&
        (!s->last_refresh || (s->sample_now>=s->last_refresh && s->sample_now-s->last_refresh>=10000000))) {
        s->refresh_requested=true;s->last_refresh=s->sample_now;return true;
    }
    return false;
}
