"""Compile the production aggregator with a deterministic clock and cold storage."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[2]
source=(root/'xdk/src/performance_xdk.cpp').read_text()
source=source.replace('#include "xdk.h"','').replace('#include "../../src/config.h"','')
source=source.replace('#include "../../src/performance.h"','#include "'+(root/'src/performance.h').as_posix()+'"')
fixture=r'''
#define _XBOX
#include <assert.h>
#include <stdint.h>
#include <string>
#include <stdio.h>
typedef long LONG;struct LARGE_INTEGER {long long QuadPart;};
static uint64_t tick;static unsigned clock_calls,writes;static std::string report,path;
static void QueryPerformanceCounter(LARGE_INTEGER *out){clock_calls++;out->QuadPart=tick;}
static void QueryPerformanceFrequency(LARGE_INTEGER *out){out->QuadPart=1000000;}
static LONG InterlockedExchange(volatile LONG *p,LONG value){LONG old=*p;*p=value;return old;}
static bool storage_write(const char *p,const void *data,size_t n){path=p;report.assign((const char*)data,n);writes++;return true;}
/* ACTUAL */
int main(){
    perf_begin(false);clock_calls=0;{PerfScope scope(PERF_UI_UPLOAD);}
    perf_record(PERF_AUDIO_DECODE,100);perf_mark(PERF_CONNECTED);perf_flush();
    assert(!clock_calls && !writes);
    tick=10;perf_begin(true);perf_mark(PERF_CONNECT_BEGIN);
    for(int i=1;i<=128;i++)perf_record(PERF_AUDIO_DECODE,i);
    tick=20;perf_mark(PERF_FIRST_PACKET);tick=30;perf_mark(PERF_FIRST_PACKET);
    tick=100;{PerfScope scope(PERF_INPUT_WORK);tick=125;}
    perf_record(PERF_AUDIO_GLITCHES,3);perf_record(PERF_AUDIO_QUEUE_FULL,1);
    perf_ui_begin();tick+=8;perf_ui_end();perf_flush();
    assert(path=="performance-session.txt" && writes==1);
    assert(report.find("opus_decode_avg_us=64.500")!=std::string::npos);
    assert(report.find("opus_decode_p95_us=122")!=std::string::npos);
    assert(report.find("opus_decode_p99_us=127")!=std::string::npos);
    assert(report.find("input_poll_work_avg_us=25.000")!=std::string::npos);
    assert(report.find("first_video_packet_us=10")!=std::string::npos);
    assert(report.find("first_presented_observed=0")!=std::string::npos);
    assert(report.find("audio_glitches_total=3")!=std::string::npos);
    perf_flush_menu();assert(path=="performance-menu.txt" && writes==2);
    tick=0xffffffffffffull;assert(perf_clock()==tick);
    perf_begin(true);perf_flush();assert(report.find("opus_decode_count=0")!=std::string::npos);
    puts("PASS: production profiling OFF zero clock/I-O, scopes, single-shot events, exact percentiles, counters, reset and long uptime clock.");
}
'''
Path(sys.argv[1]).write_text(fixture.replace('/* ACTUAL */',source))
