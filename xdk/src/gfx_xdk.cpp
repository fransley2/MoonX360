/*
 * Direct3D on the Xbox 360: one device with a physical-mode backbuffer,
 * shared by the logical 1280x720 UI (main
 * thread) and video presentation (render thread), a fullscreen-quad vertex
 * shader, and pixel shaders for RGB textures and YUV 4:2:0 planes.
 *
 * The device isn't thread-safe: each user takes it with gfx_begin(), which
 * also moves D3D's thread ownership to the caller.
 */
#include "xdk.h"
#include "../../src/performance.h"

#include <d3dx9shader.h>
#include <xgraphics.h>
#include <string.h>
#include <stdlib.h>

#include "../../src/ui/ui.h"
#include "../../src/ui/draw.h"
#include "../../src/config.h"
#include "../../src/ui/font.h"
#include "../../src/system_ui.h"
#include "../../src/display_policy.h"
#include "../../src/platform.h"
#include "../../src/input.h"

static D3DDevice *dev;
static CRITICAL_SECTION dev_lock;
static volatile LONG system_ui_active;
static HANDLE system_listener;
/* XAM draws the Guide during Present, including when no video arrives or the
 * main thread is joining the transport. Never wait for Guide close in gfx_begin:
 * that stops the very Presents needed to draw/close it (SDK TCR 022). */
static void system_ui_present(bool idle_only)
{
    /* The notification reader must remain able to observe Guide close even if
     * a renderer is inside D3D. It never waits for another device owner. */
    if(!TryEnterCriticalSection(&dev_lock))return;
    if(gfx_system_ui_active() && (!idle_only || !video_xdk_is_streaming())) {
        dev->AcquireThreadOwnership();
        dev->Clear(0,NULL,D3DCLEAR_TARGET,0xff000000,1.0f,0);
        dev->Present(NULL,NULL,NULL,NULL);
        dev->ReleaseThreadOwnership();
    }
    LeaveCriticalSection(&dev_lock);
}
void gfx_present_system_ui(void) {system_ui_present(false);}
static DWORD WINAPI system_ui_main(LPVOID)
{
    struct system_ui_state state={0};
    for(;;) {
        WaitForSingleObject(system_listener,state.active?16:32);
        DWORD id;ULONG_PTR value;
        while(XNotifyGetNext(system_listener,0,&id,&value))if(id==XN_SYS_UI && system_ui_change(&state,value!=0)) {
            InterlockedExchange(&system_ui_active,state.active?1:0);
            plat_log(state.active?"[SYSUI] opened; Guide presentation active\n":"[SYSUI] closed; video presentation resumed\n");
        }
        if(state.active)system_ui_present(true); /* stream renderer owns Guide Presents while running */
    }
}
bool gfx_system_ui_active(void) {return InterlockedCompareExchange(&system_ui_active,0,0)!=0;}
void gfx_system_ui_init(void)
{
    system_listener=XNotifyCreateListener(XNOTIFY_SYSTEM);
    if(!system_listener || system_listener==INVALID_HANDLE_VALUE){plat_log("[SYSUI] listener unavailable\n");return;}
    /* D3D/XAM run on this stack when presenting an idle menu's Guide. XDK
     * stacks cannot grow; overflow freezes the title (SDK CreateThread). */
    HANDLE worker=CreateThread(NULL,512*1024,system_ui_main,NULL,0,NULL);
    if(worker)CloseHandle(worker); /* process-lifetime listener, never a session-owned handle */
    else {CloseHandle(system_listener);system_listener=NULL;plat_log("[SYSUI] worker unavailable\n");}
}
static D3DVertexDeclaration *decl;
static D3DVertexShader *vs;
static D3DPixelShader *ps[4];
static int backbuffer_width=1280, backbuffer_height=720;
static int display_width=1280, display_height=720;
static float display_refresh;
static bool display_is_interlaced;
static LARGE_INTEGER gfx_frequency;

struct vertex {
    float x, y;     /* clip space */
    float u, v;
};

static const char vs_src[] =
    "struct V { float4 pos : POSITION; float2 uv : TEXCOORD0; };\n"
    "V main(V v) { return v; }\n";

static const char ps_rgb_src[] =
    "sampler rgb : register(s0);\n"
    "float4 main(float2 uv : TEXCOORD0) : COLOR { return float4(tex2D(rgb, uv).rgb, 1); }\n";

/* HUDs need texture alpha. Keep opaque RGB/video sampling unchanged. */
static const char ps_rgba_src[] =
    "sampler rgba : register(s0);\n"
    "float4 main(float2 uv : TEXCOORD0) : COLOR { return tex2D(rgba, uv); }\n";

/* Limited range: c0 contains the session matrix coefficients selected with
 * the SAME colorspace as the host request. Set once before presentation. */
static const char ps_yuv_src[] =
    "sampler ty : register(s0);\n"
    "sampler tu : register(s1);\n"
    "sampler tv : register(s2);\n"
    "float4 chroma : register(c0);\n"
    "float4 main(float2 uv : TEXCOORD0) : COLOR {\n"
    "    float y = 1.164383 * (tex2D(ty, uv).r - 0.062745);\n"
    "    float u = tex2D(tu, uv).r - 0.501961;\n"
    "    float v = tex2D(tv, uv).r - 0.501961;\n"
    "    return float4(y + chroma.x * v, y + chroma.y * u + chroma.z * v, y + chroma.w * u, 1);\n"
    "}\n";

/* Single pass: 5 luma + U/V, local contrast protects large edges.
 * The small luma residual is capped; chroma remains untouched. */
static const char ps_yuv_quality_src[] =
    "sampler ty : register(s0);\n"
    "sampler tu : register(s1);\n"
    "sampler tv : register(s2);\n"
    "float4 chroma : register(c0);\n"
    "float4 texel : register(c1);\n"
    "float4 main(float2 uv : TEXCOORD0) : COLOR {\n"
    " float c=tex2D(ty,uv).r;\n"
    " float4 n=float4(tex2D(ty,uv-float2(texel.x,0)).r,tex2D(ty,uv+float2(texel.x,0)).r,\n"
    "                 tex2D(ty,uv-float2(0,texel.y)).r,tex2D(ty,uv+float2(0,texel.y)).r);\n"
    " float4 diff=n-c;\n"
    " float4 weight=saturate(1-abs(diff)*(255.0/12.0));\n"
    " float smooth=c+0.20*dot(diff,weight)*0.25;\n"
    " float edge=max(max(abs(diff.x),abs(diff.y)),max(abs(diff.z),abs(diff.w)));\n"
    " float detail=c-dot(n,float4(0.25,0.25,0.25,0.25));\n"
    " float sharpen=clamp(detail,-2.0/255.0,2.0/255.0)*0.20*saturate(edge*(255.0/6.0))*saturate(1-edge*(255.0/32.0));\n"
    " float y=1.164383*(saturate(smooth+sharpen)-0.062745);\n"
    " float u=tex2D(tu,uv).r-0.501961;float v=tex2D(tv,uv).r-0.501961;\n"
    " return float4(y+chroma.x*v,y+chroma.y*u+chroma.z*v,y+chroma.w*u,1);\n"
    "}\n";

uint64_t gfx_now_us(void)
{
    LARGE_INTEGER t;QueryPerformanceCounter(&t);
    return (uint64_t)(t.QuadPart*1000000/gfx_frequency.QuadPart);
}
void gfx_backbuffer_size(int *w,int *h) { *w=backbuffer_width;*h=backbuffer_height; }
void gfx_display_size(int *w,int *h) { *w=display_width;*h=display_height; }
float gfx_display_hz(void) {return display_refresh;}
bool gfx_display_interlaced(void) {return display_is_interlaced;}
bool gfx_quality_available(void) { return ps[GFX_SHADER_YUV_QUALITY]!=NULL; }
bool gfx_fence_pending(DWORD fence) { return fence && D3DDevice_IsFencePending(fence)!=FALSE; }

static ID3DXBuffer *compile(const char *src, const char *profile)
{
    ID3DXBuffer *code = NULL, *errors = NULL;
    HRESULT hr = D3DXCompileShader(src, (UINT)strlen(src), NULL, NULL, "main", profile, 0, &code, &errors, NULL);

    if (errors) {
        OutputDebugStringA((const char *)errors->GetBufferPointer());
        errors->Release();
    }
    return SUCCEEDED(hr) ? code : NULL;
}

bool gfx_init(void)
{
    static const D3DVERTEXELEMENT9 elements[] = {
        { 0, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
        { 0, 8, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
        D3DDECL_END()
    };
    const char *ps_src[4] = { ps_rgb_src, ps_yuv_src, ps_yuv_quality_src, ps_rgba_src };
    Direct3D *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp;
    XVIDEO_MODE mode;
    ID3DXBuffer *code;
    int i;

    if (d3d == NULL)
        return false;
    QueryPerformanceFrequency(&gfx_frequency);
    ZeroMemory(&mode,sizeof(mode));XGetVideoMode(&mode);
    if (mode.dwDisplayWidth && mode.dwDisplayHeight) {
        display_width=(int)mode.dwDisplayWidth;display_height=(int)mode.dwDisplayHeight;
        display_refresh=mode.RefreshRate;display_is_interlaced=mode.fIsInterlaced!=FALSE;
    }
    struct config initial;config_load(&initial);
    display_render_size(initial.display_mode,display_width,display_height,&backbuffer_width,&backbuffer_height);
    ZeroMemory(&pp, sizeof(pp));
    pp.BackBufferWidth = backbuffer_width;
    pp.BackBufferHeight = backbuffer_height;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.MultiSampleType = D3DMULTISAMPLE_NONE;
    pp.EnableAutoDepthStencil = FALSE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    if (FAILED(d3d->CreateDevice(0, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev))) {
        if (dev) { dev->Release();dev=NULL; }
        backbuffer_width=UI_LOGICAL_WIDTH;backbuffer_height=UI_LOGICAL_HEIGHT;
        pp.BackBufferWidth=backbuffer_width;pp.BackBufferHeight=backbuffer_height;
        if (FAILED(d3d->CreateDevice(0,D3DDEVTYPE_HAL,NULL,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&dev))) {
            d3d->Release();return false;
        }
    }
    d3d->Release();

    if (FAILED(dev->CreateVertexDeclaration(elements, &decl)))
        return false;
    code = compile(vs_src, "vs_3_0");
    if (code == NULL || FAILED(dev->CreateVertexShader((const DWORD *)code->GetBufferPointer(), &vs)))
        return false;
    code->Release();
    for (i = 0; i < 4; i++) {
        code = compile(ps_src[i], "ps_3_0");
        if (code == NULL || FAILED(dev->CreatePixelShader((const DWORD *)code->GetBufferPointer(), &ps[i]))) {
            if (code) code->Release();
            if (i==GFX_SHADER_YUV_QUALITY) continue;
            return false;
        }
        code->Release();
    }
    for (i = 0; i < 3; i++) {
        dev->SetSamplerState(i, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        dev->SetSamplerState(i, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        dev->SetSamplerState(i, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        dev->SetSamplerState(i, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(i, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    }
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_HALFPIXELOFFSET, TRUE); /* SDK: DX10-style texel centers */
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);

    InitializeCriticalSection(&dev_lock);
    dev->ReleaseThreadOwnership();   /* gfx_begin() takes it per use */
    return true;
}

D3DDevice *gfx_begin(void)
{
    EnterCriticalSection(&dev_lock);
    dev->AcquireThreadOwnership();return dev;
}

void gfx_end(void)
{
    dev->ReleaseThreadOwnership();
    LeaveCriticalSection(&dev_lock);
}
void gfx_wait_fence(D3DDevice *d,DWORD fence)
{
    (void)d;
    while(gfx_fence_pending(fence)) {
        gfx_end();Sleep(1);gfx_begin(); /* let the Guide presenter run during cleanup */
    }
}

void gfx_draw_rect(D3DDevice *d, int shader, float x0, float y0, float x1, float y1)
{
    struct vertex q[4];
    float l = x0 / GFX_WIDTH * 2 - 1, r = x1 / GFX_WIDTH * 2 - 1;
    float t = 1 - y0 / GFX_HEIGHT * 2, b = 1 - y1 / GFX_HEIGHT * 2;

    q[0].x = l; q[0].y = t; q[0].u = 0; q[0].v = 0;
    q[1].x = r; q[1].y = t; q[1].u = 1; q[1].v = 0;
    q[2].x = l; q[2].y = b; q[2].u = 0; q[2].v = 1;
    q[3].x = r; q[3].y = b; q[3].u = 1; q[3].v = 1;
    d->SetVertexDeclaration(decl);
    d->SetVertexShader(vs);
    d->SetPixelShader(ps[shader]);
    d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(q[0]));
}

bool gfx_upload(D3DTexture *tex, const void *src, int src_pitch, int row_bytes, int rows)
{
    D3DLOCKED_RECT lr;
    const BYTE *s = (const BYTE *)src;
    BYTE *d;
    int y;

    if (tex == NULL || src == NULL || row_bytes <= 0 || rows <= 0 || src_pitch < row_bytes)
        return false;
    if (FAILED(tex->LockRect(0, &lr, NULL, 0)))
        return false;
    if (lr.pBits == NULL || lr.Pitch < row_bytes) {
        tex->UnlockRect(0);
        return false;
    }
    d = (BYTE *)lr.pBits;
    /* Usage=0 textures use write-combined GPU memory. XMemCpy requires
     * cached memory even when both pointers are 128-byte aligned. Keep the
     * OLD memcpy path here; staging planes in cached RAM may use XMemCpy. */
    if (lr.Pitch == src_pitch && src_pitch == row_bytes) {
        memcpy(d, s, (size_t)row_bytes * rows);
    } else {
        for (y = 0; y < rows; y++)
            memcpy(d + (size_t)y * lr.Pitch, s + (size_t)y * src_pitch, row_bytes);
    }
    tex->UnlockRect(0);
    return true;
}

bool gfx_upload_timed(D3DTexture *tex,const void *src,int src_pitch,int row_bytes,int rows,struct gfx_copy_cost *cost)
{
    if (!cost) return gfx_upload(tex,src,src_pitch,row_bytes,rows);
    D3DLOCKED_RECT lr;int y;
    if (!tex || !src || row_bytes<=0 || rows<=0 || src_pitch<row_bytes) return false;
    uint64_t t=gfx_now_us();ZeroMemory(&lr,sizeof(lr));
    if (FAILED(tex->LockRect(0,&lr,NULL,0))) return false;
    if (!lr.pBits || lr.Pitch<row_bytes) { tex->UnlockRect(0);return false; }
    uint64_t done=gfx_now_us();cost->lock_us+=(DWORD)(done-t);t=done;
    /* LEGACY write-combined destination: memcpy ONLY, never XMemCpy. */
    if (lr.Pitch==src_pitch && src_pitch==row_bytes) memcpy(lr.pBits,src,(size_t)row_bytes*rows);
    else for (y=0;y<rows;y++) memcpy((BYTE*)lr.pBits+(size_t)y*lr.Pitch,(const BYTE*)src+(size_t)y*src_pitch,row_bytes);
    done=gfx_now_us();cost->copy_us+=(DWORD)(done-t);t=done;
    bool ok=SUCCEEDED(tex->UnlockRect(0));cost->lock_us+=(DWORD)(gfx_now_us()-t);return ok;
}

/* XDK 21256.3 D3DLOCK documentation: keep CPU flush AND GPU invalidation
 * on UnlockRect. NOOVERWRITE avoids resource waits only because our slot
 * owner proves its previous unbound + InsertFence has completed. */
bool gfx_cached_upload(D3DTexture *texture,const BYTE *src,int src_pitch,
                       int width,int height,struct gfx_copy_cost *cost)
{
    D3DLOCKED_RECT lr;
    D3DSURFACE_DESC desc;
    uint64_t t=cost?gfx_now_us():0, copied;
    int y;
    if (!texture || !src || width<=0 || height<=0 || src_pitch<width ||
        !(texture->Common & D3DCOMMON_CPU_CACHED_MEMORY)) return false;
    texture->GetLevelDesc(0,&desc);
    if (desc.Format!=D3DFMT_LIN_L8 || desc.Width!=(UINT)width || desc.Height!=(UINT)height) return false;
    ZeroMemory(&lr,sizeof(lr));
    if (FAILED(texture->LockRect(0,&lr,NULL,D3DLOCK_NOOVERWRITE))) return false;
    if (!lr.pBits || lr.Pitch<width ||
        (uint64_t)(unsigned)lr.Pitch*desc.Height>0x7fffffffU ||
        (uint64_t)(unsigned)src_pitch*height>0x7fffffffU) {
        texture->UnlockRect(0);return false;
    }
    if (cost) { copied=gfx_now_us();cost->lock_us+=(DWORD)(copied-t);t=copied; }
    /* Checked cached destination ONLY. AVFrame is av_malloc cached RAM.
     * XMemCpy has no alignment restrictions (local xbox.h / XMemCpy.htm).
     * Never route Usage=0 write-combined video/UI textures into this helper. */
    if (lr.Pitch==width && src_pitch==width) XMemCpy(lr.pBits,src,(size_t)width*height);
    else for (y=0;y<height;y++)
        XMemCpy((BYTE*)lr.pBits+(size_t)y*lr.Pitch,src+(size_t)y*src_pitch,width);
    if (cost) { copied=gfx_now_us();cost->copy_us+=(DWORD)(copied-t);t=copied; }
    bool ok=SUCCEEDED(texture->UnlockRect(0));
    if (cost) cost->lock_us+=(DWORD)(gfx_now_us()-t); /* includes coherency work */
    return ok;
}

bool gfx_cached_selftest(D3DDevice *d)
{
    /* Native GPU readback, twice on the SAME texture: detects stale CPU/GPU
     * cache views as well as format/layout failure. Cold path only, no Present.
     * Host fixtures do not stand in for this test. */
    static int result;
    D3DTexture *input=NULL,*readback=NULL;
    BYTE source[16*16];
    bool ok=true;
    int pass;
    if (result) return result>0;
    if (FAILED(d->CreateTexture(16,16,1,D3DUSAGE_CPU_CACHED_MEMORY,D3DFMT_LIN_L8,0,&input,NULL)) ||
        FAILED(d->CreateTexture(32,32,1,0,D3DFMT_A8R8G8B8,0,&readback,NULL))) ok=false;
    for (pass=0;ok && pass<2;pass++) {
        int value=pass?224:32;
        memset(source,value,sizeof(source));
        ok=gfx_cached_upload(input,source,16,16,16,NULL);
        if (!ok) break;
        d->SetTexture(0,input);
        gfx_draw_rect(d,GFX_SHADER_RGB,0,0,UI_LOGICAL_WIDTH,UI_LOGICAL_HEIGHT);
        d->SetTexture(0,NULL);
        /* Insert after unbind, as required for the next off-owner lock. */
        d->InsertFence();
        d->Resolve(D3DRESOLVE_RENDERTARGET0,NULL,readback,NULL,0,0,NULL,1.0f,0,NULL);
        DWORD fence=d->InsertFence();gfx_wait_fence(d,fence);
        D3DLOCKED_RECT lr;ZeroMemory(&lr,sizeof(lr));
        if (FAILED(readback->LockRect(0,&lr,NULL,D3DLOCK_READONLY))) { ok=false;break; }
        if (!lr.pBits || lr.Pitch<32*4) ok=false;
        for (int probe=1;ok && probe<=3;probe++) {
            /* Resolve requires a TILED, 4KB-aligned destination in this SDK.
             * LockRect does not untile: address probes with the real XG helper. */
            BYTE *pixel=(BYTE*)lr.pBits+(size_t)XGAddress2DTiledOffset(probe*8,probe*8,32,4)*4;
            int matches=0;
            for (int channel=0;channel<4;channel++) {
                if (abs((int)pixel[channel]-value)<=1) matches++;
            }
            if (matches<3) ok=false; /* X8 render target alpha is unspecified */
        }
        readback->UnlockRect(0);
    }
    d->SetTexture(0,NULL);d->BlockUntilIdle();
    if (input) input->Release();if (readback) readback->Release();
    result=ok?1:-1;
    const char *report=ok?"PASS: native cached L8 -> GPU RGB -> tiled Resolve -> CPU; two patterns, six probes\n":
                           "FAIL: cached texture GPU selftest; use legacy upload\n";
    OutputDebugStringA(report);
    if (storage_dir()) storage_write("gpu-cached-selftest.txt",report,strlen(report));
    return ok;
}

/* ------------------------------------------------------------ UI */

/* Small once-per-second telemetry texture, measured from displayed lines.
 * No full menu layout or cover processing during video presentation. */
static canvas_t overlay_canvas;
static D3DTexture *overlay_tex;
static DWORD overlay_fence;
static D3DTexture *mouse_indicator;
static bool mouse_indicator_failed;
static int mouse_indicator_width,mouse_indicator_height;
void gfx_draw_hud_rect(D3DDevice *d,float x0,float y0,float x1,float y1)
{
    const int keys[]={D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_BLENDOP,D3DRS_SEPARATEALPHABLENDENABLE};
    const DWORD values[]={TRUE,D3DBLEND_SRCALPHA,D3DBLEND_INVSRCALPHA,D3DBLENDOP_ADD,FALSE};
    DWORD previous[5];
    for(int i=0;i<5;i++){d->GetRenderState((D3DRENDERSTATETYPE)keys[i],previous+i);d->SetRenderState((D3DRENDERSTATETYPE)keys[i],values[i]);}
    gfx_draw_rect(d,GFX_SHADER_RGBA,x0,y0,x1,y1);
    for(int i=0;i<5;i++)d->SetRenderState((D3DRENDERSTATETYPE)keys[i],previous[i]);
}
void gfx_mouse_draw(D3DDevice *d)
{
    int mode=ui_mouse_hud_mode();
    if(mode==2 || !input_mouse_active() || gfx_system_ui_active() || mouse_indicator_failed)return;
    if(!mouse_indicator) {
        const char *text=ui_translate("Mouse mode");
        int width=((font_text_width(FONT_REGULAR,18,text)+16+31)/32)*32;
        if(width<64)width=64;if(width>256)width=256;
        canvas_t label;
        if(!cv_init(&label,width,32)){mouse_indicator_failed=true;input_mouse_indicator_available(false);return;}
        cv_fill(&label,0,0,width,32,mode==1?0xFF000000:0xFF20271F);
        font_draw_fit(&label,FONT_REGULAR,18,8,23,width-16,mode==1?0xFFFFFFFF:ui_theme_accent(),text);
        if(mode==1)for(int i=0;i<width*32;i++) {
            unsigned coverage=label.px[i]&255;
            label.px[i]=(coverage<<24)|0x00F2F5F3u;
        }
        if(FAILED(d->CreateTexture(width,32,1,0,D3DFMT_LIN_A8R8G8B8,0,&mouse_indicator,NULL)) ||
            !gfx_upload(mouse_indicator,label.px,width*4,width*4,32)) {
            if(mouse_indicator)mouse_indicator->Release();mouse_indicator=NULL;mouse_indicator_failed=true;input_mouse_indicator_available(false);
        } else {mouse_indicator_width=width;mouse_indicator_height=32;}
        cv_free(&label);
    }
    if(mouse_indicator) {
        d->SetTexture(0,mouse_indicator);gfx_draw_hud_rect(d,(float)(1264-mouse_indicator_width),(float)(704-mouse_indicator_height),1264,704);d->SetTexture(0,NULL);
    }
}


void gfx_overlay_clear(D3DDevice *d)
{
    if (overlay_fence) gfx_wait_fence(d,overlay_fence);
    overlay_fence = 0;
    if (overlay_tex) overlay_tex->Release();
    overlay_tex = NULL;
    if (overlay_canvas.px) cv_free(&overlay_canvas);
}

void gfx_hud_pixels(uint32_t *pixels,int width,int height)
{
    for(int i=0;i<width*height;i++) {
        unsigned coverage=pixels[i]&255;
        unsigned background=(184*(255-coverage)+127)/255;
        unsigned alpha=coverage+background;
        unsigned r=(242*coverage+23*background+alpha/2)/alpha;
        unsigned g=(245*coverage+31*background+alpha/2)/alpha;
        unsigned b=(243*coverage+29*background+alpha/2)/alpha;
        pixels[i]=(alpha<<24)|(r<<16)|(g<<8)|b;
    }
}
bool gfx_overlay_set(const char *text)
{
    D3DDevice *d = dev; /* Caller already holds ownership. */
    if(gfx_system_ui_active() || gfx_fence_pending(overlay_fence))return true; /* retain previous text */
    const char *p = text;
    if(!text || !*text)return true;
    int lines=0,widest=0;
    for(const char *q=text;*q;) {
        char line[212];size_t n=0;
        while(*q && *q!='\n'){if(n+1<sizeof(line))line[n++]=*q;q++;}
        if(*q=='\n')q++;line[n]=0;lines++;
    }
    int font_size=lines<=5?22:lines<=9?20:18,step=lines<=5?30:lines<=9?26:24;
    for(const char *q=text;*q;) {
        char line[212];size_t n=0;
        while(*q && *q!='\n'){if(n+1<sizeof(line))line[n++]=*q;q++;}
        if(*q=='\n')q++;line[n]=0;
        int measured=font_text_width(FONT_REGULAR,(float)font_size,line);if(measured>widest)widest=measured;
    }
    /* Logical 1280x720 coordinates scale with the backbuffer (480/720/1080).
     * Tiny fixed margins; no full-screen surface or layout work in draw(). */
    int width=((widest+24+31)/32)*32,height=16+font_size+(lines-1)*step;
    if(width<64)width=64;if(width>1248)width=1248;if(height>688)height=688;
    int y=8+(int)font_ascent(FONT_REGULAR,(float)font_size);
    if(overlay_canvas.px && (overlay_canvas.w!=width || overlay_canvas.h!=height))gfx_overlay_clear(d);
    if (!overlay_canvas.px && !cv_init(&overlay_canvas, width, height)) return false;
    if (!overlay_tex && FAILED(d->CreateTexture(width, height, 1, 0, D3DFMT_LIN_A8R8G8B8, 0, &overlay_tex, NULL))) {
        overlay_tex = NULL;
        gfx_overlay_clear(d);
        return false;
    }
    /* Rasterize white glyph coverage first: canvas compositing intentionally
     * produces opaque pixels. Convert this mask to straight RGBA locally,
     * without changing the menu/font compositing contract. */
    cv_fill(&overlay_canvas, 0, 0, width, height, 0xFF000000);
    while (p && *p && y < height) {
        char line[212];
        size_t n = 0;
        while (*p && *p != '\n') { if (n + 1 < sizeof(line)) line[n++] = *p; p++; }
        if (*p == '\n') p++;
        line[n] = '\0';
        font_draw_fit(&overlay_canvas, FONT_REGULAR, (float)font_size, 12, y, width-24, 0xFFFFFFFF, line);
        y += step;
    }
    gfx_hud_pixels(overlay_canvas.px,width,height);
    cv_fill(&overlay_canvas,0,0,3,height,ui_theme_accent());
    /* Wait only on a once-per-second text upload, never overwrite a texture
     * still sampled by the preceding frame. Preserve cached-memory restrictions. */
    return gfx_upload(overlay_tex, overlay_canvas.px, width * 4, width * 4, height);
}

void gfx_overlay_draw(D3DDevice *d)
{
    if (!overlay_tex) return;
    d->SetTexture(0, overlay_tex);
    gfx_draw_hud_rect(d,16,16,(float)(16+overlay_canvas.w),(float)(16+overlay_canvas.h));
    d->SetTexture(0, NULL);
}

void gfx_overlay_mark_used(DWORD fence)
{
    if (overlay_tex) overlay_fence = fence;
}

/* Two textures, so the CPU fills one while the GPU may still read the other. */
static D3DTexture *ui_tex[2];
static DWORD ui_fence[2];
static int ui_next;

void gfx_release_ui_textures(void)
{
    D3DDevice *d = gfx_begin();
    int k;
    /* app.c calls this only after the connecting UI task is finished. The
     * video renderer shares this ownership lock. ui_present recreates lazily. */
    for (k = 0; k < 2; k++)
        if (ui_fence[k]) gfx_wait_fence(d,ui_fence[k]);
    d->BlockUntilIdle();
    gfx_stream_menu_reset(d);
    if(mouse_indicator)mouse_indicator->Release();
    mouse_indicator=NULL;mouse_indicator_failed=false;
    gfx_ui_release(d);
    for (k = 0; k < 2; k++) {
        if (ui_tex[k]) ui_tex[k]->Release();
        ui_tex[k] = NULL;
        ui_fence[k] = 0;
    }
    ui_next = 0;
    gfx_end();
}

void ui_present(void)
{
    perf_ui_end();
    if(gfx_system_ui_active() || !gfx_ui_ready())return;
    canvas_t *c = ui_canvas();
    D3DDevice *d = gfx_begin();
    int command_count;
    if (ui_draw_commands(&command_count)) {
        if (gfx_ui_draw(d)) {
            gfx_ui_submitted(d->InsertFence());
            if(!gfx_system_ui_active()){d->Present(NULL, NULL, NULL, NULL);ui_focus_presented();}
            gfx_end();
            return;
        }
        /* Fail closed to the complete software frame, never a partial UI. */
        ui_focus_finish();
        ui_draw_software();
        ui_draw_enable(false);
        gfx_ui_release(d);
    }
    int k = ui_next;

    ui_next ^= 1;
    if (ui_tex[k] == NULL &&
        FAILED(d->CreateTexture(c->w, c->h, 1, 0, D3DFMT_LIN_A8R8G8B8, 0, &ui_tex[k], NULL))) {
        ui_tex[k] = NULL;
        gfx_end();
        return;
    }
    if(gfx_fence_pending(ui_fence[k])){gfx_end();return;}
    if (!gfx_upload(ui_tex[k], c->px, c->w * 4, c->w * 4, c->h)) {
        gfx_end();
        return;
    }

    d->SetTexture(0, ui_tex[k]);
    gfx_draw_rect(d, GFX_SHADER_RGB, 0, 0, GFX_WIDTH, GFX_HEIGHT);
    d->SetTexture(0, NULL);
    ui_fence[k] = d->InsertFence();
    if(!gfx_system_ui_active()){d->Present(NULL, NULL, NULL, NULL);ui_focus_presented();}
    gfx_end();
}
