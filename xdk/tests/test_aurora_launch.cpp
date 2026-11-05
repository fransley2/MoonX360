#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../src/aurora_launch.h"
#include "../../src/aurora_index.h"
#include "../../src/aurora_paths.h"
#include "../../src/aurora_mount_map.h"
#include "../../src/aurora_link_io.h"
#include "../../src/aurora_metadata.h"
#include "../../src/aurora_detect.h"

static int matching_mount(const char *path,void *context)
{return !strcmp(path,(const char*)context);}
struct fake_aurora_fs { const char *xex; int readable,cancel; };
static int fake_aurora_xex(const char *path,void *context)
{fake_aurora_fs *fs=(fake_aurora_fs*)context;return fs->readable && !strcmp(path,fs->xex);}
static int fake_aurora_dirs(const char *folder,char names[][64],int capacity,void *)
{
    if(capacity<1)return -1;
    const char *next=NULL;
    if(!strcmp(folder,"Hdd1:\\"))next="Homebrew";
    else if(!strcmp(folder,"Hdd1:\\Homebrew"))next="Dashboards";
    else if(!strcmp(folder,"Hdd1:\\Homebrew\\Dashboards"))next="Custom";
    if(!next)return 0;
    strcpy(names[0],next);return 1;
}
static int fake_aurora_cancel(void *context)
{return ((fake_aurora_fs*)context)->cancel;}

int main()
{
    moonx_launch_data d={};
    strcpy(d.host_id,"01234567-89ab-cdef-0123-456789abcdef");
    d.app_id=123;strcpy(d.profile_id,"factory:1");
    strcpy(d.title,"Hollow Knight");
    strcpy(d.main_xex,"Hdd1:\\Apps\\MoonX360\\default.xex");
    moonx_launch_seal(&d);
    assert(moonx_launch_valid(&d,sizeof(d)));
    moonx_aurora_metadata meta={};strcpy(meta.host_id,d.host_id);meta.app_id=d.app_id;
    strcpy(meta.display_title,d.title);strcpy(meta.description,"PC game streamed through MoonX360.");
    strcpy(meta.source,"none");moonx_aurora_metadata_seal(&meta);
    assert(moonx_aurora_metadata_valid(&meta,sizeof(meta),&d));
    moonx_aurora_metadata another=meta;strcpy(another.display_title,"Blasphemous");
    another.app_id=124;moonx_aurora_metadata_seal(&another);
    moonx_launch_data second=d;second.app_id=124;strcpy(second.title,"Blasphemous");moonx_launch_seal(&second);
    assert(moonx_aurora_metadata_valid(&another,sizeof(another),&second));
    assert(!moonx_aurora_metadata_valid(&another,sizeof(another),&d));
    meta.metadata_title_id=0x545408A7u;strcpy(meta.source,"manual");moonx_aurora_metadata_seal(&meta);
    assert(moonx_aurora_metadata_valid(&meta,sizeof(meta),&d) && moonx_launch_valid(&d,sizeof(d)));
    meta.metadata_title_id=0;moonx_aurora_metadata_seal(&meta);
    assert(!moonx_aurora_metadata_valid(&meta,sizeof(meta),&d));
    meta.metadata_title_id=0x545408A7u;meta.description[0]='X';
    assert(!moonx_aurora_metadata_valid(&meta,sizeof(meta),&d));
    assert(!moonx_launch_path("game:\\default.xex",160));
    assert(!moonx_launch_folder("Game:\\Shortcuts",160));
    assert(aurora_path_normalize(d.main_xex,sizeof(d.main_xex),"Usb1:/Games/MoonX360.xex",1));
    moonx_launch_seal(&d);assert(moonx_launch_valid(&d,sizeof(d)));
    strcpy(d.main_xex,"Hdd1:\\Apps\\MoonX360\\default.xex");moonx_launch_seal(&d);
    FILE *cross=tmpfile();assert(cross);
    assert(moonx_link_write_payload(cross,&d)==468);rewind(cross);
    moonx_launch_data parsed={};assert(moonx_link_read_payload(cross,&parsed)==468);
    assert(moonx_launch_valid(&parsed,sizeof(parsed)) && !memcmp(&d,&parsed,468));fclose(cross);
    FILE *f=tmpfile();assert(f);assert(fwrite(&d,1,sizeof(d),f)==sizeof(d));rewind(f);
    moonx_launch_data loaded={};assert(fread(&loaded,1,sizeof(loaded),f)==sizeof(loaded));fclose(f);
    assert(moonx_launch_valid(&loaded,sizeof(loaded)));
    assert(!moonx_launch_valid(&loaded,sizeof(loaded)-1));
    loaded.magic[0]='Q';assert(!moonx_launch_valid(&loaded,sizeof(loaded)));loaded=d;
    loaded.version=2;loaded.checksum=moonx_launch_checksum(&loaded);assert(!moonx_launch_valid(&loaded,sizeof(loaded)));loaded=d;
    loaded.app_id=0;moonx_launch_seal(&loaded);assert(!moonx_launch_valid(&loaded,sizeof(loaded)));loaded=d;
    loaded.title[0]='X';assert(!moonx_launch_valid(&loaded,sizeof(loaded)));loaded=d;
    memset(loaded.host_id,'A',sizeof(loaded.host_id));moonx_launch_seal(&loaded);assert(!moonx_launch_valid(&loaded,sizeof(loaded)));loaded=d;
    strcpy(loaded.main_xex,"Hdd1:\\Apps\\..\\Other\\default.xex");moonx_launch_seal(&loaded);
    assert(!moonx_launch_valid(&loaded,sizeof(loaded)));loaded=d;
    strcpy(loaded.profile_id,"user:Updated profile");moonx_launch_seal(&loaded);
    assert(moonx_launch_valid(&loaded,sizeof(loaded)));
    char first[64],same[64],other[64];
    moonx_shortcut_leaf(first,"Hollow: Knight?",d.host_id,d.app_id);
    moonx_shortcut_leaf(same,"Hollow: Knight?","01234567-89AB-CDEF-0123-456789ABCDEF",d.app_id);
    moonx_shortcut_leaf(other,"Hollow: Knight?",d.host_id,d.app_id+1);
    assert(!strcmp(first,same) && strcmp(first,other));
    assert(strchr(first,':')==NULL && strchr(first,'?')==NULL);
    assert(!strcmp(d.title,"Hollow Knight"));
    assert(moonx_launch_folder("Hdd1:\\Apps\\MoonX360\\Shortcuts\\",160));
    assert(!moonx_launch_folder("Hdd1:\\Apps\\..\\Games\\",160));
    static aurora_index_file index={};
    memcpy(index.magic,MOONX_INDEX_MAGIC,8);index.version=1;index.count=1;
    index.items[0].link=d;
    strcpy(index.items[0].folder,"Hdd1:\\Apps\\MoonX360\\Shortcuts\\Hollow-Knight-ABCD1234");
    index.checksum=moonx_index_hash(&index);
    assert(moonx_index_valid(&index,sizeof(index)));
    assert(moonx_index_find(&index,"01234567-89AB-CDEF-0123-456789ABCDEF",123)==0);
    assert(moonx_index_find(&index,d.host_id,124)<0);
    char before=index.items[0].folder[10];index.items[0].folder[10]='X';assert(!moonx_index_valid(&index,sizeof(index)));
    index.items[0].folder[10]=before;index.checksum=moonx_index_hash(&index);
    assert(moonx_index_valid(&index,sizeof(index)));
    index.items[1]=index.items[0];index.count=2;index.checksum=moonx_index_hash(&index);
    assert(!moonx_index_valid(&index,sizeof(index)));
    index.count=1;moonx_index_remove(&index,0);index.checksum=moonx_index_hash(&index);
    assert(index.count==0 && moonx_index_valid(&index,sizeof(index)));
    char path[160],long_path[180];memset(long_path,'A',sizeof(long_path));long_path[sizeof(long_path)-1]=0;
    assert(aurora_path_normalize(path,sizeof(path),"Hdd1:/Apps/MoonX360/default.xex",1));
    assert(!strcmp(path,"Hdd1:\\Apps\\MoonX360\\default.xex"));
    assert(aurora_path_normalize(path,sizeof(path),"Usb0:/Apps/MoonX360/Shortcuts/",0));
    assert(!strcmp(path,"Usb0:\\Apps\\MoonX360\\Shortcuts\\"));
    assert(!aurora_path_normalize(path,sizeof(path),long_path,1));
    assert(!aurora_path_normalize(path,sizeof(path),"Hdd1:/Apps/../default.xex",1));
    assert(!aurora_path_normalize(path,sizeof(path),"Hdd1:/Apps/not-an-xex.txt",1));
    assert(aurora_path_join(path,sizeof(path),"Usb0:\\Apps","MoonX360"));
    assert(!strcmp(path,"Usb0:\\Apps\\MoonX360"));
    assert(aurora_path_parent(path) && !strcmp(path,"Usb0:\\Apps\\"));
    assert(!aurora_path_join(path,sizeof(path),"Usb0:\\Apps",".."));
    assert(!aurora_path_join(path,sizeof(path),"Usb0:\\Apps","bad:name"));
    assert(aurora_path_xex_name("default.xex") && aurora_path_xex_name("DEFAULT.XEX"));
    assert(!aurora_path_xex_name("default.txt"));
    assert(aurora_xex_magic_valid("XEX2") && !aurora_xex_magic_valid("BAD2"));
    FILE *header=tmpfile();assert(header);assert(fwrite("XEX2",1,4,header)==4);rewind(header);
    assert(aurora_xex_stream_header(header));rewind(header);
    assert(fwrite("BAD2",1,4,header)==4);rewind(header);assert(!aurora_xex_stream_header(header));fclose(header);
    const char *hdd="Hdd1:\\Apps\\MoonX360\\default.xex";
    const char *usb="Usb0:\\Apps\\MoonX360\\default.xex";
    assert(aurora_path_detect(path,sizeof(path),"Hdd1:/Apps/MoonX360/default.xex",NULL,matching_mount,(void*)hdd));
    assert(!strcmp(path,hdd));
    assert(aurora_path_detect(path,sizeof(path),"Hdd1:\\Missing\\default.xex","Usb0:\\Apps\\MoonX360\\Shortcuts\\",matching_mount,(void*)usb));
    assert(!strcmp(path,usb));
    assert(!aurora_path_detect(path,sizeof(path),hdd,NULL,matching_mount,(void*)"Unavailable:"));
    char target[64];
    assert(moonx_mount_target("Hdd1",target,sizeof(target)) && !strcmp(target,"\\Device\\Harddisk0\\Partition1"));
    assert(moonx_mount_target("usb2",target,sizeof(target)) && !strcmp(target,"\\Device\\Mass2"));
    assert(moonx_mount_target("UsbMu0",target,sizeof(target)) && !strcmp(target,"\\Device\\Mass0PartitionFile\\Storage"));
    assert(!moonx_mount_target("IntMu",target,sizeof(target)));
    assert(!moonx_mount_target("Usb",target,sizeof(target)));
    assert(!moonx_mount_target("Usb0",target,3));
    const char *roots_hdd[]={"Hdd1:\\"};const char *roots_usb[]={"Usb0:\\"};
    fake_aurora_fs fs={"Hdd1:\\Aurora\\Aurora.xex",1,0};
    moonx_aurora_search_io io={fake_aurora_xex,fake_aurora_dirs,fake_aurora_cancel,NULL,NULL,&fs};
    assert(moonx_aurora_find(path,sizeof(path),roots_hdd,1,&io)==1 && !strcmp(path,"Hdd1:\\Aurora\\"));
    fs.xex="Hdd1:\\Apps\\Aurora\\Aurora.xex";
    assert(moonx_aurora_find(path,sizeof(path),roots_hdd,1,&io)==1 && !strcmp(path,"Hdd1:\\Apps\\Aurora\\"));
    fs.xex="Hdd1:\\Applications\\Aurora\\Aurora.xex";
    assert(moonx_aurora_find(path,sizeof(path),roots_hdd,1,&io)==1 && !strcmp(path,"Hdd1:\\Applications\\Aurora\\"));
    fs.xex="Usb0:\\Apps\\Aurora\\Aurora.xex";
    assert(moonx_aurora_find(path,sizeof(path),roots_usb,1,&io)==1 && !strcmp(path,"Usb0:\\Apps\\Aurora\\"));
    fs.xex="Hdd1:\\Homebrew\\Dashboards\\Custom\\Aurora.xex";
    assert(moonx_aurora_find(path,sizeof(path),roots_hdd,1,&io)==1 && !strcmp(path,"Hdd1:\\Homebrew\\Dashboards\\Custom"));
    fs.xex="Hdd1:\\Aurora\\default.xex";
    assert(moonx_aurora_find(path,sizeof(path),roots_hdd,1,&io)==0);
    fs.xex="Hdd1:\\Aurora\\Aurora.xex";fs.readable=0;
    assert(moonx_aurora_find(path,sizeof(path),roots_hdd,1,&io)==0);
    fs.cancel=1;assert(moonx_aurora_find(path,sizeof(path),roots_hdd,1,&io)==-1);
    fs.cancel=0;assert(moonx_aurora_find(path,sizeof(path),roots_hdd,0,&io)==0);
    parsed=d;memset(parsed.main_xex,'A',sizeof(parsed.main_xex));moonx_launch_seal(&parsed);
    assert(!moonx_launch_valid(&parsed,sizeof(parsed)));
    puts("Aurora launch protocol: PASS");
}
