"""Real GameStream requests and MoonX360 scope cleanup; simulated hosts only."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[2]
app=(root/'src/app.c').read_text(encoding='utf-8')
gs=(root/'src/gamestream.c').read_text(encoding='utf-8')
def function(source,sig):
    a=source.index(sig);b=source.index('{',a)+1;d=1
    while d:d+=(source[b]=='{')-(source[b]=='}');b+=1
    return source[a:b]
fixture=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <Limelight.h>
#include "../src/host_compat.h"
#include <initializer_list>
struct client_identity {int unused;};
/* SERVER */
#include "../src/config.h"
#include "../src/wake.h"
#define UNIQUE_ID "fixture-client"
#define TIMEOUT_MS 10000
static const client_identity *ident;
static volatile int *request_cancel;
static char last_error[256];static gs_error_code last_error_kind;
static unsigned tick,host_app,launches,resumes,stops,http_calls;
static bool cancel_connect,wrong_identity,refuse_stop,fail_http;
static int info_calls,drift_at,change_at,offline_at,cancel_at,stop_delay;
static bool missing_identity,malformed_current,hold_stop;
static volatile int switch_cancel;
static const char *cancel_reply;static bool switch_testing;
static const char *software="Sunshine";
struct http_response {int status;char *body;size_t len;};
static int plat_atomic_load(volatile int *p){return *p;}
static void plat_atomic_store(volatile int *p,int n){*p=n;}
static uint32_t plat_now_ms(){return tick++;}
static void plat_log(const char*){}
static void plat_sleep_ms(unsigned int ms){tick+=ms;}
static void plat_diagnostics_checkpoint(const char*){}
static void cl_log(const char*,...){}
static void set_error(const char *format,const char *arg){snprintf(last_error,sizeof(last_error),format,arg?arg:"");}
static void make_uuid(char uuid[37]){strcpy(uuid,"fixture-request");}
static void crypto_random(void *p,size_t n){memset(p,1,n);}
static void bytes_to_hex(const unsigned char*,char *p,size_t n){memset(p,'1',n*2);p[n*2]=0;}
const char *LiGetLaunchUrlQueryParameters(){return "";}
static void http_response_free(http_response *r){free(r->body);r->body=NULL;}
static bool http_get_cancel(const char*,int,bool tls,const char *path,const client_identity*,int timeout,volatile int *cancel,http_response *r){
    assert(tls);
    http_calls++;if((cancel && *cancel) || fail_http)return false;
    assert(timeout>0);
    if(switch_testing && !strncmp(path,"/serverinfo",11))assert(timeout<=500);
    if(!strncmp(path,"/serverinfo",11)){
        info_calls++;
        if(hold_stop && info_calls>=3)tick+=(unsigned)timeout;
        if(offline_at==info_calls)return false;
        if(cancel_at==info_calls){switch_cancel=1;return false;}
        if(change_at==info_calls)host_app=99;
        if(stop_delay>0 && --stop_delay==0)host_app=0;
    }
    char body[1024];r->status=200;
    if(!strncmp(path,"/serverinfo",11))snprintf(body,sizeof(body),"<root status_code=\"200\"><uniqueid>%s</uniqueid><hostname>Fixture PC</hostname><appversion>7.1.0</appversion><state>%s</state><currentgame>%u</currentgame><PairStatus>1</PairStatus><ServerSoftware>%s</ServerSoftware></root>",missing_identity?"":(wrong_identity || drift_at==info_calls)?"wrong-host":"host-id",host_app?"SUNSHINE_SERVER_BUSY":"SUNSHINE_SERVER_FREE",host_app,software);
    else if(!strncmp(path,"/launch?",8) || !strncmp(path,"/resume?",8)) {
        unsigned id=0;const char *at=strstr(path,"appid=");assert(at && sscanf(at+6,"%u",&id)==1);
        if(path[1]=='l'){assert(!host_app);launches++;}else {assert(host_app==id);resumes++;}
        host_app=id;strcpy(body,"<root status_code=\"200\"><gamesession>1</gamesession><sessionUrl0>rtsp://fixture:48010</sessionUrl0></root>");
    } else {assert(!strncmp(path,"/cancel?",8));stops++;if(!refuse_stop && !hold_stop)host_app=0;snprintf(body,sizeof(body),"<root status_code=\"200\"><cancel>%d</cancel></root>",refuse_stop?0:1);}
    if(cancel_reply && !strncmp(path,"/cancel?",8))snprintf(body,sizeof(body),"<root status_code=\"200\">%s</root>",cancel_reply);
    if(malformed_current && !strncmp(path,"/serverinfo",11))strcpy(body,"<root status_code=\"200\"><appversion>7.1</appversion><state>SUNSHINE_SERVER_BUSY</state><currentgame>garbage</currentgame></root>");
    r->body=_strdup(body);r->len=strlen(body);return true;
}
static bool http_get_plain_probe(const char *a,int b,const char *c,int d,volatile int *e,http_response *f){return http_get_cancel(a,b,false,c,ident,d,e,f);}
/* GS */
static gs_server server;
static volatile int task_cancel,connection_poor;
static bool task_was_cancelled,task_timed_out,task_interrupt_transport,ui_suspended,connection_terminated;
static int termination_error;static unsigned session_generation,task_generation;
static const char *host_key(){return server.uuid;}
static bool stream_once(int id,const char*) {
    assert(!task_cancel && !server.cancel && !server.wake_cancel && !connection_terminated);
    STREAM_CONFIGURATION options={};options.width=1280;options.height=720;options.fps=60;options.bitrate=12000;options.audioConfiguration=AUDIO_CONFIGURATION_STEREO;
    gs_set_request_cancel(&task_cancel);assert(gs_launch(&server,&options,id,false,false));
    if(cancel_connect){task_cancel=1;task_was_cancelled=true;connection_terminated=true;termination_error=123;}
    ui_suspended=true;return false;
}
/* APP */
int main(){
    strcpy(server.address,"10.0.0.2");strcpy(server.uuid,"host-id");server.http_port=47989;server.https_port=47984;
    int old_cancel=1,fresh_cancel=0;server.current_game=host_app=42;server.cancel=&old_cancel;gs_set_request_cancel(&fresh_cancel);
    assert(!gs_quit(&server) && last_error_kind==GS_ERROR_CANCELLED); // original poisoned-token reproduction
    server.cancel=NULL;host_app=server.current_game=0;
    for(const char *backend:{"Sunshine","Apollo","Vibepollo"}) {
        software=backend;unsigned before_launch=launches,before_resume=resumes;
        for(int cycle=0;cycle<5;cycle++) {
            assert(!stream_session(42,"Desktop"));
            assert(!task_cancel && !server.cancel && !server.wake_cancel && !server.rtsp_session_url && !server.info.rtspSessionUrl && !server.launch_resumed && !ui_suspended && !connection_terminated);
        }
        assert(launches==before_launch+1 && resumes==before_resume+4);
        bool ok=false;task_generation++;gs_set_request_cancel(&task_cancel);quit_task(&ok);assert(ok && !host_app && !server.current_game);
        assert(!stream_session(42,"A"));task_generation++;gs_set_request_cancel(&task_cancel);quit_task(&ok);assert(ok);
        assert(!stream_session(84,"B"));task_generation++;gs_set_request_cancel(&task_cancel);quit_task(&ok);assert(ok && !host_app);
        cancel_connect=true;assert(!stream_session(42,"Cancel A"));cancel_connect=false;
        assert(!stream_session(42,"A again"));task_generation++;gs_set_request_cancel(&task_cancel);quit_task(&ok);assert(ok && !host_app);
        STREAM_CONFIGURATION cfg={};cfg.audioConfiguration=AUDIO_CONFIGURATION_STEREO;
        for(int cycle=0;cycle<5;cycle++){
            // Each host reports idle again: every request must really /launch.
            host_app=0;
            unsigned l=launches,r=resumes,q=stops;
            assert(gs_launch(&server,&cfg,42,false,false));
            assert(launches==l+1 && resumes==r && stops==q && host_app==42 && !server.launch_resumed);
        }
        int previous=42;
        for(int cycle=0;cycle<5;cycle++){
            int target=previous==42?84:42;
            unsigned l=launches,r=resumes,q=stops;
            assert(gs_switch_app(&server,previous,target));
            // Confirmed idle and cancel precede the separate target launch.
            assert(stops==q+1 && launches==l && resumes==r && host_app==0 && server.current_game==0);
            assert(gs_launch(&server,&cfg,target,false,false));
            assert(launches==l+1 && resumes==r && stops==q+1 && host_app==(unsigned)target && !server.launch_resumed);
            previous=target;
        }
        assert(gs_quit(&server));gs_stream_reset(&server);

    }
    host_app=server.current_game=42;wrong_identity=true;unsigned before=stops;assert(!gs_quit(&server) && last_error_kind==GS_ERROR_IDENTITY && stops==before);wrong_identity=false;strcpy(server.uuid,"host-id");
    host_app=84;assert(!gs_quit(&server) && last_error_kind==GS_ERROR_APP_MISSING && stops==before);
    server.current_game=84;strcpy(last_error,"Connection cancelled");refuse_stop=true;assert(!gs_quit(&server) && strstr(last_error,"Host refused to quit the app"));
    refuse_stop=false;assert(gs_quit(&server));
    // Real protocol switch paths: secure refresh -> guarded cancel -> observed idle.
    gs_set_request_cancel(&switch_cancel);server.cancel=NULL;server.info_timeout_ms=3210;
    switch_testing=true;
    for(int scenario=0;scenario<19;scenario++){
        cancel_reply=NULL;switch_cancel=0;info_calls=0;drift_at=change_at=offline_at=cancel_at=stop_delay=0;
        missing_identity=malformed_current=hold_stop=refuse_stop=wrong_identity=false;
        host_app=server.current_game=42;strcpy(server.uuid,"host-id");
        unsigned l=launches,r=resumes,q=stops,start=tick;
        bool expected=false;
        switch(scenario){
        case 0:expected=true;break;
        case 1:host_app=0;expected=true;break;
        case 2:host_app=84;expected=true;break;
        case 3:host_app=99;break;
        case 4:refuse_stop=true;break;
        case 5:hold_stop=true;break;
        case 6:drift_at=1;break;
        case 7:drift_at=2;break;
        case 8:drift_at=3;break;
        case 9:hold_stop=true;change_at=3;break;
        case 10:offline_at=3;break;
        case 11:cancel_at=3;break;
        case 12:missing_identity=true;break;
        case 13:malformed_current=true;break;
        case 14:hold_stop=true;stop_delay=5;expected=true;break;
        case 15:cancel_reply="<cancel>unsupported</cancel>";break;
        case 16:cancel_reply="";break;
        case 17:switch_cancel=1;break;
        case 18:change_at=2;break;
        }
        // A stale wake probe token must never route control refresh to plain HTTP.
        server.wake_cancel=&old_cancel;
        assert(gs_switch_app(&server,42,84)==expected);
        assert(launches==l && resumes==r && server.info_timeout_ms==3210);
        if(scenario==0 || scenario>=4 && scenario<=11 || (scenario==14 || scenario==15 || scenario==16))assert(stops==q+(scenario==6 || scenario==7?0:1));
        else assert(stops==q);
        if(scenario==5)assert(tick-start>=5000 && tick-start<5100 && last_error_kind==GS_ERROR_PROTOCOL);
        if(scenario==6 || scenario==7 || scenario==8 || scenario==12)assert(last_error_kind==GS_ERROR_IDENTITY);
        if(scenario==15 || scenario==16)assert(last_error_kind==GS_ERROR_PROTOCOL && strstr(last_error,"does not support cancel"));
        if(scenario==11 || scenario==17)assert(last_error_kind==GS_ERROR_CANCELLED);
        server.wake_cancel=NULL;
    }
    switch_testing=false;cancel_reply=NULL;switch_cancel=0;hold_stop=false;stop_delay=0;host_app=0;strcpy(server.uuid,"host-id");
    STREAM_CONFIGURATION options={};options.audioConfiguration=AUDIO_CONFIGURATION_STEREO;
    assert(gs_launch(&server,&options,42,false,false));host_app=0;
    assert(gs_launch(&server,&options,42,false,false));assert(gs_launch(&server,&options,42,false,false));
    unsigned count=http_calls;assert(!gs_launch(&server,&options,0,false,false) && !gs_launch(&server,&options,-1,false,false) && http_calls==count);
    assert(!gs_switch_app(&server,42,0) && http_calls==count);
    printf("PASS: real launch/resume/request/quit and scoped cleanup; 5 repeated cycles + A/B + cancel/relaunch per Sunshine/Apollo/Vibepollo fixture; 19 switch scenarios with bounded polling, no launch, unsupported cancel, stale tokens, app/identity guards and fresh errors (%u HTTP requests)\n",http_calls);
}
'''
signatures=['enum gs_error_code gs_error_kind(', 'const char *gs_error(', 'void gs_set_request_cancel(', 'static char *xml_get_from(', 'static char *xml_get(',
 'static const char *xml_span_find(', 'static const char *xml_span_text(', 'static bool xml_span_copy(',
 'static bool xml_span_identity(', 'static bool xml_status_ok(', 'static bool request(',
 'static bool load_serverinfo(', 'bool gs_launch(', 'bool gs_quit(', 'bool gs_switch_app(', 'void gs_stream_reset(', 'void gs_server_free(']
code='\n'.join(function(gs,s) for s in signatures)
application='\n'.join(function(app,s) for s in ['static bool stream_session(', 'static void *quit_task(void *arg)\n{'])
Path(sys.argv[1]).write_text(fixture.replace('/* SERVER */',(root/'src/gamestream.h').read_text(encoding='utf-8').split('struct gs_server {',1)[1].split('};',1)[0].join(['struct gs_server {','};'])).replace('/* GS */',code).replace('/* APP */',application),encoding='utf-8')
