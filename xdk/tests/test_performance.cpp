#include <assert.h>
#include <stdio.h>
#include "../../src/performance.h"
#include "../../src/app_list_cache.h"
int main(){
    perf_window w={0};
    for(unsigned i=1;i<=1000;i++)perf_window_add(&w,i);
    assert(w.count==1000 && w.filled==128 && w.next==104 && w.total==500500 && w.maximum==1000);
    for(unsigned i=0;i<128;i++)assert(w.samples[i]>=873 && w.samples[i]<=1000);
    perf_window_add(&w,0x100000001ull);assert(w.maximum==0xffffffffu);
    assert(!perf_enabled() && perf_clock()==0);{PerfScope scope(PERF_INPUT_WORK);}
    app_list_cache cache={0};assert(!app_list_cache_valid(&cache,"uuid-a",0));
    app_list_cache_store(&cache,"uuid-a",10);
    assert(app_list_cache_valid(&cache,"uuid-a",30009));
    assert(!app_list_cache_valid(&cache,"uuid-a",30010));
    assert(!app_list_cache_valid(&cache,"uuid-b",10));
    app_list_cache_store(&cache,"uuid-a",0xfffffff0u);
    assert(app_list_cache_valid(&cache,"uuid-a",100));
    assert(!app_list_cache_valid(&cache,"uuid-a",30000));
    char oversized[100];memset(oversized,'a',99);oversized[99]=0;
    app_list_cache_store(&cache,oversized,0);assert(!cache.valid);
    puts("PASS: bounded profiling window, disabled no-op, stable identity, TTL and tick wrap.");
}
