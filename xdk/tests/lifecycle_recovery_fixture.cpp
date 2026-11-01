#include "../src/wake.h"
#include "../src/host_compat.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Limelight.h>

namespace guide_test {
static bool guide,locked,owned,close_on_acquire,streaming;
static unsigned presents,clears;
static int dev_lock;
#define D3DCLEAR_TARGET 1
struct D3DDevice {
    void AcquireThreadOwnership(){assert(locked&&!owned);owned=true;if(close_on_acquire)guide=false;}
    void ReleaseThreadOwnership(){assert(owned);owned=false;}
    void Clear(int,void*,int,unsigned,float,int){assert(owned);clears++;}
    void Present(void*,void*,void*,void*){assert(owned&&guide);presents++;}
};
static D3DDevice device,*dev=&device;
static void EnterCriticalSection(int*){assert(!locked);locked=true;}
static bool TryEnterCriticalSection(int*){if(locked)return false;locked=true;return true;}
static void LeaveCriticalSection(int*){assert(locked&&!owned);locked=false;}
static bool gfx_system_ui_active(){return guide;}
static bool video_xdk_is_streaming(){return streaming;}
/* GUIDE */
static void test(){
    for(int i=0;i<100;i++) {
        guide=true;system_ui_present(false);assert(!owned&&!locked);
        // Teardown may acquire the GPU while Guide is still open.
        assert(gfx_begin()==dev);gfx_end();assert(guide);
    }
    assert(presents==100&&clears==100);
    guide=false;system_ui_present(false);assert(presents==100&&!locked);
    guide=true;streaming=true;system_ui_present(true);assert(presents==100&&!locked);
    system_ui_present(false);assert(presents==101&&!locked); // stream renderer heartbeat
    streaming=false;system_ui_present(true);assert(presents==102&&!locked); // idle fallback
    locked=true;system_ui_present(false);assert(presents==102&&locked);locked=false;
    guide=false;system_ui_present(true);assert(presents==102&&!locked);
}
}

namespace render_pump_test {
typedef unsigned DWORD;typedef void *LPVOID;
typedef unsigned LONG;
#define WINAPI
#define D3DRS_PRESENTINTERVAL 1
#define D3DPRESENT_INTERVAL_ONE 1
#define D3DPRESENT_INTERVAL_IMMEDIATE 0
enum {PIPE_LEGACY,PIPE_CACHED,PIPE_SWITCHING,PIPE_DISABLED};
static bool running,guide,owned;
static unsigned waits,heartbeats,draws,tick;
static int frame_event,mailbox_lock,slot_pending=-1,slot_rendering=-1;
static struct {int color_space;bool vsync;} options;
static struct {unsigned rendered,dropped,avg_render_us;} stats;
struct D3DDevice {
    void SetPixelShaderConstantF(int,const float*,int){assert(owned);}
    void SetRenderState(int,int){assert(owned);}
};
static D3DDevice device;
static D3DDevice *gfx_begin(){assert(!owned);owned=true;return &device;}
static void gfx_end(){assert(owned);owned=false;}
static void xdk_audit_thread(const char*){}
static void video_yuv_coefficients(int,float*){}
static bool gfx_system_ui_active(){return guide;}
static void gfx_present_system_ui(){assert(guide&&!owned);heartbeats++;}
enum {WAIT_TIMEOUT=258};
static DWORD WaitForSingleObject(int,unsigned timeout){
    assert(!owned&&timeout<=32);waits++;tick+=timeout;
    if(waits==1)guide=true; // Guide arrives during a timeout with no frames.
    if(waits==5)guide=false;
    if(waits==7)running=false;
    return WAIT_TIMEOUT;
}
static int current_pipeline(){return PIPE_CACHED;}
static void activate_legacy_fallback(){assert(false);}
static uint64_t now_us(){return tick*1000;}
static int draw_cached_frame(){assert(!guide);draws++;return 1;}
static bool draw_frame(int){assert(!guide);return true;}
static void EnterCriticalSection(int*){}
static void LeaveCriticalSection(int*){}
static unsigned InterlockedIncrement(volatile unsigned *p){return ++*p;}
static unsigned stream_smooth(unsigned,unsigned n){return n;}
static bool gfx_launch_pending(){return true;}
static int cover_heartbeats;
static void gfx_launch_idle(D3DDevice*){assert(owned && !guide);cover_heartbeats++;}
/* RENDER_PUMP */
static void test(){
    running=true;render_main(NULL);
    assert(!owned&&waits==7&&heartbeats==4&&draws==2&&stats.rendered==2 && cover_heartbeats==0);
}
}

namespace resize_test {
typedef long LONG;
enum {VIDEO_STREAM_OK,VIDEO_STREAM_UNSUPPORTED_SIZE,VIDEO_STREAM_RESOURCE_ERROR};
enum {PIPE_AUTO,PIPE_CACHED,PIPE_LEGACY,PIPE_SWITCHING,PIPE_DISABLED};
static int vid_w=1280,vid_h=720,chroma_w=640,chroma_h=360,requested_pipeline;
static volatile LONG stream_error,active_pipeline;
static bool joined,freed,cached_ok,legacy_ok,fail_thread;
static int stops,starts,tex_next;
static uint64_t streaming_start_us=123,streaming_end_us;
static unsigned char *cap_buf;
static unsigned cap_len,cap_frames;
static void vlog(const char*,...){}
static LONG InterlockedExchange(volatile LONG *p,LONG n){LONG old=*p;*p=n;return old;}
static int video_get_stream_error(){return stream_error;}
static void video_stop(){joined=true;stops++;streaming_end_us=900;}
static void free_video_buffers(){assert(joined);freed=true;}
static bool cached_video_init(int w,int h){assert(joined&&freed&&w==vid_w&&h==vid_h);return cached_ok;}
static bool create_legacy_video(){assert(joined&&freed);return legacy_ok;}
static void video_start(){assert(joined&&freed);joined=false;freed=false;starts++;streaming_start_us=999;streaming_end_us=0;if(fail_thread)stream_error=VIDEO_STREAM_RESOURCE_ERROR;}
/* RESIZE */
static void test(){
    cap_buf=(unsigned char*)malloc(16);cap_len=16;cap_frames=1;
    cached_ok=true;assert(resize_video(1920,1080));assert(stops==1&&starts==1&&active_pipeline==PIPE_CACHED);
    assert(!cap_buf&&!cap_len&&!cap_frames);
    assert(vid_w==1920&&vid_h==1080&&chroma_w==960&&chroma_h==540&&streaming_start_us==123);
    cached_ok=false;legacy_ok=true;assert(resize_video(853,479));assert(active_pipeline==PIPE_LEGACY);
    assert(chroma_w==427&&chroma_h==240&&streaming_start_us==123);
    assert(!resize_video(3840,2160)&&stream_error==VIDEO_STREAM_UNSUPPORTED_SIZE&&stops==2);
    stream_error=0;assert(!resize_video(0,720)&&stops==2);
    stream_error=0;legacy_ok=false;assert(!resize_video(1280,720));assert(joined&&freed&&stream_error==VIDEO_STREAM_RESOURCE_ERROR&&active_pipeline==PIPE_DISABLED);
    stream_error=0;legacy_ok=true;fail_thread=true;assert(!resize_video(1280,720)&&stream_error==VIDEO_STREAM_RESOURCE_ERROR);
}
}

namespace launch_test {
#define UNIQUE_ID "test-client"
struct http_response {char *body;};
static gs_error_code last_error_kind;static void plat_log(const char*){}
struct gs_server {gs_host_metadata metadata;char uuid[80];int current_game;bool launch_resumed,is_nvidia;char *rtsp_session_url;SERVER_INFORMATION info;};
static char last_error[256],endpoint[1024];
static int actual_game,requests;
static bool info_ok=true,reject;
static void set_error(const char *fmt,const char *arg){snprintf(last_error,sizeof(last_error),fmt,arg?arg:"");}
static bool load_serverinfo(gs_server *s,bool secure){assert(secure);s->current_game=actual_game;return info_ok;}
static void crypto_random(void *p,size_t n){memset(p,1,n);}
static void bytes_to_hex(const unsigned char*,char *out,size_t n){memset(out,'1',n*2);out[n*2]=0;}
static void make_uuid(char out[37]){memset(out,'a',36);out[36]=0;}
static const char *LiGetLaunchUrlQueryParameters(){return "";}
static bool request(gs_server*,bool secure,const char *path,int timeout,http_response *r){
    assert(secure&&timeout==15000);requests++;snprintf(endpoint,sizeof(endpoint),"%s",path);
    r->body=_strdup(reject?"<root status_code=\"500\"></root>":"<root status_code=\"200\"><gamesession>1</gamesession><sessionUrl0>rtsp://new</sessionUrl0></root>");return true;
}
static bool xml_status_ok(const char *s){return strstr(s,"status_code=\"200\"")!=NULL;}
static void http_response_free(http_response *r){free(r->body);r->body=NULL;}
/* LAUNCH */
static void test(){
    gs_server s={};STREAM_CONFIGURATION cfg={};cfg.width=1280;cfg.height=720;cfg.fps=60;
    // Stale BUSY after the host restarted must launch, not resume.
    s.current_game=42;actual_game=0;assert(gs_launch(&s,&cfg,42,true,false));assert(!strncmp(endpoint,"/launch?",8));
    assert(s.current_game==42&&s.info.rtspSessionUrl==s.rtsp_session_url&&!strcmp(s.rtsp_session_url,"rtsp://new"));
    // Stale IDLE after a local crash must resume the existing session.
    s.current_game=0;actual_game=42;assert(gs_launch(&s,&cfg,42,true,false));assert(!strncmp(endpoint,"/resume?",8));
    int before=requests;actual_game=7;assert(!gs_launch(&s,&cfg,42,true,false)&&requests==before&&s.current_game==7);
    assert(strstr(last_error,"Another app"));
    info_ok=false;assert(!gs_launch(&s,&cfg,42,true,false)&&requests==before);info_ok=true;
    actual_game=42;reject=true;char *old=s.rtsp_session_url;assert(!gs_launch(&s,&cfg,42,true,false)&&s.rtsp_session_url==old);
    free(s.rtsp_session_url);
}
}
int main(){guide_test::test();render_pump_test::test();resize_test::test();launch_test::test();puts("PASS: nonblocking Guide ownership, idle/stream presenter exclusion, actual render heartbeat without frames and resume; resize lifetimes; stale host launch/resume.");}
