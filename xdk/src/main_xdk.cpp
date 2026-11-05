/*
 * Moonlight for Xbox 360, Xbox 360 SDK build: a regular title (.xex) that
 * starts from the dashboard or Aurora. Brings up graphics and the network,
 * provides the platform layer for the shared app (src/app.c) and keeps the
 * settings and client certificate next to the .xex (game:\moonlight).
 */
#include "xdk.h"
#include "../../src/performance.h"

#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/config.h"
#include "../../src/aurora_launch.h"
#define STORAGE_OS_ERROR() ((unsigned)GetLastError())
#include "../../src/safe_storage.h"
#include "../../src/platform.h"
#include "../../src/ui/canvas.h"
#include "../../src/ui/image.h"
#include "../../src/ui/ui.h"
#include "../../src/ui/draw.h"
#include "../../src/diagnostic_ring.h"

static char console_ip[20];
static struct diagnostic_ring diagnostic_log;
static CRITICAL_SECTION diagnostic_lock;
static volatile LONG diagnostic_enabled;
static bool diagnostic_initialized;
static volatile bool diagnostic_save_failed;
static char previous_log[DIAG_SNAPSHOT_BYTES];
static char previous_shutdown[256], previous_phase[256];
static struct diagnostic_ring connection_trace;
static CRITICAL_SECTION connection_trace_lock;
static bool connection_trace_ready;

static void diagnostic_add(const char *line)
{
    if (!diagnostic_enabled) return;
    EnterCriticalSection(&diagnostic_lock);
    diagnostic_ring_add(&diagnostic_log, GetTickCount(), line);
    LeaveCriticalSection(&diagnostic_lock);
}

static void diagnostic_store(const char *name, const char *text, size_t length)
{
    if (!storage_write(name, text, length)) diagnostic_save_failed = true;
}

/* ------------------------------------------------------------ platform layer */

/* Cold path only: observes affinity/priority without changing scheduling. */
extern "C" void xdk_audit_thread(const char *name)
{
    char line[192];DWORD hw=GetCurrentProcessorNumber();
    snprintf(line,sizeof(line),"thread %s id=%u hw=%u core=%u priority=%d\n",name,
        GetCurrentThreadId(),hw,hw/2,GetThreadPriority(GetCurrentThread()));
    plat_log(line);
}

uint32_t plat_now_ms(void)
{
    return (uint32_t)GetTickCount();
}

void plat_sleep_ms(unsigned int ms)
{
    Sleep(ms);
}
int plat_atomic_load(volatile int *value) {return (int)InterlockedCompareExchange((volatile LONG*)value,0,0);}
void plat_atomic_store(volatile int *value,int next) {InterlockedExchange((volatile LONG*)value,(LONG)next);}

struct plat_thread {
    HANDLE handle;
    void *(*fn)(void *);
    void *arg;
};

static DWORD WINAPI thread_entry(LPVOID p)
{
    plat_thread *t = (plat_thread *)p;
    t->fn(t->arg);
    return 0;
}

plat_thread *plat_thread_start(void *(*fn)(void *), void *arg)
{
    plat_thread *t = (plat_thread *)calloc(1, sizeof(*t));

    if (t == NULL)
        return NULL;
    t->fn = fn;
    t->arg = arg;
    t->handle = CreateThread(NULL, 512 * 1024, thread_entry, t, 0, NULL);
    if (t->handle == NULL) {
        free(t);
        return NULL;
    }
    return t;
}

void plat_thread_join(plat_thread *t)
{
    WaitForSingleObject(t->handle, INFINITE);
    CloseHandle(t->handle);
    free(t);
}

/* Decodes on another core so the menu stays smooth while covers load. */
uint32_t *plat_decode_art(const unsigned char *data, size_t len, int tw, int th)
{
    PerfScope timing(PERF_COVER_DECODE);
    DWORD prev = XSetThreadProcessor(GetCurrentThread(), HW_THREAD_ART);
    uint32_t *full, *scaled = NULL;
    int w, h;

    full = image_decode_argb(data, len, &w, &h);
    if (full)
        scaled = image_scale_cover(full, w, h, tw, th);
    free(full);
    if (prev != (DWORD)-1)
        XSetThreadProcessor(GetCurrentThread(), prev);
    return scaled;
}

const char *plat_console_ip(void)
{
    return console_ip;
}

void plat_fixup_config(struct config *cfg)
{
    cfg->renderer = 0;   /* always the GPU here */
}

bool plat_has_renderer_choice(void)
{
    return false;
}

/* Debug log: each line is broadcast as one UDP datagram to port 47555 on the
 * LAN, so xdk/scripts/log_listener.py on the PC can show it live. */
#define LOG_PORT 47555
static SOCKET log_sock = INVALID_SOCKET;

static void log_init(void)
{
    BOOL on = TRUE;
    u_long nonblocking=1;

    log_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (log_sock != INVALID_SOCKET) {
        setsockopt(log_sock, SOL_SOCKET, SO_BROADCAST, (const char *)&on, sizeof(on));
        if(ioctlsocket(log_sock,FIONBIO,&nonblocking)!=0){closesocket(log_sock);log_sock=INVALID_SOCKET;}
    }
}

void plat_log(const char *line)
{
    struct sockaddr_in to;
    char buf[600];
    int n;

    diagnostic_add(line);
    if(!strncmp(line,"[NET]",5)||!strncmp(line,"[SYSUI]",7)) {
        OutputDebugStringA(line);
        if(connection_trace_ready) {
            EnterCriticalSection(&connection_trace_lock);
            diagnostic_ring_add(&connection_trace,GetTickCount(),line);
            LeaveCriticalSection(&connection_trace_lock);
        }
    }
    if (log_sock == INVALID_SOCKET || video_xdk_is_streaming())
        return;
    n = snprintf(buf, sizeof(buf), "%8lu %s", GetTickCount(), line);
    if (n < 0)
        return;
    if (n > (int)sizeof(buf) - 1)
        n = (int)sizeof(buf) - 1;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(LOG_PORT);
    to.sin_addr.s_addr = INADDR_BROADCAST;
    sendto(log_sock, buf, n, 0, (const struct sockaddr *)&to, sizeof(to));
}

/* ------------------------------------------------------------ storage */

/* game: is the folder the .xex was started from. */
static const char storage_root[] = "game:\\moonlight";
static bool storage_ok;
static struct moonx_launch_data direct_launch_data;
static bool direct_launch_ready;
typedef char moonx_main_abi_check[(sizeof(moonx_launch_data)==468 &&
    offsetof(moonx_launch_data,checksum)==464)?1:-1];
static DWORD direct_trace_started;
static bool direct_trace_writable;
static const char direct_trace_path[]="game:\\moonlight\\direct-launch.log";

static void direct_trace_stage(const char *format,...)
{
    char detail[256],line[320];va_list args;
    va_start(args,format);vsnprintf(detail,sizeof(detail),format,args);va_end(args);
    snprintf(line,sizeof(line),"[%u ms] %s\n",(unsigned)(GetTickCount()-direct_trace_started),detail);
    OutputDebugStringA(line);
    if(direct_trace_writable){
        FILE *f=fopen(direct_trace_path,"ab");
        if(f){fputs(line,f);fflush(f);fclose(f);}
        else direct_trace_writable=false;
    }
}
static void direct_trace_begin(void)
{
    /* First action at entry: truncate stale evidence, then close after every stage. */
    FILE *f=fopen(direct_trace_path,"wb");
    if(!f){CreateDirectoryA("game:\\moonlight",NULL);f=fopen(direct_trace_path,"wb");}
    direct_trace_writable=f!=NULL;
    if(f){fflush(f);fclose(f);}
    direct_trace_started=GetTickCount();
    direct_trace_stage("[MAIN 01] MoonX360 entry build=%s %s ABI=%u log=%s",
        __DATE__,__TIME__,(unsigned)sizeof(moonx_launch_data),direct_trace_writable?"open":"unavailable");
}
void plat_direct_launch_stage(const char *stage)
{if(stage)direct_trace_stage("[MAIN] %s",stage);}

bool plat_direct_launch_read(struct moonx_launch_data *out)
{
    if(!out || !direct_launch_ready)return false;
    *out=direct_launch_data;direct_launch_ready=false;
    return true;
}

void plat_shutdown_stage(const char *stage)
{
    diagnostic_store("shutdown-stage.txt", stage, strlen(stage));
    diagnostic_add(stage);
}

bool plat_has_diagnostics(void) { return true; }
void plat_connection_trace_flush(void) {
    if(!connection_trace_ready)return;
    char text[DIAG_SNAPSHOT_BYTES];size_t n;
    EnterCriticalSection(&connection_trace_lock);n=diagnostic_ring_read(&connection_trace,text,sizeof(text));LeaveCriticalSection(&connection_trace_lock);
    storage_write("connection-timing.txt",text,n);
}
void plat_release_menu_textures(void) { gfx_release_ui_textures(); }

static void read_previous(const char *name, char *out, size_t capacity)
{
    if (!capacity) return;
    char *text = storage_read(name, NULL);
    snprintf(out, capacity, "%s", text ? text : "No saved record.\n");
    out[capacity - 1] = '\0';
    free(text);
}

void plat_diagnostics_configure(bool enabled)
{
    if (!diagnostic_initialized) {
        InitializeCriticalSection(&diagnostic_lock);
        read_previous("last-log.txt", previous_log, sizeof(previous_log));
        read_previous("shutdown-stage.txt", previous_shutdown, sizeof(previous_shutdown));
        read_previous("diagnostic-stage.txt", previous_phase, sizeof(previous_phase));
        diagnostic_initialized = true;
    }
    if (enabled && !diagnostic_enabled) {
        diagnostic_save_failed = false;
        InterlockedExchange(&diagnostic_enabled, 1);
        plat_diagnostics_checkpoint("Diagnostic capture enabled\n");
    } else if (!enabled && diagnostic_enabled) {
        plat_diagnostics_flush();
        InterlockedExchange(&diagnostic_enabled, 0);
    }
}

void plat_diagnostics_checkpoint(const char *phase)
{
    if (!diagnostic_enabled) return;
    diagnostic_add(phase);
    diagnostic_store("diagnostic-stage.txt", phase, strlen(phase));
}

void plat_diagnostics_flush(void)
{
    char text[DIAG_SNAPSHOT_BYTES];
    size_t n;
    if (!diagnostic_enabled) return;
    EnterCriticalSection(&diagnostic_lock);
    n = diagnostic_ring_read(&diagnostic_log, text, sizeof(text));
    LeaveCriticalSection(&diagnostic_lock);
    diagnostic_store("last-log.txt", text, n);
}

void plat_diagnostics_read(char *out, size_t capacity, bool previous)
{
    int n;
    if (!capacity) return;
    if (previous) {
        snprintf(out, capacity, "Previous run: last shutdown phase:\n%s\nLast diagnostic checkpoint:\n%s\nLast saved log (may predate a sudden crash):\n%s",
                 previous_shutdown, previous_phase, previous_log);
        out[capacity - 1] = '\0';
        return;
    }
    n = snprintf(out, capacity, "Current run - capture %s; RAM ring, last %d lines.%s\n",
                 diagnostic_enabled ? "ON" : "OFF", DIAG_LOG_LINES,
                 diagnostic_save_failed ? " Save failed: check writable folder." : "");
    if (n < 0 || (size_t)n >= capacity) { out[capacity - 1] = '\0'; return; }
    if (diagnostic_initialized) {
        EnterCriticalSection(&diagnostic_lock);
        diagnostic_ring_read(&diagnostic_log, out + n, capacity - n);
        LeaveCriticalSection(&diagnostic_lock);
    }
}

bool storage_init(void)
{
    DWORD attr;

    CreateDirectoryA(storage_root, NULL);
    attr = GetFileAttributesA(storage_root);
    storage_ok = attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
    return storage_ok;
}

const char *storage_dir(void)
{
    return storage_ok ? storage_root : NULL;
}

char *storage_read(const char *name, size_t *len)
{
    char path[128];
    FILE *f;
    char *buf;
    long size;

    if (!storage_ok) {errno=EIO;return NULL;}
    snprintf(path, sizeof(path), "%s\\%s", storage_root, name);
    f = fopen(path, "rb");
    if(!f && errno==ENOENT && storage_recoverable_name(name)){char backup[160];snprintf(backup,sizeof(backup),"%s.bak",path);f=fopen(backup,"rb");}
    if (f == NULL)
        return NULL;
    if(fseek(f,0,SEEK_END)!=0){fclose(f);errno=EIO;return NULL;}
    size = ftell(f);
    if(size<0 || ((!strcmp(name,"profiles-a.tsv") || !strcmp(name,"profiles-b.tsv")) && size>=CONFIG_PROFILES_BYTES) ||
       (!strcmp(name,"sessions.tsv") && size>16384) ||
       ((!strcmp(name,"launch-settings.tsv") || !strcmp(name,"launch-settings.tsv.bak")) && size>8192) ||
       (!strcmp(name,"game-bindings.tsv") && size>8192) ||
       (!strcmp(name,"profile-link-pending.tsv") && size>34816) ||
       ((!strcmp(name,"aurora-shortcuts.dat") || !strcmp(name,"aurora-shortcuts.dat.bak")) && size>65536) ||
       (!strncmp(name,"app-catalog-",12) && size>12288)) {fclose(f);errno=ERANGE;return NULL;}
    if(fseek(f,0,SEEK_SET)!=0){fclose(f);errno=EIO;return NULL;}
    buf = (char *)malloc(size + 1);
    if (buf && fread(buf, 1, size, f) != (size_t)size) {
        free(buf);
        buf = NULL;errno=EIO;
    }
    fclose(f);
    if (buf) {
        buf[size] = '\0';
        if (len)
            *len = (size_t)size;
    }
    return buf;
}

static void storage_log_failure(const char *path)
{
    const struct storage_write_diagnostic *d=&storage_write_diagnostic;
    char line[320];snprintf(line,sizeof(line),"[STORAGE] path=%s stage=%s errno=%d os=%u expected=%u written/read=%u\n",
        path,d->stage?d->stage:"unknown",d->error,d->os_error,(unsigned)d->expected,(unsigned)d->actual);
    plat_log(line);errno=d->error;
}
bool storage_write(const char *name, const void *data, size_t len)
{
    char path[128];bool ok=false;
    int n=snprintf(path,sizeof(path),"%s\\%s",storage_root,name);
    if(n<0 || (size_t)n>=sizeof(path)) {
        storage_write_failed("path",EINVAL,0,len,0);storage_log_failure(name);return false;
    }
    DWORD attr=GetFileAttributesA(storage_root);DWORD os=GetLastError();
    if(!storage_ok || attr==INVALID_FILE_ATTRIBUTES || !(attr&FILE_ATTRIBUTE_DIRECTORY)) {
        storage_write_failed("storage-directory",EIO,os,len,0);storage_log_failure(path);return false;
    }
    if(storage_recoverable_name(name))ok=storage_replace_file(path,data,len);
    else {
        errno=0;FILE *f=fopen(path,"wb");int e=errno;unsigned error=GetLastError();
        if(!f)storage_write_failed("open",e,error,len,0);
        else {
            errno=0;size_t written=fwrite(data,1,len,f);e=errno;error=GetLastError();
            if(written!=len){fclose(f);storage_write_failed("write",e,error,len,written);}
            else {errno=0;int rc=fclose(f);e=errno;error=GetLastError();ok=rc==0;if(!ok)storage_write_failed("close",e,error,len,written);}
        }
    }
    if(!ok)storage_log_failure(path);
    return ok;
}

/* ------------------------------------------------------------ startup */

static void splash(const char *status)
{
    ui_draw_splash(status, plat_now_ms());
    ui_present();
}

static void fatal(const char *title, const char *text)
{
    for (;;) {
        ui_draw_message(ICON_ERROR, title, text);
        ui_present();
        Sleep(500);
    }
}

static void *prepare_video_worker(void *done)
{
    video_xdk_init();
    plat_atomic_store((volatile int*)done,1);
    return NULL;
}
static void prepare_video(void)
{
    volatile int done=0;
    plat_thread *worker=plat_thread_start(prepare_video_worker,(void*)&done);
    if(!worker){video_xdk_init();return;}
    while(!plat_atomic_load(&done)) {
        splash("Preparing video decoder...");
        plat_sleep_ms(16);
    }
    plat_thread_join(worker);
}
static bool video_prepared;
void plat_prepare_video(void)
{
    if(video_prepared)return;
    prepare_video();video_prepared=true;
}

/* Starts the network stack and waits (up to 20 s) for an IP address. */
static bool network_init(void)
{
    XNetStartupParams xnsp;
    WSADATA wsa;
    XNADDR addr;
    DWORD start = GetTickCount(), status = 0;
    plat_log("[NET] network_init start\n");

    ZeroMemory(&xnsp, sizeof(xnsp));
    xnsp.cfgSizeOfStruct = sizeof(xnsp);
    /* Plain TCP/UDP to the PC instead of Xbox Live's secure protocol. */
    xnsp.cfgFlags = XNET_STARTUP_BYPASS_SECURITY;
    /* The video stream arrives in bursts of up to a frame (~100 KB). */
    xnsp.cfgSockDefaultRecvBufsizeInK = 255;
    xnsp.cfgSockDefaultSendBufsizeInK = 64;
    if (XNetStartup(&xnsp) != 0 || WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return false;

    DWORD dhcp_start=GetTickCount();unsigned polls=0;
    while (GetTickCount() - start < 20000) {
        polls++;
        status = XNetGetTitleXnAddr(&addr);
        if (status != XNET_GET_XNADDR_PENDING && (status & (XNET_GET_XNADDR_DHCP | XNET_GET_XNADDR_STATIC)))
            break;
        splash("Obtaining network address...");
        Sleep(100);
    }
    if (!(status & (XNET_GET_XNADDR_DHCP | XNET_GET_XNADDR_STATIC)))
        return false;
    char timing[160];snprintf(timing,sizeof(timing),"[NET] XNetGetTitleXnAddr/DHCP: %u ms (%u polls, flags=%lx)\n",GetTickCount()-dhcp_start,polls,status);plat_log(timing);
    snprintf(timing,sizeof(timing),"[NET] XNet ready: %u ms\n",GetTickCount()-start);plat_log(timing);
    snprintf(console_ip, sizeof(console_ip), "%u.%u.%u.%u", addr.ina.S_un.S_un_b.s_b1, addr.ina.S_un.S_un_b.s_b2,
             addr.ina.S_un.S_un_b.s_b3, addr.ina.S_un.S_un_b.s_b4);
    return true;
}

/* Cold-path breadcrumbs: preserve the last completed startup boundary even
 * if graphics initialization or a native DSP selftest faults before app_main. */
static void startup_stage(const char *stage)
{
    OutputDebugStringA(stage);
    if (storage_dir()) storage_write("startup-stage.txt", stage, strlen(stage));
}

extern "C" VOID HalReturnToFirmware(DWORD mode);
void plat_display_info(char *out,size_t capacity)
{
    int w,h,rw,rh;gfx_display_size(&w,&h);gfx_backbuffer_size(&rw,&rh);
    snprintf(out,capacity,ui_translate("Output %dx%d%s %.2f Hz | App %dx%d"),w,h,gfx_display_interlaced()?"i":"p",gfx_display_hz(),rw,rh);
}
int plat_display_refresh_x100(void) {float hz=gfx_display_hz();return hz>=20 && hz<=240?(int)(hz*100+0.5f):6000;}
bool plat_system_ui_active(void) {return gfx_system_ui_active();}
bool plat_power_available(void) {return !video_xdk_is_streaming();}
bool plat_power_action(bool restart)
{
    if(video_xdk_is_streaming())return false;
    plat_diagnostics_flush();
    HalReturnToFirmware(restart?6:5);
    return false; /* reports failure if firmware unexpectedly returns */
}
bool plat_restart_available(void)
{
    if(video_xdk_is_streaming())return false;
    DWORD attributes=GetFileAttributesA("game:\\default.xex");
    return attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY);
}
bool plat_restart_app(void)
{
    if(!plat_restart_available())return false;
    plat_diagnostics_flush();
    XLaunchNewImage("game:\\default.xex",0);
    return false;
}
void plat_exit_to_dashboard(void)
{
    plat_log("[APP] Exiting to dashboard\n");
    plat_diagnostics_flush();
    XLaunchNewImage(XLAUNCH_KEYWORD_DEFAULT_APP,0);
}
static int balanced_threads=1;
void xdk_set_thread_layout(int balanced) {balanced_threads=balanced!=0;}
int xdk_aux_processor(void) {return balanced_threads?0:HW_THREAD_RENDER;}

void __cdecl main()
{
    direct_trace_begin();
    XSetThreadProcessor(GetCurrentThread(),0);
    DWORD launch_bytes=0;
    direct_trace_stage("[MAIN 02] checking launch data");
    DWORD size_status=XGetLaunchDataSize(&launch_bytes);
    direct_trace_stage("[MAIN 03] XGetLaunchDataSize status=0x%08lX bytes=%lu",
        (unsigned long)size_status,(unsigned long)launch_bytes);
    if(size_status==ERROR_SUCCESS && launch_bytes==sizeof(direct_launch_data)){
        DWORD read_status=XGetLaunchData(&direct_launch_data,launch_bytes);
        direct_trace_stage("[MAIN 04] XGetLaunchData status=0x%08lX",(unsigned long)read_status);
        if(read_status==ERROR_SUCCESS){
            direct_launch_ready=moonx_launch_valid(&direct_launch_data,launch_bytes)!=0;
            direct_trace_stage("[MAIN 05] MX36 valid=%s magic=%02X%02X%02X%02X version=%u checksum=%s",
                direct_launch_ready?"YES":"NO",(unsigned char)direct_launch_data.magic[0],
                (unsigned char)direct_launch_data.magic[1],(unsigned char)direct_launch_data.magic[2],
                (unsigned char)direct_launch_data.magic[3],(unsigned)direct_launch_data.version,
                direct_launch_data.checksum==moonx_launch_checksum(&direct_launch_data)?"OK":"BAD");
        }
    }
    direct_trace_stage("[MAIN 06] mode=%s",direct_launch_ready?"DIRECT":"NORMAL");
    bool storage_ready=storage_init();
    direct_trace_stage("[MAIN 07] storage_init=%s",storage_ready?"OK":"FAIL");
    InitializeCriticalSection(&connection_trace_lock);connection_trace_ready=true;
    startup_stage("gfx_init begin\n");
    direct_trace_stage("[MAIN 08] gfx_init begin");
    if (!gfx_init()) {
        direct_trace_stage("[MAIN FATAL] gfx_init failed");
        startup_stage("gfx_init failed\n");
        return;   /* nothing to show an error on */
    }
    /* Optional recovery switch affects menus only; video remains untouched. */
    gfx_system_ui_init();
    ui_draw_enable(GetFileAttributesA("game:\\moonlight\\ui-legacy.flag")==INVALID_FILE_ATTRIBUTES);
    startup_stage("ui_init begin\n");
    direct_trace_stage("[MAIN 09] ui_init begin");
    struct config startup_config;config_load(&startup_config);
    int ui_width=startup_config.ui_layout?960:UI_LOGICAL_WIDTH;
    if (!ui_init_layout(ui_width, UI_LOGICAL_HEIGHT, startup_config.ui_layout)) {
        direct_trace_stage("[MAIN FATAL] ui_init failed");
        startup_stage("ui_init failed\n");
        return;
    }

    ui_set_language(startup_config.language);
    startup_stage("network splash begin\n");
    splash("Connecting to the network...");
    startup_stage("network_init begin\n");
    direct_trace_stage("[MAIN 10] network_init begin");
    if (!network_init())
        fatal("No network",
              "The console has no IP address. Connect a network cable or set up Wi-Fi in the dashboard's "
              "network settings, then start Moonlight again.");

    log_init();
    if(direct_launch_ready){char line[96];snprintf(line,sizeof(line),"[Aurora] LaunchData parsed at %u ms\n",(unsigned)GetTickCount());plat_log(line);}
    xdk_audit_thread("main");
    startup_stage("video_xdk_init begin\n");
    plat_log("Moonlight (Xbox 360 SDK build) started\n");
    if (!storage_ready)
        plat_log("Storage: game:\\moonlight isn't writable; settings won't be saved\n");
    if(!direct_launch_ready){splash("Preparing video decoder...");plat_prepare_video();}
    /* One-shot native replay for XBDM monitors without controller automation.
     * Only an explicitly deployed marker enables it; ordinary startup is
     * unchanged. Consume the marker before running to prevent restart loops. */
    if (!direct_launch_ready && GetFileAttributesA("game:\\moonlight\\run-benchmark.once") != INVALID_FILE_ATTRIBUTES) {
        if (DeleteFileA("game:\\moonlight\\run-benchmark.once")) {
            startup_stage("native offline benchmark begin\n");
            plat_run_benchmark();
            startup_stage("native offline benchmark returned\n");
        }
    }
    startup_stage("app_main begin\n");
    direct_trace_stage("[MAIN 11] app_main begin");
    app_main();
}


int plat_edit_text(char *name,size_t capacity,const char *prompt)
{
    WCHAR initial[64],result[64],title[96],description[160];
    if(video_xdk_is_streaming() || !name || capacity<2 || capacity>64)return -1;
    if(!MultiByteToWideChar(CP_UTF8,0,name,-1,initial,64))return -1;
    MultiByteToWideChar(CP_UTF8,0,ui_translate(prompt?prompt:"Rename profile"),-1,title,96);
    const char *help=prompt && !strcmp(prompt,"Search library")?
        "Enter a game or app name. B cancels.":"Choose a short name. B cancels.";
    MultiByteToWideChar(CP_UTF8,0,ui_translate(help),-1,description,160);
    DWORD user=0;XINPUT_STATE state;
    for(DWORD i=0;i<4;i++)if(XInputGetState(i,&state)==ERROR_SUCCESS){user=i;break;}
    XOVERLAPPED pending;ZeroMemory(&pending,sizeof(pending));ZeroMemory(result,sizeof(result));
    DWORD status=XShowKeyboardUI(user,VKBD_DEFAULT,initial,title,description,result,64,&pending);
    if(status==ERROR_IO_PENDING) {
        while(!XHasOverlappedIoCompleted(&pending)) {
            if(gfx_system_ui_active())gfx_present_system_ui();else ui_present();
            Sleep(16);
        }
        status=XGetOverlappedResult(&pending,NULL,FALSE);
        if(status==ERROR_FUNCTION_FAILED)status=XGetOverlappedExtendedError(&pending);
    }
    if(status==ERROR_CANCELLED)return 0;
    if(status!=ERROR_SUCCESS)return -1;
    char utf8[CONFIG_PROFILE_NAME_BYTES];
    if(!WideCharToMultiByte(CP_UTF8,0,result,-1,utf8,(int)capacity,NULL,NULL) || !config_profile_name_valid(utf8))return -1;
    strcpy(name,utf8);return 1;
}
int plat_edit_profile_name(char *name,size_t capacity)
{return plat_edit_text(name,capacity,"Rename profile");}
