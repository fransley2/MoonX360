"""Exercise the actual Home discovery scheduler with a deterministic worker/mailbox."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[2]
s=(root/'src/app.c').read_text()
start=s.index('static struct host_cache hosts;')
end=s.index('/* ------------------------------------------------------------ IP editor */',start)
code=s[start:end]
fixture=r'''
#include <assert.h>
#include "../src/input.h"
static int input_menu_pressed(){return 0;}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/config.h"
#include "../src/product_features.h"
#include "../src/wake.h"
#include "../src/session_history.h"
#include "../src/library_state.h"
static session_history session_history;static library_favorites favorites;
#include <Limelight.h>
struct gs_server {char address[64],hostname[64],mac[18],uuid[80],app_version[32],gfe_version[32];unsigned short http_port;bool paired;int info_timeout_ms;volatile int *cancel,*wake_cancel;char *rtsp_session_url;SERVER_INFORMATION info;};
#include "../src/mdns.h"
#include "../src/host_cache.h"
static config cfg;
static const char *selected_profile_name(){return config_preset_name(config_video_preset(&cfg));}
static const char *host_key(void);
static gs_server server;
static bool server_ok,have_identity=true,worker_context,fail_thread,probe_valid=true;
static char server_error[200];
static unsigned now=100,sends,polls,http_calls,writes,draws,joins;
struct plat_thread {void *(*fn)(void*);void *arg;};
static plat_thread worker;
static unsigned plat_now_ms(){return now;}
static int plat_atomic_load(volatile int *p){return *p;}
static void plat_atomic_store(volatile int *p,int v){*p=v;}
static plat_thread *plat_thread_start(void *(*fn)(void*),void *arg){if(fail_thread)return NULL;worker.fn=fn;worker.arg=arg;return &worker;}
static void complete(){assert(worker.fn);worker_context=true;worker.fn(worker.arg);worker_context=false;worker.fn=NULL;}
static void plat_thread_join(plat_thread*){assert(!worker.fn);joins++;}
static void plat_sleep_ms(unsigned ms){now+=ms;if(worker.fn)complete();}
static void ui_draw_busy(const char*,const char*,unsigned){assert(!worker_context);draws++;}
static void ui_present(){}
static void plat_log(const char*){}
bool storage_write(const char*,const void*,size_t){assert(!worker_context);writes++;return true;}
char *storage_read(const char*,size_t*){return NULL;}
bool gs_server_info_port(gs_server *s,const char *ip,unsigned short port,int timeout){
    assert(worker_context && timeout==1200);http_calls++;
    strcpy(s->address,ip);strcpy(s->hostname,"Gaming-PC");strcpy(s->uuid,"real-server-uuid");
    strcpy(s->app_version,"7.1.0");s->http_port=port;s->paired=true;
    s->info.address=s->address;s->info.serverInfoAppVersion=s->app_version;return probe_valid;
}
void gs_server_free(gs_server *s){free(s->rtsp_session_url);s->rtsp_session_url=NULL;}
bool mdns_open(){assert(!worker_context);return true;}
void mdns_close(){assert(!worker_context);}
bool mdns_send(const char*,unsigned short){sends++;return true;}
bool mdns_poll(mdns_state*){polls++;return false;}
const char *ui_translate(const char *s){return s;}
/* CODE */
int main(){
    config_defaults(&cfg);cfg.mdns_discovery=false;
    char cached[]="real-server-uuid\tCached PC\t10.0.0.2\t48000\n";
    host_cache_decode(&hosts,cached);assert(hosts.count==1 && !hosts.hosts[0].online);
    assert(!discovery_tick());assert(probe_thread && !http_calls && !polls && !sends); // no inline HTTP
    complete();assert(discovery_tick() && server_ok && hosts.hosts[0].online);
    assert(!strcmp(cfg.host,"10.0.0.2") && server.http_port==48000);
    assert(server.info.address==server.address && server.info.serverInfoAppVersion==server.app_version);
    assert(!strcmp(server.uuid,"real-server-uuid") && server.info_timeout_ms==0);
    assert(writes==2 && http_calls==1 && !probe_thread);
    now+=50000;assert(!discovery_tick() && !probe_thread); // cached host retries bounded at 60 seconds
    now+=10001;assert(!discovery_tick() && probe_thread);probe_valid=false;complete();
    assert(discovery_tick() && !server_ok && !hosts.hosts[0].online);
    cfg.mdns_discovery=true;now+=60001;discovery_tick();assert(probe_thread && sends && polls);
    unsigned dns_sends=sends,dns_polls=polls;discovery_pause();
    assert(!probe_thread && draws && joins==3 && server.cancel==NULL && server.wake_cancel==NULL);
    /* Streaming never invokes discovery_tick: verify idle scheduler does no independent work. */
    now+=600000;assert(sends==dns_sends && polls==dns_polls);
    memset(attempts,0,sizeof(attempts));fail_thread=true;unsigned calls=http_calls;
    assert(!probe_start("10.0.0.3",47989,now) && !probe_thread && http_calls==calls);
    puts("PASS: actual async host validation, immediate cached state, correct SRV port, pointer rebinding, offline status, 60s throttle, pause/drain, no inline fallback.");
}
'''
# No hidden discovery activity or changed stream negotiation.
stream=s[s.index('static bool stream_once('):s.index('static void stream(int app_id,')]
assert stream.count('discovery_pause();')==1 and 'discovery_tick' not in stream and 'mdns_' not in stream
for field,value in [('width','cfg.width'),('height','cfg.height'),('fps','cfg.fps'),('bitrate','cfg.bitrate_kbps')]:
    assert f'lj.sc.{field} = {value};' in stream
assert 'lj.sc.supportedVideoFormats = VIDEO_FORMAT_H264;' in stream
home=s[s.index('static void main_menu(void)'):]
assert stream.index('discovery_pause();')<stream.index('refresh_server();')
Path(sys.argv[1]).write_text(fixture.replace('/* CODE */',code))
