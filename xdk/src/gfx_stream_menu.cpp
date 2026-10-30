#include "xdk.h"
#include "../../src/input.h"
#include "../../src/audio.h"
#include "../../src/ui/ui.h"
#include "../../src/ui/font.h"
#include "../../src/ui/canvas.h"
#include "../../src/platform.h"
#include <stdio.h>
#include <string.h>
struct stream_panel {D3DTexture *texture,*spare;canvas_t canvas;DWORD fence,spare_fence,updated;unsigned previous;bool failed,used,ready,was_open,spare_failed;unsigned raster_total,raster_max,updates,last_shown;};
static struct stream_panel panels[2];
void gfx_stream_menu_reset(D3DDevice *d)
{
    for(int i=0;i<2;i++) {
        stream_panel *p=panels+i;if(p->fence)gfx_wait_fence(d,p->fence);
        if(p->spare_fence)gfx_wait_fence(d,p->spare_fence);
        if(p->texture)p->texture->Release();if(p->spare)p->spare->Release();if(p->canvas.px)cv_free(&p->canvas);memset(p,0,sizeof(*p));
    }
}
static void swap_panel(stream_panel *p)
{
    D3DTexture *texture=p->texture;p->texture=p->spare;p->spare=texture;
    DWORD fence=p->fence;p->fence=p->spare_fence;p->spare_fence=fence;
}
static void line(stream_panel *p,int y,const char *text,bool selected)
{
    if(selected)cv_fill(&p->canvas,12,y-23,488,30,ui_theme_accent());
    font_draw_fit(&p->canvas,FONT_REGULAR,20,20,y,472,selected?0xFF132316:0xFFC0C9C5,text);
}
static void menu_row(stream_panel *p,int y,const char *label,const char *value,bool selected,bool danger)
{
    if(selected) {
        cv_rrect(&p->canvas,16,(float)y-32,416,56,6,cv_mix(0xFF22272B,ui_theme_accent(),32));
        cv_rrect_outline(&p->canvas,16,(float)y-32,416,56,6,2,ui_theme_accent());
        cv_fill(&p->canvas,16,y-21,3,32,ui_theme_accent());
    }
    uint32_t ink=danger?0xFFF1B172:0xFFF2F5F3;
    font_draw_fit(&p->canvas,selected?FONT_BOLD:FONT_REGULAR,21,30,y,264,ink,label);
    font_draw_fit(&p->canvas,FONT_REGULAR,18,306,y,108,selected?0xFFF2F5F3:0xFFC0C9C5,value);
}
static void compact_menu(stream_panel *p,unsigned state,bool statistics,const char *audio)
{
    cv_fill(&p->canvas,1,1,510,52,0xFF202B26);cv_fill(&p->canvas,1,1,510,3,ui_theme_accent());
    font_draw_fit(&p->canvas,FONT_BOLD,24,20,35,330,0xFFF2F5F3,ui_translate("Quick menu"));
    font_draw_fit(&p->canvas,FONT_REGULAR,15,374,34,114,0xFFC0C9C5,"Back + L3");
    const char *labels[]={"Resume stream","Volume","Mouse mode","Statistics","Audio output","Disconnect stream"};
    unsigned selected=(state>>1)&7,volume=(state>>4)&127;char text[64];
    for(int i=0;i<6;i++) {
        bool on=(unsigned)i==selected;int y=80+i*32;
        if(on)cv_fill(&p->canvas,12,y-23,488,30,ui_theme_accent());
        uint32_t ink=on?0xFF132316:i==5?0xFFF1B172:0xFFF2F5F3;
        const char *value=i==2?(input_mouse_active()?"On":"Off"):i==3?(statistics?"On":"Off"):i==4?audio:"";
        if(i==1){snprintf(text,sizeof(text),"%u%%",volume);value=(state&2048)?ui_translate("Muted"):text;}
        else value=ui_translate(value);
        font_draw_fit(&p->canvas,on?FONT_BOLD:FONT_REGULAR,20,24,y,264,ink,ui_translate(labels[i]));
        font_draw_fit(&p->canvas,FONT_REGULAR,18,304,y,184,ink,value);
    }
    const char *reason=audio_fallback_reason();if(reason)line(p,275,ui_translate(reason),false);
    cv_fill(&p->canvas,16,285,480,1,0xFF414A4D);
    font_draw_fit(&p->canvas,FONT_REGULAR,15,24,309,170,0xFFC0C9C5,ui_translate("A: select"));
    font_draw_fit(&p->canvas,FONT_REGULAR,15,210,309,270,0xFFC0C9C5,ui_translate("B: resume / Left-Right: volume"));
}
void gfx_stream_menu_draw(D3DDevice *d,unsigned decoded,unsigned shown,unsigned dropped,unsigned decode_us,bool detailed)
{
    unsigned state=input_quick_snapshot();bool open=(state&1)!=0,statistics=(state&4096)!=0;
    if(!open && panels[0].was_open) {
        char report[160];snprintf(report,sizeof(report),"[UI] stream menu style=%d updates=%u raster_total_ms=%u raster_peak_ms=%u\n",ui_stream_menu_style(),panels[0].updates,panels[0].raster_total,panels[0].raster_max);
        plat_log(report);panels[0].was_open=false;
    }
    for(int i=0;i<2;i++)panels[i].used=false;
    if(gfx_system_ui_active() || (!open && (!statistics || detailed)))return;
    /* One retained sidebar plus the existing compact stats panel. Geometry is
     * in the renderer's 1280x720 logical space; no per-frame animation/upload. */
    bool sidebar=open && ui_stream_menu_style()==1;
    int which=open?0:1,width=sidebar?448:512,height=open?(sidebar?640:320):128;stream_panel *p=panels+which;
    if(open && !p->was_open){p->was_open=true;p->updates=p->raster_total=p->raster_max=0;}
    if(p->failed)return;
    if(!p->texture) {
        if(!cv_init(&p->canvas,width,height)){p->failed=true;if(which==0)input_quick_ui_unavailable();return;}
        if(FAILED(d->CreateTexture(width,height,1,0,D3DFMT_LIN_A8R8G8B8,0,&p->texture,NULL))){cv_free(&p->canvas);p->failed=true;if(which==0)input_quick_ui_unavailable();return;}
        p->previous=~state;
    }
    DWORD now=GetTickCount();
    bool update=state!=p->previous || now-p->updated>=1000,swapped=false;
    if(update && gfx_fence_pending(p->fence)) {
        /* The inactive texture keeps its original fence, so a stream that is
         * continuously one frame in flight cannot starve every menu update. */
        if(!p->spare && !p->spare_failed) {
            if(FAILED(d->CreateTexture(width,height,1,0,D3DFMT_LIN_A8R8G8B8,0,&p->spare,NULL)))p->spare_failed=true;
        }
        if(p->spare && !gfx_fence_pending(p->spare_fence)){swap_panel(p);swapped=true;}
        else update=false;
    }
    if(update) {
        DWORD raster_started=GetTickCount();
        cv_fill(&p->canvas,0,0,width,height,0xFF414A4D);
        cv_fill(&p->canvas,1,1,width-2,height-2,0xFF1C2327);
        char text[192];int channels=audio_output_channels();
        const char *audio=channels==6?"5.1":channels==2?"Stereo":"Unavailable";
        if(open && !sidebar)compact_menu(p,state,statistics,audio);
        else if(open) {
            cv_fill(&p->canvas,1,1,width-2,102,0xFF202B26);
            cv_fill(&p->canvas,1,1,width-2,3,ui_theme_accent());
            font_draw_fit(&p->canvas,FONT_BOLD,18,24,38,230,ui_theme_accent(),"MoonX360");
            font_draw_fit(&p->canvas,FONT_BOLD,28,24,80,400,0xFFF2F5F3,ui_translate("Quick menu"));
            cv_rrect(&p->canvas,298,19,126,28,4,0xFF121619);
            font_draw_fit(&p->canvas,FONT_REGULAR,15,310,39,102,0xFFC0C9C5,"Back + L3");
            const char *labels[]={"Resume stream","Volume","Mouse mode","Statistics","Audio output","Disconnect stream"};
            unsigned selected=(state>>1)&7,volume=(state>>4)&127;
            for(int i=0;i<6;i++) {
                const char *value=i==2?(input_mouse_active()?"On":"Off"):i==3?(statistics?"On":"Off"):i==4?audio:"";
                if(i==1){snprintf(text,sizeof(text),"%u%%",volume);value=(state&2048)?ui_translate("Muted"):text;}
                else value=ui_translate(value);
                int y=146+i*64;
                menu_row(p,y,ui_translate(labels[i]),value,(unsigned)i==selected,i==5);
                if(i==1) {
                    cv_fill(&p->canvas,30,y+12,384,3,0xFF414A4D);
                    cv_fill(&p->canvas,30,y+12,(int)(384*volume/100),3,(state&2048)?0xFF99A6A0:ui_theme_accent());
                }
            }
            const char *help=selected==1?"Left / Right: volume. A: mute / unmute.":
                selected==2?"A toggles controller mouse mode and resumes streaming.":
                selected==3?"A toggles the streaming statistics overlay.":
                selected==4?"Read only":selected==5?"A disconnects this session and returns to MoonX360.":"Resume stream";
            const char *reason=audio_fallback_reason();
            /* The effective audio reason takes precedence while inspecting
             * Audio output. Other actions keep their own control hints. */
            if(selected==4 && reason)help=reason;
            font_draw_wrapped(&p->canvas,FONT_REGULAR,16,24,528,400,22,0xFFC0C9C5,ui_translate(help));
            cv_fill(&p->canvas,24,584,400,1,0xFF414A4D);
            if(selected!=4) {
                cv_circle(&p->canvas,32,614,9,0xFF5DB82E);
                font_draw_fit(&p->canvas,FONT_BOLD,12,28,619,12,0xFFFFFFFF,"A");
                font_draw_fit(&p->canvas,FONT_REGULAR,15,50,619,136,0xFFC0C9C5,ui_translate("Select"));
            } else font_draw_fit(&p->canvas,FONT_REGULAR,15,24,619,162,0xFF99A6A0,ui_translate("Read only"));
            cv_circle(&p->canvas,216,614,9,0xFFE0453A);
            font_draw_fit(&p->canvas,FONT_BOLD,12,212,619,12,0xFFFFFFFF,"B");
            font_draw_fit(&p->canvas,FONT_REGULAR,15,234,619,190,0xFFC0C9C5,ui_translate("Resume stream"));
        } else {
            cv_fill(&p->canvas,0,0,width,height,0xFF000000);
            if(p->ready && now-p->updated>=1000 && shown>=p->last_shown)
                snprintf(text,sizeof(text),ui_translate("Shown: %.1f FPS"),(shown-p->last_shown)*1000.0/(now-p->updated));
            else snprintf(text,sizeof(text),"%s",ui_translate("Measuring..."));
            font_draw_fit(&p->canvas,FONT_REGULAR,22,20,30,472,0xFFFFFFFF,text);
            snprintf(text,sizeof(text),ui_translate("Decode: %.2f ms"),decode_us/1000.0);
            font_draw_fit(&p->canvas,FONT_REGULAR,20,20,65,472,0xFFFFFFFF,text);
            snprintf(text,sizeof(text),ui_translate("Skipped display: %u | Audio: %s"),dropped,ui_translate(audio));
            font_draw_fit(&p->canvas,FONT_REGULAR,20,20,100,472,0xFFFFFFFF,text);
            gfx_hud_pixels(p->canvas.px,width,height);
            cv_fill(&p->canvas,0,0,3,height,ui_theme_accent());
        }
        if(open){unsigned elapsed=GetTickCount()-raster_started;p->updates++;p->raster_total+=elapsed;if(elapsed>p->raster_max)p->raster_max=elapsed;}
        if(gfx_upload(p->texture,p->canvas.px,width*4,width*4,height)){p->previous=state;p->updated=now;p->ready=true;p->last_shown=shown;}
        else {
            if(swapped)swap_panel(p); /* retain the last complete image on upload failure */
            if(!p->ready){p->failed=true;if(which==0)input_quick_ui_unavailable();}
        }
    }
    if(!p->ready)return;
    d->SetTexture(0,p->texture);
    if(open){if(sidebar)gfx_draw_rect(d,GFX_SHADER_RGB,40,40,488,680);else gfx_draw_rect(d,GFX_SHADER_RGB,384,190,896,510);}
    else gfx_draw_hud_rect(d,16,16,528,144);
    d->SetTexture(0,NULL);p->used=true;
}
void gfx_stream_menu_submitted(DWORD fence)
{for(int i=0;i<2;i++)if(panels[i].used)panels[i].fence=fence;}
