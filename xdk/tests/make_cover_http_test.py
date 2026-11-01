"""Actual bounded socket wait and TLS read/write loops with controlled readiness/clock."""
from pathlib import Path
import sys
r=Path(__file__).resolve().parents[2];s=(r/'src/http.c').read_text()
def fn(sig):
 a=s.index(sig);b=s.index('{',a)+1;d=1
 while d:d+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
fixture=r'''
#include <winsock2.h>
#pragma comment(lib,"ws2_32.lib")
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
struct conn {int fd;bool tls,probe;uint32_t deadline;volatile int *cancel;int ssl;};
static uint32_t tick;static int polls,reads,writes,sleeps;static bool buffered;
static uint32_t plat_now_ms(){return tick;}
static int plat_atomic_load(volatile int *p){return *p;}
static void plat_sleep_ms(unsigned n){sleeps++;tick+=n;}
static int fake_select(int,fd_set *r,fd_set *w,fd_set *e,const timeval*){polls++;if(r)FD_ZERO(r);if(w)FD_ZERO(w);if(e)FD_ZERO(e);return 0;}
#define select fake_select
static bool again=true;
#define sock_again() again
#define MBEDTLS_ERR_SSL_WANT_READ -100
#define MBEDTLS_ERR_SSL_WANT_WRITE -101
#define MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY -102
#define MBEDTLS_ERR_SSL_TIMEOUT -103
#define MBEDTLS_ERR_NET_RECV_FAILED -104
#define MBEDTLS_ERR_NET_SEND_FAILED -105
static int mbedtls_ssl_read(int*,unsigned char *data,size_t){reads++;if(buffered){memcpy(data,"new",3);return 3;}return MBEDTLS_ERR_SSL_WANT_READ;}
static int mbedtls_ssl_write(int*,const unsigned char*,size_t){writes++;return MBEDTLS_ERR_SSL_WANT_WRITE;}
static int socket_result=-1;
static int plain_send(int fd,const char*,int,int){assert(fd==1);return socket_result;}
static int plain_recv(int fd,char*,int,int){assert(fd==1);return socket_result;}
#define send plain_send
#define recv plain_recv
/* CODE */
int main(){
 conn c={};c.fd=1;c.tls=c.probe=true;c.deadline=100;char data[8];
 // Exercise the actual BIO, including the formerly fatal nonblocking receive.
 assert(bio_recv(&c,(unsigned char*)data,sizeof(data))==MBEDTLS_ERR_SSL_WANT_READ);
 assert(bio_send(&c,(const unsigned char*)data,sizeof(data))==MBEDTLS_ERR_SSL_WANT_WRITE);
 c.probe=false;assert(bio_recv(&c,(unsigned char*)data,sizeof(data))==MBEDTLS_ERR_SSL_TIMEOUT);c.probe=true;
 again=false;assert(bio_recv(&c,(unsigned char*)data,sizeof(data))==MBEDTLS_ERR_NET_RECV_FAILED);again=true;
 socket_result=3;assert(bio_recv(&c,(unsigned char*)data,sizeof(data))==3);socket_result=0;
 assert(bio_recv(&c,(unsigned char*)data,sizeof(data))==0);socket_result=-1;

 assert(conn_read(&c,data,sizeof(data))==-1 && tick==100 && reads==1 && sleeps==10 && polls==10);
 tick=0;polls=sleeps=writes=0;assert(conn_write(&c,"x",1)==-1 && tick==100 && writes==1 && polls==10 && sleeps==10);
 tick=0;polls=0;buffered=true;assert(conn_read(&c,data,sizeof(data))==3 && !memcmp(data,"new",3) && !polls && tick==0);
 volatile int cancel=1;c.cancel=&cancel;assert(conn_read(&c,data,sizeof(data))==-1);cancel=0;
 tick=UINT32_MAX-49;c.deadline=50;buffered=false;polls=sleeps=0;
 assert(conn_read(&c,data,sizeof(data))==-1 && tick==50 && sleeps==10);
 puts("PASS: actual cover TLS loops honor absolute deadline/cancel/wrap, wait instead of busy-spin and consume buffered TLS immediately");
}
'''
Path(sys.argv[1]).write_text(fixture.replace('/* CODE */','\n'.join(fn(x) for x in ['static int bio_send(', 'static int bio_recv(', 'static bool probe_live(', 'static bool probe_wait(', 'static int conn_write(', 'static int conn_read('])))
