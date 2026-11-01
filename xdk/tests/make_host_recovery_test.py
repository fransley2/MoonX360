"""Exercise the actual worker + publication with a controlled LAN and clock."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[2]
source=(root/'src/app.c').read_text()
def fn(signature):
    a=source.index(signature);b=source.index('{',a)+1;depth=1
    while depth:depth+=(source[b]=='{')-(source[b]=='}');b+=1
    return source[a:b]
a=source.index('struct refresh_job {');b=source.index('};',a)+2
code=source[a:b]+'\n'+'\n'.join(fn(f) for f in ('static const char *host_saved_identity(void)\n{','static bool refresh_probe(', 'static void *refresh_task(', 'static void refresh_server(void)\n{'))
fixture=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/config.h"
#include "../src/wake.h"
#include "../src/connection_flow.h"
#include "../src/host_cache.h"
#include "../src/mdns.h"
#include "../src/ui/ui.h"
#include <Limelight.h>
struct gs_server {char hostname[64],address[64],uuid[80],app_version[32],gfe_version[32];unsigned short http_port,https_port;bool paired;
 volatile int *cancel,*wake_cancel;int info_timeout_ms;char *rtsp_session_url;SERVER_INFORMATION info;};

static config cfg;static gs_server server;static host_cache hosts;
static bool server_ok,task_start_failed,task_was_cancelled,worker;
static volatile int task_cancel,task_phase;
static unsigned tick,cancel_at;static int old_kind,auth_kind,plain_calls,auth_calls,opens,closes,saves,pairs;
static bool advertise,legacy_confirm;static int confirm_keys;static char server_error[200];
static unsigned plat_now_ms(){return tick;}
static int plat_atomic_load(volatile int *p){return *p;}
static void task_state(int p){task_phase=p;}
static void plat_sleep_ms(unsigned ms){tick+=ms;if(cancel_at && tick>=cancel_at)task_cancel=1;}
static bool config_save(config*){assert(!worker);saves++;return true;}
static unsigned short host_port(const char*){return 47989;}
static void host_offline(const char*){}
static const char *ui_translate(const char *s){return s;}
static ui_host host_view(){ui_host h={"Gaming PC"};return h;}
static void ui_draw_settings_page(const char*,const ui_setting*,int,int){}
static void ui_present(){}
static int wait_button(){return legacy_confirm?(confirm_keys++?A_FLAG:DOWN_FLAG):B_FLAG;}
static void gs_server_free(gs_server *s){free(s->rtsp_session_url);s->rtsp_session_url=NULL;}
static bool gs_wake_probe(gs_server *s,const char *ip,unsigned short port,int timeout,volatile int *cancel){
 assert(worker && timeout>0 && timeout<=1200);plain_calls++;
 int kind=!strcmp(ip,"10.0.0.50")?old_kind:1;
 unsigned duration=kind?20:timeout;
 for(unsigned i=0;i<duration && !*cancel;i+=10)plat_sleep_ms(10);
 if(*cancel || !kind)return false;
 strcpy(s->address,ip);strcpy(s->uuid,kind==2?"different-pc":"selected-pc");s->http_port=port;s->https_port=48123;return true;
}
static bool gs_server_info_port(gs_server *s,const char *ip,unsigned short port,int timeout){
 assert(worker && task_phase==HOST_AUTHENTICATING && timeout==HOST_AUTH_MS);auth_calls++;assert(s->https_port==48123);
 for(int i=0;i<4 && !task_cancel;i++)plat_sleep_ms(10);if(task_cancel || auth_kind==0)return false;
 strcpy(s->address,ip);strcpy(s->uuid,auth_kind==2?"different-pc":"selected-pc");s->http_port=port;s->paired=true;return true;
}
static void remember_server(const gs_server *s){
 assert(!worker);cached_host h={};strcpy(h.uuid,s->uuid);strcpy(h.ip,s->address);h.port=s->http_port;h.online=h.paired=true;
 assert(host_cache_upsert(&hosts,&h)>=0);saves++;
}
static bool mdns_open(){assert(worker);opens++;return true;}
static void mdns_close(){assert(worker);closes++;}
static bool mdns_send(const char*,unsigned short){assert(worker);return true;}
static bool mdns_poll(mdns_state*){assert(worker);return false;}
static int mdns_candidates(const mdns_state*,mdns_candidate *out,int){
 if(!advertise)return 0;strcpy(out[0].ip,"10.0.0.87");out[0].port=47989;return 1;
}
static void *run_busy(const char*,const char*,void *(*fn)(void*),void *arg){
 task_cancel=0;task_was_cancelled=false;worker=true;fn(arg);worker=false;task_was_cancelled=task_cancel!=0;return NULL;
}
/* CODE */
static void reset(){
 memset(&cfg,0,sizeof(cfg));memset(&server,0,sizeof(server));memset(&hosts,0,sizeof(hosts));
 strcpy(cfg.host,"10.0.0.50");strcpy(cfg.host_uuid,"selected-pc");cfg.mdns_discovery=true;
 cached_host h={};strcpy(h.uuid,cfg.host_uuid);strcpy(h.ip,cfg.host);h.port=47989;h.pairing_saved=true;host_cache_upsert(&hosts,&h);
 task_cancel=0;tick=0;cancel_at=0;old_kind=auth_kind=1;plain_calls=auth_calls=opens=closes=saves=pairs=0;advertise=legacy_confirm=false;confirm_keys=0;
}
int main(){
 reset();refresh_server();assert(server_ok && server.paired && !strcmp(cfg.host,"10.0.0.50") && !opens && auth_calls==1 && !pairs);
 reset();old_kind=0;advertise=true;refresh_server();assert(server_ok && server.paired && !strcmp(cfg.host,"10.0.0.87"));
 assert(hosts.count==1 && !strcmp(hosts.hosts[0].ip,cfg.host) && saves==2 && opens==1 && closes==1 && !pairs);
 assert(server.info.address==server.address && server.info.serverInfoAppVersion==server.app_version && !server.cancel && !server.wake_cancel);
 reset();old_kind=0;refresh_server();assert(!server_ok && tick<=HOST_PROBE_MS+HOST_DISCOVERY_MS && opens==1 && closes==1 && auth_calls==0 && !saves);
 reset();old_kind=2;refresh_server();assert(!server_ok && !auth_calls && !strcmp(cfg.host,"10.0.0.50") && !saves);
 reset();old_kind=2;advertise=true;refresh_server();assert(server_ok && !strcmp(cfg.host,"10.0.0.87") && auth_calls==1);
 reset();old_kind=0;advertise=true;auth_kind=2;refresh_server();assert(!server_ok && !saves && !strcmp(cfg.host,"10.0.0.50"));
 for(int phase=0;phase<3;phase++){
  reset();old_kind=phase==2?1:0;cancel_at=phase==0?40:phase==1?HOST_PROBE_MS+70:40;
  refresh_server();assert(task_was_cancelled && !server_ok && !saves && tick<=cancel_at+10 && !strcmp(cfg.host,"10.0.0.50"));
 }
 reset();cfg.host_uuid[0]=hosts.hosts[0].uuid[0]=0;old_kind=0;refresh_server();assert(!server_ok && !opens && tick==HOST_PROBE_MS);
 reset();cfg.host_uuid[0]=hosts.hosts[0].uuid[0]=0;refresh_server();assert(!server_ok && task_was_cancelled && !saves && !cfg.host_uuid[0]);
 reset();cfg.host_uuid[0]=hosts.hosts[0].uuid[0]=0;legacy_confirm=true;refresh_server();assert(server_ok && !strcmp(cfg.host_uuid,"selected-pc") && server.paired && saves==2);
 reset();cfg.host_uuid[0]=0;refresh_server();assert(server_ok && !strcmp(cfg.host_uuid,"selected-pc") && !confirm_keys);
 reset();cfg.mdns_discovery=false;old_kind=0;refresh_server();assert(!server_ok && !opens && tick==HOST_PROBE_MS);
 assert(host_remaining(UINT32_MAX-20,30,100)==51 && host_remaining(40,30,100)==0);
 puts("PASS: actual bounded endpoint/recovery/authentication worker, publication/persistence, pairing preserved, reused-IP rejection, authenticated UUID mismatch, B at probe/discovery/auth, legacy/off discovery and clock wrap.");
}
'''
Path(sys.argv[1]).write_text(fixture.replace('/* CODE */',code))
