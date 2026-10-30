#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include "../src/config.h"
#include "../src/display_policy.h"
static int selected_mode;
void config_load(config *c){memset(c,0,sizeof(*c));c->display_mode=selected_mode;}
typedef uint32_t DWORD;
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
enum {GFX_SHADER_RGB,GFX_SHADER_YUV_FAST,GFX_SHADER_YUV_QUALITY};
struct vertex {float x,y,u,v;};
struct D3DVERTEXELEMENT9 {int a,b,c,d,e,f;};
#define D3DDECL_END() {0,0,0,0,0,0}
struct D3DPRESENT_PARAMETERS {
    int BackBufferWidth,BackBufferHeight,BackBufferFormat,BackBufferCount,MultiSampleType,
        EnableAutoDepthStencil,SwapEffect,PresentationInterval;
};
struct XVIDEO_MODE {DWORD dwDisplayWidth,dwDisplayHeight;int fIsInterlaced;float RefreshRate;};
struct LARGE_INTEGER {int64_t QuadPart;};
static LARGE_INTEGER gfx_frequency;
static int backbuffer_width=1280,backbuffer_height=720,display_width=1280,display_height=720,dev_lock;
static XVIDEO_MODE video_mode={1920,1080,0,60.0f};
static float display_refresh;
static bool display_is_interlaced;
static bool reject_1080,reject_all,reject_quality,half_pixel;
static int d3d_releases,attempts;
static std::vector<int> widths;
static vertex drawn[4];
struct D3DVertexDeclaration {};
struct D3DVertexShader {};
struct D3DPixelShader {};
static D3DVertexDeclaration declaration,*decl;
static D3DVertexShader vertex_shader,*vs;
static D3DPixelShader pixel_shaders[4],*ps[4];
struct D3DDevice {
    void Release() {}
    int CreateVertexDeclaration(const D3DVERTEXELEMENT9*,D3DVertexDeclaration **p) { *p=&declaration;return 0; }
    int CreateVertexShader(const DWORD*,D3DVertexShader **p) { *p=&vertex_shader;return 0; }
    int CreatePixelShader(const DWORD*,D3DPixelShader **p) { *p=&pixel_shaders[0];return 0; }
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
static const char *vs_src="vs",*ps_rgb_src="rgb",*ps_yuv_src="fast",*ps_yuv_quality_src="quality",*ps_rgba_src="rgba";
struct ID3DXBuffer {DWORD code;const DWORD *GetBufferPointer(){return &code;}void Release(){delete this;} };
static ID3DXBuffer *compile(const char *s,const char*) { return reject_quality && !strcmp(s,"quality") ? NULL : new ID3DXBuffer; }
/* ACTUAL_INIT */
/* ACTUAL_RECT */
static void reset() {
    dev=NULL;memset(ps,0,sizeof(ps));widths.clear();attempts=d3d_releases=0;
    selected_mode=0;reject_1080=reject_all=reject_quality=half_pixel=false;video_mode.dwDisplayWidth=1920;video_mode.dwDisplayHeight=1080;
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
    reset();video_mode.dwDisplayWidth=720;video_mode.dwDisplayHeight=480;assert(gfx_init() && backbuffer_height==480);
    reset();reject_quality=true;assert(gfx_init() && !ps[GFX_SHADER_YUV_QUALITY] && ps[GFX_SHADER_YUV_FAST]);
    reset();reject_all=true;assert(!gfx_init() && d3d_releases==1);
    puts("PASS: actual gfx_init/quad: configured physical 1080p, 720p fallback, no MSAA/depth, shader fallback, logical UI transform and texel-center state.");
}
