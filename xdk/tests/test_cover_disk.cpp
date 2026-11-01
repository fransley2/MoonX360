#include "../../src/cover_disk.h"
#include <assert.h>
#include <filesystem>
#include <vector>
#include <string>
#include <windows.h>
static ui_cover_key key(int app,int w=150,int h=200){ui_cover_key k={};strcpy(k.host,"uuid:pc");k.app=app;k.w=w;k.h=h;return k;}
static void check_bound(const char *dir){
 size_t bytes=0,count=0;
 for(auto &e:std::filesystem::directory_iterator(dir)){bytes+=e.file_size();count++;}
 assert(bytes<=COVER_DISK_BUDGET && count<=COVER_DISK_SLOTS);
}
int main(){
 char temp[MAX_PATH];GetTempPathA(MAX_PATH,temp);
 std::string dir=std::string(temp)+"moonx-art-disk-"+std::to_string(GetCurrentProcessId());std::filesystem::create_directories(dir);
 cover_disk_cache c={};std::vector<uint32_t> pixels(150*200,0xff123456);ui_cover_key k=key(1);uint32_t fetched=0;
 assert(cover_disk_write(&c,dir.c_str(),&k,pixels.data(),100));check_bound(dir.c_str());
 uint32_t *p=cover_disk_read(&c,dir.c_str(),&k,200,&fetched);assert(p && fetched==100 && !memcmp(p,pixels.data(),120000));free(p);
 k.app=2;assert(!cover_disk_read(&c,dir.c_str(),&k,200,&fetched));k=key(1,200,150);assert(!cover_disk_read(&c,dir.c_str(),&k,200,&fetched));
 k=key(1);strcpy(k.host,"uuid:other");assert(!cover_disk_read(&c,dir.c_str(),&k,200,&fetched));k=key(1);
 memset(&c,0,sizeof(c));p=cover_disk_read(&c,dir.c_str(),&k,1000,&fetched);assert(p && (uint32_t)(1000-fetched)<UI_COVER_VALID_MS);free(p);
 p=cover_disk_read(&c,dir.c_str(),&k,50000,&fetched);assert(p && (uint32_t)(1000-fetched)<UI_COVER_VALID_MS);free(p); // hydration never resets freshness
 int slot=cover_disk_find(&c,&k);char path[256];cover_disk_path(path,sizeof(path),dir.c_str(),slot);
 FILE *f=fopen(path,"r+b");assert(f);fseek(f,sizeof(cover_disk_header)+10,SEEK_SET);fputc(99,f);fclose(f);
 assert(!cover_disk_read(&c,dir.c_str(),&k,50000,&fetched)); // payload checksum
 assert(cover_disk_write(&c,dir.c_str(),&k,pixels.data(),50001));
 for(int i=2;i<=40;i++){k=key(i);assert(cover_disk_write(&c,dir.c_str(),&k,pixels.data(),i));check_bound(dir.c_str());}
 assert(c.bytes<=COVER_DISK_BUDGET);
 // Bigger thumbnails force byte-budget eviction before the entry limit.
 pixels.resize(300*400);for(size_t i=0;i<pixels.size();i++)pixels[i]=(uint32_t)(i*2654435761u);
 for(int i=41;i<53;i++){k=key(i,300,400);assert(cover_disk_write(&c,dir.c_str(),&k,pixels.data(),i));check_bound(dir.c_str());}
 memset(&c,0,sizeof(c));p=cover_disk_read(&c,dir.c_str(),&k,123,&fetched);assert(p);free(p);check_bound(dir.c_str());
 // Bad magic, truncated payload, oversized dimensions and missing storage are misses.
 slot=cover_disk_find(&c,&k);cover_disk_path(path,sizeof(path),dir.c_str(),slot);f=fopen(path,"wb");assert(f);fputs("bad",f);fclose(f);
 memset(&c,0,sizeof(c));assert(!cover_disk_read(&c,dir.c_str(),&k,0,&fetched));check_bound(dir.c_str());
 cover_disk_header bad={};memcpy(bad.magic,"XCVR003",8);bad.key=key(3,100000,100000);bad.bytes=4;assert(!cover_disk_header_valid(&bad));
 cover_disk_cache unavailable={};k=key(1);assert(!cover_disk_write(&unavailable,NULL,&k,pixels.data(),0));
 std::string missing=dir+"/missing";assert(!cover_disk_write(&unavailable,missing.c_str(),&k,pixels.data(),0));assert(unavailable.write_failed);
 assert(!cover_disk_write(&unavailable,dir.c_str(),&k,pixels.data(),1)); // storage circuit breaker, RAM remains independent
 std::filesystem::remove_all(dir);
 puts("PASS: bounded disk slots/bytes, exact identity, checksum/corruption, restart, stable freshness, byte eviction and storage failure circuit breaker");
}
