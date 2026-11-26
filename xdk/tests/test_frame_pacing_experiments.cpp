#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include "../../src/frame_pacing.h"

struct SimFrame {
    uint32_t id;
    uint32_t rtp_ts;
    uint64_t arrival_us;
    uint32_t decode_us;
};

static std::vector<SimFrame> make_scenario(int scenario, int count) {
    std::vector<SimFrame> out;
    uint64_t t = 10000;
    for (int i = 0; i < count; i++) {
        SimFrame f{};
        f.id = (uint32_t)(i + 1);
        uint32_t step = (scenario == 2) ? 33333u : 16667u;
        int jitter = 0;
        if (scenario == 0 || scenario == 1) {
            static const int j[8] = { -2200, 1800, -1100, 2900, -2500, 1400, 900, -1200 };
            jitter = j[i & 7];
        } else if (scenario == 5) {
            static const int j[8] = { -4000, 6500, -3000, 9000, -5000, 4200, -2000, 7500 };
            jitter = j[i & 7];
        }
        uint64_t arr = t + (uint64_t)((int64_t)step * i + jitter);
        uint32_t dec = 3800u + (uint32_t)((i * 313) % 1400);
        if (scenario == 3 && i >= 60 && i < 66) dec = 26000u; /* decode overload burst */
        if (scenario == 4 && (i % 60 == 0)) dec = 14500u;      /* periodic IDR spike */
        f.arrival_us = arr;
        f.decode_us = dec;
        f.rtp_ts = (uint32_t)(f.arrival_us * 90ULL / 1000ULL);
        out.push_back(f);
    }
    return out;
}

static frame_pacing_summary run_sim(int mode, int target_fps, int display_hz_x100, const std::vector<SimFrame> &frames) {
    frame_pacing_state s;
    frame_pacing_init(&s, mode, target_fps, display_hz_x100);
    uint64_t refresh_us = 100000000ULL / (uint64_t)display_hz_x100;
    int interval = frame_pacing_present_interval(mode, true, target_fps, display_hz_x100);
    uint64_t vblank_step = refresh_us * (uint64_t)(interval > 0 ? interval : 1);

    struct ReadyItem {
        SimFrame f;
        uint64_t dec_start, dec_end;
    };
    std::vector<ReadyItem> q;
    uint64_t dec_free = 0;
    size_t idx = 0;
    uint64_t next_vblank = 10000 + vblank_step;
    uint64_t last_sim = frames.back().arrival_us + 200000ULL;

    while ((idx < frames.size() || !q.empty()) && next_vblank <= last_sim) {
        /* Handle frames whose decode completed before this vblank */
        while (idx < frames.size() && frames[idx].arrival_us <= next_vblank) {
            uint64_t ds = frames[idx].arrival_us > dec_free ? frames[idx].arrival_us : dec_free;
            uint64_t de = ds + frames[idx].decode_us;
            if (de > next_vblank) break;
            dec_free = de;
            q.push_back({ frames[idx], ds, de });
            idx++;
        }

        if (q.empty()) {
            s.repeated_count++;
            next_vblank += vblank_step;
            continue;
        }

        if (mode != FRAME_PACING_BASELINE) {
            while (q.size() > 1 && frame_pacing_should_prune_ready(&s, next_vblank, q.front().dec_end, (unsigned)q.size())) {
                s.dropped_count++;
                s.superseded_count++;
                q.erase(q.begin());
            }
        }
        if (q.size() > 1) {
            s.dropped_count += (uint32_t)(q.size() - 1);
            s.superseded_count += (uint32_t)(q.size() - 1);
            ReadyItem newest = q.back();
            q.clear();
            q.push_back(newest);
        }

        ReadyItem cur = q.back();
        q.pop_back();
        uint32_t wait_us = frame_pacing_compute_wait_us(&s, next_vblank, cur.f.arrival_us, cur.f.rtp_ts, 1);
        uint64_t p_start = next_vblank;
        if (wait_us > (uint32_t)(refresh_us / 3u)) {
            /* Waiting pushes presentation to the next vblank slot on a VSync display. */
            p_start = next_vblank + refresh_us;
        }
        frame_pacing_record rec{};
        rec.frame_id = cur.f.id;
        rec.rtp_timestamp = cur.f.rtp_ts;
        rec.arrival_us = cur.f.arrival_us;
        rec.enqueue_us = cur.f.arrival_us + 200u;
        rec.decode_start_us = cur.dec_start;
        rec.decode_end_us = cur.dec_end;
        rec.present_start_us = p_start;
        rec.present_end_us = p_start + 350u;
        rec.frame_age_us = (uint32_t)(p_start - cur.f.arrival_us);
        frame_pacing_record_sample(&s, &rec);
        next_vblank = p_start + vblank_step;
    }

    frame_pacing_summary sum;
    frame_pacing_summarize(&s, &sum);
    return sum;
}

int main(void) {
    struct ScenarioSpec { const char *name; int id; int fps; int hz_x100; };
    const ScenarioSpec specs[6] = {
        { "60fps_jitter_60hz",       0, 60, 6000 },
        { "60fps_ntsc_5994hz",       1, 60, 5994 },
        { "30fps_on_60hz",           2, 30, 6000 },
        { "60fps_decode_overload",   3, 60, 6000 },
        { "60fps_periodic_idr",      4, 60, 6000 },
        { "60fps_wireless_jitter",   5, 60, 6000 }
    };

    printf("| Scenario | Policy | FPS | Age p50/p95/p99 (us) | Int p50/p95/p99 (us) | Dropped | Repeated | Late |\n");
    printf("|---|---|---:|---|---|---:|---:|---:|\n");

    frame_pacing_summary base_30{}, align_30{};
    frame_pacing_summary base_ov{}, bounded_ov{};
    for (int sc = 0; sc < 6; sc++) {
        auto frames = make_scenario(specs[sc].id, 240);
        for (int mode = 0; mode < FRAME_PACING_COUNT; mode++) {
            frame_pacing_summary r = run_sim(mode, specs[sc].fps, specs[sc].hz_x100, frames);
            printf("| %s | %s | %.2f | %u/%u/%u | %u/%u/%u | %u | %u | %u |\n",
                   specs[sc].name, frame_pacing_mode_name(mode), r.fps_presented,
                   r.frame_age_p50_us, r.frame_age_p95_us, r.frame_age_p99_us,
                   r.present_interval_p50_us, r.present_interval_p95_us, r.present_interval_p99_us,
                   r.dropped_count, r.repeated_count, r.late_count);
            if (sc == 2 && mode == FRAME_PACING_BASELINE) base_30 = r;
            if (sc == 2 && mode == FRAME_PACING_REFRESH_ALIGNED) align_30 = r;
            if (sc == 3 && mode == FRAME_PACING_BASELINE) base_ov = r;
            if (sc == 3 && mode == FRAME_PACING_BOUNDED_QUEUE) bounded_ov = r;
        }
    }

    /* Core pacing invariants */
    assert(align_30.present_interval_p50_us >= 33000 && align_30.present_interval_p50_us <= 34000);
    assert(align_30.repeated_count == 0);
    assert(base_30.repeated_count > 0);
    assert(bounded_ov.frame_age_p99_us <= base_ov.frame_age_p99_us);

    /* Present interval selection check */
    assert(frame_pacing_present_interval(FRAME_PACING_BASELINE, true, 30, 6000) == 1);
    assert(frame_pacing_present_interval(FRAME_PACING_REFRESH_ALIGNED, true, 30, 6000) == 2);
    assert(frame_pacing_present_interval(FRAME_PACING_REFRESH_ALIGNED, false, 30, 6000) == 0);
    assert(frame_pacing_present_interval(FRAME_PACING_REFRESH_ALIGNED, true, 60, 5994) == 1);

    puts("PASS: Frame Pacing A-F instrumentation, p50/p95/p99 summaries, D3DPRESENT_INTERVAL_TWO 30-on-60Hz lock, and bounded-queue overload protection.");
    return 0;
}
