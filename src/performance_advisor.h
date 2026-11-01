#pragma once
#include <stdint.h>
#include <stdbool.h>

enum performance_class { PERF_UNKNOWN, PERF_EXCELLENT, PERF_GOOD, PERF_UNSTABLE };
enum performance_cause { PERF_CAUSE_UNKNOWN, PERF_CPU, PERF_PACING, PERF_GPU };
struct performance_sample {
    uint32_t budget_us, decode_avg_us, decode_p95_us, decode_p99_us;
    uint32_t callback_p99_us, callback_us, gap_p99_us, queue_us, queue_peak_us, fence_p99_us;
    uint32_t rtt_ms, drops, starvation, failovers, frames, frame_gaps;
    int mcc_queue;
    double shown_fps, target_fps, payload_mbps;
    bool tail_valid, rtt_valid;
    int requested_fps, setup_fps_argument, negotiated_fps;
    bool negotiated_fps_known; /* setup_fps_argument is a copy of the request in MCC */
    double callback_fps, decoded_fps, presented_fps; /* backend stages, not scanout */
};
struct performance_result { int grade, cause; };
static const char *performance_grade(int grade) {
    switch(grade) { case PERF_EXCELLENT:return "Excellent";case PERF_GOOD:return "Good";
    case PERF_UNSTABLE:return "Unstable";default:return "Not measured"; }
}
static const char *performance_cause_name(int cause) {
    switch(cause) {case PERF_CPU:return "CPU video decoding";case PERF_PACING:return "Network / host pacing";
    case PERF_GPU:return "GPU pipeline pressure";default:return "Unknown / insufficient evidence";}
}
/* Pure, conservative assessment. Low FPS alone never diagnoses network loss.
 * Fallback alone is not a GPU fault. Present/VSync is deliberately excluded. */
static struct performance_result performance_assess(const struct performance_sample *s) {
    struct performance_result r={PERF_UNKNOWN,PERF_CAUSE_UNKNOWN};
    if(!s->budget_us || s->frames<30) return r;
    r.grade=PERF_GOOD;
    bool queue=s->queue_us>s->budget_us*3/4 || s->queue_peak_us>s->budget_us*3 || s->mcc_queue>=2;
    bool cpu=s->decode_avg_us>s->budget_us*95/100 ||
        (s->tail_valid && s->decode_p99_us>s->budget_us*95/100);
    if(cpu && queue) {r.grade=PERF_UNSTABLE;r.cause=PERF_CPU;return r;}
    if(s->decode_avg_us<s->budget_us && (s->starvation>=5 || s->failovers>=2) &&
       (s->drops>5 || s->fence_p99_us>s->budget_us/4)) {
        r.grade=PERF_UNSTABLE;r.cause=PERF_GPU;return r;
    }
    if(queue && (s->callback_p99_us>s->budget_us || s->callback_us>s->budget_us)) {
        r.grade=PERF_UNSTABLE;return r; /* callback pressure is not necessarily the codec */
    }
    bool low=s->target_fps>0 && s->shown_fps<s->target_fps*0.80;
    if(low && !queue && s->decode_avg_us<s->budget_us*3/4 &&
       ((s->frame_gaps>2 && s->gap_p99_us>s->budget_us*2) ||
        (s->rtt_valid && s->rtt_ms>100))) {
        r.grade=PERF_UNSTABLE;r.cause=PERF_PACING;return r;
    }
    if(s->queue_peak_us>s->budget_us*4 || (s->frames>100 && s->drops>s->frames/10)) {
        r.grade=PERF_UNSTABLE;return r; /* problem observed, cause not established */
    }
    if(s->tail_valid && s->decode_p99_us<s->budget_us*9/10 &&
       (!s->callback_p99_us || s->callback_p99_us<s->budget_us) &&
       s->queue_peak_us<s->budget_us*2 && s->mcc_queue<=1 && !s->starvation &&
       !s->frame_gaps && s->drops<=s->frames/100 && s->target_fps>0 &&
       s->shown_fps>=s->target_fps*0.93 && s->rtt_valid && s->rtt_ms<25) r.grade=PERF_EXCELLENT;
    return r;
}
