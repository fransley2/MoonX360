/* Platform-independent stream input policy. No device or kernel patches. */
#pragma once
#include "input.h"
#include "stick_calibration.h"
#include <string.h>
#include <Limelight.h>

struct input_settings {
    int quit_combo; /* 0: shoulders + sticks; 1: Back + Start + shoulders */
    int deadzone;   /* legacy fallback when calibrated == false */
    bool calibrated;
    int left_min,left_max,right_min,right_max;
};

struct input_session {
    struct pad last[MAX_PADS];
    bool announced[MAX_PADS], holding[MAX_PADS];
    uint32_t held_since[MAX_PADS];
    int mask;
};

static int input_exit_mask(int choice)
{
    return LB_FLAG | RB_FLAG | (choice == 1 ? BACK_FLAG | PLAY_FLAG : LS_CLK_FLAG | RS_CLK_FLAG);
}

/* Axial deadzone with continuous rescaling and exact signed endpoints. */
static int16_t input_axis(int16_t value, int percent)
{
    int magnitude = value < 0 ? -(int)value : (int)value;
    int limit = value < 0 ? 32768 : 32767;
    int threshold;
    if (percent <= 0) return value;
    if (percent > 30) percent = 30;
    threshold = limit * percent / 100;
    if (magnitude <= threshold) return 0;
    magnitude = (magnitude - threshold) * limit / (limit - threshold);
    return (int16_t)(value < 0 ? -magnitude : magnitude);
}

/* Returns true after one controller holds its complete combo for two seconds.
 * Disconnects, releases and mixed controllers never accumulate hold time. */
static bool input_session_step(struct input_session *s, struct pad pads[MAX_PADS],
                               int mask, uint32_t now, const struct input_settings *opts)
{
    bool quit = false;
    int i, combo = input_exit_mask(opts->quit_combo);
    for (i = 0; i < MAX_PADS; i++) {
        struct pad *p = &pads[i];
        if (!p->present) {
            if (s->announced[i]) {
                LiSendMultiControllerEvent((short)i, (short)mask, 0, 0, 0, 0, 0, 0, 0);
                s->announced[i] = false;
                memset(&s->last[i], 0, sizeof(s->last[i]));
            }
            s->holding[i] = false;
            s->held_since[i] = 0;
            continue;
        }
        bool arrived = !s->announced[i];
        if (arrived) {
            LiSendControllerArrivalEvent((uint8_t)i, (uint16_t)mask, LI_CTYPE_XBOX,
                A_FLAG | B_FLAG | X_FLAG | Y_FLAG | UP_FLAG | DOWN_FLAG | LEFT_FLAG | RIGHT_FLAG |
                LB_FLAG | RB_FLAG | PLAY_FLAG | BACK_FLAG | LS_CLK_FLAG | RS_CLK_FLAG | SPECIAL_FLAG,
                LI_CCAP_ANALOG_TRIGGERS | LI_CCAP_RUMBLE);
            s->announced[i] = true;
        }
        if ((p->buttons & combo) == combo) {
            if (!s->holding[i]) {
                s->held_since[i] = now;
                s->holding[i] = true;
            } else if ((uint32_t)(now - s->held_since[i]) >= 2000) {
                quit = true;
            }
            /* Release anything previously sent; keep the full local combo off the PC. */
            memset(p, 0, sizeof(*p));
            p->present = true;
        } else {
            s->holding[i] = false;
            p->lx = opts->calibrated ? stick_calibrate(p->lx,opts->left_min,opts->left_max) : input_axis(p->lx, opts->deadzone);
            p->ly = opts->calibrated ? stick_calibrate(p->ly,opts->left_min,opts->left_max) : input_axis(p->ly, opts->deadzone);
            p->rx = opts->calibrated ? stick_calibrate(p->rx,opts->right_min,opts->right_max) : input_axis(p->rx, opts->deadzone);
            p->ry = opts->calibrated ? stick_calibrate(p->ry,opts->right_min,opts->right_max) : input_axis(p->ry, opts->deadzone);
        }
        if (arrived || mask != s->mask || memcmp(p, &s->last[i], sizeof(*p)) != 0) {
            LiSendMultiControllerEvent((short)i, (short)mask, p->buttons, p->lt, p->rt,
                                      p->lx, p->ly, p->rx, p->ry);
            s->last[i] = *p;
        }
    }
    s->mask = mask;
    return quit;
}

static void input_session_release(struct input_session *s)
{
    int i;
    for (i = 0; i < MAX_PADS; i++)
        if (s->announced[i])
            LiSendMultiControllerEvent((short)i, 0, 0, 0, 0, 0, 0, 0, 0);
    memset(s,0,sizeof(*s));
}
