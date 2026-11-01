#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <thread>
#include <mutex>
#include <set>
#include <atomic>
#include <filesystem>
#include <string>
#include "../src/app_list_cache.h"
#include "../src/host_compat.h"
#include "../src/connection_flow.h"
static app_list_cache app_cache;
#define MAX_APPS 64
struct gs_app {int id;char name[64],uuid[80];};
static void reconcile_app_ids(gs_app*,int){}
static volatile int task_cancel;static bool task_was_cancelled,server_ok;static void task_state(int){}
static gs_app apps[MAX_APPS];
static uint32_t *app_art[MAX_APPS],art_generation[MAX_APPS];
static int app_count;
struct gs_server {gs_host_metadata metadata;volatile int *cancel;char address[64];char *rtsp_session_url;struct {char *rtspSessionUrl;} info;};
static gs_server server;
static void gs_server_free(gs_server *s){assert(!s->rtsp_session_url);}
static char host_id[96]="uuid:first";
static const char *host_key(){return host_id;}
static unsigned cache_count,peak;
static std::atomic<unsigned> tick(0);
static std::mutex allocation_lock;
static std::set<void*> allocations;
static void *fixture_alloc(size_t bytes){void *p=malloc(bytes);assert(p);std::lock_guard<std::mutex> guard(allocation_lock);allocations.insert(p);return p;}
static void fixture_free(void *p){if(!p)return;{std::lock_guard<std::mutex> guard(allocation_lock);assert(allocations.erase(p)==1);}free(p);}
static int plat_atomic_load(volatile int *p){return InterlockedCompareExchange((volatile LONG*)p,0,0);}
static void plat_atomic_store(volatile int *p,int n){InterlockedExchange((volatile LONG*)p,n);}
static uint32_t plat_now_ms(){return tick;}
static void plat_sleep_ms(unsigned){std::this_thread::yield();}
static void ui_art_size(int *w,int *h){*w=150;*h=200;}
static int ui_apps_rows(){return 1;}
static void ui_drop_main_cache(){}
static void ui_draw_cover_memory(unsigned count,unsigned bytes){assert(bytes==count*150*200*4);cache_count=count;if(count>peak)peak=count;assert(count<=12);}
static std::atomic<int> requests(0),inflight(0),max_inflight(0),fail_id(-1),hold(0),content(7);
static std::atomic<bool> decoder_allowed(true);
static std::string disk_dir;
static const char *storage_dir(){return disk_dir.empty()?NULL:disk_dir.c_str();}
static bool cover_decode_allowed(const unsigned char*,size_t){return decoder_allowed;}
static bool gs_app_art(gs_server*,int id,unsigned char **png,size_t *len){
 requests++;int live=++inflight;if(live>max_inflight)max_inflight=live;
 while(hold)Sleep(1);--inflight;if(id==fail_id)return false;
 *png=(unsigned char*)fixture_alloc(1);**png=(unsigned char)(id+content);*len=1;return true;
}
static uint32_t *plat_decode_art(unsigned char *data,size_t,int w,int h){uint32_t *p=(uint32_t*)fixture_alloc(w*h*4);for(int i=0;i<w*h;i++)p[i]=*data;return p;}
struct plat_thread {std::thread worker;};
static plat_thread *art_worker;
static plat_thread *plat_thread_start(void *(*f)(void*),void *p){plat_thread *t=new plat_thread;t->worker=std::thread([=]{f(p);});return t;}
static void plat_thread_join(plat_thread *t){t->worker.join();delete t;}
static void plat_log(const char*){}
static void cl_log(const char*,...){}
static bool metadata_fail;
static int gs_applist(gs_server*,gs_app *out,int capacity){
 assert(capacity==MAX_APPS);if(metadata_fail)return -1;
 out[0].id=40;strcpy(out[0].name,"New title");out[1].id=5;strcpy(out[1].name,"Other");return 2;
}
static void run_busy(const char*,const char*,void *(*fn)(void*),void *p){
 int count=app_count,first=apps[0].id;std::thread t([=]{fn(p);});t.join();
 assert(app_count==count && apps[0].id==first); // no worker publication
}
#define HOME_ART_LIMIT 4
static struct {struct {bool pinned;} entries[HOME_ART_LIMIT];} home_art;
static const uint32_t *home_art_get(const void*,const char*,int,uint32_t*){return NULL;}
static void home_art_remember(void*,const char*,int,const uint32_t*,int,int,uint32_t){}
#define malloc fixture_alloc
#define free fixture_free
#include "../src/ui/cover_cache.h"
#include "../src/cover_disk.h"
/* ACTUAL_COVER_WORKER */
#undef free
#undef malloc
static void finish(){
 for(int i=0;i<3000 && plat_atomic_load(&art_ready)<0;i++)Sleep(1);
 assert(plat_atomic_load(&art_ready)>=0);art_drain();
}
static ui_cover_key key(int id){ui_cover_key k;art_key(&k,id);return k;}
int main(){
 app_count=40;int map[MAX_APPS];for(int i=0;i<app_count;i++){apps[i].id=i+1;map[i]=i;}
 art_loader_start();plat_thread *same=art_worker;art_loader_start();assert(same==art_worker);
 art_update(map,app_count,4,6);assert(art_job.key.app==5);finish();
 int baseline=requests;uint32_t *pixels=app_art[4];unsigned generation=art_generation[4];
 for(tick=1;tick<UI_COVER_VALID_MS;tick+=UI_COVER_VALID_MS/10){assert(!art_request_app(5,true,true));assert(app_art[4]==pixels);}
 assert(requests==baseline);
 tick=UI_COVER_VALID_MS+1;assert(art_request_app(5,true,true));assert(app_art[4]==pixels);finish();
 assert(app_art[4]==pixels && art_generation[4]==generation); // identical content, no GPU generation/upload change
 tick=UI_COVER_VALID_MS*2+2;fail_id=5;assert(art_request_app(5,true,true));assert(app_art[4]==pixels);finish();
 assert(app_art[4]==pixels);baseline=requests;tick+=9999;assert(!art_request_app(5,true,true));assert(requests==baseline);
 tick++;fail_id=-1;content++;assert(art_request_app(5,true,true));assert(app_art[4]==pixels);finish();
 assert(art_generation[4]!=generation);pixels=app_art[4];
 ui_cover_invalidate(&art_cache,host_key());assert(app_art[4]==pixels);assert(art_request_app(5,true,true));finish();
 // Metadata refresh publishes only on the menu and preserves an in-flight result.
 hold=1;assert(art_request_app(40,true,true));while(!inflight)Sleep(1);
 unsigned context=art_context;int refreshed=-1;artwork_refresh_apps("Apps",&refreshed);
 assert(refreshed==2 && art_context==context && app_art[1]==pixels);
 hold=0;finish();assert(app_art[0] && app_art[0][0]==48);
 metadata_fail=true;artwork_refresh_apps("Apps",&refreshed);metadata_fail=false;
 assert(refreshed==-1 && app_count==2 && app_art[1]==pixels);
 // Reordering and reconciliation preserve identity, never the old list index.
 apps[0].id=5;apps[1].id=1;art_sync_views();assert(app_art[0]==pixels && !app_art[1]);
 opening_art_key=key(5);opening_art_pin=true;int pinned=ui_cover_find(&art_cache,&opening_art_key);art_cache.items[pinned].pinned=true;
 for(int id=6;id<30;id++){assert(art_request_app(id,false,true));finish();}
 assert(art_cache.items[pinned].pixels==pixels && peak==12);
 // No prefetch eviction, no new requests while streaming.
 assert(!art_request_app(40,false,false));art_pause(true);baseline=requests;tick+=UI_COVER_VALID_MS;
 assert(!art_request_app(5,true,true) && requests==baseline);art_pause(false);
 // A job published after switching host/list cannot reach any wrong app view.
 hold=1;assert(art_request_app(40,true,true));while(!inflight)Sleep(1);
 clear_apps();strcpy(host_id,"uuid:second");app_count=1;apps[0].id=40;art_sync_views();hold=0;finish();assert(!app_art[0]);
 assert(art_request_app(40,true,true));finish();assert(app_art[0] && app_art[0][0]==48);
 strcpy(host_id,"uuid:first");apps[0].id=5;art_sync_views();assert(app_art[0]==pixels);
 // Explicit dimensions are part of identity.
 ui_cover_key other=key(5);other.w++;assert(ui_cover_find(&art_cache,&other)<0);
 // A real failure remains cooling after its empty decoded slot is evicted.
 opening_art_pin=false;tick+=UI_COVER_VALID_MS;fail_id=50;assert(art_request_app(50,false,true));finish();
 baseline=requests;
 for(int id=51;id<64;id++){assert(art_request_app(id,false,true));finish();}
 assert(ui_cover_find(&art_cache,&(other=key(50)))<0);
 assert(art_request_app(50,false,true));finish(); // one disk lookup, HTTP still forbidden
 assert(requests==baseline+13 && !art_request_app(50,false,true));
 tick+=10000;assert(art_request_app(50,false,true));finish();assert(requests==baseline+14);fail_id=-1;
 // Actual disk warm-up, eviction and restart avoid both HTTP and decode.
 char temp[MAX_PATH];GetTempPathA(MAX_PATH,temp);disk_dir=std::string(temp)+"moonx-art-worker-"+std::to_string(GetCurrentProcessId());
 std::filesystem::create_directories(disk_dir);tick+=UI_COVER_VALID_MS;
 assert(art_request_app(64,false,true));finish();assert(cover_disk_find(&art_disk,&(other=key(64)))>=0);
 baseline=requests;
 for(int id=1;id<14;id++){assert(art_request_app(id,false,true));finish();}
 assert(ui_cover_find(&art_cache,&(other=key(64)))<0);
 decoder_allowed=false;assert(art_request_app(64,false,true));finish();assert(requests==baseline+13);
 assert(art_cache.items[ui_cover_find(&art_cache,&other)].pixels);
 stop_art_loader(&art_worker);ui_cover_dispose(&art_cache);memset(&art_disk,0,sizeof(art_disk));art_pause(false);art_loader_start();
 baseline=requests;assert(art_request_app(64,false,true));finish();assert(requests==baseline);
 // A rejected source is a decode failure and receives the same persistent cooldown.
 assert(art_request_app(30,false,true));finish();assert(!art_cache.items[ui_cover_find(&art_cache,&(other=key(30)))].pixels);
 assert(!ui_cover_retry(&art_cache,&other,tick));decoder_allowed=true;
 // Optional UUID identity survives both a host endpoint change and numeric app ID change.
 app_count=1;apps[0].id=21;strcpy(apps[0].uuid,"stable-game-uuid");tick+=UI_COVER_VALID_MS;
 assert(art_request_app(21,true,true));assert(art_job.protocol_app==21 && art_job.key.app==1);finish();
 assert(app_art[0]);uint32_t *stable=app_art[0];unsigned stable_generation=art_generation[0];
 strcpy(server.address,"10.0.0.87");apps[0].id=22;art_sync_views();baseline=requests;
 assert(app_art[0]==stable && art_generation[0]==stable_generation && !art_request_app(22,true,true) && requests==baseline);
 stop_art_loader(&art_worker);ui_cover_dispose(&art_cache);memset(&art_disk,0,sizeof(art_disk));art_pause(false);art_loader_start();
 baseline=requests;assert(art_request_app(22,true,true));finish();assert(requests==baseline && app_art[0]);
 for(int id=0;id<app_count;id++)art_update(map,app_count,id,6);
 stop_art_loader(&art_worker);assert(!art_worker && !art_result && art_ready==-1 && art_request==-1);
 stop_art_loader(&art_worker);ui_cover_dispose(&art_cache);art_memory();assert(!cache_count && allocations.empty() && max_inflight==1);
 std::filesystem::remove_all(disk_dir);
 puts("PASS: real single worker, stable host/app/size, daily reuse, stale refresh, same-content generation, 10s retry, manual refresh, reorder, pinned 12-entry LRU, host switch, streaming pause, no duplicate work and leak-free stop");
}
