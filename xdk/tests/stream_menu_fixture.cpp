#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <set>
typedef unsigned DWORD;
#define FAILED(x) ((x)<0)
#define D3DFMT_LIN_A8R8G8B8 1
#define GFX_SHADER_RGB 0
#define FONT_REGULAR 0
#define FONT_BOLD 1
struct canvas_t {uint32_t *px;int w,h;};
static unsigned snapshot,tick,allocations,releases,canvas_allocations,canvas_releases,uploads,draws,waits,unavailable;
static bool guide,fail_texture,fail_upload;
static const char *reason;
static int style;
static float last_x,last_y,last_right,last_bottom;
static std::set<DWORD> pending;
static bool gfx_fence_pending(DWORD fence){return pending.count(fence)!=0;}
static bool cv_init(canvas_t *c,int w,int h){c->px=(uint32_t*)calloc(w*h,4);c->w=w;c->h=h;canvas_allocations++;return c->px!=NULL;}
static void cv_free(canvas_t *c){free(c->px);c->px=NULL;canvas_releases++;}
static void cv_fill(canvas_t *c,int x,int y,int w,int h,uint32_t){assert(c->px && x>=0 && y>=0 && x+w<=c->w && y+h<=c->h);}
static void cv_circle(canvas_t *c,float x,float y,float r,uint32_t){assert(c->px && x-r>=0 && y-r>=0 && x+r<c->w && y+r<c->h);}
static void cv_rrect(canvas_t *c,float x,float y,float w,float h,float,uint32_t){assert(c->px && x>=0 && y>=0 && x+w<=c->w && y+h<=c->h);}
static void cv_rrect_outline(canvas_t *c,float x,float y,float w,float h,float,float,uint32_t){assert(c->px && x>=0 && y>=0 && x+w<=c->w && y+h<=c->h);}
static void font_draw_fit(canvas_t *c,int,int,int x,int y,int width,uint32_t,const char *s){assert(c->px && x>=0 && y>0 && y<c->h && x+width<=c->w && s);}
static int font_draw_wrapped(canvas_t *c,int,float,int x,int y,int width,float,uint32_t,const char *s){assert(c->px && x>=0 && y>0 && y<584 && x+width<=c->w && s);return 1;}
struct D3DTexture {DWORD fence;D3DTexture():fence(0){}void Release(){assert(!gfx_fence_pending(fence));releases++;delete this;}};
struct D3DDevice {
 D3DTexture *bound;
 int CreateTexture(int w,int h,int,int,int,int,D3DTexture **out,void*){assert((w==448 && h==640) || (w==512 && (h==128 || h==320)));if(fail_texture){*out=NULL;return -1;}*out=new D3DTexture;allocations++;return 0;}
 void SetTexture(int,D3DTexture *t){bound=t;}
};
static unsigned input_quick_snapshot(){return snapshot;}
static bool input_mouse_active(){return false;}
static void input_quick_ui_unavailable(){unavailable++;}
static int audio_output_channels(){return 2;}
static const char *audio_fallback_reason(){return reason;}
static uint32_t ui_theme_accent(){return 0xFF72C840;}
static uint32_t cv_mix(uint32_t a,uint32_t,unsigned){return a;}
static const char *ui_translate(const char *s){return s;}
static int ui_stream_menu_style(){return style;}
static void plat_log(const char*){}
static DWORD GetTickCount(){return tick;}
static bool gfx_system_ui_active(){return guide;}
static void gfx_wait_fence(D3DDevice*,DWORD fence){assert(fence);waits++;for(std::set<DWORD>::iterator i=pending.begin();i!=pending.end() && *i<=fence;)pending.erase(i++);}
static bool gfx_upload(D3DTexture *t,const void*,int pitch,int row,int height){assert(!gfx_fence_pending(t->fence) && ((pitch==1792 && row==1792 && height==640)||(pitch==2048 && row==2048 && (height==128 || height==320))));uploads++;return !fail_upload;}
static void gfx_draw_rect(D3DDevice *d,int,float x,float y,float r,float b){assert(d->bound);last_x=x;last_y=y;last_right=r;last_bottom=b;draws++;}
static unsigned hud_draws,hud_masks;
static void gfx_draw_hud_rect(D3DDevice *d,float x,float y,float r,float b){hud_draws++;gfx_draw_rect(d,3,x,y,r,b);}
static void gfx_hud_pixels(uint32_t *pixels,int w,int h){assert(pixels && w==512 && h==128);hud_masks++;}
/* ACTUAL_STREAM_MENU */
static void submit(DWORD fence){gfx_stream_menu_submitted(fence);for(int i=0;i<2;i++)if(panels[i].used){panels[i].texture->fence=fence;pending.insert(fence);}}
int main(){
 D3DDevice d={};gfx_stream_menu_draw(&d,1,1,0,1000,false);assert(!allocations && !draws);
 for(style=0;style<2;style++){
  snapshot=1|(100<<4);gfx_stream_menu_draw(&d,1,1,0,1000,false);
  int width=style?448:512,height=style?640:320;assert(panels[0].canvas.w==width && panels[0].canvas.h==height);
  assert(last_x==(style?40:384) && last_y==(style?40:190) && last_right==(style?488:896) && last_bottom==(style?680:510));
  D3DTexture *first=panels[0].texture;unsigned initial=uploads;submit(1);
  snapshot|=2;gfx_stream_menu_draw(&d,2,2,0,1000,false);
  assert(uploads==initial+1 && panels[0].texture!=first && panels[0].spare==first);submit(2);
  snapshot=1|4|(100<<4);initial=uploads;gfx_stream_menu_draw(&d,2,2,0,1000,false);assert(uploads==initial);submit(3); // both busy
  pending.erase(1);gfx_stream_menu_draw(&d,2,2,0,1000,false);assert(uploads==initial+1 && panels[0].texture==first && panels[0].previous==snapshot);submit(4);
  // Upload failure on the free spare must keep the last complete image.
  D3DTexture *complete=panels[0].texture;pending.erase(2);pending.erase(3);snapshot|=8;fail_upload=true;
  gfx_stream_menu_draw(&d,2,2,0,1000,false);assert(panels[0].texture==complete && panels[0].ready);fail_upload=false;
  pending.clear();gfx_stream_menu_draw(&d,2,2,0,1000,false);
  initial=uploads;unsigned created=allocations;for(int i=0;i<300;i++)gfx_stream_menu_draw(&d,2,2,0,1000,false);assert(uploads==initial && allocations==created);
  tick+=1000;gfx_stream_menu_draw(&d,2,2,0,1000,false);assert(uploads==initial+1);
  unsigned before=draws;guide=true;gfx_stream_menu_draw(&d,3,3,0,1000,false);assert(draws==before);guide=false;
  for(unsigned cycle=0;cycle<100;cycle++){
   snapshot=0;gfx_stream_menu_draw(&d,1,1,0,1000,false);
   snapshot=1|((cycle%6)<<1)|(100<<4);gfx_stream_menu_draw(&d,1,1,0,1000,false);
  }
  assert(allocations==created);reason="Stereo fallback: host negotiated stereo.";snapshot=1|8;gfx_stream_menu_draw(&d,1,1,0,1000,false);reason=NULL;
  snapshot=4096;gfx_stream_menu_draw(&d,4,4,0,1000,false);assert(last_x==16 && last_y==16 && last_right==528 && last_bottom==144);
  assert(hud_draws && hud_masks);pending.clear();unsigned start_uploads=uploads;tick+=1000;gfx_stream_menu_draw(&d,34,34,0,1000,false);assert(uploads==start_uploads+1 && panels[1].last_shown==34);
  before=draws;gfx_stream_menu_draw(&d,4,4,0,1000,true);assert(draws==before);
  submit(10);gfx_stream_menu_reset(&d);pending.clear();assert(allocations==releases && canvas_allocations==canvas_releases);
 }
 snapshot=1;fail_texture=true;unsigned before=draws;gfx_stream_menu_draw(&d,0,0,0,0,false);assert(unavailable==1 && draws==before);gfx_stream_menu_reset(&d);fail_texture=false;
 fail_upload=true;gfx_stream_menu_draw(&d,0,0,0,0,false);assert(unavailable==2 && draws==before);gfx_stream_menu_reset(&d);fail_upload=false;
 style=0;snapshot=1;gfx_stream_menu_draw(&d,1,1,0,1000,false);submit(20);
 unsigned no_failure=unavailable;D3DTexture *retained=panels[0].texture;fail_texture=true;snapshot|=2;
 gfx_stream_menu_draw(&d,1,1,0,1000,false);assert(panels[0].texture==retained && panels[0].ready && unavailable==no_failure);
 fail_texture=false;pending.clear();gfx_stream_menu_draw(&d,1,1,0,1000,false);gfx_stream_menu_reset(&d);
 assert(allocations==releases && canvas_allocations==canvas_releases && waits);
 puts("PASS: compact/sidebar, per-texture fences, progress with current texture busy, both-busy retention, failed-spare-upload rollback, 100 reopens/style, no recurring allocations, 1 Hz, Guide, stats and cleanup.");
}
