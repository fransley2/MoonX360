/* Small, allocation-free video progress classifier. Timestamps use the same
 * wrap-safe millisecond clock as the streaming loop. CPU Present completion
 * does not prove that a new image reached the display. */
#pragma once
#include <stdint.h>
#include <string.h>

enum video_progress_stage {
    VP_RX, VP_ASSEMBLED, VP_SUBMITTED, VP_CALLBACK_ENTERED, VP_CALLBACK_RETURNED,
    VP_DECODED, VP_QUEUED,
    VP_SELECTED, VP_UPLOAD_STARTED, VP_UPLOADED, VP_GPU_SUBMITTED, VP_PRESENT_CALLED,
    VP_PRESENT_COMPLETED, VP_RENDER_ITERATION, VP_AUDIO, VP_INPUT,
    VP_FENCE_COMPLETED, VP_IDR_REQUESTED, VP_DECODER_RESET, VP_STAGE_COUNT
};
enum video_stall_kind {
    VS_NONE, VS_RX, VS_ASSEMBLY, VS_DECODER, VS_QUEUE,
    VS_UPLOAD, VS_GPU, VS_PRESENTATION
};
struct video_progress_snapshot {
    uint32_t count[VP_STAGE_COUNT], last_ms[VP_STAGE_COUNT];
    uint32_t frame_serial, cached_in_flight, cached_ready, gpu_slot_capacity;
    /* frame_serial is a session-wide completed-new-frame generation. */
    uint32_t max_queue_age_ms, max_gpu_wait_ms;
};
struct video_watchdog {
    struct video_progress_snapshot baseline;
    uint32_t last_present_ms, warned_ms, next_action_ms;
    unsigned level, attempts[7], successes, failures, classifications;
    uint32_t maximum_stall_ms;
    enum video_stall_kind kind,last_kind;
    int armed, warned;
};
static const char *video_stall_name(enum video_stall_kind kind)
{
    static const char *names[]={"NONE","VIDEO_RX_STALLED","VIDEO_ASSEMBLY_STALLED",
        "DECODER_STALLED","FRAME_QUEUE_STALLED","UPLOAD_STALLED",
        "GPU_STALLED","PRESENTATION_STALLED"};
    return kind>=VS_NONE && kind<=VS_PRESENTATION?names[kind]:"UNKNOWN";
}
static enum video_stall_kind video_stall_classify(const struct video_progress_snapshot *a,
                                                   const struct video_progress_snapshot *b)
{
#define VP_DELTA(s) (b->count[s]-a->count[s])
    if (VP_DELTA(VP_PRESENT_CALLED)>VP_DELTA(VP_PRESENT_COMPLETED)) return VS_PRESENTATION;
    if (b->gpu_slot_capacity && b->cached_in_flight>=b->gpu_slot_capacity &&
        VP_DELTA(VP_DECODED) && !VP_DELTA(VP_UPLOADED)) return VS_GPU;
    if (VP_DELTA(VP_UPLOAD_STARTED)>VP_DELTA(VP_UPLOADED) &&
        (!VP_DELTA(VP_UPLOADED) ||
         (int32_t)(b->last_ms[VP_UPLOAD_STARTED]-b->last_ms[VP_UPLOADED])>0 ||
         (VP_DELTA(VP_CALLBACK_ENTERED)>VP_DELTA(VP_CALLBACK_RETURNED) &&
          b->last_ms[VP_UPLOAD_STARTED]==b->last_ms[VP_UPLOADED]))) return VS_UPLOAD;
    if (VP_DELTA(VP_CALLBACK_ENTERED)>VP_DELTA(VP_CALLBACK_RETURNED) &&
        VP_DELTA(VP_SUBMITTED) && !VP_DELTA(VP_DECODED)) return VS_DECODER;
    if (!VP_DELTA(VP_RX))
        return VP_DELTA(VP_AUDIO) || VP_DELTA(VP_INPUT)?VS_RX:VS_NONE;
    if (!VP_DELTA(VP_ASSEMBLED)) return VS_ASSEMBLY;
    if (!VP_DELTA(VP_SUBMITTED) || !VP_DELTA(VP_DECODED)) return VS_DECODER;
    if (!VP_DELTA(VP_QUEUED) || !VP_DELTA(VP_SELECTED)) return VS_QUEUE;
    if (!VP_DELTA(VP_UPLOADED)) return VS_UPLOAD;
    if (VP_DELTA(VP_UPLOADED) && !VP_DELTA(VP_GPU_SUBMITTED)) return VS_GPU;
    if (!VP_DELTA(VP_PRESENT_COMPLETED) || b->frame_serial==a->frame_serial)
        return VS_PRESENTATION;
    return VS_NONE;
#undef VP_DELTA
}
/* Called every ~250 ms on the application thread. Only a previously shown
 * stream is armed. A short gap cannot trigger a recovery action. */
static unsigned video_watchdog_tick(struct video_watchdog *w,
                                    const struct video_progress_snapshot *p,uint32_t now)
{
    if (!p->count[VP_PRESENT_COMPLETED]) return 0;
    if (!w->armed) {
        w->baseline=*p;w->last_present_ms=now;w->armed=1;return 0;
    }
    if (p->count[VP_PRESENT_COMPLETED]!=w->baseline.count[VP_PRESENT_COMPLETED] &&
        p->frame_serial!=w->baseline.frame_serial) {
        if(w->warned && w->level)w->successes++;
        w->baseline=*p;w->last_present_ms=now;w->warned=w->level=0;
        w->kind=VS_NONE;return 0;
    }
    uint32_t age=now-w->last_present_ms;
    if(age>w->maximum_stall_ms)w->maximum_stall_ms=age;
    if(age<1000)return 0;
    enum video_stall_kind kind=video_stall_classify(&w->baseline,p);
    if(kind==VS_NONE)return 0;
    if(!w->warned || kind!=w->kind){w->warned=1;w->warned_ms=now;w->kind=w->last_kind=kind;w->classifications|=1u<<kind;}
    /* Observation spans at least 3 s; actions are spaced so an IDR can land. */
    if(age<3000 || (w->next_action_ms && (int32_t)(now-w->next_action_ms)<0))return 0;
    if(w->level>=6)return 0;
    /* Lack of UDP video alone is less specific than a broken decoder or GPU.
     * Give the host an extended loading/encoder pause before reconnecting. */
    if(w->level==5 && w->kind==VS_RX && age<30000)return 0;
    w->level++;
    w->attempts[w->level]++;
    w->next_action_ms=now+(w->level==1?2000:3000);
    return w->level;
}
static int video_watchdog_reconnect_safe(const struct video_watchdog *w,
                                        const struct video_progress_snapshot *p,
                                        uint32_t now,unsigned attempts)
{
    return attempts<1 && (w->kind==VS_RX || w->kind==VS_ASSEMBLY || w->kind==VS_DECODER) &&
        p->count[VP_CALLBACK_ENTERED]==p->count[VP_CALLBACK_RETURNED] &&
        (w->kind!=VS_DECODER || p->count[VP_CALLBACK_RETURNED]!=w->baseline.count[VP_CALLBACK_RETURNED]) &&
        !p->cached_in_flight && p->count[VP_PRESENT_CALLED]==p->count[VP_PRESENT_COMPLETED] &&
        p->count[VP_RENDER_ITERATION] && now-p->last_ms[VP_RENDER_ITERATION]<1000;
}
