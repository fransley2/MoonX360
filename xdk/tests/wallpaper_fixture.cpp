#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <filesystem>
#include <windows.h>
#include "../src/ui/image.h"
#include "../src/safe_storage.h"
#include "../src/aurora_paths.h"
static bool oom;
static uint32_t *test_decode(const unsigned char *data,size_t len,int *w,int *h){return oom?NULL:image_decode_argb(data,len,w,h);}
#define image_decode_argb test_decode
#include "../src/wallpaper.h"
#undef image_decode_argb
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb/stb_image_write.h"
static std::string folder,selection;
static bool cancel_select,fail_storage,fail_config;
static int applied,messages,saves,last_w,last_h;
static struct {int ui_background;} cfg;
enum {AURORA_BROWSER_IMAGE=3,ICON_ERROR=1};
static bool aurora_browse_path(const char*,char path[160],int mode){assert(mode==AURORA_BROWSER_IMAGE);if(cancel_select)return false;snprintf(path,160,"%s",selection.c_str());return true;}
static bool storage_write(const char *name,const void *data,size_t len){assert(!strcmp(name,"home-wallpaper.png"));return !fail_storage && storage_replace_file((folder+"/home-wallpaper.png").c_str(),data,len);}
static void message(int,const char*,const char*){messages++;}
static void ui_home_set_user_wallpaper(const uint32_t *pixels,int w,int h){assert(pixels);applied++;last_w=w;last_h=h;}
static bool config_save(void*){saves++;return !fail_config;}
/* ACTUAL_SELECT */
struct encoded {std::vector<unsigned char> data;};
static void output(void *p,void *data,int len){encoded *e=(encoded*)p;unsigned char *s=(unsigned char*)data;e->data.insert(e->data.end(),s,s+len);}
static void write_bytes(const std::string &name,const std::vector<unsigned char> &bytes){FILE *f=fopen(name.c_str(),"wb");assert(f);assert(fwrite(bytes.data(),1,bytes.size(),f)==bytes.size());assert(!fclose(f));}
static std::vector<unsigned char> read_bytes(const std::string &name){size_t n=0;unsigned char *p=wallpaper_read_file(name.c_str(),&n);assert(p);std::vector<unsigned char> bytes(p,p+n);free(p);return bytes;}
int main(){
 char temp[MAX_PATH];GetTempPathA(MAX_PATH,temp);folder=std::string(temp)+"mx-wall-"+std::to_string(GetCurrentProcessId());std::filesystem::create_directories(folder);
 std::vector<unsigned char> pixels(1920*1080*4,127);encoded png,jpg;
 assert(stbi_write_png_to_func(output,&png,1920,1080,4,pixels.data(),1920*4));
 assert(!cover_decode_allowed(png.data.data(),png.data.size()));assert(wallpaper_allowed(png.data.data(),png.data.size()));
 selection=folder+"/fullhd.PNG";write_bytes(selection,png.data);choose_wallpaper();
 assert(applied==1 && saves==1 && cfg.ui_background==3 && last_w==1920 && last_h==1080 && !messages);
 std::vector<unsigned char> old=read_bytes(folder+"/home-wallpaper.png");assert(old==png.data);
 // Reload through the same decoder used at startup; extension does not control format.
 int w,h;uint32_t *p=wallpaper_decode(old.data(),old.size(),&w,&h);assert(p && w==1920 && h==1080);free(p);
 assert(stbi_write_jpg_to_func(output,&jpg,1920,1080,4,pixels.data(),90));selection=folder+"/game.JPEG";write_bytes(selection,jpg.data);choose_wallpaper();
 assert(applied==2 && saves==2 && read_bytes(folder+"/home-wallpaper.png")==jpg.data);
 assert(read_bytes(folder+"/home-wallpaper.png.bak")==old);
 cancel_select=true;choose_wallpaper();cancel_select=false;assert(applied==2 && saves==2);
 fail_storage=true;selection=folder+"/fullhd.PNG";choose_wallpaper();fail_storage=false;
 assert(applied==2 && read_bytes(folder+"/home-wallpaper.png")==jpg.data && messages==1);
 oom=true;choose_wallpaper();oom=false;assert(applied==2 && messages==2);
 std::vector<unsigned char> broken={'b','a','d'};selection=folder+"/broken.png";write_bytes(selection,broken);choose_wallpaper();assert(applied==2 && messages==3);
 auto bomb=png.data;bomb[16]=0;bomb[17]=0;bomb[18]=8;bomb[19]=1;assert(!wallpaper_allowed(bomb.data(),bomb.size()));
 assert(!wallpaper_allowed(png.data.data(),WALLPAPER_ENCODED_MAX+1));assert(!wallpaper_allowed(NULL,0));
 selection=folder+"/oversize.png";FILE *f=fopen(selection.c_str(),"wb");assert(f);assert(!fseek(f,WALLPAPER_ENCODED_MAX,SEEK_SET));assert(fputc(0,f)!=EOF);fclose(f);choose_wallpaper();assert(applied==2 && messages==4);
 assert(aurora_path_image_name("a.PNG") && aurora_path_image_name("a.Jpg") && aurora_path_image_name("a.JPEG"));
 assert(!aurora_path_image_name("a.xex") && !aurora_path_image_name("a.png.exe"));assert(aurora_path_xex_name("default.xex"));
 fail_config=true;selection=folder+"/fullhd.PNG";choose_wallpaper();assert(applied==3 && messages==5);fail_config=false;
 assert(storage_recoverable_name("home-wallpaper.png"));std::filesystem::remove_all(folder);
 puts("PASS: actual wallpaper selection, Full HD PNG/JPEG, restart decode, previous backup, cancelled/invalid/oversize/OOM/write failure, image filters and XEX compatibility");
}
