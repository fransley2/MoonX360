/*
 * Moonlight's UI flow: splash, client identity, main menu, settings, pairing,
 * app grid with box art, and the streaming session. Shared by the libxenon
 * build and the Xbox 360 SDK build; platform specifics come from platform.h.
 *
 * Kept valid as both C99 and C++ (the SDK build compiles it as C++): explicit
 * casts from void*, no designated initialisers or compound literals.
 *
 * Slow operations (network requests, pairing, key generation) run in a
 * background thread while the UI keeps a spinner moving.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>
#include "session_history.h"
#include "library_state.h"

#include <Limelight.h>

#include "platform.h"
#include "audio.h"
#include "config.h"
#include "app_list_cache.h"
#include "performance.h"
#include "crypto.h"
#include "gamestream.h"
#include "input.h"
#include "settings_layout.h"
#include "adaptive_stream.h"
#include "stick_calibration.h"
#include "mdns.h"
#include "host_cache.h"
#include "product_features.h"
#include "wake.h"
#include "home_art.h"
#include "video.h"
#include "ui/ui.h"
#include "ui/draw.h"
#include "ui/cover_cache.h"

#define MAX_APPS 64

static struct config cfg;
static struct client_identity identity;
static struct gs_server server;
static bool have_identity;
static bool server_ok;
static char server_error[200];

/* App list of the current host, kept for names and box art. */
static struct gs_app apps[MAX_APPS];
static uint32_t *app_art[MAX_APPS];
static uint32_t art_generation[MAX_APPS];
static int app_count;
static struct app_list_cache app_cache;

static struct session_history session_history;
static struct library_favorites favorites;
static bool history_save_failed;
static void history_menu(void);
static void advisor_menu(void);
static int settings_initial_group=-1;
static int navigation_pending=-1;
/* Active only while settings own the controller; nested dialogs unwind on tab changes. */
static int settings_navigation_page=-1;
static void host_options_menu(void);
static void *quit_task(void *arg);
static void hosts_menu(int page);
static void manage_host(void);
static const char *host_key(void);
static int selected_host_index(void);
static const char *host_alias(void);
static const char *game_profile_label(int id);
static void game_profile_menu(int id);
static bool wake_host(void);
static struct home_art_cache home_art;
static void ui_memory_menu(void);
static void network_diagnostics_menu(void);
static bool tab_key(int key,int page)
{
    if(!(key&(LB_FLAG|RB_FLAG)))return false;
    navigation_pending=ui_tab_move(page,(key&LB_FLAG)?-1:1);return true;
}

/* ------------------------------------------------------------ input */

static int wait_button(void)
{
    int b;
    if(settings_navigation_page>=0 && navigation_pending>=0)return B_FLAG;

    while ((b = input_menu_pressed()) == 0)
        plat_sleep_ms(16);
    if(settings_navigation_page>=0 && tab_key(b,settings_navigation_page))return B_FLAG;
    return b;
}

/* ------------------------------------------------------------ background tasks */

struct task {
    void *(*fn)(void *);
    void *arg;
    void *ret;
    volatile int done;
    uint32_t started_ms,finished_ms;
};

static void *task_entry(void *p)
{
    struct task *t = (struct task *)p;

    t->started_ms=plat_now_ms();t->ret = t->fn(t->arg);t->finished_ms=plat_now_ms();
    plat_atomic_store(&t->done,1);
    return NULL;
}

struct busy_view {
    const char *title;
    char message[200];
};

/* Set once video owns the screen, so the busy screen stops drawing. */
static volatile bool ui_suspended;
static bool task_start_failed;
static void message(int icon,const char *title,const char *body);

/* Runs fn(arg) in a background thread and animates draw(ctx, t) until it finishes. */
static void *run_task(void *(*fn)(void *), void *arg, void (*draw)(void *ctx, uint32_t t), void *ctx)
{
    struct task t;
    plat_thread *th;
    uint32_t begun=plat_now_ms(),next_draw=begun+40;

    task_start_failed=false;
    t.fn = fn;
    t.arg = arg;
    t.ret = NULL;
    t.done = false;
    th = plat_thread_start(task_entry, &t);
    if (th == NULL) {
        task_start_failed=true;
        message(ICON_ERROR,"Background task unavailable","The operation was not started. Free resources and try again.");
        return NULL;
    }
    while (!plat_atomic_load(&t.done)) {
        uint32_t now=plat_now_ms();
        if (!ui_suspended && (int32_t)(now-next_draw)>=0) {
            draw(ctx, now);
            ui_present();
            next_draw=now+40;
        }
        if(!plat_atomic_load(&t.done))plat_sleep_ms(4);
    }
    plat_thread_join(th);
    uint32_t total=plat_now_ms()-begun,worker=t.finished_ms-t.started_ms;
    char timing[144];snprintf(timing,sizeof(timing),"[NET] run_task total: %u ms; worker %u ms; dispatch %u ms; return/UI %u ms\n",
        total,worker,t.started_ms-begun,total-(t.finished_ms-begun));plat_log(timing);
    return t.ret;
}

static void draw_busy(void *ctx, uint32_t t)
{
    struct busy_view *v = (struct busy_view *)ctx;
    ui_draw_busy(v->title, v->message[0] ? v->message : NULL, t);
}

static void *run_busy(const char *title, const char *message, void *(*fn)(void *), void *arg)
{
    struct busy_view v;

    v.title = title;
    v.message[0] = '\0';
    if (message)
        snprintf(v.message, sizeof(v.message), "%s", message);
    return run_task(fn, arg, draw_busy, &v);
}

/* ------------------------------------------------------------ messages */

static void message(int icon, const char *title, const char *body)
{
    ui_draw_message(icon, title, body);
    ui_present();
    while (!(wait_button() & (A_FLAG | B_FLAG)))
        ;
}

/* ------------------------------------------------------------ host state */

static const char *app_name(int id)
{
    int i;

    for (i = 0; i < app_count; i++)
        if (apps[i].id == id)
            return apps[i].name;
    for(i=0;i<session_history.count;i++)
        if(session_history.records[i].app_id==id && !strcmp(session_history.records[i].host,host_key()))
            return session_history.records[i].app;
    return NULL;
}

static struct ui_host host_view(void)
{
    static char detail[220], running[80];
    struct ui_host h;

    h.name = NULL;
    h.address = cfg.host;
    h.state = HOST_UNKNOWN;
    h.running_app = NULL;
    h.detail = NULL;
    if (!cfg.host[0])
        return h;
    if (!server_ok) {
        h.state = HOST_OFFLINE;
        h.detail = server_error[0] ? server_error : NULL;
        return h;
    }
    h.name = host_alias()?host_alias():server.hostname;
    h.state = server.paired ? HOST_PAIRED : HOST_UNPAIRED;
    if (server.current_game) {
        const char *n = app_name(server.current_game);
        snprintf(running, sizeof(running), "%s", n ? n : "A game or app");
        h.running_app = running;
    }
    snprintf(detail, sizeof(detail), "%s %s", server.is_nvidia ? "GeForce Experience" : "GameStream host",
             server.app_version);
    h.detail = detail;
    return h;
}

static unsigned short host_port(const char *ip);
static void remember_server(const struct gs_server *srv);
static void host_offline(const char *ip);

static void *refresh_task(void *arg)
{
    uint32_t begun=plat_now_ms();plat_log("[NET] server_info start\n");
    (void)arg;
    gs_server_free(&server);
    memset(&server, 0, sizeof(server));
    server_ok = gs_server_info_port(&server, cfg.host,host_port(cfg.host),0);
    bool identity_changed=server_ok && cfg.host_uuid[0] && !wake_identity(cfg.host_uuid,server.uuid);
    if(identity_changed)server_ok=false;
    if(server_ok)remember_server(&server);
    char timing[96];snprintf(timing,sizeof(timing),"[NET] server_info done %u ms (ok=%d)\n",plat_now_ms()-begun,server_ok);plat_log(timing);
    if (!server_ok) {
        host_offline(cfg.host);
        snprintf(server_error, sizeof(server_error), "%s",identity_changed?"The PC at this IP has a different identity. Choose the intended PC again in Hosts.":gs_error());
    }
    return NULL;
}

static void refresh_server(void)
{
    char title[96];

    if (!cfg.host[0]) {
        server_ok = false;
        return;
    }
    snprintf(title, sizeof(title), ui_translate("Connecting to %s"), cfg.host);
    server_ok=false;server_error[0]=0;
    run_busy(title, "Checking the host's status...", refresh_task, NULL);
    if(task_start_failed)snprintf(server_error,sizeof(server_error),"%s",ui_translate("Background task unavailable"));
}

/* Menu-only discovery. A single worker validates candidates; UI owns all cache/socket writes.
 * Leaving Home drains the worker with a responsive screen before ANY foreground gs_* call.
 * Streaming and Library therefore have no discovery worker, UDP polling or cache writes. */
static struct host_cache hosts;
static struct game_bindings game_bindings;
static char library_query[CONFIG_PROFILE_NAME_BYTES];
static int selected_host_index(void)
{
    for(int i=0;i<hosts.count;i++)if(cfg.host_uuid[0]?wake_identity(cfg.host_uuid,hosts.hosts[i].uuid):!strcmp(hosts.hosts[i].ip,cfg.host))return i;
    return -1;
}
static const char *host_alias(void)
{int i=selected_host_index();return i>=0 && hosts.hosts[i].alias[0]?hosts.hosts[i].alias:NULL;}
static const char *game_profile_label(int id)
{
    static char label[160];int index=binding_find(&game_bindings,host_key(),id);
    if(index<0)return config_preset_name(config_video_preset(&cfg));
    const char *token=game_bindings.items[index].profile;
    const char *name=!strncmp(token,"user:",5)?token+5:ui_translate(config_preset_name(atoi(token+8)));
    snprintf(label,sizeof(label),ui_translate("Game profile: %s"),name);return label;
}
static const char *host_key(void)
{
    static char key[96];const char *uuid=cfg.host_uuid;
    int index=selected_host_index();if(!*uuid && index>=0)uuid=hosts.hosts[index].uuid;
    if(*uuid){snprintf(key,sizeof(key),"uuid:%s",uuid);for(char *p=key;*p;p++)if(*p>='A'&&*p<='Z')*p+='a'-'A';return key;}
    return cfg.host;
}
static const char *host_label(const char *key)
{
    for(int i=0;i<hosts.count;i++) {
        const struct cached_host *h=hosts.hosts+i;
        if(!strcmp(key,h->ip) || (!strncmp(key,"uuid:",5) && h->uuid[0] && wake_identity(h->uuid,key+5)))return h->alias[0]?h->alias:h->name[0]?h->name:h->ip;
    }
    return key;
}
static void migrate_host_records(const char *ip,const char *key)
{
    if(!strcmp(ip,key))return;bool history_changed=false,favorites_changed=false,bindings_changed=false;
    for(int i=0;i<session_history.count;i++)if(!strcmp(session_history.records[i].host,ip)){history_text(session_history.records[i].host,96,key);history_changed=true;}
    for(int i=0;i<favorites.count;i++)if(!strcmp(favorites.entries[i].host,ip)) {
        if(library_favorite_has(&favorites,key,favorites.entries[i].id)){memmove(favorites.entries+i,favorites.entries+i+1,(--favorites.count-i)*sizeof(favorites.entries[0]));i--;}
        else history_text(favorites.entries[i].host,96,key);favorites_changed=true;
    }
    for(int i=0;i<game_bindings.count;i++)if(!strcmp(game_bindings.items[i].host,ip)) {
        if(binding_find(&game_bindings,key,game_bindings.items[i].app)>=0){memmove(game_bindings.items+i,game_bindings.items+i+1,(--game_bindings.count-i)*sizeof(game_bindings.items[0]));i--;}
        else strcpy(game_bindings.items[i].host,key);bindings_changed=true;
    }
    if(history_changed){char text[SESSION_HISTORY_BYTES];size_t n=history_encode(&session_history,text,sizeof(text));if(n)storage_write("sessions.tsv",text,n);}
    if(favorites_changed){char text[8192];size_t n=library_favorites_encode(&favorites,text,sizeof(text));if(n)storage_write("favorites.tsv",text,n);}
    if(bindings_changed && !game_bindings.read_only){char text[8192];size_t n=bindings_encode(&game_bindings,text,sizeof(text));if(n)storage_write("game-bindings.tsv",text,n);}
}
static struct mdns_state dns;
static plat_thread *probe_thread;
static struct {
    struct gs_server result;
    char ip[64];unsigned short port;
    bool ok;volatile int done;
} probe;
static uint32_t next_discovery,next_resolve,next_socket_retry,next_dns_poll;
static int discovery_round,resolve_cursor;
static bool host_cache_save_failed,mdns_ready;
static struct {char ip[64];unsigned short port;uint32_t checked;bool used;} attempts[32];

static void host_offline(const char *ip)
{
    for(int i=0;i<hosts.count;i++)if(!strcmp(hosts.hosts[i].ip,ip)) {hosts.hosts[i].online=false;hosts.hosts[i].paired=false;}
}
static void save_hosts(void)
{
    char text[8192];size_t n=host_cache_encode(&hosts,text,sizeof(text));
    host_cache_save_failed=(!n && hosts.count!=0) || !storage_write("hosts.tsv",text,n);
}
static unsigned short host_port(const char *ip)
{
    int selected=selected_host_index();if(selected>=0 && !strcmp(ip,cfg.host))return hosts.hosts[selected].port;
    for(int i=0;i<hosts.count;i++)if(!strcmp(hosts.hosts[i].ip,ip))return hosts.hosts[i].port;
    return 47989;
}
static void *probe_task(void *arg)
{
    (void)arg;
    probe.ok=gs_server_info_port(&probe.result,probe.ip,probe.port,1200);
    plat_atomic_store(&probe.done,1);return NULL;
}
static void remember_server(const struct gs_server *srv)
{
    struct cached_host h;memset(&h,0,sizeof(h));
    snprintf(h.uuid,sizeof(h.uuid),"%s",srv->uuid);snprintf(h.name,sizeof(h.name),"%s",srv->hostname);
    snprintf(h.ip,sizeof(h.ip),"%s",srv->address);h.port=srv->http_port;h.online=true;h.paired=srv->paired;
    snprintf(h.mac,sizeof(h.mac),"%s",srv->mac);
    char previous[64]="";
    for(int i=0;i<hosts.count;i++)if(h.uuid[0] && hosts.hosts[i].uuid[0] && wake_identity(hosts.hosts[i].uuid,h.uuid))snprintf(previous,sizeof(previous),"%s",hosts.hosts[i].ip);
    int index=host_cache_upsert(&hosts,&h);
    if(index>=0 && h.uuid[0]) {
        char key[96];snprintf(key,sizeof(key),"uuid:%s",h.uuid);for(char *p=key;*p;p++)if(*p>='A'&&*p<='Z')*p+='a'-'A';
        int same_address=0;for(int i=0;i<hosts.count;i++)if(!strcmp(hosts.hosts[i].ip,h.ip))same_address++;
        if(same_address<=1)migrate_host_records(h.ip,key);if(previous[0] && same_address<=1)migrate_host_records(previous,key);
        if(!cfg.host_uuid[0] && !strcmp(cfg.host,h.ip)){strcpy(cfg.host_uuid,h.uuid);config_save(&cfg);}
        if(previous[0] && !strcmp(cfg.host,previous) && strcmp(previous,h.ip) && wake_identity(cfg.host_uuid,h.uuid)){snprintf(cfg.host,sizeof(cfg.host),"%s",h.ip);config_save(&cfg);}
    }
    save_hosts();
}
static bool discovery_collect(void)
{
    if(!probe_thread || !plat_atomic_load(&probe.done))return false;
    plat_thread_join(probe_thread);probe_thread=NULL;
    if(probe.ok) {
        remember_server(&probe.result);
        if(!cfg.host[0]) {snprintf(cfg.host,sizeof(cfg.host),"%s",probe.ip);snprintf(cfg.host_uuid,sizeof(cfg.host_uuid),"%s",probe.result.uuid);config_save(&cfg);}
        if(!strcmp(cfg.host,probe.ip) && (!cfg.host_uuid[0] || wake_identity(cfg.host_uuid,probe.result.uuid))) {
            gs_server_free(&server);server=probe.result;memset(&probe.result,0,sizeof(probe.result));
            server.info.address=server.address;server.info.serverInfoAppVersion=server.app_version;
            server.info.serverInfoGfeVersion=server.gfe_version;server.info.rtspSessionUrl=server.rtsp_session_url;
            server.info_timeout_ms=0;server_ok=true;server_error[0]=0;
        } else if(!strcmp(cfg.host,probe.ip) && cfg.host_uuid[0]) {
            server_ok=false;snprintf(server_error,sizeof(server_error),"%s","The PC at this IP has a different identity. Choose the intended PC again in Hosts.");
        }
    } else {
        host_offline(probe.ip);
        if(!strcmp(cfg.host,probe.ip)) {server_ok=false;snprintf(server_error,sizeof(server_error),"Host offline");}
    }
    gs_server_free(&probe.result);return true;
}
static void discovery_pause(void)
{
    mdns_close();mdns_ready=false;
    uint32_t last=0;
    while(probe_thread && !plat_atomic_load(&probe.done)) {
        uint32_t now=plat_now_ms();
        if(now-last>=40) {ui_draw_busy("Checking host",NULL,now);ui_present();last=now;}
        plat_sleep_ms(4);
    }
    discovery_collect();
}
static bool probe_start(const char *ip,unsigned short port,uint32_t now)
{
    int slot=-1;
    for(int i=0;i<32;i++) {
        if(attempts[i].used && attempts[i].port==port && !strcmp(attempts[i].ip,ip)) {
            if(now-attempts[i].checked<60000)return false;
            slot=i;break;
        }
        if((!attempts[i].used || now-attempts[i].checked>=60000) && slot<0)slot=i;
    }
    if(slot<0)return false;
    snprintf(attempts[slot].ip,sizeof(attempts[slot].ip),"%s",ip);attempts[slot].port=port;attempts[slot].checked=now;attempts[slot].used=true;
    memset(&probe,0,sizeof(probe));snprintf(probe.ip,sizeof(probe.ip),"%s",ip);probe.port=port;
    probe_thread=plat_thread_start(probe_task,NULL);
    /* Never perform HTTP inline if thread creation failed. */
    return probe_thread!=NULL;
}
static bool discovery_tick(void)
{
    uint32_t now=plat_now_ms();bool changed=discovery_collect();
    if(cfg.mdns_discovery && have_identity) {
        if(!next_socket_retry || (int32_t)(now-next_socket_retry)>=0) {
            bool ready=mdns_open();
            if(ready && !mdns_ready){next_discovery=now;plat_log("[MDNS] multicast discovery ready\n");}
            if(!ready)plat_log("[MDNS] multicast unavailable; cached/manual hosts remain available\n");
            mdns_ready=ready;next_socket_retry=now+(ready?1000:30000);
        }
        if(!next_discovery || (int32_t)(now-next_discovery)>=0) {
            memset(&dns,0,sizeof(dns));mdns_send(MDNS_SERVICE,12);
            static const uint32_t intervals[]={2000,5000,15000,60000};
            next_discovery=now+intervals[discovery_round<3?discovery_round++:3];
        }
        if(!next_dns_poll || (int32_t)(now-next_dns_poll)>=0){mdns_poll(&dns);next_dns_poll=now+100;}
        if(!next_resolve || (int32_t)(now-next_resolve)>=0) {
            /* Resolve split packets without a burst of per-frame queries. */
            for(int step=0;step<MDNS_MAX;step++) {
                struct mdns_service *v=dns.services+(resolve_cursor++%MDNS_MAX);
                if(!v->ptr)continue;
                mdns_send(v->target[0]?v->target:v->instance,v->target[0]?1:33);break;
            }
            next_resolve=now+1000;
        }
    }
    if(probe_thread || !have_identity)return changed;
    /* Cached hosts are validated before newly discovered IPs. */
    for(int i=0;i<hosts.count;i++)if(probe_start(hosts.hosts[i].ip,hosts.hosts[i].port,now))return changed;
    if(cfg.mdns_discovery) {
        struct mdns_candidate candidates[MDNS_MAX];int n=mdns_candidates(&dns,candidates,MDNS_MAX);
        for(int i=0;i<n;i++)if(probe_start(candidates[i].ip,candidates[i].port,now))break;
    }
    return changed;
}

/* ------------------------------------------------------------ IP editor */

static bool edit_ip(char *ip, size_t len)
{
    int o[4] = { 192, 168, 1, 2 }, sel = 0;

    sscanf(ip, "%d.%d.%d.%d", &o[0], &o[1], &o[2], &o[3]);
    for (;;) {
        int b;

        ui_draw_ip_editor(o, sel);
        ui_present();
        b = wait_button();
        if (b & LEFT_FLAG) sel = (sel + 3) % 4;
        if (b & RIGHT_FLAG) sel = (sel + 1) % 4;
        if (b & UP_FLAG) o[sel] = (o[sel] + 1) & 255;
        if (b & DOWN_FLAG) o[sel] = (o[sel] + 255) & 255;
        if(tab_key(b,UI_HOME))return false;
        if (b & Y_FLAG) o[sel] = (o[sel] + 10) & 255;
        if (b & X_FLAG) o[sel] = (o[sel] + 246) & 255;
        if (b & B_FLAG)
            return false;
        if (b & A_FLAG) {
            snprintf(ip, len, "%d.%d.%d.%d", o[0], o[1], o[2], o[3]);
            return true;
        }
    }
}

/* ------------------------------------------------------------ settings */

static const int resolutions[][2] = { { 854, 480 }, { 1280, 720 }, { 1920, 1080 } };

static void log_viewer(void)
{
    char *text = (char *)malloc(16384);
    const char *lines[128];
    bool previous = false;
    int first = 0;
    if (!text) { message(ICON_ERROR, "Log unavailable", "Not enough memory for a log snapshot."); return; }
    for (;;) {
        int count = 0, b;
        char *p;
        plat_diagnostics_read(text, 16384, previous);
        p = text;
        while (*p && count < 128) {
            char *end = strchr(p, '\n');
            lines[count++] = p;
            if (!end) break;
            *end = '\0';
            p = end + 1;
        }
        if (first > count - 10) first = count > 10 ? count - 10 : 0;
        if (first < 0) first = 0;
        ui_draw_log(previous ? "Logs - previous run" : "Logs - current run", lines, count, first);
        ui_present();
        b = wait_button();
        if (b & B_FLAG) break;
        if (b & (LEFT_FLAG | RIGHT_FLAG)) { previous = !previous; first = 0; }
        if (b & UP_FLAG) first--;
        if (b & DOWN_FLAG) first++;
        if(tab_key(b,UI_SETTINGS))break;
        if (b & PLAY_FLAG) first += 10;
    }
    free(text);
}

static void calibrate_sticks(void)
{
    int sel=0,stick=0;
    for(;;) {
        struct pad pads[MAX_PADS];int mask=input_poll(pads),port=0;
        while(port<MAX_PADS && !(mask&(1<<port)))port++;
        int *minimum=stick?&cfg.right_min:&cfg.left_min,*maximum=stick?&cfg.right_max:&cfg.left_max;
        char limits[2][32],raw[64],filtered[64];
        snprintf(limits[0],32,"%d%%",*minimum);snprintf(limits[1],32,"%d%%",*maximum);
        int16_t x=0,y=0;
        if(port<MAX_PADS){x=stick?pads[port].rx:pads[port].lx;y=stick?pads[port].ry:pads[port].ly;}
        snprintf(raw,64,port<MAX_PADS?"X %d | Y %d":"No controller",x,y);
        snprintf(filtered,64,"X %d | Y %d",stick_calibrate(x,*minimum,*maximum),stick_calibrate(y,*minimum,*maximum));
        struct ui_setting rows[]={
            {"Stick",stick?"Right":"Left","Choose the stick to calibrate. Menu navigation is unchanged."},
            {"Minimum",limits[0],"Increase until the resting stick reads zero. 0-30%."},
            {"Maximum",limits[1],"Input reaches full output at this travel. 50-100%."},
            {"Raw input",raw,"Live values from the first connected controller."},
            {"Calibrated input",filtered,"Axial calibration preview. Applied when streaming."},
            {"Reset calibration","Reset","Restore both sticks to 0-100%."}};
        int kinds[]={UI_OPTION_CYCLE,UI_OPTION_CYCLE,UI_OPTION_CYCLE,UI_OPTION_INFO,UI_OPTION_INFO,UI_OPTION_ACTION};
        ui_draw_settings_options("Stick calibration",rows,kinds,6,sel);ui_present();
        int b=input_menu_pressed();if(tab_key(b,UI_SETTINGS) || (b&B_FLAG))break;
        if(b&UP_FLAG)sel=(sel+5)%6;if(b&DOWN_FLAG)sel=(sel+1)%6;
        if(b&(LEFT_FLAG|RIGHT_FLAG|A_FLAG)) {
            int d=b&LEFT_FLAG?-1:1;
            if(sel==0)stick=!stick;
            if(sel==1){*minimum+=d;if(*minimum<0)*minimum=0;if(*minimum>30)*minimum=30;}
            if(sel==2){*maximum+=d;if(*maximum<50)*maximum=50;if(*maximum>100)*maximum=100;}
            if(sel==5 && (b&A_FLAG)){cfg.left_min=cfg.right_min=0;cfg.left_max=cfg.right_max=100;}
        }
        plat_sleep_ms(16);
    }
}
static void pointer_settings_menu(void)
{
    int selected=0;
    for(;;){
        char speed[32],scroll[32];snprintf(speed,sizeof(speed),"%d%%",cfg.mouse_speed);snprintf(scroll,sizeof(scroll),"%d ms",cfg.mouse_scroll_ms);
        struct ui_setting rows[]={{"Mouse sensitivity",speed,"25-200%. Small movements accumulate instead of being discarded."},
            {"Mouse acceleration",cfg.mouse_acceleration?"On":"Off","Optional extra speed near the edge of the stick."},
            {"Scroll interval",scroll,"50-400 ms per wheel step."},
            {"Quick menu","Back + L3","Opens during streaming. A selects, B resumes. The full exit shortcut keeps priority."},
            {"Remote input","Back + R3","USB keyboard forwarding is suspended while the quick menu or Guide is open."}};
        int kinds[]={UI_OPTION_CYCLE,UI_OPTION_TOGGLE,UI_OPTION_CYCLE,UI_OPTION_INFO,UI_OPTION_INFO};
        ui_draw_settings_options("Remote mouse and keyboard",rows,kinds,5,selected);ui_present();int key=wait_button();
        if(key&B_FLAG)return;if(key&UP_FLAG)selected=(selected+4)%5;if(key&DOWN_FLAG)selected=(selected+1)%5;
        if(key&(LEFT_FLAG|RIGHT_FLAG|A_FLAG)){int d=key&LEFT_FLAG?-1:1;
            if(selected==0){cfg.mouse_speed+=d*25;if(cfg.mouse_speed<25)cfg.mouse_speed=200;if(cfg.mouse_speed>200)cfg.mouse_speed=25;}
            if(selected==1)cfg.mouse_acceleration=!cfg.mouse_acceleration;
            if(selected==2){cfg.mouse_scroll_ms+=d*50;if(cfg.mouse_scroll_ms<50)cfg.mouse_scroll_ms=400;if(cfg.mouse_scroll_ms>400)cfg.mouse_scroll_ms=50;}
        }
    }
}
static void power_menu(bool restart)
{
    if(!plat_power_available())return;
    const char *title=restart?"Restart console":"Shut down console";
    struct ui_setting rows[]={{"Cancel","Back","Return without changing console power."},
        {"Confirm",restart?"Restart":"Shut down","Save settings and perform the selected power action."}};
    int kinds[]={UI_OPTION_ACTION,UI_OPTION_ACTION},sel=0;
    for(;;) {
        ui_draw_settings_options(title,rows,kinds,2,sel);ui_present();int b=wait_button();
        if(b&B_FLAG)return;if(b&(UP_FLAG|DOWN_FLAG))sel=!sel;
        if(b&A_FLAG){if(!sel)return;break;}
    }
    discovery_pause();
    if(!plat_power_available())return;
    if(!config_save(&cfg)){message(ICON_ERROR,"Settings not saved","Power action cancelled. Check storage and try again.");return;}
    if(!plat_power_action(restart))message(ICON_ERROR,"Power action unavailable","The console did not accept this action.");
}

/* Controller editor also works without a signed-in user or a USB keyboard.
 * The XDK keyboard is preferred when available; cancellation leaves the name. */
static bool edit_profile_name(char name[CONFIG_PROFILE_NAME_BYTES])
{
#ifdef _XBOX
    int result=plat_edit_profile_name(name,CONFIG_PROFILE_NAME_BYTES);
    if(result>=0)return result!=0;
#endif
    static const char alphabet[]=" ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.";
    char edited[CONFIG_PROFILE_NAME_BYTES];snprintf(edited,sizeof(edited),"%s",name);
    /* Existing UTF-8 names remain intact if cancelled. The fallback edits ASCII. */
    for(int i=0;edited[i];i++)if((unsigned char)edited[i]>=128)edited[i]='_';
    int cursor=0;
    for(;;) {
        char position[100];snprintf(position,sizeof(position),"%s | %d",edited,cursor+1);
        struct ui_setting rows[]={{"Name",position,"D-pad: position / character. X deletes; Y inserts a space. A saves; B cancels."}};
        ui_draw_settings_page("Rename profile",rows,1,0);ui_present();int key=wait_button();
        if(key&B_FLAG)return false;
        if(key&A_FLAG){if(config_profile_name_valid(edited)){strcpy(name,edited);return true;}continue;}
        int n=(int)strlen(edited);
        if(key&LEFT_FLAG)cursor=cursor>0?cursor-1:0;
        if(key&RIGHT_FLAG)cursor=cursor<n?cursor+1:n;
        if((key&X_FLAG) && cursor<n)memmove(edited+cursor,edited+cursor+1,n-cursor);
        if((key&Y_FLAG) && n<CONFIG_PROFILE_NAME_BYTES-1){memmove(edited+cursor+1,edited+cursor,n-cursor+1);edited[cursor]=' ';}
        if(key&(UP_FLAG|DOWN_FLAG)) {
            n=(int)strlen(edited);if(cursor==n){if(n>=CONFIG_PROFILE_NAME_BYTES-1)continue;edited[n]=' ';edited[n+1]=0;}
            const char *at=strchr(alphabet,edited[cursor]);int pos=at?(int)(at-alphabet):0;
            edited[cursor]=alphabet[(pos+((key&UP_FLAG)?1:(int)sizeof(alphabet)-2))%((int)sizeof(alphabet)-1)];
        }
    }
}
static void user_profiles_menu(void)
{
    struct user_profiles profiles;memset(&profiles,0,sizeof(profiles));profiles.slot=-1;
    bool loaded=config_profiles_load(&profiles);
    int location=ui_location_enter("Saved profiles"),sel=0;
    if(!loaded && profiles.read_only)message(ICON_ERROR,"Profiles not loaded","Could not read profiles, or unknown version. Existing files were preserved.");
    for(;;) {
        char summaries[CONFIG_USER_PROFILE_MAX][180];struct ui_setting rows[CONFIG_USER_PROFILE_MAX+1];
        rows[0].label="Save current configuration";rows[0].value="Save";rows[0].help="Save stream, decoder, renderer and audio options. Host, language, controls and diagnostics stay global.";
        for(int i=0;i<profiles.count;i++) {
            const struct stream_options *o=&profiles.items[i].options;
            snprintf(summaries[i],sizeof(summaries[i]),ui_translate("%dx%d | %d FPS | %.1f Mbps | packet %d | slices %d | Fast %d | GPU/CPU %d | VSync %d | image %d | Auto %d | workers %d | layout %d | sops %d | PC audio %d"),
                o->width,o->height,o->fps,o->bitrate_kbps/1000.0,o->packet_size,o->slice_count,o->fast_decode,o->renderer,o->vsync,o->image_quality,o->adaptive_quality,o->decode_workers,o->thread_layout,o->sops,o->local_audio);
            rows[i+1].label="Saved profile";rows[i+1].value=profiles.items[i].name;rows[i+1].help=summaries[i];
        }
        if(sel>profiles.count)sel=profiles.count;
        ui_draw_settings_page("Saved profiles",rows,profiles.count+1,sel);ui_present();int key=wait_button();
        if(key&B_FLAG)break;
        if(key&UP_FLAG)sel=(sel+profiles.count)%(profiles.count+1);
        if(key&DOWN_FLAG)sel=(sel+1)%(profiles.count+1);
        if(!(key&A_FLAG))continue;
        if(!sel) {
            char name[CONFIG_PROFILE_NAME_BYTES];int number;
            for(number=1;number<=CONFIG_USER_PROFILE_MAX+1;number++) {
                snprintf(name,sizeof(name),ui_translate("My profile %d"),number);bool used=false;
                for(int i=0;i<profiles.count;i++)if(!strcmp(name,profiles.items[i].name))used=true;
                if(!used)break;
            }
            if(!config_profiles_create(&profiles,&cfg,name))message(ICON_ERROR,"Profile not saved","Check storage, profile limit and name. Existing profiles were preserved.");
            else sel=profiles.count;
            continue;
        }
        int index=sel-1,action=1;
        for(;;) {
            struct ui_setting actions[]={{"Name",profiles.items[index].name,summaries[index]},
                {"Apply","Next connection","Only stream options change. Saved profiles are never changed by temporary Auto adaptation."},
                {"Rename","Edit","Use the controller keyboard. B cancels."},{"Delete","Confirm...","Deletes only this saved profile. Factory presets stay available."}};
            ui_draw_settings_page("Profile actions",actions,4,action);ui_present();key=wait_button();
            if(key&B_FLAG)break;
            if(key&UP_FLAG)action=(action+3)%4;
            if(key&DOWN_FLAG)action=(action+1)%4;
            if(!(key&A_FLAG) || !action)continue;
            if(action==1) {
                struct config next=cfg;
                if(!config_profiles_apply(&profiles,index,&next) || !config_save(&next))message(ICON_ERROR,"Profile not applied","Could not save settings. Current selection was preserved.");
                else {cfg=next;message(ICON_CHECK,"Profile applied","Applies on the next connection.");}
                break;
            }
            if(action==2) {
                char name[CONFIG_PROFILE_NAME_BYTES];strcpy(name,profiles.items[index].name);
                if(edit_profile_name(name) && !config_profiles_rename(&profiles,index,name))message(ICON_ERROR,"Profile not saved","Check storage, profile limit and name. Existing profiles were preserved.");
            } else {
                int confirm=0;
                for(;;) {
                    struct ui_setting choices[]={{"Cancel","Back",NULL},{"Delete",profiles.items[index].name,NULL}};
                    ui_draw_settings_page("Delete profile?",choices,2,confirm);ui_present();key=wait_button();
                    if(key&B_FLAG){confirm=0;break;}
                    if(key&(UP_FLAG|DOWN_FLAG))confirm=1-confirm;
                    if(key&A_FLAG)break;
                }
                if(confirm && !config_profiles_delete(&profiles,index))message(ICON_ERROR,"Profile not saved","Check storage, profile limit and name. Existing profiles were preserved.");
                break;
            }
        }
    }
    ui_location_restore(location);
}
static void appearance_menu(void)
{
    static const char *backgrounds[]={"Geometry","Midnight","Solid graphite"};
    static const char *accents[]={"Xbox green","Ocean blue","Warm amber","Soft violet"};
    int selected=0,location=ui_location_enter("Appearance");
    for(;;) {
        struct ui_setting rows[]={
            {"Background",backgrounds[cfg.ui_background],"Static backgrounds. Changes are previewed immediately."},
            {"Accent color",accents[cfg.ui_accent],"Changes focus, the app symbol and the streaming panel. Launcher icon stays green."},
            {"Focus animation",cfg.ui_motion?"On":"Off","Brief focus animation in Home and Library. Off uses an immediate highlight."},
            {"Reset appearance","Reset","Restore the original geometric background and green accent."}};
        const int kinds[]={UI_OPTION_CYCLE,UI_OPTION_CYCLE,UI_OPTION_TOGGLE,UI_OPTION_ACTION};
        ui_draw_settings_options("Appearance",rows,kinds,4,selected);ui_present();
        int key=wait_button();if(key&B_FLAG)break;
        if(key&UP_FLAG)selected=(selected+3)%4;
        if(key&DOWN_FLAG)selected=(selected+1)%4;
        if(key&(A_FLAG|LEFT_FLAG|RIGHT_FLAG)) {
            int direction=(key&LEFT_FLAG)?-1:1;
            if(selected==0)cfg.ui_background=(cfg.ui_background+direction+3)%3;
            if(selected==1)cfg.ui_accent=(cfg.ui_accent+direction+4)%4;
            if(selected==2)cfg.ui_motion=!cfg.ui_motion;
            if(selected==3 && (key&A_FLAG)){cfg.ui_background=0;cfg.ui_accent=0;cfg.ui_motion=false;}
            ui_set_appearance(cfg.ui_background,cfg.ui_accent);ui_set_motion(cfg.ui_motion);ui_set_stream_hud(cfg.stream_menu_style,cfg.mouse_hud);
        }
    }
    ui_location_restore(location);
}

static void overlay_options_menu(void)
{
    static const char *menus[]={"Compact","Sidebar"};
    static const char *indicators[]={"Compact","Text only","Hidden"};
    int selected=0,location=ui_location_enter("Streaming interface");
    for(;;) {
        struct ui_setting rows[]={
            {"Mouse indicator",indicators[cfg.mouse_hud],"Compact badge, text without background, or hidden. Applied next session."},
            {"Quick menu style",menus[cfg.stream_menu_style],"Compact uses fewer pixels. Sidebar keeps the larger layout. Applied next session."}};
        const int kinds[]={UI_OPTION_CYCLE,UI_OPTION_CYCLE};
        ui_draw_settings_options("Streaming interface",rows,kinds,2,selected);ui_present();
        int key=wait_button();if(key&B_FLAG)break;
        if(key&(UP_FLAG|DOWN_FLAG))selected=!selected;
        if(key&(LEFT_FLAG|RIGHT_FLAG|A_FLAG)) {
            int direction=key&LEFT_FLAG?-1:1;
            if(!selected)cfg.mouse_hud=(cfg.mouse_hud+direction+3)%3;
            else cfg.stream_menu_style=(cfg.stream_menu_style+direction+2)%2;
        }
    }
    ui_location_restore(location);
}
static void maintenance_menu(int selected)
{
    if(selected && !plat_restart_available())return;
    const char *title=selected?"Restart app?":"Reset app settings?";
    struct ui_setting confirmation[]={{"Cancel","Back","Return without changing settings."},
        {"Confirm",selected?"Restart app":"Reset app settings",selected?
        "Save preferences and reopen this app. Does not restart the Xbox.":
        "Reset preferences; keep saved PCs, pairing and named profiles."}};
    const int kinds[]={UI_OPTION_ACTION,UI_OPTION_ACTION};
    int confirm=0;
    for(;;) {
        ui_draw_settings_options(title,confirmation,kinds,2,confirm);ui_present();int key=wait_button();
        if(key&B_FLAG)return;
        if(key&(UP_FLAG|DOWN_FLAG))confirm=!confirm;
        if(key&A_FLAG){if(!confirm)return;break;}
    }
    if(!selected) {
        if(!config_reset_preferences(&cfg)){message(ICON_ERROR,"Settings not reset","Could not save preferences. Existing settings were preserved.");return;}
        ui_set_language(cfg.language);ui_set_appearance(cfg.ui_background,cfg.ui_accent);ui_set_motion(cfg.ui_motion);
        ui_set_stream_hud(cfg.stream_menu_style,cfg.mouse_hud);ui_set_exit_shortcut(cfg.quit_combo);
        plat_diagnostics_configure(cfg.diagnostic_log);
        message(ICON_CHECK,"Settings reset","Saved PCs, pairing and named profiles were kept.");
    } else {
        if(!config_save(&cfg)){message(ICON_ERROR,"Restart cancelled","Could not save preferences. Existing settings were preserved.");return;}
        if(!plat_restart_app())message(ICON_ERROR,"Restart unavailable","Could not reopen the current executable.");
    }
}

static void settings_menu(void)
{
    bool stream_page=settings_initial_group==SETTINGS_STREAM;
    int page=stream_page?MENU_STREAM:MENU_SETTINGS;
    if(!stream_page && settings_initial_group>=0 && settings_initial_group<SETTINGS_STREAM)
        page=MENU_PCS+settings_initial_group;
    int focus[MENU_COUNT]={0},tokens[MENU_COUNT];
    for(int t=0;t<MENU_COUNT;t++)tokens[t]=-1;
    settings_initial_group=-1;
    settings_navigation_page=stream_page?UI_STREAM:UI_SETTINGS;
    ui_set_settings_context(settings_navigation_page,settings_pages[page].category);
    int location=ui_location_enter(stream_page?"Stream":"Settings");
    if(page!=MENU_STREAM && page!=MENU_SETTINGS)tokens[page]=ui_location_enter(settings_pages[page].title);
    for (;;) {
        if(navigation_pending>=0)break;
        ui_set_settings_context(settings_navigation_page,settings_pages[page].category);
        int sel=focus[page];
        static const char *labels[SET_COUNT] = {
            "Video resolution", "Frames per second (FPS)", "Video bitrate (Mbps)", "Fast decode", "Renderer", "V-Sync",
            "Optimize game settings", "Play audio on PC", "Decoder benchmark",
            "Stream Profile", "Disconnect shortcut", "Calibrate sticks", "Save session report",
            "RTP packet size",
            "On-screen statistics", "Diagnostic capture", "View logs", "H.264 slices", "Bitrate comparisons",
            "Image quality", "Record session history", "Recent sessions", "Performance analysis",
            "Find PCs automatically (mDNS)", "UI resources", "Recovery information", "Network diagnostics",
            "Automatic quality adjustment", "Detected video output", "App resolution", "Language", "CPU layout", "Decode workers", "Restart Xbox", "Shut down Xbox", "Saved profiles", "Xbox audio output", "Remote mouse and keyboard",
        };
        static const char *helps[SET_COUNT] = {
            "Requested host resolution. Compare measured decode and queue time on hardware; lower resolution alone does not guarantee better performance.",
            "24, 30, 40, 50 or 60 FPS requested from the host, without interpolation. 30 FPS is the default for new configurations. "
            "Higher rates cost more decode time; rates that do not divide the 60 Hz display may look uneven.",
            "Requested bitrate: 1-70 Mbps. Changing it keeps the current decoder and image settings, then selects Custom. Applies next connection.",
            "Shows the effective setting. On skips H.264 deblocking and may show blocks. Changing it selects Custom. Off retains automatic partial recovery under overload.",
            "GPU: the Xenos GPU converts and scales each frame (fastest). CPU modes are fallbacks if "
            "the GPU path misbehaves.",
            "Wait for the TV's refresh before showing a frame (no tearing). Off gives slightly lower latency.",
            "Sends the standard GameStream sops=1 optimization request. Resolution/FPS use mode=WxHxFPS; host support determines game/display changes.",
            "Also play the game's audio on the host PC's speakers.",
            "Decodes the recording of your first stream with and without the console's vector "
            "unit and shows the time per frame. Takes about a minute.",
            "Performance: 480p60/25. Standard (default): 720p30/12. Smooth: 720p60/18. 1080p Smooth: 1080p60/20. Maximum Quality: 720p60/35. Custom: manual resolution/FPS/bitrate. Smooth/Performance use Fast decode and the light GPU shader during playback.",
            "Hold for 2 seconds on one controller. L3/R3 mean clicking the sticks. "
            "The full shortcut is handled locally; the app keeps running on the PC.",
            "Filters small stick movements during streaming and rescales the remaining range. "
            "0% preserves the original input. Does not affect menu navigation.",
            "Saves last-session.txt in the moonlight folder after the stream stops. "
            "No disk writes or extra drawing while streaming; overwrites the previous report.",
            "1024 bytes is the default. Larger packets reduce packet count but may fragment on some networks. Existing saved values are kept until changed.",
            "Shows measured FPS, local drops, decode/render time, RTT, host processing, video payload and queues. "
            "Text updates once per second; one extra GPU quad per shown frame. Off avoids this cost. Applies next stream. "
            "Frame gaps are not packet loss; payload Mbps is not total link bandwidth.",
            "Keeps a bounded log in RAM. Saves connection checkpoints and flushes logs after stopping/failure. "
            "Sudden crashes may lose unsaved RAM lines; saved checkpoints remain. Off by default.",
            "Browse current and previous logs. Left/Right: source. Up/Down: lines. Start: ten lines. LB/RB: tabs. "
            "This is not a kernel crash dump.",
            "Next connection only: Auto uses 8 slices for 720p60, 12 for 1080p60. "
            "Overrides 8/12/16 are diagnostic requests, not guaranteed speedups. Decode workers: 3 or 4 in Decoder settings; rebuild FFmpeg when updating this version.",
            "Next session only. 720p: 10/15/18/25 Mbps; 1080p: 12/16/20/25 Mbps. "
            "Changes the next session bitrate, not a network speed test. Compare queues, recovery, decode p99 and shown FPS.",
            "GPU image processing, separate from the H.264 filter. Quality keeps smoothing; Auto Quality may use Fast after measurement. Changing it keeps Fast Decode and selects Custom. Next connection.",
            "Keep up to 32 small summaries on this console (16 KiB file). Old duplicates are discarded first. Written after stop, never during streaming. Off preserves existing history.",
            "Browse recent sessions for this console, including host, game, quality and measured results.",
            "Advice from measured sessions. Apply explicitly selects Standard / 720p30 / 12 Mbps for the next connection. Missing metrics remain unavailable.",
            "Discover GameStream hosts on the LAN using _nvstream._tcp.local. Cached hosts remain available. No subnet scan. Paused outside Home.",
            "Show actual UI resource usage.",
            "Current automatic H.264 recovery and latest-frame behavior. This page does not change decoder, queue or recovery policy.",
            "Last stopped session: measured H.264 payload, queue age, drops and RTT. Loss and FEC counters are unavailable in this collector.",
            "Manual keeps your selection. Auto may lower bitrate or resolution after sustained network/decode pressure, with a brief reconnection at most 3 times. FPS and saved settings are preserved.",
            "Physical output detected from the console. HDMI/VGA/component output mode is configured in the Xbox dashboard.",
            "Rendering resolution: Auto, 480p, 720p or 1080p. Applies after restarting the app; the console scales it to the physical output. It does not change the cable mode.",
            "Menu language: English, Spanish or Brazilian Portuguese. Host/game names and diagnostic logs are kept as received.",
            "Balanced moves audio and input to HW0; Legacy keeps them on HW5 with presentation. Both keep the patched workers on HW1,2,4[,3]; Legacy does not restore the old worker map. Next connection; compare on hardware.",
            "Four workers is the default. Three workers use one per physical core after rebuilding FFmpeg. More workers are not automatically faster. Next connection.",
            "Restart the console after a confirmation. No action is sent to the PC.",
            "Shut down the console after a confirmation. No action is sent to the PC.",
            "Save, apply, rename or delete your stream profiles. Factory presets are unchanged.",
            "Stereo by default. 5.1 requires a configured surround output; unavailable outputs use stereo.",
            "Back + R3: mouse mode. Left stick moves; A/B click; right stick scrolls. USB keyboard forwards only during streaming.",
        };
        char val[SET_COUNT][48];
        snprintf(val[SET_REMOTE_INPUT],48,"%s","Back + R3");
        snprintf(val[SET_AUDIO_MODE],48,"%s",cfg.audio_mode?"5.1":"Stereo");
        snprintf(val[SET_USER_PROFILES],48,"Manage");
        struct ui_setting rows[SET_COUNT];
        int map[SET_COUNT],children[SET_COUNT],kinds[SET_COUNT];   /* row -> setting id and interaction */
        int b, i, n = 0, res = 0;

        for (i = 0; i < 3; i++)
            if (resolutions[i][0] == cfg.width && resolutions[i][1] == cfg.height)
                res = i;
        snprintf(val[SET_RES], 48, "%dx%d", cfg.width, cfg.height);
        snprintf(val[SET_FPS], 48, "%d FPS", cfg.fps);
        snprintf(val[SET_BITRATE], 48, "%d Mbps", cfg.bitrate_kbps / 1000);
        struct stream_options effective;
        config_profile_capture(&effective,&cfg);
        snprintf(val[SET_FAST], 48, "%s", effective.fast_decode ? "On" : "Off");
        snprintf(val[SET_RENDERER], 48, "%s", cfg.renderer == RENDERER_GPU ? "GPU" :
                 cfg.renderer == RENDERER_CPU ? "CPU" : "CPU + colour matrix");
        snprintf(val[SET_VSYNC], 48, "%s", cfg.vsync ? "On" : "Off");
        snprintf(val[SET_SOPS], 48, "%s", cfg.sops ? "On" : "Off");
        snprintf(val[SET_AUDIO], 48, "%s", cfg.local_audio ? "On" : "Off");
        snprintf(val[SET_BENCH],48,"Run test");
        {
            snprintf(val[SET_PRESET], 48, "%s", config_preset_name(config_video_preset(&cfg)));
        }
        snprintf(val[SET_EXIT], 48, "%s", cfg.quit_combo == 1 ? "Back+Start+LB+RB" : "LB+RB+L3+R3");
        snprintf(val[SET_DEADZONE], 48, "L %d-%d%% | R %d-%d%%", cfg.left_min,cfg.left_max,cfg.right_min,cfg.right_max);
        snprintf(val[SET_ADAPTIVE],48,"%s",cfg.adaptive_quality?"Auto (reconnect)":"Manual");
        char display_info[128];plat_display_info(display_info,sizeof(display_info));
        snprintf(val[SET_DISPLAY_INFO],48,"%s",display_info);
        static const char *display_modes[]={"Auto","480p","720p","1080p"};
        snprintf(val[SET_DISPLAY_MODE],48,"%s",display_modes[cfg.display_mode]);
        static const char *language_names[]={"English","Spanish","Portuguese (Brazil)"};
        snprintf(val[SET_LANGUAGE],48,"%s",language_names[cfg.language]);
        snprintf(val[SET_CPU_LAYOUT],48,"%s",cfg.thread_layout?"Balanced":"Legacy");
        snprintf(val[SET_WORKERS],48,"%d",cfg.decode_workers);
        snprintf(val[SET_RESTART],48,"%s",plat_power_available()?"Confirm...":"Unavailable");
        snprintf(val[SET_SHUTDOWN],48,"%s",plat_power_available()?"Confirm...":"Unavailable");
        snprintf(val[SET_REPORT], 48, "%s", cfg.session_report ? "On" : "Off");
        snprintf(val[SET_PACKET], 48, "%d bytes", cfg.packet_size);
        static const char *overlay_names[]={"Off","Basic","Detailed","Developer"};
        snprintf(val[SET_OVERLAY],48,"%s",overlay_names[config_overlay_mode(&cfg)]);
        snprintf(val[SET_IMAGE_QUALITY],48,"%s",effective.image_quality==1?"Fast":effective.image_quality==2?"Quality":"Auto Quality");
        snprintf(val[SET_HISTORY],48,"%s",cfg.history_enabled?"On":"Off");
        snprintf(val[SET_HISTORY_VIEW],48,ui_translate("%d sessions"),session_history.count);
        snprintf(val[SET_ADVISOR],48,"View analysis");
        snprintf(val[SET_NETWORK_INFO],48,"View metrics");
        snprintf(val[SET_MDNS],48,"%s",cfg.mdns_discovery?"On":"Off");
        snprintf(val[SET_MEMORY],48,"View usage");
        snprintf(val[SET_RECOVERY_INFO],48,"Automatic (current)");
        snprintf(val[SET_LOG_CAPTURE], 48, "%s", cfg.diagnostic_log ? "On" : "Off");
        snprintf(val[SET_LOG_VIEW],48,"View logs");
        snprintf(val[SET_TEST_BITRATE], 48, "%d Mbps", cfg.bitrate_kbps/1000);
        if (cfg.slice_count) snprintf(val[SET_SLICES], 48, "%d", cfg.slice_count);
        else snprintf(val[SET_SLICES], 48, "Auto");

        for (i = 0; i < SETTINGS_ENTRY_COUNT; i++) {
            const struct settings_menu_entry *entry=settings_entries+i;
            if(entry->page!=page)continue;
            int id=entry->setting;
            if(id<0) {
                int child=entry->child;
                rows[n].label=settings_pages[child].title;rows[n].value="Open";rows[n].help=settings_pages[child].help;
                kinds[n]=(child==MENU_RESTART_APP || child==MENU_RESET_APP)?UI_OPTION_ACTION:UI_OPTION_OPEN;
                if(child==MENU_RESTART_APP)rows[n].value=plat_restart_available()?"Confirm...":"Unavailable";
                if(child==MENU_RESET_APP)rows[n].value="Confirm...";
                map[n]=-1;children[n++]=child;continue;
            }
            if (id == SET_RENDERER && !plat_has_renderer_choice())
                continue;
            if (id == SET_BENCH && !plat_benchmark_available())
                continue;
            if ((id == SET_OVERLAY || id == SET_LOG_CAPTURE || id == SET_LOG_VIEW) && !plat_has_diagnostics())
                continue;
            rows[n].label = labels[id];
            rows[n].value = id==SET_DISPLAY_INFO?display_info:val[id];
            rows[n].help = helps[id];
            kinds[n]=settings_kind(id);children[n]=-1;map[n++] = id;
        }

        if(sel>=n)sel=focus[page]=n-1;
        ui_draw_settings_options(settings_pages[page].title,rows,kinds,n,sel);
        ui_present();
        b = wait_button();
        if(navigation_pending>=0 || tab_key(b,settings_navigation_page))break;
        if (b & UP_FLAG) focus[page]=(sel+n-1)%n;
        if (b & DOWN_FLAG) focus[page]=(sel+1)%n;
        if (b & B_FLAG) {
            if(settings_pages[page].parent<0)break;
            if(tokens[page]>=0)ui_location_restore(tokens[page]);
            tokens[page]=-1;page=settings_pages[page].parent;continue;
        }
        if(page==MENU_APPEARANCE_SCREEN && (b&X_FLAG)){appearance_menu();continue;}
        if(page==MENU_HISTORY && (b&X_FLAG)) {
            ui_location_restore(tokens[page]);
            int branch=ui_location_enter("Appearance and display");
            ui_set_settings_context(UI_SETTINGS,SETTINGS_APPEARANCE);
            overlay_options_menu();ui_location_restore(branch);
            tokens[page]=ui_location_enter(settings_pages[page].title);continue;
        }
        if((b&(A_FLAG|PLAY_FLAG)) && children[sel]>=0) {
            int child=children[sel];
            int branch=ui_location_enter(settings_pages[child].title);
            switch(child) {
            case MENU_HOSTS:hosts_menu(UI_SETTINGS);break;
            case MENU_APPEARANCE:appearance_menu();break;
            case MENU_STREAM_UI:overlay_options_menu();break;
            case MENU_RESTART_APP:maintenance_menu(1);break;
            case MENU_RESET_APP:maintenance_menu(0);break;
            default:tokens[child]=branch;page=child;continue;
            }
            ui_location_restore(branch);continue;
        }
        if (map[sel]>=0 && (b & (LEFT_FLAG | RIGHT_FLAG | A_FLAG))) {
            int d = (b & LEFT_FLAG) ? -1 : 1;
            int branch=ui_location_enter(NULL);
            switch (map[sel]) {
            case SET_REMOTE_INPUT:if(b&A_FLAG)pointer_settings_menu();break;
            case SET_USER_PROFILES:if(b&A_FLAG)user_profiles_menu();break;
            case SET_NETWORK_INFO:if(b&A_FLAG)network_diagnostics_menu();break;
            case SET_MDNS:cfg.mdns_discovery=!cfg.mdns_discovery;break;
            case SET_MEMORY:if(b&A_FLAG)ui_memory_menu();break;
            case SET_RECOVERY_INFO:break;
            case SET_OVERLAY: cfg.overlay_mode=(config_overlay_mode(&cfg)+d+4)%4;cfg.stats_overlay=cfg.overlay_mode!=0;break;
            case SET_IMAGE_QUALITY:config_customize_stream(&cfg);cfg.image_quality=(cfg.image_quality+d+3)%3;break;
            case SET_HISTORY:cfg.history_enabled=!cfg.history_enabled;break;
            case SET_HISTORY_VIEW:if(b&A_FLAG)history_menu();break;
            case SET_ADVISOR:if(b&A_FLAG)advisor_menu();break;
            case SET_LOG_CAPTURE:
                cfg.diagnostic_log = !cfg.diagnostic_log;
                plat_diagnostics_configure(cfg.diagnostic_log);
                break;
            case SET_LOG_VIEW:
                if (b & A_FLAG) log_viewer();
                break;
            case SET_PACKET: {
                static const int sizes[] = { 1024, 1200, 1392 };
                int packet = -1, j;
                for (j = 0; j < 3; j++) if (cfg.packet_size == sizes[j]) packet = j;
                packet = packet < 0 ? (d > 0 ? 0 : 2) : (packet + d + 3) % 3;
                cfg.packet_size = sizes[packet];
                break;
            }
            case SET_PRESET: {
                int preset = config_video_preset(&cfg);
                static const int order[]={CONFIG_PROFILE_PERFORMANCE,CONFIG_PROFILE_STANDARD,CONFIG_PROFILE_SMOOTH,CONFIG_PROFILE_1080_SMOOTH,CONFIG_PROFILE_QUALITY,0};
                int pos=0;while(pos<5 && order[pos]!=preset)pos++;
                preset=order[(pos+d+6)%6];
                config_apply_preset(&cfg, preset);
                break;
            }
            case SET_EXIT: cfg.quit_combo = !cfg.quit_combo; break;
            case SET_DEADZONE:if(b&A_FLAG)calibrate_sticks();break;
            case SET_ADAPTIVE:cfg.adaptive_quality=!cfg.adaptive_quality;break;
            case SET_DISPLAY_INFO:break;
            case SET_DISPLAY_MODE:cfg.display_mode=(cfg.display_mode+d+4)%4;break;
            case SET_LANGUAGE:cfg.language=(cfg.language+d+CONFIG_LANGUAGE_COUNT)%CONFIG_LANGUAGE_COUNT;ui_set_language(cfg.language);break;
            case SET_CPU_LAYOUT:cfg.thread_layout=!cfg.thread_layout;break;
            case SET_WORKERS:cfg.decode_workers=cfg.decode_workers==3?4:3;break;
            case SET_RESTART:case SET_SHUTDOWN:if((b&A_FLAG) && plat_power_available())power_menu(map[sel]==SET_RESTART);break;
            case SET_REPORT: cfg.session_report = !cfg.session_report; break;
            case SET_RES:
                res = (res + d + 3) % 3;
                config_customize_stream(&cfg);
                cfg.width = resolutions[res][0];
                cfg.height = resolutions[res][1];
                break;
            case SET_FPS: config_customize_stream(&cfg);cfg.fps = config_cycle_fps(cfg.fps, d); break;
            case SET_BITRATE:
                config_customize_stream(&cfg);
                cfg.bitrate_kbps = config_step_bitrate(cfg.bitrate_kbps, d);
                break;
            case SET_TEST_BITRATE: config_customize_stream(&cfg);cfg.bitrate_kbps = config_test_bitrate(&cfg,d); break;
            case SET_SLICES: cfg.slice_count = config_cycle_slices(cfg.slice_count, d); break;
            case SET_FAST: config_customize_stream(&cfg);cfg.fast_decode = !cfg.fast_decode; break;
            case SET_RENDERER: cfg.renderer = (cfg.renderer + d + RENDERER_COUNT) % RENDERER_COUNT; break;
            case SET_VSYNC: cfg.vsync = !cfg.vsync; break;
            case SET_SOPS: cfg.sops = !cfg.sops; break;
            case SET_AUDIO_MODE: cfg.audio_mode=!cfg.audio_mode; break;
            case SET_AUDIO: cfg.local_audio = !cfg.local_audio; break;
            case SET_BENCH:
                if (b & A_FLAG)
                    plat_run_benchmark();
                break;
            }
            ui_location_restore(branch);
        }
    }
    if (!config_save(&cfg))
        message(ICON_INFO, "Settings kept for this session", "Couldn't save moonlight.ini. Check that the app folder is writable.");
    ui_set_exit_shortcut(cfg.quit_combo);
    ui_location_restore(location);
    settings_navigation_page=-1;ui_set_settings_context(-1,-1);
    perf_begin(cfg.diagnostic_log || cfg.stats_overlay);
}

/* ------------------------------------------------------------ pairing */

struct pair_job {
    char pin[5];
    bool ok;
};

static void *pair_task(void *arg)
{
    struct pair_job *j = (struct pair_job *)arg;
    j->ok = gs_pair(&server, j->pin);
    return NULL;
}

static void draw_pair(void *ctx, uint32_t t)
{
    struct pair_job *j = (struct pair_job *)ctx;
    struct ui_host h = host_view();
    ui_draw_pair(&h, j->pin, t);
}

static void do_pair(void)
{
    struct pair_job job;
    unsigned char r[4];
    int i;

    crypto_random(r, sizeof(r));
    for (i = 0; i < 4; i++)
        job.pin[i] = (char)('0' + r[i] % 10);
    job.pin[4] = '\0';
    job.ok = false;

    run_task(pair_task, &job, draw_pair, &job);
    if (job.ok)
        message(ICON_CHECK, "Paired", "This console can now stream from the host. You won't need to pair again.");
    else
        message(ICON_ERROR, "Pairing failed", gs_error());
}

/* ------------------------------------------------------------ streaming */

static volatile bool connection_terminated;
static bool stream_return_home;
static volatile int connection_poor;
static unsigned adaptive_reconnects;
static void cl_connection_quality(int status) {plat_atomic_store(&connection_poor,status==CONN_STATUS_POOR);}

static volatile int termination_error;
static char stage_text[96];
static uint32_t connection_stage_ms;

/* Moonlight's log lines go to the platform's debug output, not the screen. */
static void cl_log(const char *fmt, ...)
{
    char line[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (!strncmp(line,"Received first video packet after ",34)) perf_mark(PERF_FIRST_PACKET);
    plat_log(line);
}

static void cl_stage_starting(int stage)
{
    connection_stage_ms=plat_now_ms();
    cl_log("[NET] %s start\n",LiGetStageName(stage));
    cl_log("Stage starting: %s\n", LiGetStageName(stage));
    snprintf(stage_text, sizeof(stage_text), "Starting %s...", LiGetStageName(stage));
    if (!ui_suspended) plat_diagnostics_checkpoint(stage_text);
    /* From here on the video renderer takes over the screen. */
    if (stage >= STAGE_VIDEO_STREAM_INIT)
        ui_suspended = true;
}
static void cl_stage_complete(int stage)
{
    if(stage==STAGE_RTSP_HANDSHAKE)perf_record(PERF_RTSP,(uint64_t)(plat_now_ms()-connection_stage_ms)*1000);
    cl_log("[NET] %s done %u ms\n",LiGetStageName(stage),plat_now_ms()-connection_stage_ms);
}

static void cl_stage_failed(int stage, int err)
{
    cl_log("Stage failed: %s (error %d)\n", LiGetStageName(stage), err);
    snprintf(stage_text, sizeof(stage_text), "%s failed (error %d)", LiGetStageName(stage), err);
    plat_diagnostics_checkpoint(stage_text);
}

static void cl_connection_terminated(int err)
{
    cl_log("Connection terminated (error %d)\n", err);
    termination_error = err;
    connection_terminated = true;
}

struct launch_job {
    int app_id;
    STREAM_CONFIGURATION sc;
    bool ok;
};

static void *launch_task(void *arg)
{
    uint32_t begun=plat_now_ms();plat_log("[NET] launch start\n");
    struct launch_job *j = (struct launch_job *)arg;
    if(cfg.host_uuid[0] && !wake_identity(cfg.host_uuid,server.uuid)) {
        j->ok=false;server_ok=false;snprintf(server_error,sizeof(server_error),"%s","The PC at this IP has a different identity. Choose the intended PC again in Hosts.");return NULL;
    }
    j->ok = gs_launch(&server, &j->sc, j->app_id, cfg.sops, cfg.local_audio);
    if(cfg.host_uuid[0] && !wake_identity(cfg.host_uuid,server.uuid)) {j->ok=false;server_ok=false;snprintf(server_error,sizeof(server_error),"%s","The PC at this IP has a different identity. Choose the intended PC again in Hosts.");}
    cl_log("[NET] launch done %u ms (ok=%d)\n",plat_now_ms()-begun,j->ok);
    return NULL;
}

struct connect_job {
    STREAM_CONFIGURATION *sc;
    CONNECTION_LISTENER_CALLBACKS *cl;
    DECODER_RENDERER_CALLBACKS *dr;
    AUDIO_RENDERER_CALLBACKS *ar;
    int ret;
};

static void *connect_task(void *arg)
{
    uint32_t begun=plat_now_ms();plat_log("[NET] LiStartConnection start\n");
    struct connect_job *j = (struct connect_job *)arg;
    j->ret = LiStartConnection(&server.info, j->sc, j->cl, j->dr, j->ar, NULL, 0, NULL, 0);
    perf_record(PERF_NEGOTIATION,(uint64_t)(plat_now_ms()-begun)*1000);
    if(j->ret==0)perf_mark(PERF_CONNECTED);
    cl_log("[NET] LiStartConnection TOTAL %u ms (result=%d)\n",plat_now_ms()-begun,j->ret);
    return NULL;
}

static void draw_connecting(void *ctx, uint32_t t)
{
    ui_draw_busy((const char *)ctx, stage_text, t);
}

static bool show_stream_stats(uint32_t elapsed)
{
    struct video_stats vs;
    struct ui_stat st[8];
    char title[64];
    int n = 0, b;
    bool retry = connection_terminated && termination_error != ML_ERROR_GRACEFUL_TERMINATION;

    video_get_stats(&vs);
    if (connection_terminated && termination_error != ML_ERROR_GRACEFUL_TERMINATION)
        snprintf(title, sizeof(title), ui_translate("Connection lost (error %d)"), termination_error);
    else
        snprintf(title, sizeof(title), "%s",ui_translate("Stream ended"));

    st[n].label = "Frames received / decoded / shown";
    snprintf(st[n++].value, 64, "%u / %u / %u", vs.received, vs.decoded, vs.rendered);
    st[n].label = "Dropped (decoder too slow)";
    snprintf(st[n++].value, 64, "%u", vs.dropped);
    st[n].label = "Decode time per frame";
    snprintf(st[n++].value, 64, ui_translate("%.1f ms (%d threads)"), vs.avg_decode_us / 1000.0, vs.decode_threads);
    st[n].label = "Render time per frame";
    snprintf(st[n++].value, 64, "%.1f ms (%s%s)", vs.avg_render_us / 1000.0, video_renderer_name(vs.renderer),
             vs.renderer == RENDERER_GPU && cfg.vsync ? ", incl. v-sync" : "");
    st[n].label = "Video";
    snprintf(st[n++].value, 64, ui_translate("%dx%d @ %d FPS on %dx%d"), cfg.width, cfg.height, cfg.fps, vs.display_w, vs.display_h);
    st[n].label = "Frame budget";
    snprintf(st[n++].value, 64, "%.1f ms", 1000.0 / cfg.fps);
    st[n].label = "Decode errors";
    snprintf(st[n++].value, 64, "%u", vs.decode_errors);
    st[n].label = "Shown FPS / session duration";
    snprintf(st[n++].value, 64, "%.1f / %.1f s", elapsed ? vs.rendered * 1000.0 / elapsed : 0.0, elapsed / 1000.0);

    ui_draw_stream_stats(title, st, n, retry ? "Press A to reconnect or B to return. The app stays open on the PC." :
                        "The app keeps running on the PC. Use \"Quit running app\" to close it.", retry);
    ui_present();
    do { b = wait_button(); } while (!(b & (A_FLAG | B_FLAG)));
    return retry && (b & A_FLAG) && !(b & B_FLAG);
}

static void save_session_report(uint32_t elapsed)
{
    char text[2048];
    struct video_stats vs;
    struct stream_options effective;
    int n;
    if (!cfg.session_report) return;
    config_profile_capture(&effective,&cfg);
    video_get_stats(&vs);
    n = snprintf(text, sizeof(text),
        "Moonlight session report\nvideo=%dx%d@%d\nbitrate_kbps=%d\nfast_decode=%d\nfast_decode_saved=%d\nimage_quality=%d\n"
        "duration_ms=%u\nreceived=%u\ndecoded=%u\nrendered=%u\ndropped=%u\ndecode_errors=%u\n"
        "shown_fps=%.2f\ndecode_ms_smoothed=%.2f\nrender_ms_smoothed=%.2f\ndecode_threads=%d\n"
        "connection_terminated=%d\ntermination_error=%d\n",
        cfg.width, cfg.height, cfg.fps, cfg.bitrate_kbps, effective.fast_decode,cfg.fast_decode,effective.image_quality,elapsed,
        vs.received, vs.decoded, vs.rendered, vs.dropped, vs.decode_errors,
        elapsed ? vs.rendered * 1000.0 / elapsed : 0.0, vs.avg_decode_us / 1000.0,
        vs.avg_render_us / 1000.0, vs.decode_threads, connection_terminated,
        connection_terminated ? termination_error : 0);
    if(n>0 && (size_t)n<sizeof(text)) {
        struct performance_sample sample;video_get_session_performance(&sample);
        char tail[96],payload[40];
        if(sample.tail_valid)snprintf(tail,sizeof(tail),"decode_p95_us=%u\ndecode_p99_us=%u",sample.decode_p95_us,sample.decode_p99_us);
        else snprintf(tail,sizeof(tail),"decode_p95_us=unavailable\ndecode_p99_us=unavailable");
        if(cfg.stats_overlay)snprintf(payload,sizeof(payload),"%.3f",sample.payload_mbps);
        else snprintf(payload,sizeof(payload),"unavailable");
        n+=snprintf(text+n,sizeof(text)-(size_t)n,
            "stream_profile=%s\ndecode_avg_us=%u\n%s\nqueue_age_us=%u\nqueue_peak_us=%u\n"
            "present_fps=%.3f\nincoming_h264_payload_mbps=%s\n"
            "packet_loss=unavailable\nfec_recovered=unavailable\nfec_unrecoverable=unavailable\n"
            "percentiles=rolling_256_samples; Detailed/Developer overlay required\n",
            config_profile_key(config_video_preset(&cfg)),sample.decode_avg_us,tail,sample.queue_us,sample.queue_peak_us,
            sample.shown_fps,payload);
    }
    if (n < 0 || (size_t)n >= sizeof(text) || !storage_write("last-session.txt", text, (size_t)n))
        message(ICON_INFO, "Report not saved", "Couldn't write last-session.txt. Streaming is unaffected.");
}

static void record_session(int app_id,const char *title,uint32_t elapsed)
{
    if(!cfg.history_enabled)return;
    struct performance_sample sample;video_get_session_performance(&sample);
    struct video_stats vs;video_get_stats(&vs);
    if(!sample.budget_us)sample.budget_us=1000000/(cfg.fps>0?cfg.fps:60);
    sample.target_fps=cfg.fps;
    if(!sample.shown_fps && elapsed)sample.shown_fps=vs.rendered*1000.0/elapsed;
    struct performance_result result=performance_assess(&sample);
    struct session_record record;memset(&record,0,sizeof(record));
    time_t clock=time(NULL);record.timestamp=clock>0?(unsigned)clock:0;
    history_text(record.host,sizeof(record.host),host_key());history_text(record.app,sizeof(record.app),title);
    record.app_id=app_id;record.width=cfg.width;record.height=cfg.height;record.fps=cfg.fps;record.bitrate=cfg.bitrate_kbps;
    record.shown_milli=(unsigned)(sample.shown_fps*1000.0);record.decode_avg_us=sample.decode_avg_us;
    record.decode_p99_us=sample.decode_p99_us;record.tail_valid=sample.tail_valid;
    record.rtt_ms=sample.rtt_ms;record.rtt_valid=sample.rtt_valid;record.grade=result.grade;record.cause=result.cause;
    history_push(&session_history,&record);
    char text[SESSION_HISTORY_BYTES];size_t length=history_encode(&session_history,text,sizeof(text));
    history_save_failed=!length || !storage_write("sessions.tsv",text,length);
    if(history_save_failed)plat_log("Session history retained in RAM; save failed.\n");
}
static const struct session_record *latest_session(void)
{
    for(int i=0;i<session_history.count;i++)if(!strcmp(session_history.records[i].host,host_key()))return session_history.records+i;
    return NULL;
}
static void record_date(const struct session_record *r,char *out,size_t size)
{
    time_t t=(time_t)r->timestamp;struct tm *utc=r->timestamp?gmtime(&t):NULL;
    if(!utc || !strftime(out,size,"%Y-%m-%d %H:%M UTC",utc))snprintf(out,size,"Clock unavailable");
}
static void session_details(const struct session_record *r)
{
    struct ui_stat rows[7];int n=0;char note[256],date[48];record_date(r,date,sizeof(date));
    rows[n].label="Host";snprintf(rows[n++].value,64,"%s",host_label(r->host));
    rows[n].label="Stream";snprintf(rows[n++].value,64,"%dx%d @ %d | %d Mbps",r->width,r->height,r->fps,r->bitrate/1000);
    rows[n].label="Shown FPS";snprintf(rows[n++].value,64,"%.2f",r->shown_milli/1000.0);
    rows[n].label="Decode average / p99";
    if(r->tail_valid)snprintf(rows[n++].value,64,ui_translate("%.2f / %.2f ms (tail window)"),r->decode_avg_us/1000.0,r->decode_p99_us/1000.0);
    else snprintf(rows[n++].value,64,"%.2f ms / N/A",r->decode_avg_us/1000.0);
    rows[n].label="RTT";if(r->rtt_valid)snprintf(rows[n++].value,64,"%u ms",r->rtt_ms);else snprintf(rows[n++].value,64,"N/A");
    rows[n].label="Connection";snprintf(rows[n++].value,64,"%s",performance_grade(r->grade));
    rows[n].label="Likely bottleneck";snprintf(rows[n++].value,64,"%s",performance_cause_name(r->cause));
    snprintf(note,sizeof(note),ui_translate("%s. Tail percentiles require telemetry. Low shown FPS alone does not prove network loss.%s"),date,
        history_save_failed?ui_translate(" History not saved to disk."):"");
    ui_draw_stats(r->app,rows,n,note);ui_present();wait_button();
}
static void history_menu(void)
{
    int location=ui_location_enter("Recent Sessions");
    int sel=0;
    if(!session_history.count) {message(ICON_INFO,"Recent Sessions","Play a session to create a summary. History is saved only after streaming stops.");{ui_location_restore(location);return;}}
    for(;;) {
        struct ui_setting rows[SESSION_HISTORY_MAX];char values[SESSION_HISTORY_MAX][128],help[SESSION_HISTORY_MAX][192];
        for(int i=0;i<session_history.count;i++) {
            const struct session_record *r=session_history.records+i;char date[48];record_date(r,date,sizeof(date));
            snprintf(values[i],sizeof(values[i]),"%dp%d | %d Mbps | %.0f FPS | %s",r->height,r->fps,r->bitrate/1000,r->shown_milli/1000.0,performance_grade(r->grade));
            snprintf(help[i],sizeof(help[i]),"%s | %s | %s",date,host_label(r->host),performance_cause_name(r->cause));
            rows[i].label=r->app;rows[i].value=values[i];rows[i].help=help[i];
        }
        ui_draw_list("Recent Sessions",rows,session_history.count,sel);ui_present();int key=wait_button();
        if(key&B_FLAG){ui_location_restore(location);return;}
        if(tab_key(key,UI_SETTINGS)){ui_location_restore(location);return;}
        if(key&UP_FLAG)sel=(sel+session_history.count-1)%session_history.count;
        if(key&DOWN_FLAG)sel=(sel+1)%session_history.count;
        if(key&A_FLAG)session_details(session_history.records+sel);
    }
}
static void advisor_menu(void)
{
    const struct session_record *r=latest_session();
    if(!r) {message(ICON_INFO,"Performance Advisor","No session summary for this host. Enable Session history in System and play a session.");return;}
    for(;;) {
        bool apply=r->grade==PERF_UNSTABLE && config_video_preset(&cfg)!=CONFIG_PROFILE_STANDARD;
        char mode[64],latency[40];snprintf(mode,sizeof(mode),"%dp%d | %d Mbps",r->height,r->fps,r->bitrate/1000);
        if(r->rtt_valid)snprintf(latency,sizeof(latency),"%u ms",r->rtt_ms);else snprintf(latency,sizeof(latency),"Not measured");
        ui_draw_performance(performance_grade(r->grade),mode,r->shown_milli/1000.0f,
            r->cause==PERF_CPU?"CPU limited":r->tail_valid?"See analysis":"Tail not measured",latency,
            performance_cause_name(r->cause),apply?"Recommended: Standard / 720p30 / 12 Mbps. Apply changes only the next connection.":
            "Analysis uses your last session. No new benchmark or automatic settings change.",apply);
        ui_present();int key=wait_button();if(key&B_FLAG)return;if(key&X_FLAG)session_details(r);
        if(tab_key(key,UI_SETTINGS))return;
        if((key&A_FLAG)&&apply) {config_apply_preset(&cfg,CONFIG_PROFILE_STANDARD);
            bool saved=config_save(&cfg);message(ICON_CHECK,"Safe profile selected",saved?"Standard / 720p30 / 12 Mbps. Applies on next connection.":"Applies next connection; settings could not be saved.");return;}
    }
}

static bool stream_once(int app_id, const char *app_title)
{
    uint32_t connection_begun=plat_now_ms();
    perf_flush_menu(); /* Cover worker is already joined by the caller. */
    perf_begin(cfg.diagnostic_log || cfg.stats_overlay);
    perf_mark(PERF_CONNECT_BEGIN);
    struct launch_job lj;
    struct connect_job cj;
    CONNECTION_LISTENER_CALLBACKS cl;
    DECODER_RENDERER_CALLBACKS dr;
    AUDIO_RENDERER_CALLBACKS ar;
    struct video_options vo;
    char title[128], msg[200];
    uint32_t started, elapsed, last_network_ms;
    bool local_exit, allow_adapt=false;
    int adapt_reason=0;
    struct adaptive_stream adaptation={0};
    struct config adapted=cfg;
    plat_atomic_store(&connection_poor,0);
    int video_error=VIDEO_STREAM_OK;

    stream_return_home = false;
    ui_drop_main_cache();

    memset(&lj, 0, sizeof(lj));
    LiInitializeStreamConfiguration(&lj.sc);
    lj.app_id = app_id;
    lj.sc.width = cfg.width;
    lj.sc.height = cfg.height;
    lj.sc.fps = cfg.fps;
    lj.sc.bitrate = cfg.bitrate_kbps;
    lj.sc.packetSize = cfg.packet_size;
    lj.sc.streamingRemotely = STREAM_CFG_AUTO;
    lj.sc.audioConfiguration = AUDIO_CONFIGURATION_STEREO;
#ifdef _XBOX
    lj.sc.audioConfiguration=audio_select_configuration(cfg.audio_mode==1);
    if(audio_fallback_reason())message(ICON_INFO,"Audio output",audio_fallback_reason());
#endif
    lj.sc.supportedVideoFormats = VIDEO_FORMAT_H264;
    lj.sc.clientRefreshRateX100 = plat_display_refresh_x100();
    lj.sc.colorSpace = video_colorspace(cfg.width, cfg.height);
    lj.sc.colorRange = COLOR_RANGE_LIMITED;
    /* Video decryption would cost CPU time we don't have; audio is cheap. */
    lj.sc.encryptionFlags = ENCFLG_AUDIO;

    snprintf(title, sizeof(title), ui_translate("Starting %s"), app_title);
    plat_diagnostics_checkpoint(title);
    snprintf(msg, sizeof(msg), ui_translate("%dx%d at %d FPS, %d Mbps"), cfg.width, cfg.height, cfg.fps, cfg.bitrate_kbps / 1000);
    run_busy(title, msg, launch_task, &lj);
    perf_mark(PERF_LAUNCH_RESPONSE);
    if (!lj.ok) {
        perf_flush();
        perf_begin(cfg.diagnostic_log || cfg.stats_overlay);
        plat_log(gs_error());
        plat_diagnostics_flush();
        message(ICON_ERROR, "Couldn't start the app",!server_ok && server_error[0]?server_error:gs_error());
        return false;
    }

    LiInitializeConnectionCallbacks(&cl);
    cl.stageStarting = cl_stage_starting;
    cl.stageComplete = cl_stage_complete;
    cl.stageFailed = cl_stage_failed;
    cl.connectionTerminated = cl_connection_terminated;
    cl.connectionStatusUpdate=cl_connection_quality;
    cl.logMessage = cl_log;
    cl.rumble = input_rumble;
    memset(&vo, 0, sizeof(vo));
    struct stream_options effective;
    config_profile_capture(&effective,&cfg);
    vo.fast_decode = effective.fast_decode!=0;
    vo.renderer = cfg.renderer;
    vo.vsync = cfg.vsync;
    vo.stats_overlay = cfg.stats_overlay && plat_has_diagnostics();
    vo.overlay_mode=config_overlay_mode(&cfg);
    vo.shader_mode=effective.image_quality;
    vo.decode_workers=cfg.decode_workers;vo.thread_layout=cfg.thread_layout;
    vo.connection_started_ms=connection_begun;
    vo.target_fps = cfg.fps;
    vo.bitrate_kbps = cfg.bitrate_kbps;
    vo.width = cfg.width;
    vo.height = cfg.height;
    vo.slice_count = cfg.slice_count;
    vo.color_space = lj.sc.colorSpace;
    video_set_options(&vo);
    video_init_callbacks(&dr);
    audio_init_callbacks(&ar);

    connection_terminated = false;
    termination_error = 0;
    ui_suspended = false;
    snprintf(stage_text, sizeof(stage_text), "Connecting...");
    cj.sc = &lj.sc;
    cj.cl = &cl;
    cj.dr = &dr;
    cj.ar = &ar;
    cj.ret = -1;
    snprintf(title, sizeof(title), ui_translate("Connecting to %s"), server.hostname[0] ? server.hostname : server.address);
    run_task(connect_task, &cj, draw_connecting, title);
#ifdef _XBOX
    if(cj.ret!=0 && lj.sc.audioConfiguration==AUDIO_CONFIGURATION_51_SURROUND) {
        /* LiStartConnection unwinds failed initialization before returning. Retry once
         * with stereo, keeping the user's saved surround preference for future sessions. */
        ui_suspended=false;
        message(ICON_INFO,"Audio output","Surround connection failed. Retrying once with stereo.");
        lj.sc.audioConfiguration=audio_select_configuration(false);
        connection_terminated=false;termination_error=0;
        snprintf(stage_text,sizeof(stage_text),"Connecting...");
        run_task(connect_task,&cj,draw_connecting,title);
    }
#endif

    if (cj.ret != 0) {
        perf_flush();
        perf_begin(cfg.diagnostic_log || cfg.stats_overlay);
        plat_connection_trace_flush();
        ui_suspended = false;
        plat_diagnostics_flush();
        snprintf(msg, sizeof(msg), ui_translate("%s\n\nCheck that UDP ports 47998-48010 aren't blocked by the PC's firewall."),
                 stage_text);
        message(ICON_ERROR, "Couldn't connect", msg);
        return false;
    }

    plat_diagnostics_checkpoint("Releasing menu GPU textures for streaming\n");
    plat_release_menu_textures();
    input_configure(cfg.quit_combo, cfg.stick_deadzone);
    input_calibrate(cfg.left_min,cfg.left_max,cfg.right_min,cfg.right_max);
#ifdef _XBOX
    ui_set_stream_hud(cfg.stream_menu_style,cfg.mouse_hud);
    input_configure_pointer(cfg.mouse_speed,cfg.mouse_acceleration,cfg.mouse_scroll_ms,cfg.stats_overlay);
#endif
    started = plat_now_ms();
    last_network_ms = started - 1000;
    adaptation.started=started;
    plat_diagnostics_checkpoint("Streaming active; no clean return recorded yet\n");
    plat_diagnostics_flush();
    input_stream_start();
    while (!connection_terminated && !input_quit_requested()) {
        video_error=video_get_stream_error();
        if(video_error!=VIDEO_STREAM_OK)break;
        uint32_t now = plat_now_ms();
        if ((vo.stats_overlay || cfg.history_enabled || cfg.adaptive_quality) && now - last_network_ms >= 1000) {
            uint32_t rtt = 0, variance = 0;
            bool valid = LiGetEstimatedRttInfo(&rtt, &variance);
            video_set_network_stats(rtt, variance, valid, LiGetPendingVideoFrames(), LiGetPendingAudioDuration());
            last_network_ms = now;
            if(cfg.adaptive_quality && adaptive_reconnects<3) {
                struct video_stats measured;video_get_stats(&measured);
                int reason=adaptive_stream_sample(&adaptation,now,plat_atomic_load(&connection_poor)!=0,
                    (unsigned)LiGetPendingVideoFrames(),measured.avg_decode_us,cfg.fps,plat_system_ui_active());
                if(reason && adaptive_stream_lower(reason,&adapted.width,&adapted.height,&adapted.bitrate_kbps)) {
                    adapt_reason=reason;break;
                }
            }
        }
        plat_sleep_ms(50);
    }
    video_error=video_get_stream_error();
    local_exit = input_quit_requested() && !connection_terminated && video_error==VIDEO_STREAM_OK;
    allow_adapt=adapt_reason && !local_exit && !connection_terminated;
    plat_shutdown_stage("Stopping controller forwarding\n");
    perf_mark(PERF_STOP_BEGIN);
    input_stream_stop();
    elapsed = plat_now_ms() - started;
    plat_shutdown_stage("Stopping Moonlight transport\n");
    plat_diagnostics_flush();
    LiStopConnection();
    perf_mark(PERF_RETURN_MENU);
    perf_flush();
    perf_begin(cfg.diagnostic_log || cfg.stats_overlay);
    plat_connection_trace_flush();
    plat_shutdown_stage("Transport stopped; returning to Moonlight\n");
    ui_suspended = false;
    plat_diagnostics_checkpoint(local_exit ? "Clean local return to Moonlight\n" : "Stream ended; see termination error in log\n");
    plat_diagnostics_flush();

#ifdef _XBOX
    if(audio_fallback_reason())message(ICON_INFO,"Audio output",audio_fallback_reason());
#endif
    save_session_report(elapsed);
    record_session(app_id,app_title,elapsed);
    if(video_error!=VIDEO_STREAM_OK) {
        message(ICON_ERROR,"Video stream stopped",video_error==VIDEO_STREAM_UNSUPPORTED_SIZE?
            "The game changed to a resolution above 1920x1080 or an invalid size. Set the PC game to 1080p or lower, then reconnect.":
            "Not enough resources to display the video. The connection was closed safely; reconnect with a lower resolution.");
        stream_return_home=true;
        return false;
    }
    if(allow_adapt) {
        cfg=adapted;cfg.stream_profile=0;cfg.fast_decode=true;cfg.image_quality=1;
        adaptive_reconnects++;
        ui_draw_busy("Adapting stream","Reconnecting with a lighter video setting...",plat_now_ms());ui_present();
        return true;
    }
    if (local_exit) {
        stream_return_home = true;
        return false;
    }
    return show_stream_stats(elapsed);
}

static void stream(int app_id, const char *app_title)
{
    struct config requested=cfg;
    int binding=binding_find(&game_bindings,host_key(),app_id);
    if(binding>=0) {
        const char *name=game_bindings.items[binding].profile;
        if(!strncmp(name,"factory:",8))config_apply_preset(&cfg,atoi(name+8));
        else if(!strncmp(name,"user:",5)) {
            struct user_profiles profiles;memset(&profiles,0,sizeof(profiles));bool applied=false;
            if(config_profiles_load(&profiles))for(int i=0;i<profiles.count;i++)if(!strcmp(profiles.items[i].name,name+5))applied=config_profiles_apply(&profiles,i,&cfg);
            if(!applied)message(ICON_INFO,"Game profile unavailable","The assigned saved profile is missing or unreadable. Current stream settings will be used.");
        }
    }
    adaptive_reconnects=0;
    char stable_title[64];history_text(stable_title,sizeof(stable_title),app_title);
    while (stream_once(app_id, stable_title)) {
        /* The old transport and decoder are fully stopped before a fresh resume. */
        refresh_server();
        if (!server_ok || !server.paired) {
            message(ICON_ERROR, "Couldn't reconnect", server_ok ? "Pair with the host again." : server_error);
            break;
        }
        if (server.current_game && server.current_game != app_id) {
            message(ICON_INFO, "Another app is running", "Return to the app list to choose the running app.");
            break;
        }
    }
    cfg=requested; /* adaptation never overwrites the saved/manual preference */
}

/* ------------------------------------------------------------ apps */

static volatile int art_cancel,art_request=-1,art_ready=-1;
static uint32_t *art_result;
static int art_version;
static bool art_failed[MAX_APPS];
static unsigned art_used[MAX_APPS],art_clock;

static void *art_loader(void *arg)
{
    int tw, th;
    (void)arg;

    ui_art_size(&tw, &th);
    while(!plat_atomic_load(&art_cancel)) {
        int i=plat_atomic_load(&art_request);
        if(i<0 || plat_atomic_load(&art_ready)>=0) {plat_sleep_ms(4);continue;}
        unsigned char *png=NULL;size_t len=0;uint32_t *scaled=NULL;
        uint64_t fetched=perf_enabled()?perf_clock():0;
        bool available=gs_app_art(&server,apps[i].id,&png,&len);
        if(fetched)perf_record(PERF_COVER_HTTP,perf_clock()-fetched);
        if(available)scaled=plat_decode_art(png,len,tw,th);
        free(png);
        if(plat_atomic_load(&art_cancel)){free(scaled);break;}
        art_result=scaled;plat_atomic_store(&art_ready,i);
        while(plat_atomic_load(&art_ready)>=0 && !plat_atomic_load(&art_cancel))plat_sleep_ms(4);
    }
    return NULL;
}

static void art_memory(void) {
    unsigned count=0;int w,h;ui_art_size(&w,&h);
    for(int i=0;i<app_count;i++)if(app_art[i])count++;
    ui_draw_cover_memory(count,count*w*h*4);
}
static void art_update(const int *map,int count,int selected,int columns) {
    int completed=plat_atomic_load(&art_ready);
    if(completed>=0) {
        uint32_t *result=art_result;art_result=NULL;
        plat_atomic_store(&art_request,-1);plat_atomic_store(&art_ready,-1);
        if(result) {
            unsigned loaded=0;int oldest=-1;
            for(int i=0;i<app_count;i++)if(app_art[i]) {loaded++;if(oldest<0||art_used[i]<art_used[oldest])oldest=i;}
            if(loaded>=UI_COVER_LIMIT && oldest>=0) {free(app_art[oldest]);app_art[oldest]=NULL;}
            app_art[completed]=result;art_generation[completed]=(uint32_t)++art_version;art_used[completed]=++art_clock;
        } else {art_failed[completed]=true;art_version++;}
        art_memory();
    }
    int first,n=ui_cover_window(selected,count,columns,&first);
    /* Touch the whole visible window before selecting a replacement. */
    for(int j=first;j<first+n;j++)if(app_art[map[j]])art_used[map[j]]=++art_clock;
    if(plat_atomic_load(&art_request)>=0 || plat_atomic_load(&art_ready)>=0)return;
    for(int j=first;j<first+n;j++) {int i=map[j];
        if(!app_art[i] && !art_failed[i]) {plat_atomic_store(&art_request,i);return;}}
}

static void *applist_task(void *arg)
{
    uint32_t begun=plat_now_ms();plat_log("[NET] app list start\n");
    int *n = (int *)arg;
    *n = gs_applist(&server, apps, MAX_APPS);
    if(*n>=0)app_list_cache_store(&app_cache,host_key(),plat_now_ms());else app_cache.valid=false;
    cl_log("[NET] app list done %u ms (apps=%d)\n",plat_now_ms()-begun,*n);
    return NULL;
}

static void clear_apps(void)
{
    int i;
    ui_drop_main_cache(); /* cached home pixels may contain these covers */

    for (i = 0; i < MAX_APPS; i++) {
        free(app_art[i]);
        app_art[i] = NULL;
    }
    app_count = 0;app_cache.valid=false;
    memset(art_failed,0,sizeof(art_failed));memset(art_used,0,sizeof(art_used));
    ui_draw_cover_memory(0,0);
}

static int library_map(int filter,int map[MAX_APPS])
{
    int n=0;
    if(filter==0) {
        for(int h=0;h<session_history.count;h++) {
            const struct session_record *r=session_history.records+h;if(strcmp(r->host,host_key()))continue;
            for(int i=0;i<app_count;i++)if(apps[i].id==r->app_id) {
                bool duplicate=false;for(int j=0;j<n;j++)if(map[j]==i)duplicate=true;
                if(!duplicate)map[n++]=i;break;
            }
        }
    } else for(int i=0;i<app_count;i++) {
        if(filter==1 && !library_favorite_has(&favorites,host_key(),apps[i].id))continue;
        if(filter==3 && !library_desktop(apps[i].name))continue;
        map[n++]=i;
    }
    if(library_query[0]){int kept=0;for(int j=0;j<n;j++)if(feature_search(apps[map[j]].name,library_query))map[kept++]=map[j];n=kept;}
    return n;
}
static void draw_apps(int sel,int filter,const int *map,int count)
{
    static const char *filters[]={"Recent","Favorites","All Apps","Desktop"};
    struct ui_app views[MAX_APPS];struct ui_host h=host_view();
    for(int i=0;i<count;i++) {int index=map[i];views[i].name=apps[index].name;views[i].art=app_art[index];
        views[i].generation=art_generation[index];
        views[i].running=server.current_game==apps[index].id;
        views[i].favorite=library_favorite_has(&favorites,host_key(),apps[index].id);}
    char title[100];snprintf(title,sizeof(title),"%s%s%s",ui_translate(filters[filter]),library_query[0]?" / ":"",library_query);ui_draw_library(&h,views,count,sel,title);ui_focus_tick(plat_now_ms());ui_present();
}

static void stop_art_loader(plat_thread **loader)
{
    if (*loader) {
        plat_atomic_store(&art_cancel,1);
        plat_thread_join(*loader);
        *loader = NULL;
    }
    free(art_result);art_result=NULL;plat_atomic_store(&art_request,-1);plat_atomic_store(&art_ready,-1);
}

static bool app_details_for(int id,const char *name,int index) {
    int selected=0,location=ui_location_enter_raw(name);
    for(;;) {
        struct ui_host h=host_view();struct ui_app a;
        a.name=name;a.art=index>=0?app_art[index]:NULL;a.running=server.current_game==id;
        a.favorite=library_favorite_has(&favorites,host_key(),id);a.generation=index>=0?art_generation[index]:0;
        ui_draw_app_details(&h,&a,game_profile_label(id),selected);ui_present();
        int key=wait_button();if(key&B_FLAG)return (ui_location_restore(location),false);
        if(tab_key(key,UI_LIBRARY))return (ui_location_restore(location),false);
        if(key&UP_FLAG)selected=(selected+4)%5;if(key&DOWN_FLAG)selected=(selected+1)%5;
        if(key&A_FLAG) {
            if(!selected)return (ui_location_restore(location),true);
            if(selected==1){game_profile_menu(id);if(navigation_pending>=0)return (ui_location_restore(location),false);}
            if(selected==2) {
                if(library_favorite_toggle(&favorites,host_key(),id)) {
                    char text[4096];size_t len=library_favorites_encode(&favorites,text,sizeof(text));
                    if(!len||!storage_write("favorites.tsv",text,len))message(ICON_INFO,"Favorite kept for this run","Couldn't save favorites.");
                } else message(ICON_INFO,"Favorites full","Remove a favorite first (limit 32).");
            }
            if(selected==3) {
                const struct session_record *last=NULL;
                for(int i=0;i<session_history.count;i++)if(session_history.records[i].app_id==id &&
                    !strcmp(session_history.records[i].host,host_key())){last=session_history.records+i;break;}
                if(last)session_details(last);
                else message(ICON_INFO,"No sessions for this app","Play this app with Session history enabled to see its measured performance.");
            }
            if(selected==4) {
                if(server.current_game!=id) {message(ICON_INFO,"App is not running","This app has no active session on the PC.");continue;}
                bool ok=false;run_busy("Stopping app",name,quit_task,&ok);
                if(ok){server.current_game=0;refresh_server();}
                else message(ICON_ERROR,"Couldn't stop app",gs_error());
            }
        }
    }
}

static bool app_details(int index) {return app_details_for(apps[index].id,apps[index].name,index);}

static void library_context(int *filter)
{
    int location=ui_location_enter("Menu");
    static const char *names[]={"Recent","Favorites","All Apps","Desktop"};
    int sel=0;
    for(;;) {
        struct ui_setting rows[]={{"View",names[*filter],"A cycles the library filter."},
            {"Host actions","Open","Refresh, pair, quit or unpair the current host."},
            {"Search library",library_query[0]?library_query:"All Apps","Edit the search text with the controller. Empty text shows all games."},
            {"Clear search","Reset","Keep the current library filter and clear the search."}};
        ui_draw_settings_page("Library menu",rows,4,sel);ui_present();
        int key=wait_button();if((key&B_FLAG)||tab_key(key,UI_LIBRARY)){ui_location_restore(location);return;}
        if(key&(UP_FLAG|DOWN_FLAG))sel=(sel+((key&UP_FLAG)?3:1))%4;
        if(!sel && (key&(LEFT_FLAG|RIGHT_FLAG)))*filter=(*filter+((key&LEFT_FLAG)?3:1))%4;
        if(key&A_FLAG) {if(!sel)*filter=(*filter+1)%4;else if(sel==1)host_options_menu();else if(sel==2){char query[64];snprintf(query,sizeof(query),"%s",library_query[0]?library_query:" ");if(edit_profile_name(query)){if(!strcmp(query," "))query[0]=0;strcpy(library_query,query);}}else library_query[0]=0;}
    }
}
static void app_menu(void)
{
    plat_thread *loader=NULL;int count=-1,sel=0,seen_version=-1,cols=ui_apps_columns(),filter=2,map[MAX_APPS];
    char title[96];snprintf(title,sizeof(title),ui_translate("Loading apps from %s"),server.hostname[0]?server.hostname:server.address);
    if(server_ok && server.paired && app_list_cache_valid(&app_cache,host_key(),plat_now_ms()))count=app_count;
    else {clear_apps();run_busy(title,NULL,applist_task,&count);}
    if(count<0) {message(ICON_ERROR,"Couldn't load the app list",gs_error());return;}
    app_count=count;for(int i=0;i<app_count;i++)if(apps[i].id==server.current_game)sel=i;
    plat_atomic_store(&art_cancel,0);loader=plat_thread_start(art_loader,NULL);
    for(;;) {
        if(seen_version<0){count=library_map(filter,map);if(sel>=count)sel=count?count-1:0;}
        if(loader)art_update(map,count,sel,cols);
        if(seen_version!=art_version) {seen_version=art_version;draw_apps(sel,filter,map,count);}
        int key=input_menu_pressed();if(!key) {if(ui_focus_tick(plat_now_ms()))ui_present();plat_sleep_ms(16);continue;}
        if(key&B_FLAG)break;
        if(tab_key(key,UI_LIBRARY))break;
        if(key&PLAY_FLAG) {stop_art_loader(&loader);library_context(&filter);
            if(navigation_pending>=0)break;
            plat_atomic_store(&art_cancel,0);loader=plat_thread_start(art_loader,NULL);seen_version=-1;continue;}
        if(key&Y_FLAG) {
            stop_art_loader(&loader);clear_apps();count=-1;run_busy(title,NULL,applist_task,&count);
            if(count<0){message(ICON_ERROR,"Couldn't refresh apps",gs_error());break;}
            app_count=count;sel=0;plat_atomic_store(&art_cancel,0);loader=plat_thread_start(art_loader,NULL);seen_version=-1;continue;
        }
        if(count) {
            if(key&LEFT_FLAG)sel=ui_grid_move(sel,count,cols,-1,0);if(key&RIGHT_FLAG)sel=ui_grid_move(sel,count,cols,1,0);
            if(key&UP_FLAG)sel=ui_grid_move(sel,count,cols,0,-1);if(key&DOWN_FLAG)sel=ui_grid_move(sel,count,cols,0,1);
            int index=map[sel];
            bool play=(key&A_FLAG)!=0;
            if(key&X_FLAG) {
                stop_art_loader(&loader);play=app_details(index);
                if(!play && navigation_pending<0) {
                    plat_atomic_store(&art_cancel,0);loader=plat_thread_start(art_loader,NULL);
                }
            }
            if(navigation_pending>=0)break;
            if(play) {
                if(server.current_game && server.current_game!=apps[index].id)
                    message(ICON_INFO,"Another app is running","Open the running app with X, then choose Stop app.");
                else {stop_art_loader(&loader);stream(apps[index].id,apps[index].name);
                    if(stream_return_home)break;refresh_task(NULL);}
            }
        }
        seen_version=-1;
    }
    stop_art_loader(&loader);
}

/* ------------------------------------------------------------ main menu */

static void *quit_task(void *arg)
{
    bool *ok = (bool *)arg;
    *ok = gs_quit(&server);
    return NULL;
}

static void *unpair_task(void *arg)
{
    bool *ok = (bool *)arg;
    *ok = gs_unpair(&server);
    return NULL;
}

static void set_item(struct ui_menu_item *it, const char *title, const char *subtitle, int icon, bool enabled)
{
    it->title = title;
    it->subtitle = subtitle;
    it->icon = icon;
    it->enabled = enabled;
}

static void ui_memory_menu(void)
{
    struct ui_memory_info info;ui_draw_memory(&info);struct ui_stat rows[7];
    rows[0].label="GPU UI textures (pixel payload)";snprintf(rows[0].value,64,"%u KiB",info.gpu_bytes/1024);
    rows[1].label="GPU glyph atlas";snprintf(rows[1].value,64,"%u KiB",info.glyph_bytes/1024);
    rows[2].label="Cover cache CPU / GPU";snprintf(rows[2].value,64,"%u / %u KiB",info.cover_cpu_bytes/1024,info.cover_gpu_bytes/1024);
    rows[3].label="Loaded covers CPU / GPU";snprintf(rows[3].value,64,"%u / %u",info.loaded_covers,info.gpu_covers);
    rows[4].label="Button atlas CPU / GPU";snprintf(rows[4].value,64,"%u / %u bytes",info.button_cpu_bytes,info.button_gpu_bytes);
    rows[5].label="Commands / fixed command storage";snprintf(rows[5].value,64,"%u / %u KiB",info.commands,info.command_bytes/1024);
    rows[6].label="Software frame fallbacks";snprintf(rows[6].value,64,"%u",info.software_fallbacks);
    ui_draw_stats(ui_translate("UI resources"),rows,7,"Logical texture payload excludes XDK allocation padding. Menu textures are released before video starts.");ui_present();wait_button();
}

static void network_diagnostics_menu(void)
{
    struct performance_sample sample;video_get_session_performance(&sample);
    struct ui_stat rows[8];
    rows[0].label="Incoming H.264 payload (last session)";
    if(sample.tail_valid)snprintf(rows[0].value,64,"%.2f Mbps",sample.payload_mbps);
    else snprintf(rows[0].value,64,"Unavailable (enable Detailed overlay)");
    rows[1].label="Packet loss";snprintf(rows[1].value,64,"Unavailable");
    rows[2].label="FEC recovered";snprintf(rows[2].value,64,"Unavailable");
    rows[3].label="FEC unrecoverable";snprintf(rows[3].value,64,"Unavailable");
    rows[4].label="Queue age / peak (last session)";
    if(sample.frames)snprintf(rows[4].value,64,"%.2f / %.2f ms",sample.queue_us/1000.0,sample.queue_peak_us/1000.0);
    else snprintf(rows[4].value,64,"Unavailable");
    rows[5].label="Frames dropped";
    if(sample.frames)snprintf(rows[5].value,64,"%u",sample.drops);else snprintf(rows[5].value,64,"Unavailable");
    rows[6].label="Network RTT";
    if(sample.rtt_valid)snprintf(rows[6].value,64,"%u ms",sample.rtt_ms);else snprintf(rows[6].value,64,"Unavailable");
    rows[7].label="Network jitter";snprintf(rows[7].value,64,"Unavailable");
    ui_draw_stats(ui_translate("Network diagnostics"),rows,8,"Last stopped session. H.264 payload excludes network overhead. Frame gaps are not packet loss.");
    ui_present();int key=wait_button();tab_key(key,UI_SETTINGS);
}

/* Wake operations use a private server object and never execute on the UI owner. */
struct wake_job {
    struct cached_host host;
    volatile int cancel,done,phase;
    bool replied;
};
static void *wake_task(void *arg)
{
    struct wake_job *j=(struct wake_job*)arg;
    if(!wake_send(j->host.mac)){plat_atomic_store(&j->phase,-1);plat_atomic_store(&j->done,1);return NULL;}
    plat_atomic_store(&j->phase,1);
    uint32_t start=plat_now_ms();
    while(!plat_atomic_load(&j->cancel) && plat_now_ms()-start<30000) {
        struct gs_server reply;memset(&reply,0,sizeof(reply));
        int remaining=30000-(int)(plat_now_ms()-start);
        if(remaining<=0)break;
        j->replied=gs_wake_probe(&reply,j->host.ip,j->host.port,remaining<1200?remaining:1200,&j->cancel);
        /* A response from a different UUID at a reused address is not this PC. */
        if(j->replied && !wake_identity(j->host.uuid,reply.uuid))j->replied=false;
        gs_server_free(&reply);
        if(j->replied)break;
        for(int i=0;i<10 && !plat_atomic_load(&j->cancel) && plat_now_ms()-start<30000;i++)plat_sleep_ms(50);
    }
    plat_atomic_store(&j->done,1);return NULL;
}
static bool wake_host(void)
{
    int index=selected_host_index();
    if(index<0)return false;
    struct wake_job job;memset(&job,0,sizeof(job));job.host=hosts.hosts[index];
    unsigned char mac[6];
    if(!wake_mac(job.host.mac,mac)){message(ICON_INFO,"Wake PC","Set a valid host MAC first.");return false;}
    plat_thread *thread=plat_thread_start(wake_task,&job);
    if(!thread){message(ICON_ERROR,"Wake PC","Could not start the wake worker.");return false;}
    while(!plat_atomic_load(&job.done)) {
        if(input_menu_pressed()&B_FLAG)plat_atomic_store(&job.cancel,1);
        const char *status=plat_atomic_load(&job.cancel)?"Cancelling...":plat_atomic_load(&job.phase)==1?
            "Packet sent. Waiting up to 30 seconds for the server. B cancels.":"Sending wake packet...";
        ui_draw_busy("Wake PC",status,plat_now_ms());ui_present();plat_sleep_ms(16);
    }
    plat_thread_join(thread);
    if(plat_atomic_load(&job.cancel))return false;
    if(job.replied) {
        hosts.hosts[index].online=true;next_discovery=0;
        message(ICON_INFO,"PC responded","The selected GameStream server is available.");return true;
    }
    else message(ICON_INFO,"Wake PC",plat_atomic_load(&job.phase)<0?"Could not send the wake packet.":
        "Packet sent, but the PC did not respond. Check PC Wake-on-LAN support, power and network settings.");
    return false;
}
static void edit_host_mac(void)
{
    int index=selected_host_index();
    if(index<0)return;
    unsigned char bytes[6]={0};wake_mac(hosts.hosts[index].mac,bytes);int cursor=0;
    for(;;) {
        char text[64],mac[18];snprintf(mac,sizeof(mac),"%02X:%02X:%02X:%02X:%02X:%02X",bytes[0],bytes[1],bytes[2],bytes[3],bytes[4],bytes[5]);
        snprintf(text,sizeof(text),"%s  [%d/12]",mac,cursor+1);
        struct ui_setting rows[]={{"Host MAC",text,"Left/Right: digit. Up/Down: hex value. A saves; B cancels."},
            {"Wake-on-LAN","PC setup","Requires PC firmware, network adapter and power settings that support Wake-on-LAN."}};
        ui_draw_settings_page("Host MAC",rows,2,0);ui_present();int key=wait_button();
        if(key&B_FLAG)return;
        if(key&LEFT_FLAG)cursor=(cursor+11)%12;if(key&RIGHT_FLAG)cursor=(cursor+1)%12;
        if(key&(UP_FLAG|DOWN_FLAG)) {
            int shift=(cursor&1)?0:4, digit=(bytes[cursor/2]>>shift)&15;
            digit=(digit+((key&UP_FLAG)?1:15))&15;
            bytes[cursor/2]=(unsigned char)((bytes[cursor/2]&~(15<<shift))|(digit<<shift));
        }
        if(key&A_FLAG) {
            unsigned char valid[6];if(!wake_mac(mac,valid)){message(ICON_ERROR,"Host MAC","Enter a nonzero unicast MAC address.");continue;}
            strcpy(hosts.hosts[index].mac,mac);hosts.hosts[index].mac_manual=true;save_hosts();
            if(host_cache_save_failed)message(ICON_ERROR,"Host MAC","MAC kept for this run. Could not save hosts.tsv.");
            return;
        }
    }
}
static void connection_diagnosis_menu(void)
{
    struct ui_setting rows[]={{"Host",cfg.host,"Selected network address."},
        {"Availability",server_ok?"Online":"Offline",feature_diagnosis(server_error[0]?server_error:gs_error(),server_ok,server_ok && server.paired)},
        {"Pairing",server_ok && server.paired?"Paired":"Pairing required","This diagnosis does not change host configuration or permissions."}};
    ui_draw_settings_page("Connection diagnosis",rows,3,1);ui_present();while(!(wait_button()&(A_FLAG|B_FLAG))){}
}
static void manage_host(void)
{
    int index=selected_host_index();
    if(index<0)return;int selected=0;
    for(;;) {
        struct cached_host *h=hosts.hosts+index;
        struct ui_setting rows[]={
            {"Host nickname",h->alias[0]?h->alias:h->name,"A local display name. The server identity and pairing stay unchanged."},
            {"Default host",h->is_default?"On":"Off","Select this PC at application startup."},
            {"Wake when connecting",h->auto_wake?"On":"Off","Optional. An offline host is sent a wake packet before connecting. B cancels."},
            {"Host UUID",h->uuid[0]?h->uuid:"Unavailable","Stable identity received from this host."},
            {"IP address",h->ip,"Address used by discovery and connection."},
            {"Host MAC",h->mac[0]?h->mac:"Edit","Set the MAC for the network adapter that supports Wake-on-LAN."},
            {"Wake PC","Send packet","Packet sent and PC responded are different results."},
            {"Remove saved host","Confirm...","Removes this cache entry. Certificates are preserved; discovery may find the PC again."},
            {"Connection diagnosis","Open","Review availability, pairing and recommended checks."}};
        int kinds[]={UI_OPTION_ACTION,UI_OPTION_TOGGLE,UI_OPTION_TOGGLE,UI_OPTION_INFO,UI_OPTION_INFO,UI_OPTION_OPEN,UI_OPTION_ACTION,UI_OPTION_ACTION,UI_OPTION_OPEN};
        ui_draw_settings_options("Host settings",rows,kinds,9,selected);ui_present();int key=wait_button();
        if(key&B_FLAG)return;if(key&UP_FLAG)selected=(selected+8)%9;if(key&DOWN_FLAG)selected=(selected+1)%9;
        if(!(key&A_FLAG))continue;
        if(selected==0){char name[64];snprintf(name,sizeof(name),"%s",h->alias[0]?h->alias:h->name[0]?h->name:"PC");if(edit_profile_name(name))strcpy(h->alias,name);}
        if(selected==1){bool next=!h->is_default;for(int i=0;i<hosts.count;i++)hosts.hosts[i].is_default=false;h->is_default=next;}
        if(selected==2)h->auto_wake=!h->auto_wake;
        if(selected==5)edit_host_mac();if(selected==6)wake_host();if(selected==8)connection_diagnosis_menu();
        if(selected==7) {
            int confirm=0;struct ui_setting options[]={{"Cancel","Back",NULL},{"Remove saved host",h->alias[0]?h->alias:h->ip,"Only this saved host entry is removed."}};
            for(;;){ui_draw_settings_page("Remove saved host?",options,2,confirm);ui_present();key=wait_button();if(key&B_FLAG){confirm=0;break;}if(key&(UP_FLAG|DOWN_FLAG))confirm=!confirm;if(key&A_FLAG)break;}
            if(confirm){memmove(h,h+1,(hosts.count-index-1)*sizeof(*h));hosts.count--;cfg.host[0]=0;cfg.host_uuid[0]=0;server_ok=false;clear_apps();save_hosts();bool saved=config_save(&cfg);if(host_cache_save_failed || !saved)message(ICON_ERROR,"Host settings","Host removed for this run. Could not persist the removal.");return;}
        }
        if(selected<=2){save_hosts();if(host_cache_save_failed)message(ICON_ERROR,"Host settings","Could not save hosts.tsv. Previous file is recoverable from backup.");}
    }
}
static void game_profile_menu(int id)
{
    struct user_profiles profiles;memset(&profiles,0,sizeof(profiles));config_profiles_load(&profiles);
    int selected=0,index=binding_find(&game_bindings,host_key(),id);
    if(index>=0){const char *token=game_bindings.items[index].profile;
        if(!strncmp(token,"factory:",8))selected=atoi(token+8);
        else if(!strncmp(token,"user:",5))for(int i=0;i<profiles.count;i++)if(!strcmp(token+5,profiles.items[i].name))selected=CONFIG_PROFILE_COUNT+i;
    }
    for(;;) {
        struct ui_setting rows[CONFIG_PROFILE_COUNT+CONFIG_USER_PROFILE_MAX];
        rows[0].label="Use current settings";rows[0].value="Inherit";rows[0].help="Remove the automatic profile for this game.";
        for(int i=1;i<CONFIG_PROFILE_COUNT;i++){rows[i].label="Factory profile";rows[i].value=config_preset_name(i);rows[i].help="Apply this factory profile when launching this game.";}
        for(int i=0;i<profiles.count;i++){rows[CONFIG_PROFILE_COUNT+i].label="Saved profile";rows[CONFIG_PROFILE_COUNT+i].value=profiles.items[i].name;rows[CONFIG_PROFILE_COUNT+i].help="Apply this named profile when launching this game. Reassign it if the profile is renamed or deleted.";}
        int count=CONFIG_PROFILE_COUNT+profiles.count;
        ui_draw_settings_page("Game stream profile",rows,count,selected);ui_present();int key=wait_button();
        if(key&B_FLAG)return;if(key&UP_FLAG)selected=(selected+count-1)%count;if(key&DOWN_FLAG)selected=(selected+1)%count;
        if(key&A_FLAG) {
            char token[80]="";if(selected>0 && selected<CONFIG_PROFILE_COUNT)snprintf(token,sizeof(token),"factory:%d",selected);
            if(selected>=CONFIG_PROFILE_COUNT)snprintf(token,sizeof(token),"user:%s",profiles.items[selected-CONFIG_PROFILE_COUNT].name);
            struct game_bindings next=game_bindings;char text[8192];
            if(!binding_set(&next,host_key(),id,token)){message(ICON_ERROR,"Game profile not saved","The binding file is protected, unreadable or full.");continue;}
            size_t n=bindings_encode(&next,text,sizeof(text));
            if(!n || !storage_write("game-bindings.tsv",text,n)){message(ICON_ERROR,"Game profile not saved","Check available storage. The previous file was preserved.");continue;}
            game_bindings=next;message(ICON_CHECK,"Game profile saved","Applies when this game is launched. Global stream settings are preserved.");return;
        }
    }
}
static void host_options_menu(void)
{
    int location=ui_location_enter("Host actions");
    int sel=0;
    for(;;) {
        struct ui_setting rows[8]={
            {"Refresh host","Check",cfg.host},
            {"Pair with host",server_ok && server.paired?"Paired":"Pairing required","Enter the PIN in Vibepollo or Sunshine."},
            {"Stop app",server.current_game?"Stop on PC":"Not running","Stops the app on the selected PC."},
            {"Unpair","Remove pairing","Forget this console on this host."},
            {"Wake PC","Send packet","Requires PC firmware, network adapter and power settings that support Wake-on-LAN."},
            {"Host MAC","Edit","Set a valid host MAC first."},
            {"Host settings","Open","Nickname, default host, automatic wake and removal."},
            {"Connection diagnosis","Open","Review availability, pairing and recommended checks."}
        };
        ui_draw_settings_page("Host actions",rows,8,sel);ui_present();int key=wait_button();
        if((key&B_FLAG) || tab_key(key,UI_SETTINGS)){ui_location_restore(location);return;}
        if(key&UP_FLAG)sel=(sel+7)%8;if(key&DOWN_FLAG)sel=(sel+1)%8;
        if(!(key&(A_FLAG|PLAY_FLAG)))continue;
        if(!cfg.host[0]) {message(ICON_INFO,"Choose a host","Press Y on Home to choose or add a host.");continue;}
        if(sel==0)refresh_server();
        else if(sel==1) {if(server_ok && !server.paired)do_pair();refresh_server();}
        else if(sel==4)wake_host();
        else if(sel==5)edit_host_mac();
        else if(sel==6)manage_host();
        else if(sel==7)connection_diagnosis_menu();
        else {
            bool ok=false;
            if(server_ok && server.paired)refresh_server();
            if(!server_ok || !server.paired) {message(ICON_INFO,"Host unavailable","Connect and pair with this host first.");continue;}
            run_busy(sel==2?"Quitting app":"Unpairing",NULL,sel==2?quit_task:unpair_task,&ok);
            if(!ok)message(ICON_ERROR,"Host request failed",gs_error());
            refresh_server();
        }
    }
}
/* Details/context stays explicit; A on Home never opens it as an intermediate step. */
static bool host_details(void)
{
    int sel=0,location=ui_location_enter("Host details");
    for(;;) {
        struct ui_host h=host_view();char mode[100];
        snprintf(mode,sizeof(mode),"%s | %dx%d | %d FPS | %d Mbps",config_preset_name(config_video_preset(&cfg)),cfg.width,cfg.height,cfg.fps,cfg.bitrate_kbps/1000);
        struct ui_setting rows[]={
            {"Play / Connect",server_ok?(server.paired?"Ready":"Pair first"):"Check host",mode},
            {"Host",h.name?h.name:cfg.host,cfg.host},
            {"Status",server_ok?(server.paired?"Online / Paired":"Online / Unpaired"):"Offline",h.detail?h.detail:""},
            {"Host actions","Open","Refresh, pair, quit or unpair"}
        };
        ui_set_page(UI_HOME);ui_draw_settings_page("Host details",rows,4,sel);ui_present();
        int key=wait_button();if((key&B_FLAG)||tab_key(key,UI_HOME))return (ui_location_restore(location),false);
        if(key&UP_FLAG)sel=(sel+3)%4;if(key&DOWN_FLAG)sel=(sel+1)%4;
        if(key&A_FLAG) {if(sel==0)return (ui_location_restore(location),true);if(sel==3)host_options_menu();}
    }
}
static void home_connect(bool open_library)
{
    if(!cfg.host[0]) {message(ICON_INFO,"Choose a host","Press Y to choose or add a host.");return;}
    if(!server_ok){int i=selected_host_index();if(i>=0 && hosts.hosts[i].auto_wake && !wake_host())return;refresh_server();}
    if(server_ok && !server.paired) {do_pair();refresh_server();}
    if(server_ok && server.paired) {
        if(server.current_game && !open_library)stream(server.current_game,app_name(server.current_game)?app_name(server.current_game):"Running app");
        else app_menu();
    }
}
static void hosts_menu(int page)
{
    int location=ui_location_enter("Hosts");
    int sel=0;bool dirty=true;
    next_discovery=0;discovery_round=0;next_socket_retry=0;
    for(int i=0;i<32;i++)attempts[i].used=false;
    for(;;) {
        if(navigation_pending>=0){ui_location_restore(location);return;}
        if(discovery_tick())dirty=true;
        if(sel>hosts.count+1)sel=hosts.count+1;
        if(dirty) {
            struct ui_setting rows[HOST_CACHE_MAX+2];char names[HOST_CACHE_MAX][96];
            for(int i=0;i<hosts.count;i++) {
                const struct cached_host *h=hosts.hosts+i;
                snprintf(names[i],sizeof(names[i]),"%s%s",i==selected_host_index()?"*  ":"",h->alias[0]?h->alias:h->name[0]?h->name:h->ip);
                rows[i].label=names[i];rows[i].value=h->online?(h->paired?"Online / Paired":"Online / Unpaired"):"Offline";
                rows[i].help=h->ip;
            }
            rows[hosts.count].label="+ Add host manually";rows[hosts.count].value="IPv4";rows[hosts.count].help="Connect using a known IP address";
            rows[hosts.count+1].label="Host actions";rows[hosts.count+1].value="Open";rows[hosts.count+1].help="Refresh, pair, quit or unpair the active PC";
            ui_draw_hosts(rows,hosts.count+2,sel,page,cfg.mdns_discovery);ui_present();dirty=false;
        }
        int key=input_menu_pressed();
        if(!key){plat_sleep_ms(16);continue;}
        if((key&B_FLAG)||tab_key(key,page)){ui_location_restore(location);return;}
        if(key&UP_FLAG)sel=sel>0?sel-1:0;
        if(key&DOWN_FLAG)sel=sel<hosts.count+1?sel+1:sel;
        if(key&Y_FLAG) {
            next_discovery=0;discovery_round=0;next_socket_retry=0;
            for(int i=0;i<32;i++)attempts[i].used=false;
        }
        dirty=true;
        if(!(key&(A_FLAG|X_FLAG)))continue;
        discovery_pause();
        if(sel<hosts.count) {
            const char *ip=hosts.hosts[sel].ip;
            if(strcmp(cfg.host,ip) || strcmp(cfg.host_uuid,hosts.hosts[sel].uuid)){snprintf(cfg.host,sizeof(cfg.host),"%s",ip);snprintf(cfg.host_uuid,sizeof(cfg.host_uuid),"%s",hosts.hosts[sel].uuid);config_save(&cfg);clear_apps();server_ok=false;}
            if(key&X_FLAG){manage_host();dirty=true;continue;}
            refresh_server();
            {ui_location_restore(location);return;} /* A selects a PC; Home's A connects/plays directly. */
        }
        if(sel==hosts.count) {
            char ip[64];snprintf(ip,sizeof(ip),"%s",cfg.host);
            if(edit_ip(ip,sizeof(ip))) {snprintf(cfg.host,sizeof(cfg.host),"%s",ip);cfg.host_uuid[0]=0;config_save(&cfg);clear_apps();refresh_server();{ui_location_restore(location);return;}}
        } else host_options_menu();
    }
}
/* Cold network operation: only on host change, newly online host or return
 * from a session. Home never validates a game against another host's library. */
static void home_sync_apps(void)
{
    discovery_pause();
    refresh_server();clear_apps();
    if(server_ok && server.paired) {
        int count=-1;run_busy("Loading apps",NULL,applist_task,&count);
        if(count>=0)app_count=count;
    }
}
static void main_menu(void)
{
    enum { MAX_RECENT=4,MAX_ITEMS=1+MAX_RECENT+4 };
    int sel=0,n=0,recent_count=0,shortcuts=1;
    int recent_ids[MAX_RECENT],recent_indices[MAX_RECENT];
    struct ui_app recent[MAX_RECENT],resume;
    int resume_id=0,resume_index=-1;
    char library_host[64]="";bool had_server=false;
    struct ui_menu_item items[MAX_ITEMS];
    bool dirty=true;
    for(;;) {
        if(navigation_pending>=0) {
            int page=navigation_pending;navigation_pending=-1;discovery_pause();
            if(page==UI_LIBRARY)home_connect(true);
            else if(page==UI_STREAM) {settings_initial_group=SETTINGS_STREAM;settings_menu();}
            else if(page==UI_SETTINGS)settings_menu();
            dirty=true;sel=0;next_discovery=0;
            if(navigation_pending>=0)continue;
        }
        if(discovery_tick())dirty=true;
        if(dirty) {
            int selected_shortcut=sel>=shortcuts?sel-shortcuts:-1;
            int selected_recent=sel>0 && sel<=recent_count?recent_ids[sel-1]:0;
            if(cfg.host[0] && (strcmp(library_host,cfg.host) || stream_return_home || (server_ok && !had_server))) {
                home_sync_apps();snprintf(library_host,sizeof(library_host),"%s",cfg.host);
            }
            had_server=server_ok;
            recent_count=0;
            int art_w,art_h;ui_art_size(&art_w,&art_h);
            if(!home_art.loaded) {
                uint64_t read_start=perf_enabled()?perf_clock():0;
                home_art_load(&home_art,art_w,art_h);
                if(read_start)perf_record(PERF_HOME_READ,perf_clock()-read_start);
            }
            int library_ids[MAX_APPS];for(int i=0;i<app_count;i++)library_ids[i]=apps[i].id;
            struct history_home_targets targets=history_home_select(&session_history,host_key(),library_ids,app_count,server_ok?server.current_game:0);
            resume_index=targets.resume;resume_id=resume_index>=0?apps[resume_index].id:0;
            memset(&resume,0,sizeof(resume));
            if(resume_index>=0) {
                resume.name=apps[resume_index].name;resume.art=app_art[resume_index];resume.generation=art_generation[resume_index];
                resume.running=server_ok && server.current_game==resume_id;
            }
            for(int i=0;i<targets.count;i++) {
                int index=targets.recent[i],id=apps[index].id;
                recent_ids[recent_count]=id;recent_indices[recent_count]=index;
                struct ui_app *a=recent+recent_count++;
                a->name=apps[index].name;a->art=app_art[index];a->generation=art_generation[index];
                a->running=server_ok && server.current_game==id;a->favorite=library_favorite_has(&favorites,host_key(),id);
            }
            /* Pin all current keys before replacing any thumbnail payload. */
            for(int i=0;i<HOME_ART_LIMIT;i++)home_art.entries[i].pinned=false;
            if(resume_id) {
                int slot=home_art_slot(&home_art,cfg.host,resume_id);
                if(slot>=0)home_art.entries[slot].pinned=true;
            }
            for(int i=0;i<recent_count;i++) {
                int slot=home_art_slot(&home_art,cfg.host,recent_ids[i]);
                if(slot>=0)home_art.entries[slot].pinned=true;
            }
            for(int i=0;i<recent_count;i++)if(recent[i].art) {
                int slot=home_art_slot(&home_art,cfg.host,recent_ids[i]);
                if(slot<0 || home_art.entries[slot].source_generation!=recent[i].generation) {
                    ui_drop_main_cache();
                    home_art_remember(&home_art,cfg.host,recent_ids[i],recent[i].art,art_w,art_h,recent[i].generation);
                }
            }
            for(int i=0;i<recent_count;i++)if(!recent[i].art)
                recent[i].art=home_art_get(&home_art,cfg.host,recent_ids[i],&recent[i].generation);
            if(resume_id) {
                if(resume.art)home_art_remember(&home_art,cfg.host,resume_id,resume.art,art_w,art_h,resume.generation);
                else resume.art=home_art_get(&home_art,cfg.host,resume_id,&resume.generation);
            }
            set_item(items,"Play / Connect",NULL,ICON_PLAY,true);
            for(int i=0;i<recent_count;i++)set_item(items+i+1,recent[i].name,"Recent app",ICON_PLAY,true);
            shortcuts=recent_count+1;
            if(selected_shortcut>=0)sel=shortcuts+selected_shortcut;
            else if(selected_recent) {sel=0;for(int i=0;i<recent_count;i++)if(recent_ids[i]==selected_recent)sel=i+1;}
            set_item(items+shortcuts,"Library","Games & apps",ICON_PLAY,true);
            set_item(items+shortcuts+1,"Stream","Profiles & audio",ICON_MONITOR,true);
            set_item(items+shortcuts+2,resume_id?"Continue":"Recent Session",resume_id?"Play now":"Your play history",ICON_PLAY,true);
            set_item(items+shortcuts+3,"Settings","Hosts & advanced",ICON_GEAR,true);
            n=shortcuts+4;
            if(sel>=n)sel=n-1;
            struct ui_host h=host_view();
            int selected_host=selected_host_index();if(selected_host>=0 && (hosts.hosts[selected_host].alias[0] || !h.name || !h.name[0]))h.name=hosts.hosts[selected_host].alias[0]?hosts.hosts[selected_host].alias:hosts.hosts[selected_host].name;
            char mode[100];snprintf(mode,sizeof(mode),"%dp%d  \xE2\x80\xA2  %d Mbps",cfg.height,cfg.fps,cfg.bitrate_kbps/1000);
            struct ui_dashboard dashboard={config_preset_name(config_video_preset(&cfg)),mode,recent,recent_count,resume_id?&resume:NULL,resume_id};
            ui_draw_dashboard(&h,items,n,sel,plat_console_ip(),&dashboard);ui_focus_tick(plat_now_ms());ui_present();dirty=false;
            if(stream_return_home) {plat_shutdown_stage("Returned to Moonlight home screen\n");stream_return_home=false;}
        }
        int key=input_menu_pressed();
        if(!key) {if(ui_focus_tick(plat_now_ms()))ui_present();plat_sleep_ms(16);continue;}
        dirty=true;
        if(tab_key(key,UI_HOME))continue;
        if(key&UP_FLAG)sel=ui_dashboard_move_tiles(sel,0,-1,n,recent_count);
        if(key&DOWN_FLAG)sel=ui_dashboard_move_tiles(sel,0,1,n,recent_count);
        if(key&LEFT_FLAG)sel=ui_dashboard_move_tiles(sel,-1,0,n,recent_count);
        if(key&RIGHT_FLAG)sel=ui_dashboard_move_tiles(sel,1,0,n,recent_count);
        if(key&B_FLAG){sel=0;continue;}
        if(key&Y_FLAG) {hosts_menu(UI_HOME);continue;}
        if(!(key&(A_FLAG|X_FLAG|PLAY_FLAG)))continue;
        discovery_pause(); /* No GS concurrency or discovery during a session. */
        if(key&PLAY_FLAG){host_options_menu();continue;}
        bool play=(key&A_FLAG)!=0;
        if(sel==0) {
            if(key&X_FLAG)play=host_details();
            if(play)home_connect(false);
        } else if(sel<=recent_count || (sel==shortcuts+2 && resume_id)) {
            bool continuing=sel==shortcuts+2;int i=continuing?0:sel-1;
            int id=continuing?resume_id:recent_ids[i],index=continuing?resume_index:recent_indices[i];
            char name[64];snprintf(name,sizeof(name),"%s",continuing?resume.name:recent[i].name);
            if(key&X_FLAG)play=app_details_for(id,name,index);
            if(play) {
                if(!server_ok)refresh_server();
                if(server_ok && !server.paired){do_pair();refresh_server();}
                if(server_ok && server.paired) {
                    if(server.current_game && server.current_game!=id)message(ICON_INFO,"Another app is running","Open the running app with X, then choose Stop app.");
                    else stream(id,name);
                }
            }
        } else if(sel==shortcuts)navigation_pending=UI_LIBRARY;
        else if(sel==shortcuts+1)navigation_pending=UI_STREAM;
        else if(sel==shortcuts+2)history_menu();
        else if(sel==shortcuts+3)navigation_pending=UI_SETTINGS;
        if(host_cache_save_failed)message(ICON_INFO,"Host kept for this session","Could not save hosts.tsv. Check storage permissions.");
        next_discovery=0;
    }
}

/* ------------------------------------------------------------ startup */

struct identity_job {
    bool ok;
};

static void identity_status(const char *msg)
{
    (void)msg;   /* shown through the splash's fixed text instead */
}

static void *identity_task(void *arg)
{
    struct identity_job *j = (struct identity_job *)arg;
    j->ok = crypto_load_identity(&identity, identity_status);
    return NULL;
}

static void draw_splash_task(void *ctx, uint32_t t)
{
    ui_draw_splash((const char *)ctx, t);
}

void app_main(void)
{
    struct identity_job ij;

    config_load(&cfg);
    ui_set_language(cfg.language);
    ui_set_appearance(cfg.ui_background,cfg.ui_accent);ui_set_motion(cfg.ui_motion);ui_set_stream_hud(cfg.stream_menu_style,cfg.mouse_hud);
    plat_fixup_config(&cfg);
    char *saved_hosts=storage_read("hosts.tsv",NULL);host_cache_decode(&hosts,saved_hosts);free(saved_hosts);
    if(cfg.host_uuid[0])for(int i=0;i<hosts.count;i++)if(wake_identity(cfg.host_uuid,hosts.hosts[i].uuid)){snprintf(cfg.host,sizeof(cfg.host),"%s",hosts.hosts[i].ip);break;}
    if(cfg.host[0]) {
        bool found=false;for(int i=0;i<hosts.count;i++)if(!strcmp(hosts.hosts[i].ip,cfg.host))found=true;
        if(!found) {struct cached_host h;memset(&h,0,sizeof(h));snprintf(h.ip,sizeof(h.ip),"%s",cfg.host);h.port=47989;host_cache_upsert(&hosts,&h);}
    } else if(hosts.count){snprintf(cfg.host,sizeof(cfg.host),"%s",hosts.hosts[0].ip);snprintf(cfg.host_uuid,sizeof(cfg.host_uuid),"%s",hosts.hosts[0].uuid);}
    char *saved_history=storage_read("sessions.tsv",NULL);history_decode(&session_history,saved_history);free(saved_history);
    char *saved_favorites=storage_read("favorites.tsv",NULL);library_favorites_decode(&favorites,saved_favorites);free(saved_favorites);
    errno=0;char *bindings_text=storage_read("game-bindings.tsv",NULL);int bindings_error=errno;bindings_decode(&game_bindings,bindings_text);if(!bindings_text && bindings_error!=ENOENT)game_bindings.read_only=true;free(bindings_text);
    for(int i=0;i<hosts.count;i++)if(hosts.hosts[i].is_default){snprintf(cfg.host,sizeof(cfg.host),"%s",hosts.hosts[i].ip);snprintf(cfg.host_uuid,sizeof(cfg.host_uuid),"%s",hosts.hosts[i].uuid);break;}
    plat_diagnostics_configure(cfg.diagnostic_log);
    ui_set_exit_shortcut(cfg.quit_combo);
    perf_begin(cfg.diagnostic_log || cfg.stats_overlay);

    if (!crypto_init()) {
        message(ICON_ERROR, "Crypto init failed", "The random number generator couldn't be seeded.");
        for (;;)
            plat_sleep_ms(1000);
    }
    ij.ok = false;
    run_task(identity_task, &ij, draw_splash_task, (void *)"Preparing this console's client certificate...");
    have_identity = ij.ok;
    if (have_identity)
        gs_init(&identity);
    else
        message(ICON_ERROR, "No client certificate", "The certificate couldn't be created, so pairing isn't possible.");

    main_menu();
}
