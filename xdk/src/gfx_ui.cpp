/* Original Xenonlight menu renderer. No video shader/state ownership changes. */
#include "xdk.h"
#include "../../src/performance.h"
#include <d3dx9shader.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/ui/ui.h"
#include "../../src/ui/draw.h"
#include "../../src/platform.h"
#include "../../src/launch_cover.h"

#define GLYPH_W 1024
#define GLYPH_H 512
#define GLYPH_MAX 2048
#define IMAGE_MAX 14 /* twelve visible/prefetched covers, button atlas and brand icon */
struct ui_vertex {float x,y,u,v;DWORD color;};
struct glyph_slot {const void *key;int x,y,w,h;};
struct image_slot {const void *key;DWORD generation;int w,h;D3DTexture *texture;unsigned stamp;};
static D3DVertexDeclaration *ui_decl;
static D3DVertexShader *ui_vs;
static D3DPixelShader *ui_texture_ps,*ui_shape_ps,*launch_ps;
static D3DTexture *glyph_texture;
static uint32_t *glyph_pixels;
static glyph_slot glyphs[GLYPH_MAX];
static int glyph_count,pack_x,pack_y,pack_row;
static image_slot images[IMAGE_MAX];
static unsigned frame_stamp;
static DWORD previous_fence;
static bool glyph_dirty;
static ui_vertex batch[6*256];
static const char ui_shader[]=
    "struct P {float2 pos:POSITION;float2 uv:TEXCOORD0;float4 color:COLOR0;};"
    "struct R {float4 pos:POSITION;float2 uv:TEXCOORD0;float4 color:COLOR0;};"
    "R vertex(P p){R r;r.pos=float4(p.pos,0,1);r.uv=p.uv;r.color=p.color;return r;}"
    "sampler atlas:register(s0);"
    "float4 sample_ui(R p):COLOR{return tex2D(atlas,p.uv)*p.color;}"
    "float4 box:register(c0);float4 upper:register(c1);float4 lower:register(c2);"
    "struct S {float2 uv:TEXCOORD0;};"
    "float4 shape(S p):COLOR {float4 c=lerp(upper,lower,p.uv.y);"
    "if(box.z>0 || box.w>0){float2 q=abs((p.uv-.5)*box.xy)-(.5*box.xy-box.z);"
    "float d=length(max(q,0))+min(max(q.x,q.y),0)-box.z;"
    "c.a*=saturate(.5-d);if(box.w>0)c.a*=saturate(d+box.w+.5);}return c;}"
    "sampler words:register(s1);sampler scene:register(s2);sampler previous:register(s3);"
    "float4 rect:register(c3);float4 phase:register(c4);float4 flags:register(c5);"
    "float4 detail:register(c6);float4 text_layout:register(c7);"
    "float4 opening(S p):COLOR {"
    " float2 xy=p.uv*float2(1280,720);float3 c=float3(8,11,14)/255;"
    " if(flags.x>0)c=lerp(tex2D(scene,p.uv).rgb,c,phase.w);"
    " float2 q=(xy-rect.xy)/rect.zw;"
    " if(all(q>=0)&&all(q<=1)){"
    "  float3 fallback=float3(32,39,42)/255;"
    "  float2 logo=float2((xy.x-rect.x-rect.z*.5+256)/512,(xy.y-rect.y-rect.w*.5+24)/48);"
    "  if(all(logo>=0)&&all(logo<=1)){float4 g=tex2D(words,float2(logo.x,(text_layout.w+logo.y*48)/512));fallback=lerp(fallback,g.rgb,g.a);}"
    "  if(flags.w>0){float4 old=tex2D(previous,q);fallback=lerp(fallback,old.rgb,old.a);}"
    "  float4 art=tex2D(atlas,q);c=lerp(fallback,art.rgb,art.a*phase.z);"
    " }"
    " float2 t=(xy-float2(384,470))/float2(512,48);"
    " if(all(t>=0)&&all(t<=1)){float4 g=tex2D(words,float2(t.x,t.y*48/512));c=lerp(c,g.rgb,g.a*phase.y);}"
    " t=(xy-float2(384,520))/float2(512,48);"
    " if(all(t>=0)&&all(t<=1)){float4 g=tex2D(words,float2(t.x,(48+t.y*48)/512));c=lerp(c,g.rgb,g.a);}"
    " t=(xy-float2(384,578))/float2(512,24);"
    " if(all(t>=0)&&all(t<=1)){float4 g=tex2D(words,float2(t.x,(text_layout.x+flags.y*text_layout.y+t.y*text_layout.y)/512));c=lerp(c,g.rgb,g.a);}"
    " t=(xy-float2(384,634))/float2(512,24);"
    " if(all(t>=0)&&all(t<=1)){float4 g=tex2D(words,float2(t.x,(text_layout.z+detail.x*24+t.y*24)/512));c=lerp(c,g.rgb,g.a);}"
    " float segment=floor((xy.x-514)/37);"
    " if(xy.y>=614&&xy.y<617&&segment>=0&&segment<7&&xy.x-514-segment*37<30)"
    "  c=segment<flags.z?detail.yzw:float3(65,74,77)/255;"
    " return float4(c,phase.x);"
    "}"
;

static ID3DXBuffer *compile_ui(const char *entry,const char *profile) {
    ID3DXBuffer *code=NULL,*error=NULL;
    HRESULT hr=D3DXCompileShader(ui_shader,(UINT)strlen(ui_shader),NULL,NULL,entry,profile,0,&code,&error,NULL);
    if(error){OutputDebugStringA((const char*)error->GetBufferPointer());error->Release();}
    return SUCCEEDED(hr)?code:NULL;
}
static bool initialize(D3DDevice *d) {
    if(ui_decl && ui_vs && ui_texture_ps && ui_shape_ps && launch_ps)return true;
    static const D3DVERTEXELEMENT9 elements[]={
        {0,0,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
        {0,8,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},
        {0,16,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},D3DDECL_END()};
    if(!ui_decl && FAILED(d->CreateVertexDeclaration(elements,&ui_decl)))return false;
    ID3DXBuffer *code;
    if(!ui_vs){code=compile_ui("vertex","vs_3_0");if(!code)return false;HRESULT hr=d->CreateVertexShader((DWORD*)code->GetBufferPointer(),&ui_vs);code->Release();if(FAILED(hr))return false;}
    if(!ui_texture_ps){code=compile_ui("sample_ui","ps_3_0");if(!code)return false;HRESULT hr=d->CreatePixelShader((DWORD*)code->GetBufferPointer(),&ui_texture_ps);code->Release();if(FAILED(hr))return false;}
    if(!ui_shape_ps){code=compile_ui("shape","ps_3_0");if(!code)return false;HRESULT hr=d->CreatePixelShader((DWORD*)code->GetBufferPointer(),&ui_shape_ps);code->Release();if(FAILED(hr))return false;}
    if(!launch_ps){code=compile_ui("opening","ps_3_0");if(!code)return false;HRESULT hr=d->CreatePixelShader((DWORD*)code->GetBufferPointer(),&launch_ps);code->Release();if(FAILED(hr))return false;}
    return true;
}
static void reset_glyphs(void) {
    glyph_count=pack_x=pack_y=pack_row=0;
    memset(glyph_pixels,0,GLYPH_W*GLYPH_H*4);glyph_dirty=true;
}
static glyph_slot *glyph(const ui_draw_command *c) {
    for(int i=0;i<glyph_count;i++)if(glyphs[i].key==c->pixels)return glyphs+i;
    if(c->iw<=0||c->ih<=0||c->iw+2>GLYPH_W||c->ih+2>GLYPH_H||glyph_count==GLYPH_MAX)return NULL;
    if(pack_x+c->iw+2>GLYPH_W){pack_x=0;pack_y+=pack_row;pack_row=0;}
    if(pack_y+c->ih+2>GLYPH_H)return NULL;
    glyph_slot *g=glyphs+glyph_count++;g->key=c->pixels;g->x=pack_x+1;g->y=pack_y+1;g->w=c->iw;g->h=c->ih;
    const BYTE *mask=(const BYTE*)c->pixels;
    for(int y=0;y<g->h;y++)for(int x=0;x<g->w;x++)glyph_pixels[(g->y+y)*GLYPH_W+g->x+x]=ARGB(mask[y*c->stride+x],255,255,255);
    pack_x+=c->iw+2;if(c->ih+2>pack_row)pack_row=c->ih+2;glyph_dirty=true;return g;
}
static bool upload(D3DTexture *t,const void *p,int w,int h) {
    PerfScope timing(PERF_UI_UPLOAD);
    bool ok=gfx_upload(t,p,w*4,w*4,h);
    return ok;
}
static image_slot *image(D3DDevice *d,const ui_draw_command *c) {
    for(int i=0;i<IMAGE_MAX;i++)if(images[i].key==c->pixels && images[i].generation==c->generation &&
        images[i].w==c->iw && images[i].h==c->ih){images[i].stamp=frame_stamp;return images+i;}
    int slot=-1;for(int i=0;i<IMAGE_MAX;i++)if(!images[i].texture){slot=i;break;}
    if(slot<0)for(int i=0;i<IMAGE_MAX;i++)if(images[i].stamp!=frame_stamp && (slot<0||images[i].stamp<images[slot].stamp))slot=i;
    if(slot<0)return NULL;
    image_slot *s=images+slot;if(s->texture)s->texture->Release();memset(s,0,sizeof(*s));
    if(FAILED(d->CreateTexture(c->iw,c->ih,1,0,D3DFMT_LIN_A8R8G8B8,0,&s->texture,NULL)))return NULL;
    if(!upload(s->texture,c->pixels,c->iw,c->ih)){s->texture->Release();memset(s,0,sizeof(*s));return NULL;}
    s->key=c->pixels;s->generation=c->generation;s->w=c->iw;s->h=c->ih;s->stamp=frame_stamp;return s;
}
static void color_f(DWORD color,float out[4]) {out[0]=((color>>16)&255)/255.0f;out[1]=((color>>8)&255)/255.0f;out[2]=(color&255)/255.0f;out[3]=(color>>24)/255.0f;}
static void quad(ui_vertex *v,float x,float y,float w,float h,float u,float top,float right,float bottom,DWORD color,float cw,float ch) {
    float xs[6]={x,x+w,x,x,x+w,x+w},ys[6]={y,y,y+h,y+h,y,y+h};
    float us[6]={u,right,u,u,right,right},vs[6]={top,top,bottom,bottom,top,bottom};
    for(int i=0;i<6;i++){v[i].x=xs[i]*2/cw-1;v[i].y=1-ys[i]*2/ch;v[i].u=us[i];v[i].v=vs[i];v[i].color=color;}
}
static void clip(D3DDevice *d,const ui_draw_command *c,float cw,float ch,int bw,int bh) {
    RECT r={(LONG)(c->clip[0]*bw/cw),(LONG)(c->clip[1]*bh/ch),(LONG)(c->clip[2]*bw/cw),(LONG)(c->clip[3]*bh/ch)};
    d->SetScissorRect(&r);
}
bool gfx_ui_draw(D3DDevice *d) {
    int count;const ui_draw_command *list=ui_draw_commands(&count);if(!list)return false;
    if(!gfx_ui_ready())return false;
    if(!initialize(d))return false;
    if(!glyph_pixels){glyph_pixels=(uint32_t*)malloc(GLYPH_W*GLYPH_H*4);if(!glyph_pixels)return false;reset_glyphs();}
    if(!glyph_texture && FAILED(d->CreateTexture(GLYPH_W,GLYPH_H,1,0,D3DFMT_LIN_A8R8G8B8,0,&glyph_texture,NULL)))return false;
    bool fit=false;
    for(int attempt=0;attempt<2 && !fit;attempt++) {
        fit=true;for(int i=0;i<count;i++)if(list[i].kind==UI_GLYPH && !glyph(list+i)){fit=false;break;}
        if(!fit)reset_glyphs();
    }
    if(!fit || (glyph_dirty && !upload(glyph_texture,glyph_pixels,GLYPH_W,GLYPH_H)))return false;
    glyph_dirty=false;frame_stamp++;
    // Reserve all visible images before evicting any entries from the previous frame.
    for(int i=0;i<count;i++)if(list[i].kind==UI_IMAGE)
        for(int j=0;j<IMAGE_MAX;j++)if(images[j].key==list[i].pixels && images[j].generation==list[i].generation)images[j].stamp=frame_stamp;
    for(int i=0;i<count;i++)if(list[i].kind==UI_IMAGE && !image(d,list+i))return false;
    canvas_t *canvas=ui_canvas();float cw=(float)canvas->w,ch=(float)canvas->h;int bw,bh;gfx_backbuffer_size(&bw,&bh);
    d->SetVertexDeclaration(ui_decl);d->SetVertexShader(ui_vs);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
    d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE);
    for(int i=0;i<count;i++) {
        const ui_draw_command *c=list+i;if(c->clip[0]>=c->clip[2]||c->clip[1]>=c->clip[3])continue;
        clip(d,c,cw,ch,bw,bh);
        if(c->kind==UI_GLYPH) {
            unsigned n=0;int j=i;
            while(j<count && n<256 && list[j].kind==UI_GLYPH && !memcmp(list[j].clip,c->clip,sizeof(c->clip))) {
                const ui_draw_command *t=list+j;glyph_slot *g=glyph(t);if(!g)break;
                quad(batch+n*6,t->x,t->y,(float)g->w,(float)g->h,(float)g->x/GLYPH_W,(float)g->y/GLYPH_H,
                    (float)(g->x+g->w)/GLYPH_W,(float)(g->y+g->h)/GLYPH_H,t->color,cw,ch);n++;j++;
            }
            d->SetTexture(0,glyph_texture);d->SetPixelShader(ui_texture_ps);d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,n*2,batch,sizeof(ui_vertex));i=j-1;
        } else if(c->kind==UI_IMAGE) {
            image_slot *s=image(d,c);quad(batch,c->x,c->y,c->w,c->h,(float)c->sx/c->iw,(float)c->sy/c->ih,
                (float)(c->sx+c->sw)/c->iw,(float)(c->sy+c->sh)/c->ih,c->color,cw,ch);
            d->SetTexture(0,s->texture);d->SetPixelShader(ui_texture_ps);d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,batch,sizeof(ui_vertex));
        } else {
            float width=c->w,height=c->h;unsigned primitives=2;
            quad(batch,c->x,c->y,width,height,0,0,1,1,0xffffffff,cw,ch);
            if(c->kind==UI_LINE) {
                float dx=c->w-c->x,dy=c->h-c->y,length=sqrtf(dx*dx+dy*dy);if(length<0.01f)continue;
                width=length;height=c->t;float nx=-dy/length*c->t/2,ny=dx/length*c->t/2;
                float xx[6]={c->x+nx,c->w+nx,c->x-nx,c->x-nx,c->w+nx,c->w-nx};
                float yy[6]={c->y+ny,c->h+ny,c->y-ny,c->y-ny,c->h+ny,c->h-ny};
                for(int v=0;v<6;v++){batch[v].x=xx[v]*2/cw-1;batch[v].y=1-yy[v]*2/ch;}
            } else if(c->kind==UI_TRIANGLE) {
                float xx[3]={c->x,c->w,c->x2},yy[3]={c->y,c->h,c->y2};
                for(int v=0;v<3;v++){batch[v].x=xx[v]*2/cw-1;batch[v].y=1-yy[v]*2/ch;}width=height=0;primitives=1;
            }
            float box[4]={width,height,c->r,c->kind==UI_OUTLINE?c->t:0},top[4],bottom[4];
            color_f(c->color,top);color_f(c->kind==UI_GRADIENT?c->bottom:c->color,bottom);
            d->SetTexture(0,NULL);d->SetPixelShader(ui_shape_ps);d->SetPixelShaderConstantF(0,box,1);
            d->SetPixelShaderConstantF(1,top,1);d->SetPixelShaderConstantF(2,bottom,1);
            d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,primitives,batch,sizeof(ui_vertex));
        }
    }
    d->SetTexture(0,NULL);d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
    unsigned cover_bytes=0,atlas_bytes=0,covers=0;
    for(int i=0;i<IMAGE_MAX;i++)if(images[i].texture) {
        unsigned bytes=images[i].w*images[i].h*4;
        if(images[i].w==256&&images[i].h==32)atlas_bytes+=bytes;else {cover_bytes+=bytes;covers++;}
    }
    ui_draw_gpu_memory(GLYPH_W*GLYPH_H*4,cover_bytes,atlas_bytes,covers);return true;
}
void gfx_ui_submitted(DWORD fence){previous_fence=fence;}
bool gfx_ui_ready(void){return !gfx_fence_pending(previous_fence);}
void gfx_ui_release(D3DDevice *d) {
    d->SetTexture(0,NULL);if(previous_fence)gfx_wait_fence(d,previous_fence);previous_fence=0;
    for(int i=0;i<IMAGE_MAX;i++){if(images[i].texture)images[i].texture->Release();memset(images+i,0,sizeof(images[i]));}
    if(glyph_texture)glyph_texture->Release();glyph_texture=NULL;
    free(glyph_pixels);glyph_pixels=NULL;glyph_count=pack_x=pack_y=pack_row=0;
    // Shader objects are small persistent device resources; no video state is modified.
    ui_draw_gpu_memory(0,0,0,0);ui_draw_suspend();
}

/* Dedicated launch resources. The menu prepares them before handing device
 * ownership to video. Text and art are immutable uploads, never decoded here. */
static D3DTexture *launch_backdrop,*launch_text,*launch_art[2];
static DWORD launch_fence,launch_background_fence;
static struct launch_cover cover;
static struct launch_visual visual;
static volatile LONG launch_block,launch_cancel,launch_reveal,launch_first,launch_first_ms,launch_status,launch_art_ready;
static bool launch_resources,launch_prepared,launch_attempted,launch_have_generation;
static uint32_t *launch_staging;
static const uint32_t *launch_initial;
static int launch_aw,launch_ah,launch_current;
static uint32_t launch_generation;
static bool launch_previous;
static unsigned launch_milestone;
static float launch_accent[4];
extern "C" void launch_begin(unsigned wait_ms,bool motion)
{
    launch_cover_begin(&cover,wait_ms,motion);
    InterlockedExchange(&launch_cancel,0);InterlockedExchange(&launch_reveal,0);
    InterlockedExchange(&launch_first,0);InterlockedExchange(&launch_first_ms,0);
    InterlockedExchange(&launch_art_ready,0);InterlockedExchange(&launch_status,0);
    InterlockedExchange(&launch_block,1);launch_resources=true;launch_prepared=launch_attempted=launch_have_generation=false;
    launch_initial=NULL;launch_generation=0;launch_previous=false;launch_current=-1;
    launch_milestone=1;color_f(ui_theme_accent(),launch_accent);
}
extern "C" void launch_set_wait(unsigned wait_ms){cover.wait_ms=wait_ms>10000?10000:wait_ms;}
extern "C" void launch_retry(void)
{
    /* The failed connection has stopped its render thread. Keep the same
     * presentation resources/clock, but wait for this attempt's first frame. */
    cover.first=cover.revealing=false;cover.first_ms=cover.fade_ms=0;
    InterlockedExchange(&launch_first,0);InterlockedExchange(&launch_first_ms,0);
    InterlockedExchange(&launch_reveal,0);InterlockedExchange(&launch_status,LAUNCH_RETRYING);
}
extern "C" void launch_set_status(enum launch_status status)
{if(status>=0 && status<LAUNCH_STATUS_COUNT)InterlockedExchange(&launch_status,(LONG)status);}
extern "C" bool launch_active(void){return InterlockedCompareExchange(&launch_block,0,0)!=0;}
extern "C" bool launch_has_video(void){return InterlockedCompareExchange(&launch_first,0,0)!=0;}
extern "C" bool launch_cancelled(void){return InterlockedCompareExchange(&launch_cancel,0,0)!=0;}
extern "C" void launch_command(bool cancel,bool reveal)
{
    if(cancel)InterlockedExchange(&launch_cancel,1);
    if(reveal && launch_has_video())InterlockedExchange(&launch_reveal,1);
}
extern "C" void launch_decoded(void)
{
    if(launch_active() && !launch_has_video()) {
        InterlockedExchange(&launch_first_ms,(LONG)GetTickCount());InterlockedExchange(&launch_first,1);
    }
}
extern "C" bool launch_offer_art(const uint32_t *pixels,int w,int h,uint32_t generation)
{
    if(!launch_active() || !pixels || (launch_have_generation && generation==launch_generation))return false;
    if(!launch_attempted){
        int aw,ah;ui_art_size(&aw,&ah);if(w!=aw || h!=ah)return false;
        launch_initial=pixels;launch_generation=generation;launch_have_generation=true;return true;
    }
    if(!launch_prepared)return false; /* failed preparation must never retain a borrowed cache pointer */
    if(w!=launch_aw || h!=launch_ah || !launch_staging || InterlockedCompareExchange(&launch_art_ready,0,0))return false;
    /* The producer owns this fixed staging buffer until release publication;
     * cache replacement can then safely release the old CPU image. */
    memcpy(launch_staging,pixels,(size_t)w*h*4);launch_generation=generation;launch_have_generation=true;
    InterlockedExchange(&launch_art_ready,1);return true;
}
extern "C" void launch_collect(void)
{
    /* Producer only. The renderer publishes inactive after its last CPU upload
     * and never reads staging again while inactive, even if a GPU fence remains. */
    if(!launch_active()){free(launch_staging);launch_staging=NULL;}
}
static void launch_release(D3DDevice *d,bool wait)
{
    if(wait && launch_fence)gfx_wait_fence(d,launch_fence);
    if(gfx_fence_pending(launch_fence))return;
    for(int i=0;i<4;i++)d->SetTexture(i,NULL);
    if(launch_backdrop)launch_backdrop->Release();launch_backdrop=NULL;
    if(launch_text)launch_text->Release();launch_text=NULL;
    for(int i=0;i<2;i++){if(launch_art[i])launch_art[i]->Release();launch_art[i]=NULL;}
    if(wait){free(launch_staging);launch_staging=NULL;}launch_initial=NULL;
    launch_fence=launch_background_fence=0;launch_resources=false;
    /* Producer observes prepared throughout the active lifetime. Renderer-side
     * fence cleanup must not race its bounded staging publication. */
    if(wait)launch_prepared=false;
}
extern "C" bool launch_prepare(void)
{
    if(launch_attempted)return launch_prepared;
    launch_attempted=true;
    ui_draw_software();canvas_t *c=ui_canvas();ui_art_size(&launch_aw,&launch_ah);
    launch_staging=(uint32_t*)malloc((size_t)launch_aw*launch_ah*4);
    uint32_t *text=(uint32_t*)malloc(LAUNCH_TEXT_W*LAUNCH_TEXT_H*4);
    if(text)ui_launch_text(text,LAUNCH_TEXT_W,LAUNCH_TEXT_H);
    D3DDevice *d=gfx_begin();
    bool ok=initialize(d) && launch_staging && text;
    if(ok)ok=SUCCEEDED(d->CreateTexture(c->w,c->h,1,0,D3DFMT_LIN_A8R8G8B8,0,&launch_backdrop,NULL)) && upload(launch_backdrop,c->px,c->w,c->h);
    if(ok)ok=SUCCEEDED(d->CreateTexture(LAUNCH_TEXT_W,LAUNCH_TEXT_H,1,0,D3DFMT_LIN_A8R8G8B8,0,&launch_text,NULL)) && upload(launch_text,text,LAUNCH_TEXT_W,LAUNCH_TEXT_H);
    for(int i=0;i<2 && ok;i++)ok=SUCCEEDED(d->CreateTexture(launch_aw,launch_ah,1,0,D3DFMT_LIN_A8R8G8B8,0,&launch_art[i],NULL));
    if(ok && launch_initial){
        if(upload(launch_art[0],launch_initial,launch_aw,launch_ah))launch_current=0;
        else launch_have_generation=false; /* allow the producer to retry the same image */
    }
    float x,y,w,h;ui_launch_origin(&x,&y,&w,&h);
    launch_visual_begin(&visual,GetTickCount(),cover.motion,x,y,w,h,launch_current>=0);
    launch_initial=NULL;launch_prepared=true;
    if(!ok){launch_release(d,true);launch_resources=true;} /* opaque, cancellable fallback */
    gfx_end();free(text);return ok;
}
extern "C" void launch_end(void)
{
    D3DDevice *d=gfx_begin();launch_release(d,true);memset(&cover,0,sizeof(cover));
    InterlockedExchange(&launch_block,0);InterlockedExchange(&launch_cancel,0);
    InterlockedExchange(&launch_reveal,0);InterlockedExchange(&launch_first,0);InterlockedExchange(&launch_art_ready,0);gfx_end();
}
bool gfx_launch_pending(void){return launch_resources;}
void gfx_launch_draw(D3DDevice *d,bool usable)
{
    if(!launch_resources)return;
    DWORD now=GetTickCount();
    if(launch_active() && InterlockedCompareExchange(&launch_art_ready,0,0) && launch_prepared && !gfx_fence_pending(launch_fence)) {
        int next=launch_current==0?1:0;
        if(upload(launch_art[next],launch_staging,launch_aw,launch_ah)) {
            launch_previous=launch_current>=0;launch_current=next;launch_visual_art_ready(&visual,now);
        }
        InterlockedExchange(&launch_art_ready,0);
    }
    struct launch_visual_sample v;launch_visual_get(&visual,now,&v);
    if(!cover.first && launch_has_video())launch_cover_frame(&cover,(DWORD)InterlockedCompareExchange(&launch_first_ms,0,0));
    if(launch_cancelled())launch_cover_cancel(&cover);
    enum launch_status status=launch_display_status((enum launch_status)InterlockedCompareExchange(&launch_status,0,0),launch_has_video(),launch_cancelled());
    unsigned milestone=launch_status_progress(status);if(milestone>launch_milestone)launch_milestone=milestone;
    unsigned alpha=launch_cover_alpha(&cover,now,usable && !v.intro,InterlockedCompareExchange(&launch_reveal,0,0)!=0);
    if(!alpha){InterlockedExchange(&launch_block,0);launch_release(d,false);return;}
    if(!v.intro && launch_backdrop && !gfx_fence_pending(launch_background_fence)) {
        d->SetTexture(2,NULL);launch_backdrop->Release();launch_backdrop=NULL;
    }
    if(!launch_prepared || !launch_ps){d->Clear(0,NULL,D3DCLEAR_TARGET,0xff080b0e,1,0);return;}
    d->SetVertexDeclaration(ui_decl);d->SetVertexShader(ui_vs);d->SetPixelShader(launch_ps);
    d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
    float rect[4]={v.x,v.y,v.w,v.h},phase[4]={alpha/255.0f,v.title,v.art,v.dim};
    float flags[4]={v.intro && launch_backdrop?1.0f:0.0f,(float)status,(float)launch_milestone,launch_previous?1.0f:0.0f};
    float detail[4]={launch_has_video() && !launch_cancelled()?1.0f:0.0f,launch_accent[0],launch_accent[1],launch_accent[2]};
    d->SetPixelShaderConstantF(3,rect,1);d->SetPixelShaderConstantF(4,phase,1);d->SetPixelShaderConstantF(5,flags,1);
    d->SetPixelShaderConstantF(6,detail,1);
    static const float text_layout[4]={(float)LAUNCH_STATUS_Y,(float)LAUNCH_STATUS_H,(float)LAUNCH_HINT_Y,(float)LAUNCH_LOGO_Y};
    d->SetPixelShaderConstantF(7,text_layout,1);
    d->SetTexture(0,launch_current>=0?launch_art[launch_current]:launch_text);d->SetTexture(1,launch_text);
    d->SetTexture(2,launch_backdrop);d->SetTexture(3,launch_previous?launch_art[1-launch_current]:launch_text);
    quad(batch,0,0,1280,720,0,0,1,1,0xffffffff,1280,720);d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,batch,sizeof(ui_vertex));
    for(int i=0;i<4;i++)d->SetTexture(i,NULL);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);launch_fence=d->InsertFence();
    if(v.intro && launch_backdrop)launch_background_fence=launch_fence;
}
extern "C" void launch_present(void)
{
    D3DDevice *d=gfx_begin();gfx_launch_draw(d,false);
    if(!gfx_system_ui_active())d->Present(NULL,NULL,NULL,NULL);gfx_end();
}
void gfx_launch_idle(D3DDevice *d)
{
    if(!launch_resources)return;
    if(!launch_active()){gfx_launch_draw(d,false);return;}
    if(cover.revealing)return;
    gfx_launch_draw(d,false);
    if(!gfx_system_ui_active())d->Present(NULL,NULL,NULL,NULL);
}
