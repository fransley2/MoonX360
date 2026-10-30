"""Exercise the real offline benchmark policy using a synthetic decoder/clock.
These timings test statistics only and are not performance measurements.
"""
from pathlib import Path
import sys
tests = Path(__file__).resolve().parent
source = (tests.parent / 'src/video_xdk.cpp').read_text()
out = r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <stdio.h>
#include "../xdk/ffmpeg_xbox/xbox_dsp.h"
int ff_xbox_vmx_flags;
unsigned ff_xbox_loopfilter_mask;
#define DECODE_THREADS 4
#define FF_THREAD_SLICE 2
#define CODEC_FLAG_LOW_DELAY 1
#define CODEC_FLAG2_FAST 1
#define AV_PIX_FMT_YUV420P 0
#define AVDISCARD_ALL 48
#define FF_INPUT_BUFFER_PADDING_SIZE 32
struct AVCodecContext { int flags, flags2, thread_type, thread_count, width, height, pix_fmt, skip_loop_filter; };
struct AVFrame { uint8_t *data[3]; int linesize[3]; };
struct AVPacket { uint8_t *data; int size; };
static int ff_h264_decoder, call_number, opened_threads;
static uint64_t timer;
static bool inject_error, corrupt_loop, missing_output;
#if XDK_SLICE_DISPATCH_BATCHED
static int dispatch_version=2;
extern "C" int ff_xbox_slice_dispatch_version(void){return dispatch_version;}
#endif
static AVCodecContext *avcodec_alloc_context3(int*) { return (AVCodecContext*)calloc(1,sizeof(AVCodecContext)); }
static int avcodec_open2(AVCodecContext *c,int*,void*) { call_number=0;opened_threads=c->thread_count;return 0; }
static void avcodec_close(AVCodecContext*) {}
static void av_free(void *p) { free(p); }
static AVFrame *avcodec_alloc_frame() { return (AVFrame*)calloc(1,sizeof(AVFrame)); }
static void avcodec_free_frame(AVFrame **p) { free(*p); *p=0; }
static void av_init_packet(AVPacket *p) { memset(p,0,sizeof(*p)); }
static uint64_t now_us() { return timer; }
static void vlog(const char*,...) {}
static void plat_shutdown_stage(const char*) {}
static int avcodec_decode_video2(AVCodecContext *c, AVFrame *f, int *got, AVPacket *pkt) {
    static uint8_t pixels[24];
    call_number++; timer+=call_number*1000;
    memset(pixels,c->skip_loop_filter==AVDISCARD_ALL ? 99 : 42,sizeof(pixels));
    if(corrupt_loop && (ff_xbox_vmx_flags & XVMX_LOOPFILTER)) pixels[0]++;
    f->data[0]=pixels; f->data[1]=pixels+16; f->data[2]=pixels+20;
    f->linesize[0]=4; f->linesize[1]=f->linesize[2]=2;
    *got=!missing_output;
    return inject_error ? -1 : pkt->size;
}
'''
out += source[source.index('static AVCodecContext *open_h264('):source.index('/* ---------------------------------------------------- render thread */')]
out += source[source.index('static const struct {', source.index('/* ---------------------------------------------------- benchmark */')):
              source.index('static DWORD WINAPI bench_main(')]
out += r'''
int main() {
#if XDK_SLICE_DISPATCH_BATCHED
    dispatch_version=0;assert(open_h264(4,4,4,false)==NULL);dispatch_version=2;
#endif
    bench b={};
    uint8_t data[516]={}; uint32_t hashes[101]={};
    b.data=data; b.size=sizeof(data); b.frames=100; b.w=b.h=4; b.ref_hash=hashes;
    for(int i=0;i<100;i++) { uint32_t length=1; memcpy(data+16+i*5,&length,4); }
    for(int run=0;run<(int)BENCH_RUNS;run++) {
        bench_run(&b,run);
        assert(!b.failed[run] && !b.mismatches[run] && b.decoded[run]==100);
        assert(opened_threads==bench_configs[run].threads);
        assert(b.p50[run]==50);
        assert(b.ms[run]==50.5 && b.worst_ms[run]==100 && b.p95[run]==95 && b.p99[run]==99);
    }
    assert(bench_best_threads(&b,true)==4 && bench_best_threads(&b,false)==4);
    for(int run=0;run<(int)BENCH_RUNS;run++) if(bench_configs[run].thread_matrix) {
        int t=bench_configs[run].threads;
        b.ms[run]=t==5?1.0:20.0; // best average is deliberately NOT best p99
        b.p99[run]=t==3?10.0:t==4?12.0:30.0;
    }
    assert(bench_best_threads(&b,true)==3 && bench_best_threads(&b,false)==3);
    assert(DECODE_THREADS==4); // diagnostic result never modifies streaming
    bench_run(&b,0); corrupt_loop=true; bench_run(&b,4);
    assert(b.mismatches[4]==100); corrupt_loop=false;
    inject_error=true; bench_run(&b,0); assert(b.failed[0] && !b.reference_valid);
    inject_error=false; bench_run(&b,1); assert(b.failed[1]);
    missing_output=true; bench_run(&b,0); assert(b.failed[0] && !b.reference_valid);
    puts("PASS: actual benchmark FULL/FAST reference isolation, 3/4/5 threads, p99 selection, synthetic mean/p50/max/p95/p99, hashes, decode errors, missing output");
}
'''
Path(sys.argv[1]).write_text(out, encoding='utf-8', newline='\n')
