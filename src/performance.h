#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#define PERF_SAMPLES 128
/* Each metric has one producer. Read/reset only after its worker is joined. */
struct perf_window {uint32_t samples[PERF_SAMPLES],count,filled,next,maximum;uint64_t total;};
static void perf_window_add(struct perf_window *w,uint64_t us)
{
    uint32_t value=us>0xFFFFFFFFu?0xFFFFFFFFu:(uint32_t)us;
    w->samples[w->next]=value;w->next=(w->next+1)%PERF_SAMPLES;
    if(w->filled<PERF_SAMPLES)w->filled++;
    w->count++;w->total+=value;if(value>w->maximum)w->maximum=value;
}
enum perf_metric {PERF_TCP,PERF_TLS,PERF_RTSP,PERF_NEGOTIATION,PERF_COVER_HTTP,PERF_COVER_DECODE,
    PERF_UI_BUILD,PERF_UI_UPLOAD,PERF_HOME_READ,PERF_INPUT_INTERVAL,PERF_INPUT_WORK,PERF_INPUT_SEND,
    PERF_AUDIO_CALLBACK,PERF_AUDIO_DECODE,PERF_AUDIO_GLITCHES,PERF_AUDIO_QUEUE_FULL,PERF_AUDIO_SUBMIT_FAIL,PERF_COUNT};
enum perf_event {PERF_CONNECT_BEGIN,PERF_LAUNCH_RESPONSE,PERF_CONNECTED,PERF_FIRST_PACKET,PERF_FIRST_DECODED,PERF_FIRST_PRESENTED,PERF_STOP_BEGIN,PERF_RETURN_MENU,PERF_EVENT_COUNT};
#ifdef __cplusplus
extern "C" {
#endif
#ifdef _XBOX
extern volatile int perf_active;
static inline bool perf_enabled(void){return perf_active!=0;}
uint64_t perf_clock(void);
void perf_begin(bool enabled);
void perf_record(int metric,uint64_t us);
void perf_mark(int event);
void perf_flush(void);
void perf_flush_menu(void);
void perf_ui_begin(void);
void perf_ui_end(void);
#else
static inline bool perf_enabled(void){return false;}
static inline uint64_t perf_clock(void){return 0;}
static inline void perf_begin(bool enabled){(void)enabled;}
static inline void perf_record(int metric,uint64_t us){(void)metric;(void)us;}
static inline void perf_mark(int event){(void)event;}
static inline void perf_flush(void){}
static inline void perf_flush_menu(void){}
static inline void perf_ui_begin(void){}
static inline void perf_ui_end(void){}
#endif
#ifdef __cplusplus
}
class PerfScope {
    int metric;uint64_t started;
public:
    explicit PerfScope(int value):metric(value),started(perf_enabled()?perf_clock():0){}
    ~PerfScope(){if(started)perf_record(metric,perf_clock()-started);}
};
#endif
