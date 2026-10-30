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

#define GLYPH_W 1024
#define GLYPH_H 512
#define GLYPH_MAX 2048
#define IMAGE_MAX 14 /* twelve visible/prefetched covers, button atlas and brand icon */
struct ui_vertex {float x,y,u,v;DWORD color;};
struct glyph_slot {const void *key;int x,y,w,h;};
struct image_slot {const void *key;DWORD generation;int w,h;D3DTexture *texture;unsigned stamp;};
static D3DVertexDeclaration *ui_decl;
static D3DVertexShader *ui_vs;
static D3DPixelShader *ui_texture_ps,*ui_shape_ps;
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
    "c.a*=saturate(.5-d);if(box.w>0)c.a*=saturate(d+box.w+.5);}return c;}";

static ID3DXBuffer *compile_ui(const char *entry,const char *profile) {
    ID3DXBuffer *code=NULL,*error=NULL;
    HRESULT hr=D3DXCompileShader(ui_shader,(UINT)strlen(ui_shader),NULL,NULL,entry,profile,0,&code,&error,NULL);
    if(error){OutputDebugStringA((const char*)error->GetBufferPointer());error->Release();}
    return SUCCEEDED(hr)?code:NULL;
}
static bool initialize(D3DDevice *d) {
    if(ui_decl && ui_vs && ui_texture_ps && ui_shape_ps)return true;
    static const D3DVERTEXELEMENT9 elements[]={
        {0,0,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
        {0,8,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},
        {0,16,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},D3DDECL_END()};
    if(!ui_decl && FAILED(d->CreateVertexDeclaration(elements,&ui_decl)))return false;
    ID3DXBuffer *code;
    if(!ui_vs){code=compile_ui("vertex","vs_3_0");if(!code)return false;HRESULT hr=d->CreateVertexShader((DWORD*)code->GetBufferPointer(),&ui_vs);code->Release();if(FAILED(hr))return false;}
    if(!ui_texture_ps){code=compile_ui("sample_ui","ps_3_0");if(!code)return false;HRESULT hr=d->CreatePixelShader((DWORD*)code->GetBufferPointer(),&ui_texture_ps);code->Release();if(FAILED(hr))return false;}
    if(!ui_shape_ps){code=compile_ui("shape","ps_3_0");if(!code)return false;HRESULT hr=d->CreatePixelShader((DWORD*)code->GetBufferPointer(),&ui_shape_ps);code->Release();if(FAILED(hr))return false;}
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
