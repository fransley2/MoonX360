#include "draw.h"
#include "anim.h"
#include "design_system.h"
#include "font.h"
#include "layout.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static struct ui_draw_command commands[UI_DRAW_CAPACITY];
static struct ui_draw_command scene[512];
static unsigned scene_count;
static unsigned used;
static bool enabled,recording,ready;
static canvas_t *target;
static canvas_t buttons;
static struct ui_memory_info memory;
static bool motion_enabled=true;
bool ui_motion_enabled(void){return motion_enabled;}
static struct {bool active,started;uint32_t start;int index;} transition={false,false,0,-1};
void ui_transition_begin(void)
{transition.active=motion_enabled;transition.started=false;transition.index=-1;}
bool ui_transition_active(void){return enabled && ready && transition.active;}
bool ui_transition_tick(uint32_t now)
{
    if(!ui_transition_active())return false;
    if(!transition.started){transition.started=true;transition.start=now;}
    uint32_t elapsed=(uint32_t)(now-transition.start);
    unsigned alpha=elapsed>=UI_PAGE_FADE_MS?0:150u*(UI_PAGE_FADE_MS-elapsed)/UI_PAGE_FADE_MS;
    if(transition.index<0){
        cv_unclip(target);ui_fill(target,0,0,target->w,target->h,alpha<<24);
        if(!ready || !used || commands[used-1].kind!=UI_RECT)return false;
        transition.index=(int)used-1;
    } else commands[transition.index].color=alpha<<24;
    if(elapsed>=UI_PAGE_FADE_MS)transition.active=false;
    return true; /* present the final transparent frame once */
}
static bool focus_feedback; /* Older synchronous presenters need no terminal-frame retry. */
static struct cover_fade {const void *pixels;uint32_t generation,start,stamp;bool started,active;} art_fades[12];
static struct cover_ref {unsigned command,slot;uint32_t color;} art_refs[16];
static unsigned art_ref_count,art_stamp;
static bool art_pending;
static bool image_retry;
static bool image_retry_started;
static uint32_t image_retry_clock;
bool ui_draw_retry_pending(void){return image_retry;}
void ui_draw_retry_images(bool pending){image_retry=pending;if(!pending)image_retry_started=false;}
static bool image_retry_tick(uint32_t now)
{
    if(!image_retry)return false;
    if(!image_retry_started){image_retry_started=true;image_retry_clock=now;return false;}
    if(now-image_retry_clock<1000u)return false;
    image_retry_clock=now;return true;
}
static bool cover_tick(uint32_t now)
{
    bool changed=false;
    for(unsigned i=0;i<art_ref_count;i++) {
        struct cover_ref *r=art_refs+i;struct cover_fade *a=art_fades+r->slot;
        if(r->command>=used || commands[r->command].kind!=UI_IMAGE)continue;
        if(!a->active){if(commands[r->command].color!=r->color){commands[r->command].color=r->color;changed=true;}continue;}
        if(!a->started){a->started=true;a->start=now;}
        uint32_t elapsed=(uint32_t)(now-a->start);
        unsigned alpha=r->color>>24;
        if(!motion_enabled || elapsed>=UI_ART_FADE_MS)a->active=false;
        else alpha=alpha*elapsed/UI_ART_FADE_MS;
        uint32_t color=(r->color&0xffffffu)|(alpha<<24);
        if(commands[r->command].color!=color){commands[r->command].color=color;changed=true;}
        /* Include the final opaque frame in the present/ack protocol. */
        if(a->active)changed=true;
    }
    if(changed)art_pending=true;
    return changed || (focus_feedback && art_pending);
}
void ui_cover_fade_mark(void)
{
    if(!ready || !used || commands[used-1].kind!=UI_IMAGE || art_ref_count==16)return;
    struct ui_draw_command *c=commands+used-1;int slot=-1,oldest=0;
    for(int i=0;i<12;i++) {
        if(art_fades[i].pixels==c->pixels && art_fades[i].generation==c->generation){slot=i;break;}
        if(!art_fades[i].pixels || (int32_t)(art_fades[i].stamp-art_fades[oldest].stamp)<0)oldest=i;
    }
    if(slot<0){memory.cover_fade_starts++;slot=oldest;memset(art_fades+slot,0,sizeof(art_fades[slot]));art_fades[slot].pixels=c->pixels;art_fades[slot].generation=c->generation;art_fades[slot].active=motion_enabled;}
    else memory.cover_fade_reuses++;
    art_fades[slot].stamp=++art_stamp;
    struct cover_ref *r=art_refs+art_ref_count++;r->command=used-1;r->slot=(unsigned)slot;r->color=c->color;
}
static struct {
    bool known,registered,active,started,dirty,pending,glide;
    int context,item;
    unsigned index;
    uint32_t start,last;
    struct ui_draw_command base,prev;
} focus;

void ui_focus_presented(void){focus_feedback=true;focus.pending=art_pending=false;}
void ui_focus_finish(void)
{
    if(ready && focus.registered && focus.index<used)commands[focus.index]=focus.base;
    focus.active=focus.pending=focus.glide=false;
}

void ui_set_motion(bool value)
{
    motion_enabled=value;
    if(!value) {
        transition.active=false;
        bool visible=ready && focus.registered;
        ui_focus_finish();focus.pending=visible;
    }
}
void ui_focus_mark(int context,int item)
{
    if(!ready || !used || commands[used-1].kind!=UI_OUTLINE)return;
    struct ui_draw_command *c=commands+used-1;
    bool changed=!focus.known || context!=focus.context || item!=focus.item ||
        c->x!=focus.base.x || c->y!=focus.base.y || c->w!=focus.base.w || c->h!=focus.base.h || c->color!=focus.base.color;
    if(changed){
        bool same_context=focus.known && context==focus.context && item!=focus.item;
        if(same_context){focus.prev=focus.base;focus.glide=motion_enabled;}
        else focus.glide=false;
        focus.active=motion_enabled;focus.started=false;
    }
    focus.context=context;focus.item=item;focus.index=used-1;focus.base=*c;
    focus.known=focus.registered=focus.dirty=true;
}
bool ui_focus_tick(uint32_t now)
{
    if(!enabled || !ready)return false;
    bool art_changed=cover_tick(now);
    if(image_retry_tick(now))art_changed=true;
    if(!focus.registered || focus.index>=used)return art_changed;
    if(!focus.active)return art_changed || (focus_feedback && focus.pending);
    uint32_t elapsed=0;
    if(!focus.started){focus.started=true;focus.start=now;}
    else {elapsed=(uint32_t)(now-focus.start);if(elapsed==focus.last && !focus.dirty)return art_changed || (focus_feedback && focus.pending);}
    focus.dirty=false;focus.pending=true;
    focus.last=elapsed;
    struct ui_draw_command *c=commands+focus.index;
    *c=focus.base;
    if(elapsed>=UI_FOCUS_MS){focus.active=false;focus.glide=false;return true;}
    float progress=ui_anim_progress(now,focus.start,UI_FOCUS_MS);
    float remaining=1.0f-ui_ease_out_cubic(progress);
    float spread=focus.base.t*1.25f*remaining;
    c->x-=spread;c->y-=spread;c->w+=2*spread;c->h+=2*spread;c->r+=spread;
    unsigned alpha=(unsigned)((float)(focus.base.color>>24)*(1.0f-0.42f*remaining));
    c->color=(focus.base.color&0xFFFFFFu)|(alpha<<24);
    return true;
}

static void raster(const struct ui_draw_command *c) {
    cv_clip(target,c->clip[0],c->clip[1],c->clip[2]-c->clip[0],c->clip[3]-c->clip[1]);
    switch(c->kind) {
    case UI_RECT:cv_rrect(target,c->x,c->y,c->w,c->h,c->r,c->color);break;
    case UI_GRADIENT:cv_vgradient(target,(int)c->x,(int)c->y,(int)c->w,(int)c->h,c->color,c->bottom);break;
    case UI_OUTLINE:cv_rrect_outline(target,c->x,c->y,c->w,c->h,c->r,c->t,c->color);break;
    case UI_LINE:cv_line(target,c->x,c->y,c->w,c->h,c->t,c->color);break;
    case UI_TRIANGLE:cv_triangle(target,c->x,c->y,c->w,c->h,c->x2,c->y2,c->color);break;
    case UI_GLYPH:cv_mask(target,(int)c->x,(int)c->y,(const uint8_t*)c->pixels,c->iw,c->ih,c->stride,c->color);break;
    case UI_IMAGE: {
        const uint32_t *p=(const uint32_t*)c->pixels;
        unsigned cmd_a=c->color>>24;
        int x0=(int)c->x,y0=(int)c->y,w=(int)c->w,h=(int)c->h;
        if(!cmd_a || w<=0 || h<=0 || c->sw<=0 || c->sh<=0)break;
        int dx0=x0>target->cx0?x0:target->cx0;
        int dy0=y0>target->cy0?y0:target->cy0;
        int dx1=(x0+w)<target->cx1?(x0+w):target->cx1;
        int dy1=(y0+h)<target->cy1?(y0+h):target->cy1;
        if(dx0>=dx1 || dy0>=dy1)break;
        int x_cnt=dx1-dx0;
        if(x_cnt<=1920) {
            int sx_tab[1920];
            for(int x=0;x<x_cnt;x++) {
                int sx=c->sx+(dx0+x-x0)*c->sw/w;
                sx_tab[x]=(sx<0 || sx>=c->iw)?-1:sx;
            }
            for(int dy=dy0;dy<dy1;dy++) {
                int sy=c->sy+(dy-y0)*c->sh/h;
                if(sy<0 || sy>=c->ih)continue;
                const uint32_t *src_row=p+sy*c->iw;
                uint32_t *dst_row=target->px+dy*target->w+dx0;
                if(cmd_a==255u) {
                    for(int x=0;x<x_cnt;x++) {
                        int sx=sx_tab[x];if(sx<0)continue;
                        uint32_t color=src_row[sx];
                        unsigned sa=color>>24;
                        if(sa==255u)dst_row[x]=color;
                        else if(sa)dst_row[x]=cv_mix(dst_row[x],color,sa);
                    }
                } else {
                    for(int x=0;x<x_cnt;x++) {
                        int sx=sx_tab[x];if(sx<0)continue;
                        uint32_t color=src_row[sx];
                        unsigned alpha=(color>>24)*cmd_a/255u;
                        if(alpha)dst_row[x]=cv_mix(dst_row[x],color,alpha);
                    }
                }
            }
        } else {
            for(int dy=dy0;dy<dy1;dy++) {
                int sy=c->sy+(dy-y0)*c->sh/h;
                if(sy<0 || sy>=c->ih)continue;
                for(int dx=dx0;dx<dx1;dx++) {
                    int sx=c->sx+(dx-x0)*c->sw/w;
                    if(sx<0 || sx>=c->iw)continue;
                    uint32_t color=p[sy*c->iw+sx];
                    unsigned alpha=(color>>24)*cmd_a/255u;
                    target->px[dy*target->w+dx]=cv_mix(target->px[dy*target->w+dx],color,alpha);
                }
            }
        }
        break;
    }
    }
}
void ui_draw_software(void) {
    if(!ready)return;
    /* A CPU fallback has no retained GPU animation loop. Complete cover fades
     * before rasterizing so a failed shader cannot leave permanently dim art. */
    for(unsigned i=0;i<art_ref_count;i++){if(commands[art_refs[i].command].kind==UI_IMAGE)commands[art_refs[i].command].color=art_refs[i].color;art_fades[art_refs[i].slot].active=false;}
    int clip[4]={target->cx0,target->cy0,target->cx1,target->cy1};
    for(unsigned i=0;i<used;i++)raster(commands+i);
    cv_clip(target,clip[0],clip[1],clip[2]-clip[0],clip[3]-clip[1]);
    ready=recording=false;memory.software_fallbacks++;
}
static struct ui_draw_command *push(canvas_t *c,int kind) {
    if(!recording||c!=target)return NULL;
    if(used==UI_DRAW_CAPACITY) {ui_draw_software();return NULL;}
    struct ui_draw_command *d=commands+used++;memset(d,0,sizeof(*d));d->kind=kind;
    d->clip[0]=c->cx0;d->clip[1]=c->cy0;d->clip[2]=c->cx1;d->clip[3]=c->cy1;
    memory.commands=used;return d;
}
void ui_draw_enable(bool value) {enabled=value;if(!value)ui_draw_suspend();}
bool ui_draw_enabled(void) {return enabled;}
bool ui_draw_recording(void) {return recording;}
void ui_draw_begin(canvas_t *c) {art_ref_count=0;art_pending=false;focus.registered=focus.pending=false;transition.index=-1;target=c;used=0;ready=recording=enabled;memory.commands=0;memory.canvas_bytes=(uint32_t)c->w*c->h*4;}
void ui_draw_suspend(void) {art_ref_count=0;art_pending=false;focus.known=focus.active=focus.registered=focus.pending=false;transition.active=false;recording=ready=false;used=0;}
void ui_scene_drop(void) {scene_count=0;}
bool ui_scene_store(void) {
    scene_count=0;
    if(!ready || used>sizeof(scene)/sizeof(scene[0]))return false;
    memcpy(scene,commands,used*sizeof(commands[0]));scene_count=used;
    for(unsigned i=0;i<art_ref_count;i++)scene[art_refs[i].command].color=art_refs[i].color;
    return true;
}
bool ui_scene_restore(canvas_t *canvas) {
    if(!enabled || !scene_count)return false;
    ui_draw_begin(canvas);memcpy(commands,scene,scene_count*sizeof(commands[0]));
    used=scene_count;memory.commands=used;
    /* Recover only known cover commands; brand/button images never fade. */
    unsigned total=used;
    for(unsigned i=0;i<total;i++)if(commands[i].kind==UI_IMAGE)for(int j=0;j<12;j++)
        if(art_fades[j].pixels==commands[i].pixels && art_fades[j].generation==commands[i].generation){used=i+1;ui_cover_fade_mark();break;}
    used=total;return true;
}
const struct ui_draw_command *ui_draw_commands(int *count) {*count=ready?(int)used:0;return ready?commands:NULL;}
void ui_draw_gpu_memory(uint32_t glyph,uint32_t covers,uint32_t atlas,unsigned count) {
    memory.glyph_bytes=glyph;memory.cover_gpu_bytes=covers;memory.button_gpu_bytes=atlas;
    memory.gpu_bytes=glyph+covers+atlas;memory.gpu_covers=count;
}
void ui_draw_render_stats(uint32_t draw_calls,uint32_t batched_commands,uint32_t state_changes,uint32_t glyph_uploads) {
    memory.draw_calls=draw_calls;memory.batched_commands=batched_commands;
    memory.state_changes=state_changes;memory.glyph_uploads=glyph_uploads;
}
void ui_draw_cover_memory(unsigned count,unsigned bytes) {memory.loaded_covers=count;memory.cover_cpu_bytes=bytes;}
void ui_draw_memory(struct ui_memory_info *out) {*out=memory;out->command_bytes=sizeof(commands)+sizeof(scene);}
void ui_round(canvas_t *c,float x,float y,float w,float h,float r,uint32_t color) {
    struct ui_draw_command *d=push(c,UI_RECT);if(!d) {cv_rrect(c,x,y,w,h,r,color);return;}
    d->x=x;d->y=y;d->w=w;d->h=h;d->r=r;d->color=color;
}
void ui_fill(canvas_t *c,int x,int y,int w,int h,uint32_t color) {ui_round(c,(float)x,(float)y,(float)w,(float)h,0,color);}
void ui_gradient(canvas_t *c,int x,int y,int w,int h,uint32_t top,uint32_t bottom) {
    struct ui_draw_command *d=push(c,UI_GRADIENT);if(!d){cv_vgradient(c,x,y,w,h,top,bottom);return;}
    d->x=(float)x;d->y=(float)y;d->w=(float)w;d->h=(float)h;d->color=top;d->bottom=bottom;
}
void ui_outline(canvas_t *c,float x,float y,float w,float h,float r,float t,uint32_t color) {
    struct ui_draw_command *d=push(c,UI_OUTLINE);if(!d){cv_rrect_outline(c,x,y,w,h,r,t,color);return;}
    d->x=x;d->y=y;d->w=w;d->h=h;d->r=r;d->t=t;d->color=color;
}
void ui_shadow(canvas_t *c,float x,float y,float w,float h,float r,float blur,uint32_t color) {
    (void)blur;ui_round(c,x+2,y+4,w,h,r,WITH_ALPHA(color,45));
}
void ui_circle(canvas_t *c,float x,float y,float r,uint32_t color) {ui_round(c,x-r,y-r,r*2,r*2,r,color);}
void ui_ring(canvas_t *c,float x,float y,float r,float t,uint32_t color) {ui_outline(c,x-r,y-r,r*2,r*2,r,t,color);}
void ui_line(canvas_t *c,float x,float y,float x1,float y1,float t,uint32_t color) {
    struct ui_draw_command *d=push(c,UI_LINE);if(!d){cv_line(c,x,y,x1,y1,t,color);return;}
    d->x=x;d->y=y;d->w=x1;d->h=y1;d->t=t;d->color=color;
}
void ui_triangle(canvas_t *c,float x,float y,float x1,float y1,float x2,float y2,uint32_t color) {
    struct ui_draw_command *d=push(c,UI_TRIANGLE);if(!d){cv_triangle(c,x,y,x1,y1,x2,y2,color);return;}
    d->x=x;d->y=y;d->w=x1;d->h=y1;d->x2=x2;d->y2=y2;d->color=color;
}
void ui_picture_region(canvas_t *c,const uint32_t *pixels,int iw,int ih,int sx,int sy,int sw,int sh,
                       float x,float y,float w,float h,unsigned alpha,uint32_t generation) {
    if(!pixels||iw<=0||ih<=0||sw<=0||sh<=0||w<=0||h<=0)return;
    struct ui_draw_command local,*d=push(c,UI_IMAGE);
    if(!d) {memset(&local,0,sizeof(local));d=&local;d->kind=UI_IMAGE;d->clip[0]=c->cx0;d->clip[1]=c->cy0;d->clip[2]=c->cx1;d->clip[3]=c->cy1;}
    d->pixels=pixels;d->iw=iw;d->ih=ih;d->sx=sx;d->sy=sy;d->sw=sw;d->sh=sh;d->x=x;d->y=y;d->w=w;d->h=h;
    d->color=ARGB(alpha,255,255,255);d->generation=generation;
    if(d==&local) {canvas_t *save=target;target=c;raster(d);target=save;}
}
void ui_picture(canvas_t *c,const uint32_t *p,int w,int h,int x,int y,float r,unsigned a) {
    (void)r;ui_picture_region(c,p,w,h,0,0,w,h,(float)x,(float)y,(float)w,(float)h,a,0);
}
void ui_glyph(canvas_t *c,int x,int y,const uint8_t *mask,int w,int h,int stride,uint32_t color) {
    if(x+w<=c->cx0 || y+h<=c->cy0 || x>=c->cx1 || y>=c->cy1)return;
    struct ui_draw_command *d=push(c,UI_GLYPH);if(!d){cv_mask(c,x,y,mask,w,h,stride,color);return;}
    d->x=(float)x;d->y=(float)y;d->pixels=mask;d->iw=w;d->ih=h;d->stride=stride;d->color=color;
}
static void draw_atlas_dpad(canvas_t *c,int ox,int mode)
{
    /* mode: 0=all, 1=horizontal (Left/Right), 2=vertical (Up/Down) */
    uint32_t dish=HEX(0x252D33),base=HEX(0x5A666E),hi=HEX(0xF4F7F5);
    for(int py=0;py<32;py++)for(int px=0;px<32;px++) {
        int dx=px-16,dy=py-16,d2=dx*dx+dy*dy;
        if(d2<14*14)c->px[py*UI_BUTTON_ATLAS_W+ox+px]=dish;
        bool horiz=(abs(dy)<=3 && abs(dx)<=10);
        bool vert=(abs(dx)<=3 && abs(dy)<=10);
        if(horiz || vert) {
            uint32_t col=base;
            if(mode==0)col=hi;
            else if(mode==1 && horiz && abs(dx)>=3)col=hi;
            else if(mode==2 && vert && abs(dy)>=3)col=hi;
            c->px[py*UI_BUTTON_ATLAS_W+ox+px]=col;
        }
    }
}
static void draw_atlas_stick(canvas_t *c,int ox,const char *ch)
{
    for(int py=0;py<32;py++)for(int px=0;px<32;px++) {
        int dx=px-16,dy=py-16,d2=dx*dx+dy*dy;
        if(d2<14*14) {
            uint32_t col=d2>11*11?HEX(0x9AA6AE):d2>9*9?HEX(0x20272C):HEX(0x364148);
            c->px[py*UI_BUTTON_ATLAS_W+ox+px]=col;
        }
    }
    int tw=font_text_width(FONT_BOLD,14.0f,ch);
    font_draw(c,FONT_BOLD,14.0f,ox+(32-tw)/2,21,HEX(0xF4F7F5),ch);
}
bool ui_button(canvas_t *c,const char *label,int x,int y,int w,int h) {
    static const struct {const char *name;int sx,sw;} map[]={
        {"A",0,32},{"B",32,32},{"X",64,32},{"Y",96,32},
        {"LB",128,40},{"RB",168,40},{"LT",208,40},{"RT",248,40},
        {"Start",288,32},{"Back",320,32},
        {"D-Pad",352,32},
        {"Left / Right",384,32},{"Left Right",384,32},{"\xE2\x86\x90 \xE2\x86\x92",384,32},
        {"Up / Down",416,32},{"Up Down",416,32},{"\xE2\x86\x91 \xE2\x86\x93",416,32},
        {"L3",448,32},{"LS",448,32},{"R3",480,32},{"RS",480,32}
    };
    int index=-1;
    for(int i=0;i<(int)(sizeof(map)/sizeof(map[0]));i++)if(!strcmp(label,map[i].name)){index=i;break;}
    if(index<0)return false;
    if(!buttons.px) {
        if(!cv_init(&buttons,UI_BUTTON_ATLAS_W,UI_BUTTON_ATLAS_H))return false;
        memset(buttons.px,0,UI_BUTTON_ATLAS_W*UI_BUTTON_ATLAS_H*4);
        /* 0..3: Xbox 360 A/B/X/Y face buttons with subtle console rim */
        const uint32_t face_colors[]={DS_BTN_A,DS_BTN_B,DS_BTN_X,DS_BTN_Y};
        const char *face_names[]={"A","B","X","Y"};
        for(int b=0;b<4;b++) {
            int bx=b*32;
            for(int py=0;py<32;py++)for(int px=0;px<32;px++) {
                int dx=px-16,dy=py-16,d2=dx*dx+dy*dy;
                if(d2<14*14) {
                    uint32_t col=face_colors[b];
                    if(d2>11*11)col=cv_mix(col,HEX(0x000000),65);
                    else if(py<11)col=cv_mix(col,HEX(0xFFFFFF),28);
                    buttons.px[py*UI_BUTTON_ATLAS_W+bx+px]=col;
                }
            }
            int tw=font_text_width(FONT_BOLD,18.0f,face_names[b]);
            font_draw(&buttons,FONT_BOLD,18.0f,bx+(32-tw)/2,22,HEX(0xFFFFFF),face_names[b]);
        }
        /* 4..7: LB, RB, LT, RT shoulder bumpers & triggers */
        const char *sh_names[]={"LB","RB","LT","RT"};
        for(int b=0;b<4;b++) {
            int bx=128+b*40;
            uint32_t col=b<2?DS_BTN_BUMPER:DS_BTN_TRIGGER;
            for(int py=5;py<27;py++)for(int px=2;px<38;px++) {
                bool cut=(px<5 && py<8)||(px>34 && py<8)||(px<5 && py>23)||(px>34 && py>23);
                if(!cut)buttons.px[py*UI_BUTTON_ATLAS_W+bx+px]=col;
            }
            int tw=font_text_width(FONT_BOLD,14.0f,sh_names[b]);
            font_draw(&buttons,FONT_BOLD,14.0f,bx+(40-tw)/2,21,HEX(0x161D22),sh_names[b]);
        }
        /* 8..9: Start (right arrow pill) & Back (left arrow pill) */
        for(int b=0;b<2;b++) {
            int bx=288+b*32;
            for(int py=7;py<25;py++)for(int px=3;px<29;px++) {
                int dx=px-16,dy=py-16;
                if(dx*dx*9+dy*dy*20<=13*13*9)
                    buttons.px[py*UI_BUTTON_ATLAS_W+bx+px]=HEX(0x364148);
            }
            for(int py=11;py<=21;py++) {
                int half=(5-abs(py-16));
                if(b==0) { /* Start: right-pointing triangle */
                    for(int px=13;px<=13+half*2;px++)buttons.px[py*UI_BUTTON_ATLAS_W+bx+px]=HEX(0xF4F7F5);
                } else {   /* Back: left-pointing triangle */
                    for(int px=19-half*2;px<=19;px++)buttons.px[py*UI_BUTTON_ATLAS_W+bx+px]=HEX(0xF4F7F5);
                }
            }
        }
        /* 10..12: D-Pad (all, horizontal, vertical) */
        draw_atlas_dpad(&buttons,352,0);
        draw_atlas_dpad(&buttons,384,1);
        draw_atlas_dpad(&buttons,416,2);
        /* 13..14: L3/LS and R3/RS stick caps */
        draw_atlas_stick(&buttons,448,"L");
        draw_atlas_stick(&buttons,480,"R");
        memory.button_cpu_bytes=UI_BUTTON_ATLAS_W*UI_BUTTON_ATLAS_H*4;
    }
    ui_picture_region(c,buttons.px,UI_BUTTON_ATLAS_W,UI_BUTTON_ATLAS_H,map[index].sx,0,map[index].sw,UI_BUTTON_ATLAS_H,(float)x,(float)y,(float)w,(float)h,255,1);
    return true;
}

bool ui_focus_bounds(float *x,float *y,float *w,float *h)
{
    if(!ready || !focus.registered)return false;
    *x=focus.base.x;*y=focus.base.y;*w=focus.base.w;*h=focus.base.h;return true;
}

void ui_draw_hide_image(const void *pixels,float x,float y,float w,float h)
{
    if(!ready)return;
    for(unsigned i=0;i<used;i++)if(commands[i].kind==UI_IMAGE && commands[i].pixels==pixels &&
        commands[i].x==x && commands[i].y==y && commands[i].w==w && commands[i].h==h) {
        commands[i].color&=0x00ffffffu;
        /* Fade ticks and software snapshots restore this command's target
         * opacity. Hide that too, without hiding another use of the image. */
        for(unsigned j=0;j<art_ref_count;j++)if(art_refs[j].command==i)art_refs[j].color&=0x00ffffffu;
    }
}
void ui_draw_retire_image(const void *pixels)
{
    if(!pixels)return;
    ui_scene_drop();
    for(unsigned i=0;i<12;i++)if(art_fades[i].pixels==pixels)memset(art_fades+i,0,sizeof(art_fades[i]));
    for(unsigned i=0;i<used;i++)if(commands[i].kind==UI_IMAGE && commands[i].pixels==pixels){
        commands[i].kind=UI_RECT;commands[i].pixels=NULL;commands[i].r=0;
        commands[i].color=0xff20272au;
    }
}
