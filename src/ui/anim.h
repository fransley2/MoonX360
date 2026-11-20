#pragma once
/* Lightweight, zero-allocation native animation primitives for Xbox 360 UI.
 * Uses bounded real-time elapsed ms with ease-out cubic interpolation so motion
 * stays smooth regardless of presentation cadence and never blocks input. */
#include <stdbool.h>
#include <stdint.h>

#define UI_ANIM_FOCUS_MS      90u
#define UI_ANIM_SCROLL_MS     130u
#define UI_ANIM_PAGE_MS       120u
#define UI_ANIM_QUICK_MS      120u
#define UI_ANIM_WALLPAPER_MS  180u
#define UI_ANIM_DIALOG_MS     110u

static inline float ui_anim_clamp01(float t)
{
    return t <= 0.0f ? 0.0f : t >= 1.0f ? 1.0f : t;
}

static inline float ui_ease_out_cubic(float t)
{
    float r = 1.0f - ui_anim_clamp01(t);
    return 1.0f - r * r * r;
}

static inline float ui_ease_out_quad(float t)
{
    float r = 1.0f - ui_anim_clamp01(t);
    return 1.0f - r * r;
}

static inline float ui_anim_progress(uint32_t now, uint32_t started, uint32_t duration_ms)
{
    if (!duration_ms) return 1.0f;
    uint32_t elapsed = (uint32_t)(now - started);
    if (elapsed >= duration_ms) return 1.0f;
    return (float)elapsed / (float)duration_ms;
}

static inline float ui_anim_lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

struct ui_anim {
    float origin, current, target;
    uint32_t started, duration_ms;
    bool active;
};

static inline void ui_anim_snap(struct ui_anim *a, float value)
{
    a->origin = a->current = a->target = value;
    a->started = 0;
    a->duration_ms = 0;
    a->active = false;
}

static inline void ui_anim_to(struct ui_anim *a, float target, uint32_t now,
                              uint32_t duration_ms, bool motion_enabled)
{
    if (a->target == target && (a->active || a->current == target)) return;
    a->origin = a->current;
    a->target = target;
    a->started = now;
    a->duration_ms = duration_ms;
    a->active = motion_enabled && duration_ms > 0 && a->origin != target;
    if (!a->active) a->current = target;
}

static inline bool ui_anim_tick(struct ui_anim *a, uint32_t now, bool motion_enabled)
{
    if (!a->active) return false;
    if (!motion_enabled || !a->duration_ms) {
        a->current = a->target;
        a->active = false;
        return true;
    }
    float t = ui_anim_progress(now, a->started, a->duration_ms);
    a->current = ui_anim_lerp(a->origin, a->target, ui_ease_out_cubic(t));
    if (t >= 1.0f) {
        a->current = a->target;
        a->active = false;
    }
    return true;
}
