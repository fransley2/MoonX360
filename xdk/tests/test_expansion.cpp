#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <map>
#include <string>
#include "../../src/product_features.h"
#include "../../src/quick_menu.h"
#include "../../src/remote_input.h"
#include "../../src/host_cache.h"
#include "../../src/wake.h"
static bool fail_promote;
static int tested_rename(const char *a,const char *b){if(fail_promote && strstr(a,".new"))return -1;return rename(a,b);}
#define rename tested_rename
#include "../../src/safe_storage.h"
#undef rename
static std::map<std::string,std::string> files;
char *storage_read(const char *name,size_t *len){if(!files.count(name)){errno=ENOENT;return NULL;}const std::string &s=files[name];char *p=(char*)malloc(s.size()+1);memcpy(p,s.c_str(),s.size()+1);if(len)*len=s.size();return p;}
bool storage_write(const char *name,const void *data,size_t n){files[name]=std::string((const char*)data,n);return true;}
static int movement;
int LiSendMouseMoveEvent(short x,short){movement+=x;return 0;}
int LiSendMouseButtonEvent(char,int){return 0;}int LiSendScrollEvent(signed char){return 0;}int LiSendKeyboardEvent(short,char,char){return 0;}
int LiSendMultiControllerEvent(short,short,int,unsigned char,unsigned char,short,short,short,short){return 0;}
int LiSendControllerArrivalEvent(uint8_t,uint16_t,uint8_t,uint32_t,uint16_t){return 0;}
static std::string read_file(const char *name){FILE *f=fopen(name,"rb");assert(f);char data[128];size_t n=fread(data,1,sizeof(data),f);fclose(f);return std::string(data,n);}
static bool press(quick_menu *q,pad *pads,int buttons,int exit,bool *quit,int *action,uint32_t clock){pads[0].buttons=0;quick_step(q,pads,false,exit,quit,action,clock);pads[0].buttons=buttons;return quick_step(q,pads,false,exit,quit,action,clock);}
int main(){
    assert(wake_identity_valid("{ABC-123}") && !wake_identity_valid("pc\nhost_uuid=other") && !wake_identity_valid("pc\tother"));
    assert(feature_search("Grand Theft Auto","THEFT") && feature_search("Desktop","") && !feature_search("Desktop","Xbox"));
    game_bindings b={};assert(binding_set(&b,"uuid:server",42,"factory:2"));assert(binding_set(&b,"uuid:server",43,"user:TV"));
    assert(!binding_set(&b,"uuid:server",44,"factory:9") && !binding_set(&b,"uuid:server",44,"unknown"));
    char text[8192];size_t bytes=bindings_encode(&b,text,sizeof(text));assert(bytes);
    game_bindings decoded;assert(bindings_decode(&decoded,text) && decoded.count==2);
    assert(binding_find(&decoded,"uuid:server",43)==1 && binding_find(&decoded,"uuid:other",43)<0);
    text[bytes-3]^=1;assert(!bindings_decode(&decoded,text) && decoded.read_only && decoded.count==0);
    assert(!binding_set(&decoded,"uuid:server",42,"factory:1"));
    assert(!bindings_decode(&decoded,"MOONX_BINDINGS 2\n") && decoded.read_only);
    assert(binding_set(&b,"uuid:server",42,"") && b.count==1);
    config cfg;config_defaults(&cfg);assert(cfg.mouse_speed==100 && cfg.mouse_scroll_ms==100 && !cfg.mouse_acceleration);
    cfg.mouse_speed=175;cfg.mouse_acceleration=1;cfg.mouse_scroll_ms=250;assert(config_save(&cfg));config loaded;config_load(&loaded);
    assert(loaded.mouse_speed==175 && loaded.mouse_acceleration==1 && loaded.mouse_scroll_ms==250);
    puts("PASS: filtered search, per-game identity/assignment/removal, version/checksum fail-closed, pointer settings restart");

    host_cache cache={};cached_host host={};strcpy(host.uuid,"stable");strcpy(host.name,"Server");strcpy(host.ip,"10.1.2.3");host.port=47989;host_cache_upsert(&cache,&host);
    strcpy(cache.hosts[0].alias,"Living room");cache.hosts[0].auto_wake=cache.hosts[0].is_default=true;
    strcpy(host.ip,"10.9.8.7");host_cache_upsert(&cache,&host);assert(cache.count==1 && cache.hosts[0].auto_wake && !strcmp(cache.hosts[0].alias,"Living room"));
    assert(host_cache_encode(&cache,text,sizeof(text)));host_cache restored;host_cache_decode(&restored,text);
    assert(restored.count==1 && restored.hosts[0].is_default && restored.hosts[0].auto_wake && !strcmp(restored.hosts[0].alias,"Living room"));
    puts("PASS: host nickname/default/automatic wake persistence and stable UUID after address change");

    quick_menu q={};q.volume=100;pad pads[MAX_PADS]={};pads[0].present=true;bool quit=false;int action=-1;int exit=input_exit_mask(0);
    assert(!press(&q,pads,BACK_FLAG|PLAY_FLAG,exit,&quit,&action,0) && !q.open); // screenshot chord belongs to the console
    assert(!press(&q,pads,BACK_FLAG|RS_CLK_FLAG,exit,&quit,&action,0) && !q.open); // mouse shortcut
    assert(press(&q,pads,BACK_FLAG|LS_CLK_FLAG,exit,&quit,&action,0) && q.open);
    quick_step(&q,pads,false,exit,&quit,&action,0);assert(q.open); // held chord toggles once
    press(&q,pads,DOWN_FLAG,exit,&quit,&action,0);assert(q.row==1);
    press(&q,pads,LEFT_FLAG,exit,&quit,&action,0);assert(q.volume==95);
    press(&q,pads,A_FLAG,exit,&quit,&action,0);assert(q.muted);quick_step(&q,pads,false,exit,&quit,&action,0);assert(q.muted);
    press(&q,pads,B_FLAG,exit,&quit,&action,0);assert(!q.open && q.neutral);
    pads[0].buttons=A_FLAG;assert(quick_step(&q,pads,false,exit,&quit,&action,0));pads[0].buttons=0;quick_step(&q,pads,false,exit,&quit,&action,0);assert(!q.neutral);
    press(&q,pads,BACK_FLAG|LS_CLK_FLAG,exit,&quit,&action,0);assert(q.open);pads[0].buttons=exit;assert(!quick_step(&q,pads,false,exit,&quit,&action,0) && !q.open);
    q={};q.volume=100;pads[0].buttons=A_FLAG;quick_step(&q,pads,true,exit,&quit,&action,0);assert(!q.neutral && !q.open);
    assert(!quick_step(&q,pads,false,exit,&quit,&action,0));
    for(int choice=0;choice<2;choice++){q={};pads[0].buttons=input_exit_mask(choice);assert(!quick_step(&q,pads,false,input_exit_mask(choice),&quit,&action,0));}
    q={};q.volume=100;press(&q,pads,BACK_FLAG|LS_CLK_FLAG,exit,&quit,&action,0);pads[0].present=false;assert(quick_step(&q,pads,false,exit,&quit,&action,0) && !q.open);
    q={};q.volume=100;pads[0].present=true;pads[0].lx=pads[0].ly=0;
    for(unsigned cycle=0;cycle<100;cycle++) {
        uint32_t start=0xFFFFF000u+cycle*1000;
        press(&q,pads,BACK_FLAG|LS_CLK_FLAG,exit,&quit,&action,start);assert(q.open && q.row==0);
        pads[0].buttons=0;quick_step(&q,pads,false,exit,&quit,&action,start+4);
        pads[0].buttons=DOWN_FLAG;quick_step(&q,pads,false,exit,&quit,&action,start+8);assert(q.row==1);
        quick_step(&q,pads,false,exit,&quit,&action,start+327);assert(q.row==1);
        quick_step(&q,pads,false,exit,&quit,&action,start+328);assert(q.row==2);
        quick_step(&q,pads,false,exit,&quit,&action,start+423);assert(q.row==3);
        pads[0].buttons=B_FLAG;quick_step(&q,pads,false,exit,&quit,&action,start+424);assert(!q.open && !q.repeat);
        pads[0].buttons=0;quick_step(&q,pads,false,exit,&quit,&action,start+428);
    }
    press(&q,pads,BACK_FLAG|LS_CLK_FLAG,exit,&quit,&action,200000);pads[0].buttons=0;quick_step(&q,pads,false,exit,&quit,&action,200004);
    pads[0].ly=-25000;quick_step(&q,pads,false,exit,&quit,&action,200008);assert(q.row==1);
    quick_step(&q,pads,false,exit,&quit,&action,200328);assert(q.row==2);pads[0].ly=0;
    remote_input remote={};remote.mouse[0]=true;remote.speed=25;pad pointer={};pointer.present=true;
    for(unsigned i=0;i<32;i++){pointer.lx=10000;remote_pad(&remote,&pointer,0,i*4,false,exit);}assert(movement>0);
    puts("PASS: quick menu edges/volume/mute/neutral/Guide/unplug, both exit shortcuts, pointer fractional movement");

    const char *path="expansion-storage.txt";remove(path);remove("expansion-storage.txt.bak");remove("expansion-storage.txt.new");
    assert(storage_replace_file(path,"first",5));assert(storage_replace_file(path,"second",6));
    assert(read_file(path)=="second" && read_file("expansion-storage.txt.bak")=="first");
    fail_promote=true;assert(!storage_replace_file(path,"bad",3));fail_promote=false;assert(read_file(path)=="second");
    assert(!storage_replace_file("missing-folder/file","bad",3));assert(!storage_recoverable_name("client.pem") && storage_recoverable_name("hosts.tsv"));
    remove(path);remove("expansion-storage.txt.bak");remove("expansion-storage.txt.new");
    puts("PASS: verified file replacement, previous backup, injected promotion failure/rollback, missing-directory failure, certificates excluded");
}
