#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "../../../src/host_cache.h"
#include "../../../src/wake.h"
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define AF_INET 2
#define SOCK_DGRAM 2
#define IPPROTO_UDP 17
#define SOL_SOCKET 0xffff
#define SO_BROADCAST 32
#define INADDR_BROADCAST 0xffffffffu
struct sockaddr {int unused;};
struct sockaddr_in {int sin_family;unsigned short sin_port;struct {unsigned s_addr;} sin_addr;};
static bool fail_socket,fail_option,fail_send;static int closed,sent;
static unsigned short htons(unsigned short n){return (unsigned short)((n<<8)|(n>>8));}
static SOCKET socket(int,int,int){return fail_socket?INVALID_SOCKET:1;}
static int setsockopt(SOCKET,int,int,const char *v,int){assert(*(const int*)v==1);return fail_option?-1:0;}
static int sendto(SOCKET,const char *data,int n,int,const sockaddr *address,int){
    const sockaddr_in *to=(const sockaddr_in*)address;
    assert(to->sin_family==AF_INET && to->sin_addr.s_addr==0xffffffffu && to->sin_port==htons(9));
    assert(n==102 && (unsigned char)data[0]==255 && data[6]==2);sent++;return fail_send?-1:n;
}
static int closesocket(SOCKET){closed++;return 0;}
/* ACTUAL_SEND */
struct gs_server {char uuid[80];};
static uint32_t tick;static int probes,freed,respond_after;static bool wrong_uuid;
static uint32_t plat_now_ms(){return tick;}
static int plat_atomic_load(volatile int *p){return *p;}
static void plat_atomic_store(volatile int *p,int n){*p=n;}
static void plat_sleep_ms(int n){assert(n==50);tick+=n;}
static bool gs_wake_probe(gs_server *r,const char *ip,unsigned short port,int timeout,volatile int *cancel){
    assert(!*cancel);
    assert(!strcmp(ip,"10.1.2.3") && port==47989 && timeout>0 && timeout<=1200);
    tick+=timeout;probes++;strcpy(r->uuid,wrong_uuid?"another-host":"host-uuid");return probes>=respond_after;
}
static void gs_server_free(gs_server*){freed++;}
/* ACTUAL_WORKER */
static wake_job job(){wake_job j={};strcpy(j.host.ip,"10.1.2.3");j.host.port=47989;strcpy(j.host.mac,"02:12:34:56:78:9A");strcpy(j.host.uuid,"host-uuid");return j;}
int main(){
    assert(wake_send("02:12:34:56:78:9A") && sent==1 && closed==1);
    fail_option=true;assert(!wake_send("02:12:34:56:78:9A") && closed==2);fail_option=false;
    fail_send=true;assert(!wake_send("02:12:34:56:78:9A") && closed==3);fail_send=false;
    fail_socket=true;assert(!wake_send("02:12:34:56:78:9A") && closed==3);fail_socket=false;
    wake_job j=job();respond_after=3;wake_task(&j);assert(j.done && j.phase==1 && j.replied && probes==3 && freed==3);
    probes=freed=0;tick=0;respond_after=999;j=job();wake_task(&j);assert(j.done && !j.replied && tick<=30000 && tick>=29000 && probes==freed);
    probes=freed=0;tick=0;j=job();j.cancel=1;wake_task(&j);assert(j.done && !j.replied && !probes && !tick);
    probes=freed=0;tick=0;respond_after=1;wrong_uuid=true;j=job();wake_task(&j);assert(!j.replied && tick<=30000 && probes==freed);
    fail_send=true;j=job();int before=probes;wake_task(&j);assert(j.done && j.phase<0 && !j.replied && probes==before);
    puts("PASS: actual WOL socket/broadcast/port and failures; actual worker response identity, send vs reply, cancellation, deadline and server cleanup");
}
