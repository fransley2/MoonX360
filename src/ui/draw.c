#include "draw.h"
#include "font.h"
#include "layout.h"
#include <math.h>
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
static bool focus_feedback; /* Older synchronous presenters need no terminal-frame retry. */
static struct cover_fade {const void *pixels;uint32_t generation,start,stamp;bool started,active;} art_fades[12];
static struct cover_ref {unsigned command,slot;uint32_t color;} art_refs[16];
static unsigned art_ref_count,art_stamp;
static bool art_pending;
static bool cover_tick(uint32_t now)
{
    bool changed=false;
    for(unsigned i=0;i<art_ref_count;i++) {
        struct cover_ref *r=art_refs+i;struct cover_fade *a=art_fades+r->slot;
        if(r->command>=used)continue;
        if(!a->active){if(commands[r->command].color!=r->color){commands[r->command].color=r->color;changed=true;}continue;}
        if(!a->started){a->started=true;a->start=now;}
        unsigned elapsed=now-a->start,alpha=r->color>>24;
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
    if(slot<0){slot=oldest;memset(art_fades+slot,0,sizeof(art_fades[slot]));art_fades[slot].pixels=c->pixels;art_fades[slot].generation=c->generation;art_fades[slot].active=motion_enabled;}
    art_fades[slot].stamp=++art_stamp;
    struct cover_ref *r=art_refs+art_ref_count++;r->command=used-1;r->slot=(unsigned)slot;r->color=c->color;
}
static struct {
    bool known,registered,active,started,dirty,pending;
    int context,item;
    unsigned index;
    uint32_t start,last;
    struct ui_draw_command base;
} focus;

void ui_focus_presented(void){focus_feedback=true;focus.pending=art_pending=false;}
void ui_focus_finish(void)
{
    if(ready && focus.registered && focus.index<used)commands[focus.index]=focus.base;
    focus.active=focus.pending=false;
}

void ui_set_motion(bool value)
{
    motion_enabled=value;
    if(!value) {
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
    if(changed){focus.active=motion_enabled;focus.started=false;}
    focus.context=context;focus.item=item;focus.index=used-1;focus.base=*c;
    focus.known=focus.registered=focus.dirty=true;
}
bool ui_focus_tick(uint32_t now)
{
    if(!enabled || !ready)return false;
    bool art_changed=cover_tick(now);
    if(!focus.registered || focus.index>=used)return art_changed;
    if(!focus.active)return art_changed || (focus_feedback && focus.pending);
    uint32_t elapsed=0;
    if(!focus.started){focus.started=true;focus.start=now;}
    else {elapsed=(uint32_t)(now-focus.start);if(elapsed==focus.last && !focus.dirty)return art_changed || (focus_feedback && focus.pending);}
    focus.dirty=false;focus.pending=true;
    focus.last=elapsed;
    struct ui_draw_command *c=commands+focus.index;
    *c=focus.base;
    if(elapsed>=UI_FOCUS_MS){focus.active=false;return true;}
    float remaining=1.0f-(float)elapsed/UI_FOCUS_MS;
    float ease=remaining*remaining*remaining;
    float spread=focus.base.t*1.3f*ease;
    c->x-=spread;c->y-=spread;c->w+=2*spread;c->h+=2*spread;c->r+=spread;
    unsigned alpha=(unsigned)((float)(focus.base.color>>24)*(1.0f-.45f*ease));
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
        int x0=(int)c->x,y0=(int)c->y,w=(int)c->w,h=(int)c->h;
        for(int y=0;y<h;y++)for(int x=0;x<w;x++) {
            int dx=x0+x,dy=y0+y;if(dx<target->cx0||dx>=target->cx1||dy<target->cy0||dy>=target->cy1)continue;
            int sx=c->sx+x*c->sw/w,sy=c->sy+y*c->sh/h;
            if(sx<0||sy<0||sx>=c->iw||sy>=c->ih)continue;
            uint32_t color=p[sy*c->iw+sx];unsigned alpha=(color>>24)*(c->color>>24)/255;
            target->px[dy*target->w+dx]=cv_mix(target->px[dy*target->w+dx],color,alpha);
        }break;
    }
    }
}
void ui_draw_software(void) {
    if(!ready)return;
    /* A CPU fallback has no retained GPU animation loop. Complete cover fades
     * before rasterizing so a failed shader cannot leave permanently dim art. */
    for(unsigned i=0;i<art_ref_count;i++){commands[art_refs[i].command].color=art_refs[i].color;art_fades[art_refs[i].slot].active=false;}
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
void ui_draw_begin(canvas_t *c) {art_ref_count=0;art_pending=false;focus.registered=focus.pending=false;target=c;used=0;ready=recording=enabled;memory.commands=0;memory.canvas_bytes=(uint32_t)c->w*c->h*4;}
void ui_draw_suspend(void) {art_ref_count=0;art_pending=false;memset(art_fades,0,sizeof(art_fades));focus.known=focus.active=focus.registered=focus.pending=false;recording=ready=false;used=0;}
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
bool ui_button(canvas_t *c,const char *label,int x,int y,int w,int h) {
    static const char *labels[]={"A","B","X","Y","LB","RB"};
    int index=-1;for(int i=0;i<6;i++)if(!strcmp(label,labels[i]))index=i;
    if(index<0)return false;
    if(!buttons.px) {
        if(!cv_init(&buttons,256,32))return false;memset(buttons.px,0,256*32*4);
        const uint32_t colors[]={HEX(0x5FAE27),HEX(0xCE4038),HEX(0x3678C0),HEX(0xD6AF25),HEX(0xCCCCCC),HEX(0xCCCCCC)};
        for(int b=0;b<6;b++) {
            int bx=b<4?b*32:128+(b-4)*48,bw=b<4?32:48;
            for(int py=0;py<32;py++)for(int px=0;px<bw;px++) {
                bool inside=b<4?((px-16)*(px-16)+(py-16)*(py-16)<14*14):(px>1&&px<bw-2&&py>4&&py<28);
                if(inside)buttons.px[py*256+bx+px]=colors[b];
            }
            int size=b<4?18:14,tw=font_text_width(FONT_BOLD,(float)size,labels[b]);
            font_draw(&buttons,FONT_BOLD,(float)size,bx+(bw-tw)/2,22,b<4?HEX(0xFFFFFF):HEX(0x202420),labels[b]);
        }
        memory.button_cpu_bytes=256*32*4;
    }
    int sx=index<4?index*32:128+(index-4)*48,sw=index<4?32:48;
    ui_picture_region(c,buttons.px,256,32,sx,0,sw,32,(float)x,(float)y,(float)w,(float)h,255,1);return true;
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
        commands[i].x==x && commands[i].y==y && commands[i].w==w && commands[i].h==h)commands[i].color&=0x00ffffffu;
}
