#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <vector>
#include "../../src/input_session.h"
#include "../../src/input_shortcut.h"
#include "../../src/input_guide.h"

struct sent_event {
    short port;
    short mask;
    int buttons;
    unsigned char lt, rt;
    short lx, ly, rx, ry;
};

static std::vector<sent_event> events;
static int arrivals[MAX_PADS];

int LiSendControllerArrivalEvent(uint8_t port, uint16_t capabilities, uint8_t, uint32_t supported, uint16_t)
{
    assert((capabilities & 0x01) != 0 || capabilities != 0);
    assert((supported & SPECIAL_FLAG) != 0);
    arrivals[port]++;
    return 0;
}

int LiSendMultiControllerEvent(short port, short mask, int buttons, unsigned char lt,
                               unsigned char rt, short lx, short ly, short rx, short ry)
{
    sent_event e = { port, mask, buttons, lt, rt, lx, ly, rx, ry };
    events.push_back(e);
    return 0;
}

static bool has_special(const sent_event &e) { return (e.buttons & SPECIAL_FLAG) != 0; }

int main(void)
{
    const int COMBO = PLAY_FLAG | LB_FLAG;
    struct input_guide_state g;
    struct input_session s;
    struct input_settings opts = {};
    struct pad pads[MAX_PADS] = {};

    /* 1. Temporal hold boundary tests: 100 / 250 / 349 / 350 / 500 ms + release. */
    input_guide_reset(&g);
    memset(&s, 0, sizeof(s));
    events.clear();
    pads[0].present = true;

    for (uint32_t t = 0; t <= 349; t += 1) {
        pads[0].buttons = COMBO;
        input_guide_step(&g, &pads[0].buttons, 0, COMBO, true, false, t, true, 350u);
        assert(!(pads[0].buttons & SPECIAL_FLAG));
        assert(!(pads[0].buttons & COMBO));
        input_session_step(&s, pads, 1, t, &opts);
    }
    assert(g.ports[0].phase == INPUT_GUIDE_CANDIDATE);
    for (size_t i = 0; i < events.size(); i++)
        assert(!has_special(events[i]) && !(events[i].buttons & COMBO));

    /* Exact 350 ms threshold -> SPECIAL_FLAG asserted without stick movement. */
    pads[0].buttons = COMBO;
    input_guide_step(&g, &pads[0].buttons, 0, COMBO, true, false, 350u, true, 350u);
    assert(g.ports[0].phase == INPUT_GUIDE_GUIDE_HELD && g.ports[0].guide_active);
    assert((pads[0].buttons & SPECIAL_FLAG) && !(pads[0].buttons & COMBO));
    input_session_step(&s, pads, 1, 350u, &opts);
    assert(!events.empty() && events.back().buttons == SPECIAL_FLAG);

    /* Hold to 500 ms -> stays active, no duplicate packets when state unchanged. */
    size_t count_before = events.size();
    for (uint32_t t = 354; t <= 500; t += 4) {
        pads[0].buttons = COMBO;
        input_guide_step(&g, &pads[0].buttons, 0, COMBO, true, false, t, true, 350u);
        assert((pads[0].buttons & SPECIAL_FLAG) && !(pads[0].buttons & COMBO));
        input_session_step(&s, pads, 1, t, &opts);
    }
    assert(events.size() == count_before);

    /* Release Start first while LB stays pressed -> immediate SPECIAL_FLAG release, LB suppressed. */
    pads[0].buttons = LB_FLAG;
    input_guide_step(&g, &pads[0].buttons, 0, LB_FLAG, true, false, 504u, true, 350u);
    assert(g.ports[0].phase == INPUT_GUIDE_SUPPRESS_UNTIL_RELEASE && !g.ports[0].guide_active);
    assert(pads[0].buttons == 0);
    input_session_step(&s, pads, 1, 504u, &opts);
    assert(events.size() == count_before + 1 && events.back().buttons == 0);

    /* Holding LB alone briefly stays suppressed until full release. */
    pads[0].buttons = LB_FLAG;
    input_guide_step(&g, &pads[0].buttons, 0, LB_FLAG, true, false, 520u, true, 350u);
    assert(pads[0].buttons == 0);
    pads[0].buttons = 0;
    input_guide_step(&g, &pads[0].buttons, 0, 0, true, false, 524u, true, 350u);
    assert(g.ports[0].phase == INPUT_GUIDE_IDLE);

    /* Now pressing standalone LB forwards immediately with 0 ms added latency. */
    pads[0].buttons = LB_FLAG;
    input_guide_step(&g, &pads[0].buttons, 0, LB_FLAG, true, false, 530u, true, 350u);
    assert(pads[0].buttons == LB_FLAG && g.ports[0].phase == INPUT_GUIDE_IDLE);
    pads[0].buttons = 0;
    input_guide_step(&g, &pads[0].buttons, 0, 0, true, false, 540u, true, 350u);
    input_session_step(&s, pads, 1, 540u, &opts);

    /* Re-hold Start+LB >= 350 ms, then release LB first -> immediate release & clean re-arm. */
    pads[0].buttons = COMBO;
    input_guide_step(&g, &pads[0].buttons, 0, COMBO, true, false, 600u, true, 350u);
    pads[0].buttons = COMBO;
    input_guide_step(&g, &pads[0].buttons, 0, COMBO, true, false, 950u, true, 350u);
    assert(pads[0].buttons == SPECIAL_FLAG);
    input_session_step(&s, pads, 1, 950u, &opts);
    assert(events.back().buttons == SPECIAL_FLAG);
    pads[0].buttons = PLAY_FLAG;
    input_guide_step(&g, &pads[0].buttons, 0, PLAY_FLAG, true, false, 954u, true, 350u);
    assert(pads[0].buttons == 0 && g.ports[0].phase == INPUT_GUIDE_SUPPRESS_UNTIL_RELEASE);
    input_session_step(&s, pads, 1, 954u, &opts);
    assert(events.back().buttons == 0);
    pads[0].buttons = 0;
    input_guide_step(&g, &pads[0].buttons, 0, 0, true, false, 960u, true, 350u);
    assert(g.ports[0].phase == INPUT_GUIDE_IDLE);

    /* 2. Quick taps (< 350 ms): 100 ms and 250 ms must NEVER emit SPECIAL_FLAG or Start/LB. */
    events.clear();
    pads[0].buttons = COMBO;
    input_guide_step(&g, &pads[0].buttons, 0, COMBO, true, false, 1000u, true, 350u);
    input_session_step(&s, pads, 1, 1000u, &opts);
    pads[0].buttons = COMBO;
    input_guide_step(&g, &pads[0].buttons, 0, COMBO, true, false, 1100u, true, 350u);
    input_session_step(&s, pads, 1, 1100u, &opts);
    pads[0].buttons = 0;
    input_guide_step(&g, &pads[0].buttons, 0, 0, true, false, 1250u, true, 350u);
    input_session_step(&s, pads, 1, 1250u, &opts);
    for (size_t i = 0; i < events.size(); i++)
        assert(events[i].buttons == 0);
    assert(g.ports[0].phase == INPUT_GUIDE_IDLE);

    /* Quick tap releasing only LB at 250 ms and holding Start until 400 ms must NEVER trigger Guide. */
    pads[0].buttons = COMBO;
    input_guide_step(&g, &pads[0].buttons, 0, COMBO, true, false, 2000u, true, 350u);
    pads[0].buttons = PLAY_FLAG;
    input_guide_step(&g, &pads[0].buttons, 0, PLAY_FLAG, true, false, 2250u, true, 350u);
    assert(pads[0].buttons == 0 && g.ports[0].phase == INPUT_GUIDE_SUPPRESS_UNTIL_RELEASE);
    pads[0].buttons = COMBO; /* re-pressing LB while Start remained suppressed must stay suppressed */
    input_guide_step(&g, &pads[0].buttons, 0, COMBO, true, false, 2600u, true, 350u);
    assert(pads[0].buttons == 0 && !(pads[0].buttons & SPECIAL_FLAG));
    pads[0].buttons = 0;
    input_guide_step(&g, &pads[0].buttons, 0, 0, true, false, 2610u, true, 350u);
    assert(g.ports[0].phase == INPUT_GUIDE_IDLE);

    /* 3. Standalone Start & LB zero-latency forwarding + sequential (> 80 ms) pass-through. */
    pads[0].buttons = PLAY_FLAG;
    input_guide_step(&g, &pads[0].buttons, 0, PLAY_FLAG, true, false, 3000u, true, 350u);
    assert(pads[0].buttons == PLAY_FLAG);
    input_session_step(&s, pads, 1, 3000u, &opts);
    assert(events.back().buttons == PLAY_FLAG);
    /* Wait 120 ms (> 80 ms simultaneity window) then press LB -> normal pass-through, no Guide. */
    pads[0].buttons = COMBO;
    input_guide_step(&g, &pads[0].buttons, 0, COMBO, true, false, 3120u, true, 350u);
    assert(g.ports[0].phase == INPUT_GUIDE_CANCELLED && pads[0].buttons == COMBO);
    pads[0].buttons = COMBO;
    input_guide_step(&g, &pads[0].buttons, 0, COMBO, true, false, 3600u, true, 350u);
    assert(pads[0].buttons == COMBO && !(pads[0].buttons & SPECIAL_FLAG));
    pads[0].buttons = 0;
    input_guide_step(&g, &pads[0].buttons, 0, 0, true, false, 3610u, true, 350u);
    input_session_step(&s, pads, 1, 3610u, &opts);
    assert(g.ports[0].phase == INPUT_GUIDE_IDLE);

    /* Near-simultaneous press (Start at t=4000, LB at t=4012 <= 80 ms) coherently releases Start and triggers Guide at 4362 ms. */
    events.clear();
    pads[0].buttons = PLAY_FLAG;
    input_guide_step(&g, &pads[0].buttons, 0, PLAY_FLAG, true, false, 4000u, true, 350u);
    assert(pads[0].buttons == PLAY_FLAG);
    input_session_step(&s, pads, 1, 4000u, &opts);
    assert(events.back().buttons == PLAY_FLAG);
    pads[0].buttons = COMBO;
    input_guide_step(&g, &pads[0].buttons, 0, COMBO, true, false, 4012u, true, 350u);
    assert(g.ports[0].phase == INPUT_GUIDE_CANDIDATE && pads[0].buttons == 0);
    input_session_step(&s, pads, 1, 4012u, &opts);
    assert(events.back().buttons == 0); /* coherent release of Start! */
    pads[0].buttons = COMBO;
    input_guide_step(&g, &pads[0].buttons, 0, COMBO, true, false, 4362u, true, 350u);
    assert(pads[0].buttons == SPECIAL_FLAG);
    pads[0].buttons = 0;
    input_guide_step(&g, &pads[0].buttons, 0, 0, true, false, 4370u, true, 350u);

    /* 4. Unrelated buttons/triggers/axes stay intact during CANDIDATE and GUIDE_HELD. */
    pads[0].buttons = COMBO | A_FLAG | X_FLAG;
    pads[0].lt = 200; pads[0].lx = -18000;
    input_guide_step(&g, &pads[0].buttons, 0, pads[0].buttons, true, false, 5000u, true, 350u);
    assert(pads[0].buttons == (A_FLAG | X_FLAG) && pads[0].lt == 200 && pads[0].lx == -18000);
    pads[0].buttons = COMBO | A_FLAG | X_FLAG;
    input_guide_step(&g, &pads[0].buttons, 0, pads[0].buttons, true, false, 5350u, true, 350u);
    assert(pads[0].buttons == (SPECIAL_FLAG | A_FLAG | X_FLAG));
    pads[0].buttons = A_FLAG; pads[0].lt = 0; pads[0].lx = 0;
    input_guide_step(&g, &pads[0].buttons, 0, pads[0].buttons, true, false, 5360u, true, 350u);
    assert(pads[0].buttons == A_FLAG && g.ports[0].phase == INPUT_GUIDE_IDLE);

    /* 5. Counter wraparound (UINT32_MAX - 100 -> 250) and jumped/irregular polling interval. */
    input_guide_reset(&g);
    uint32_t t_wrap = UINT32_MAX - 100u;
    pads[0].buttons = COMBO;
    input_guide_step(&g, &pads[0].buttons, 0, COMBO, true, false, t_wrap, true, 350u);
    assert(g.ports[0].phase == INPUT_GUIDE_CANDIDATE && !(pads[0].buttons & SPECIAL_FLAG));
    pads[0].buttons = COMBO;
    input_guide_step(&g, &pads[0].buttons, 0, COMBO, true, false, 248u, true, 350u);
    assert(g.ports[0].phase == INPUT_GUIDE_CANDIDATE && !(pads[0].buttons & SPECIAL_FLAG));
    pads[0].buttons = COMBO;
    input_guide_step(&g, &pads[0].buttons, 0, COMBO, true, false, 250u, true, 350u);
    assert(g.ports[0].phase == INPUT_GUIDE_GUIDE_HELD && (pads[0].buttons & SPECIAL_FLAG));
    pads[0].buttons = 0;
    input_guide_step(&g, &pads[0].buttons, 0, 0, true, false, 260u, true, 350u);

    /* 6. Higher-priority shortcut conflicts: Back+Start+LB+RB, Quick Menu, Desktop Mode, native Guide. */
    /* 6a. Holding Back+Start+LB(+RB) conflicts immediately and never triggers Guide even if Back released early. */
    pads[0].buttons = BACK_FLAG | COMBO | RB_FLAG;
    input_guide_step(&g, &pads[0].buttons, 0, pads[0].buttons, true, false, 6000u, true, 350u);
    assert(g.ports[0].phase == INPUT_GUIDE_SUPPRESS_UNTIL_RELEASE && !(pads[0].buttons & SPECIAL_FLAG));
    pads[0].buttons = COMBO; /* Back released while Start+LB stay held */
    input_guide_step(&g, &pads[0].buttons, 0, pads[0].buttons, true, false, 6500u, true, 350u);
    assert(g.ports[0].phase == INPUT_GUIDE_SUPPRESS_UNTIL_RELEASE && pads[0].buttons == 0);
    pads[0].buttons = 0;
    input_guide_step(&g, &pads[0].buttons, 0, 0, true, false, 6510u, true, 350u);
    assert(g.ports[0].phase == INPUT_GUIDE_IDLE);

    /* 6b. Blocked while GUIDE_HELD (native Guide / Quick Menu / Desktop Mode) clears SPECIAL_FLAG and suppresses until release. */
    pads[0].buttons = COMBO;
    input_guide_step(&g, &pads[0].buttons, 0, COMBO, true, false, 7000u, true, 350u);
    pads[0].buttons = COMBO;
    input_guide_step(&g, &pads[0].buttons, 0, COMBO, true, false, 7350u, true, 350u);
    assert(pads[0].buttons & SPECIAL_FLAG);
    pads[0].buttons = 0; /* native Guide zeroed pad */
    input_guide_step(&g, &pads[0].buttons, 0, COMBO, true, true, 7360u, true, 350u);
    assert(!(pads[0].buttons & SPECIAL_FLAG) && !g.ports[0].guide_active);
    assert(g.ports[0].phase == INPUT_GUIDE_SUPPRESS_UNTIL_RELEASE);
    /* Guide closes while Start+LB still physically held -> NO residual SPECIAL_FLAG. */
    pads[0].buttons = COMBO;
    input_guide_step(&g, &pads[0].buttons, 0, COMBO, true, false, 7800u, true, 350u);
    assert(pads[0].buttons == 0 && !(pads[0].buttons & SPECIAL_FLAG));
    pads[0].buttons = 0;
    input_guide_step(&g, &pads[0].buttons, 0, 0, true, false, 7810u, true, 350u);
    assert(g.ports[0].phase == INPUT_GUIDE_IDLE);

    /* 7. Hot-unplug mid-hold resets state and clears SPECIAL_FLAG cleanly. */
    pads[0].buttons = COMBO;
    input_guide_step(&g, &pads[0].buttons, 0, COMBO, true, false, 8000u, true, 350u);
    pads[0].buttons = COMBO;
    input_guide_step(&g, &pads[0].buttons, 0, COMBO, true, false, 8350u, true, 350u);
    assert(pads[0].buttons & SPECIAL_FLAG);
    /* Physically disconnected mid-hold (raw_present = false) */
    input_guide_step(&g, &pads[0].buttons, 0, COMBO, false, false, 8354u, true, 350u);
    assert(!(pads[0].buttons & SPECIAL_FLAG) && g.ports[0].phase == INPUT_GUIDE_IDLE);
    pads[0].present = false; pads[0].buttons = 0;
    input_session_step(&s, pads, 0, 8354u, &opts);
    assert(events.back().buttons == 0);

    /* 8. Multi-controller isolation: cross-pad Start + LB never triggers; independent ports work concurrently. */
    input_guide_reset(&g);
    pads[0].present = true; pads[1].present = true;
    pads[0].buttons = PLAY_FLAG; pads[1].buttons = LB_FLAG;
    input_guide_step(&g, &pads[0].buttons, 0, pads[0].buttons, true, false, 9000u, true, 350u);
    input_guide_step(&g, &pads[1].buttons, 1, pads[1].buttons, true, false, 9000u, true, 350u);
    input_guide_step(&g, &pads[0].buttons, 0, PLAY_FLAG, true, false, 9500u, true, 350u);
    input_guide_step(&g, &pads[1].buttons, 1, LB_FLAG, true, false, 9500u, true, 350u);
    assert(pads[0].buttons == PLAY_FLAG && pads[1].buttons == LB_FLAG);
    pads[0].buttons = 0; pads[1].buttons = 0;
    input_guide_step(&g, &pads[0].buttons, 0, 0, true, false, 9510u, true, 350u);
    input_guide_step(&g, &pads[1].buttons, 1, 0, true, false, 9510u, true, 350u);

    pads[0].buttons = COMBO; pads[1].buttons = A_FLAG;
    input_guide_step(&g, &pads[0].buttons, 0, pads[0].buttons, true, false, 10000u, true, 350u);
    input_guide_step(&g, &pads[1].buttons, 1, pads[1].buttons, true, false, 10000u, true, 350u);
    pads[0].buttons = COMBO; pads[1].buttons = COMBO;
    input_guide_step(&g, &pads[0].buttons, 0, COMBO, true, false, 10350u, true, 350u);
    input_guide_step(&g, &pads[1].buttons, 1, COMBO, true, false, 10350u, true, 350u);
    assert((pads[0].buttons & SPECIAL_FLAG) && !(pads[1].buttons & SPECIAL_FLAG));
    pads[0].buttons = COMBO; pads[1].buttons = COMBO;
    input_guide_step(&g, &pads[0].buttons, 0, COMBO, true, false, 10700u, true, 350u);
    input_guide_step(&g, &pads[1].buttons, 1, COMBO, true, false, 10700u, true, 350u);
    assert((pads[0].buttons & SPECIAL_FLAG) && (pads[1].buttons & SPECIAL_FLAG));

    /* 9. Feature disabled -> raw pass-through, no SPECIAL_FLAG; teardown zeroes everything. */
    pads[2].present = true; pads[2].buttons = COMBO;
    input_guide_step(&g, &pads[2].buttons, 2, COMBO, true, false, 11000u, false, 350u);
    input_guide_step(&g, &pads[2].buttons, 2, COMBO, true, false, 11500u, false, 350u);
    assert(pads[2].buttons == COMBO && g.ports[2].phase == INPUT_GUIDE_IDLE);

    input_session_release(&s);
    assert(events.back().mask == 0 && events.back().buttons == 0);

    puts("PASS: PC Guide Button (Start+LB 350ms, quick-tap suppression, coherent leading release, wraparound, conflicts, hotplug, 4-pad isolation, teardown).");
    return 0;
}
