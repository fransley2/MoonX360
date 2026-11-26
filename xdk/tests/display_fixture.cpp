#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include "../src/config.h"
#include "../src/display_policy.h"
#include "../src/ui_layout_geometry.h"
static int selected_mode,selected_layout,stream_width,stream_height;
void config_load(config *c){
    memset(c,0,sizeof(*c));c->display_mode=selected_mode;c->ui_layout=selected_layout;
    c->width=stream_width;c->height=stream_height;
}
typedef uint32_t DWORD;
typedef long LONG;
static void InterlockedExchange(volatile LONG *dest,LONG value){*dest=value;}
static void plat_log(const char *){}
static bool reject_layout_resize;
static bool ui_init_layout(int,int,int){return !reject_layout_resize;}
typedef unsigned UINT;
#define FAILED(x) ((x)<0)
#define FALSE 0
#define TRUE 1
#define ZeroMemory(p,n) memset(p,0,n)
enum {UI_LOGICAL_WIDTH=1280,UI_LOGICAL_HEIGHT=720,GFX_WIDTH=1280,GFX_HEIGHT=720};
enum {D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,D3DDECLUSAGE_TEXCOORD,
 D3D_SDK_VERSION,D3DFMT_X8R8G8B8,D3DMULTISAMPLE_NONE,D3DSWAPEFFECT_DISCARD,D3DPRESENT_INTERVAL_ONE,
 D3DDEVTYPE_HAL,D3DCREATE_HARDWARE_VERTEXPROCESSING,D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,
 D3DTEXF_LINEAR,D3DTEXF_NONE,D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP,
 D3DRS_CULLMODE,D3DCULL_NONE,D3DRS_ZENABLE,D3DRS_HALFPIXELOFFSET,D3DRS_ALPHABLENDENABLE,D3DPT_TRIANGLESTRIP};
enum {GFX_SHADER_RGB,GFX_SHADER_YUV_FAST,GFX_SHADER_YUV_QUALITY,GFX_SHADER_RGBA,GFX_SHADER_CRT_CLARITY,GFX_SHADER_YUV_SMOOTH,GFX_SHADER_YUV_SMOOTH_CRT,GFX_SHADER_YUV_BICUBIC,GFX_SHADER_YUV_POST_DEBLOCK,GFX_SHADER_YUV_SHARP_BILINEAR};
struct vertex {float x,y,u,v;};
struct D3DVERTEXELEMENT9 {int a,b,c,d,e,f;};
#define D3DDECL_END() {0,0,0,0,0,0}
struct D3DPRESENT_PARAMETERS {
    int BackBufferWidth,BackBufferHeight,BackBufferFormat,BackBufferCount,MultiSampleType,
        EnableAutoDepthStencil,SwapEffect,PresentationInterval;
};
struct XVIDEO_MODE {DWORD dwDisplayWidth,dwDisplayHeight;int fIsInterlaced;float RefreshRate;int fIsWideScreen;};
struct LARGE_INTEGER {int64_t QuadPart;};
static LARGE_INTEGER gfx_frequency;
static int backbuffer_width=1280,backbuffer_height=720,display_width=1280,display_height=720,dev_lock;
static int saved_ui_bb_width=1280,saved_ui_bb_height=720;
static bool crt_240p_bb_active=false;
static int layout_width=1280,layout_height=720,ui_layout_choice;
static volatile LONG stream_layout_width;
static XVIDEO_MODE video_mode={1920,1080,0,60.0f,1};
static float display_refresh;
static bool display_is_interlaced,display_mode_known,display_is_widescreen=true;
static int active_display_mode=0;
static int ui_safe_area;
static bool reject_1080,reject_all,reject_quality,half_pixel,reject_crt,reject_smooth,reject_sharp;
static int d3d_releases,attempts;
static std::vector<int> widths;
static vertex drawn[4];
struct D3DVertexDeclaration {};
struct D3DVertexShader {};
struct D3DPixelShader {};
static D3DVertexDeclaration declaration,*decl;
static D3DVertexShader vertex_shader,*vs;
static D3DPixelShader pixel_shaders[10],*ps[10];
struct D3DDevice {
    void Release() {}
    int CreateVertexDeclaration(const D3DVERTEXELEMENT9*,D3DVertexDeclaration **p) { *p=&declaration;return 0; }
    int CreateVertexShader(const DWORD*,D3DVertexShader **p) { *p=&vertex_shader;return 0; }
    int CreatePixelShader(const DWORD *code,D3DPixelShader **p) { unsigned idx=code?*code%10u:0u;*p=&pixel_shaders[idx];return 0; }
    void SetSamplerState(int,int,int) {}
    void SetRenderState(int name,int value) {
        if(name==D3DRS_HALFPIXELOFFSET)half_pixel=value!=0;
        if(name==D3DRS_ZENABLE)assert(!value);
    }
    void ReleaseThreadOwnership() {}
    void SetVertexDeclaration(D3DVertexDeclaration*) {}
    void SetVertexShader(D3DVertexShader*) {}
    void SetPixelShader(D3DPixelShader *p) { assert(p); }
    void DrawPrimitiveUP(int,int,const void *v,size_t stride) { assert(stride==sizeof(vertex));memcpy(drawn,v,sizeof(drawn)); }
};
static D3DDevice device,*dev;
struct Direct3D {
    int CreateDevice(int,int,void*,int,const D3DPRESENT_PARAMETERS *p,D3DDevice **out) {
        attempts++;widths.push_back(p->BackBufferWidth);
        assert(p->MultiSampleType==D3DMULTISAMPLE_NONE && !p->EnableAutoDepthStencil);
        if(reject_all || (reject_1080 && p->BackBufferWidth==1920)) { *out=NULL;return -1; }
        *out=&device;return 0;
    }
    void Release() { d3d_releases++; }
};
static Direct3D direct;
static Direct3D *Direct3DCreate9(int) { return &direct; }
static void QueryPerformanceFrequency(LARGE_INTEGER *v) { v->QuadPart=1000000; }
static void XGetVideoMode(XVIDEO_MODE *m) { *m=video_mode; }
static void InitializeCriticalSection(int *p) { *p=1; }
static const char *vs_src="vs",*ps_rgb_src="rgb",*ps_rgba_src="rgba",*ps_yuv_src="fast",*ps_yuv_quality_src="quality",*ps_yuv_crt_src="crt",*ps_yuv_smooth_src="smooth",*ps_yuv_smooth_crt_src="smooth_crt",*ps_yuv_bicubic_src="bicubic",*ps_yuv_post_deblock_src="deblock",*ps_yuv_sharp_bilinear_src="sharp";
struct ID3DXBuffer {DWORD code;const DWORD *GetBufferPointer(){return &code;}void Release(){delete this;} };
static ID3DXBuffer *compile(const char *s,const char*) { return (reject_quality && !strcmp(s,"quality")) || (reject_crt && !strcmp(s,"crt")) || (reject_smooth && (!strcmp(s,"smooth") || !strcmp(s,"smooth_crt"))) || (reject_sharp && !strcmp(s,"sharp")) ? NULL : new ID3DXBuffer; }
/* ACTUAL_INIT */
/* ACTUAL_RECT */
/* ACTUAL_LAYOUT */
/* ACTUAL_CRT_AVAILABLE */
static void reset() {
    dev=NULL;memset(ps,0,sizeof(ps));widths.clear();attempts=d3d_releases=0;crt_240p_bb_active=false;
    stream_layout_width=0;reject_layout_resize=false;
    selected_mode=0;selected_layout=UI_LAYOUT_AUTO;stream_width=1280;stream_height=720;video_mode.fIsWideScreen=1;
    reject_1080=reject_all=reject_quality=half_pixel=reject_crt=reject_smooth=reject_sharp=false;display_mode_known=false;video_mode.fIsInterlaced=0;video_mode.RefreshRate=60.0f;video_mode.dwDisplayWidth=1920;video_mode.dwDisplayHeight=1080;
}
int main() {
    reset();assert(gfx_init());assert(attempts==1 && backbuffer_width==1920 && backbuffer_height==1080 && half_pixel);
    assert(display_refresh==60.0f && !display_is_interlaced);
    gfx_draw_rect(dev,GFX_SHADER_RGB,0,0,1280,720);
    assert(drawn[0].x==-1 && drawn[0].y==1 && drawn[3].x==1 && drawn[3].y==-1);
    gfx_draw_rect(dev,GFX_SHADER_RGB,640,360,1280,720);
    assert(drawn[0].x==0 && drawn[0].y==0); // physical center 960,540; logical UI stays 1280x720
    reset();reject_1080=true;assert(gfx_init());
    assert(attempts==2 && widths[0]==1920 && widths[1]==1280 && backbuffer_height==720 && display_height==1080);
    reset();video_mode.dwDisplayWidth=1280;video_mode.dwDisplayHeight=720;
    assert(gfx_init() && attempts==1 && backbuffer_width==1280);
    reset();selected_mode=1;assert(gfx_init() && backbuffer_width==848 && backbuffer_height==480 && display_width==1920);
    reset();selected_mode=2;assert(gfx_init() && backbuffer_width==1280 && display_width==1920);
    reset();video_mode.dwDisplayWidth=720;video_mode.dwDisplayHeight=480;assert(gfx_init() && backbuffer_width==848 && backbuffer_height==480);
    // Auto UI layout follows display configuration, not stream resolution.
    reset();stream_width=800;stream_height=600;assert(gfx_init() && backbuffer_width==1920 && layout_width==1280 && layout_height==720);
    reset();stream_width=1280;stream_height=720;video_mode.dwDisplayWidth=720;video_mode.dwDisplayHeight=480;
    video_mode.fIsWideScreen=0;assert(gfx_init() && backbuffer_width==640 && backbuffer_height==480 && layout_width==960);
    reset();video_mode.dwDisplayWidth=1024;video_mode.dwDisplayHeight=768;video_mode.fIsWideScreen=0;
    assert(gfx_init() && backbuffer_width==640 && backbuffer_height==480 && layout_width==960);
    reset();selected_layout=UI_LAYOUT_1_1;assert(gfx_init() && backbuffer_width==1920 && layout_width==720);
    ui_configure_layout(UI_LAYOUT_AUTO,800,600);
    assert(gfx_layout_width()==1280 && backbuffer_width==1920);
    ui_configure_layout(UI_LAYOUT_4_3,800,600);
    assert(gfx_layout_width()==960 && backbuffer_width==1920);
    gfx_draw_layout_rect(dev,GFX_SHADER_RGB,0,0,960,720);
    assert(drawn[0].x==-0.75f && drawn[0].y==1 && drawn[3].x==0.75f && drawn[3].y==-1);
    gfx_set_stream_layout(512,512);
    assert(gfx_layout_width()==960);
    gfx_set_stream_layout(0,0);
    assert(gfx_layout_width()==960);
    gfx_draw_rect_uv(dev,GFX_SHADER_YUV_FAST,0,0,1280,720,0.125f,0.0f,0.875f,1.0f);
    assert(drawn[0].u==0.125f && drawn[0].v==0.0f && drawn[3].u==0.875f && drawn[3].v==1.0f);
    reject_layout_resize=true;
    ui_configure_layout(UI_LAYOUT_16_9,800,600);
    assert(gfx_layout_width()==960 && ui_layout_choice==UI_LAYOUT_4_3);
    reset();reject_quality=true;assert(gfx_init() && !ps[GFX_SHADER_YUV_QUALITY] && ps[GFX_SHADER_YUV_FAST]);
    reset();video_mode.dwDisplayWidth=640;video_mode.dwDisplayHeight=480;video_mode.fIsInterlaced=1;video_mode.RefreshRate=59.94f;
    selected_mode=4;assert(gfx_init() && gfx_crt_clarity_available() && backbuffer_width==640);
    video_mode.fIsInterlaced=0;assert(gfx_init() && !gfx_crt_clarity_available());
    reset();reject_crt=true;assert(gfx_init() && !ps[GFX_SHADER_CRT_CLARITY] && ps[GFX_SHADER_YUV_FAST]);
    reset();reject_sharp=true;assert(gfx_init() && !ps[GFX_SHADER_YUV_SHARP_BILINEAR] && ps[GFX_SHADER_YUV_FAST]);
    gfx_draw_rect(dev,GFX_SHADER_YUV_SHARP_BILINEAR,0,0,1280,720);
    crt_240p_bb_active=true;video_mode.dwDisplayWidth=640;video_mode.dwDisplayHeight=480;video_mode.fIsInterlaced=1;video_mode.RefreshRate=59.94f;
    selected_mode=4;display_mode_known=true;display_height=480;display_is_interlaced=true;display_refresh=59.94f;
    assert(!gfx_crt_clarity_available());crt_240p_bb_active=false;
    reset();video_mode.dwDisplayWidth=video_mode.dwDisplayHeight=0;assert(gfx_init() && !display_mode_known && !gfx_crt_clarity_available());
    reset();reject_smooth=true;assert(gfx_init() && !ps[GFX_SHADER_YUV_SMOOTH] && !ps[GFX_SHADER_YUV_SMOOTH_CRT] && ps[GFX_SHADER_YUV_FAST]);
    reset();reject_all=true;assert(!gfx_init() && d3d_releases==1);
    puts("PASS: actual gfx_init/quad: configured physical 1080p, 720p fallback, no MSAA/depth, shader fallback, logical UI transform and texel-center state.");
}
