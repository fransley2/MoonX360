#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../src/video.h"
#include "../src/stream_metrics.h"
typedef uint32_t DWORD;
typedef int32_t LONG;
typedef void *LPVOID;
#define WINAPI
static void xdk_audit_thread(const char*) {}
namespace render_test {
static video_options options;
static video_stats stats;
static bool telemetry_ready, overlay_failed, owned, locked, fail_upload,last_present_shown,guide;
static bool pending_fences[16];
static int slot_pending, slot_rendering, mailbox_lock, tex_next;
static int inject_begin, inject_fence, inject_upload, uploaded_slot, uploaded_texture, uploads, presents, clock_calls;
static int vid_w=1920, vid_h=1080, chroma_w=960, chroma_h=540;
#define GFX_SHADER_YUV 1
#define GFX_WIDTH 1280
#define GFX_HEIGHT 720
struct staging { unsigned char *plane[3]; };
static unsigned char values[3][3];
static staging slots[3];
struct D3DTexture {};
static D3DTexture objects[2][3], *tex[2][3];
static DWORD tex_fence[2];
static void inject(int s) {
    if(s<0) return;
    assert(!locked && s != slot_rendering);
    slot_pending=s;
}
struct D3DDevice {
    void BlockOnFence(DWORD) { assert(false && "render selection must not wait for a busy texture"); }
    void SetTexture(int, D3DTexture *) { assert(owned); }
    DWORD InsertFence() { assert(owned && slot_rendering == -1); return 7; }
    void Present(void*,void*,void*,void*) { assert(owned); presents++; }
};
static D3DDevice device;
static D3DDevice *gfx_begin() { assert(!owned); owned=true; inject(inject_begin); return &device; }
static void gfx_end() { assert(owned && !locked); owned=false; }
static void EnterCriticalSection(int*) { assert(!locked); locked=true; }
static void LeaveCriticalSection(int*) { assert(locked); locked=false; }
static LONG InterlockedIncrement(volatile LONG *p) { return ++*p; }
static uint64_t now_us() { return (uint64_t)++clock_calls * 100; }
static bool overlay_text(char*,size_t) { return false; }
static bool gfx_upload(D3DTexture *texture,const void *src,int,int,int) {
    assert(owned && !locked && slot_rendering>=0);
    assert(src == slots[slot_rendering].plane[uploads%3]);
    int set=texture==tex[0][uploads%3]?0:texture==tex[1][uploads%3]?1:-1;
    assert(set>=0 && !pending_fences[tex_fence[set]]);
    assert(!uploads || uploaded_texture==set); // all three planes share one safe set
    uploaded_texture=set;
    uploaded_slot=slot_rendering; uploads++;
    inject(inject_upload);
    return !fail_upload;
}
static void gfx_draw_rect(D3DDevice*,int,int,int,int,int) { assert(uploads==3); }
static bool gfx_overlay_set(const char*) { return true; }
static void gfx_overlay_clear(D3DDevice*) {}
static void gfx_overlay_draw(D3DDevice*) {}
static void gfx_overlay_mark_used(DWORD) {}
static void vlog(const char*) {}
struct gfx_copy_cost { DWORD lock_us,copy_us; };
enum {TIME_FENCE_WAIT,TIME_TEXTURE_LOCK,TIME_TEXTURE_COPY};
static void pipeline_sample(int,uint64_t) {}
static bool gfx_upload_timed(D3DTexture *t,const void *s,int p,int w,int h,gfx_copy_cost*) { return gfx_upload(t,s,p,w,h); }
static DWORD present_video(D3DDevice *d,D3DTexture**,int,const char*,bool) {
    last_present_shown=true;gfx_draw_rect(d,0,0,0,0,0);DWORD f=d->InsertFence();d->Present(NULL,NULL,NULL,NULL);return f;
}
static bool gfx_system_ui_active(){return guide;}
static bool gfx_fence_pending(DWORD fence){assert(fence<16);inject(inject_fence);return pending_fences[fence];}
/* ACTUAL_DRAW */
#define FRAME_SLOTS 3
struct AVFrame { unsigned char *data[3]; int linesize[3]; };
static int frame_event, consume_during_copy;
static void SetEvent(int) {}
static void copy_plane(unsigned char*,int,int,const unsigned char*,int) {
    assert(!locked);
    if(consume_during_copy) { slot_pending=-1; consume_during_copy=0; }
}
/* ACTUAL_PUBLISH */
static void reset() {
    memset(&options,0,sizeof(options)); memset(&stats,0,sizeof(stats));
    slot_rendering=0;slot_pending=-1; tex_next=0;
    inject_begin=inject_fence=inject_upload=-1;
    uploads=presents=clock_calls=0; uploaded_slot=uploaded_texture=-1; fail_upload=false;
    memset(pending_fences,0,sizeof(pending_fences));
    tex_fence[0]=3; tex_fence[1]=4;
    for(int i=0;i<3;i++) for(int k=0;k<3;k++) slots[i].plane[k]=&values[i][k];
    for(int i=0;i<2;i++) for(int k=0;k<3;k++) tex[i][k]=&objects[i][k];
}
static void test() {
    reset(); assert(draw_frame(0)); assert(uploaded_slot==0 && presents==1 && clock_calls==0);
    reset(); inject_fence=1; assert(draw_frame(0));
    assert(uploaded_slot==1 && slot_pending==-1 && slot_rendering==-1);
    assert(stats.dropped==1 && stats.render_superseded==1);
    reset(); inject_begin=2; assert(draw_frame(0)); assert(uploaded_slot==2);
    reset(); inject_fence=1; inject_upload=2; assert(draw_frame(0));
    assert(uploaded_slot==1 && slot_pending==2 && slot_rendering==-1 && stats.dropped==1);
    reset(); fail_upload=true; assert(!draw_frame(0));
    assert(slot_rendering==-1 && !owned && presents==0);
    reset(); options.stats_overlay=true; assert(draw_frame(0));
    assert(clock_calls==3 && stats.avg_upload_us>0 && stats.avg_gpu_wait_us>0);
    reset();guide=true;assert(!draw_frame(0));assert(!owned && slot_rendering==-1 && presents==0);guide=false;
    // Cover both preferences and every independent fence combination. A busy
    // preferred set may fall back only to the other FREE set, never an in-flight one.
    for(int preferred=0;preferred<2;preferred++) for(int mask=0;mask<4;mask++) {
        reset();tex_next=preferred;
        pending_fences[3]=(mask&1)!=0;pending_fences[4]=(mask&2)!=0;
        int expected=!(mask&(1<<preferred))?preferred:!(mask&(1<<(preferred^1)))?(preferred^1):-1;
        assert(draw_frame(0)==(expected>=0));
        assert(!owned && slot_rendering==-1 && slot_pending==-1);
        assert(pending_fences[3]==((mask&1)!=0) && pending_fences[4]==((mask&2)!=0));
        if(expected<0)assert(!uploads && !presents && uploaded_texture==-1 && tex_fence[0]==3 && tex_fence[1]==4);
        else {
            assert(uploads==3 && presents==1 && uploaded_texture==expected);
            assert(tex_next==(expected^1) && tex_fence[expected]==7);
            assert(tex_fence[expected^1]==(DWORD)(3+(expected^1)));
        }
    }
    reset();tex_fence[0]=0;pending_fences[4]=true;assert(draw_frame(0));assert(uploaded_texture==0);
    AVFrame f={0};
    reset(); slot_pending=1; consume_during_copy=1; publish(&f);
    assert(slot_pending==2 && stats.dropped==0 && slot_rendering==0);
    reset(); slot_pending=1; publish(&f);
    assert(slot_pending==2 && stats.dropped==1 && stats.render_superseded==1 && slot_rendering==0);
}
}
namespace input_test {
#define MAX_PADS 4
#define SEND_INTERVAL_MS 4
struct pad {bool present;int buttons;unsigned char lt,rt;short lx,ly,rx,ry;};
struct input_session {};
static volatile LONG stream_active,quit_requested;
static struct {int quit_combo;} stream_settings;
static int steps, releases, slept[5], polls;
static bool test_cover;static int cover_commands,remote_forwards;
static bool launch_active(){return test_cover && polls<=2;}
static void launch_command(bool cancel,bool reveal){assert(cancel && reveal);cover_commands++;}
/* This fixture tests cadence; remote key/mouse behavior has its own state tests. */
typedef unsigned char BYTE;typedef unsigned short WORD;
enum {XINPUT_MAX_KEYBOARD_STATE=6,ERROR_SUCCESS=0,
 XINPUT_MODIFIER_LSHIFT=1,XINPUT_MODIFIER_RSHIFT=2,XINPUT_MODIFIER_LCONTROL=4,XINPUT_MODIFIER_RCONTROL=8,
 XINPUT_MODIFIER_LALT=16,XINPUT_MODIFIER_RALT=32,XINPUT_MODIFIER_LGUI=64,XINPUT_MODIFIER_RGUI=128};
struct XINPUT_KEYBOARDSTATE {WORD VKCodes[6];BYTE bModifiers;};
static int XInputGetKeyboardState(DWORD,XINPUT_KEYBOARDSTATE *state){memset(state,0,sizeof(*state));if(!test_cover)return 1;if(polls<=3)state->VKCodes[0]=65;return ERROR_SUCCESS;}
struct remote_input {bool mouse[4],suppress[4];int speed,acceleration,scroll_ms;};
struct quick_menu {bool open,neutral,latch,muted,stats;int volume;};
static int pointer_speed=100,pointer_acceleration,pointer_scroll_ms=100;
static bool initial_stats,physical_connected[4]={true,true,true,true};
static volatile LONG quick_available=1,mouse_available=1,mouse_mode,quick_snapshot;
static int input_exit_mask(int){return 0;}
static bool quick_step(quick_menu*,pad*,bool,int,bool*,int*,DWORD){return false;}
static unsigned quick_pack(quick_menu*){return 0;}
static void remote_mouse_release(remote_input*,int){}
static void remote_release(remote_input*){}
static void remote_keys(remote_input*,bool*,bool enabled){if(test_cover && polls<=4)assert(!enabled);}
static void remote_pad(remote_input*,pad*,int port,DWORD,bool,int){if(test_cover && !port){assert(polls==5);remote_forwards++;}}
static void audio_set_volume(int){}
static volatile LONG rumble_connected_mask;
static LONG InterlockedExchange(volatile LONG *p,LONG n){LONG old=*p;*p=n;return old;}
static LONG InterlockedCompareExchange(volatile LONG *p,LONG n,LONG old){LONG value=*p;if(value==old)*p=n;return value;}
static bool gfx_system_ui_active(){return false;}
static void input_rumble(unsigned short,unsigned short,unsigned short){}
static DWORD tick;
static const DWORD costs[]={0,1,3,4,17};
static DWORD GetTickCount() { return tick; }
static int input_poll(pad *pads) {memset(pads,0,4*sizeof(*pads));tick+=test_cover?17:costs[polls];polls++;if(test_cover){pads[0].present=true;pads[0].buttons=polls<=3?(B_FLAG|X_FLAG):polls==5?A_FLAG:0;}return 1; }
static bool input_session_step(input_session*,pad *pads,int,DWORD,void*) {if(test_cover){if(polls<=4)assert(!pads[0].buttons);else assert(pads[0].buttons==A_FLAG);}return ++steps==3;}
static void input_session_release(input_session*) { releases++; }
static void Sleep(DWORD delay) {
    assert(delay>=1 && delay<=4); slept[polls-1]=(int)delay;tick+=delay;
    if(polls==5) stream_active=false;
}
/* ACTUAL_INPUT */
static void test() {
    for(int pass=0;pass<2;pass++) {
        tick=pass ? UINT32_MAX-2 : 0;polls=steps=releases=0;
        stream_active=true; quit_requested=false;
        input_stream_main(NULL);
        const int expected[]={4,3,1,1,1};
        assert(polls==5 && steps==5 && releases==1 && quit_requested);
        for(int i=0;i<5;i++) assert(slept[i]==expected[i]);
    }
    test_cover=true;tick=0;polls=steps=releases=cover_commands=remote_forwards=0;stream_active=true;quit_requested=false;
    input_stream_main(NULL);assert(cover_commands==2 && remote_forwards==1 && polls==5 && releases==1);
}
}
int main() {
    render_test::test();input_test::test();
    puts("PASS: actual draw selects newest and a free texture set; independent fences in all 8 preference/busy combinations; no busy reuse or fence wait; failure cleanup; overlay-OFF no clocks.");
    puts("PASS: actual input cadence includes work, yields after overrun, tick wrap, quit and release.");
}
