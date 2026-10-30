#include <assert.h>
#include <stdio.h>
#include <vector>
#include "../../src/input_session.h"

struct event { int port, mask, buttons, lt, lx; };
static std::vector<event> events;
static int arrivals[MAX_PADS];
int LiSendControllerArrivalEvent(uint8_t port, uint16_t, uint8_t, uint32_t, uint16_t)
{
    arrivals[port]++;
    return 0;
}
int LiSendMultiControllerEvent(short port, short mask, int buttons, unsigned char lt,
                              unsigned char, short lx, short, short, short)
{
    event e = { port, mask, buttons, lt, lx };
    events.push_back(e);
    return 0;
}

int main(void)
{
    input_session s = {};
    input_settings opts = {};
    pad pads[MAX_PADS] = {};
    pads[0].present = true;
    pads[0].buttons = A_FLAG;
    pads[0].lt = 255;
    pads[0].lx = -32768;
    assert(!input_session_step(&s, pads, 1, 0, &opts));
    assert(arrivals[0] == 1 && events.back().buttons == A_FLAG);
    assert(events.back().lx == -32768 && events.back().lt == 255);
    size_t count = events.size();
    input_session_step(&s, pads, 1, 4, &opts);
    assert(events.size() == count); // unchanged input sends nothing
    memset(pads, 0, sizeof(pads));
    input_session_step(&s, pads, 0, 8, &opts);
    assert(events.size() == count + 1 && events.back().mask == 0);
    assert(events.back().buttons == 0 && events.back().lt == 0 && events.back().lx == 0);
    input_session_step(&s, pads, 0, 12, &opts);
    assert(events.size() == count + 1); // no repeated disconnect events
    pads[0].present = true;
    input_session_step(&s, pads, 1, 16, &opts);
    assert(arrivals[0] == 2); // identical neutral controller is reannounced
    pads[1].present = true;
    input_session_step(&s, pads, 3, 20, &opts);
    assert(arrivals[1] == 1 && events[events.size()-2].mask == 3);
    pads[1].present = false;
    input_session_step(&s, pads, 1, 24, &opts);
    assert(events[events.size()-2].port == 0 && events[events.size()-2].mask == 1);
    assert(events.back().port == 1 && events.back().buttons == 0);

    int combo = input_exit_mask(0);
    pads[0].buttons = combo;
    assert(!input_session_step(&s, pads, 1, 100, &opts));
    assert(events.back().buttons == 0); // full shortcut suppressed
    pads[0].buttons = combo;
    assert(!input_session_step(&s, pads, 1, 2099, &opts));
    pads[0].buttons = combo;
    assert(input_session_step(&s, pads, 1, 2100, &opts));
    pads[0].buttons = 0;
    input_session_step(&s, pads, 1, 2200, &opts);
    pads[0].buttons = combo;
    assert(!input_session_step(&s, pads, 1, 2300, &opts));
    pads[0].present = false;
    input_session_step(&s, pads, 0, 2400, &opts);
    pads[0].present = true; pads[0].buttons = combo;
    assert(!input_session_step(&s, pads, 1, 5000, &opts));
    pads[0].buttons = 0;
    input_session_step(&s, pads, 1, 5004, &opts);
    pads[0].buttons = combo;
    input_session_step(&s, pads, 1, UINT32_MAX - 999, &opts);
    pads[0].buttons = combo;
    assert(input_session_step(&s, pads, 1, 1000, &opts)); // timer wraps
    pads[0].buttons = LB_FLAG | RB_FLAG;
    pads[1].present = true; pads[1].buttons = LS_CLK_FLAG | RS_CLK_FLAG;
    assert(!input_session_step(&s, pads, 3, 10000, &opts));
    assert(!input_session_step(&s, pads, 3, 13000, &opts)); // cannot combine pads
    opts.quit_combo = 1;
    pads[0].buttons = input_exit_mask(1);
    assert(!input_session_step(&s, pads, 3, 14000, &opts));
    pads[0].buttons = input_exit_mask(1);
    assert(input_session_step(&s, pads, 3, 16000, &opts));
    input_session_release(&s);
    assert(events.back().mask == 0 && events.back().buttons == 0);

    for (int percent = 0; percent <= 30; percent++) {
        int previous = -32769;
        for (int value = -32768; value <= 32767; value++) {
            int filtered = input_axis((int16_t)value, percent);
            assert(filtered >= previous);
            if (!percent) assert(filtered == value);
            previous = filtered;
        }
        assert(input_axis(-32768, percent) == -32768);
        assert(input_axis(32767, percent) == 32767);
        assert(input_axis(0, percent) == 0);
        assert(input_axis((int16_t)(32767 * percent / 100), percent) == 0);
    }
    puts("PASS: arrival, unchanged input, disconnect, reconnect, mask changes, neutral release,");
    puts("hold/release/disconnect/wrap/multiple-pad/legacy shortcuts; 2031616 axis cases.");
    return 0;
}
