#include "config.h"
#include "wake.h"

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

void config_defaults(struct config *c)
{
    memset(c, 0, sizeof(*c));
    c->ui_motion = false;
    c->width = 1280;
    c->height = 720;
    c->fps = CONFIG_FPS_DEFAULT;
    c->bitrate_kbps = 12000;
    c->packet_size = 1024;
    c->fast_decode = true;
    c->renderer = 0;
    c->vsync = true;
    c->sops = true;
    c->history_enabled = true;
    c->local_audio = false;
    c->audio_mode = 0;
    c->mouse_speed=100;c->mouse_acceleration=0;c->mouse_scroll_ms=100;
    c->stream_profile = CONFIG_PROFILE_SMOOTH;
    c->launch_cover_ms = 5000;
    c->launch_animation = true;
    c->mdns_discovery = true;
    c->thread_layout = 1;
    c->decode_workers = 4;
    c->left_max = c->right_max = 100;
}

static void trim(char *s)
{
    char *e;

    while (*s == ' ' || *s == '\t')
        memmove(s, s + 1, strlen(s));
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
        *--e = '\0';
}

void config_load(struct config *c)
{
    char *text, *line, *save;
    bool have_overlay_mode=false, have_profile=false, have_left_min=false, have_right_min=false;

    config_defaults(c);
    text = storage_read("moonlight.ini", NULL);
    if (text == NULL)
        return;

    for (line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *eq = strchr(line, '=');

        if (line[0] == '#' || line[0] == ';' || eq == NULL)
            continue;
        *eq = '\0';
        trim(line);
        trim(eq + 1);

        if (!strcmp(line, "host"))
            snprintf(c->host, sizeof(c->host), "%s", eq + 1);
        else if (!strcmp(line,"host_uuid")){if(wake_identity_valid(eq+1))snprintf(c->host_uuid,sizeof(c->host_uuid),"%s",eq+1);else c->host_uuid[0]=0;}
        else if (!strcmp(line, "mdns_discovery"))
            c->mdns_discovery = !strcmp(eq+1,"true") || atoi(eq+1)!=0;
        else if (!strcmp(line,"user_profile")) {if(config_profile_name_valid(eq+1))snprintf(c->user_profile,sizeof(c->user_profile),"%s",eq+1);}
        else if (!strcmp(line,"launch_animation"))c->launch_animation=!strcmp(eq+1,"1") || !strcmp(eq+1,"true");
        else if (!strcmp(line,"launch_cover_ms")) {int v=atoi(eq+1);c->launch_cover_ms=v<0?0:v>10000?10000:v;}
        else if (!strcmp(line, "stream_profile")) {
            c->stream_profile=0;have_profile=true;
            for(int i=0;i<CONFIG_PROFILE_COUNT;i++)
                if(!strcmp(eq+1,config_profile_key(i)))c->stream_profile=i;
        }
        else if (!strcmp(line,"performance_overlay")) {
            static const char *names[]={"off","basic","detailed","developer"};
            c->overlay_mode=0;have_overlay_mode=true;
            for(int i=0;i<4;i++)if(!strcmp(eq+1,names[i]))c->overlay_mode=i;
        }
        else if (!strcmp(line, "adaptive_quality")) c->adaptive_quality=atoi(eq+1)==1;
        else if (!strcmp(line, "display_mode")) c->display_mode=atoi(eq+1);
        else if (!strcmp(line,"mouse_hud")) {int v=atoi(eq+1);c->mouse_hud=v>=0 && v<3?v:0;}
        else if (!strcmp(line,"stream_menu_style")) {int v=atoi(eq+1);c->stream_menu_style=v==1?1:0;}
        else if (!strcmp(line,"ui_motion"))c->ui_motion=!strcmp(eq+1,"1");
        else if (!strcmp(line,"ui_background")) {int v=atoi(eq+1);c->ui_background=v>=0 && v<3?v:0;}
        else if (!strcmp(line,"ui_accent")) {int v=atoi(eq+1);c->ui_accent=v>=0 && v<4?v:0;}
        else if (!strcmp(line, "language")) {
            int language=atoi(eq+1);
            c->language=language>=0 && language<CONFIG_LANGUAGE_COUNT?language:CONFIG_LANGUAGE_ENGLISH;
        }
        else if (!strcmp(line, "thread_layout")) c->thread_layout=atoi(eq+1)==1;
        else if (!strcmp(line, "decode_workers")) c->decode_workers=atoi(eq+1)==3?3:4;
        else if (!strcmp(line, "left_min")) {c->left_min=atoi(eq+1);have_left_min=true;}
        else if (!strcmp(line, "left_max")) c->left_max=atoi(eq+1);
        else if (!strcmp(line, "right_min")) {c->right_min=atoi(eq+1);have_right_min=true;}
        else if (!strcmp(line, "right_max")) c->right_max=atoi(eq+1);
        else if (!strcmp(line, "width"))
            c->width = atoi(eq + 1);
        else if (!strcmp(line, "height"))
            c->height = atoi(eq + 1);
        else if (!strcmp(line, "fps"))
            c->fps = atoi(eq + 1);
        else if (!strcmp(line, "bitrate")) {
            long rate = strtol(eq + 1, NULL, 10);
            c->bitrate_kbps = rate < CONFIG_BITRATE_MIN ? CONFIG_BITRATE_MIN :
                              rate > CONFIG_BITRATE_MAX ? CONFIG_BITRATE_MAX : (int)rate;
        }
        else if (!strcmp(line, "slices")) {
            long slices = strtol(eq + 1, NULL, 10);
            c->slice_count = slices == 8 || slices == 12 || slices == 16 ? (int)slices : 0;
        }
        else if (!strcmp(line, "packet_size"))
            c->packet_size = atoi(eq + 1);
        else if (!strcmp(line, "fast_decode"))
            c->fast_decode = atoi(eq + 1) != 0;
        else if (!strcmp(line, "renderer")) {
            if (!strcmp(eq + 1, "cpu"))
                c->renderer = 1;
            else if (!strcmp(eq + 1, "cpu_hwcolor"))
                c->renderer = 2;
            else
                c->renderer = 0;
        }
        else if (!strcmp(line, "vsync"))
            c->vsync = atoi(eq + 1) != 0;
        else if (!strcmp(line, "sops"))
            c->sops = atoi(eq + 1) != 0;
        else if (!strcmp(line, "quit_combo"))
            c->quit_combo = atoi(eq + 1) == 1 ? 1 : 0;
        else if (!strcmp(line, "stick_deadzone")) {
            int v = atoi(eq + 1);
            c->stick_deadzone = v < 0 ? 0 : v > 30 ? 30 : v;
        }
        else if (!strcmp(line, "session_report"))
            c->session_report = atoi(eq + 1) != 0;
        else if (!strcmp(line, "stats_overlay"))
            c->stats_overlay = atoi(eq + 1) != 0;
        else if (!strcmp(line, "overlay_mode")) {
            int mode=atoi(eq+1);c->overlay_mode=mode>=0 && mode<=3?mode:0;have_overlay_mode=true;
        }
        else if (!strcmp(line, "image_quality")) {
            int mode=atoi(eq+1);c->image_quality=mode>=0 && mode<=2?mode:0;
        }
        else if (!strcmp(line, "history_enabled"))
            c->history_enabled=atoi(eq+1)!=0;
        else if (!strcmp(line, "diagnostic_log"))
            c->diagnostic_log = atoi(eq + 1) != 0;
        else if (!strcmp(line,"mouse_speed")){int v=atoi(eq+1);c->mouse_speed=v<25?25:v>200?200:v;}
        else if (!strcmp(line,"mouse_acceleration"))c->mouse_acceleration=atoi(eq+1)!=0;
        else if (!strcmp(line,"mouse_scroll_ms")){int v=atoi(eq+1);c->mouse_scroll_ms=v<50?50:v>400?400:v;}
        else if (!strcmp(line, "audio_mode")) c->audio_mode=atoi(eq+1)==1?1:0;
        else if (!strcmp(line, "local_audio"))
            c->local_audio = atoi(eq + 1) != 0;
    }
    free(text);
    if(!have_left_min)c->left_min=c->stick_deadzone;
    if(!have_right_min)c->right_min=c->stick_deadzone;
    if(c->display_mode<0 || c->display_mode>3)c->display_mode=0;
    c->left_min=c->left_min<0?0:c->left_min>30?30:c->left_min;
    c->right_min=c->right_min<0?0:c->right_min>30?30:c->right_min;
    c->left_max=c->left_max<50?50:c->left_max>100?100:c->left_max;
    c->right_max=c->right_max<50?50:c->right_max>100?100:c->right_max;
    if(!have_overlay_mode)c->overlay_mode=c->stats_overlay?3:0;
    c->stats_overlay=c->overlay_mode!=0;
    if (!config_fps_supported(c->fps)) c->fps = CONFIG_FPS_DEFAULT;
    if (!config_slices_supported(c->slice_count)) c->slice_count = 0;
    if(!have_profile && !c->user_profile[0]) {
        c->stream_profile=0;
        for(int i=1;i<CONFIG_PROFILE_COUNT;i++) {
            struct config test=*c;config_apply_preset(&test,i);
            if(test.width==c->width && test.height==c->height && test.fps==c->fps &&
               test.bitrate_kbps==c->bitrate_kbps)c->stream_profile=i;
        }
    }
    c->stream_profile=config_video_preset(c);
}

size_t config_serialize(const struct config *c,char *buf,size_t capacity)
{
    int n = snprintf(buf, capacity,
        "# Moonlight for Xbox 360\n"
        "host=%s\nhost_uuid=%s\n"
        "width=%d\n"
        "height=%d\n"
        "fps=%d\n"
        "# kbps\n"
        "bitrate=%d\n"
        "packet_size=%d\n"
        "# slices: 0 Auto, 8, 12, 16 (next connection only)\n"
        "slices=%d\n"
        "# 1 = skip H.264 deblocking (much faster, slightly blockier)\n"
        "fast_decode=%d\n"
        "# gpu (default), cpu, or cpu_hwcolor (experimental)\n"
        "renderer=%s\n"
        "# GPU renderer: wait for vertical blank before showing a frame (no tearing)\n"
        "vsync=%d\n"
        "sops=%d\n"
        "local_audio=%d\naudio_mode=%d\nmouse_speed=%d\nmouse_acceleration=%d\nmouse_scroll_ms=%d\n"
        "quit_combo=%d\n"
        "stick_deadzone=%d\n"
        "session_report=%d\n"
        "stats_overlay=%d\n"
        "diagnostic_log=%d\n"
        "overlay_mode=%d\nimage_quality=%d\nhistory_enabled=%d\n"
        "user_profile=%s\nlaunch_cover_ms=%d\nlaunch_animation=%d\nstream_profile=%s\nmdns_discovery=%s\nperformance_overlay=%s\n"
        "adaptive_quality=%d\ndisplay_mode=%d\nlanguage=%d\nthread_layout=%d\ndecode_workers=%d\n"
        "left_min=%d\nleft_max=%d\nright_min=%d\nright_max=%d\nui_background=%d\nui_accent=%d\nui_motion=%d\nmouse_hud=%d\nstream_menu_style=%d\n",
        c->host,c->host_uuid, c->width, c->height, c->fps, c->bitrate_kbps, c->packet_size, c->slice_count,
        c->fast_decode, c->renderer == 1 ? "cpu" : c->renderer == 2 ? "cpu_hwcolor" : "gpu",
        c->vsync, c->sops, c->local_audio, c->audio_mode,c->mouse_speed,c->mouse_acceleration,c->mouse_scroll_ms, c->quit_combo, c->stick_deadzone, c->session_report,
        config_overlay_mode(c)!=0, c->diagnostic_log,config_overlay_mode(c),c->image_quality,c->history_enabled,
        c->user_profile,c->launch_cover_ms,c->launch_animation,config_profile_key(config_video_preset(c)),c->mdns_discovery?"true":"false",
        config_overlay_mode(c)==0?"off":config_overlay_mode(c)==1?"basic":config_overlay_mode(c)==2?"detailed":"developer",
        c->adaptive_quality,c->display_mode,c->language,c->thread_layout,c->decode_workers,
        c->left_min,c->left_max,c->right_min,c->right_max,c->ui_background,c->ui_accent,c->ui_motion,c->mouse_hud,c->stream_menu_style);
    return n >= 0 && (size_t)n < capacity ? (size_t)n : 0;
}
bool config_save(const struct config *c)
{
    char buf[2048];size_t n=config_serialize(c,buf,sizeof(buf));
    return n && storage_write("moonlight.ini",buf,n);
}

bool config_reset_preferences(struct config *c)
{
    struct config next;config_defaults(&next);
    memcpy(next.host,c->host,sizeof(next.host));memcpy(next.host_uuid,c->host_uuid,sizeof(next.host_uuid));
    if(!config_save(&next))return false;
    *c=next;return true;
}

/* All fields are prepared before publication by the menu thread. No live session reads cfg. */
void config_apply_preset(struct config *c, int preset)
{
    static const int modes[CONFIG_PROFILE_COUNT][4]={
        {0,0,0,0},{1280,720,60,12000},{854,480,60,25000},
        {1280,720,60,35000},{1280,720,30,12000},{1920,1080,24,8000}
    };
    if(preset<0 || preset>=CONFIG_PROFILE_COUNT)return;
    if(!preset){config_customize_stream(c);return;}
    struct config next=*c;
    next.stream_profile=preset;next.user_profile[0]=0;
    if(preset) {
        next.width=modes[preset][0];next.height=modes[preset][1];
        next.fps=modes[preset][2];next.bitrate_kbps=modes[preset][3];
    }
    *c=next;
}
int config_video_preset(const struct config *c)
{
    if(c->user_profile[0] || c->stream_profile<=0 || c->stream_profile>=CONFIG_PROFILE_COUNT)return 0;
    struct config test=*c;config_apply_preset(&test,c->stream_profile);
    return c->width==test.width && c->height==test.height && c->fps==test.fps &&
        c->bitrate_kbps==test.bitrate_kbps?c->stream_profile:0;
}
const char *config_preset_name(int preset)
{
    static const char *names[]={"Custom","Smooth","Performance","Maximum Quality","Standard","1080p (Experimental)"};
    return names[preset>=0 && preset<CONFIG_PROFILE_COUNT?preset:0];
}
const char *config_profile_key(int preset)
{
    static const char *names[]={"custom","smooth","performance","maximum_quality","standard","1080_smooth"};
    return names[preset>=0 && preset<CONFIG_PROFILE_COUNT?preset:0];
}

int config_step_bitrate(int bitrate, int direction)
{
    int step;
    if (bitrate < CONFIG_BITRATE_MIN) bitrate = CONFIG_BITRATE_MIN;
    if (bitrate > CONFIG_BITRATE_MAX) bitrate = CONFIG_BITRATE_MAX;
    step = (direction < 0 ? bitrate > 30000 : bitrate >= 30000) ? 5000 : 1000;
    bitrate += direction < 0 ? -step : step;
    if (direction < 0 && step == 5000 && bitrate < 30000) bitrate = 30000;
    return bitrate < CONFIG_BITRATE_MIN ? CONFIG_BITRATE_MIN :
           bitrate > CONFIG_BITRATE_MAX ? CONFIG_BITRATE_MAX : bitrate;
}

bool config_slices_supported(int slices)
{
    return slices == 0 || slices == 8 || slices == 12 || slices == 16;
}
int config_cycle_slices(int slices, int direction)
{
    static const int values[] = { 0, 8, 12, 16 };
    int i;
    for (i = 0; i < 4; i++)
        if (values[i] == slices) return values[(i + (direction < 0 ? 3 : 1)) % 4];
    return 0;
}

static const int frame_rates[] = { 24, 30, 40, 50, 60 };
bool config_fps_supported(int fps)
{
    int i;
    for (i = 0; i < 5; i++) if (frame_rates[i] == fps) return true;
    return false;
}
int config_cycle_fps(int fps, int direction)
{
    int i;
    for (i = 0; i < 5; i++)
        if (frame_rates[i] == fps)
            return frame_rates[(i + (direction < 0 ? -1 : 1) + 5) % 5];
    return CONFIG_FPS_DEFAULT;
}

int config_test_bitrate(const struct config *c,int direction)
{
    static const int hd[]={10000,15000,18000,25000};
    static const int fullhd[]={12000,16000,20000,25000};
    const int *rates=c->width>=1920 && c->height>=1080?fullhd:hd;
    int i;
    for(i=0;i<4;i++) if(rates[i]==c->bitrate_kbps) return rates[(i+(direction<0?3:1))%4];
    return rates[direction<0?3:0];
}

int config_overlay_mode(const struct config *c)
{
    if(!c->stats_overlay)return 0;
    return c->overlay_mode>=1 && c->overlay_mode<=3?c->overlay_mode:3;
}


/* Alternating, checksummed snapshots: a failed/short write never destroys the
 * other generation. Each file is bounded to 8 KiB, eight profiles, 63 UTF-8 bytes
 * per name. Unknown versions are read-only; moonlight.ini is never migrated. */
bool config_profile_name_valid(const char *name)
{
    if(!name || !*name)return false;
    size_t n=strlen(name);if(n>=CONFIG_PROFILE_NAME_BYTES || name[0]==' ' || name[n-1]==' ')return false;
    const unsigned char *p=(const unsigned char*)name;
    while(*p) {
        unsigned c=*p++;if(c<32 || c==127)return false;
        if(c<128)continue;
        int extra;unsigned cp;
        if(c>=0xC2 && c<=0xDF){extra=1;cp=c&31;}
        else if(c>=0xE0 && c<=0xEF){extra=2;cp=c&15;}
        else if(c>=0xF0 && c<=0xF4){extra=3;cp=c&7;}else return false;
        for(int i=0;i<extra;i++){if((*p&0xC0)!=0x80)return false;cp=(cp<<6)|(*p++&63);}
        if((extra==2 && cp<0x800)||(extra==3 && cp<0x10000)||cp>0x10FFFF || (cp>=0xD800 && cp<=0xDFFF) || cp==0x85 || cp==0x2028 || cp==0x2029)return false;
    }
    return true;
}
static bool profile_options_valid(const struct stream_options *o)
{
    return ((o->width==854 && o->height==480)||(o->width==1280 && o->height==720)||(o->width==1920 && o->height==1080)) &&
        config_fps_supported(o->fps) && o->bitrate_kbps>=CONFIG_BITRATE_MIN && o->bitrate_kbps<=CONFIG_BITRATE_MAX &&
        o->packet_size>=512 && o->packet_size<=1400 && config_slices_supported(o->slice_count) &&
        o->fast_decode>=0 && o->fast_decode<=1 && o->renderer>=0 && o->renderer<=2 &&
        o->vsync>=0 && o->vsync<=1 && o->sops>=0 && o->sops<=1 && o->local_audio>=0 && o->local_audio<=1 &&
        o->image_quality>=0 && o->image_quality<=2 && o->adaptive_quality>=0 && o->adaptive_quality<=1 &&
        o->thread_layout>=0 && o->thread_layout<=1 && (o->decode_workers==3 || o->decode_workers==4) && o->audio_mode>=0 && o->audio_mode<=1;
}
void config_profile_capture(struct stream_options *o,const struct config *c)
{
    int p=config_video_preset(c);
    bool fast=p==CONFIG_PROFILE_SMOOTH || p==CONFIG_PROFILE_1080_SMOOTH || p==CONFIG_PROFILE_PERFORMANCE;
    o->width=c->width;o->height=c->height;o->fps=c->fps;o->bitrate_kbps=c->bitrate_kbps;
    o->packet_size=c->packet_size;o->slice_count=c->slice_count;
    o->fast_decode=fast || c->fast_decode;o->renderer=c->renderer;o->vsync=c->vsync;
    o->sops=c->sops;o->local_audio=c->local_audio;o->image_quality=fast?1:c->image_quality;
    o->adaptive_quality=c->adaptive_quality;o->thread_layout=c->thread_layout;o->decode_workers=c->decode_workers;o->audio_mode=c->audio_mode;
}
void config_customize_stream(struct config *c)
{
    struct stream_options effective;
    config_profile_capture(&effective,c);
    c->fast_decode=effective.fast_decode!=0;
    c->image_quality=effective.image_quality;
    c->stream_profile=0;c->user_profile[0]=0;
}
static unsigned profiles_checksum(const char *p,size_t n)
{
    unsigned h=2166136261u;for(size_t i=0;i<n;i++)h=(h^(unsigned char)p[i])*16777619u;return h;
}
static bool profiles_valid(const struct user_profiles *p)
{
    if(p->count<0 || p->count>CONFIG_USER_PROFILE_MAX)return false;
    for(int i=0;i<p->count;i++) {
        if(!memchr(p->items[i].name,0,CONFIG_PROFILE_NAME_BYTES) || !config_profile_name_valid(p->items[i].name) || !profile_options_valid(&p->items[i].options))return false;
        for(int j=0;j<i;j++)if(!strcmp(p->items[i].name,p->items[j].name))return false;
    }return true;
}
size_t config_profiles_serialize(const struct user_profiles *p,char *text,size_t cap)
{
    if(!profiles_valid(p))return 0;
    int n=snprintf(text,cap,"XENON_PROFILES 2 %u %d\n",p->generation,p->count);
    if(n<0 || (size_t)n>=cap)return 0;size_t used=(size_t)n;
    for(int i=0;i<p->count;i++) {
        const struct user_stream_profile *r=p->items+i;const struct stream_options *o=&r->options;
        n=snprintf(text+used,cap-used,"%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",r->name,
            o->width,o->height,o->fps,o->bitrate_kbps,o->packet_size,o->slice_count,o->fast_decode,o->renderer,o->vsync,o->sops,o->local_audio,o->image_quality,o->adaptive_quality,o->thread_layout,o->decode_workers,o->audio_mode);
        if(n<0 || (size_t)n>=cap-used)return 0;used+=(size_t)n;
    }
    n=snprintf(text+used,cap-used,"CHECKSUM %08x\n",profiles_checksum(text,used));
    return n>0 && (size_t)n<cap-used?used+(size_t)n:0;
}
/* Strict decimal parsing avoids scanf integer overflow on damaged files. */
static bool profiles_number(const char **cursor,unsigned limit,char delimiter,unsigned *out)
{
    const char *p=*cursor;unsigned n=0;if(*p<'0' || *p>'9')return false;
    while(*p>='0' && *p<='9') {unsigned digit=(unsigned)(*p++-'0');if(n>limit/10 || (n==limit/10 && digit>limit%10))return false;n=n*10+digit;}
    if(*p!=delimiter)return false;*cursor=p+1;*out=n;return true;
}
static bool profiles_decode(struct user_profiles *out,const char *text,size_t bytes)
{
    if(!text || !bytes || bytes>=CONFIG_PROFILES_BYTES || memchr(text,0,bytes) || (strncmp(text,"XENON_PROFILES 1 ",17) && strncmp(text,"XENON_PROFILES 2 ",17)))return false;
    bool v2=text[15]=='2';
    struct user_profiles p;memset(&p,0,sizeof(p));const char *line=text+17;unsigned count;
    if(!profiles_number(&line,0xffffffffu,' ',&p.generation) || !profiles_number(&line,CONFIG_USER_PROFILE_MAX,'\n',&count))return false;
    p.count=(int)count;
    for(int i=0;i<p.count;i++) {
        const char *tab=strchr(line,'\t');if(!tab || tab==line || tab-line>=CONFIG_PROFILE_NAME_BYTES)return false;
        struct user_stream_profile *r=p.items+i;memcpy(r->name,line,tab-line);r->name[tab-line]=0;line=tab+1;
        struct stream_options *o=&r->options;
        int *fields[]={&o->width,&o->height,&o->fps,&o->bitrate_kbps,&o->packet_size,&o->slice_count,&o->fast_decode,&o->renderer,&o->vsync,&o->sops,&o->local_audio,&o->image_quality,&o->adaptive_quality,&o->thread_layout,&o->decode_workers,&o->audio_mode};
        for(int j=0;j<(v2?16:15);j++) {unsigned n;if(!profiles_number(&line,0x7fffffffu,j==(v2?15:14)?'\n':'\t',&n))return false;*fields[j]=(int)n;}
    }
    if(strncmp(line,"CHECKSUM ",9) || (size_t)(line-text)+18!=bytes)return false;
    unsigned checksum=0;
    for(int i=0;i<8;i++) {char c=line[9+i];unsigned digit;
        if(c>='0' && c<='9')digit=c-'0';else if(c>='a' && c<='f')digit=c-'a'+10;else return false;
        checksum=(checksum<<4)|digit;
    }
    if(line[17]!='\n' || checksum!=profiles_checksum(text,line-text) || !profiles_valid(&p))return false;
    *out=p;return true;
}
bool config_profiles_load(struct user_profiles *p)
{
    const char *names[]={"profiles-a.tsv","profiles-b.tsv"};struct user_profiles candidate,best;
    bool found=false,unknown=false;memset(&best,0,sizeof(best));best.slot=-1;
    for(int i=0;i<2;i++) {
        size_t n=0;errno=0;char *text=storage_read(names[i],&n);
        if(!text && errno!=ENOENT)unknown=true;
        if(text && !strncmp(text,"XENON_PROFILES ",15) && strncmp(text,"XENON_PROFILES 1 ",17) && strncmp(text,"XENON_PROFILES 2 ",17))unknown=true;
        if(profiles_decode(&candidate,text,n) && (!found || candidate.generation>best.generation)) {best=candidate;best.slot=i;found=true;}
        free(text);
    }
    if(found){best.read_only=unknown;*p=best;}else if(unknown)p->read_only=true;
    return found && !unknown;
}
bool config_profiles_save(struct user_profiles *current,const struct user_profiles *next)
{
    if(current->read_only || current->generation==0xffffffffu)return false;
    struct user_profiles copy=*next;copy.generation=current->generation+1;copy.slot=current->slot==0?1:0;copy.read_only=false;
    char text[CONFIG_PROFILES_BYTES];size_t n=config_profiles_serialize(&copy,text,sizeof(text));if(!n)return false;
    const char *name=copy.slot?"profiles-b.tsv":"profiles-a.tsv";
    if(!storage_write(name,text,n))return false;
    size_t read_n=0;char *check=storage_read(name,&read_n);
    bool ok=check && read_n==n && !memcmp(check,text,n);free(check);
    if(ok)*current=copy;return ok;
}
bool config_profiles_create(struct user_profiles *p,const struct config *c,const char *name)
{
    if(!config_profile_name_valid(name) || p->count<0 || p->count>=CONFIG_USER_PROFILE_MAX)return false;
    struct user_profiles next=*p;struct user_stream_profile *r=next.items+next.count++;
    strcpy(r->name,name);config_profile_capture(&r->options,c);return config_profiles_save(p,&next);
}
bool config_profiles_rename(struct user_profiles *p,int index,const char *name)
{
    if(index<0 || index>=p->count || p->count>CONFIG_USER_PROFILE_MAX || !config_profile_name_valid(name))return false;
    struct user_profiles next=*p;strcpy(next.items[index].name,name);return config_profiles_save(p,&next);
}
bool config_profiles_delete(struct user_profiles *p,int index)
{
    if(index<0 || index>=p->count || p->count>CONFIG_USER_PROFILE_MAX)return false;struct user_profiles next=*p;
    memmove(next.items+index,next.items+index+1,(next.count-index-1)*sizeof(next.items[0]));next.count--;
    return config_profiles_save(p,&next);
}
static bool profile_entry_valid(const struct user_profiles *p,int index)
{
    return p && p->count>=0 && p->count<=CONFIG_USER_PROFILE_MAX && index>=0 && index<p->count &&
        memchr(p->items[index].name,0,CONFIG_PROFILE_NAME_BYTES) &&
        config_profile_name_valid(p->items[index].name) && profile_options_valid(&p->items[index].options);
}
bool config_profiles_apply(const struct user_profiles *p,int index,struct config *c)
{
    if(!profile_entry_valid(p,index))return false;
    const struct stream_options *o=&p->items[index].options;
    c->width=o->width;c->height=o->height;c->fps=o->fps;c->bitrate_kbps=o->bitrate_kbps;
    c->packet_size=o->packet_size;c->slice_count=o->slice_count;c->fast_decode=o->fast_decode!=0;
    c->renderer=o->renderer;c->vsync=o->vsync!=0;c->sops=o->sops!=0;c->local_audio=o->local_audio!=0;
    c->image_quality=o->image_quality;c->adaptive_quality=o->adaptive_quality;
    c->thread_layout=o->thread_layout;c->decode_workers=o->decode_workers;c->audio_mode=o->audio_mode;c->stream_profile=0;strcpy(c->user_profile,p->items[index].name);return true;
}

int config_profiles_selected(const struct user_profiles *p,const struct config *c)
{
    if(!p || p->count<0 || p->count>CONFIG_USER_PROFILE_MAX)return -1;
    struct stream_options effective;config_profile_capture(&effective,c);
    for(int i=0;i<p->count;i++)if(profile_entry_valid(p,i) && !strcmp(c->user_profile,p->items[i].name) &&
        !memcmp(&effective,&p->items[i].options,sizeof(effective)))return i;
    for(int i=0;i<p->count;i++)if(profile_entry_valid(p,i) &&
        !memcmp(&effective,&p->items[i].options,sizeof(effective)))return i;
    return -1;
}

int config_match_factory(const struct config *c)
{
    struct stream_options active;config_profile_capture(&active,c);
    for(int i=1;i<CONFIG_PROFILE_COUNT;i++) {
        struct config factory;struct stream_options known;config_defaults(&factory);
        config_apply_preset(&factory,i);config_profile_capture(&known,&factory);
        if(!memcmp(&active,&known,sizeof(active)))return i;
    }
    return 0;
}
void config_select_factory(struct config *c,int preset)
{
    if(preset<=0 || preset>=CONFIG_PROFILE_COUNT)return;
    struct config factory;config_defaults(&factory);config_apply_preset(&factory,preset);
    struct user_profiles p;memset(&p,0,sizeof(p));p.count=1;strcpy(p.items[0].name,"Factory");
    config_profile_capture(&p.items[0].options,&factory);
    config_profiles_apply(&p,0,c);c->user_profile[0]=0;c->stream_profile=preset;
}

bool config_cycle_profile(const struct user_profiles *p,struct config *c,int direction)
{
    int choices[CONFIG_PROFILE_COUNT+CONFIG_USER_PROFILE_MAX],count=0;
    for(int i=1;i<CONFIG_PROFILE_COUNT;i++)choices[count++]=i;
    if(p && !p->read_only && p->count>=0 && p->count<=CONFIG_USER_PROFILE_MAX)
        for(int i=0;i<p->count;i++)if(profile_entry_valid(p,i))choices[count++]=CONFIG_PROFILE_COUNT+i;
    int personal=config_profiles_selected(p,c);
    int current=personal>=0?CONFIG_PROFILE_COUNT+personal:config_match_factory(c),position=direction<0?0:count-1;
    for(int i=0;i<count;i++)if(choices[i]==current){position=i;break;}
    int choice=choices[(position+(direction<0?count-1:1))%count];
    if(choice<CONFIG_PROFILE_COUNT){config_select_factory(c,choice);return true;}
    return config_profiles_apply(p,choice-CONFIG_PROFILE_COUNT,c);
}
