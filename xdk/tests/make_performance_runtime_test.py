"""Compile the production aggregator with a deterministic clock and cold storage."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[2]
source=(root/'xdk/src/performance_xdk.cpp').read_text()
source=source.replace('#include "xdk.h"','').replace('#include "../../src/config.h"','')
source=source.replace('#include "../../src/performance.h"','#include "'+(root/'src/performance.h').as_posix()+'"')
source=source.replace('#include "../../src/timing.h"','#include "'+(root/'src/timing.h').as_posix()+'"')
fixture=r'''
#define _XBOX
#include <assert.h>
#include <stdint.h>
#include <string>
#include <map>
#include <stdio.h>
typedef long LONG;struct LARGE_INTEGER {long long QuadPart;};
static uint64_t tick;static unsigned clock_calls,writes;static std::string report,path;
static std::map<std::string,std::string> reports;
static void QueryPerformanceCounter(LARGE_INTEGER *out){clock_calls++;out->QuadPart=tick;}
static void QueryPerformanceFrequency(LARGE_INTEGER *out){out->QuadPart=1000000;}
static LONG InterlockedExchange(volatile LONG *p,LONG value){LONG old=*p;*p=value;return old;}
static LONG InterlockedCompareExchange(volatile LONG *p,LONG value,LONG expected){LONG old=*p;if(old==expected)*p=value;return old;}
static LONG InterlockedIncrement(volatile LONG *p){return ++*p;}
static LONG InterlockedExchangeAdd(volatile LONG *p,LONG n){LONG old=*p;*p+=n;return old;}
static uint32_t GetTickCount(){return (uint32_t)(tick/1000);}
static bool storage_write(const char *p,const void *data,size_t n){path=p;report.assign((const char*)data,n);reports[path]=report;writes++;return true;}
/* ACTUAL */
int main(){
    perf_begin(false);clock_calls=0;{PerfScope scope(PERF_UI_UPLOAD);}
    perf_pressure before,after;perf_pressure_read(&before);
    perf_count(PERF_C_VIDEO_FEC_FAILED,3);perf_count(PERF_C_VIDEO_QUEUE_OVERFLOW,1);perf_pressure_read(&after);
    assert(after.valid && after.missing-before.missing==3 && after.overflows-before.overflows==1);
    perf_record(PERF_AUDIO_DECODE,100);perf_mark(PERF_CONNECTED);perf_flush();
    assert(!clock_calls && !writes);
    tick=10;perf_begin(true);perf_mark(PERF_CONNECT_BEGIN);
    for(int i=1;i<=128;i++)perf_record(PERF_AUDIO_DECODE,i);
    tick=20;perf_mark(PERF_FIRST_PACKET);tick=30;perf_mark(PERF_FIRST_PACKET);
    tick=100;{PerfScope scope(PERF_INPUT_WORK);tick=125;}
    perf_record(PERF_AUDIO_GLITCHES,3);perf_record(PERF_AUDIO_QUEUE_FULL,1);
    perf_ui_begin();tick+=8;perf_ui_end();perf_flush();
    assert(path=="fps-request-path.txt" && writes==2);
    report=reports["performance-session.txt"];
    assert(report.find("opus_decode_avg_us=64.500")!=std::string::npos);
    assert(report.find("opus_decode_p95_us=122")!=std::string::npos);
    assert(report.find("opus_decode_p99_us=127")!=std::string::npos);
    assert(report.find("input_poll_work_avg_us=25.000")!=std::string::npos);
    assert(report.find("first_video_packet_us=10")!=std::string::npos);
    assert(report.find("first_presented_observed=0")!=std::string::npos);
    assert(report.find("audio_glitches_total=3")!=std::string::npos);
    assert(report.find("video_fec_packets_accepted=0")!=std::string::npos);
    perf_flush_menu();assert(path=="performance-menu.txt" && writes==3);
    tick=1000;perf_begin(true);perf_count(PERF_C_VIDEO_PACKETS,7);
    perf_capture_start(true);perf_count(PERF_C_VIDEO_PACKETS,13);
    tick=45000999;assert(!perf_capture_tick());
    tick=45001000;assert(perf_capture_tick() && !perf_capture_tick() && !perf_enabled());
    uint64_t counted=perf_counter_read(PERF_C_VIDEO_PACKETS);
    assert(perf_window_counter_read(PERF_C_VIDEO_PACKETS)==13 && perf_window_duration_us()==45000000);
    perf_video_packet_seen(42);assert(perf_video_tracking_enabled());
    video_progress_snapshot progress;video_progress_snapshot_get(&progress);
    assert(progress.count[VP_RX]==1 && progress.last_ms[VP_RX]==GetTickCount());
    video_progress_new_present();video_progress_new_present();
    video_progress_snapshot_get(&progress);assert(progress.frame_serial==2);
    assert(perf_last_video_packet_age_ms()==0 && perf_counter_read(PERF_C_VIDEO_PACKETS)==counted);
    perf_count(PERF_C_VIDEO_PACKETS,5);perf_flush();
    assert(perf_capture_duration_us()==45000000);
    assert(perf_last_counter_read(PERF_C_VIDEO_PACKETS)==13);
    tick=100;perf_begin(true);perf_capture_start(true);
    assert(perf_window_duration_us()==0 && perf_window_counter_read(PERF_C_VIDEO_PACKETS)==0);
    tick=50100;perf_count(PERF_C_VIDEO_PACKETS,2);perf_capture_close();
    assert(!perf_enabled());perf_flush();
    assert(perf_capture_duration_us()==50000 && perf_last_counter_read(PERF_C_VIDEO_PACKETS)==2);
    tick=1000000;perf_begin(true);
    video_progress_snapshot_get(&progress);assert(progress.frame_serial==0);
    assert(perf_last_video_packet_age_ms()==UINT32_MAX);
    perf_video_packet_seen(100);tick+=25000;
    assert(perf_counter_read(PERF_C_VIDEO_PACKETS)==1 && perf_counter_read(PERF_C_VIDEO_BYTES)==100);
    assert(perf_last_video_packet_age_ms()==25);
    for(int i=0;i<63;i++)perf_video_packet_seen(10);
    assert(perf_last_video_packet_age_ms()==0);
    tick+=100000;assert(perf_last_video_packet_age_ms()==100);
    tick=0xffffffffffffull;assert(perf_clock()==tick);
    perf_begin(true);perf_flush();report=reports["performance-session.txt"];
    assert(report.find("opus_decode_count=0")!=std::string::npos);
    puts("PASS: production profiling OFF zero clock/I-O, scopes, single-shot events, exact percentiles, counters, reset and long uptime clock.");
}
'''
Path(sys.argv[1]).write_text(fixture.replace('/* ACTUAL */',source))
