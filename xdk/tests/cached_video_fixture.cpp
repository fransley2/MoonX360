#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <vector>
#include <algorithm>
#include "../src/video.h"
#include "../src/video_session.h"
#include "../src/video_gpu_policy.h"
#include "../src/video_picture.h"
#include "../src/video_smoothing.h"
#include "../src/video_viewport.h"
typedef unsigned char BYTE;
typedef uint32_t DWORD;
typedef unsigned UINT;
typedef int32_t LONG;
#define FAILED(x) ((x)<0)
#define SUCCEEDED(x) ((x)>=0)
#define ZeroMemory(p,n) memset(p,0,n)
#define D3DUSAGE_CPU_CACHED_MEMORY 4
#define D3DCOMMON_CPU_CACHED_MEMORY 0x200000
#define D3DFMT_LIN_L8 1
#define D3DLOCK_NOOVERWRITE 0x1000
#define D3DCLEAR_TARGET 1
struct D3DLOCKED_RECT { int Pitch;void *pBits; };
struct D3DSURFACE_DESC { UINT Width,Height;int Format; };
struct gfx_copy_cost { DWORD lock_us,copy_us; bool verify; unsigned mismatch_rows; };
static uint64_t clock_us;
static uint64_t gfx_now_us() { return clock_us+=10; }
static unsigned creates,releases,waits,ownerships,xmem_calls,desc_calls;
static bool corrupt_cached_copy;
static int fail_create,fail_probe,fail_flags;
static bool native_test_pass=true,owned;
static bool pending[1024];
static DWORD next_fence;
static void on_present();
static bool system_ui,first_present_logged,last_present_shown,freeze_first_present;
static void plat_diagnostics_checkpoint(const char *){}
static bool gfx_system_ui_active(){return system_ui;}
static bool temporal_available=true;
static bool gfx_smoothing_available(bool){return temporal_available;}
static float last_transition[4];
static unsigned temporal_draws,produce_many;
static bool crt_available;
static bool gfx_crt_clarity_available(){return crt_available;}
static DWORD fake_tick=100;
static DWORD GetTickCount(){return fake_tick;}
struct D3DTexture;
static std::vector<D3DTexture*> live;
struct D3DTexture {
    DWORD Common,fence;
    int pitch,locks,unlocks;UINT width,height;
    bool bound,fail_lock,null_bits,bad_pitch,locked;
    std::vector<BYTE> bytes;
    D3DTexture(UINT w,UINT h,bool cached) : Common(cached?(D3DCOMMON_CPU_CACHED_MEMORY|1):1),fence(0),
        pitch((w+63)&~63),locks(0),unlocks(0),width(w),height(h),bound(false),fail_lock(false),
        null_bits(false),bad_pitch(false),locked(false),bytes((size_t)pitch*h+256,0xcc) { live.push_back(this); }
    void GetLevelDesc(int,D3DSURFACE_DESC *d) { desc_calls++;d->Width=width;d->Height=height;d->Format=D3DFMT_LIN_L8; }
    int LockRect(int,D3DLOCKED_RECT *r,void*,DWORD flags) {
        assert(!bound && (!fence || !pending[fence]) && !locked);
        if (Common & D3DCOMMON_CPU_CACHED_MEMORY) assert(flags==D3DLOCK_NOOVERWRITE); else assert(flags==0);
        locks++;if(fail_lock) return -1;
        locked=true;r->pBits=null_bits?NULL:&bytes[128];r->Pitch=bad_pitch?(int)width-1:pitch;return 0;
    }
    int UnlockRect(int) { assert(locked);locked=false;unlocks++;return 0; }
    void Release() {
        assert(owned && !locked && !bound && (!fence || !pending[fence]));
        live.erase(std::find(live.begin(),live.end(),this));releases++;delete this;
    }
};
struct D3DDevice {
    D3DTexture *bound[6];std::vector<D3DTexture*> drawn;
    D3DDevice() { memset(bound,0,sizeof(bound)); }
    int CreateTexture(int w,int h,int,int usage,int,int,D3DTexture **out,void*) {
        assert(owned);creates++;*out=NULL;if ((int)creates==fail_create) return -1;
        *out=new D3DTexture(w,h,usage==D3DUSAGE_CPU_CACHED_MEMORY);
        if ((int)creates==fail_flags) (*out)->Common &= ~D3DCOMMON_CPU_CACHED_MEMORY;
        if ((int)creates==fail_probe) (*out)->bad_pitch=true;return 0;
    }
    void SetTexture(int k,D3DTexture *t) {
        assert(owned);if(bound[k]) bound[k]->bound=false;bound[k]=t;if(t)t->bound=true;
    }
    void Draw() { for(int k=0;k<(bound[3]?6:3);k++) { assert(bound[k]);drawn.push_back(bound[k]); } }
    DWORD InsertFence() {
        assert(owned);DWORD f=++next_fence;pending[f]=true;
        for(size_t i=0;i<drawn.size();i++) { assert(!drawn[i]->bound);drawn[i]->fence=f; }
        drawn.clear();return f;
    }
    void BlockOnFence(DWORD f) { assert(owned);waits++;pending[f]=false; }
    void BlockUntilIdle() { assert(owned);memset(pending,0,sizeof(pending)); }
    void SetPixelShaderConstantF(int reg,const float *v,int) {if(reg==2)memcpy(last_transition,v,sizeof(last_transition));}
    void Clear(int,void*,int,DWORD,float,int) { assert(owned); }
    void Present(void*,void*,void*,void*) { on_present(); }
};
static D3DDevice device;
typedef int CRITICAL_SECTION;
static void InitializeCriticalSection(CRITICAL_SECTION *p) { *p=0; }
static void EnterCriticalSection(CRITICAL_SECTION *p) { assert(*p==0);*p=1; }
static void LeaveCriticalSection(CRITICAL_SECTION *p) { assert(*p==1);*p=0; }
static D3DDevice *gfx_begin() { assert(!owned);owned=true;ownerships++;return &device; }
static void gfx_end() { assert(owned);owned=false; }
static bool gfx_cached_selftest(D3DDevice*) { return native_test_pass; }
static bool gfx_fence_pending(DWORD f) { return pending[f]; }
static void XMemCpy(void *dst,const void *src,size_t n) {
    bool cached=false;
    for(size_t i=0;i<live.size();i++) {
        uintptr_t d=(uintptr_t)dst,b=(uintptr_t)&live[i]->bytes[128];
        if(d>=b && d+n<=b+(size_t)live[i]->pitch*live[i]->height) cached=(live[i]->Common & D3DCOMMON_CPU_CACHED_MEMORY)!=0;
    }
    assert(cached);xmem_calls++;memcpy(dst,src,n);
    if(corrupt_cached_copy && n)((BYTE*)dst)[0]^=1;
}
/* ACTUAL_UPLOAD */
static void gfx_wait_fence(D3DDevice *d,DWORD f){d->BlockOnFence(f);}
static uint64_t gfx_texture_allocation_bytes(D3DTexture *t){return ((uint64_t)t->pitch*t->height+4095)&~(uint64_t)4095;}
/* ACTUAL_MODULE */
enum {PIPE_AUTO,PIPE_CACHED,PIPE_LEGACY,PIPE_SWITCHING,PIPE_DISABLED};
enum {TIME_FRAME_COPY,TIME_TEXTURE_LOCK,TIME_TEXTURE_COPY,TIME_GPU_SUBMIT,TIME_PRESENT_WAIT};
enum {GFX_SHADER_YUV_FAST=1,GFX_SHADER_YUV_QUALITY=2,GFX_SHADER_CRT_CLARITY=4,GFX_SHADER_YUV_SMOOTH=5,GFX_SHADER_YUV_SMOOTH_CRT=6,GFX_WIDTH=1280,GFX_HEIGHT=720};
static int active_pipeline;
static video_options options;
static struct { int capture_yuv; } h264_diagnostic;
static bool yuv_upload_compared;
static video_stats stats;
static unsigned gpu_slot_starvation,cached_failovers,legacy_publishes,event_count,legacy_creates;
static bool legacy_ok=true;
static bool telemetry_ready,overlay_failed,quality_available=true,produce_during_present;
static int requested_shader,vid_w=65,vid_h=33,last_shader,expected_slot=-1;
static gpu_quality_policy quality_policy;
static uint64_t measured[5];
static uint64_t previous_present_start_us,last_present_start_us,last_present_return_us,last_presented_us;
static uint32_t present_age_sample_ordinal;
static uint32_t last_present_serial,present_duplicates;
static uint32_t idr_present_calls;
static bool last_present_cached;
struct present_trace_entry {uint64_t start_us,return_us;uint32_t serial,frame;uint8_t cached,idr;};
static present_trace_entry present_trace[64];
static uint32_t present_trace_next,present_trace_count;
struct AVFrame { BYTE *data[3];int linesize[3];int64_t reordered_opaque;int key_frame; };
static int frame_event;
static LONG current_pipeline() { return active_pipeline; }
static LONG InterlockedIncrement(volatile LONG *p) { return ++*p; }
static LONG InterlockedExchangeAdd(volatile LONG *p,LONG n) { LONG old=*p;*p+=n;return old; }
static LONG InterlockedExchange(int *p,LONG v) { LONG old=*p;*p=v;return old; }
static uint64_t now_us() { return gfx_now_us(); }
static void pipeline_sample(int metric,uint64_t us) { measured[metric]=us; }
static void SetEvent(int) { event_count++; }
static void publish(const AVFrame*) { legacy_publishes++; }
static bool create_legacy_video() { assert(!owned && live.empty());legacy_creates++;return legacy_ok; }
static void vlog(const char*,...) {}
static bool gfx_quality_available() { return quality_available; }
static void gfx_backbuffer_size(int *w,int *h) { *w=1280;*h=720; }
static void gfx_draw_rect(D3DDevice *d,int shader,float,float,float,float) {
    last_shader=shader;
    bool smoothing=shader==GFX_SHADER_YUV_SMOOTH || shader==GFX_SHADER_YUV_SMOOTH_CRT;
    for(int k=0;k<3;k++) {
        assert(d->bound[k]);
        if(smoothing)assert(d->bound[k+3] && d->bound[k+3]!=d->bound[k]);
        else assert(!d->bound[k+3]);
    }
    if(smoothing){assert(last_transition[0]>0 && last_transition[0]<=0.15f);temporal_draws++;}
    d->Draw();
}
static bool gfx_overlay_set(const char*) { return true; }
static void gfx_overlay_clear(D3DDevice*) {}
static void gfx_overlay_draw(D3DDevice*) {}
static void gfx_mouse_draw(D3DDevice*) {}
static void gfx_stream_menu_draw(D3DDevice*,unsigned,unsigned,unsigned,unsigned,bool) {}
static void gfx_stream_menu_submitted(DWORD) {}
static bool input_stats_visible(){return true;}
static unsigned overlay_prepares;
static bool overlay_text(char*,size_t){overlay_prepares++;return false;}
static void gfx_overlay_mark_used(DWORD) {}
/* ACTUAL_PUBLISH */
/* ACTUAL_FALLBACK */
static bool opening,opening_drawn;
static bool gfx_launch_pending(){return opening;}
static bool launch_active(){return opening;}
static void gfx_launch_draw(D3DDevice*,bool usable){assert(owned && usable);opening_drawn=true;}
static char smoothing_report[2048];
static bool storage_write(const char *name,const void *data,size_t n){assert(!strcmp(name,"smoothing-session.txt") && n<sizeof(smoothing_report));memcpy(smoothing_report,data,n);smoothing_report[n]=0;return true;}
/* ACTUAL_CONFIGURE_HISTORY */
/* ACTUAL_PRESENT */
/* ACTUAL_DRAW_CACHED */
/* ACTUAL_SMOOTH_REPORT */
static const AVFrame *present_frame;
static void on_present() {
    if(opening)assert(opening_drawn);opening_drawn=false;
    assert(owned);if(expected_slot>=0)assert(cached_slots[expected_slot].fence==next_fence);
    for(int i=0;i<6;i++)assert(!device.bound[i]);
    clock_us+=50000; // simulated VSync must not enter shader submission measurement
    if(produce_during_present) { pending[next_fence]=false;publish_frame(present_frame); }
    if(produce_many){pending[next_fence]=false;for(unsigned i=0;i<produce_many;i++)publish_frame(present_frame);}
}
static void cleanup() { gfx_begin();cached_video_cleanup(&device);gfx_end();assert(live.empty()); }
static void reset() {
    cached_video_reset_smoothing_diagnostics();
    assert(live.empty());creates=releases=waits=ownerships=xmem_calls=0;
    fail_create=fail_probe=fail_flags=0;native_test_pass=true;next_fence=0;memset(pending,0,sizeof(pending));
    stats=video_stats();options=video_options();gpu_slot_starvation=cached_failovers=legacy_publishes=event_count=legacy_creates=0;
    active_pipeline=PIPE_CACHED;legacy_ok=true;
    produce_during_present=false;quality_available=true;requested_shader=0;
    temporal_available=true;crt_available=false;produce_many=temporal_draws=0;fake_tick=100;memset(last_transition,0,sizeof(last_transition));
}
static void guard_content(D3DTexture *t,const BYTE *s,int sp,int w,int h) {
    for(int y=0;y<h;y++) {
        assert(!memcmp(&t->bytes[128+(size_t)y*t->pitch],s+(size_t)y*sp,w));
        for(int x=w;x<t->pitch;x++) assert(t->bytes[128+(size_t)y*t->pitch+x]==0xcc);
    }
    for(int i=0;i<128;i++) { assert(t->bytes[i]==0xcc);assert(t->bytes[t->bytes.size()-1-i]==0xcc); }
}
static void temporal_present(const AVFrame *f) {
    publish_frame(f);D3DTexture *planes[3];unsigned discarded=0;
    expected_slot=cached_video_take(planes,&discarded);assert(expected_slot>=0);
    gfx_begin();present_video(&device,planes,expected_slot,"",false,cached_video_ready_time(expected_slot),cached_video_frame_serial(expected_slot),cached_video_picture(expected_slot));gfx_end();
}
static void test_temporal_history() {
    for(int size=0;size<2;size++) {
        vid_w=size?1280:640;vid_h=size?720:480;
        int w=vid_w,h=vid_h,cw=(w+1)/2,ch=(h+1)/2;
        std::vector<BYTE> y(w*h,100),u(cw*ch,128),v(cw*ch,128);
        AVFrame f={{&y[0],&u[0],&v[0]},{w,cw,cw},0,0};
        // No fourth slot or new texture in the default Off mode.
        reset();assert(cached_video_init(w,h));assert(creates==9 && cached_count==3);cleanup();
        // Optional allocation failure preserves all original slots and falls back FAST.
        reset();assert(cached_video_init(w,h));options.refresh_smoothing=1;fail_create=11;
        assert(!cached_video_enable_history() && cached_count==3 && live.size()==9);
        temporal_present(&f);assert(last_shader==GFX_SHADER_YUV_FAST && !temporal_draws);cleanup();
        reset();assert(cached_video_init(w,h));options.refresh_smoothing=1;temporal_available=false;
        configure_smoothing_history(true,h);assert(cached_count==3 && creates==9);
        temporal_present(&f);assert(last_shader==GFX_SHADER_YUV_FAST);cleanup();
        for(int fault=0;fault<2;fault++) {
            reset();assert(cached_video_init(w,h));options.refresh_smoothing=1;
            if(fault==0)fail_probe=11;else fail_flags=11;
            assert(!cached_video_enable_history() && cached_count==3 && live.size()==9);
            temporal_present(&f);assert(last_shader==GFX_SHADER_YUV_FAST);cleanup();
        }
        // Preserve three publish slots while pinning current BEFORE a producer runs in Present.
        reset();assert(cached_video_init(w,h));options.refresh_smoothing=1;configure_smoothing_history(true,h);
        assert(cached_count==4 && creates==12);present_frame=&f;produce_many=3;
        temporal_present(&f);produce_many=0;
        assert(cached_slots[expected_slot].state==GPU_HISTORY && cached_slots[expected_slot].serial==1);
        unsigned ready=0,flight=0;cached_video_snapshot(&ready,&flight);assert(ready==3 && !gpu_slot_starvation);cleanup();
        // First IDR, following P and next direct picture: real six-texture renderer dispatch.
        reset();assert(cached_video_init(w,h));options.refresh_smoothing=1;configure_smoothing_history(true,h);
        f.reordered_opaque=(int64_t)video_picture_tag(1,0,false);f.key_frame=0;temporal_present(&f);
        int before=cached_history;DWORD before_fence=cached_slots[before].fence;
        pending[before_fence]=false;
        std::fill(y.begin(),y.end(),104);f.reordered_opaque=(int64_t)video_picture_tag(2,1,true);f.key_frame=1;
        temporal_present(&f);assert(last_shader==GFX_SHADER_YUV_SMOOTH && temporal_draws==1 && last_transition[0]==0.10f);
        assert(cached_slots[before].state==GPU_IN_FLIGHT && cached_slots[before].fence==next_fence && pending[next_fence]);
        assert(cached_history!=before && cached_slots[cached_history].state==GPU_HISTORY);
        std::fill(y.begin(),y.end(),105);f.reordered_opaque=(int64_t)video_picture_tag(3,1,false);f.key_frame=0;
        temporal_present(&f);assert(last_shader==GFX_SHADER_YUV_SMOOTH && temporal_draws==2 && last_transition[0]==0.04f);
        f.reordered_opaque=(int64_t)video_picture_tag(4,1,false);temporal_present(&f);
        assert(last_shader==GFX_SHADER_YUV_FAST && temporal_draws==2 && !gpu_slot_starvation);
        smoothing_diagnostics m;cached_video_smoothing_snapshot(&m);
        assert(m.enabled && m.idr_transition_started==1 && m.idr_transition_applied==1 && m.actual_blended_present_calls==2);
        assert(m.additional_history_row_bytes==(uint64_t)w*h+(uint64_t)cw*ch*2);
        assert(m.additional_gpu_history_bytes>=m.additional_history_row_bytes);
        write_smoothing_report();assert(strstr(smoothing_report,"gpu_smoothing_enabled=1\n") && strstr(smoothing_report,"actual_blended_present_calls=2\n"));
        // A pinned picture is never offered to the producer, even after its fence completes.
        for(unsigned i=0;i<=next_fence;i++)pending[i]=false;
        int retained=cached_history;uint32_t serial=cached_slots[retained].serial;
        publish_frame(&f);publish_frame(&f);publish_frame(&f);
        assert(cached_slots[retained].state==GPU_HISTORY && cached_slots[retained].serial==serial && !gpu_slot_starvation);
        publish_frame(&f);assert(gpu_slot_starvation==1);cleanup();
        // Known invalidity, skipped presentation and a stale picture all bypass.
        for(int why=0;why<3;why++) {
            reset();std::fill(y.begin(),y.end(),100);assert(cached_video_init(w,h));options.refresh_smoothing=1;configure_smoothing_history(true,h);
            f.reordered_opaque=video_picture_tag(10,0,false);f.key_frame=0;temporal_present(&f);
            for(unsigned i=0;i<=next_fence;i++)pending[i]=false;
            if(why==0)cached_video_invalidate_history();
            if(why==1)cached_video_presented(cached_history,false);
            if(why==2)fake_tick=251;
            f.reordered_opaque=video_picture_tag(11,1,true);f.key_frame=1;temporal_present(&f);
            assert(!temporal_draws && last_shader==GFX_SHADER_YUV_FAST);
            cached_video_smoothing_snapshot(&m);assert(m.transition_bypassed_invalid_history==1);cleanup();
        }
        // Major scene correction bypasses before shader dispatch and appears directly.
        reset();std::fill(y.begin(),y.end(),30);assert(cached_video_init(w,h));options.refresh_smoothing=1;configure_smoothing_history(true,h);
        f.reordered_opaque=video_picture_tag(20,0,false);f.key_frame=0;temporal_present(&f);
        std::fill(y.begin(),y.end(),200);f.reordered_opaque=video_picture_tag(21,1,true);f.key_frame=1;temporal_present(&f);
        cached_video_smoothing_snapshot(&m);assert(!temporal_draws && m.transition_bypassed_motion==1);cleanup();
        // No recursive framebuffer history: each history resource is the raw uploaded input.
        reset();std::fill(y.begin(),y.end(),100);assert(cached_video_init(w,h));options.refresh_smoothing=1;options.smoothing_strength=1;configure_smoothing_history(true,h);
        f.reordered_opaque=video_picture_tag(30,0,false);f.key_frame=0;temporal_present(&f);
        std::fill(y.begin(),y.end(),102);f.reordered_opaque=video_picture_tag(31,1,true);f.key_frame=1;temporal_present(&f);
        assert(last_transition[0]==0.15f && cached_textures[cached_history][0]->bytes[128]==102);
        for(unsigned i=0;i<=next_fence;i++)pending[i]=false;
        std::fill(y.begin(),y.end(),103);f.reordered_opaque=video_picture_tag(32,1,false);f.key_frame=0;temporal_present(&f);
        assert(last_transition[0]==0.07f && cached_textures[cached_history][0]->bytes[128]==103);cleanup();
        assert(cached_video_memory()==0 && live.empty());
        // CRT stays a separate option, combined into the same temporal draw.
        reset();std::fill(y.begin(),y.end(),100);assert(cached_video_init(w,h));
        options.refresh_smoothing=1;options.crt_clarity=1;crt_available=true;configure_smoothing_history(true,h);
        f.reordered_opaque=video_picture_tag(40,0,false);f.key_frame=0;temporal_present(&f);
        assert(last_shader==(h<=480?GFX_SHADER_CRT_CLARITY:GFX_SHADER_YUV_FAST));
        std::fill(y.begin(),y.end(),101);f.reordered_opaque=video_picture_tag(41,1,true);f.key_frame=1;temporal_present(&f);
        assert(last_shader==(h<=480?GFX_SHADER_YUV_SMOOTH_CRT:GFX_SHADER_YUV_SMOOTH) && temporal_draws==1);cleanup();
        // A READY IDR superseded by a newer P never starts a transition.
        reset();assert(cached_video_init(w,h));options.refresh_smoothing=1;configure_smoothing_history(true,h);
        f.reordered_opaque=video_picture_tag(50,0,false);f.key_frame=0;temporal_present(&f);
        f.reordered_opaque=video_picture_tag(51,1,true);f.key_frame=1;publish_frame(&f);
        f.reordered_opaque=video_picture_tag(52,1,false);f.key_frame=0;temporal_present(&f);
        cached_video_smoothing_snapshot(&m);assert(!temporal_draws && !m.idr_transition_started);cleanup();
        // Unknown (zero) completion must stay protected, including after unpin.
        reset();assert(cached_video_init(w,h));assert(cached_video_enable_history());
        publish_frame(&f);D3DTexture *p[3];unsigned dropped=0;int old=cached_video_take(p,&dropped);
        cached_video_submitted(old,0);cached_video_presented(old,true);
        publish_frame(&f);int next=cached_video_take(p,&dropped);assert(next!=old);
        cached_video_submitted(next,0);cached_video_presented(next,true);cached_reap();
        assert(cached_slots[old].state==GPU_IN_FLIGHT && cached_slots[next].state==GPU_HISTORY);
        publish_frame(&f);publish_frame(&f);unsigned stalled=gpu_slot_starvation;publish_frame(&f);
        assert(gpu_slot_starvation==stalled+1);cleanup();
    }
    // Forced Legacy has zero temporal draws, even after a prior smoothed session.
    reset();active_pipeline=PIPE_LEGACY;options.refresh_smoothing=1;expected_slot=-1;
    D3DTexture *legacy[3];gfx_begin();for(int k=0;k<3;k++)legacy[k]=new D3DTexture(65,33,false);
    present_video(&device,legacy,-1,"",false,0,1,video_picture_tag(90,1,true));gfx_end();
    assert(last_shader==GFX_SHADER_YUV_FAST && !temporal_draws);
    write_smoothing_report();assert(strstr(smoothing_report,"gpu_smoothing_enabled=0\n") && strstr(smoothing_report,"actual_blended_present_calls=0\n"));
    for(unsigned i=0;i<=next_fence;i++)pending[i]=false;
    gfx_begin();for(int k=0;k<3;k++)legacy[k]->Release();gfx_end();assert(live.empty());
    vid_w=65;vid_h=33;
    puts("PASS: actual temporal renderer: two distinct raw YUV pictures, six sampler bindings, nonzero constants and shader path; 2-step decay, three publish slots, optional allocation/shader fallback, invalid/Guide/stale/motion bypass, raw history and 480/720p lifetimes. GPU execution/cache coherency NOT emulated.");
}
int main() {
    reset();native_test_pass=false;assert(!cached_video_init(65,33) && live.empty());
    reset();fail_create=5;assert(!cached_video_init(65,33) && live.empty() && releases==4);
    reset();fail_flags=2;assert(!cached_video_init(65,33) && live.empty() && releases==2 && xmem_calls==0);
    reset();fail_probe=3;assert(!cached_video_init(65,33) && live.empty() && releases==3);
    reset();assert(cached_video_init(65,33) && creates==9 && cached_video_memory()==19200);
    BYTE y[80*33],u[48*17],v[48*17];
    for(size_t i=0;i<sizeof(y);i++)y[i]=(BYTE)i;
    for(size_t i=0;i<sizeof(u);i++) { u[i]=(BYTE)(i*3);v[i]=(BYTE)(i*7); }
    AVFrame f={{y,u,v},{80,48,48}};
    assert(!cached_video_has_ready());
    unsigned empty_owners=ownerships,empty_prepares=overlay_prepares;
    for(int i=0;i<100;i++)assert(draw_cached_frame()==0);
    assert(ownerships==empty_owners && overlay_prepares==empty_prepares);
    // A publish after the empty peek still signals and is consumed next time.
    unsigned empty_events=event_count;publish_frame(&f);
    assert(event_count==empty_events+1 && cached_video_has_ready());
    expected_slot=-1;assert(draw_cached_frame()==1 && !owned);
    assert(!cached_video_has_ready()); // GPU-owned is not READY.
    empty_owners=ownerships;assert(draw_cached_frame()==0 && ownerships==empty_owners);
    // The preflight is read-only: the existing take still chooses latest READY.
    pending[next_fence]=false;publish_frame(&f);publish_frame(&f);
    unsigned newest=cached_serial;assert(cached_video_has_ready());
    system_ui=true;empty_owners=ownerships;
    assert(draw_cached_frame()==0 && ownerships==empty_owners && cached_video_has_ready());
    system_ui=false;assert(draw_cached_frame()==1 && last_present_serial==newest);
    assert(stats.render_superseded==1);
    cleanup();reset();assert(cached_video_init(65,33));
    unsigned before_wait=waits,before_owner=ownerships,before_desc=desc_calls;
    publish_frame(&f);publish_frame(&f);publish_frame(&f);publish_frame(&f);
    assert(desc_calls-before_desc==(OPTIMIZED_YUV_COPY?0u:9u));
    assert(gpu_slot_starvation==1 && stats.dropped==1 && waits==before_wait && ownerships==before_owner);
    unsigned discarded=0;D3DTexture *planes[3];int slot=cached_video_take(planes,&discarded);
    assert(slot==2 && discarded==2);
    guard_content(planes[0],y,80,65,33);guard_content(planes[1],u,48,33,17);guard_content(planes[2],v,48,33,17);
    gfx_begin();for(int k=0;k<3;k++)device.SetTexture(k,planes[k]);device.Draw();
    for(int k=0;k<3;k++)device.SetTexture(k,NULL);
    DWORD fence=device.InsertFence();cached_video_submitted(slot,fence);gfx_end();
    publish_frame(&f);publish_frame(&f);publish_frame(&f);
    assert(gpu_slot_starvation==2 && cached_slots[2].state==GPU_IN_FLIGHT);
    pending[fence]=false;publish_frame(&f);assert(cached_slots[2].state==GPU_READY);
    cleanup();
    // Recovery prunes only READY frames. The selected first frame remains
    // protected until its fence completes; reusing its slot changes serial.
    reset();assert(cached_video_init(65,33));
    publish_frame(&f);publish_frame(&f);publish_frame(&f);
    unsigned ready_count=0,flight_count=0;
    cached_video_snapshot(&ready_count,&flight_count);
    assert(ready_count==3 && flight_count==0);
    assert(cached_video_prune_ready()==2);
    cached_video_snapshot(&ready_count,&flight_count);
    assert(ready_count==1 && flight_count==0);
    slot=cached_video_take(planes,&discarded);
    assert(slot==2 && discarded==0 && cached_slots[slot].state==GPU_IN_FLIGHT);
    uint32_t first_serial=cached_video_frame_serial(slot);
    assert(cached_video_prune_ready()==0 && cached_slots[slot].state==GPU_IN_FLIGHT);
    gfx_begin();for(int k=0;k<3;k++)device.SetTexture(k,planes[k]);device.Draw();
    for(int k=0;k<3;k++)device.SetTexture(k,NULL);
    fence=device.InsertFence();cached_video_submitted(slot,fence);gfx_end();
    publish_frame(&f);publish_frame(&f);
    cached_video_snapshot(&ready_count,&flight_count);
    assert(ready_count==2 && flight_count==1 && cached_slots[slot].fence==fence);
    assert(cached_video_prune_ready()==1 && cached_slots[slot].state==GPU_IN_FLIGHT);
    publish_frame(&f); // refill the discarded READY slot, not the GPU-owned slot
    pending[fence]=false;
    publish_frame(&f);
    assert(cached_slots[slot].state==GPU_READY && cached_video_frame_serial(slot)>first_serial);
    // Negative/short pitches, NULL buffers and locks fall back without waiting.
    cleanup();reset();assert(cached_video_init(65,33));
    cached_textures[0][0]->fail_lock=true;publish_frame(&f);
    assert(active_pipeline==PIPE_SWITCHING && cached_failovers==1 && waits==0);
    unsigned l=cached_textures[0][0]->locks;publish_frame(&f);assert(cached_textures[0][0]->locks==(int)l);
    activate_legacy_fallback();assert(active_pipeline==PIPE_LEGACY && legacy_creates==1 && live.empty());
    publish_frame(&f);assert(legacy_publishes==1);
    for(int failure=0;failure<3;failure++) {
        reset();assert(cached_video_init(65,33));
        if(failure==0)cached_textures[0][0]->null_bits=true;
        if(failure==1)cached_textures[0][1]->bad_pitch=true;
        int saved=f.linesize[0];if(failure==2)f.linesize[0]=-80;
        publish_frame(&f);assert(active_pipeline==PIPE_SWITCHING && waits==0);f.linesize[0]=saved;cleanup();
    }
    // Timed legacy path must never enter cached-only XMemCpy.
    reset();gfx_begin();D3DTexture *wc=new D3DTexture(65,33,false);
    gfx_copy_cost cost={0,0};unsigned calls=xmem_calls;
    assert(gfx_upload_timed(wc,y,80,65,33,&cost));assert(calls==xmem_calls && cost.copy_us && cost.lock_us);
    assert(!gfx_cached_upload(wc,y,80,65,33,NULL));guard_content(wc,y,80,65,33);wc->Release();gfx_end();
    reset();gfx_begin();D3DTexture *probe=new D3DTexture(65,33,true);
    gfx_copy_cost verified={0,0};verified.verify=true;
    assert(gfx_cached_upload(probe,y,80,65,33,&verified) && verified.mismatch_rows==0);
    verified=gfx_copy_cost();verified.verify=true;corrupt_cached_copy=true;
    assert(!gfx_cached_upload(probe,y,80,65,33,&verified) && verified.mismatch_rows>0);
    corrupt_cached_copy=false;probe->Release();gfx_end();
    reset();assert(cached_video_init(65,33));options.stats_overlay=true;
    f.reordered_opaque=(int64_t)video_picture_tag(100,7,true);f.key_frame=1;publish_frame(&f);
    expected_slot=cached_video_take(planes,&discarded);present_frame=&f;produce_during_present=true;
    assert(cached_video_picture(expected_slot)==(uint64_t)f.reordered_opaque);
    uint64_t selected_picture=cached_video_picture(expected_slot);
    f.key_frame=0; // replacement produced during Present has a different identity
    unsigned idrs_before=idr_present_calls;
    gfx_begin();present_video(&device,planes,expected_slot,"",false,cached_video_ready_time(expected_slot),cached_video_frame_serial(expected_slot),selected_picture);gfx_end();
    assert(idr_present_calls==idrs_before+1 && cached_video_picture(expected_slot)==((uint64_t)f.reordered_opaque&~(uint64_t)1));
    assert(last_shader==GFX_SHADER_YUV_QUALITY && measured[TIME_GPU_SUBMIT]<100 && measured[TIME_PRESENT_WAIT]>=50000);
    assert(cached_slots[expected_slot].state==GPU_READY); // producer may reuse AFTER draw fence, BEFORE Present returns
    expected_slot=cached_video_take(planes,&discarded);quality_available=false;produce_during_present=false;
    gfx_begin();present_video(&device,planes,expected_slot,"",false);gfx_end();
    assert(last_shader==GFX_SHADER_YUV_FAST);cleanup();
    // Real renderer: changing CPU Fast Decode never selects the GPU shader.
    // Manual Quality also ignores a leftover automatic-policy fast flag.
    for(int cpu=0;cpu<2;cpu++)for(int request=0;request<3;request++)for(int automatic=0;automatic<2;automatic++){
        reset();assert(cached_video_init(65,33));options.fast_decode=cpu!=0;
        options.shader_mode=requested_shader=request;quality_policy=gpu_quality_policy();quality_policy.fast=automatic!=0;
        publish_frame(&f);expected_slot=cached_video_take(planes,&discarded);
        gfx_begin();present_video(&device,planes,expected_slot,"",false);gfx_end();
        assert(last_shader==(request==1 || (request==0 && automatic)?GFX_SHADER_YUV_FAST:GFX_SHADER_YUV_QUALITY));cleanup();
    }
    for(int compiled=0;compiled<2;compiled++)for(int enabled=0;enabled<2;enabled++) {
        reset();assert(cached_video_init(65,33));crt_available=compiled!=0;options.crt_clarity=enabled!=0;requested_shader=2;
        publish_frame(&f);expected_slot=cached_video_take(planes,&discarded);
        gfx_begin();present_video(&device,planes,expected_slot,"",false,cached_video_ready_time(expected_slot),cached_video_frame_serial(expected_slot),cached_video_picture(expected_slot));gfx_end();
        assert(last_shader==(compiled && enabled?GFX_SHADER_CRT_CLARITY:GFX_SHADER_YUV_QUALITY));cleanup();
    }
    crt_available=false;
    reset();assert(cached_video_init(65,33));publish_frame(&f);expected_slot=cached_video_take(planes,&discarded);
    opening=true;gfx_begin();present_video(&device,planes,expected_slot,"",false);gfx_end();opening=false;cleanup();
    const int layouts[][4]={{64,64,64,16},{80,128,65,33},{96,64,64,6},
        {1920,1920,1920,1080},{2048,2048,1920,1080},{960,1024,960,540}};
    for(int test=0;test<6;test++) {
        int sp=layouts[test][0],dp=layouts[test][1],w=layouts[test][2],h=layouts[test][3];
        std::vector<BYTE> src((size_t)sp*h+16);
        for(size_t i=0;i<src.size();i++)src[i]=(BYTE)(i*13);
        gfx_begin();D3DTexture *t=new D3DTexture(w,h,true);
        t->pitch=dp;t->bytes.assign((size_t)dp*h+256,0xcc);
        gfx_copy_cost measured={0,0};
        assert(gfx_cached_upload(t,&src[0],sp,w,h,&measured));
        guard_content(t,&src[0],sp,w,h);assert(measured.lock_us && measured.copy_us);
        assert(!gfx_cached_upload(t,NULL,sp,w,h,NULL));
        assert(!gfx_cached_upload(t,&src[0],w-1,w,h,NULL));
        assert(!gfx_cached_upload(t,&src[0],sp,w+1,h,NULL));
        assert(!gfx_cached_upload(t,&src[0],sp,w-1,h,NULL));
        assert(!gfx_cached_upload(t,&src[0],sp,w,h+1,NULL));
        t->Release();gfx_end();
    }
    // Full lifecycle including IN_FLIGHT cleanup: unbind -> fence -> idle -> release.
    for(int cycle=0;cycle<30;cycle++) {
        reset();assert(cached_video_init(65,33));publish_frame(&f);
        slot=cached_video_take(planes,&discarded);gfx_begin();
        for(int k=0;k<3;k++)device.SetTexture(k,planes[k]);device.Draw();
        for(int k=0;k<3;k++)device.SetTexture(k,NULL);
        fence=device.InsertFence();cached_video_submitted(slot,fence);gfx_end();
        cleanup();assert(waits==1 && releases==9);cleanup();
    }
    test_temporal_history();
    puts("PASS: actual cached module/upload/publish/fallback: native-selftest rejection, allocation/lock/pitch faults, guards, starvation, latest READY, fences and 30 cleanup cycles.");
    puts("PASS: XMemCpy only cached destinations; no device ownership/fence wait in decoder publish. Cache coherency NOT emulated.");
}
