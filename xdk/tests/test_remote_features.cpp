#include <assert.h>
#include <stdio.h>
#include <vector>
#include <string>
#include <map>
#include <errno.h>
#include "../../src/remote_input.h"
#include "../../src/wake.h"
#include "../../src/host_cache.h"
#include "../../src/audio_layout.h"
#include "../../src/config.h"
struct key_event {int key,action,mod;};
static std::vector<key_event> keys;
static int clicks[4],movement,scrolls;
int LiSendMouseButtonEvent(char action,int button){clicks[button]+=action==BUTTON_ACTION_PRESS?1:-1;assert(clicks[button]>=0);return 0;}
int LiSendMouseMoveEvent(short x,short y){movement+=x-y;return 0;}
int LiSendScrollEvent(signed char n){scrolls+=n;return 0;}
int LiSendKeyboardEvent(short k,char a,char m){key_event e={k,a,m};keys.push_back(e);return 0;}
int LiSendMultiControllerEvent(short,short,int,unsigned char,unsigned char,short,short,short,short){return 0;}
int LiSendControllerArrivalEvent(uint8_t,uint16_t,uint8_t,uint32_t,uint16_t){return 0;}
static std::map<std::string,std::string> files;
char *storage_read(const char *name,size_t *len){if(!files.count(name)){errno=ENOENT;return NULL;}const std::string &s=files[name];char *p=(char*)malloc(s.size()+1);memcpy(p,s.c_str(),s.size()+1);if(len)*len=s.size();return p;}
bool storage_write(const char *name,const void *s,size_t n){files[name]=std::string((const char*)s,n);return true;}
static std::string snapshot(const char *body){unsigned h=2166136261u;for(const char *p=body;*p;p++)h=(h^(unsigned char)*p)*16777619u;char end[32];sprintf(end,"CHECKSUM %08x\n",h);return std::string(body)+end;}
int main()
{
    unsigned char packet[104];memset(packet,0x42,sizeof(packet));
    assert(wake_packet("02:12:34:56:78:9A",packet));
    for(int i=0;i<6;i++)assert(packet[i]==255);
    for(int i=0;i<16;i++)assert(!memcmp(packet+6+i*6,"\x02\x12\x34\x56\x78\x9a",6));
    assert(packet[102]==0x42 && packet[103]==0x42);
    const char *invalid[]={"", "00:00:00:00:00:00","FF:FF:FF:FF:FF:FF","01:02:03:04:05:06","02:12:34:56:78:9","02:12-34:56:78:9A","02:12:34:56:78:GG","02123456789A"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++)assert(!wake_packet(invalid[i],packet));
    assert(wake_packet("02-12-34-56-78-9a",packet));
    host_cache c={};char legacy[]="uuid-a\tPC\t10.1.2.3\t47989\n";host_cache_decode(&c,legacy);assert(c.count==1 && !c.hosts[0].mac[0]);
    strcpy(c.hosts[0].mac,"02:12:34:56:78:9A");c.hosts[0].mac_manual=true;
    cached_host update=c.hosts[0];strcpy(update.ip,"10.8.7.6");update.mac_manual=false;strcpy(update.mac,"02:AA:BB:CC:DD:EE");
    assert(host_cache_upsert(&c,&update)==0 && !strcmp(c.hosts[0].mac,"02:12:34:56:78:9A"));
    strcpy(update.uuid,"uuid-b");assert(host_cache_upsert(&c,&update)==1 && c.count==2 && !strcmp(c.hosts[1].mac,update.mac));
    update.uuid[0]=0;strcpy(update.ip,"10.8.7.9");assert(host_cache_upsert(&c,&update)==2 && !c.hosts[2].uuid[0]);
    char text[4096];assert(host_cache_encode(&c,text,sizeof(text)));host_cache restored;host_cache_decode(&restored,text);
    assert(restored.count==3 && restored.hosts[0].mac_manual && !strcmp(restored.hosts[0].mac,c.hosts[0].mac));
    puts("PASS: magic packet bytes/guards, MAC validation, legacy cache, UUID/IP movement, manual override, distinct hosts and restart");

    remote_input r={};pad p={};p.present=true;p.buttons=BACK_FLAG|RS_CLK_FLAG;
    int exit=input_exit_mask(0);remote_pad(&r,&p,0,0,false,exit);assert(r.mouse[0] && !p.buttons);
    p.buttons=BACK_FLAG|RS_CLK_FLAG;remote_pad(&r,&p,0,4,false,exit);assert(r.mouse[0]);
    p.buttons=A_FLAG|B_FLAG;p.lx=32767;p.ly=32767;p.ry=32767;remote_pad(&r,&p,0,100,false,exit);
    assert(clicks[BUTTON_LEFT]==1 && clicks[BUTTON_RIGHT]==1 && movement && scrolls==1 && !p.buttons && !p.lx);
    pad other={};other.present=true;other.buttons=X_FLAG;remote_pad(&r,&other,1,104,false,exit);assert(other.buttons==X_FLAG);
    p.buttons=BACK_FLAG|RS_CLK_FLAG;remote_pad(&r,&p,0,108,false,exit);assert(!r.mouse[0] && !clicks[1] && !clicks[3] && !p.buttons);
    p.buttons=0;remote_pad(&r,&p,0,112,false,exit);p.buttons=BACK_FLAG|RS_CLK_FLAG;remote_pad(&r,&p,0,116,false,exit);
    p.buttons=A_FLAG;remote_pad(&r,&p,0,120,false,exit);assert(clicks[1]==1);
    remote_pad(&r,&p,0,124,true,exit);assert(!r.mouse[0] && !clicks[1]);
    p.buttons=0;remote_pad(&r,&p,0,128,false,exit);p.buttons=BACK_FLAG|RS_CLK_FLAG;remote_pad(&r,&p,0,132,false,exit);
    p.buttons=A_FLAG;remote_pad(&r,&p,0,136,false,exit);p.present=false;remote_pad(&r,&p,0,140,false,exit);assert(!clicks[1] && !r.mouse[0] && !p.buttons);
    p.present=true;p.buttons=A_FLAG;remote_pad(&r,&p,0,144,false,exit);assert(!p.buttons);
    p.buttons=0;remote_pad(&r,&p,0,148,false,exit);p.buttons=X_FLAG;remote_pad(&r,&p,0,152,false,exit);assert(p.buttons==X_FLAG);
    bool down[256]={false};down['A']=true;remote_keys(&r,down,true);assert(keys.empty());
    down['A']=false;remote_keys(&r,down,true);down['A']=true;down[0xa2]=true;remote_keys(&r,down,true);
    assert(keys.size()==2 && (keys[0].mod&MODIFIER_CTRL));size_t n=keys.size();remote_keys(&r,down,true);assert(keys.size()==n);
    remote_keys(&r,down,false);assert(keys.size()==n+2);remote_keys(&r,down,true);assert(keys.size()==n+2);
    memset(down,0,sizeof(down));remote_keys(&r,down,true);down['B']=true;remote_keys(&r,down,true);
    remote_release(&r);assert(keys.back().key=='B' && keys.back().action==KEY_ACTION_UP && !clicks[1] && !clicks[3]);
    puts("PASS: mouse toggle/debounce, consumed controls, other pad, motion/click/scroll, Guide/unplug/end release, keyboard edges/modifiers/repeat/local-UI suppression");

    assert(audio_layout_valid(6,48000,960) && audio_layout_valid(2,48000,240));
    assert(!audio_layout_valid(8,48000,960) && !audio_layout_valid(6,44100,960) && !audio_layout_valid(6,48000,961));
    assert(audio_pcm_bytes(960,6)==11520 && audio_pcm_bytes(960,2)==3840 && !audio_pcm_bytes(961,6));
    for(int ch=0;ch<6;ch++) {
        int16_t pcm[14]={};pcm[ch]=30000;pcm[12]=1234;pcm[13]=2345;audio_downmix(pcm,2);
        assert(pcm[12]==1234 && pcm[13]==2345);
        if(ch==0||ch==4)assert(pcm[0]>0 && pcm[1]==0);
        if(ch==1||ch==5)assert(pcm[1]>0 && pcm[0]==0);
        if(ch==2)assert(pcm[0]>0 && pcm[0]==pcm[1]);if(ch==3)assert(!pcm[0]&&!pcm[1]);
    }
    config cfg;config_defaults(&cfg);assert(cfg.audio_mode==0);cfg.audio_mode=1;assert(config_save(&cfg));config loaded;config_load(&loaded);assert(loaded.audio_mode==1);
    files.clear();files["profiles-a.tsv"]=snapshot("XENON_PROFILES 1 4 1\nOld\t1280\t720\t30\t12000\t1024\t0\t0\t0\t1\t1\t0\t0\t0\t1\t4\n");
    user_profiles profiles={};assert(config_profiles_load(&profiles) && profiles.items[0].options.audio_mode==0);
    assert(config_profiles_create(&profiles,&cfg,"Surround"));user_profiles read={};assert(config_profiles_load(&read));assert(read.items[1].options.audio_mode==1);
    loaded.language=2;strcpy(loaded.host,"global-host");loaded.left_min=17;assert(config_profiles_apply(&read,1,&loaded));
    assert(loaded.audio_mode==1 && loaded.language==2 && loaded.left_min==17 && !strcmp(loaded.host,"global-host"));
    puts("PASS: stereo/5.1 sample and byte limits, channel impulses/downmix guards, config roundtrip, v1 to v2 profiles, global isolation");
}
