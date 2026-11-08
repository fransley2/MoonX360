#include "xdk.h"
#include "../../src/input.h"
#include "../../src/audio.h"
#include "../../src/video.h"
#include "../../src/quick_menu.h"
#include "../../src/ui/ui.h"
#include "../../src/ui/font.h"
#include "../../src/ui/canvas.h"
#include "../../src/platform.h"
#include <stdio.h>
#include <string.h>

/* A single retained panel replaces both old menu compositions. The second
 * panel is the existing optional statistics HUD. GPU copies obey fences. */
struct stream_panel {
    D3DTexture *texture,*spare;
    canvas_t canvas;
    DWORD fence,spare_fence,updated;
    unsigned previous;
    bool failed,used,ready,was_open,spare_failed,fps_valid;
    unsigned raster_total,raster_max,updates,last_shown;
    unsigned last_decoded;
    float decoded_fps,presented_fps;
};
static stream_panel panels[2];
static const uint32_t C_BG=0xFF12191E,C_PANEL=0xFF1C2327,C_CARD=0xFF1D2934;
static const uint32_t C_ICON=0xFF233038,C_TEXT=0xFFF2F5F3,C_SUB=0xFFA6B7C0;
static const uint32_t C_MUTED=0xFF91A5AD,C_DIV=0xFF36444D,C_DANGER=0xFFEDB0AA;

void gfx_stream_menu_reset(D3DDevice *d)
{
    for(int i=0;i<2;i++){
        stream_panel *p=panels+i;
        if(p->fence)gfx_wait_fence(d,p->fence);
        if(p->spare_fence)gfx_wait_fence(d,p->spare_fence);
        if(p->texture)p->texture->Release();
        if(p->spare)p->spare->Release();
        if(p->canvas.px)cv_free(&p->canvas);
        memset(p,0,sizeof(*p));
    }
}
static void swap_panel(stream_panel *p)
{
    D3DTexture *t=p->texture;p->texture=p->spare;p->spare=t;
    DWORD f=p->fence;p->fence=p->spare_fence;p->spare_fence=f;
}
static void ink(stream_panel *p,int face,float size,int x,int y,int width,uint32_t color,const char *s)
{
    if(s)font_draw_fit(&p->canvas,face,size,x,y,width,color,ui_translate(s));
}
static void icon(stream_panel *p,int kind,int x,int y,uint32_t color)
{
    canvas_t *c=&p->canvas;
    switch(kind){
    case QUICK_RESUME:
        cv_ring(c,x+12,y+12,8,1.8f,color);
        cv_triangle(c,x+10,y+7,x+10,y+17,x+17,y+12,color);break;
    case QUICK_REFRESH:
        cv_ring(c,x+12,y+12,7,1.8f,color);
        cv_fill(c,x+3,y+9,5,7,C_ICON);
        cv_line(c,x+5,y+9,x+9,y+6,1.8f,color);
        cv_triangle(c,x+8,y+3,x+13,y+6,x+8,y+9,color);
        cv_line(c,x+18,y+16,x+14,y+19,1.8f,color);
        cv_triangle(c,x+16,y+16,x+11,y+19,x+16,y+22,color);break;
    case QUICK_VOLUME:
        cv_fill(c,x+3,y+9,5,6,color);
        cv_triangle(c,x+7,y+9,x+13,y+5,x+13,y+19,color);
        cv_line(c,x+16,y+8,x+18,y+12,1.6f,color);
        cv_line(c,x+18,y+12,x+16,y+16,1.6f,color);
        cv_line(c,x+19,y+5,x+22,y+12,1.6f,color);
        cv_line(c,x+22,y+12,x+19,y+19,1.6f,color);break;
    case QUICK_MOUSE:
        cv_line(c,x+5,y+3,x+5,y+19,1.8f,color);
        cv_line(c,x+5,y+3,x+18,y+13,1.8f,color);
        cv_line(c,x+18,y+13,x+11,y+13,1.8f,color);
        cv_line(c,x+11,y+13,x+8,y+20,1.8f,color);break;
    case QUICK_PERFORMANCE:
        cv_fill(c,x+3,y+13,4,8,color);cv_fill(c,x+10,y+8,4,13,color);
        cv_fill(c,x+17,y+4,4,17,color);break;
    case QUICK_SESSION_DETAILS:
        cv_rrect_outline(c,x+3,y+4,18,13,2,1.6f,color);
        cv_line(c,x+12,y+17,x+12,y+21,1.6f,color);
        cv_line(c,x+8,y+21,x+16,y+21,1.6f,color);break;
    case QUICK_DISCONNECT:
        cv_line(c,x+5,y+5,x+5,y+19,1.8f,color);
        cv_line(c,x+5,y+5,x+12,y+5,1.8f,color);
        cv_line(c,x+5,y+19,x+12,y+19,1.8f,color);
        cv_line(c,x+10,y+12,x+20,y+12,1.8f,color);
        cv_line(c,x+17,y+8,x+21,y+12,1.8f,color);
        cv_line(c,x+21,y+12,x+17,y+16,1.8f,color);break;
    case QUICK_CLOSE_APP:
        cv_ring(c,x+12,y+13,8,1.8f,color);
        cv_fill(c,x+9,y+2,6,8,C_ICON);
        cv_line(c,x+12,y+2,x+12,y+12,1.8f,color);break;
    }
}
static void header(stream_panel *p,int w,const char *title)
{
    canvas_t *c=&p->canvas;
    cv_fill(c,0,0,w,55,0xFF202B26);
    cv_rrect_outline(c,18,20,22,15,4,1.5f,cv_mix(C_SUB,ui_theme_accent(),100));
    cv_circle(c,25,27,2,ui_theme_accent());cv_circle(c,34,27,2,ui_theme_accent());
    ink(p,FONT_BOLD,19,48,36,w-195,C_TEXT,"MoonX360");
    ink(p,FONT_REGULAR,12,w-126,35,106,C_SUB,"BACK + L3");
    ink(p,FONT_BOLD,27,20,89,w-40,C_TEXT,title);
}
static void session_card(stream_panel *p,int w,bool compact,const video_stats *vs,
                         const video_quick_info *info)
{
    const int y=101,h=67;
    canvas_t *c=&p->canvas;char actual[32],bitrate[32],summary[104];
    (void)compact;
    if(vs->actual_w>0 && vs->actual_h>0)snprintf(actual,sizeof(actual),"%d x %d",vs->actual_w,vs->actual_h);
    else snprintf(actual,sizeof(actual),"N/A");
    snprintf(bitrate,sizeof(bitrate),"%.1f Mbps",info->bitrate_kbps/1000.0f);
    snprintf(summary,sizeof(summary),"%s  |  H.264  |  %s",actual,bitrate);
    cv_rrect(c,15,y,w-30,h,8,C_CARD);
    ink(p,FONT_REGULAR,12,29,y+21,w-185,C_SUB,"STREAM SESSION");
    bool connected=video_xdk_is_streaming() && vs->decoded>0;
    const char *status=connected?"Connected":"Recovering";
    cv_ring(c,w-117,y+16,4,1.2f,connected?0xFFB4EDC6:C_SUB);
    ink(p,FONT_REGULAR,12,w-106,y+21,85,connected?0xFFB4EDC6:C_SUB,status);
    cv_fill(c,29,y+30,w-58,1,C_DIV);
    ink(p,FONT_BOLD,17,29,y+56,w-58,C_TEXT,summary);
}
static const char *row_labels[QUICK_ROW_COUNT]={
    "Resume Stream","Refresh Picture","Audio Volume","Mouse Mode",
    "Performance Overlay","Session Details","Disconnect Stream",
    "Close Host App & Disconnect"
};
static const char *row_subs[6]={
    "Return to your game","Request a new keyframe",NULL,NULL,NULL,
    "Video, network and diagnostics"
};
static const char *refresh_text(int state)
{
    switch(state){
    case VIDEO_REFRESH_REQUESTED:return "Refresh requested";
    case VIDEO_REFRESH_WAITING:return "Waiting for keyframe";
    case VIDEO_REFRESH_DECODED:return "Keyframe decoded";
    case VIDEO_REFRESH_UNAVAILABLE:return "Refresh unavailable";
    default:return row_subs[1];
    }
}
static void action_row(stream_panel *p,int w,int top,int height,int row,bool selected,
                       unsigned state,const char *audio,int refresh)
{
    canvas_t *c=&p->canvas;uint32_t accent=ui_theme_accent();
    bool default_green=accent==0xFF72C840;
    uint32_t focus=default_green?0xFF244D3A:cv_mix(C_PANEL,accent,57);
    uint32_t focus_border=default_green?0xFF7FD6A2:cv_mix(accent,C_TEXT,80);
    if(selected){
        cv_rrect(c,16,top,w-32,height-3,8,focus);
        cv_rrect_outline(c,16,top,w-32,height-3,8,1,focus_border);
    }
    int icon_y=top+(height-32)/2;
    cv_rrect(c,27,icon_y,32,32,7,
             selected?(default_green?0xFF38674F:cv_mix(C_ICON,accent,70)):C_ICON);
    icon(p,row,31,icon_y+4,row==QUICK_CLOSE_APP?C_DANGER:selected?C_TEXT:C_SUB);
    if(row==QUICK_VOLUME && (state&QUICK_MUTED))
        cv_line(c,34,icon_y+27,54,icon_y+5,2,C_DANGER);
    const char *sub=NULL;char value[64];
    if(row==QUICK_REFRESH && refresh!=VIDEO_REFRESH_IDLE)sub=refresh_text(refresh);
    if(row==QUICK_VOLUME){
        snprintf(value,sizeof(value),"%u%%  |  %s",(state>>QUICK_VOLUME_SHIFT)&QUICK_VOLUME_MASK,audio);
        sub=(state&QUICK_MUTED)?ui_translate("Muted"):value;
    }
    if(row==QUICK_MOUSE)sub=input_mouse_active()?"On":"Off";
    if(row==QUICK_PERFORMANCE)sub=(state&QUICK_STATS)?"On":"Off";
    int label_y=top+(height+15)/2;
    ink(p,FONT_BOLD,row==QUICK_CLOSE_APP?16:17,72,label_y,w-103,
        row==QUICK_CLOSE_APP?C_DANGER:C_TEXT,row_labels[row]);
    if(sub)
        ink(p,FONT_REGULAR,12,w-135,label_y,112,selected?C_TEXT:C_SUB,sub);
    if(row==QUICK_SESSION_DETAILS){
        cv_line(c,w-40,top+height/2-5,w-35,top+height/2,1.5f,C_SUB);
        cv_line(c,w-35,top+height/2,w-40,top+height/2+5,1.5f,C_SUB);
    }
}
static void main_page(stream_panel *p,int w,int h,unsigned state,const video_stats *vs,
                      const video_quick_info *info,int refresh)
{
    bool compact=w>500;unsigned selected=(state>>QUICK_ROW_SHIFT)&QUICK_ROW_MASK;
    int channels=audio_output_channels();
    const char *audio=channels==6?"5.1":channels==2?"Stereo":"N/A";
    header(p,w,"Quick Menu");
    session_card(p,w,compact,vs,info);
    int quick_y=183;
    ink(p,FONT_REGULAR,12,20,quick_y,w-40,C_MUTED,"QUICK ACTIONS");
    int row_h=compact?42:45;
    int top=compact?193:199;
    for(int row=0;row<6;row++)
        action_row(p,w,top+row*row_h,row_h,row,selected==(unsigned)row,
                   state,audio,refresh);
    int control=compact?455:479;
    cv_fill(&p->canvas,20,control,w-40,1,C_DIV);
    ink(p,FONT_REGULAR,12,20,control+17,w-40,C_MUTED,"SESSION CONTROL");
    int control_top=compact?482:506,control_h=compact?38:42;
    action_row(p,w,control_top,control_h,QUICK_DISCONNECT,selected==QUICK_DISCONNECT,state,audio,refresh);
    action_row(p,w,control_top+control_h,control_h,QUICK_CLOSE_APP,selected==QUICK_CLOSE_APP,state,audio,refresh);
    cv_fill(&p->canvas,20,h-38,w-40,1,C_DIV);
    ink(p,FONT_REGULAR,12,20,h-14,w/4,C_SUB,"A Select");
    ink(p,FONT_REGULAR,12,w/4+12,h-14,w/4,C_SUB,"B Resume");
    ink(p,FONT_REGULAR,12,w/2+25,h-14,w/2-45,C_SUB,
        selected==QUICK_VOLUME?"Left/Right Volume":"D-Pad Navigate");
}
static void detail_row(stream_panel *p,int w,int y,const char *label,const char *value)
{
    ink(p,FONT_REGULAR,15,30,y,w/2-45,C_SUB,label);
    ink(p,FONT_BOLD,16,w/2+5,y,w/2-35,C_TEXT,value);
    cv_fill(&p->canvas,28,y+9,w-56,1,C_DIV);
}
static void details_page(stream_panel *p,int w,int h,unsigned state,const video_stats *vs,
                         const video_quick_info *info,const video_refresh_snapshot *rs)
{
    int section=(state>>QUICK_DETAILS_SHIFT)&QUICK_DETAILS_MASK;
    const char *sections[]={"VIDEO","NETWORK","RECOVERY","AUDIO"};
    char values[16][80];const char *names[16];int n=0;
    header(p,w,"Session Details");
    cv_rrect(&p->canvas,15,105,w-30,h-160,12,C_CARD);
    for(int i=0;i<4;i++){
        int x=27+i*(w-54)/4;
        if(i==section)cv_fill(&p->canvas,x,136,(w-54)/4-4,2,ui_theme_accent());
        ink(p,FONT_REGULAR,12,x,129,(w-54)/4-5,i==section?C_TEXT:C_MUTED,sections[i]);
    }
    if(section==0){
        names[n]="Actual stream";if(vs->actual_w)snprintf(values[n],80,"%d x %d",vs->actual_w,vs->actual_h);else snprintf(values[n],80,"N/A");n++;
        names[n]="Requested stream";snprintf(values[n++],80,"%d x %d",info->requested_w,info->requested_h);
        names[n]="Xbox display";if(vs->display_w)snprintf(values[n],80,"%d x %d",vs->display_w,vs->display_h);else snprintf(values[n],80,"N/A");n++;
        int bw=0,bh=0;gfx_backbuffer_size(&bw,&bh);
        names[n]="App render target";snprintf(values[n++],80,"%d x %d",bw,bh);
        names[n]="Requested FPS";snprintf(values[n++],80,"%d",info->requested_fps);
        names[n]="Decoded FPS";if(p->fps_valid)snprintf(values[n],80,"%.1f",p->decoded_fps);else snprintf(values[n],80,"N/A");n++;
        names[n]="Presented FPS (CPU)";if(p->fps_valid)snprintf(values[n],80,"%.1f",p->presented_fps);else snprintf(values[n],80,"N/A");n++;
        names[n]="Requested bitrate";snprintf(values[n++],80,"%.1f Mbps",info->bitrate_kbps/1000.0f);
        names[n]="Codec";snprintf(values[n++],80,"H.264");
        names[n]="Decoder workers";snprintf(values[n++],80,"%d",info->decode_workers);
        names[n]="H.264 slices";snprintf(values[n++],80,"%d",info->slices);
        names[n]="Pipeline";snprintf(values[n++],80,"%s",info->pipeline[0]?info->pipeline:"N/A");
        names[n]="Fast Decode";snprintf(values[n++],80,"%s",fast_decode_name(info->fast_decode));
        names[n]="Image mode";snprintf(values[n++],80,"%s",info->shader_mode==1?"Fast":info->shader_mode==2?"Quality":"N/A");
    } else if(section==1){
        names[n]="Host";snprintf(values[n++],80,"%s",info->host_name[0]?info->host_name:"N/A");
        names[n]="RTT";if(info->network_valid)snprintf(values[n],80,"%u ms",info->rtt_ms);else snprintf(values[n],80,"N/A");n++;
        names[n]="Network variance";if(info->network_valid)snprintf(values[n],80,"%u ms",info->variance_ms);else snprintf(values[n],80,"N/A");n++;
        names[n]="Lost frames";snprintf(values[n++],80,"N/A");
        names[n]="FEC recovered";snprintf(values[n++],80,"N/A");
        names[n]="FEC failures";snprintf(values[n++],80,"N/A");
        names[n]="Video queue age";if(info->queue_age_ms)snprintf(values[n],80,"%u ms",info->queue_age_ms);else snprintf(values[n],80,"N/A");n++;
    } else if(section==2){
        names[n]="Decoder errors";snprintf(values[n++],80,"%u",vs->decode_errors);
        names[n]="Dropped frames";snprintf(values[n++],80,"%u",vs->dropped);
        names[n]="IDR requests";snprintf(values[n++],80,"%u",rs->requests);
        names[n]="Decoded IDRs";snprintf(values[n++],80,"%u",rs->decoded_idrs);
        names[n]="Refresh state";snprintf(values[n++],80,"%s",refresh_text(rs->state));
        names[n]="Last decoded IDR";if(rs->last_idr_ms)snprintf(values[n],80,"%u s ago",(GetTickCount()-rs->last_idr_ms)/1000);else snprintf(values[n],80,"N/A");n++;
    } else {
        int channels=audio_output_channels();
        names[n]="Output";snprintf(values[n++],80,"%s",channels==6?"5.1":channels==2?"Stereo":"N/A");
        names[n]="Volume";snprintf(values[n++],80,"%u%%",(state>>QUICK_VOLUME_SHIFT)&QUICK_VOLUME_MASK);
        names[n]="Mute";snprintf(values[n++],80,"%s",(state&QUICK_MUTED)?"Muted":"Unmuted");
    }
    int step=(h-205)/(n>9?n:9);
    for(int i=0;i<n;i++)detail_row(p,w,171+i*step,names[i],values[i]);
    cv_fill(&p->canvas,20,h-38,w-40,1,C_DIV);
    ink(p,FONT_REGULAR,12,20,h-14,w-40,C_SUB,"D-Pad Sections     B Back");
}
static void confirm_page(stream_panel *p,int w,int h)
{
    header(p,w,"Close Host App?");
    cv_rrect(&p->canvas,16,110,w-32,190,12,C_CARD);
    cv_rrect(&p->canvas,32,133,40,40,8,C_ICON);
    icon(p,QUICK_CLOSE_APP,40,141,C_DANGER);
    ink(p,FONT_BOLD,20,86,160,w-110,C_DANGER,"Close Host App & Disconnect");
    ink(p,FONT_REGULAR,16,32,211,w-64,C_SUB,
        "Close the running app on your PC and return to MoonX360.");
    ink(p,FONT_REGULAR,14,32,254,w-64,C_SUB,
        "The host computer stays on.");
    cv_fill(&p->canvas,20,h-38,w-40,1,C_DIV);
    ink(p,FONT_REGULAR,12,20,h-14,w-40,C_SUB,"A Confirm     B Cancel");
}
void gfx_stream_menu_draw(D3DDevice *d,unsigned decoded,unsigned shown,unsigned dropped,unsigned decode_us,bool detailed)
{
    unsigned state=input_quick_snapshot();
    bool open=(state&QUICK_OPEN)!=0,statistics=(state&QUICK_STATS)!=0;
    if(!open && panels[0].was_open){
        char report[220];stream_panel *menu=&panels[0];
        snprintf(report,sizeof(report),
            "[UI] quick panel style=%d updates=%u raster_total_ms=%u raster_peak_ms=%u texture_bytes=%u allocations=%u\n",
            ui_stream_menu_style(),menu->updates,menu->raster_total,menu->raster_max,
            (unsigned)(menu->canvas.w*menu->canvas.h*4*(menu->spare?2:1)),
            menu->spare?2u:1u);
        plat_log(report);menu->was_open=false;
    }
    for(int i=0;i<2;i++)panels[i].used=false;
    if(gfx_system_ui_active() || (!open && (!statistics || detailed)))return;
    bool sidebar=open && ui_stream_menu_style()==1;
    int which=open?0:1,width=open?(sidebar?448:640):512;
    int height=open?(sidebar?640:600):128;
    stream_panel *p=panels+which;
    if(open && !p->was_open){p->was_open=true;p->updates=p->raster_total=p->raster_max=0;p->previous=~state;p->updated=0;p->fps_valid=false;}
    if(p->failed)return;
    if(!p->texture){
        if(!cv_init(&p->canvas,width,height)){p->failed=true;if(which==0)input_quick_ui_unavailable();return;}
        if(FAILED(d->CreateTexture(width,height,1,0,D3DFMT_LIN_A8R8G8B8,0,&p->texture,NULL))){
            cv_free(&p->canvas);p->failed=true;if(which==0)input_quick_ui_unavailable();return;
        }
        p->previous=~state;
    }
    DWORD now=GetTickCount();
    bool update=state!=p->previous || now-p->updated>=1000,swapped=false;
    if(update && gfx_fence_pending(p->fence)){
        if(!p->spare && !p->spare_failed)
            if(FAILED(d->CreateTexture(width,height,1,0,D3DFMT_LIN_A8R8G8B8,0,&p->spare,NULL)))p->spare_failed=true;
        if(p->spare && !gfx_fence_pending(p->spare_fence)){swap_panel(p);swapped=true;}
        else update=false;
    }
    if(update){
        DWORD started=GetTickCount();char text[192];
        if(open){
            video_stats vs;video_quick_info info;video_refresh_snapshot rs;
            video_get_stats(&vs);video_get_quick_info(&info);video_get_refresh_snapshot(&rs);
            if(p->updated && now>p->updated && now-p->updated>=500){
                p->decoded_fps=(decoded-p->last_decoded)*1000.0f/(now-p->updated);
                p->presented_fps=(shown-p->last_shown)*1000.0f/(now-p->updated);
                p->fps_valid=true;
            }
            cv_fill(&p->canvas,0,0,width,height,C_BG);
            int page=(state>>QUICK_PAGE_SHIFT)&QUICK_PAGE_MASK;
            if(page==QUICK_DETAILS)details_page(p,width,height,state,&vs,&info,&rs);
            else if(page==QUICK_CONFIRM)confirm_page(p,width,height);
            else main_page(p,width,height,state,&vs,&info,rs.state);
        } else {
            cv_fill(&p->canvas,0,0,width,height,0xFF000000);
            if(p->ready && now-p->updated>=1000 && shown>=p->last_shown)
                snprintf(text,sizeof(text),ui_translate("Shown: %.1f FPS"),(shown-p->last_shown)*1000.0/(now-p->updated));
            else snprintf(text,sizeof(text),"%s",ui_translate("Measuring..."));
            ink(p,FONT_REGULAR,22,20,30,472,C_TEXT,text);
            snprintf(text,sizeof(text),ui_translate("Decode: %.2f ms"),decode_us/1000.0);
            ink(p,FONT_REGULAR,20,20,65,472,C_TEXT,text);
            int channels=audio_output_channels();
            snprintf(text,sizeof(text),ui_translate("Skipped display: %u | Audio: %s"),dropped,
                channels==6?"5.1":channels==2?"Stereo":"N/A");
            ink(p,FONT_REGULAR,20,20,100,472,C_TEXT,text);
            gfx_hud_pixels(p->canvas.px,width,height);
            cv_fill(&p->canvas,0,0,3,height,ui_theme_accent());
        }
        if(open){unsigned ms=GetTickCount()-started;p->updates++;p->raster_total+=ms;if(ms>p->raster_max)p->raster_max=ms;}
        if(gfx_upload(p->texture,p->canvas.px,width*4,width*4,height)){
            p->previous=state;p->updated=now;p->ready=true;p->last_shown=shown;p->last_decoded=decoded;
        } else {
            if(swapped)swap_panel(p);
            if(!p->ready){p->failed=true;if(which==0)input_quick_ui_unavailable();}
        }
    }
    if(!p->ready)return;
    d->SetTexture(0,p->texture);
    if(open){
        float x=sidebar?38.0f:(gfx_layout_width()-width)*0.5f;
        float y=sidebar?40.0f:60.0f;
        gfx_draw_layout_rect(d,GFX_SHADER_RGB,x,y,x+width,y+height);
    } else gfx_draw_hud_rect(d,16,16,528,144);
    d->SetTexture(0,NULL);p->used=true;
}
void gfx_stream_menu_submitted(DWORD fence)
{
    for(int i=0;i<2;i++)if(panels[i].used)panels[i].fence=fence;
}
