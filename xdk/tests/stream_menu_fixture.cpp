#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <set>
#include "src/video.h"
#include "src/performance.h"
#include "src/quick_menu.h"
#include "src/desktop_keyboard_layout.h"
#include "src/ui/smart_keyboard_remote.h"
#include "src/ui_layout_geometry.h"
typedef unsigned DWORD;
#define FAILED(x) ((x)<0)
#define D3DFMT_LIN_A8R8G8B8 1
#define GFX_SHADER_RGB 0
#define FONT_REGULAR 0
#define FONT_BOLD 1
struct canvas_t {uint32_t *px;int w,h;};
static unsigned desktop_snapshot;
static struct sk_remote_session stub_kb;
static bool stub_kb_active;
static unsigned snapshot,tick,allocations,releases,canvas_allocations,canvas_releases,uploads,draws,waits,unavailable;
static bool guide,fail_texture,fail_upload;
static const char *reason;
static int style;
static float last_x,last_y,last_right,last_bottom;
static std::set<DWORD> pending;
static bool gfx_fence_pending(DWORD fence){return pending.count(fence)!=0;}
static bool cv_init(canvas_t *c,int w,int h){c->px=(uint32_t*)calloc(w*h,4);c->w=w;c->h=h;canvas_allocations++;return c->px!=NULL;}
static void cv_free(canvas_t *c){free(c->px);c->px=NULL;canvas_releases++;}
static void cv_clip(canvas_t *c,int x,int y,int w,int h){assert(c->px && x>=0 && y>=0 && x+w<=c->w && y+h<=c->h);}
static void cv_unclip(canvas_t *){}
static void cv_fill(canvas_t *c,int x,int y,int w,int h,uint32_t){assert(c->px && x>=0 && y>=0 && x+w<=c->w && y+h<=c->h);}
static void cv_circle(canvas_t *c,float x,float y,float r,uint32_t){assert(c->px && x-r>=0 && y-r>=0 && x+r<c->w && y+r<c->h);}
static void cv_ring(canvas_t *c,float x,float y,float r,float,uint32_t){cv_circle(c,x,y,r,0);}
static void cv_line(canvas_t *c,float x,float y,float xx,float yy,float,uint32_t){assert(c->px && x>=0 && y>=0 && xx>=0 && yy>=0 && x<c->w && y<c->h && xx<c->w && yy<c->h);}
static void cv_triangle(canvas_t *c,float x,float y,float xx,float yy,float xxx,float yyy,uint32_t){assert(c->px && x>=0 && y>=0 && xx>=0 && yy>=0 && xxx>=0 && yyy>=0 && x<c->w && xx<c->w && xxx<c->w && y<c->h && yy<c->h && yyy<c->h);}
static void cv_rrect(canvas_t *c,float x,float y,float w,float h,float,uint32_t){assert(c->px && x>=0 && y>=0 && x+w<=c->w && y+h<=c->h);}
static void cv_rrect_outline(canvas_t *c,float x,float y,float w,float h,float,float,uint32_t){assert(c->px && x>=0 && y>=0 && x+w<=c->w && y+h<=c->h);}
static int font_text_width(int,float,const char *s){return s?(int)strlen(s)*8:0;}
static int font_draw(canvas_t *c,int,float,int,int y,uint32_t,const char *s){assert(c->px && y>0 && y<c->h && s);return 0;}
static void font_draw_fit(canvas_t *c,int,float,int x,int y,int width,uint32_t,const char *s){assert(c->px && x>=0 && y>0 && y<c->h && x+width<=c->w && s);}
static int font_draw_wrapped(canvas_t *c,int,float,int x,int y,int width,float,uint32_t,const char *s){assert(c->px && x>=0 && y>0 && y<584 && x+width<=c->w && s);return 1;}
struct D3DTexture {DWORD fence;int w,h;D3DTexture(int width,int height):fence(0),w(width),h(height){}void Release(){assert(!gfx_fence_pending(fence));releases++;delete this;}};
struct D3DDevice {
 D3DTexture *bound;
 int CreateTexture(int w,int h,int,int,int,int,D3DTexture **out,void*){
  assert((w%64)==0);
  assert((w==384 && h==480) || (w==448 && h==448) || (w==640 && h==300) ||
         (w==704 && (h==312 || h==352)) || (w==768 && (h==312 || h==352 || h==368 || h==416)) ||
         (w==896 && (h==368 || h==416)));
  if(fail_texture){*out=NULL;return -1;}
  *out=new D3DTexture(w,h);allocations++;return 0;
 }
 void SetTexture(int,D3DTexture *t){bound=t;}
};
static bool input_smart_keyboard_snapshot(struct sk_remote_session *out,unsigned *revision){
 if(!stub_kb_active)return false;
 *out=stub_kb;
 if(revision)*revision=stub_kb.revision;
 return true;
}
static unsigned input_desktop_snapshot(){return desktop_snapshot;}
static void input_desktop_ui_unavailable(){unavailable++;}
static unsigned input_quick_snapshot(){return snapshot;}
static bool input_mouse_active(){return false;}
static void video_get_refresh_snapshot(video_refresh_snapshot *out){memset(out,0,sizeof(*out));}
static int gfx_layout_width(){return style?1280:960;}
static void gfx_draw_layout_rect(D3DDevice *d,int mode,float x,float y,float r,float b);
static void input_quick_ui_unavailable(){unavailable++;}
static int audio_output_channels(){return 2;}
static uint32_t ui_theme_accent(){return 0xFF72C840;}
static uint32_t cv_mix(uint32_t a,uint32_t,unsigned){return a;}
static const char *ui_translate(const char *s){return s;}
static int ui_stream_menu_style(){return style;}
static void plat_log(const char*){}
static DWORD GetTickCount(){return tick;}
static bool gfx_system_ui_active(){return guide;}
static void gfx_wait_fence(D3DDevice*,DWORD fence){assert(fence);waits++;for(std::set<DWORD>::iterator i=pending.begin();i!=pending.end() && *i<=fence;)pending.erase(i++);}
static bool gfx_upload(D3DTexture *t,const void*,int pitch,int row,int height){
 assert(!gfx_fence_pending(t->fence) && pitch==t->w*4 && row==t->w*4 && height==t->h);
 uploads++;return !fail_upload;
}
static void gfx_draw_rect(D3DDevice *d,int,float x,float y,float r,float b){assert(d->bound);last_x=x;last_y=y;last_right=r;last_bottom=b;draws++;}
static void gfx_draw_layout_rect(D3DDevice *d,int mode,float x,float y,float r,float b){gfx_draw_rect(d,mode,x,y,r,b);}
/* ACTUAL_STREAM_MENU */
static void submit(DWORD fence){gfx_stream_menu_submitted(fence);for(int i=0;i<4;i++)if(panels[i].used){panels[i].texture->fence=fence;pending.insert(fence);}}
int main(){
 D3DDevice d={};gfx_stream_menu_draw(&d,1,1,0,1000,false);assert(!allocations && !draws);
 for(style=0;style<2;style++){
  snapshot=QUICK_OPEN|(100<<QUICK_VOLUME_SHIFT);gfx_stream_menu_draw(&d,1,1,0,1000,false);
  int width=style?384:448,height=style?480:448;assert(panels[0].canvas.w==width && panels[0].canvas.h==height);
  assert(last_x==(style?32.0f:256.0f) && last_y==(style?120.0f:136.0f) && last_right==(last_x+width) && last_bottom==(last_y+height));
  D3DTexture *first=panels[0].texture;unsigned initial=uploads;submit(1);
  snapshot|=2;gfx_stream_menu_draw(&d,2,2,0,1000,false);
  assert(uploads==initial+1 && panels[0].texture!=first && panels[0].spare==first);submit(2);
  snapshot=QUICK_OPEN|(2<<QUICK_ROW_SHIFT)|(100<<QUICK_VOLUME_SHIFT);initial=uploads;gfx_stream_menu_draw(&d,2,2,0,1000,false);assert(uploads==initial);submit(3); // both busy
  pending.erase(1);gfx_stream_menu_draw(&d,2,2,0,1000,false);assert(uploads==initial+1 && panels[0].texture==first && panels[0].previous==snapshot);submit(4);
  D3DTexture *complete=panels[0].texture;pending.erase(2);pending.erase(3);snapshot|=8;fail_upload=true;
  gfx_stream_menu_draw(&d,2,2,0,1000,false);assert(panels[0].texture==complete && panels[0].ready);fail_upload=false;
  pending.clear();gfx_stream_menu_draw(&d,2,2,0,1000,false);
  initial=uploads;unsigned created=allocations;for(int i=0;i<300;i++)gfx_stream_menu_draw(&d,2,2,0,1000,false);assert(uploads==initial && allocations==created);
  tick+=1000;gfx_stream_menu_draw(&d,2,2,0,1000,false);assert(uploads==initial+1);
  unsigned before=draws;guide=true;gfx_stream_menu_draw(&d,3,3,0,1000,false);assert(draws==before);guide=false;
  for(unsigned cycle=0;cycle<100;cycle++){
   snapshot=0;gfx_stream_menu_draw(&d,1,1,0,1000,false);
   snapshot=QUICK_OPEN|((cycle%8)<<QUICK_ROW_SHIFT)|(100<<QUICK_VOLUME_SHIFT);gfx_stream_menu_draw(&d,1,1,0,1000,false);
  }
  assert(allocations==created);
  for(unsigned om=0;om<4;om++){
   snapshot=QUICK_OPEN|(QUICK_PERFORMANCE<<QUICK_ROW_SHIFT)|(om<<QUICK_OVERLAY_SHIFT)|(om?QUICK_STATS:0u);
   gfx_stream_menu_draw(&d,4,4,0,1000,false);
  }
  before=draws;snapshot=QUICK_STATS|(2u<<QUICK_OVERLAY_SHIFT);gfx_stream_menu_draw(&d,4,4,0,1000,false);assert(draws==before);
  submit(10);gfx_stream_menu_reset(&d);pending.clear();assert(allocations==releases && canvas_allocations==canvas_releases);
 }

 /* CRT 4:3 (style=0 -> layout_width=960) & 16:9 Smart Keyboard <-> Desktop Toolbar isolation & reallocation */
 style=0;snapshot=0;desktop_snapshot=DESK_TOOLBAR;gfx_stream_menu_draw(&d,1,1,0,1000,false);
 assert(panels[2].ready && panels[2].canvas.w==640 && last_bottom==680 && last_y==380);submit(30);
 unsigned desktop_uploads=uploads,desktop_allocations=allocations;
 for(int i=0;i<300;i++)gfx_stream_menu_draw(&d,1,1,0,1000,false);
 assert(uploads==desktop_uploads && allocations==desktop_allocations);
 desktop_snapshot=DESK_KEYBOARD|(10<<2)|(1<<8);gfx_stream_menu_draw(&d,1,1,0,1000,false);
 assert(uploads==desktop_uploads+1);submit(31);

 /* Open Smart Keyboard in CRT 4:3 mode on separate panels[3] -> toggle Full PC / Compact -> return to Toolbar */
 memset(&stub_kb,0,sizeof(stub_kb));
 stub_kb.active=true;stub_kb.sk.full_pc=true;stub_kb.layout=SK_LAYOUT_FULL;stub_kb.revision=1;
 stub_kb_active=true;pending.clear();
 gfx_stream_menu_draw(&d,1,1,0,1000,false);
 assert(panels[3].ready && panels[3].canvas.w==896 && panels[3].canvas.h==416);
 submit(32);
 stub_kb.sk.full_pc=false;stub_kb.layout=SK_LAYOUT_COMPACT;stub_kb.revision=2;
 gfx_stream_menu_draw(&d,1,1,0,1000,false);
 assert(panels[3].ready && panels[3].canvas.w==768 && panels[3].canvas.h==312);
 submit(33);
 stub_kb_active=false;
 gfx_stream_menu_draw(&d,1,1,0,1000,false);
 assert(panels[2].ready && panels[2].canvas.w==640 && panels[2].canvas.h==300 && !panels[3].used);
 pending.clear();
 desktop_snapshot=0;unsigned closed_draws=draws;gfx_stream_menu_draw(&d,1,1,0,1000,false);
 assert(draws==closed_draws && !panels[2].used && !panels[3].used);

 /* Verify CRT 240p (640x240) UI projection preserves full 4:3 aspect without half-width crush */
 int vl=0,vt=0,vr=0,vb=0;
 ui_layout_safe_viewport(960,720,640,240,5,&vl,&vt,&vr,&vb);
 assert(vl==32 && vr==608 && vt==12 && vb==228);

 gfx_stream_menu_reset(&d);pending.clear();
 snapshot=1;fail_texture=true;unsigned before=draws;gfx_stream_menu_draw(&d,0,0,0,0,false);assert(unavailable==1 && draws==before);gfx_stream_menu_reset(&d);fail_texture=false;
 fail_upload=true;gfx_stream_menu_draw(&d,0,0,0,0,false);assert(unavailable==2 && draws==before);gfx_stream_menu_reset(&d);fail_upload=false;
 style=0;snapshot=1;gfx_stream_menu_draw(&d,1,1,0,1000,false);submit(20);
 unsigned no_failure=unavailable;D3DTexture *retained=panels[0].texture;fail_texture=true;snapshot|=2;
 gfx_stream_menu_draw(&d,1,1,0,1000,false);assert(panels[0].texture==retained && panels[0].ready && unavailable==no_failure);
 fail_texture=false;pending.clear();gfx_stream_menu_draw(&d,1,1,0,1000,false);gfx_stream_menu_reset(&d);
 snapshot=QUICK_STOPPING;gfx_stream_menu_draw(&d,1,1,0,1000,false);
 assert(panels[0].ready && panels[0].previous==QUICK_STOPPING && panels[0].used);
 unsigned stop_allocations=allocations;submit(30);tick+=16;
 gfx_stream_menu_draw(&d,1,1,0,1000,false);assert(allocations==stop_allocations);
 gfx_stream_menu_reset(&d);
 assert(allocations==releases && canvas_allocations==canvas_releases && waits);
 puts("PASS: clean quick menu layout, 64px-aligned CRT/16:9 keyboard & panel isolation, 240p UI viewport, fences, and cleanup.");
}
