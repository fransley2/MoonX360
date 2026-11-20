#include "xdk.h"
#include "../../src/input.h"
#include "../../src/desktop_keyboard_layout.h"
#include "../../src/audio.h"
#include "../../src/video.h"
#include "../../src/quick_menu.h"
#include "../../src/ui/ui.h"
#include "../../src/ui/draw.h"
#include "../../src/ui/smart_keyboard_remote.h"
#include "../../src/ui/font.h"
#include "../../src/ui/canvas.h"
#include "../../src/platform.h"
#include "../../src/performance.h"
#include <stdio.h>
#include <string.h>

/* Retained overlay panels: [0]=Quick Menu, [2]=Desktop Toolbar, [3]=Smart Keyboard.
 * All linear textures use 64-texel (256-byte) aligned widths for Xenos safety. */
struct stream_panel {
    D3DTexture *texture,*spare;
    canvas_t canvas;
    DWORD fence,spare_fence,updated,opened_at;
    unsigned previous;
    bool failed,used,ready,was_open,spare_failed;
    unsigned raster_total,raster_max,updates;
};
static stream_panel panels[4];
static unsigned quick_drawn_state,quick_reported_state;
static bool quick_drawn_valid;
static const uint32_t C_BG=0xFF101519,C_PANEL=0xFF182026,C_CARD=0xFF1A232A;
static const uint32_t C_TEXT=0xFFF4F7F5,C_BODY=0xFFD4DDE2,C_SUB=0xFFAAB8C0;
static const uint32_t C_MUTED=0xFF8899A2,C_DIV=0xFF2E3A42,C_DANGER=0xFFEDB0AA,C_DANGER_FILL=0xFF3A1F22;

static void release_panel(D3DDevice *d,stream_panel *p)
{
    if(p->fence)gfx_wait_fence(d,p->fence);
    if(p->spare_fence)gfx_wait_fence(d,p->spare_fence);
    if(p->texture)p->texture->Release();
    if(p->spare)p->spare->Release();
    if(p->canvas.px)cv_free(&p->canvas);
    memset(p,0,sizeof(*p));
}
void gfx_stream_menu_reset(D3DDevice *d)
{
    quick_drawn_valid=false;quick_drawn_state=quick_reported_state=0;
    for(int i=0;i<4;i++)release_panel(d,panels+i);
}
static void swap_panel(stream_panel *p)
{
    D3DTexture *t=p->texture;p->texture=p->spare;p->spare=t;
    DWORD f=p->fence;p->fence=p->spare_fence;p->spare_fence=f;
}
static bool ensure_panel(D3DDevice *d,stream_panel *p,int w,int h,unsigned invalidate,
                         bool is_quick,bool alloc_spare)
{
    if(p->failed)return false;
    if(p->texture && (p->canvas.w!=w || p->canvas.h!=h)) {
        release_panel(d,p);
        p->previous=invalidate;
    }
    if(!p->texture){
        if(!cv_init(&p->canvas,w,h) ||
           FAILED(d->CreateTexture(w,h,1,0,D3DFMT_LIN_A8R8G8B8,0,&p->texture,NULL))){
            if(p->canvas.px)cv_free(&p->canvas);
            p->texture=NULL;p->failed=true;
            if(is_quick)input_quick_ui_unavailable();
            else input_desktop_ui_unavailable();
            return false;
        }
        if(alloc_spare && FAILED(d->CreateTexture(w,h,1,0,D3DFMT_LIN_A8R8G8B8,0,&p->spare,NULL)))
            p->spare_failed=true;
        p->previous=invalidate;
    }
    return true;
}
static void ink(stream_panel *p,int face,float size,int x,int y,int width,uint32_t color,const char *s)
{
    if(s)font_draw_fit(&p->canvas,face,size,x,y,width,color,ui_translate(s));
}
static void panel_header(stream_panel *p,int w,const char *title,const char *hint)
{
    canvas_t *c=&p->canvas;
    int hint_w=0,hint_draw_w=0;
    if(hint && hint[0]){
        const char *ht=ui_translate(hint);
        hint_w=font_text_width(FONT_REGULAR,12,ht);
        hint_draw_w=hint_w<16?64:hint_w;
        if(hint_draw_w>w/2-20)hint_draw_w=w/2-20;
        int hx=w-20-hint_draw_w;
        font_draw_fit(c,FONT_REGULAR,12,hx,31,hint_draw_w,C_MUTED,ht);
    }
    ink(p,FONT_BOLD,19,22,32,w-(hint_draw_w?hint_draw_w+52:44),C_TEXT,title);
    cv_fill(c,18,48,w-36,1,C_DIV);
}
static const char *row_labels[QUICK_ROW_COUNT]={
    "Resume","Audio","Keyboard","Desktop Mode",
    "Performance Overlay","Refresh Picture","Disconnect","Close Host App"
};
static const char *refresh_text(int state)
{
    switch(state){
    case VIDEO_REFRESH_REQUESTED:return "Requested";
    case VIDEO_REFRESH_WAITING:return "Waiting";
    case VIDEO_REFRESH_DECODED:return "Updated";
    case VIDEO_REFRESH_UNAVAILABLE:return "Unavailable";
    default:return NULL;
    }
}
static void draw_chevrons(canvas_t *c,int x_left,int x_right,int cy,uint32_t col)
{
    cv_line(c,(float)x_left,(float)cy,(float)(x_left+4),(float)(cy-4),1.4f,col);
    cv_line(c,(float)x_left,(float)cy,(float)(x_left+4),(float)(cy+4),1.4f,col);
    cv_line(c,(float)(x_right-4),(float)(cy-4),(float)x_right,(float)cy,1.4f,col);
    cv_line(c,(float)x_right,(float)cy,(float)(x_right-4),(float)(cy+4),1.4f,col);
}
static void action_row(stream_panel *p,int w,int top,int height,int row,bool selected,
                       unsigned state,const char *audio,int refresh)
{
    canvas_t *c=&p->canvas;uint32_t accent=ui_theme_accent();
    bool danger=(row==QUICK_CLOSE_APP);
    bool default_green=accent==0xFF72C840;
    uint32_t focus=danger?C_DANGER_FILL:default_green?0xFF223D30:cv_mix(C_PANEL,accent,52);
    uint32_t bar_col=danger?C_DANGER:accent;
    if(selected){
        cv_rrect(c,14,(float)(top+2),(float)(w-28),(float)(height-4),6,focus);
        cv_fill(c,14,top+7,3,height-14,bar_col);
    }
    const char *raw_val=NULL;char value[48];
    uint32_t val_col=selected?C_TEXT:C_SUB;
    if(row==QUICK_VOLUME){
        if(state&QUICK_MUTED){raw_val="Muted";val_col=C_DANGER;}
        else{
            unsigned vol=(state>>QUICK_VOLUME_SHIFT)&QUICK_VOLUME_MASK;
            if(audio && audio[0]=='5')snprintf(value,sizeof(value),"%u%% 5.1",vol);
            else snprintf(value,sizeof(value),"%u%%",vol);
            raw_val=value;
            if(selected)val_col=accent;
        }
    } else if(row==QUICK_DESKTOP){
        bool on=input_mouse_active();
        raw_val=on?"On":"Off";
        val_col=on?accent:C_MUTED;
    } else if(row==QUICK_PERFORMANCE){
        int om=quick_overlay_mode(state)&3;
        if(om==0){raw_val="Off";val_col=C_MUTED;}
        else if(om==1){raw_val="Simple";val_col=accent;}
        else if(om==2){raw_val="Detailed";val_col=accent;}
        else{raw_val=quick_dev_page(state)?"Dev P2":"Dev P1";val_col=accent;}
    } else if(row==QUICK_REFRESH){
        raw_val=refresh_text(refresh);
        if(refresh==VIDEO_REFRESH_UNAVAILABLE)val_col=C_DANGER;
        else if(refresh==VIDEO_REFRESH_DECODED)val_col=accent;
    }
    int label_y=top+height/2+6;
    int val_reserve=0;
    if(raw_val){
        const char *vtext=ui_translate(raw_val);
        int vface=selected?FONT_BOLD:FONT_REGULAR;
        int vw=font_text_width(vface,13,vtext);
        if(vw<12)vw=44;
        if(vw>w/2-30)vw=w/2-30;
        bool chev=selected && (row==QUICK_VOLUME || row==QUICK_PERFORMANCE);
        int right_margin=chev?38:26;
        int vx=w-right_margin-vw;
        if(chev)draw_chevrons(c,vx-12,w-24,top+height/2,accent);
        font_draw_fit(c,vface,13,vx,label_y,vw+2,val_col,vtext);
        val_reserve=(w-vx)+18;
    }
    uint32_t lbl_col=danger?C_DANGER:selected?C_TEXT:C_BODY;
    int lbl_max=w-52-val_reserve;
    if(lbl_max<80)lbl_max=80;
    ink(p,selected?FONT_BOLD:FONT_REGULAR,16,26,label_y,lbl_max,lbl_col,row_labels[row]);
}
static void main_page(stream_panel *p,int w,int h,unsigned state,int refresh)
{
    bool compact=w>400;unsigned selected=(state>>QUICK_ROW_SHIFT)&QUICK_ROW_MASK;
    int channels=audio_output_channels();
    const char *audio=channels==6?"5.1":"";
    panel_header(p,w,"Quick Menu","BACK + L3");
    int row_h=compact?40:44;
    int top=56;
    for(int row=0;row<QUICK_DISCONNECT;row++)
        action_row(p,w,top+row*row_h,row_h,row,selected==(unsigned)row,state,audio,refresh);
    int div_y=top+QUICK_DISCONNECT*row_h+6;
    cv_fill(&p->canvas,18,div_y,w-36,1,C_DIV);
    int exit_top=div_y+8;
    action_row(p,w,exit_top,row_h,QUICK_DISCONNECT,selected==QUICK_DISCONNECT,state,audio,refresh);
    action_row(p,w,exit_top+row_h,row_h,QUICK_CLOSE_APP,selected==QUICK_CLOSE_APP,state,audio,refresh);
    cv_fill(&p->canvas,18,h-40,w-36,1,C_DIV);
    const char *footer="A Select    Y Keyboard    B Close";
    if(selected==QUICK_VOLUME)footer="A Mute    Left/Right Volume    B Close";
    else if(selected==QUICK_DESKTOP)footer="A Toggle    X Actions    B Close";
    else if(selected==QUICK_PERFORMANCE){
        footer=(quick_overlay_mode(state)&3)==3
            ?"Left/Right Mode    X Page    B Close"
            :"A / Left/Right Mode    B Close";
    }
    ink(p,FONT_REGULAR,12,22,h-15,w-44,C_SUB,footer);
}
static void confirm_page(stream_panel *p,int w,int h)
{
    canvas_t *c=&p->canvas;
    panel_header(p,w,"Close Host App?","B Back");
    cv_rrect(c,16,66,(float)(w-32),178,8,C_CARD);
    cv_fill(c,16,74,3,162,C_DANGER);
    ink(p,FONT_BOLD,17,32,102,w-64,C_DANGER,"Close Host App & Disconnect");
    font_draw_wrapped(c,FONT_REGULAR,15,32,140,w-64,22,C_BODY,
        ui_translate("Close the running app on your PC and return to MoonX360."));
    ink(p,FONT_REGULAR,13,32,216,w-64,C_SUB,"The host computer stays on.");
    cv_fill(c,18,h-40,w-36,1,C_DIV);
    ink(p,FONT_REGULAR,12,22,h-15,w-44,C_SUB,"A Confirm    B Cancel");
}
static void desktop_page(stream_panel *p,int w,int h,unsigned state)
{
    static const char *labels[]={
        "Desktop Mode On / Off","Desktop actions","Open Keyboard",
        "Windows keyboard","Notification area","Switch window","Windows Explorer"
    };
    canvas_t *c=&p->canvas;
    uint32_t accent=ui_theme_accent();
    uint32_t focus=(accent==0xFF72C840)?0xFF223D30:cv_mix(C_PANEL,accent,52);
    panel_header(p,w,"Desktop Actions","B Back");
    unsigned selected=(state>>QUICK_ROW_SHIFT)&QUICK_ROW_MASK;
    int row_h=(w>400)?44:46,top=56;
    for(int row=0;row<7;row++){
        int y=top+row*row_h;
        bool on=selected==(unsigned)row;
        if(on){
            cv_rrect(c,14,(float)(y+2),(float)(w-28),(float)(row_h-4),6,focus);
            cv_fill(c,14,y+7,3,row_h-14,accent);
        }
        ink(p,on?FONT_BOLD:FONT_REGULAR,16,26,y+row_h/2+6,w-52,on?C_TEXT:C_BODY,labels[row]);
    }
    cv_fill(c,18,h-40,w-36,1,C_DIV);
    ink(p,FONT_REGULAR,12,22,h-15,w-44,C_SUB,"A Select    B Back");
}
static bool smart_kb_overlay_draw(D3DDevice *d)
{
    struct sk_remote_session rs;unsigned rev=0;
    if(!input_smart_keyboard_snapshot(&rs,&rev))return false;
    stream_panel *p=panels+3;
    bool full_pc=rs.sk.full_pc;
    bool full=rs.layout==SK_LAYOUT_FULL;
    bool crt=gfx_layout_width()<=960;
    const int w=full_pc?(crt?896:768):(crt?768:704);
    const int h=full_pc?(full?416:368):(full?352:312);
    if(!ensure_panel(d,p,w,h,~rev,false,true))return true;
    bool swapped=false,update=rev!=p->previous;
    if(update && gfx_fence_pending(p->fence)){
        if(p->spare && !gfx_fence_pending(p->spare_fence)){swap_panel(p);swapped=true;}
        else update=false;
    }
    if(update){
        canvas_t *c=&p->canvas;uint32_t accent=ui_theme_accent();
        cv_fill(c,0,0,w,h,C_BG);
        cv_fill(c,12,0,w-24,3,accent);
        static const char *lang_tags[]={"EN","PT-BR","ES"};
        const char *page_tag=rs.sk.dual_stick?"Dual-Stick"
                            :rs.sk.page==SK_PAGE_SYMBOLS?"123 / #"
                            :rs.sk.page==SK_PAGE_ACCENTS?"Accents"
                            :rs.sk.shift?"SHIFT":"QWERTY";
        const char *grid_tag=full_pc?"Full PC":"Compact 3x10";
        char hdr[128];
        snprintf(hdr,sizeof(hdr),"%s  |  %s  |  %s  |  %s",
                 rs.send_mode==SK_SEND_DIRECT?"Direct":"Compose",
                 grid_tag,page_tag,lang_tags[rs.sk.lang%SK_LANG_COUNT]);
        ink(p,FONT_BOLD,16,16,23,210,C_TEXT,"Virtual Keyboard");
        int hw=font_text_width(FONT_REGULAR,13,hdr);
        int max_hw=w-246;
        int draw_hw=hw<16?64:(hw<max_hw?hw:max_hw);
        ink(p,FONT_REGULAR,13,w-16-draw_hw,22,draw_hw,accent,hdr);
        cv_rrect(c,14,30,(float)(w-28),32,5,0xFF0B0F12);
        cv_rrect_outline(c,14,30,(float)(w-28),32,5,1,rs.send_failed?C_DANGER:C_DIV);
        if(rs.send_mode==SK_SEND_DIRECT && !rs.sk.text[0]) {
            ink(p,FONT_REGULAR,14,24,51,w-48,C_MUTED,"Direct mode: keys and shortcuts sent live to Windows");
        } else {
            int tlen=(int)strlen(rs.sk.text);
            int clen=rs.sk.cursor<=tlen?rs.sk.cursor:tlen;
            char prefix[SK_TEXT_MAX];memcpy(prefix,rs.sk.text,(size_t)clen);prefix[clen]='\0';
            int cur_px=font_text_width(FONT_BOLD,17,prefix);
            int scroll_px=cur_px>w-72?cur_px-(w-72):0;
            cv_clip(c,20,32,w-40,28);
            font_draw(c,FONT_BOLD,17,24-scroll_px,52,C_TEXT,rs.sk.text);
            cv_fill(c,24+cur_px-scroll_px,36,2,20,accent);
            cv_unclip(c);
        }
        int sy=68,sh=28,slot_w=(w-40)/3;
        for(int i=0;i<SK_MAX_SUGGESTIONS;i++){
            int sx=14+i*(slot_w+6);
            bool has=i<rs.sk.suggestions.count;
            bool on=has && !rs.toolbar_focused && rs.sk.zone==SK_ZONE_SUGGESTIONS && rs.sk.sug_index==i;
            cv_rrect(c,(float)sx,(float)sy,(float)slot_w,(float)sh,4,on?0xFF233829:C_CARD);
            cv_rrect_outline(c,(float)sx,(float)sy,(float)slot_w,(float)sh,4,on?2.0f:1.0f,on?accent:C_DIV);
            if(has)ink(p,on?FONT_BOLD:FONT_REGULAR,14,sx+10,sy+19,slot_w-20,on?C_TEXT:C_SUB,rs.sk.suggestions.items[i]);
            else if(i==1 && rs.send_mode==SK_SEND_DIRECT)
                ink(p,FONT_REGULAR,12,sx+8,sy+19,slot_w-16,C_MUTED,"Back -> Compose");
        }
        int gy=102,grid_bottom=gy;
        if(full_pc){
            int gap=4,kh=44;
            float inner_w=(float)(w-28);
            for(int r=0;r<SK_FULL_ROWS;r++){
                int cols=sk_row_cols(rs.sk.page,r);
                float avail=inner_w-(float)(gap*(cols-1));
                float fx=14.0f;
                int ky=gy+r*(kh+gap);
                for(int col=0;col<cols;col++){
                    const struct sk_key_def *k=sk_get_key(rs.sk.page,r,col);
                    float fw=avail*(k->weight/15.0f);
                    int kx=(int)fx,kw=(int)(fw+0.5f);
                    bool lon=!rs.toolbar_focused && rs.sk.zone==SK_ZONE_KEYS && rs.sk.row==r && rs.sk.col==col;
                    bool ron=rs.sk.dual_stick && rs.sk.right_row==r && rs.sk.right_col==col;
                    bool special=k->kind!=SK_KEY_CHAR && k->kind!=SK_KEY_SPACE;
                    bool active_mod=(k->kind==SK_KEY_SHIFT && rs.sk.shift==SK_SHIFT_ONCE) ||
                                    (k->kind==SK_KEY_CAPS && rs.sk.shift==SK_SHIFT_CAPS) ||
                                    (k->kind==SK_KEY_CTRL && rs.sk.ctrl_on) ||
                                    (k->kind==SK_KEY_ALT && rs.sk.alt_on) ||
                                    (k->kind==SK_KEY_WIN && rs.sk.win_on) ||
                                    (k->kind==SK_KEY_ALTGR && rs.sk.page==SK_PAGE_ACCENTS);
                    uint32_t bg=lon?0xFF233829:ron?0xFF1E3348:active_mod?0xFF233829:special?0xFF253039:C_CARD;
                    uint32_t bd=lon?accent:ron?0xFF3678C0:active_mod?accent:C_DIV;
                    cv_rrect(c,(float)kx,(float)ky,(float)kw,(float)kh,4,bg);
                    cv_rrect_outline(c,(float)kx,(float)ky,(float)kw,(float)kh,4,(lon||ron)?2.0f:1.0f,bd);
                    if(k->sub && rs.sk.shift==SK_SHIFT_OFF)
                        font_draw(c,FONT_REGULAR,10.0f,kx+5,ky+13,C_MUTED,k->sub);
                    const char *lbl=(rs.sk.shift!=SK_SHIFT_OFF)?k->hi:k->lo;
                    if(k->kind==SK_KEY_SPACE)lbl="";
                    float fsize=special?12.0f:16.0f;
                    int tw=font_text_width(FONT_BOLD,fsize,lbl);
                    font_draw(c,FONT_BOLD,fsize,kx+(kw-tw)/2,ky+28,(lon||ron||active_mod)?C_TEXT:special?C_SUB:C_TEXT,lbl);
                    fx+=fw+(float)gap;
                }
            }
            grid_bottom=gy+SK_FULL_ROWS*(kh+gap);
        } else {
            int gap=6,kh=52,cols=SK_COMPACT_COLS,rows=SK_COMPACT_ROWS;
            float inner_w=(float)(w-28);
            float kw=(inner_w-(float)(gap*(cols-1)))/(float)cols;
            for(int r=0;r<rows;r++){
                int ky=gy+r*(kh+gap);
                for(int col=0;col<cols;col++){
                    int kx=(int)(14.0f+(float)col*(kw+(float)gap)),cell_w=(int)(kw+0.5f);
                    bool lon=!rs.toolbar_focused && rs.sk.zone==SK_ZONE_KEYS && rs.sk.row==r && rs.sk.col==col;
                    bool ron=rs.sk.dual_stick && rs.sk.right_row==r && rs.sk.right_col==col;
                    const char *lbl=sk_compact_key_label(rs.sk.page,rs.sk.shift,r,col);
                    uint32_t bg=lon?0xFF233829:ron?0xFF1E3348:C_CARD;
                    uint32_t bd=lon?accent:ron?0xFF3678C0:C_DIV;
                    cv_rrect(c,(float)kx,(float)ky,(float)cell_w,(float)kh,5,bg);
                    cv_rrect_outline(c,(float)kx,(float)ky,(float)cell_w,(float)kh,5,(lon||ron)?2.0f:1.0f,bd);
                    int tw=font_text_width(FONT_BOLD,19.0f,lbl);
                    font_draw(c,FONT_BOLD,19.0f,kx+(cell_w-tw)/2,ky+33,C_TEXT,lbl);
                }
            }
            if(rs.sk.dual_stick){
                int mid_x=(int)(14.0f+5.0f*kw+4.5f*(float)gap);
                cv_fill(c,mid_x,gy+4,2,rows*kh+(rows-1)*gap-8,accent);
            }
            grid_bottom=gy+rows*(kh+gap);
        }
        if(full){
            int ty=grid_bottom+4,th=30,bw=(w-28-7*4)/SK_CMD_COUNT;
            for(int i=0;i<SK_CMD_COUNT;i++){
                int bx=14+i*(bw+4);
                bool on=rs.toolbar_focused && rs.toolbar_index==i;
                const char *lbl=sk_desktop_cmd_label(i,rs.send_mode);
                cv_rrect(c,(float)bx,(float)ty,(float)bw,(float)th,4,on?0xFF233829:C_PANEL);
                cv_rrect_outline(c,(float)bx,(float)ty,(float)bw,(float)th,4,on?2.0f:1.0f,on?accent:C_DIV);
                ink(p,FONT_BOLD,12,bx+6,ty+20,bw-12,on?C_TEXT:C_SUB,lbl);
            }
        }
        const char *footer_msg=rs.send_failed?"Text could not be sent. Try Compose or Direct mode."
            :rs.sk.win_on?"Win latched: press a key (e.g. D/R/Tab) or press Win again for Start Menu"
            :rs.sk.dual_stick?"L/R Stick Aim  LT/RT Type  X Bksp  Y Space  Back Direct/Compose  L3 Layout  R3 Single  Start Done  B Close"
            :"A Type  X Bksp  Y Space  LT Shift  RT Page  Back Direct/Compose  L3 Layout  R3 Dual-Stick  Start Done  B Close";
        ink(p,FONT_REGULAR,12,14,h-10,w-28,rs.send_failed?C_DANGER:rs.sk.win_on?accent:C_SUB,footer_msg);
        if(gfx_upload(p->texture,p->canvas.px,w*4,w*4,h)){p->previous=rev;p->ready=true;}
        else {if(swapped)swap_panel(p);if(!p->ready){p->failed=true;input_desktop_ui_unavailable();}}
    }
    if(!p->ready)return true;
    float x=(float)(gfx_layout_width()-w)*0.5f;
    float y=rs.layout==SK_LAYOUT_FLOATING_TOP?20.0f:(float)(704-h);
    d->SetTexture(0,p->texture);gfx_draw_layout_rect(d,GFX_SHADER_RGB,x,y,x+w,y+h);d->SetTexture(0,NULL);p->used=true;
    return true;
}
static void desktop_draw(D3DDevice *d,unsigned state)
{
    if(smart_kb_overlay_draw(d))return;
    int view=state&3;if(!view)return;
    stream_panel *p=panels+2;const int w=640,h=300;
    if(!ensure_panel(d,p,w,h,~state,false,true))return;
    bool swapped=false,update=state!=p->previous;
    if(update && gfx_fence_pending(p->fence)){
        if(p->spare && !gfx_fence_pending(p->spare_fence)){swap_panel(p);swapped=true;}
        else update=false;
    }
    if(update){
        cv_fill(&p->canvas,0,0,w,h,C_BG);
        ink(p,FONT_BOLD,21,18,30,w-36,C_TEXT,view==DESK_KEYBOARD?"MoonX360 keyboard":view==DESK_MORE?"More actions":"Desktop actions");
        int selected=(state>>2)&63,cols=view==DESK_KEYBOARD?10:3,count=desktop_view_count(view);
        int cell_w=view==DESK_KEYBOARD?61:201,cell_h=view==DESK_KEYBOARD?48:65;
        for(int i=0;i<count;i++){
            int x=15+(i%cols)*cell_w,y=45+(i/cols)*cell_h;
            cv_rrect(&p->canvas,(float)x,(float)y,(float)(cell_w-4),(float)(cell_h-5),5,i==selected?0xFF244D3A:C_CARD);
            if(i==selected)cv_rrect_outline(&p->canvas,(float)x,(float)y,(float)(cell_w-4),(float)(cell_h-5),5,1,ui_theme_accent());
            const char *label=view==DESK_KEYBOARD?desktop_key_label((state>>8)&3,(state&(1<<10))!=0,i):desktop_tool_label(view,i);
            ink(p,FONT_BOLD,view==DESK_KEYBOARD?14.0f:17.0f,x+5,y+cell_h/2+5,cell_w-14,C_TEXT,label);
        }
        ink(p,FONT_REGULAR,13,18,267,w-36,C_SUB,view==DESK_KEYBOARD?"A Type   X Bksp   Y Space   LB Caps   RB Page   Start Enter   B Close":"A Select     B Back     D-Pad Navigate");
        ink(p,FONT_REGULAR,12,18,289,w-36,C_SUB,state&(1<<12)?"Text could not be queued. Try again or use Windows keyboard.":view==DESK_KEYBOARD?"Characters are sent immediately. Enter finishes. Caps changes letter case.":"Y Keyboard     R3 Actions     Back + R3 Desktop On / Off");
        if(gfx_upload(p->texture,p->canvas.px,w*4,w*4,h)){p->previous=state;p->ready=true;}
        else {if(swapped)swap_panel(p);if(!p->ready){p->failed=true;input_desktop_ui_unavailable();}}
    }
    if(!p->ready)return;
    float x=(float)(gfx_layout_width()-w)*0.5f,y=380.0f;
    d->SetTexture(0,p->texture);gfx_draw_layout_rect(d,GFX_SHADER_RGB,x,y,x+w,y+h);d->SetTexture(0,NULL);p->used=true;
}
void gfx_stream_menu_draw(D3DDevice *d,unsigned decoded,unsigned shown,unsigned dropped,unsigned decode_us,bool detailed)
{
    unsigned state=input_quick_snapshot();
    bool stopping=(state&QUICK_STOPPING)!=0;
    bool open=(state&QUICK_OPEN)!=0 || stopping;
    (void)decoded;(void)shown;(void)dropped;(void)decode_us;(void)detailed;
    quick_drawn_valid=!open && !gfx_system_ui_active();
    if(quick_drawn_valid)quick_drawn_state=state;
    if(!open && panels[0].was_open){
        char report[220];stream_panel *menu=&panels[0];
        snprintf(report,sizeof(report),
            "[UI] quick panel style=%d updates=%u raster_total_ms=%u raster_peak_ms=%u texture_bytes=%u allocations=%u\n",
            ui_stream_menu_style(),menu->updates,menu->raster_total,menu->raster_max,
            (unsigned)(menu->canvas.w*menu->canvas.h*4*(menu->spare?2:1)),
            menu->spare?2u:1u);
        plat_log(report);menu->was_open=false;
    }
    for(int i=0;i<4;i++)panels[i].used=false;
    if(!gfx_system_ui_active() && !open)desktop_draw(d,input_desktop_snapshot());
    /* The Performance Overlay is rendered by gfx_overlay_draw() for all modes;
     * never draw a second duplicate stats panel here. */
    if(gfx_system_ui_active() || !open)return;
    bool sidebar=ui_stream_menu_style()==1;
    int width=sidebar?384:448,height=sidebar?480:448;
    stream_panel *p=panels;
    if(!p->was_open){p->was_open=true;p->opened_at=GetTickCount();p->updates=p->raster_total=p->raster_max=0;p->previous=~state;p->updated=0;}
    if(!ensure_panel(d,p,width,height,~state,true,false))return;
    DWORD now=GetTickCount();
    bool update=state!=p->previous || now-p->updated>=1000,swapped=false;
    if(update && gfx_fence_pending(p->fence)){
        if(!p->spare && !p->spare_failed)
            if(FAILED(d->CreateTexture(width,height,1,0,D3DFMT_LIN_A8R8G8B8,0,&p->spare,NULL)))p->spare_failed=true;
        if(p->spare && !gfx_fence_pending(p->spare_fence)){swap_panel(p);swapped=true;}
        else update=false;
    }
    if(update){
        DWORD started=GetTickCount();
        video_refresh_snapshot rs;
        video_get_refresh_snapshot(&rs);
        cv_fill(&p->canvas,0,0,width,height,C_BG);
        int page=(state>>QUICK_PAGE_SHIFT)&QUICK_PAGE_MASK;
        if(stopping){
            ink(p,FONT_BOLD,24,24,68,width-48,C_TEXT,"Disconnecting...");
            ink(p,FONT_REGULAR,16,24,108,width-48,C_SUB,"Stopping the session safely.");
        }
        else if(page==QUICK_DESKTOP_PAGE)desktop_page(p,width,height,state);
        else if(page==QUICK_CONFIRM)confirm_page(p,width,height);
        else main_page(p,width,height,state,rs.state);
        cv_rrect_outline(&p->canvas,0,0,(float)width,(float)height,0,1.0f,C_DIV);
        cv_fill(&p->canvas,0,0,3,height,ui_theme_accent());
        {unsigned ms=GetTickCount()-started;p->updates++;p->raster_total+=ms;if(ms>p->raster_max)p->raster_max=ms;}
        if(gfx_upload(p->texture,p->canvas.px,width*4,width*4,height)){
            p->previous=state;p->updated=now;p->ready=true;
        } else {
            if(swapped)swap_panel(p);
            if(!p->ready){p->failed=true;input_quick_ui_unavailable();}
        }
    }
    if(!p->ready)return;
    d->SetTexture(0,p->texture);
    float x=sidebar?32.0f:(float)(gfx_layout_width()-width)*0.5f;
    float y=(720.0f-(float)height)*0.5f;
    gfx_draw_layout_rect(d,GFX_SHADER_RGB,x,y,x+width,y+height);
    d->SetTexture(0,NULL);p->used=true;
    if(p->previous==state){quick_drawn_state=state;quick_drawn_valid=true;}
}
void gfx_stream_menu_submitted(DWORD fence)
{
    /* Timestamp after all panel draw commands have been enqueued. The fence
     * still has to complete and Present is not scanout evidence. */
    if(quick_drawn_valid && quick_drawn_state!=quick_reported_state){
        if(((quick_drawn_state|quick_reported_state)&(QUICK_OPEN|QUICK_STOPPING)) && perf_enabled()){
            char trace[128];snprintf(trace,sizeof(trace),"[UX] quick submit_ms=%u state=%u\n",GetTickCount(),quick_drawn_state);plat_log(trace);
        }
        quick_reported_state=quick_drawn_state;
    }
    for(int i=0;i<4;i++)if(panels[i].used)panels[i].fence=fence;
}
