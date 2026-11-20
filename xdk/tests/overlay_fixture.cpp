#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include <set>
#include <vector>
#include "../src/ui/canvas.h"
#include "../src/ui/font.h"
#include "../src/ui/ui.h"
static bool mouse_enabled;
static bool input_mouse_active(){return mouse_enabled;}
static void input_mouse_indicator_available(bool){}
static unsigned input_desktop_snapshot(){return 0;}
#include "../src/video.h"
#include "../src/video_session.h"
#include "../src/video_gpu_policy.h"
#include "../src/stream_metrics.h"
#include "../src/decode_deadline.h"
#include "../src/overlay_hud.h"

typedef uint32_t DWORD;
typedef long LONG;
#define FAILED(hr) ((hr) < 0)
#define D3DFMT_LIN_A8R8G8B8 0
#define GFX_SHADER_RGB 0
#define GFX_SHADER_RGBA 3
#define FALSE 0
#define TRUE 1
typedef int D3DRENDERSTATETYPE;
#define D3DRS_ALPHABLENDENABLE 1
#define D3DRS_SRCBLEND 2
#define D3DRS_DESTBLEND 3
#define D3DRS_BLENDOP 4
#define D3DRS_SEPARATEALPHABLENDENABLE 5
#define D3DBLENDOP_ADD 0
#define D3DBLEND_SRCALPHA 4
#define D3DBLEND_INVSRCALPHA 5
static bool fail_allocation, fail_texture, fail_upload, guide_active;
static int allocations, releases, uploads, waits, quads, lock_count;
static int layout_width=1280;
static std::set<DWORD> pending_fences;
static bool pending_any() { return !pending_fences.empty(); }
struct D3DTexture {
    std::vector<uint32_t> pixels;
    int w,h;
    DWORD fence;
    D3DTexture(int width,int height) : pixels(width*height),w(width),h(height),fence(0) { allocations++; }
    void Release() { assert(!fence || !pending_fences.count(fence)); releases++; delete this; }
};
struct D3DDevice {
    D3DTexture *bound;DWORD state[6];
    void GetRenderState(int key,DWORD *value){*value=state[key];}
    void SetRenderState(int key,DWORD value){state[key]=value;}
    int CreateTexture(int w, int h, int, int, int, int, D3DTexture **out, void *) {
        assert(w>=64 && w<=1248 && h>=32 && h<=688);
        *out = fail_texture ? NULL : new D3DTexture(w,h);
        return fail_texture ? -1 : 0;
    }
    void BlockOnFence(DWORD fence) {
        assert(fence);
        waits++;
        pending_fences.erase(fence);
    }
    void SetTexture(int slot, D3DTexture *texture) { assert(slot == 0); bound = texture; }
};
static D3DDevice device;
static D3DDevice *dev = &device;
static bool gfx_upload(D3DTexture *texture, const void *src, int pitch, int bytes, int rows)
{
    assert((!texture->fence || !pending_fences.count(texture->fence)) && pitch == texture->w * 4 && bytes == pitch && rows == texture->h);
    if (fail_upload) return false;
    memcpy(texture->pixels.data(), src, texture->pixels.size() * sizeof(uint32_t));
    uploads++;
    return true;
}
static void gfx_draw_rect(D3DDevice *d, int shader, float x0, float y0, float x1, float y1)
{
    assert(d->bound && shader == GFX_SHADER_RGBA);
    assert(d->state[1]==TRUE && d->state[2]==D3DBLEND_SRCALPHA && d->state[3]==D3DBLEND_INVSRCALPHA && d->state[4]==D3DBLENDOP_ADD && d->state[5]==FALSE);
    assert((x0==16 && y0==16 && x1==d->bound->w+16 && y1==d->bound->h+16) ||
           (x0==layout_width-16-d->bound->w && y0==704-d->bound->h && x1==layout_width-16 && y1==704));
    quads++;
}
static void gfx_draw_layout_rect(D3DDevice *d,int shader,float x0,float y0,float x1,float y1)
{gfx_draw_rect(d,shader,x0,y0,x1,y1);}
static bool overlay_cv_init(canvas_t *c, int w, int h)
{
    if (fail_allocation) return false;
    return cv_init(c, w, h);
}
#define cv_init overlay_cv_init
static bool gfx_system_ui_active(){return guide_active;}
static bool gfx_fence_pending(DWORD f){return f && pending_fences.count(f)!=0;}
static void gfx_wait_fence(D3DDevice *d,DWORD f){d->BlockOnFence(f);}
/* ACTUAL_OVERLAY */
#undef cv_init

static void mark_overlay_fence(DWORD f)
{
    if (overlay_tex[overlay_active]) overlay_tex[overlay_active]->fence = f;
    pending_fences.insert(f);
    gfx_overlay_mark_used(f);
}

static DWORD tick, overlay_last_ms, overlay_last_video_ms, network_updated_ms;
static bool overlay_has_text, overlay_rates_valid, overlay_update_pending;
static int overlay_last_mode, overlay_last_dev_page, overlay_last_alert;
static uint32_t overlay_last_loss;
static stream_rates overlay_last_rates;
static overlay_alert_tracker overlay_alerts;
static stream_observations observations;
static stream_counters overlay_previous;
static video_options options = {true, RENDERER_GPU, true, true, 60, 20000};
static video_stats stats;
static decode_deadline deadline_overlay;
static int vid_w = 1280, vid_h = 720, telemetry_lock;
static uint32_t network_rtt = 5, network_variance = 2;
static int network_video_queue, network_audio_ms = 24;
enum {TIME_DECODE,TIME_FRAME_COPY,TIME_TEXTURE_LOCK,TIME_TEXTURE_COPY,TIME_GPU_SUBMIT,TIME_FENCE_WAIT,TIME_PRESENT_WAIT,TIME_PACKET_PREPARE,TIME_CALLBACK_TOTAL,TIME_CALLBACK_GAP,TIME_COUNT};
static gpu_time_window pipeline_times[TIME_COUNT];
static uint32_t pipeline_ema[TIME_COUNT];
static int session_slices=8;
static float gfx_display_hz() {return 60.0f;}
static bool network_valid = true;
static int requested_shader;
static gpu_quality_policy quality_policy;
static unsigned gpu_slot_starvation;
static bool gfx_quality_available() {return true;}
enum { PIPE_AUTO, PIPE_CACHED, PIPE_LEGACY, PIPE_SWITCHING, PIPE_DISABLED };
static LONG current_pipeline() {return PIPE_CACHED;}
static void cached_video_snapshot(unsigned *ready, unsigned *in_flight) { *ready = 1; *in_flight = 1; }
static DWORD GetTickCount() { return tick; }
static void EnterCriticalSection(int *) { lock_count++; }
static void LeaveCriticalSection(int *) {}
static int stub_overlay_mode = OVERLAY_OFF;
static int stub_dev_page = 0;
static int active_overlay_mode(void) { return stub_overlay_mode; }
static int input_overlay_dev_page(void) { return stub_dev_page; }
/* ACTUAL_FORMATTER */

static bool step_overlay_frame(struct overlay_telemetry *t, DWORD fence)
{
    int om = active_overlay_mode();
    bool upd = overlay_collect(t);
    if (om == OVERLAY_OFF) {
        overlay_has_text = false;
        overlay_last_mode = OVERLAY_OFF;
        overlay_update_pending = false;
        return false;
    }
    if (upd) {
        assert(gfx_overlay_update(t));
        overlay_update_pending = gfx_overlay_deferred();
    }
    gfx_overlay_draw(dev);
    mark_overlay_fence(fence);
    return upd;
}

static void preview(const char *kind, int output_height) {
    float scale = output_height / 720.0f;
    int w = (int)(overlay_canvas.w * scale), h = (int)(overlay_canvas.h * scale);
    char name[100]; snprintf(name, sizeof(name), "hud-%s-%dp.ppm", kind, output_height);
    FILE *f = fopen(name, "wb"); assert(f); fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        uint32_t p = overlay_canvas.px[(int)(y / scale) * overlay_canvas.w + (int)(x / scale)];
        unsigned a = p >> 24;
        unsigned background[3] = {(unsigned)(64 + x * 160 / w), (unsigned)(90 + y * 130 / h), (unsigned)(120 + x * 80 / w)};
        unsigned char rgb[3];
        for (int c = 0; c < 3; c++)
            rgb[c] = (unsigned char)((((p >> (16 - c * 8)) & 255) * a + background[c] * (255 - a) + 127) / 255);
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

int main(void) {
    assert(font_init());
    struct overlay_telemetry t;
    memset(&t, 0x5A, sizeof(t));

    /* 1. OFF mode does zero work (0 locks, 0 allocations, 0 draws) */
    stub_overlay_mode = OVERLAY_OFF;
    assert(!overlay_collect(&t));
    assert(lock_count == 0 && allocations == 0 && quads == 0);

    /* Populate healthy 60 FPS streaming metrics */
    stats.received = 60; stats.decoded = 60; stats.rendered = 59;
    stats.avg_decode_us = 13400; stats.decode_threads = 4; stats.dropped = 0;
    deadline_overlay.queue_valid = true; deadline_overlay.queue_us = 1200;
    stream_observe(&observations, 100, 1875000, 41, 100, 2100, 3100);
    for (int i = 0; i < 64; i++) gpu_time_add(&pipeline_times[TIME_DECODE], 12800 + (i % 7) * 300);
    pipeline_ema[TIME_FRAME_COPY] = 900;
    pipeline_ema[TIME_GPU_SUBMIT] = 400;
    pipeline_ema[TIME_PRESENT_WAIT] = 2200;
    tick = 1000;
    network_updated_ms = tick;

    /* 2. SIMPLE mode (16:9): compact single line, <= 384px wide */
    stub_overlay_mode = OVERLAY_SIMPLE;
    assert(overlay_collect(&t));
    assert(t.mode == OVERLAY_SIMPLE && t.fps_valid && !t.video_stale && !t.network_stale);
    assert(gfx_overlay_update(&t));
    assert(overlay_canvas.w <= 384 && overlay_canvas.h <= 40);
    preview("simple-16x9", 720);

    /* 1 Hz throttling within same mode */
    tick = 1500;
    assert(!overlay_collect(&t));

    /* 3. DETAILED mode (16:9): immediate update even while previous texture fence is in flight! */
    stub_overlay_mode = OVERLAY_DETAILED;
    assert(overlay_collect(&t));
    assert(overlay_active_alert(&t, NULL) == NULL);
    assert(gfx_overlay_update(&t));
    assert(overlay_canvas.w >= 288 && overlay_canvas.w <= 384);
    preview("detailed-16x9", 720);

    /* Double-buffer GPU fence safety & non-blocking live-flight update regression tests:
     * Mark slot 0 in flight (fence 42); updating switches cleanly into slot 1 with waits == 0! */
    DWORD original[] = {0, 0, 9, 10, 7, 1};
    memcpy(dev->state, original, sizeof(original));
    gfx_overlay_draw(dev);
    assert(!memcmp(dev->state, original, sizeof(original)));
    mark_overlay_fence(42);
    int prev_uploads = uploads;
    assert(gfx_overlay_update(&t) && !gfx_overlay_deferred() && uploads == prev_uploads + 1 && waits == 0);
    assert(overlay_active == 1 && overlay_tex[1] != overlay_tex[0]);

    /* If BOTH buffers are simultaneously busy, defer cleanly (no wait, no dropped update), then retry! */
    gfx_overlay_draw(dev);
    mark_overlay_fence(43);
    prev_uploads = uploads;
    stub_overlay_mode = OVERLAY_SIMPLE;
    assert(step_overlay_frame(&t, 44) && gfx_overlay_deferred() && overlay_update_pending && uploads == prev_uploads && waits == 0);
    pending_fences.erase(42); /* Older buffer retires on next frame */
    pending_fences.erase(44);
    tick = 1516;
    assert(step_overlay_frame(&t, 45) && !gfx_overlay_deferred() && !overlay_update_pending && uploads == prev_uploads + 1);
    assert(overlay_w[overlay_active] <= 384 && overlay_h[overlay_active] <= 40);

    /* Continuous 1 Hz updates while active buffer fence is always pending at present time */
    for (int sec = 1; sec <= 3; sec++) {
        tick = 1516 + (DWORD)sec * 1000;
        network_updated_ms = tick;
        stats.received += 60; stats.decoded += 60; stats.rendered += 60;
        observations.payload_bytes += 1875000;
        pending_fences.clear();
        mark_overlay_fence((DWORD)(50 + sec)); /* active slot is busy */
        int before = uploads;
        assert(step_overlay_frame(&t, (DWORD)(60 + sec)) && !gfx_overlay_deferred() && uploads == before + 1);
    }

    /* OFF -> ON toggle within < 1000 ms refreshes immediately */
    pending_fences.clear();
    stub_overlay_mode = OVERLAY_OFF;
    step_overlay_frame(&t, 70);
    assert(!overlay_has_text);
    stub_overlay_mode = OVERLAY_DETAILED;
    tick += 16;
    assert(overlay_collect(&t) && gfx_overlay_update(&t) && overlay_w[overlay_active] >= 288);
    pending_fences.clear();

    /* 4. DEVELOPER mode (FFmpeg 16:9): <= 520px wide, p50/p95/p99 + sample count */
    stub_overlay_mode = OVERLAY_DEVELOPER;
    tick += 1000; network_updated_ms = tick;
    stats.received += 60; stats.decoded += 60; stats.rendered += 60;
    observations.payload_bytes += 1875000;
    assert(overlay_collect(&t));
    assert(t.percentiles_valid && t.decode_window_samples == 64);
    assert(gfx_overlay_update(&t));
    assert(overlay_canvas.w <= 520);
    preview("developer-16x9", 720);

    /* 5. Alert semantics & hysteresis:
     * 5a. Sustained active decode overload (requires 2 consecutive 1s windows, NOT 1) */
    stats.avg_decode_us = 18500;
    tick += 1000; network_updated_ms = tick;
    stats.received += 52; stats.decoded += 52; stats.rendered += 52;
    observations.payload_bytes += 1600000;
    assert(overlay_collect(&t));
    assert(overlay_active_alert(&t, NULL) == NULL); /* 1st tick: suppressed by hysteresis */
    tick += 1000; network_updated_ms = tick;
    stats.received += 52; stats.decoded += 52; stats.rendered += 52;
    observations.payload_bytes += 1600000;
    assert(overlay_collect(&t));
    uint32_t alert_col = 0;
    const char *alert = overlay_active_alert(&t, &alert_col);
    assert(alert && strcmp(alert, "Decode overload") == 0 && alert_col == DS_STATUS_ERROR);
    assert(gfx_overlay_update(&t));
    preview("developer-alert-16x9", 720);

    /* 5b. Video input interruption (0 frames received/decoded while old decode_us=18.5ms)
     * MUST NOT blame decoder; must show "No video input" and mute stale RTT! */
    DWORD stale_base = tick;
    tick = stale_base + 1000; /* no new frames, network_updated_ms still stale_base */
    assert(overlay_collect(&t));
    assert(t.video_stale && t.decode_stale);
    assert(overlay_decode_color(&t) == DS_TEXT_MUTED);
    assert(overlay_active_alert(&t, NULL) == NULL || strcmp(overlay_active_alert(&t, NULL), "Decode overload") != 0);
    tick = stale_base + 4000; /* 2nd stalled tick + network sample now 4000ms old (>3000ms -> stale!) */
    assert(overlay_collect(&t));
    assert(t.video_stale && t.network_stale);
    assert(overlay_rtt_color(&t) == DS_TEXT_MUTED); /* stale RTT NEVER green! */
    assert(overlay_fps_color(&t) != DS_STATUS_OK);  /* stale FPS NEVER green! */
    alert = overlay_active_alert(&t, &alert_col);
    assert(alert && strcmp(alert, "No video input") == 0);

    /* 5c. Recover to healthy stream and verify clean alert clearing */
    stats.avg_decode_us = 13400;
    for (int step = 0; step < 2; step++) {
        tick += 1000; network_updated_ms = tick;
        stats.received += 60; stats.decoded += 60; stats.rendered += 60;
        observations.payload_bytes += 1875000;
        assert(overlay_collect(&t));
    }
    assert(!t.video_stale && !t.network_stale && overlay_active_alert(&t, NULL) == NULL);

    /* 5d. System GPU Page 2: tight content-fitted height (no empty region below footer!) */
    struct overlay_telemetry sg = t;
    sg.mode = OVERLAY_DEVELOPER;
    sg.dev_page = 1;
    sg.sysgpu.valid = true;
    sg.sysgpu.feed_acquire_us = 3200;
    sg.sysgpu.output_wait_us = 1800;
    sg.sysgpu.in_flight = 2;
    sg.sysgpu.buffer_used = 3;
    sg.sysgpu.buffer_capacity = 6;
    sg.sysgpu.timeouts = 0;
    snprintf(sg.sysgpu.recovery_state, sizeof(sg.sysgpu.recovery_state), "Idle");
    snprintf(sg.backend_name, sizeof(sg.backend_name), "System GPU");
    assert(overlay_dev_needs_pagination(&sg, 1280));
    assert(gfx_overlay_update(&sg));
    assert(overlay_canvas.w <= 520 && overlay_canvas.h <= 116); /* exact fit for 3 rows + footer */
    preview("developer-sysgpu-p2", 720);

    /* System GPU output wait alert via tracker (2 sustained ticks) */
    struct overlay_alert_tracker sg_tr; memset(&sg_tr, 0, sizeof(sg_tr));
    sg.sysgpu.output_wait_us = 22000;
    assert(overlay_alert_update(&sg_tr, &sg) == OVERLAY_ALERT_NONE);
    assert(overlay_alert_update(&sg_tr, &sg) == OVERLAY_ALERT_SYSGPU_WAIT);

    /* 6. CRT 4:3 adaptation (640x480 / layout_width = 960) with healthy metrics */
    layout_width = 960;
    struct overlay_telemetry crt_t = t;
    crt_t.sysgpu.valid = false;
    snprintf(crt_t.backend_name, sizeof(crt_t.backend_name), "FFmpeg");
    crt_t.mode = OVERLAY_SIMPLE;
    assert(gfx_overlay_update(&crt_t));
    assert(overlay_canvas.w <= 448 && overlay_canvas.h == 42);
    preview("simple-4x3", 480);

    crt_t.mode = OVERLAY_DETAILED;
    assert(gfx_overlay_update(&crt_t));
    assert(overlay_canvas.w == 352 && overlay_canvas.h <= 226);
    preview("detailed-4x3", 480);

    crt_t.mode = OVERLAY_DEVELOPER; crt_t.dev_page = 0;
    assert(gfx_overlay_update(&crt_t));
    assert(overlay_canvas.w <= 480 && overlay_canvas.h <= 176);
    preview("developer-4x3-p1", 480);

    crt_t.dev_page = 1;
    assert(gfx_overlay_update(&crt_t));
    assert(overlay_canvas.w <= 576 && overlay_canvas.h <= 176);
    preview("developer-4x3-p2", 480);

    /* 7. Host microbenchmark: Off vs Simple vs Detailed vs Developer (60 frames = 1 s) */
    layout_width = 1280;
    puts("--- Performance Overlay Impact Comparison (1 s @ 60 FPS, 1280x720) ---");
    for (int m = OVERLAY_OFF; m <= OVERLAY_DEVELOPER; m++) {
        struct overlay_telemetry bt = t;
        bt.mode = m; bt.dev_page = 0;
        int bw = 0, bh = 0;
        overlay_measure_panel(&bt, 1280, &bw, &bh);
        int iters = 200;
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < iters; i++) {
            if (m != OVERLAY_OFF) {
                pending_fences.clear();
                gfx_overlay_update(&bt);
            }
        }
        auto t1 = std::chrono::steady_clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / iters;
        double kb_per_sec = (m == OVERLAY_OFF) ? 0.0 : (bw * bh * 4.0) / 1024.0;
        int locks_per_sec = (m == OVERLAY_OFF) ? 0 : 1;
        printf("Mode %-9s | %3dx%-3d px | raster+upload: %6.1f us/s (%.3f%% of 1s) | upload: %5.1f KB/s | locks/s: %d\n",
               overlay_mode_name(m), bw, bh, us, us / 10000.0, kb_per_sec, locks_per_sec);
    }

    /* Failure fallback & cleanup balance */
    gfx_overlay_clear(dev);
    fail_allocation = true; assert(!gfx_overlay_update(&t)); fail_allocation = false;
    fail_texture = true; assert(!gfx_overlay_update(&t) && !overlay_tex[0] && !overlay_tex[1] && !overlay_canvas.px); fail_texture = false;
    assert(gfx_overlay_update(&t));
    fail_upload = true; assert(!gfx_overlay_update(&t)); fail_upload = false;
    gfx_overlay_clear(dev);
    assert(allocations == releases);

    /* Text-only mouse indicator still works identically */
    ui_set_stream_hud(0, 1); mouse_enabled = true;
    memcpy(dev->state, original, sizeof(original));
    gfx_mouse_draw(dev);
    assert(mouse_indicator && !memcmp(dev->state, original, sizeof(original)));
    pending_fences.clear(); mouse_indicator->Release(); mouse_indicator = NULL; mouse_enabled = false;
    assert(allocations == releases);

    puts("PASS: double-buffered GPU fence safety, non-dropping mode switching, continuous 1 Hz updates, Off->On refresh, CRT 4:3 layout, and cleanup.");
}
