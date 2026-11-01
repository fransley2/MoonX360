/* Small, bounded telemetry math. No platform calls or rendering. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

struct stream_observations {
    uint32_t payload_bytes, frame_gaps, last_frame;
    uint32_t host_us, assembly_us, queue_us;
    bool have_frame, have_host, have_timing;
};
static uint32_t stream_smooth(uint32_t old, uint32_t value)
{
    return (uint32_t)(((uint64_t)old * 15 + value) / 16);
}
static void stream_observe(struct stream_observations *m, uint32_t frame, int bytes,
                           uint16_t host_100us, uint64_t receive_us,
                           uint64_t enqueue_us, uint64_t submit_us)
{
    if (bytes > 0) m->payload_bytes += (uint32_t)bytes;
    if (m->have_frame) {
        uint32_t gap = frame - m->last_frame;
        if (gap > 1 && gap < 0x80000000u) m->frame_gaps += gap - 1;
        if (gap < 0x80000000u) m->last_frame = frame;
    } else {
        m->last_frame = frame;
    }
    m->have_frame = true;
    if (host_100us) {
        uint32_t us = (uint32_t)host_100us * 100;
        m->host_us = m->have_host ? stream_smooth(m->host_us, us) : us;
        m->have_host = true;
    }
    if (receive_us && enqueue_us && enqueue_us >= receive_us && submit_us >= enqueue_us &&
        enqueue_us - receive_us <= 10000000 && submit_us - enqueue_us <= 10000000) {
        uint32_t assembly = (uint32_t)(enqueue_us - receive_us);
        uint32_t queue = (uint32_t)(submit_us - enqueue_us);
        m->assembly_us = m->have_timing ? stream_smooth(m->assembly_us, assembly) : assembly;
        m->queue_us = m->have_timing ? stream_smooth(m->queue_us, queue) : queue;
        m->have_timing = true;
    }
}
struct stream_counters { uint32_t received, decoded, shown, payload_bytes; };
struct stream_rates { double received, decoded, shown, payload_mbps; };
/* These are backend stage rates. Callbacks are not network arrivals; a video
 * Present call is not proof of scanout or visibility under an opaque cover. */
static double stream_rate_per_second(uint32_t count,uint64_t duration_us)
{
    return duration_us?count*1000000.0/duration_us:0.0;
}
static struct stream_rates stream_rates_between(const struct stream_counters *before,
                                                const struct stream_counters *after, uint32_t ms)
{
    struct stream_rates r;
    memset(&r, 0, sizeof(r));
    if (!ms) return r;
    r.received = (uint32_t)(after->received - before->received) * 1000.0 / ms;
    r.decoded = (uint32_t)(after->decoded - before->decoded) * 1000.0 / ms;
    r.shown = (uint32_t)(after->shown - before->shown) * 1000.0 / ms;
    r.payload_mbps = (uint32_t)(after->payload_bytes - before->payload_bytes) * 0.008 / ms;
    return r;
}
