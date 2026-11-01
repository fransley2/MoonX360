#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vector>
#include "../src/ui/ui.h"
#include "../src/ui/draw.h"
#include "../src/ui/cover_cache.h"
#include "../src/system_ui.h"
#include "../src/launch_cover.h"
typedef unsigned DWORD,UINT;typedef unsigned char BYTE;typedef int HRESULT,LONG;
static DWORD tick;
static DWORD GetTickCount(){return tick;}
static LONG InterlockedExchange(volatile LONG *p,LONG n){LONG old=*p;*p=n;return old;}
static LONG InterlockedCompareExchange(volatile LONG *p,LONG n,LONG old){LONG value=*p;if(value==old)*p=n;return value;}
#define D3DCLEAR_TARGET 1
#define TRUE 1
#define FALSE 0
#define FAILED(h) ((h)<0)
#define SUCCEEDED(h) ((h)>=0)
#define D3DDECLTYPE_FLOAT2 0
#define D3DDECLTYPE_D3DCOLOR 1
#define D3DDECLMETHOD_DEFAULT 0
#define D3DDECLUSAGE_POSITION 0
#define D3DDECLUSAGE_TEXCOORD 1
#define D3DDECLUSAGE_COLOR 2
#define D3DDECL_END() {0,0,0,0,0,0}
#define D3DFMT_LIN_A8R8G8B8 0
#define D3DRS_ALPHABLENDENABLE 1
#define D3DRS_SRCBLEND 2
#define D3DRS_DESTBLEND 3
#define D3DRS_SCISSORTESTENABLE 4
#define D3DBLEND_SRCALPHA 1
#define D3DBLEND_INVSRCALPHA 2
#define D3DPT_TRIANGLELIST 0
#define GFX_WIDTH 1280
#define GFX_HEIGHT 720
#define GFX_SHADER_RGB 0
struct D3DVERTEXELEMENT9 {int a,b,c,d,e,f;};
struct RECT {LONG left,top,right,bottom;};
static bool pending,fail_create,fail_upload,fail_shader,fail_glyph,owned;
static unsigned allocations,releases,uploads,waits,draws,primitives,compiles,presents;
static int physical_w=1920,physical_h=1080;
struct D3DTexture {
    int w,h;D3DTexture(int a,int b):w(a),h(b){allocations++;}
    void Release(){assert(!pending);releases++;delete this;}
};
struct D3DVertexDeclaration {};
struct D3DVertexShader {};
struct D3DPixelShader {};
struct ID3DXBuffer {
    DWORD value;void *GetBufferPointer(){return &value;}void Release(){delete this;}
};
static HRESULT D3DXCompileShader(const char*,UINT,void*,void*,const char*,const char*,int,ID3DXBuffer **code,ID3DXBuffer**,void*) {
    compiles++;if(fail_shader)return -1;*code=new ID3DXBuffer;return 0;
}
static void OutputDebugStringA(const char*) {}
static uint64_t gfx_now_us(){static uint64_t now;return ++now;}
static void plat_log(const char*) {}
static bool gfx_upload(D3DTexture *t,const void*,int pitch,int bytes,int rows) {
    assert(!pending && pitch==t->w*4 && bytes==pitch && rows==t->h);uploads++;return !fail_upload;
}
static void gfx_backbuffer_size(int *w,int *h){*w=physical_w;*h=physical_h;}
struct D3DDevice {
    bool blend,scissor;D3DTexture *bound;
    void Clear(int,void*,int,DWORD,float,int){}
    void BlockOnFence(DWORD f){assert(f);waits++;pending=false;}
    HRESULT CreateVertexDeclaration(const D3DVERTEXELEMENT9*,D3DVertexDeclaration **out){static D3DVertexDeclaration p;*out=&p;return 0;}
    HRESULT CreateVertexShader(DWORD*,D3DVertexShader **out){static D3DVertexShader p;*out=&p;return 0;}
    HRESULT CreatePixelShader(DWORD*,D3DPixelShader **out){static D3DPixelShader p;*out=&p;return 0;}
    HRESULT CreateTexture(int w,int h,int,int,int,int,D3DTexture **out,void*){
        assert(!pending);if(fail_create || (fail_glyph && w==1024)){*out=NULL;return -1;}*out=new D3DTexture(w,h);return 0;
    }
    void SetScissorRect(RECT *r){assert(r->left>=0 && r->right<=physical_w && r->top>=0 && r->bottom<=physical_h);}
    void SetVertexDeclaration(D3DVertexDeclaration*){}
    void SetVertexShader(D3DVertexShader*){}
    void SetPixelShader(D3DPixelShader*){}
    void SetPixelShaderConstantF(int,const float*,int){}
    void SetRenderState(int state,int value){if(state==D3DRS_ALPHABLENDENABLE)blend=value!=0;if(state==D3DRS_SCISSORTESTENABLE)scissor=value!=0;}
    void SetTexture(int,D3DTexture *t){bound=t;}
    void DrawPrimitiveUP(int,unsigned n,const void*,unsigned){assert(blend&&n);draws++;primitives+=n;}
    void BlockUntilIdle(){pending=false;}
    DWORD InsertFence(){static DWORD f;pending=true;return ++f;}
    void Present(void*,void*,void*,void*){presents++;}
};
static bool guide;
static bool gfx_system_ui_active(){return guide;}
static bool gfx_fence_pending(DWORD f){return f && pending;}
static void gfx_wait_fence(D3DDevice *d,DWORD f){d->BlockOnFence(f);}
bool gfx_ui_ready(void);
static D3DDevice *gfx_begin();
static void gfx_end();
/* ACTUAL_RENDERER */
static D3DDevice present_device={};
static D3DDevice *gfx_begin(){assert(!owned);owned=true;return &present_device;}
static void gfx_end(){assert(owned);owned=false;}
static void gfx_draw_rect(D3DDevice *d,int shader,float,float,float,float){assert(d->bound && shader==GFX_SHADER_RGB);}
static D3DTexture *mouse_indicator;
static bool mouse_indicator_failed;
static void gfx_stream_menu_reset(D3DDevice*){}
/* ACTUAL_PRESENT */
static void preview(const char *name) {
    ui_draw_software();canvas_t *c=ui_canvas();FILE *f=fopen(name,"wb");assert(f);
    fprintf(f,"P6\n%d %d\n255\n",c->w,c->h);
    for(int i=0;i<c->w*c->h;i++) {unsigned char rgb[]={(unsigned char)(c->px[i]>>16),(unsigned char)(c->px[i]>>8),(unsigned char)c->px[i]};fwrite(rgb,1,3,f);}fclose(f);
}
int main() {
    ui_draw_enable(true);assert(ui_init(1280,720));assert(ui_apps_columns()==6);
    D3DDevice d={};ui_host host={"Gaming PC","192.168.1.3",HOST_PAIRED,NULL,NULL};
    ui_app apps[20]={};uint32_t art[150*200];for(int i=0;i<150*200;i++)art[i]=HEX(0x66884A);
    for(int i=0;i<20;i++) {apps[i].name="Example game";apps[i].art=art;apps[i].generation=1;}
    ui_draw_library(&host,apps,20,0,"All Apps");
    int count;const ui_draw_command *commands=ui_draw_commands(&count);assert(commands&&count>100);
    unsigned glyph_commands=0;for(int i=0;i<count;i++)if(commands[i].kind==UI_GLYPH)glyph_commands++;
    fail_shader=true;assert(!gfx_ui_draw(&d));fail_shader=false;
    fail_create=true;assert(!gfx_ui_draw(&d));fail_create=false;
    fail_upload=true;assert(!gfx_ui_draw(&d));fail_upload=false;
    assert(gfx_ui_draw(&d));assert(draws<glyph_commands && !d.blend&&!d.scissor&&!d.bound);
    struct ui_memory_info memory;ui_draw_memory(&memory);
    /* Existing image accounting includes the one embedded brand image as well as art. */
    assert(memory.glyph_bytes==1024*512*4 && memory.button_gpu_bytes==256*32*4 && memory.gpu_covers==2);
    /* The brand must not displace any of the twelve existing cover slots. */
    static uint32_t distinct_art[12][150*200];
    for(int i=0;i<12;i++)apps[i].art=distinct_art[i];
    gfx_ui_release(&d); // isolate capacity from the earlier shared test image
    ui_set_motion(false);
    ui_draw_library(&host,apps,12,0,"All Apps");assert(gfx_ui_draw(&d));
    ui_draw_library(&host,apps,12,6,"All Apps");assert(gfx_ui_draw(&d));
    ui_draw_library(&host,apps,12,11,"All Apps");assert(gfx_ui_draw(&d)); // visit the final virtualized rail window
    ui_draw_memory(&memory);assert(memory.gpu_covers==13);
    for(int cover=0;cover<12;cover++) {
        bool cached=false;for(int slot=0;slot<IMAGE_MAX;slot++)if(images[slot].key==distinct_art[cover])cached=true;
        assert(cached); // twelve distinct art buffers remain resident alongside brand/buttons
    }
    ui_set_motion(true);
    for(int i=0;i<12;i++)apps[i].art=art;
    ui_draw_library(&host,apps,20,0,"All Apps");assert(gfx_ui_draw(&d));
    unsigned initial=uploads;pending=true;gfx_ui_submitted(1);
    assert(!gfx_ui_draw(&d));assert(uploads==initial && !waits);pending=false;assert(gfx_ui_draw(&d)); // reuse without re-upload
    pending=false;gfx_ui_submitted(2);apps[0].generation=2;
    ui_draw_library(&host,apps,20,0,"All Apps");assert(gfx_ui_draw(&d));assert(uploads>initial); // recycled pointer
    preview("gpu-command-library-fixture.ppm");
    for(int p=0;p<80;p++) {
        int page=p%UI_PAGE_COUNT;ui_set_page(page);ui_draw_library(&host,apps,20,p%20,"All Apps");assert(gfx_ui_draw(&d));
        pending=false;gfx_ui_submitted((DWORD)p+3);
        assert(ui_tab_move(page,1)>=0 && ui_tab_move(page,-1)<UI_PAGE_COUNT);
    }
    ui_draw_app_details(&host,apps,"720p60 Quality","5 s",0);assert(gfx_ui_draw(&d));preview("app-details-fixture.ppm");
    ui_draw_library(&host,NULL,0,0,"Favorites");assert(gfx_ui_draw(&d));preview("empty-library-fixture.ppm");
    ui_setting rows[]={{"Quality profile","720p60 Quality","Applies on next connection."}};
    for(int p=0;p<40;p++) {ui_draw_settings_page("Stream",rows,1,0);assert(gfx_ui_draw(&d));}
    ui_draw_performance("Good","720p60 | 30 Mbps",59,"Good","9 ms","None detected","Synthetic preview data",false);
    assert(gfx_ui_draw(&d));preview("performance-fixture.ppm");
    for(int columns=4;columns<=6;columns++)for(int total=0;total<65;total++)for(int selected=0;selected<total;selected++) {
        int first,n=ui_cover_window(selected,total,columns,&first);assert(first>=0&&n<=UI_COVER_LIMIT&&first+n<=total);
        assert(selected>=first && selected<first+n);
        assert(ui_grid_move(selected,total,columns,-1,0)>=0);
        if(selected%columns==0)assert(ui_grid_move(selected,total,columns,-1,0)==selected);
    }
    for(int resolution=0;resolution<2;resolution++) {
        physical_w=resolution?1280:1920;physical_h=resolution?720:1080;
        ui_draw_library(&host,apps,20,19,"All Apps");assert(gfx_ui_draw(&d));
        pending=true;gfx_ui_submitted(100);gfx_ui_release(&d);assert(!ui_draw_commands(&count));
        gfx_ui_release(&d);assert(allocations==releases);
        ui_draw_library(&host,apps,20,0,"All Apps");assert(gfx_ui_draw(&d)); // second menu after stream release
    }
    ui_draw_begin(ui_canvas());for(int i=0;i<UI_DRAW_CAPACITY+2;i++)ui_fill(ui_canvas(),1,1,1,1,HEX(0x123456));
    assert(!ui_draw_commands(&count)&&ui_canvas()->px[1280+1]==HEX(0x123456)); // complete software fallback
    ui_draw_enable(false);gfx_ui_release(&d);assert(allocations==releases);
    struct system_ui_state sys={};
    for(int i=0;i<100;i++){assert(system_ui_change(&sys,true));assert(!system_ui_change(&sys,true));guide=true;unsigned before=presents;ui_present();assert(presents==before&&!owned);assert(system_ui_change(&sys,false));guide=false;}
    assert(sys.opens==100&&sys.resumes==100);
    ui_draw_enable(true);ui_draw_library(&host,apps,20,0,"All Apps");ui_present();assert(presents==1&&!owned&&ui_draw_enabled());
    const ui_draw_command *steady=ui_draw_commands(&count);std::vector<ui_draw_command> steady_commands(steady,steady+count);
    assert(ui_focus_tick(0));unsigned before_motion=presents,before_motion_uploads=uploads;ui_present();assert(presents==before_motion); // pending prior fence
    assert(ui_focus_tick(120));guide=true;pending=false;ui_present();assert(presents==before_motion);
    assert(ui_focus_tick(160));guide=false;ui_present();assert(presents==before_motion+1 && !ui_focus_tick(176));
    assert(uploads==before_motion_uploads);
    steady=ui_draw_commands(&count);assert(!memcmp(steady_commands.data(),steady,count*sizeof(*steady)));

    gfx_release_ui_textures();assert(allocations==releases&&!owned);
    ui_draw_library(&host,apps,20,0,"All Apps");ui_draw_software();
    canvas_t *fallback_canvas=ui_canvas();std::vector<uint32_t> steady_pixels(fallback_canvas->px,fallback_canvas->px+fallback_canvas->w*fallback_canvas->h);
    ui_draw_library(&host,apps,20,0,"All Apps");assert(ui_focus_tick(200));unsigned before_fallback=presents;fail_glyph=true;ui_present();fail_glyph=false;
    assert(!memcmp(steady_pixels.data(),fallback_canvas->px,steady_pixels.size()*sizeof(uint32_t)));

    assert(presents==before_fallback+1 && !owned && !ui_draw_enabled() && (ui_tex[0]||ui_tex[1]));
    gfx_release_ui_textures();gfx_release_ui_textures();assert(allocations==releases&&!owned);
    // Even a missing image retains the exact tile geometry in GPU/software layouts.
    apps[0].art=NULL;ui_draw_enable(true);ui_draw_library(&host,apps,20,0,"All Apps");ui_launch_begin(apps,tick,true);
    float ox,oy,ow,oh;ui_launch_origin(&ox,&oy,&ow,&oh);assert(ox==64 && oy==212 && ow==198 && oh==264);
    ui_draw_enable(false);ui_draw_library(&host,apps,20,0,"All Apps");ui_launch_begin(apps,tick,true);
    ui_launch_origin(&ox,&oy,&ow,&oh);assert(ox==64 && ow==198 && oh==264);apps[0].art=art;
    // Real opening handoff: bounded dedicated resources survive menu release; no
    // glyph atlas, cached menu image, draw command or CPU cover is needed after.
    ui_draw_enable(true);ui_draw_library(&host,apps,20,0,"All Apps");
    ui_launch_begin(apps,100,true);
    preview("launch-cover-centered.ppm");pending=false;
    tick=0;launch_begin(5000,true);assert(launch_offer_art(art,150,200,1));assert(launch_prepare());gfx_release_ui_textures();
    assert(allocations-releases==4 && launch_active());unsigned launch_allocations=allocations;
    tick=999;launch_reveal=1; // Reveal still cannot reveal without a valid frame.
    D3DDevice *owner=gfx_begin();gfx_launch_idle(owner);gfx_end();assert(launch_active());launch_reveal=0;
    tick=1000;launch_decoded();owner=gfx_begin();gfx_launch_draw(owner,true);gfx_end();assert(cover.first_ms==1000);
    for(tick=1001;tick<6000;tick+=53){owner=gfx_begin();gfx_launch_draw(owner,true);gfx_end();assert(launch_active() && !cover.revealing);}
    tick=6000;owner=gfx_begin();gfx_launch_draw(owner,true);gfx_end();assert(cover.revealing && launch_active());
    unsigned during_fade=presents;owner=gfx_begin();gfx_launch_idle(owner);gfx_end();assert(presents==during_fade);
    pending=false;tick=6250;owner=gfx_begin();gfx_launch_draw(owner,true);gfx_end();assert(!launch_active());
    launch_collect();assert(!launch_staging);
    pending=false;owner=gfx_begin();gfx_launch_idle(owner);gfx_end();assert(!gfx_launch_pending() && allocations==releases && allocations==launch_allocations);
    for(int session=0;session<12;session++) {
        ui_draw_enable(true);ui_draw_library(&host,apps,20,0,"All Apps");ui_launch_begin(apps,tick,session%3!=0);
        pending=false;launch_begin(session%2?0:10000,session%3!=0);launch_prepare();tick+=400;assert(launch_active() && !cover.first);
        owner=gfx_begin();gfx_launch_draw(owner,false);gfx_end();assert(launch_active());
        if(session%2){launch_decoded();launch_command(false,true);owner=gfx_begin();gfx_launch_draw(owner,true);gfx_end();tick+=250;owner=gfx_begin();gfx_launch_draw(owner,true);gfx_end();assert(!launch_active());}
        else {launch_command(true,false);tick+=30000;launch_decoded();owner=gfx_begin();gfx_launch_draw(owner,true);gfx_end();assert(launch_active() && cover.cancelled);}
        launch_end();assert(!owned && !launch_active() && !gfx_launch_pending() && allocations==releases);
    }
    ui_draw_enable(true);ui_draw_library(&host,apps,20,0,"All Apps");ui_launch_begin(apps,tick,true);
    pending=false;launch_begin(5000,true);assert(launch_prepare());unsigned late_allocations=allocations;
    tick+=1200;pending=false;owner=gfx_begin();gfx_launch_draw(owner,false);gfx_end();
    unsigned movement_start=visual.started;assert(!visual.art);
    assert(launch_offer_art(art,150,200,12));assert(!launch_offer_art(art,150,200,12));
    tick+=100;pending=false;owner=gfx_begin();gfx_launch_draw(owner,false);gfx_end();
    assert(visual.art && visual.art_started==tick && visual.started==movement_start);
    launch_visual_sample late;launch_visual_get(&visual,tick,&late);assert(late.art==0 && late.x==514);
    launch_visual_get(&visual,tick+125,&late);assert(late.art==.5f && late.x==514);
    assert(allocations==late_allocations);launch_end();assert(!launch_offer_art(art,150,200,13));
    assert(allocations==releases);
    // Stereo fallback keeps one visual lifetime and all resources, while resetting only media readiness.
    ui_draw_enable(true);ui_draw_library(&host,apps,20,0,"All Apps");ui_launch_begin(apps,tick,true);
    launch_begin(5000,true);assert(launch_prepare());unsigned retry_allocations=allocations,retry_clock=visual.started;
    uint32_t *retry_staging=launch_staging;launch_decoded();launch_command(false,true);launch_retry();
    assert(!launch_has_video() && !cover.first && !cover.revealing && !launch_reveal && launch_active());
    assert(visual.started==retry_clock && allocations==retry_allocations && launch_staging==retry_staging);
    launch_end();assert(allocations==releases);
    fail_create=true;launch_begin(5000,false);assert(!launch_prepare());fail_create=false;
    owner=gfx_begin();gfx_launch_draw(owner,false);gfx_end();assert(launch_active());launch_end();assert(allocations==releases);
    puts("PASS: actual opening GPU handoff, bounded retained resources, no per-frame allocations, first frame/5s/250ms/early reveal/cancel/error/static/consecutive sessions and fence-safe release");
    printf("PASS: actual GPU menu commands, glyph batching, cache generation, shader/allocation/upload fallback, atlas, fences, repeated cleanup, navigation, empty library and 720/1080 scaling.\n");
}
