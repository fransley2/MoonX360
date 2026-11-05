#pragma once
#include "input_session.h"

/* Keep the configured four-button hold local. A brief missing sample does not
 * restart the two-second timer; a disconnect or a longer release does. */
struct input_shortcut { bool holding[MAX_PADS], interrupted[MAX_PADS], triggered[MAX_PADS]; uint32_t since[MAX_PADS], missing[MAX_PADS]; };
static bool input_shortcut_step(struct input_shortcut *s, const struct pad pads[MAX_PADS],
                                int combo, uint32_t now, bool consumed[MAX_PADS])
{
    bool fired=false;
    for(int i=0;i<MAX_PADS;i++) {
        consumed[i]=false;
        if(!pads[i].present){s->holding[i]=s->interrupted[i]=s->triggered[i]=false;continue;}
        if((pads[i].buttons&combo)==combo) {
            if(s->interrupted[i] && (uint32_t)(now-s->missing[i])>350)s->holding[i]=false;
            if(!s->holding[i]){s->holding[i]=true;s->triggered[i]=false;s->since[i]=now;}
            s->interrupted[i]=false;
            consumed[i]=true;
            if(!s->triggered[i] && (uint32_t)(now-s->since[i])>=2000){fired=true;s->triggered[i]=true;}
        } else if(s->holding[i]) {
            if(!s->interrupted[i]){s->interrupted[i]=true;s->missing[i]=now;}
            if((uint32_t)(now-s->missing[i])<=350)consumed[i]=true;
            else s->holding[i]=s->interrupted[i]=s->triggered[i]=false;
        }
    }
    return fired;
}
