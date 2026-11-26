#pragma once
/* MoonX360 native PC Guide Button (Start + LB held 350 ms -> SPECIAL_FLAG).
 * Pure C/C++ per-controller state machine: zero allocations, zero sleeps,
 * wrap-safe monotonic millisecond timing, and conflict isolation against
 * Xbox 360 system overlays, session disconnect chords, Quick Menu and Desktop Mode. */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <Limelight.h>

#ifndef MAX_PADS
#define MAX_PADS 4
#endif

#define INPUT_GUIDE_COMBO           (PLAY_FLAG | LB_FLAG)
#define INPUT_GUIDE_HOLD_MS         350u
#define INPUT_GUIDE_SIMULTANEITY_MS 80u

enum input_guide_phase {
    INPUT_GUIDE_IDLE = 0,
    INPUT_GUIDE_CANDIDATE = 1,
    INPUT_GUIDE_GUIDE_HELD = 2,
    INPUT_GUIDE_CANCELLED = 3,
    INPUT_GUIDE_SUPPRESS_UNTIL_RELEASE = 4
};

struct input_guide_port {
    uint8_t phase;
    int first_btn;
    uint32_t first_ms;
    uint32_t combo_start_ms;
    bool guide_active;
};

struct input_guide_state {
    struct input_guide_port ports[MAX_PADS];
};

static void input_guide_reset_port(struct input_guide_port *p)
{
    if (p)
        memset(p, 0, sizeof(*p));
}

static void input_guide_reset(struct input_guide_state *g)
{
    if (g)
        memset(g, 0, sizeof(*g));
}

/* Advances PC Guide state for one controller port and updates *io_buttons in place.
 * Standalone Start or LB passes through immediately (0 ms delay); if the second combo
 * button arrives within INPUT_GUIDE_SIMULTANEITY_MS, both are suppressed and any
 * pre-sent leading button is coherently released by the session delta sender. */
static void input_guide_step(struct input_guide_state *g, int *io_buttons, int port,
                             int raw_buttons, bool raw_present, bool blocked,
                             uint32_t now_ms, bool enabled, uint32_t hold_ms)
{
    struct input_guide_port *p;
    int combo;
    bool both, conflict;

    if (!g || port < 0 || port >= MAX_PADS)
        return;
    if (!hold_ms)
        hold_ms = INPUT_GUIDE_HOLD_MS;
    p = &g->ports[port];

    if (!enabled || !raw_present) {
        input_guide_reset_port(p);
        if (!raw_present && io_buttons)
            *io_buttons &= ~SPECIAL_FLAG;
        return;
    }

    combo = raw_buttons & INPUT_GUIDE_COMBO;
    both = (combo == INPUT_GUIDE_COMBO);
    conflict = blocked || ((raw_buttons & BACK_FLAG) != 0);

    if (conflict) {
        p->guide_active = false;
        p->first_btn = 0;
        if (io_buttons)
            *io_buttons &= ~SPECIAL_FLAG;
        if (combo != 0)
            p->phase = INPUT_GUIDE_SUPPRESS_UNTIL_RELEASE;
        else
            p->phase = INPUT_GUIDE_IDLE;
        return;
    }

    switch (p->phase) {
    case INPUT_GUIDE_IDLE:
        if (!combo) {
            p->first_btn = 0;
            break;
        }
        if (!both) {
            /* Single Start or LB: forward normally with zero artificial latency. */
            if (p->first_btn != combo) {
                p->first_btn = combo;
                p->first_ms = now_ms;
            }
            break;
        }
        if (p->first_btn == 0 || (uint32_t)(now_ms - p->first_ms) <= INPUT_GUIDE_SIMULTANEITY_MS) {
            p->phase = INPUT_GUIDE_CANDIDATE;
            p->combo_start_ms = now_ms;
            p->first_btn = 0;
            p->guide_active = false;
            if (io_buttons)
                *io_buttons &= ~(INPUT_GUIDE_COMBO | SPECIAL_FLAG);
        } else {
            /* Sequential press outside simultaneity window: normal gameplay pass-through. */
            p->phase = INPUT_GUIDE_CANCELLED;
            p->first_btn = 0;
        }
        break;

    case INPUT_GUIDE_CANDIDATE:
        if (!both) {
            /* Released before hold threshold: never emit SPECIAL_FLAG or stray taps. */
            p->guide_active = false;
            p->first_btn = 0;
            if (io_buttons)
                *io_buttons &= ~(INPUT_GUIDE_COMBO | SPECIAL_FLAG);
            p->phase = combo ? INPUT_GUIDE_SUPPRESS_UNTIL_RELEASE : INPUT_GUIDE_IDLE;
        } else if ((uint32_t)(now_ms - p->combo_start_ms) >= hold_ms) {
            p->phase = INPUT_GUIDE_GUIDE_HELD;
            p->guide_active = true;
            if (io_buttons)
                *io_buttons = (*io_buttons & ~INPUT_GUIDE_COMBO) | SPECIAL_FLAG;
        } else {
            if (io_buttons)
                *io_buttons &= ~(INPUT_GUIDE_COMBO | SPECIAL_FLAG);
        }
        break;

    case INPUT_GUIDE_GUIDE_HELD:
        if (!both) {
            /* Immediate release of SPECIAL_FLAG when either Start or LB is released. */
            p->guide_active = false;
            p->first_btn = 0;
            if (io_buttons)
                *io_buttons &= ~(INPUT_GUIDE_COMBO | SPECIAL_FLAG);
            p->phase = combo ? INPUT_GUIDE_SUPPRESS_UNTIL_RELEASE : INPUT_GUIDE_IDLE;
        } else if (io_buttons) {
            *io_buttons = (*io_buttons & ~INPUT_GUIDE_COMBO) | SPECIAL_FLAG;
        }
        break;

    case INPUT_GUIDE_CANCELLED:
        if (!combo) {
            p->phase = INPUT_GUIDE_IDLE;
            p->first_btn = 0;
        }
        break;

    case INPUT_GUIDE_SUPPRESS_UNTIL_RELEASE:
        p->guide_active = false;
        if (!combo) {
            p->phase = INPUT_GUIDE_IDLE;
            p->first_btn = 0;
        } else if (io_buttons) {
            *io_buttons &= ~(INPUT_GUIDE_COMBO | SPECIAL_FLAG);
        }
        break;
    }
}
