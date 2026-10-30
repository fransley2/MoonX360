"""Compile the actual callback timing scope with a deterministic host clock.
Times here validate accounting only, never Xbox performance.
"""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
source = (root / 'xdk/src/video_xdk.cpp').read_text()
begin = source.index('class submit_timing_scope {')
end = source.index('\n};', begin) + 3
scope = source[begin:end]
fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
enum { TIME_CALLBACK_GAP, TIME_CALLBACK_TOTAL, TIME_COUNT };
static struct { bool stats_overlay; } options;
static uint64_t clock_us, submit_last_end_us, values[TIME_COUNT];
static unsigned calls[TIME_COUNT], clocks;
static uint64_t now_us() { clocks++; return clock_us; }
static void pipeline_sample(int metric, uint64_t us) {
    calls[metric]++; values[metric]=us; clock_us+=5; // aggregation cost
}
/* ACTUAL_SCOPE */
static void reset() {
    submit_last_end_us=0; clocks=0;
    for(int i=0;i<TIME_COUNT;i++) { calls[i]=0; values[i]=0; }
}
static int callback(bool error) {
    submit_timing_scope timing;
    clock_us+=20;
    if(error) return -1;
    clock_us+=30;
    return 0;
}
int main() {
    reset(); options.stats_overlay=false; clock_us=100;
    assert(callback(false)==0 && callback(true)==-1);
    assert(clocks==0 && !calls[TIME_CALLBACK_TOTAL] && !calls[TIME_CALLBACK_GAP]);
    assert(submit_last_end_us==0); // OFF adds no timing/aggregation calls
    reset(); options.stats_overlay=true; clock_us=100;
    assert(callback(false)==0 && values[TIME_CALLBACK_TOTAL]==50);
    assert(calls[TIME_CALLBACK_TOTAL]==1 && !calls[TIME_CALLBACK_GAP]);
    assert(submit_last_end_us==155); // excludes trailing aggregation from gap
    clock_us=200;
    assert(callback(true)==-1 && values[TIME_CALLBACK_GAP]==45);
    assert(calls[TIME_CALLBACK_TOTAL]==2 && values[TIME_CALLBACK_TOTAL]==25);
    assert(submit_last_end_us==230);
    reset(); clock_us=1000;
    assert(callback(false)==0 && !calls[TIME_CALLBACK_GAP]); // next session
    reset(); submit_last_end_us=200; clock_us=100;
    callback(false);assert(!calls[TIME_CALLBACK_GAP]); // invalid backwards gap
    reset(); clock_us=100;
    { submit_timing_scope timing; clock_us=90; }
    assert(!calls[TIME_CALLBACK_TOTAL]); // no unsigned wrap on backwards clock
    puts("PASS: actual callback scope: success/error returns, reset, clock validation, OFF without clocks, outside-gap excludes trailing aggregation.");
}
'''
Path(sys.argv[1]).write_text(fixture.replace('/* ACTUAL_SCOPE */', scope))
