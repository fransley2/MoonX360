#pragma once

#include <stdbool.h>
#include <stdint.h>

#define MAX_PADS 4

struct pad {
    bool present;
    int buttons;            /* Moonlight *_FLAG bits */
    uint8_t lt, rt;
    int16_t lx, ly, rx, ry;
};

/* Refreshes pads[] from the USB controller driver. Returns present mask. */
int input_poll(struct pad pads[MAX_PADS]);

/* Edge-detected button presses on any controller, for menus. */
int input_menu_pressed(void);

/* Starts/stops the coroutine that forwards controller state to the host. */
void input_stream_start(void);
void input_stream_stop(void);

/* Configure before starting a stream; zero deadzone preserves raw input. */
void input_configure(int quit_combo, int deadzone);
void input_calibrate(int left_min,int left_max,int right_min,int right_max);

/* True after holding the configured exit shortcut for two seconds. */
bool input_quit_requested(void);

void input_rumble(unsigned short controller, unsigned short low, unsigned short high);

#ifdef _XBOX
bool input_mouse_active(void);
void input_configure_pointer(int speed,int acceleration,int scroll_ms,bool stats);
unsigned input_quick_snapshot(void);
void input_mouse_indicator_available(bool available);
bool input_stats_visible(void);
void input_quick_ui_unavailable(void);
#endif
