#pragma once
#include <stdint.h>

/* QPC ticks to microseconds without overflowing on long console uptime. */
static uint64_t moonx_ticks_to_us(uint64_t ticks,uint64_t frequency)
{
    if(!frequency)return 0;
    return (ticks/frequency)*1000000ULL+(ticks%frequency)*1000000ULL/frequency;
}
