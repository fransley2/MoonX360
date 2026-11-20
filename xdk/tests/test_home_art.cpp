#include "../../src/home_art.h"
#include <assert.h>
#include <map>
#include <string>
#include <vector>
static std::map<std::string,std::vector<char> > files;
static unsigned reads,writes;
static bool readonly;
char *storage_read(const char *name,size_t *len) {
    reads++;auto i=files.find(name);if(i==files.end())return NULL;
    *len=i->second.size();char *p=(char*)malloc(*len);memcpy(p,i->second.data(),*len);return p;
}
bool storage_write(const char *name,const void *p,size_t n) {
    writes++;if(readonly)return false;
    files[name]=std::vector<char>((const char*)p,(const char*)p+n);return true;
}
static void release(home_art_cache &cache) {
    for(int i=0;i<HOME_ART_LIMIT;i++)free(cache.entries[i].blob);
    memset(&cache,0,sizeof(cache));
}
int main() {
    home_art_cache cache={};uint32_t pixels[12],generation=0;
    for(int i=0;i<12;i++)pixels[i]=0xFF112233u+i;
    home_art_load(&cache,3,4);assert(reads==4);
    home_art_remember(&cache,"PC",42,pixels,3,4,1);assert(writes==1 && files.size()==1);
    for(int i=0;i<100;i++) {
        home_art_remember(&cache,"PC",42,pixels,3,4,1);
        const uint32_t *p=home_art_get(&cache,"PC",42,&generation);
        assert(p && !memcmp(p,pixels,sizeof(pixels)) && (generation&0x80000000u));
        home_art_load(&cache,3,4);
    }
    assert(reads==4 && writes==1 && !home_art_get(&cache,"Other PC",42,&generation));
    release(cache);home_art_load(&cache,3,4);
    assert(home_art_get(&cache,"PC",42,&generation));
    release(cache);home_art_load(&cache,6,2);assert(!home_art_get(&cache,"PC",42,&generation));
    release(cache);home_art_load(&cache,3,4);
    for(int id=43;id<=45;id++)home_art_remember(&cache,"PC",id,pixels,3,4,2);
    assert(files.size()==4);
    for(int i=0;i<4;i++)cache.entries[i].pinned=false;
    cache.entries[home_art_slot(&cache,"PC",42)].pinned=true;
    home_art_remember(&cache,"PC",99,pixels,3,4,3);
    assert(home_art_get(&cache,"PC",42,&generation) && home_art_get(&cache,"PC",99,&generation));
    assert(files.size()==4);
    readonly=true;unsigned previous=writes;
    home_art_remember(&cache,"PC",99,pixels,3,4,4);
    home_art_remember(&cache,"PC",99,pixels,3,4,4);
    assert(writes==previous+1 && home_art_get(&cache,"PC",99,&generation)); // no write retry on focus
    release(cache);files.clear();
    files["home-art-0.bin"]=std::vector<char>(1,'x');
    home_art_header bad={};memcpy(bad.magic,"BADMAGIC",8);
    std::vector<char> blob(sizeof(bad)+sizeof(pixels));memcpy(blob.data(),&bad,sizeof(bad));files["home-art-1.bin"]=blob;
    home_art_load(&cache,3,4);
    for(int i=0;i<4;i++)assert(!cache.entries[i].blob);
    release(cache);
    puts("PASS: four-thumbnail bound, host/ID/dimension isolation, restart persistence, pinned eviction, generation identity, malformed files, readonly storage and no repeated I/O on focus.");
}
