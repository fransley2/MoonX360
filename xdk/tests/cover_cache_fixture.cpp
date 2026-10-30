#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <thread>
#include <mutex>
#include <set>
#include "../src/ui/cover_cache.h"
#include "../src/app_list_cache.h"
static app_list_cache app_cache;
#define MAX_APPS 64
static struct {int id;} apps[MAX_APPS];
static uint32_t *app_art[MAX_APPS],art_generation[MAX_APPS];
static int app_count,server;
static unsigned cache_count,peak;
static std::mutex allocation_lock;
static std::set<void*> allocations;
static void *fixture_alloc(size_t bytes){void *p=malloc(bytes);assert(p);std::lock_guard<std::mutex> guard(allocation_lock);allocations.insert(p);return p;}
static void fixture_free(void *p){if(!p)return;{std::lock_guard<std::mutex> guard(allocation_lock);assert(allocations.erase(p)==1);}free(p);}
static int plat_atomic_load(volatile int *p){return InterlockedCompareExchange((volatile LONG*)p,0,0);}
static void plat_atomic_store(volatile int *p,int n){InterlockedExchange((volatile LONG*)p,n);}
static void plat_sleep_ms(unsigned){std::this_thread::yield();}
static void ui_art_size(int *w,int *h){*w=150;*h=200;}
static void ui_drop_main_cache(){}
static void ui_draw_cover_memory(unsigned count,unsigned bytes){assert(bytes==count*150*200*4);cache_count=count;if(count>peak)peak=count;assert(count<=UI_COVER_LIMIT);}
static bool gs_app_art(int*,int id,unsigned char **png,size_t *len){if(id==3)return false;*png=(unsigned char*)fixture_alloc(1);**png=(unsigned char)id;*len=1;return true;}
static uint32_t *plat_decode_art(unsigned char *data,size_t,int w,int h){uint32_t *p=(uint32_t*)fixture_alloc(w*h*4);for(int i=0;i<w*h;i++)p[i]=*data;return p;}
struct plat_thread {std::thread worker;};
static void plat_thread_join(plat_thread *t){t->worker.join();delete t;}
#define free fixture_free
/* ACTUAL_COVER_WORKER */
#undef free
static void pump(const int *map,int selected,int iterations) {
    for(int i=0;i<iterations;i++) {art_update(map,app_count,selected,5);Sleep(1);}
}
int main() {
    for(int pass=0;pass<5;pass++) {
        app_count=40;int map[MAX_APPS];for(int i=0;i<app_count;i++){apps[i].id=i;map[i]=i;}
        plat_atomic_store(&art_cancel,0);plat_thread *loader=new plat_thread;
        loader->worker=std::thread([](){art_loader(NULL);});
        pump(map,0,80);assert(cache_count==11 && art_failed[3]); // missing cover is not retried
        int version=art_version;assert(app_art[0]&&app_art[0][0]==0 && art_generation[0]>0);
        pump(map,20,90);assert(cache_count==12 && !app_art[0] && art_version>version);
        pump(map,0,80);assert(app_art[0]&&art_generation[0]>(unsigned)version);
        for(int i=0;i<app_count;i++)art_update(map,app_count,i,5); // rapid navigation with one outstanding request
        stop_art_loader(&loader);assert(!loader && !art_result && art_ready==-1 && art_request==-1);
        stop_art_loader(&loader);clear_apps();assert(!cache_count && !app_count);
        assert(allocations.empty()); // includes cancellation while decode/publication is pending
    }
    assert(peak==UI_COVER_LIMIT);
    puts("PASS: actual asynchronous cover loader, acquire/release mailbox, missing cover, 12-cover bound, eviction/reload generation, rapid navigation and repeated stop/cleanup.");
}
