#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <chrono>
#include <string>
#include "src/video.h"
#include "src/quick_menu.h"
#include "src/desktop_keyboard_layout.h"
#include "src/ui/canvas.h"
#include "src/ui/font.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third_party/stb/stb_image_write.h"
typedef unsigned DWORD;
#define FAILED(x) ((x)<0)
#define D3DFMT_LIN_A8R8G8B8 1
#define GFX_SHADER_RGB 0
struct D3DTexture {void Release(){delete this;}};
struct D3DDevice {
    int CreateTexture(int,int,int,int,int,int,D3DTexture **out,void*){*out=new D3DTexture;return 0;}
    void SetTexture(int,D3DTexture*){}
};
static unsigned snapshot,tick,desktop_snapshot;
static unsigned input_desktop_snapshot(){return desktop_snapshot;}
static void input_desktop_ui_unavailable(){}
static int style,language;
struct ui_translation {const char *en,*es,*pt_br;};
#include "src/ui/quick_translations.h"
static bool gfx_fence_pending(DWORD){return false;}
static void gfx_wait_fence(D3DDevice*,DWORD){}
static bool gfx_upload(D3DTexture*,const void*,int,int,int){return true;}
static void gfx_draw_layout_rect(D3DDevice*,int,float,float,float,float){}
static void gfx_draw_hud_rect(D3DDevice*,float,float,float,float){}
static void gfx_hud_pixels(uint32_t*,int,int){}
static bool gfx_system_ui_active(){return false;}
static int gfx_layout_width(){return style?1280:960;}
static void gfx_backbuffer_size(int *w,int *h){*w=640;*h=480;}
static unsigned input_quick_snapshot(){return snapshot;}
static bool input_mouse_active(){return false;}
static void input_quick_ui_unavailable(){}
static int audio_output_channels(){return 2;}
static uint32_t ui_theme_accent(){return 0xFF72C840;}
static const char *ui_translate(const char *s){
    if(!language)return s;
    for(unsigned i=0;i<sizeof(quick_translations)/sizeof(quick_translations[0]);i++)
        if(!strcmp(s,quick_translations[i].en))
            return language==2?quick_translations[i].pt_br:quick_translations[i].es;
    return s;
}
static int ui_stream_menu_style(){return style;}
static void plat_log(const char*){}
static DWORD GetTickCount(){return tick;}
static bool video_xdk_is_streaming(){return true;}
static void video_get_stats(video_stats *out){
    memset(out,0,sizeof(*out));out->actual_w=640;out->actual_h=480;
    out->display_w=640;out->display_h=480;out->decoded=60;
}
static void video_get_quick_info(video_quick_info *out){
    memset(out,0,sizeof(*out));out->requested_w=640;out->requested_h=480;
    out->requested_fps=60;out->bitrate_kbps=10000;out->decode_workers=4;
    out->slices=8;strcpy(out->host_name,"Living Room PC");
    strcpy(out->pipeline,"CACHED_GPU");
}
static void video_get_refresh_snapshot(video_refresh_snapshot *out){
    memset(out,0,sizeof(*out));out->state=VIDEO_REFRESH_IDLE;
}
/* ACTUAL_STREAM_MENU */
static void write_panel(const char *path,int which=0){
    stream_panel *p=panels+which;
    std::vector<unsigned char> rgb(p->canvas.w*p->canvas.h*3);
    for(int i=0;i<p->canvas.w*p->canvas.h;i++){
        uint32_t v=p->canvas.px[i];
        rgb[i*3]=(unsigned char)(v>>16);
        rgb[i*3+1]=(unsigned char)(v>>8);
        rgb[i*3+2]=(unsigned char)v;
    }
    if(!stbi_write_png(path,p->canvas.w,p->canvas.h,3,rgb.data(),p->canvas.w*3))abort();
}
int main(int argc,char **argv){
    if(argc!=4 || !font_init())return 1;
    tick=1000;style=argv[2][0]=='1';language=argv[3][0]-'0';
    snapshot=QUICK_OPEN|(80<<QUICK_VOLUME_SHIFT);
    D3DDevice d;gfx_stream_menu_draw(&d,60,60,0,1000,false);write_panel(argv[1]);
    std::string stem=argv[1];stem.resize(stem.size()-4);
    snapshot=QUICK_OPEN|(QUICK_DETAILS<<QUICK_PAGE_SHIFT)|(80<<QUICK_VOLUME_SHIFT);
    tick+=1000;gfx_stream_menu_draw(&d,120,120,0,1000,false);
    write_panel((stem+"-details.png").c_str());
    snapshot=QUICK_OPEN|(QUICK_CONFIRM<<QUICK_PAGE_SHIFT)|(80<<QUICK_VOLUME_SHIFT);
    tick+=1000;gfx_stream_menu_draw(&d,180,180,0,1000,false);
    write_panel((stem+"-confirm.png").c_str());
    snapshot=QUICK_OPEN|(QUICK_DESKTOP_PAGE<<QUICK_PAGE_SHIFT);
    gfx_stream_menu_draw(&d,1,1,0,1000,false);write_panel((stem+"-desktop-menu.png").c_str());
    snapshot=0;desktop_snapshot=DESK_TOOLBAR|(3<<2);
    gfx_stream_menu_draw(&d,1,1,0,1000,false);write_panel((stem+"-toolbar.png").c_str(),2);
    desktop_snapshot=DESK_KEYBOARD|(0<<2)|(1<<8);
    gfx_stream_menu_draw(&d,1,1,0,1000,false);write_panel((stem+"-keyboard.png").c_str(),2);
    desktop_snapshot=0;
    double total_ms=0,peak_ms=0;
    for(int i=0;i<100;i++){
        tick+=16;snapshot=QUICK_OPEN|((i%8)<<QUICK_ROW_SHIFT)|(80<<QUICK_VOLUME_SHIFT);
        auto a=std::chrono::steady_clock::now();
        gfx_stream_menu_draw(&d,60+i,60+i,0,1000,false);
        auto b=std::chrono::steady_clock::now();
        double ms=std::chrono::duration<double,std::milli>(b-a).count();
        total_ms+=ms;if(ms>peak_ms)peak_ms=ms;
    }
    printf("host_canvas_style=%d language=%d mean_ms=%.3f peak_ms=%.3f updates=%u\n",
        style,language,total_ms/100,peak_ms,panels[0].updates);
    gfx_stream_menu_reset(&d);
    return 0;
}
