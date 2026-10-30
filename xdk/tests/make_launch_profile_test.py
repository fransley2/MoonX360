"""Verify actual GameStream launch/resume query parameters for every stream profile."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[2]
s=(root/'src/gamestream.c').read_text()
a=s.index('bool gs_launch(');b=s.index('\nbool gs_quit(',a)
fixture=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Limelight.h>
#include "../src/config.h"
#include "../src/wake.h"
#define UNIQUE_ID "0123456789ABCDEF"
struct gs_server {char uuid[80];int current_game;bool is_nvidia;char *rtsp_session_url;SERVER_INFORMATION info;};
struct http_response {char *body;};
static char sent[2048];static bool reject,changed;
static bool load_serverinfo(gs_server *s,bool){if(changed)strcpy(s->uuid,"different-pc");return !reject;}
static void set_error(const char*,const char*){}
static void crypto_random(void *p,size_t n){memset(p,1,n);}
static void bytes_to_hex(const unsigned char*,char *out,size_t n){memset(out,'1',n*2);out[n*2]=0;}
static void make_uuid(char out[37]){strcpy(out,"example-uuid");}
const char *LiGetLaunchUrlQueryParameters(){return "";}
static bool request(gs_server*,bool https,const char *path,int timeout,http_response *r){assert(https && timeout==60000);strcpy(sent,path);r->body=NULL;return true;}
static bool xml_status_ok(const char*){return true;}
static char *xml_get(const char*,const char *tag){return _strdup(!strcmp(tag,"sessionUrl0")?"rtsp://host:48010":"1");}
static void http_response_free(http_response*){}
/* CODE */
char *storage_read(const char*,size_t*){return NULL;}
bool storage_write(const char*,const void*,size_t){return true;}
int main(){
    for(int mode=1;mode<CONFIG_PROFILE_COUNT;mode++)for(int resume=0;resume<2;resume++)for(int sops=0;sops<2;sops++) {
        config c;config_defaults(&c);config_apply_preset(&c,mode);
        STREAM_CONFIGURATION sc={};sc.width=c.width;sc.height=c.height;sc.fps=c.fps;sc.bitrate=c.bitrate_kbps;
        sc.audioConfiguration=AUDIO_CONFIGURATION_STEREO;sc.supportedVideoFormats=VIDEO_FORMAT_H264;
        gs_server host={};host.current_game=resume?42:0;
        assert(gs_launch(&host,&sc,42,sops!=0,false));
        char expected[80];snprintf(expected,sizeof(expected),"mode=%dx%dx%d",c.width,c.height,c.fps);assert(strstr(sent,expected));
        snprintf(expected,sizeof(expected),"&sops=%d",sops);assert(strstr(sent,expected));
        assert(strstr(sent,resume?"/resume?":"/launch?")==sent && host.current_game==42);
        assert(sc.bitrate==c.bitrate_kbps && sc.supportedVideoFormats==VIDEO_FORMAT_H264);
        assert(host.info.rtspSessionUrl==host.rtsp_session_url);free(host.rtsp_session_url);
    }
    gs_server host={};host.current_game=99;STREAM_CONFIGURATION sc={};sent[0]=0;
    assert(!gs_launch(&host,&sc,42,true,false) && !sent[0]);reject=true;host.current_game=0;
    assert(!gs_launch(&host,&sc,42,true,false));
    reject=false;changed=true;strcpy(host.uuid,"selected-pc");sent[0]=0;assert(!gs_launch(&host,&sc,42,true,false) && !sent[0]);
    puts("PASS: actual GameStream launch/resume, exact profile mode, sops 0/1, bitrate retained for RTSP, H264 only and stale-session rejection.");
}
'''
Path(sys.argv[1]).write_text(fixture.replace('/* CODE */',s[a:b]))
