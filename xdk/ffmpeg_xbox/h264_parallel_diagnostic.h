/* Per-access-unit facts from the bundled FFmpeg H.264 parser and slice pool. */
#ifndef MOONX_H264_PARALLEL_DIAGNOSTIC_H
#define MOONX_H264_PARALLEL_DIAGNOSTIC_H
#include <stdint.h>

struct AVCodecContext;
typedef struct FFH264ParallelDiagnostic {
    uint32_t slices;
    uint32_t raw_idc[3];          /* disable_deblocking_filter_idc, before FFmpeg remap */
    uint32_t effective_filter[3]; /* 0 off, 1 across slices, 2 within slices */
    uint32_t reference_slices;
    uint32_t cabac_slices;
    uint32_t forced_serial;       /* type 1 deblocking reduced max_contexts */
    uint32_t contexts_requested;
    uint32_t contexts_effective;
    uint32_t serial_dispatches;
    uint32_t parallel_dispatches;
    uint32_t max_dispatch_contexts;
    uint32_t max_worker_participants;
    uint32_t max_concurrent_workers;
    /* Xenon QPC ticks, populated only by the isolated stage-profile build. */
    uint64_t slice_ticks;
    uint64_t loopfilter_ticks;
    uint64_t entropy_sample_ticks;
    uint32_t slice_calls;
    uint32_t loopfilter_calls;
    uint32_t entropy_samples;
} FFH264ParallelDiagnostic;

#ifdef __cplusplus
extern "C" {
#endif
int ff_h264_xbox_parallel_diagnostic(const struct AVCodecContext *avctx,
                                     FFH264ParallelDiagnostic *out);
void ff_xbox_slice_last_dispatch(const struct AVCodecContext *avctx,
                                 unsigned *participants, unsigned *concurrent);
#ifdef __cplusplus
}
#endif
#endif
