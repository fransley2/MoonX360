#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../src/library_state.h"
int main() {
    performance_sample s={};s.budget_us=16666;s.frames=600;s.target_fps=60;s.shown_fps=59.5;
    s.decode_avg_us=11000;s.decode_p99_us=12500;s.callback_p99_us=14000;s.tail_valid=true;
    s.rtt_valid=true;s.rtt_ms=9;
    assert(performance_assess(&s).grade==PERF_EXCELLENT);
    s.shown_fps=49;assert(performance_assess(&s).grade==PERF_GOOD);
    assert(performance_assess(&s).cause==PERF_CAUSE_UNKNOWN); // low cadence alone is not a diagnosis
    s.decode_avg_us=33000;s.decode_p99_us=41000;s.queue_peak_us=363000;
    assert(performance_assess(&s).cause==PERF_CPU && performance_assess(&s).grade==PERF_UNSTABLE);
    s.decode_avg_us=11000;s.decode_p99_us=12000;s.queue_peak_us=0;s.starvation=6;s.drops=8;
    assert(performance_assess(&s).cause==PERF_GPU);
    s.starvation=0;s.drops=0;s.failovers=1;s.shown_fps=59;
    assert(performance_assess(&s).grade==PERF_EXCELLENT); // one safe fallback is not a GPU failure
    s.failovers=3;s.drops=8;assert(performance_assess(&s).cause==PERF_GPU);
    s.failovers=0;s.drops=0;s.queue_us=16000;s.callback_p99_us=18000;
    assert(performance_assess(&s).grade==PERF_UNSTABLE && performance_assess(&s).cause==PERF_CAUSE_UNKNOWN);
    s.queue_us=0;s.callback_p99_us=14000;
    s.shown_fps=40;s.frame_gaps=4;s.gap_p99_us=60000;
    assert(performance_assess(&s).cause==PERF_PACING);
    s.frame_gaps=0;s.gap_p99_us=0;s.tail_valid=false;s.rtt_valid=false;
    assert(performance_assess(&s).grade==PERF_GOOD);
    s.queue_peak_us=200000;assert(performance_assess(&s).grade==PERF_UNSTABLE);
    assert(performance_assess(&s).cause==PERF_CAUSE_UNKNOWN);
    s.frames=2;assert(performance_assess(&s).grade==PERF_UNKNOWN);
    session_history h={};session_record r={};
    strcpy(r.host,"192.168.1.4");strcpy(r.app,"Game\tname\nwith line");r.width=1280;r.height=720;
    r.fps=60;r.bitrate=30000;r.grade=PERF_GOOD;r.cause=PERF_CAUSE_UNKNOWN;r.shown_milli=59000;
    for(int i=1;i<=40;i++) {r.app_id=i;r.timestamp=1000+i;history_push(&h,&r);}
    assert(h.count==SESSION_HISTORY_MAX && h.records[0].app_id==40 && h.records[SESSION_HISTORY_MAX-1].app_id==9);
    assert(!strchr(h.records[0].app,'\t') && !strchr(h.records[0].app,'\n'));
    char text[SESSION_HISTORY_BYTES];size_t len=history_encode(&h,text,sizeof(text));assert(len && len<sizeof(text));
    session_history loaded={};history_decode(&loaded,text);
    assert(loaded.count==SESSION_HISTORY_MAX && loaded.records[0].app_id==40 && loaded.records[SESSION_HISTORY_MAX-1].timestamp==1009);
    assert(!history_encode(&h,text,8));
    history_decode(&loaded,"bad header\n");assert(!loaded.count);
    history_decode(&loaded,"XENON_SESSIONS 1\ntruncated\n");assert(!loaded.count);
    history_push(&h,h.records+3);assert(h.records[0].app_id==37); // alias-safe reorder
    library_favorites f={};assert(library_favorite_toggle(&f,"PC1",42));
    assert(library_favorite_has(&f,"PC1",42) && !library_favorite_has(&f,"PC2",42));
    assert(library_favorite_toggle(&f,"PC1",42) && !f.count);
    for(int i=1;i<=32;i++)assert(library_favorite_toggle(&f,"PC1",i));
    assert(!library_favorite_toggle(&f,"PC1",33));
    assert(library_favorites_encode(&f,text,sizeof(text)));
    library_favorites restored={};library_favorites_decode(&restored,text);
    assert(restored.count==32 && library_favorite_has(&restored,"PC1",32));
    assert(library_favorite_toggle(&restored,"PC1",10) && restored.count==31);
    library_favorites_decode(&restored,"XENON_FAVORITES 1\nmalformed\nPC1\t42\n");
    assert(restored.count==1 && library_favorite_has(&restored,"PC1",42));
    assert(library_desktop("Desktop") && library_desktop("DESKTOP") && !library_desktop("Desktop Game"));
    puts("PASS: conservative Advisor; unknown evidence; no false network/GPU cause; bounded sanitized history roundtrip; host-scoped favorites and limits.");
}
