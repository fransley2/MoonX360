"""Actual UUID history reconciliation with durable/failed/unknown manifests."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[2];source=(root/'src/app.c').read_text()
a=source.index('static void reconcile_app_ids(');b=source.index('{',a)+1;depth=1
while depth:depth+=(source[b]=='{')-(source[b]=='}');b+=1
fixture=r'''
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <map>
#include <string>
#include "../src/app_identity.h"
#include "../src/session_history.h"
struct gs_app {int id;char name[64],uuid[80];};
static session_history session_history;
static std::map<std::string,std::string> files;static bool fail_history,fail_map;
static const char *host_key(){return "uuid:host-a";}
static char *storage_read(const char *path,size_t*){
 auto it=files.find(path);if(it==files.end()){errno=ENOENT;return NULL;}return _strdup(it->second.c_str());
}
static bool storage_write(const char *path,const void *p,size_t n){
 if((fail_history && !strcmp(path,"sessions.tsv")) || (fail_map && !strncmp(path,"app-ids-",8)))return false;
 files[path]=std::string((const char*)p,n);return true;
}
/* CODE */
static void reset(){
 files.clear();memset(&session_history,0,sizeof(session_history));fail_history=fail_map=false;
 session_history.count=3;
 for(int i=0;i<3;i++){session_record *r=session_history.records+i;strcpy(r->host,i==2?"uuid:host-b":"uuid:host-a");r->app_id=11+i;r->width=1280;r->height=720;r->fps=60;r->bitrate=12000;strcpy(r->app,"Saved game");}
 char path[64];app_identity_path(path,host_key());files[path]="MOONX_APP_IDS 1\tuuid:host-a\n11\tapp-a\n12\tapp-b\n";
}
int main(){
 gs_app next[2]={{12,"A","app-a"},{11,"B","app-b"}};char path[64];app_identity_path(path,host_key());
 reset();reconcile_app_ids(next,2);
 assert(session_history.records[0].app_id==12 && !strcmp(session_history.records[0].app_uuid,"app-a"));
 assert(session_history.records[1].app_id==11 && !strcmp(session_history.records[1].app_uuid,"app-b") && session_history.records[2].app_id==13);
 reconcile_app_ids(next,2);assert(session_history.records[0].app_id==12 && session_history.records[1].app_id==11);
 struct session_history restarted;history_decode(&restarted,files["sessions.tsv"].c_str());assert(restarted.count==3 && !strcmp(restarted.records[0].app_uuid,"app-a"));
 reset();fail_history=true;reconcile_app_ids(next,2);assert(files[path].find("11\tapp-a")!=std::string::npos);
 fail_history=false;reconcile_app_ids(next,2);assert(session_history.records[0].app_id==12 && session_history.records[1].app_id==11);
 reset();fail_map=true;reconcile_app_ids(next,2);session_history=restarted;fail_map=false;reconcile_app_ids(next,2);
 assert(session_history.records[0].app_id==12 && session_history.records[1].app_id==11);
 reset();files[path]="MOONX_APP_IDS 99\tfuture\n";reconcile_app_ids(next,2);
 assert(files[path]=="MOONX_APP_IDS 99\tfuture\n" && !session_history.records[0].app_uuid[0] && session_history.records[0].app_id==11);
 puts("PASS: actual history UUID reconciliation, numeric ID swaps, host isolation, v1 UUID persistence, idempotent partial-write retry and future-manifest preservation.");
}
'''
Path(sys.argv[1]).write_text(fixture.replace('/* CODE */',source[a:b]))
