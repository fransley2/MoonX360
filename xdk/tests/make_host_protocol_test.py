"""Production XML parsing and error mapping against small wire fixtures."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[2]
source=(root/'src/gamestream.c').read_text()
def fn(sig):
    a=source.index(sig);b=source.index('{',a)+1;depth=1
    while depth:depth+=(source[b]=='{')-(source[b]=='}');b+=1
    return source[a:b]
fixture=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/host_compat.h"
#include "../src/wake.h"
#include <Limelight.h>
#define UNIQUE_ID "test-client"
#define TIMEOUT_MS 10000
struct gs_server {gs_host_metadata metadata;char address[64],uuid[80],mac[18],hostname[64],app_version[32],gfe_version[32];
 int info_timeout_ms,server_major_version,codec_mode_support,current_game;bool paired,is_nvidia;unsigned short http_port,https_port;volatile int *wake_cancel,*cancel;SERVER_INFORMATION info;};
struct gs_app {int id;char name[64],uuid[80];};
struct http_response {int status;char *body;size_t len;};
static gs_error_code last_error_kind;static char last_error[256];static const char *wire;static int requests;
static volatile int *request_cancel;static uint32_t tick;static bool custom_tls;static int tls_calls;
static uint32_t plat_now_ms(){return tick;}
static int plat_atomic_load(volatile int *p){return *p;}
void LiInitializeServerInformation(PSERVER_INFORMATION p){memset(p,0,sizeof(*p));}
static void set_error(const char *fmt,const char *arg){snprintf(last_error,sizeof(last_error),fmt,arg?arg:"");}
static void make_uuid(char out[37]){strcpy(out,"request-uuid");}
static bool request(gs_server *host,bool tls,const char*,int,http_response *r){requests++;tick++;
 if(custom_tls && tls){tls_calls++;if(host->https_port!=48123)return false;}
r->body=_strdup(wire);r->len=strlen(wire);r->status=200;return true;}
static bool http_get_plain_probe(const char*,int,const char*,int,volatile int*,http_response *r){return request(NULL,false,NULL,0,r);}
static void http_response_free(http_response *r){free(r->body);r->body=NULL;}
/* CODE */
int main(){
 gs_server host={};wire="<root status_code=\"200\"><uniqueid>host-a</uniqueid><hostname>Gaming PC</hostname><appversion>7.1.431.-1</appversion><state>SUNSHINE_SERVER_FREE</state><currentgame>0</currentgame><PairStatus>1</PairStatus><Permission>50331648</Permission><Future>ignore</Future></root>";
 assert(load_serverinfo(&host,true) && host.paired && !strcmp(host.uuid,"host-a") && host.metadata.caps.app_list && !host.metadata.caps.app_launch);
 wire="<root status_code=\"200\"><App><AppTitle>One</AppTitle><ID>12</ID><UUID>stable-app-a</UUID></App><App><AppTitle>Two</AppTitle><ID>13</ID></App><App><AppTitle>Denied pseudo-entry</AppTitle><ID>0</ID></App></root>";
 gs_app apps[8];memset(apps,0x7f,sizeof(apps));assert(gs_applist(&host,apps,8)==2);
 assert(apps[0].id==12 && !strcmp(apps[0].uuid,"stable-app-a") && !apps[1].uuid[0] && host.metadata.caps.stable_app_uuid);
 char long_uuid[90];memset(long_uuid,'a',sizeof(long_uuid)-1);long_uuid[sizeof(long_uuid)-1]=0;
 char body[512];snprintf(body,sizeof(body),"<root status_code=\"200\"><App><AppTitle>Long</AppTitle><ID>5</ID><UUID>%s</UUID></App></root>",long_uuid);wire=body;
 assert(gs_applist(&host,apps,8)==1 && !apps[0].uuid[0]);
 wire="<root status_code=\"200\"><App><AppTitle>A</AppTitle><ID>1</ID><UUID>same-app</UUID></App><App><AppTitle>B</AppTitle><ID>2</ID><UUID>same-app</UUID></App><App><AppTitle>Bad</AppTitle><ID>9999999999999</ID></App></root>";
 assert(gs_applist(&host,apps,8)==2 && !apps[0].uuid[0] && !apps[1].uuid[0] && !host.metadata.caps.stable_app_uuid);
 host.metadata.caps.app_list=false;int before=requests;assert(gs_applist(&host,apps,8)==-1 && requests==before && last_error_kind==GS_ERROR_PERMISSION);
 assert(!xml_status_ok("<root status_code=\"403\" status_message=\"Permission denied\"/>") && last_error_kind==GS_ERROR_PERMISSION);
 assert(!xml_status_ok("<root status_code=\"404\" status_message=\"App not found\"/>") && last_error_kind==GS_ERROR_APP_MISSING);
 assert(!xml_status_ok("<root status_code=\"401\" status_message=\"Pair first\"/>") && last_error_kind==GS_ERROR_PAIRING);
 wire="<root status_code=\"200\"><hostname>Same name</hostname><appversion>99.1</appversion><state>SUNSHINE_SERVER_FREE</state><currentgame>0</currentgame></root>";
 assert(load_serverinfo(&host,false) && !host.uuid[0] && !host.metadata.caps.permissions && host.metadata.caps.app_launch && host.server_major_version==99);
 wire="<root status_code=\"200\"><uniqueid>host-a</uniqueid><HttpsPort>48123</HttpsPort><hostname>PC</hostname><appversion>7.1.0</appversion><state>SUNSHINE_SERVER_FREE</state><currentgame>0</currentgame><PairStatus>1</PairStatus></root>";
 memset(&host,0,sizeof(host));custom_tls=true;tick=0;int initial=requests;
 assert(gs_server_info_port(&host,"10.0.0.87",47989,100) && host.paired && host.https_port==48123 && tls_calls==2 && requests-initial==3 && tick<100);
 assert(host.info.address==host.address && host.info.serverInfoAppVersion==host.app_version);
 puts("PASS: advertised nonstandard HTTPS port recovered within shared budget; actual serverinfo/app-list parsing, optional scoped UUID, UUID truncation rejection, missing identity clearing, newer protocol fields and typed permission/pairing/app errors.");
}
'''
signatures=['static char *xml_get_from(', 'static char *xml_get(', 'static const char *xml_span_find(',
 'static const char *xml_span_text(', 'static bool xml_span_copy(', 'static bool xml_span_identity(',
 'static bool xml_status_ok(', 'static bool load_serverinfo(', 'bool gs_server_info_port(', 'int gs_applist(']
Path(sys.argv[1]).write_text(fixture.replace('/* CODE */','\n'.join(fn(sig) for sig in signatures)))
