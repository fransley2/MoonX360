/* Offline correctness and timing of actual FFmpeg C / Moon VMX / adapted VMX.
 * No streaming dispatch modification, allocation, lock or disk I/O per block.
 * All sample timings are batch means, not single-block latency percentiles. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vmx_ab.h"

#define BYTES (128*48)
#define ROUTINES 320
#define SAMPLES 31
#define BATCH 64
#define WARMUP 32
static __declspec(align(128)) uint8_t source[BYTES],source_before[BYTES];
static __declspec(align(128)) uint8_t destination[3][BYTES],initial[BYTES];
static struct vmx_ab_tables tables[3];
static uint8_t valid[3][ROUTINES];
static unsigned seed;
static const int strides[4]={64,80,96,128};
static const char *names[3]={"C Reference","MoonX360 VMX","Multiplex360 VMX Adapted"};

struct operation {int kind,size,avg,pos,height,id;};
static unsigned random_byte(void) {seed=seed*1664525u+1013904223u;return seed>>24;}
static int cancelled(const struct vmx_ab_callbacks *c) {return c->cancel && c->cancel(c->context);}
static int emit(const struct vmx_ab_callbacks *c,const char *s) {return c->emit(c->context,s);}

static void invoke(int variant,const struct operation *o,uint8_t *dst,uint8_t *src,int stride)
{
    if(o->kind)tables[variant].chroma[o->size][o->avg](dst,src,stride,o->height,o->pos&7,o->pos>>3);
    else tables[variant].qpel[o->size][o->avg][o->pos](dst,src,stride);
}

static void pattern(int mode,int stride,const uint8_t *reference)
{
    int i;
    for(i=0;i<BYTES;i++) {
        int x=i%stride,y=i/stride;
        source[i]=(uint8_t)(mode==0?0:mode==1?255:mode==2?((x+y)&1)*255:
            mode==3?(x&1)*255:mode==4?(y&1)*255:
            mode==5?(x*19+y*31)&255:mode==6 && reference?reference[(y%48)*128+x%128]:random_byte());
        initial[i]=(uint8_t)random_byte();
    }
    memcpy(source_before,source,BYTES);
}

static int outside_unchanged(const uint8_t *buffer,int offset,int stride,int width,int height)
{
    int i;
    for(i=0;i<BYTES;i++) {
        int relative=i-offset;
        int inside=relative>=0 && relative/stride<height && relative%stride<width;
        if(!inside && buffer[i]!=initial[i])return 0;
    }
    return 1;
}

static int verify(const struct vmx_ab_callbacks *c,const struct operation *o,unsigned *cases,unsigned *failures)
{
    int si,a,edge,v,repeat,width=o->kind?(o->size?4:8):(o->size?8:16);
    for(si=0;si<4;si++)for(a=0;a<16;a++)for(edge=0;edge<2;edge++) {
        int stride=strides[si];
        int soff=edge?BYTES-(o->height+4)*stride-48+a:2*stride+16+a;
        int alignment=c->strict_destination_alignment?(a&1)*(o->kind?8:width):((a*7)&15);
        int doff=2*stride+32+alignment;
        int mode=(a+si+edge)%8;
        if(cancelled(c))return 2;
        pattern(mode,stride,c->reference);
        for(v=0;v<3;v++)memcpy(destination[v],initial,BYTES);
        for(repeat=0;repeat<2;repeat++) {
            for(v=0;v<3;v++)invoke(v,o,destination[v]+doff,source+soff,stride);
            for(v=0;v<3;v++) {
                int ok=!memcmp(source,source_before,BYTES) &&
                    outside_unchanged(destination[v],doff,stride,width,o->height) &&
                    !memcmp(destination[0],destination[v],BYTES);
                if(!ok) {
                    if(valid[v][o->id]) {
                        char line[192];
                        _snprintf(line,sizeof(line),"# mismatch,variant=%d,id=%d,stride=%d,src_align=%d,dst_align=%d,height=%d,pattern=%d,edge=%d,repeat=%d\n",
                            v,o->id,stride,a,alignment,o->height,mode,edge,repeat);
                        line[sizeof(line)-1]=0;
                        if(!emit(c,line))return -1;
                    }
                    valid[v][o->id]=0;(*failures)++;
                }
            }
        }
        (*cases)++;
    }
    return 0;
}

static __declspec(noinline) void empty_operation(int v,const struct operation *o,uint8_t *d,uint8_t *s,int stride)
{
    /* Actual noinline indirect-call floor; volatile prevents removal. */
    static volatile int sink;
    sink=v+o->id+stride+(int)((uintptr_t)d-(uintptr_t)s);
}

static int ticks_compare(const void *a,const void *b)
{
    uint64_t x=*(const uint64_t*)a,y=*(const uint64_t*)b;
    return x<y?-1:x>y?1:0;
}

static int measure(const struct vmx_ab_callbacks *c,const struct operation *o)
{
    int si,alignment,order,sample,i;
    for(si=0;si<4;si++)for(alignment=0;alignment<2;alignment++) {
        int stride=strides[si],width=o->kind?(o->size?4:8):(o->size?8:16);
        int doff=2*stride+32+(alignment?(o->kind?8:width):0);
        int soff=2*stride+32+(alignment?7:0);
        pattern(si==3 && c->reference?6:7,stride,c->reference);
        for(order=0;order<3;order++) {
            int v=(order+o->id)%3;
            uint64_t raw[SAMPLES],net[SAMPLES],empty[SAMPLES],sum=0,empty_sum=0;
            char line[320];
            if(cancelled(c))return 2;
            if(!valid[v][o->id])continue;
            memcpy(destination[v],initial,BYTES);
            for(i=0;i<WARMUP;i++)invoke(v,o,destination[v]+doff,source+soff,stride);
            for(sample=0;sample<SAMPLES;sample++) {
                uint64_t start=c->clock(c->context),end,elapsed;
                for(i=0;i<BATCH;i++)empty_operation(v,o,destination[v]+doff,source+soff,stride);
                end=c->clock(c->context);
                if(end<start)return -1;
                empty[sample]=end-start;
                start=c->clock(c->context);
                for(i=0;i<BATCH;i++)invoke(v,o,destination[v]+doff,source+soff,stride);
                end=c->clock(c->context);
                if(end<start)return -1;
                elapsed=end-start;raw[sample]=elapsed;
                net[sample]=elapsed>empty[sample]?elapsed-empty[sample]:0;
                sum+=elapsed;empty_sum+=empty[sample];
            }
            qsort(raw,SAMPLES,sizeof(raw[0]),ticks_compare);
            qsort(net,SAMPLES,sizeof(net[0]),ticks_compare);
            _snprintf(line,sizeof(line),"TIME,%d,%s,%d,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n",
                o->id,names[v],v,o->kind?"chroma":"qpel",width,o->avg,o->pos,o->height,stride,alignment?7:0,
                doff&15,SAMPLES,BATCH,
                (double)sum*1e9/c->frequency/BATCH/SAMPLES,
                (double)raw[15]*1e9/c->frequency/BATCH,(double)raw[29]*1e9/c->frequency/BATCH,
                (double)raw[30]*1e9/c->frequency/BATCH,(double)empty_sum*1e9/c->frequency/BATCH/SAMPLES,
                (double)net[15]*1e9/c->frequency/BATCH,(double)net[29]*1e9/c->frequency/BATCH,
                (double)net[30]*1e9/c->frequency/BATCH);
            line[sizeof(line)-1]=0;
            if(!emit(c,line))return -1;
        }
    }
    return 0;
}

int ff_xbox_vmx_ab_run(const struct vmx_ab_callbacks *c)
{
    int kind,size,avg,pos,h,phase,v,ret;
    unsigned cases=0,failures=0;
    char line[256];
    if(!c || !c->emit || (!c->integrity_only && (!c->clock || !c->frequency)))return -1;
    seed=0x360128;
    memset(tables,0,sizeof(tables));memset(valid,1,sizeof(valid));
    ff_xbox_vmx_ab_tables(tables,tables+1);
    ff_multiplex_qpel_tables(tables+2);ff_multiplex_chroma_tables(tables+2);
    _snprintf(line,sizeof(line),"# schema=1,reference=FFmpeg_1.2_C,real_reference=%s,strict_dst_alignment=%d,frequency=%.0f\n",
        c->reference?"decoded_capture_tile":"unavailable",c->strict_destination_alignment,(double)c->frequency);
    line[sizeof(line)-1]=0;if(!emit(c,line))return -1;
    if(!emit(c,"# integrity=full_destination_and_source;2_repetitions;4_strides;16_src_alignments;2_edges;8_patterns;chroma_heights_2_4_8\n"))return -1;
    for(phase=0;phase<(c->integrity_only?1:2);phase++) {
        if(phase && !emit(c,"# percentiles=31_batch_means_of_64_ops;warmup=32;raw_is_primary;net_subtracts_noinline_floor;CPU_QPC_not_GPU_or_scanout;stride128_uses_real_tile_when_available;other_timing_inputs=random\nkind,id,variant,variant_id,routine,width,avg,pos,height,stride,src_alignment,dst_alignment,samples,ops_per_sample,mean_raw_ns,p50_raw_ns,p95_raw_ns,p99_raw_ns,mean_floor_ns,p50_net_ns,p95_net_ns,p99_net_ns\n"))return -1;
        for(kind=0;kind<2;kind++)for(size=0;size<2;size++)for(avg=0;avg<2;avg++)for(pos=0;pos<(kind?64:16);pos++) {
            struct operation o;
            o.kind=kind;o.size=size;o.avg=avg;o.pos=pos;
            o.id=kind?64+(size*2+avg)*64+pos:(size*2+avg)*16+pos;
            for(h=0;h<(kind && !phase?3:1);h++) {
                o.height=kind?(phase?8:(2<<h)):(size?8:16);
                ret=phase?measure(c,&o):verify(c,&o,&cases,&failures);
                if(ret)return ret;
            }
            if(!phase)for(v=0;v<3;v++) {
                _snprintf(line,sizeof(line),"CHECK,%d,%d,%s\n",o.id,v,valid[v][o.id]?"PASS":"REJECT");
                line[sizeof(line)-1]=0;if(!emit(c,line))return -1;
            }
        }
        if(!phase) {
            _snprintf(line,sizeof(line),"# integrity_complete=1,cases=%u,comparisons=%u,mismatches=%u\n",cases,cases*6,failures);
            line[sizeof(line)-1]=0;if(!emit(c,line))return -1;
        }
    }
    return failures?1:0;
}
