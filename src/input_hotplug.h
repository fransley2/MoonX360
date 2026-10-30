#pragma once
#include "input.h"
#include <string.h>
struct input_hotplug {struct pad last;uint32_t failed_since;bool failing;};
/* Hold a previous valid sample for 20 ms; an empty port has no debounce delay. */
static bool input_hotplug_sample(struct input_hotplug *h,struct pad *p,bool valid,uint32_t now) {
    if(valid){h->last=*p;h->failing=false;return true;}
    if(h->last.present) {
        if(!h->failing){h->failed_since=now;h->failing=true;}
        if((uint32_t)(now-h->failed_since)<20){*p=h->last;return true;}
    }
    memset(&h->last,0,sizeof(h->last));memset(p,0,sizeof(*p));return false;
}
