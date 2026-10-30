#pragma once
#include <stdbool.h>
/* Notification transitions are idempotent; no reconnect/decoder lifecycle here. */
struct system_ui_state {bool active;unsigned opens,resumes;};
static bool system_ui_change(struct system_ui_state *s,bool active) {
    if(s->active==active)return false;
    s->active=active;if(active)s->opens++;else s->resumes++;return true;
}
