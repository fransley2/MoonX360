"""Real confirmation drawing, prepare helper and worker; simulated HTTP/thread boundary.

Session callbacks and gs_switch_app protocol internals have their own fixtures.
The scheduler is deterministic: input is polled by production run_task before
the pending worker executes on the next sleep, with no OS timing assumptions.
"""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
source = (root / 'src/app.c').read_text()
ui = (root / 'src/ui/ui.c').read_text()

def function(text, signature):
    a = text.index(signature)
    b = text.index('{', a) + 1
    depth = 1
    while depth:
        depth += (text[b] == '{') - (text[b] == '}')
        b += 1
    return text[a:b]

def structure(signature):
    a = source.index(signature)
    return source[a:source.index('};', a) + 2]

# Aurora and ordinary menus must enter the same gate before transport launch.
stream = function(source, 'static void stream(int app_id,')
assert stream.index('prepare_selected_app(app_id)') < stream.index('stream_session(')
direct = function(source, 'static bool aurora_direct_execute(')
assert 'stream(aurora_direct_data.app_id,aurora_direct_data.title);' in direct
assert 'stream_once(' not in direct and 'gs_switch_app(' not in direct

fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <deque>
#include <string>
#include <vector>
#include "../src/connection_flow.h"
#define MAX_APPS 8
enum {A_FLAG=1,B_FLAG=2,LEFT_FLAG=4,RIGHT_FLAG=8,UP_FLAG=16,DOWN_FLAG=32,ICON_ERROR=1};
struct gs_server {
 char uuid[80],address[64],app_version[32],gfe_version[32];
 bool paired;int current_game;volatile int *cancel;char *rtsp_session_url;
 struct {const char *address,*serverInfoAppVersion,*serverInfoGfeVersion,*rtspSessionUrl;} info;
};
struct gs_app {int id;};
static gs_server server;
static char server_error[256];
static bool server_ok,task_start_failed,task_was_cancelled,task_timed_out,task_interrupt_transport,ui_suspended;
static bool aurora_direct_active;
static int recoveries;
static int pair_calls,refreshes,pair_refresh_failure;
static bool pair_fail,pair_cancel;
static volatile int task_cancel,task_phase;
static unsigned task_generation,task_limit_ms=30000;
static uint32_t tick;
static int lists,switches,launches,errors,frees,joins,worker_inputs;
static bool offline,missing,removed,termination_fail,identity_change,in_worker,list_fail;
static int worker_delay;
static std::deque<int> buttons,rapid;
static std::vector<int> choices;
static std::vector<std::string> events,labels;
static std::vector<bool> focus;
static int plat_atomic_load(volatile int *p){return *p;}
static void plat_atomic_store(volatile int *p,int n){*p=n;}
static uint32_t plat_now_ms(){return tick;}
static void plat_log(const char*){}
static void cl_log(const char*,...){}
static void art_pause(bool){}
static void LiInterruptConnection(){assert(false);}
static void gs_set_request_cancel(volatile int*){}
static void task_state(int n){task_phase=n;}
static void message(int,const char*,const char*){errors++;}
static void discovery_pause(){events.push_back("pause");}
static void refresh_server(){
 events.push_back("refresh");refreshes++;server_ok=!offline;
 if(pair_calls && refreshes==2){
  server.current_game=7; // Busy state is learned only from the post-pair refresh.
  if(pair_refresh_failure==1)server_ok=false;
  if(pair_refresh_failure==2)task_was_cancelled=true;
  if(pair_refresh_failure==3)server.paired=false;
 }
}
static void do_pair(){
 assert(!aurora_direct_active && !server.paired && !in_worker);
 pair_calls++;events.push_back("pair");
 server.paired=!pair_fail;task_was_cancelled=pair_cancel;
}
static bool recover_host_ui(){assert(!aurora_direct_active && !in_worker);recoveries++;events.push_back("recover");return true;}
static int wait_button(){assert(!in_worker && !buttons.empty());int n=buttons.front();buttons.pop_front();return n;}
static int input_menu_pressed(){worker_inputs++;if(rapid.empty())return 0;int n=rapid.front();rapid.pop_front();return n;}
struct plat_thread {void *(*fn)(void*);void *arg;};static plat_thread pending;
static plat_thread *plat_thread_start(void *(*fn)(void*),void *arg){pending={fn,arg};return &pending;}
static void plat_thread_join(plat_thread*){assert(!pending.fn);joins++;}
static void plat_sleep_ms(unsigned ms){tick+=ms;if(pending.fn && --worker_delay<=0){auto fn=pending.fn;pending.fn=NULL;in_worker=true;fn(pending.arg);in_worker=false;}}
static void ui_present(){}
static void draw_busy(void*,uint32_t){}
static const char *gs_error(){return "simulated network failure";}
static int gs_applist(gs_server *h,gs_app *out,int cap){
 assert(in_worker && cap==MAX_APPS && h!=&server && h->cancel==&task_cancel);
 assert(h->info.address==h->address && h->info.serverInfoAppVersion==h->app_version);
 assert(h->info.serverInfoGfeVersion==h->gfe_version && !h->info.rtspSessionUrl && !h->rtsp_session_url);
 assert(task_phase==HOST_FETCHING_APPS);
 lists++;events.push_back("list");if(list_fail)return -1;
 out[0].id=(missing || (removed && lists>1))?999:42;return 1;
}
static bool gs_switch_app(gs_server *h,int previous,int target){
 assert(in_worker && previous==7 && target==42 && h->current_game==7);
 assert(task_phase==HOST_SWITCHING);
 assert(!strcmp(h->uuid,"selected-pc") && !strcmp(h->address,"10.0.0.7"));
 switches++;events.push_back("terminate");if(termination_fail)return false;
 h->current_game=0;if(identity_change)strcpy(server.uuid,"another-pc");return true;
}
static void gs_server_free(gs_server *h){assert(h!=&server);frees++;}

// Capture the actual UI draw output and card focus, rather than replacing it.
enum {FONT_BOLD,FONT_REGULAR,C_TEXT,C_TEXT_DIM,UI_CORNER};
struct {int w,h;} cv={1280,720};
struct hint {const char *key,*label;};
#define ARGB(a,b,c,d) 0
static void begin_frame(){labels.clear();focus.clear();}
static void header(const char*){} static void tabs(){} static void cv_unclip(void*){}
static void cv_fill(void*,int,int,int,int,int){}
static void card(int,int,int,int,int,bool selected){focus.push_back(selected);}
static void text(int,int,int,int,int,const char *s){labels.push_back(s);}
static void wrapped(int,int,int,int,int,int,int,const char *s){labels.push_back(s);}
static void footer(const hint *h,int n,const char*){assert(n==2);assert(!strcmp(h[0].key,"A") && !strcmp(h[1].key,"B") && !strcmp(h[1].label,"Cancel"));}
/* UI */
static void check_ui(int selected){
 ui_draw_switch_confirm(selected);choices.push_back(selected);
 assert(labels.size()==5 && labels[0]=="Switch Application?");
 assert(labels[1]=="Are you sure you want to launch this app? This will terminate the currently running app.");
 assert(labels[2]=="Any unsaved progress may be lost." && labels[3]=="Cancel" && labels[4]=="Switch App");
 assert(focus.size()==3 && focus[1]==(selected==0) && focus[2]==(selected==1));
}
/* TASK */
static void *run_busy(const char*,const char*,void *(*fn)(void*),void *arg){return run_task(fn,arg,draw_busy,NULL);}
/* PREPARE */
// Model only the caller's launch gate: real transport/callbacks are separate.
static bool select_app(){if(!prepare_selected_app(42))return false;assert(!pending.fn);launches++;events.push_back("launch");return true;}
static void reset(){
 memset(&server,0,sizeof(server));strcpy(server.uuid,"selected-pc");strcpy(server.address,"10.0.0.7");
 server.paired=true;server.current_game=7;server.rtsp_session_url=(char*)"old-session";
 server.info.rtspSessionUrl=server.rtsp_session_url;
 server_ok=true;offline=missing=removed=termination_fail=identity_change=list_fail=false;
 aurora_direct_active=false;recoveries=0;
 pair_calls=refreshes=pair_refresh_failure=0;pair_fail=pair_cancel=false;
 worker_delay=3;
 task_start_failed=task_was_cancelled=task_timed_out=false;task_cancel=0;
 lists=switches=launches=errors=frees=joins=worker_inputs=0;
 buttons.clear();rapid.clear();choices.clear();events.clear();
}
int main(){
 // Pairing must re-enter the same busy-app confirmation before any operation.
 for(int cancel: {A_FLAG,B_FLAG}){
  reset();server.paired=false;server.current_game=0;buttons={cancel};
  assert(!select_app());assert(pair_calls==1 && refreshes==2 && server.paired);
  assert(!lists && !switches && !launches && !joins && choices==std::vector<int>{0});
  assert((events==std::vector<std::string>{"pause","refresh","pair","refresh"}));
 }
 reset();server.paired=false;server.current_game=0;buttons={RIGHT_FLAG,A_FLAG};
 assert(select_app());assert(pair_calls==1 && refreshes==2 && lists==2 && switches==1 && launches==1);
 assert(choices.front()==0 && choices.back()==1 && buttons.empty());
 assert((events==std::vector<std::string>{"pause","refresh","pair","refresh","list","terminate","list","launch"}));
 for(int failure=0;failure<2;failure++){
  reset();server.paired=false;pair_fail=failure==0;pair_cancel=failure==1;
  assert(!select_app());assert(pair_calls==1 && refreshes==1);
  assert(!lists && !switches && !launches && !joins && choices.empty());
  assert((events==std::vector<std::string>{"pause","refresh","pair"}));
 }
 for(int failure=1;failure<=3;failure++){
  reset();server.paired=false;pair_refresh_failure=failure;
  assert(!select_app());assert(pair_calls==1 && refreshes==2);
  assert(!lists && !switches && !launches && !joins && choices.empty());
 }
 reset();server.paired=false;aurora_direct_active=true;
 assert(!select_app());assert(!pair_calls && refreshes==1);
 assert(!lists && !switches && !launches && !joins && !recoveries && choices.empty());
 assert((events==std::vector<std::string>{"pause","refresh"}));
 reset();buttons={A_FLAG};assert(!select_app());assert(!lists && !switches && !launches && choices==std::vector<int>{0});
 reset();buttons={B_FLAG};assert(!select_app());assert(!lists && !switches && !launches);
 reset();buttons={RIGHT_FLAG,LEFT_FLAG,A_FLAG};assert(!select_app());assert(!switches && !lists && choices.back()==0);
 reset();buttons={DOWN_FLAG,UP_FLAG,A_FLAG};assert(!select_app());assert(!switches && !lists && choices.back()==0);
 for(int direction: {RIGHT_FLAG,DOWN_FLAG}){
  reset();buttons={direction,A_FLAG};rapid={A_FLAG,A_FLAG,A_FLAG};assert(select_app());
  assert(lists==2 && switches==1 && launches==1 && joins==1 && frees==1 && server.current_game==0);
  assert(rapid.empty() && worker_inputs==3 && buttons.empty() && choices.front()==0 && choices.back()==1);
  assert((events==std::vector<std::string>{"pause","refresh","list","terminate","list","launch"}));
 }
 reset();offline=true;assert(!select_app());
 assert(recoveries==1 && !switches && !launches && !lists && !errors && choices.empty() && !joins);
 assert((events==std::vector<std::string>{"pause","refresh","recover"}));
 reset();offline=true;aurora_direct_active=true;assert(!select_app());
 assert(!recoveries && !switches && !launches && !lists && !errors && choices.empty() && !joins);
 assert((events==std::vector<std::string>{"pause","refresh"}));
 reset();list_fail=true;buttons={RIGHT_FLAG,A_FLAG};assert(!select_app());assert(lists==1 && !switches && !launches && errors==1);
 reset();missing=true;buttons={RIGHT_FLAG,A_FLAG};assert(!select_app());assert(lists==1 && !switches && !launches && errors==1);
 reset();termination_fail=true;buttons={RIGHT_FLAG,A_FLAG};assert(!select_app());assert(lists==1 && switches==1 && !launches && errors==1 && server.current_game==7);
 reset();removed=true;buttons={RIGHT_FLAG,A_FLAG};assert(!select_app());assert(lists==2 && switches==1 && !launches && errors==1);
 reset();identity_change=true;buttons={RIGHT_FLAG,A_FLAG};assert(!select_app());assert(!launches && errors==1);
 reset();server.current_game=42;assert(select_app());assert(choices.empty() && lists==1 && !switches);
 reset();server.current_game=0;assert(select_app());assert(choices.empty() && lists==1 && !switches);
 puts("PASS: actual switch UI text/default focus, A/B cancel, directional confirm, post-pair busy confirmation/switch, pairing failure/cancel/revalidation gates, Aurora no pairing, captured PC/previous/target, offline normal recovery/Aurora chooser handoff, stale/termination/identity gates, shared Aurora route, rapid A drained by actual task loop; callbacks tested separately.");
}
'''
task = structure('struct task {') + '\n' + '\n'.join(function(source, sig) for sig in (
    'static void *task_entry(', 'static void *run_task('))
prepare = structure('struct switch_job {') + '\n' + '\n'.join(function(source, sig) for sig in (
    'static void *switch_task(', 'static bool prepare_selected_app('))
# Only instrument the draw call to inspect each menu frame.
prepare = prepare.replace('ui_draw_switch_confirm(choice)', 'check_ui(choice)')
Path(sys.argv[1]).write_text(fixture.replace('/* UI */', function(ui, 'void ui_draw_switch_confirm('))
    .replace('/* TASK */', task).replace('/* PREPARE */', prepare))
