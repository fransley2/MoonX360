#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "../src/ui/canvas.h"
#include "../src/ui/font.h"
#include "../src/ui/ui.h"
static bool mouse_enabled;
static bool input_mouse_active(){return mouse_enabled;}
static void input_mouse_indicator_available(bool){}
#include "../src/video.h"
#include "../src/video_session.h"
#include "../src/video_gpu_policy.h"
#include "../src/stream_metrics.h"
#include "../src/decode_deadline.h"

typedef uint32_t DWORD;
#define FAILED(hr) ((hr) < 0)
#define D3DFMT_LIN_A8R8G8B8 0
#define GFX_SHADER_RGB 0
#define GFX_SHADER_RGBA 3
#define FALSE 0
#define TRUE 1
typedef int D3DRENDERSTATETYPE;
#define D3DRS_ALPHABLENDENABLE 1
#define D3DRS_SRCBLEND 2
#define D3DRS_DESTBLEND 3
#define D3DRS_BLENDOP 4
#define D3DRS_SEPARATEALPHABLENDENABLE 5
#define D3DBLENDOP_ADD 0
#define D3DBLEND_SRCALPHA 4
#define D3DBLEND_INVSRCALPHA 5
static bool fail_allocation, fail_texture, fail_upload, pending;
static int allocations, releases, uploads, waits, quads;
struct D3DTexture {
    std::vector<uint32_t> pixels;
    int w,h;
    D3DTexture(int width,int height) : pixels(width*height),w(width),h(height) { allocations++; }
    void Release() { assert(!pending); releases++; delete this; }
};
struct D3DDevice {
    D3DTexture *bound;DWORD state[6];
    void GetRenderState(int key,DWORD *value){*value=state[key];}
    void SetRenderState(int key,DWORD value){state[key]=value;}
    int CreateTexture(int w, int h, int, int, int, int, D3DTexture **out, void *) {
        assert(w>=64 && w<=1248 && h>=32 && h<=688);
        *out = fail_texture ? NULL : new D3DTexture(w,h);
        return fail_texture ? -1 : 0;
    }
    void BlockOnFence(DWORD fence) { assert(fence); waits++; pending = false; }
    void SetTexture(int slot, D3DTexture *texture) { assert(slot == 0); bound = texture; }
};
static D3DDevice device;
static D3DDevice *dev = &device;
static bool gfx_upload(D3DTexture *texture, const void *src, int pitch, int bytes, int rows)
{
    assert(!pending && pitch == texture->w * 4 && bytes == pitch && rows == texture->h);
    if (fail_upload) return false;
    memcpy(texture->pixels.data(), src, texture->pixels.size() * sizeof(uint32_t));
    uploads++;
    return true;
}
static void gfx_draw_rect(D3DDevice *d, int shader, float x0, float y0, float x1, float y1)
{
    assert(d->bound && shader == GFX_SHADER_RGBA);
    assert(d->state[1]==TRUE && d->state[2]==D3DBLEND_SRCALPHA && d->state[3]==D3DBLEND_INVSRCALPHA && d->state[4]==D3DBLENDOP_ADD && d->state[5]==FALSE);
    assert((x0==16 && y0==16 && x1==d->bound->w+16 && y1==d->bound->h+16) || (x0==1264-d->bound->w && y0==704-d->bound->h && x1==1264 && y1==704));
    pending = true; quads++;
}
static bool overlay_cv_init(canvas_t *c, int w, int h)
{
    if (fail_allocation) return false;
    return cv_init(c, w, h);
}
#define cv_init overlay_cv_init
static bool gfx_system_ui_active(){return false;}
static bool gfx_fence_pending(DWORD f){return f && pending;}
static void gfx_wait_fence(D3DDevice *d,DWORD f){d->BlockOnFence(f);}
/* ACTUAL_OVERLAY */
#undef cv_init

static DWORD tick, overlay_last_ms;
static bool overlay_has_text;
static stream_observations observations;
static stream_counters overlay_previous;
static video_options options = {true, RENDERER_GPU, true, true, 40, 60000};
static video_stats stats;
static decode_deadline deadline_overlay;
static int vid_w = 1280, vid_h = 720, telemetry_lock;
static uint32_t network_rtt = 9, network_variance = 4;
static int network_video_queue, network_audio_ms = 40;
enum {TIME_DECODE,TIME_FRAME_COPY,TIME_TEXTURE_LOCK,TIME_TEXTURE_COPY,TIME_GPU_SUBMIT,TIME_FENCE_WAIT,TIME_PRESENT_WAIT,TIME_PACKET_PREPARE,TIME_CALLBACK_TOTAL,TIME_CALLBACK_GAP,TIME_COUNT};
static gpu_time_window pipeline_times[TIME_COUNT];
static uint32_t pipeline_ema[TIME_COUNT];
static int session_slices=8;
static void gfx_backbuffer_size(int *w,int *h) {*w=1920;*h=1080;}
static float gfx_display_hz() {return 60;}
static bool gfx_display_interlaced() {return false;}
static bool network_valid = true;
static int requested_shader;
static bool shader_diagnostic_override;
static gpu_quality_policy quality_policy;
static unsigned gpu_slot_starvation;
static bool gfx_quality_available() {return true;}
static int current_pipeline() {return 1;}
static const char *pipeline_name(int) {return "CPU_CACHED_TEXTURE";}
static DWORD GetTickCount() { return tick; }
static void EnterCriticalSection(int *) {}
static void LeaveCriticalSection(int *) {}
/* ACTUAL_FORMATTER */


// Composite straight-alpha HUD pixels over a varied scene for visual checks.
static void preview(const char *kind,int language,int output_height) {
    float scale=output_height/720.0f;int w=(int)(overlay_canvas.w*scale),h=(int)(overlay_canvas.h*scale);
    char name[100];snprintf(name,sizeof(name),"hud-%s-lang%d-%dp.ppm",kind,language,output_height);
    FILE *f=fopen(name,"wb");assert(f);fprintf(f,"P6\n%d %d\n255\n",w,h);
    for(int y=0;y<h;y++)for(int x=0;x<w;x++) {
        uint32_t p=overlay_canvas.px[(int)(y/scale)*overlay_canvas.w+(int)(x/scale)];unsigned a=p>>24;
        unsigned background[3]={(unsigned)(64+x*160/w),(unsigned)(90+y*130/h),(unsigned)(120+x*80/w)};
        unsigned char rgb[3];for(int c=0;c<3;c++)rgb[c]=(unsigned char)((((p>>(16-c*8))&255)*a+background[c]*(255-a)+127)/255);
        fwrite(rgb,1,3,f);
    }fclose(f);
}
static void check_text_fits(const char *text) {
    int lines=1;for(const char *p=text;*p;p++)if(*p=='\n')lines++;
    int size=lines<=5?22:lines<=9?20:18;
    for(const char *p=text;*p;){char line[212];size_t n=0;while(*p && *p!='\n'){assert(n+1<sizeof(line));line[n++]=*p++;}if(*p)p++;line[n]=0;
        assert(font_text_width(FONT_REGULAR,(float)size,line)<=overlay_canvas.w-24);}
}
int main(void) {
    assert(font_init());char text[1536];
    options.stats_overlay=false;strcpy(text,"untouched");assert(!overlay_text(text,sizeof(text)) && !strcmp(text,"untouched"));options.stats_overlay=true;
    stats.received=stats.decoded=40;stats.rendered=38;stats.avg_decode_us=24000;stats.decode_threads=4;
    deadline_overlay.queue_valid=true;deadline_overlay.queue_us=600;deadline_overlay.full_frames=123;deadline_overlay.fast_frames=4;
    stream_observe(&observations,100,1000000,41,100,2100,3100);
    for(int i=0;i<64;i++)gpu_time_add(&pipeline_times[TIME_DECODE],23000);
    pipeline_ema[TIME_FRAME_COPY]=1200;options.overlay_mode=2;tick=1000;
    assert(overlay_text(text,sizeof(text)));
    assert(strstr(text,"38.0 / 40 FPS") && strstr(text,"8.00 / 60.00 Mbps (actual / set)"));
    assert(strstr(text,"Network RTT: 9 ms") && strstr(text,"Host: 4.1 ms"));
    assert(strstr(text,"Decode: 24.00 ms | Budget: 25.00 ms") && strstr(text,"H.264 filter: Fast | Fast Decode: On"));
    assert(strstr(text,"GPU image: Auto Quality | Active: Quality"));
    assert(!strstr(text,"Connection:") && !strstr(text,"Pipeline"));
    tick=1999;assert(!overlay_text(text,sizeof(text)));
    gfx_overlay_draw(dev);assert(!quads);
    fail_allocation=true;assert(!gfx_overlay_set(text));fail_allocation=false;
    fail_texture=true;assert(!gfx_overlay_set(text) && !overlay_tex && !overlay_canvas.px);fail_texture=false;
    assert(gfx_overlay_set(text));assert(allocations==1 && uploads==1);
    unsigned translucent=0,opaque=0;for(size_t i=0;i<overlay_tex->pixels.size();i++){
        unsigned alpha=overlay_tex->pixels[i]>>24;assert(alpha>=184 && alpha<=255);translucent+=alpha==184;opaque+=alpha==255;
    }assert(translucent>100 && opaque>10);
    DWORD original[]={0,0,9,10,7,1};memcpy(dev->state,original,sizeof(original));
    gfx_overlay_draw(dev);assert(!memcmp(dev->state,original,sizeof(original)));gfx_overlay_mark_used(42);
    assert(gfx_overlay_set(text) && pending && uploads==1 && waits==0);
    pending=false;assert(gfx_overlay_set(text) && uploads==2);
    gfx_overlay_draw(dev);gfx_overlay_mark_used(43);gfx_overlay_clear(dev);assert(!pending && allocations==releases);
    assert(gfx_overlay_set(text));fail_upload=true;assert(!gfx_overlay_set(text));gfx_overlay_clear(dev);fail_upload=false;assert(allocations==releases);
    tick=2000;network_valid=false;observations.have_host=false;assert(overlay_text(text,sizeof(text)));
    assert(strstr(text,"Network RTT: N/A") && strstr(text,"Host: N/A"));
    options.fast_decode=false;deadline_overlay.recovery=true;tick+=1000;assert(overlay_text(text,sizeof(text)));
    assert(strstr(text,"H.264 filter: Reduced (recovery) | Fast Decode: Off"));
    deadline_overlay.recovery=false;tick+=1000;assert(overlay_text(text,sizeof(text)) && strstr(text,"H.264 filter: Full | Fast Decode: Off"));
    for(int cpu=0;cpu<2;cpu++)for(int shader=0;shader<3;shader++)for(int automatic=0;automatic<2;automatic++){
        options.fast_decode=cpu!=0;options.shader_mode=shader;requested_shader=shader;quality_policy.fast=automatic!=0;tick+=1000;
        assert(overlay_text(text,sizeof(text)));
        assert(strstr(text,cpu?"H.264 filter: Fast | Fast Decode: On":"H.264 filter: Full | Fast Decode: Off"));
        const char *effective=shader==1 || (shader==0 && automatic)?"Active: Fast":"Active: Quality";
        assert(strstr(text,effective));
    }
    shader_diagnostic_override=true;options.shader_mode=2;requested_shader=1;tick+=1000;
    assert(overlay_text(text,sizeof(text)) && strstr(text,"GPU image: Quality | Active: Fast (diagnostic)"));
    shader_diagnostic_override=false;options.shader_mode=requested_shader=0;quality_policy.fast=false;options.fast_decode=false;
    stats.avg_decode_us=26000;tick+=1000;assert(overlay_text(text,sizeof(text)) && strstr(text,"Decoder overloaded"));
    overlay_last_ms=UINT32_MAX-500;tick=499;assert(overlay_text(text,sizeof(text)));
    // Every mode and language fits at the intended size; no silent font shrinking.
    options.target_fps=30;options.bitrate_kbps=7000;stats.avg_decode_us=32200;
    deadline_overlay.queue_us=499000;deadline_overlay.recovery=true;network_valid=true;
    for(int language=0;language<3;language++)for(int mode=1;mode<=3;mode++){
        ui_set_language(language);options.overlay_mode=mode;tick+=1000;
        stats.received+=30;stats.decoded+=27;stats.rendered+=27;observations.payload_bytes+=750000;
        assert(overlay_text(text,sizeof(text)));
        if(mode==1)assert(!strstr(text,"p95"));
        if(mode==3)assert(strstr(text,"p95 / p99") && strstr(text,"256") && strstr(text,"CPU_CACHED_TEXTURE"));
        assert(gfx_overlay_set(text));check_text_fits(text);
        for(int height=480;height<=720;height+=240){assert((16+overlay_canvas.w)*height/720.0f<1280*height/720.0f);assert((16+overlay_canvas.h)*height/720.0f<height);
            preview(mode==1?"basic":mode==2?"detailed":"developer",language,height);}
        gfx_overlay_clear(dev);
    }
    ui_set_language(0);options.overlay_mode=2;
    // Tiny output buffers remain terminated; disabled path still does no work.
    tick+=1000;char tiny[12];assert(overlay_text(tiny,sizeof(tiny)) && tiny[sizeof(tiny)-1]==0);
    // Text-only pointer indicator: zero-alpha background, antialiased glyphs,
    // alpha-aware shader and exact restoration of every modified blend state.
    for(int language=0;language<3;language++){
        ui_set_language(language);ui_set_stream_hud(0,1);mouse_enabled=true;
        memcpy(dev->state,original,sizeof(original));gfx_mouse_draw(dev);
        assert(mouse_indicator && !memcmp(dev->state,original,sizeof(original)));
        unsigned clear=0,ink=0,edge=0;for(size_t i=0;i<mouse_indicator->pixels.size();i++){unsigned a=mouse_indicator->pixels[i]>>24;clear+=a==0;ink+=a==255;edge+=a>0 && a<255;}
        assert(clear>100 && ink>10 && edge>10);
        canvas_t saved=overlay_canvas;overlay_canvas.px=mouse_indicator->pixels.data();overlay_canvas.w=mouse_indicator->w;overlay_canvas.h=mouse_indicator->h;
        preview("mouse-text",language,480);preview("mouse-text",language,720);overlay_canvas=saved;
        int created=allocations;for(int i=0;i<20;i++)gfx_mouse_draw(dev);assert(allocations==created);
        pending=false;mouse_indicator->Release();mouse_indicator=NULL;
    }
    ui_set_stream_hud(0,2);int count=quads;gfx_mouse_draw(dev);assert(!mouse_indicator && count==quads);
    ui_set_stream_hud(0,0);gfx_mouse_draw(dev);assert(mouse_indicator);pending=false;mouse_indicator->Release();mouse_indicator=NULL;
    mouse_enabled=false;count=quads;gfx_mouse_draw(dev);assert(count==quads && allocations==releases);
    puts("PASS: translated user-facing FPS/bitrate/filter/recovery; 1Hz/wrap/N/A; alpha shader and blend state isolation;");
    puts("bounded GPU lifetime/failure fallback; readable 480p/720p HUDs; text-only mouse transparency and no per-frame allocations.");
}
