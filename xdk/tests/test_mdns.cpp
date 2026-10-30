#include "../../src/mdns.h"
#include "../../src/host_cache.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <vector>

typedef std::vector<unsigned char> Bytes;
static void u16(Bytes &b,unsigned n){b.push_back((unsigned char)(n>>8));b.push_back((unsigned char)n);}
static void name(Bytes &b,const char *s){while(*s){const char *e=strchr(s,'.');size_t n=e?(size_t)(e-s):strlen(s);assert(n&&n<64);b.push_back((unsigned char)n);b.insert(b.end(),s,s+n);s+=n;if(*s=='.')s++;}b.push_back(0);}
static Bytes record(Bytes b,const char *owner,int type,const Bytes &data){name(b,owner);u16(b,type);u16(b,0x8001);b.insert(b.end(),3,0);b.push_back(120);u16(b,(unsigned)data.size());b.insert(b.end(),data.begin(),data.end());return b;}
static Bytes packet(){Bytes b(12,0);b[2]=0x84;return b;}
static Bytes full_packet(){
    Bytes b=packet(),ptr,srv,ip,txt;
    name(ptr,"Gaming-PC._nvstream._tcp.local.");
    b=record(b,MDNS_SERVICE,12,ptr); // service owner starts at offset 12
    u16(srv,0);u16(srv,0);u16(srv,47989);name(srv,"gaming-pc.local.");
    b=record(b,"Gaming-PC._nvstream._tcp.local.",33,srv);
    ip.push_back(192);ip.push_back(168);ip.push_back(1);ip.push_back(100);
    b=record(b,"GAMING-PC.local.",1,ip);
    txt.push_back(12);const char *t="uuid=example";txt.insert(txt.end(),t,t+12);
    b=record(b,"Gaming-PC._nvstream._tcp.local.",16,txt);b[7]=4;return b;
}
int main(){
    Bytes b=full_packet();mdns_state s={};mdns_candidate out[16];
    assert(mdns_parse(&s,b.data(),b.size()));assert(mdns_candidates(&s,out,16)==1);
    assert(!strcmp(out[0].ip,"192.168.1.100") && out[0].port==47989);
    assert(!strcmp(s.services[0].txt,"uuid=example"));
    assert(mdns_parse(&s,b.data(),b.size()) && mdns_candidates(&s,out,16)==1);
    for(size_t n=0;n<b.size();n++){mdns_state empty={},before=empty;assert(!mdns_parse(&empty,b.data(),n));assert(!memcmp(&before,&empty,sizeof(empty)));}
    // Compressed PTR target: label + backwards pointer to the service owner.
    Bytes compressed=packet();name(compressed,MDNS_SERVICE);u16(compressed,12);u16(compressed,1);compressed.insert(compressed.end(),3,0);compressed.push_back(120);
    u16(compressed,5);compressed.push_back(2);compressed.push_back('P');compressed.push_back('C');compressed.push_back(0xc0);compressed.push_back(12);compressed[7]=1;
    mdns_state c={};assert(mdns_parse(&c,compressed.data(),compressed.size()));assert(!strcmp(c.services[0].instance,"pc._nvstream._tcp.local."));
    char decoded[256];size_t offset=compressed.size()-5;
    assert(mdns_name(compressed.data(),compressed.size(),&offset,decoded,sizeof(decoded)) && offset==compressed.size());
    unsigned char cycle[]={0xc0,0};offset=0;assert(!mdns_name(cycle,2,&offset,decoded,sizeof(decoded)));
    unsigned char nested[]={1,'a',0,0xc0,0,0xc0,3};offset=5;assert(mdns_name(nested,sizeof(nested),&offset,decoded,sizeof(decoded)) && !strcmp(decoded,"a."));
    unsigned char bad[]={0xc0,255};offset=0;assert(!mdns_name(bad,2,&offset,decoded,sizeof(decoded)));
    offset=12;char tiny[3];assert(!mdns_name(b.data(),b.size(),&offset,tiny,sizeof(tiny)));
    // Records may arrive separately, reordered, with SRV/A only in additionals.
    size_t pos=12;std::vector<Bytes> split;
    for(int i=0;i<4;i++) {size_t start=pos;assert(mdns_name(b.data(),b.size(),&pos,decoded,sizeof(decoded)));unsigned len=(b[pos+8]<<8)|b[pos+9];pos+=10+len;Bytes part=packet();part[11]=1;part.insert(part.end(),b.begin()+start,b.begin()+pos);split.push_back(part);}
    mdns_state separate={};for(int i=3;i>=0;i--)assert(mdns_parse(&separate,split[i].data(),split[i].size()));
    assert(mdns_candidates(&separate,out,16)==1);
    Bytes unrelated=packet(),ip(4,1);unrelated=record(unrelated,"printer.local.",1,ip);unrelated[7]=1;
    mdns_state printer={};assert(mdns_parse(&printer,unrelated.data(),unrelated.size()) && !mdns_candidates(&printer,out,16));
    // Two hosts, distinct ports, multiple answers and case-insensitive DNS matching.
    Bytes multi=full_packet(),ptr,srv,addr;
    name(ptr,"second._nvstream._tcp.local.");multi=record(multi,MDNS_SERVICE,12,ptr);
    u16(srv,0);u16(srv,0);u16(srv,48000);name(srv,"second.local.");multi=record(multi,"second._nvstream._tcp.local.",33,srv);
    addr.push_back(10);addr.push_back(1);addr.push_back(2);addr.push_back(3);multi=record(multi,"second.local.",1,addr);multi[7]=7;
    mdns_state many={};assert(mdns_parse(&many,multi.data(),multi.size()));assert(mdns_candidates(&many,out,16)==2 && out[1].port==48000);
    Bytes goodbye=split[0];size_t ttl=12;assert(mdns_name(goodbye.data(),goodbye.size(),&ttl,decoded,sizeof(decoded)));memset(goodbye.data()+ttl+4,0,4);
    assert(mdns_parse(&many,goodbye.data(),goodbye.size()) && mdns_candidates(&many,out,16)==1);
    Bytes badtxt=split[3];badtxt.back()=0; // malformed TXT length, not just malformed content
    size_t r=12;assert(mdns_name(badtxt.data(),badtxt.size(),&r,decoded,sizeof(decoded)));badtxt[r+10]=255;
    mdns_state before=s;assert(!mdns_parse(&s,badtxt.data(),badtxt.size()) && !memcmp(&s,&before,sizeof(s)));
    unsigned char query[300];assert(mdns_query(query,sizeof(query),MDNS_SERVICE,12,false)>12);
    offset=12;assert(mdns_name(query,sizeof(query),&offset,decoded,sizeof(decoded)) && !strcmp(decoded,MDNS_SERVICE));
    assert(!mdns_query(query,16,MDNS_SERVICE,12,false));
    // Deterministic fuzz-like input including mutations of valid packets.
    unsigned seed=0x564d5801;
    for(int i=0;i<30000;i++) {
        struct {unsigned guard1;mdns_state state;unsigned guard2;} box={0xaabbccdd,{},0x11223344};
        seed=seed*1664525+1013904223;Bytes fuzz(i%2?b:Bytes(seed%1536));
        for(size_t j=0;j<fuzz.size();j++){seed=seed*1664525+1013904223;if(!(i%2)||seed%29==0)fuzz[j]=(unsigned char)(seed>>24);}
        mdns_parse(&box.state,fuzz.data(),fuzz.size());mdns_candidates(&box.state,out,16);
        assert(box.guard1==0xaabbccdd && box.guard2==0x11223344);
    }
    host_cache cache={};cached_host h={};strcpy(h.ip,"192.168.1.100");h.port=47989;
    assert(host_cache_upsert(&cache,&h)==0);strcpy(h.uuid,"ABC");strcpy(h.name,"Gaming-PC");h.online=h.paired=true;
    assert(host_cache_upsert(&cache,&h)==0 && cache.count==1);
    strcpy(h.ip,"192.168.1.101");strcpy(h.uuid,"abc");assert(host_cache_upsert(&cache,&h)==0 && cache.count==1);
    h.uuid[0]=0;strcpy(h.name,"GAMING-PC");strcpy(h.ip,"192.168.1.102");assert(host_cache_upsert(&cache,&h)==1 && cache.count==2 && !cache.hosts[1].uuid[0]);
    strcpy(h.uuid,"ABC");assert(host_cache_upsert(&cache,&h)==0 && cache.count==1);
    char saved[4096];size_t len=host_cache_encode(&cache,saved,sizeof(saved));assert(len);
    host_cache restored={};host_cache_decode(&restored,saved);assert(restored.count==1 && !restored.hosts[0].online && !restored.hosts[0].paired);
    assert(!strcmp(restored.hosts[0].ip,"192.168.1.102"));assert(!host_cache_encode(&cache,saved,4));
    char invalid[]="bad\n\t\t999.1.2.3\t47989\n";host_cache_decode(&restored,invalid);assert(!restored.count);
    for(int i=1;i<HOST_CACHE_MAX;i++){snprintf(h.uuid,sizeof(h.uuid),"host-%d",i);snprintf(h.name,sizeof(h.name),"PC-%d",i);snprintf(h.ip,sizeof(h.ip),"10.0.0.%d",i);assert(host_cache_upsert(&cache,&h)>=0);}
    strcpy(h.uuid,"full");strcpy(h.name,"extra");strcpy(h.ip,"10.0.0.254");assert(host_cache_upsert(&cache,&h)==-1);
    puts("PASS: DNS names/compression/PTR/SRV/A/TXT, split/multiple answers, wrong service, TTL goodbye, truncation, 30000 malformed packets, dedup and cached hosts.");
}
