#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <vector>
#include <algorithm>
#include "../src/video.h"
#include "../src/video_session.h"
#include "../src/video_gpu_policy.h"
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
struct D3DLOCKED_RECT { int Pitch;void *pBits; };
struct D3DSURFACE_DESC { UINT Width,Height;int Format; };
struct gfx_copy_cost { DWORD lock_us,copy_us; };
static uint64_t clock_us;
static uint64_t gfx_now_us() { return clock_us+=10; }
static unsigned creates,releases,waits,ownerships,xmem_calls;
static int fail_create,fail_probe,fail_flags;
static bool native_test_pass=true,owned;
static bool pending[1024];
static DWORD next_fence;
static void on_present();
static bool system_ui,first_present_logged,last_present_shown;
static bool gfx_system_ui_active(){return system_ui;}
static DWORD GetTickCount(){return 100;}
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
    void GetLevelDesc(int,D3DSURFACE_DESC *d) { d->Width=width;d->Height=height;d->Format=D3DFMT_LIN_L8; }
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
    D3DTexture *bound[3];std::vector<D3DTexture*> drawn;
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
    void Draw() { for(int k=0;k<3;k++) { assert(bound[k]);drawn.push_back(bound[k]); } }
    DWORD InsertFence() {
        assert(owned);DWORD f=++next_fence;pending[f]=true;
        for(size_t i=0;i<drawn.size();i++) { assert(!drawn[i]->bound);drawn[i]->fence=f; }
        drawn.clear();return f;
    }
    void BlockOnFence(DWORD f) { assert(owned);waits++;pending[f]=false; }
    void BlockUntilIdle() { assert(owned);memset(pending,0,sizeof(pending)); }
    void SetPixelShaderConstantF(int,const float*,int) {}
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
}
/* ACTUAL_UPLOAD */
static void gfx_wait_fence(D3DDevice *d,DWORD f){d->BlockOnFence(f);}
/* ACTUAL_MODULE */
enum {PIPE_AUTO,PIPE_CACHED,PIPE_LEGACY,PIPE_SWITCHING,PIPE_DISABLED};
enum {TIME_FRAME_COPY,TIME_TEXTURE_LOCK,TIME_TEXTURE_COPY,TIME_GPU_SUBMIT,TIME_PRESENT_WAIT};
enum {GFX_SHADER_YUV_FAST=1,GFX_SHADER_YUV_QUALITY=2,GFX_WIDTH=1280,GFX_HEIGHT=720};
static int active_pipeline;
static video_options options;
static video_stats stats;
static unsigned gpu_slot_starvation,cached_failovers,legacy_publishes,event_count,legacy_creates;
static bool legacy_ok=true;
static bool telemetry_ready,overlay_failed,quality_available=true,produce_during_present;
static int requested_shader,vid_w=65,vid_h=33,last_shader,expected_slot=-1;
static gpu_quality_policy quality_policy;
static uint64_t measured[5];
struct AVFrame { BYTE *data[3];int linesize[3]; };
static int frame_event;
static LONG current_pipeline() { return active_pipeline; }
static LONG InterlockedIncrement(volatile LONG *p) { return ++*p; }
static LONG InterlockedExchange(int *p,LONG v) { LONG old=*p;*p=v;return old; }
static uint64_t now_us() { return gfx_now_us(); }
static void pipeline_sample(int metric,uint64_t us) { measured[metric]=us; }
static void SetEvent(int) { event_count++; }
static void publish(const AVFrame*) { legacy_publishes++; }
static bool create_legacy_video() { assert(!owned && live.empty());legacy_creates++;return legacy_ok; }
static void vlog(const char*,...) {}
static bool gfx_quality_available() { return quality_available; }
static void gfx_draw_rect(D3DDevice *d,int shader,int,int,int,int) { last_shader=shader;d->Draw(); }
static bool gfx_overlay_set(const char*) { return true; }
static void gfx_overlay_clear(D3DDevice*) {}
static void gfx_overlay_draw(D3DDevice*) {}
static void gfx_mouse_draw(D3DDevice*) {}
static void gfx_stream_menu_draw(D3DDevice*,unsigned,unsigned,unsigned,unsigned,bool) {}
static void gfx_stream_menu_submitted(DWORD) {}
static bool input_stats_visible(){return true;}
static void gfx_overlay_mark_used(DWORD) {}
/* ACTUAL_PUBLISH */
/* ACTUAL_FALLBACK */
/* ACTUAL_PRESENT */
static const AVFrame *present_frame;
static void on_present() {
    assert(owned && expected_slot>=0 && cached_slots[expected_slot].fence==next_fence);
    for(int i=0;i<3;i++)assert(!device.bound[i]);
    clock_us+=50000; // simulated VSync must not enter shader submission measurement
    if(produce_during_present) { pending[next_fence]=false;publish_frame(present_frame); }
}
static void cleanup() { gfx_begin();cached_video_cleanup(&device);gfx_end();assert(live.empty()); }
static void reset() {
    assert(live.empty());creates=releases=waits=ownerships=xmem_calls=0;
    fail_create=fail_probe=fail_flags=0;native_test_pass=true;next_fence=0;memset(pending,0,sizeof(pending));
    stats=video_stats();options=video_options();gpu_slot_starvation=cached_failovers=legacy_publishes=event_count=legacy_creates=0;
    active_pipeline=PIPE_CACHED;legacy_ok=true;
    produce_during_present=false;quality_available=true;requested_shader=0;
}
static void guard_content(D3DTexture *t,const BYTE *s,int sp,int w,int h) {
    for(int y=0;y<h;y++) {
        assert(!memcmp(&t->bytes[128+(size_t)y*t->pitch],s+(size_t)y*sp,w));
        for(int x=w;x<t->pitch;x++) assert(t->bytes[128+(size_t)y*t->pitch+x]==0xcc);
    }
    for(int i=0;i<128;i++) { assert(t->bytes[i]==0xcc);assert(t->bytes[t->bytes.size()-1-i]==0xcc); }
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
    unsigned before_wait=waits,before_owner=ownerships;
    publish_frame(&f);publish_frame(&f);publish_frame(&f);publish_frame(&f);
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
    reset();assert(cached_video_init(65,33));options.stats_overlay=true;publish_frame(&f);
    expected_slot=cached_video_take(planes,&discarded);present_frame=&f;produce_during_present=true;
    gfx_begin();present_video(&device,planes,expected_slot,"",false);gfx_end();
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
    puts("PASS: actual cached module/upload/publish/fallback: native-selftest rejection, allocation/lock/pitch faults, guards, starvation, latest READY, fences and 30 cleanup cycles.");
    puts("PASS: XMemCpy only cached destinations; no device ownership/fence wait in decoder publish. Cache coherency NOT emulated.");
}
