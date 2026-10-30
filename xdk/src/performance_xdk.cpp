#include "xdk.h"
#include "../../src/performance.h"
#include "../../src/config.h"
#include <stdio.h>
#include <stdlib.h>
volatile int perf_active;
struct __declspec(align(128)) perf_slot {struct perf_window window;};
static struct perf_slot slots[PERF_COUNT];
static uint64_t marks[PERF_EVENT_COUNT],frequency,origin,ui_started;
uint64_t perf_clock(void)
{
    LARGE_INTEGER now;QueryPerformanceCounter(&now);
    uint64_t ticks=(uint64_t)now.QuadPart;
    return frequency?(ticks/frequency)*1000000+(ticks%frequency)*1000000/frequency:0;
}
void perf_begin(bool enabled)
{
    /* Called before connection workers start; no resetting a live producer. */
    InterlockedExchange((volatile LONG*)&perf_active,0);
    memset(slots,0,sizeof(slots));memset(marks,0,sizeof(marks));ui_started=0;
    LARGE_INTEGER value;QueryPerformanceFrequency(&value);frequency=(uint64_t)value.QuadPart;
    origin=enabled?perf_clock():0;
    InterlockedExchange((volatile LONG*)&perf_active,enabled?1:0);
}
void perf_record(int metric,uint64_t us)
{if(perf_enabled() && metric>=0 && metric<PERF_COUNT)perf_window_add(&slots[metric].window,us);}
void perf_mark(int event)
{if(perf_enabled() && event>=0 && event<PERF_EVENT_COUNT && !marks[event])marks[event]=perf_clock()-origin+1;}
void perf_ui_begin(void){if(perf_enabled() && !ui_started)ui_started=perf_clock();}
void perf_ui_end(void){if(ui_started){perf_record(PERF_UI_BUILD,perf_clock()-ui_started);ui_started=0;}}
static int compare(const void *a,const void *b)
{uint32_t x=*(const uint32_t*)a,y=*(const uint32_t*)b;return x<y?-1:x>y;}
static void flush_report(const char *path)
{
    /* All network/audio/input/video producers are stopped; sorting/I/O are cold. */
    if(!perf_enabled())return;
    static const char *names[]={"tcp_setup","tls_setup","rtsp_setup","moonlight_negotiation","cover_http","cover_decode",
        "ui_screen_build","ui_texture_upload","home_cache_read","input_interval","input_poll_work","input_send_enqueue",
        "audio_callback","opus_decode","audio_glitches","audio_queue_full","audio_submit_failure"};
    static const char *events[]={"connect_start","app_launch_response","moonlight_connected","first_video_packet","first_decoded","first_presented","stop_start","return_menu"};
    char report[8192];int n=snprintf(report,sizeof(report),"schema=1\nscope=single-producer CPU wall time; p95/p99 rolling 128; no per-frame logs\n");
    for(int i=0;i<PERF_EVENT_COUNT && n>0 && (size_t)n<sizeof(report);i++)
        n+=snprintf(report+n,sizeof(report)-n,"%s_observed=%d\n%s_us=%I64u\n",events[i],marks[i]!=0,events[i],(unsigned long long)(marks[i]?marks[i]-1:0));
    for(int i=0;i<PERF_COUNT && n>0 && (size_t)n<sizeof(report);i++) {
        const perf_window *w=&slots[i].window;uint32_t sorted[PERF_SAMPLES];memcpy(sorted,w->samples,w->filled*sizeof(uint32_t));
        if(i>=PERF_AUDIO_GLITCHES){n+=snprintf(report+n,sizeof(report)-n,"%s_observations=%u\n%s_total=%I64u\n",names[i],w->count,names[i],(unsigned long long)w->total);continue;}
        qsort(sorted,w->filled,sizeof(uint32_t),compare);
        n+=snprintf(report+n,sizeof(report)-n,"%s_count=%u\n%s_avg_us=%.3f\n%s_p95_us=%u\n%s_p99_us=%u\n%s_max_us=%u\n",
            names[i],w->count,names[i],w->count?(double)w->total/w->count:0.0,
            names[i],w->filled?sorted[(w->filled*95+99)/100-1]:0,names[i],w->filled?sorted[(w->filled*99+99)/100-1]:0,names[i],w->maximum);
    }
    if(n>0 && (size_t)n<sizeof(report)){
        n+=snprintf(report+n,sizeof(report)-n,"network_rx_cpu=unavailable; existing callback_gap includes waiting/scheduling\ndecrypt_depacketize_cpu=requires_packet_sampler\ndecode_subfunctions=requires_PIX_CPU_sampling\n");
        if(n>0 && (size_t)n<sizeof(report))storage_write(path,report,(size_t)n);
    }
}
void perf_flush(void){flush_report("performance-session.txt");}
void perf_flush_menu(void){flush_report("performance-menu.txt");}
