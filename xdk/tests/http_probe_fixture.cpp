#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <winsock2.h>
/* Use desktop Winsock types, with fake nonblocking I/O. No real network traffic. */
struct conn {int fd;bool tls,trace,probe;uint32_t deadline;volatile int *cancel;int ssl;};
struct client_identity{};
struct http_response {int status;char *body;size_t len;};
static uint32_t tick,begin_tick;static int closed,read_calls,write_calls,connect_calls,behavior,last_error;
static volatile int cancelled;static size_t received;static std::string response;
static uint32_t plat_now_ms(){return tick;}
static int plat_atomic_load(volatile int *p){return *p;}
static void plat_sleep_ms(unsigned n){assert(n==10);tick+=n;if(behavior==3 && tick-begin_tick>=30)cancelled=1;}
static int fake_last_error(){return last_error;}
static int fake_socket(int,int,int){return 1;}
static int fake_ioctl(int,long,u_long *value){assert(*value==1);return 0;}
static int fake_connect(int,const sockaddr*,int){connect_calls++;last_error=WSAEWOULDBLOCK;return -1;}
static int fake_select(int,fd_set *read,fd_set *write,fd_set *errors,timeval *tv){
    assert(tv->tv_sec==0 && tv->tv_usec==0);FD_ZERO(errors);
    if(behavior==1 || (read && (behavior==2 || behavior==3))){if(read)FD_ZERO(read);if(write)FD_ZERO(write);return 0;}
    return 1;
}
static int fake_send(int,const char*,int n,int){write_calls++;return n>5?5:n;}
static int fake_recv(int,char *out,int capacity,int){
    read_calls++;if(behavior==4)tick+=300;
    if(received>=response.size())return 0;
    size_t n=response.size()-received;if(n>(size_t)capacity)n=capacity;if(behavior==4 && n>1)n=1;
    memcpy(out,response.data()+received,n);received+=n;return (int)n;
}
#define socket fake_socket
#define ioctlsocket fake_ioctl
#define connect fake_connect
#define select fake_select
#define send fake_send
#define recv fake_recv
#define WSAGetLastError fake_last_error
#define _XBOX 1
#define sock_again() (fake_last_error()==WSAEWOULDBLOCK || fake_last_error()==WSAETIMEDOUT)
#define MBEDTLS_ERR_SSL_WANT_WRITE (-1)
#define MBEDTLS_ERR_SSL_WANT_READ (-2)
#define MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY (-3)
static int mbedtls_ssl_write(int*,const unsigned char*,size_t){assert(false);return -1;}
static int mbedtls_ssl_read(int*,unsigned char*,size_t){assert(false);return -1;}
static bool conn_open(conn*,const char*,int,bool,const client_identity*,int,bool,volatile int*){assert(false);return false;}
static void conn_close(conn *c){assert(c->fd==1 && !c->tls);closed++;c->fd=-1;}
#define MAX_RESPONSE (4*1024*1024)
/* ACTUAL_HTTP */
static bool request(int mode,int duration=1200){
    behavior=mode;cancelled=0;received=0;read_calls=write_calls=0;begin_tick=tick;conn c;
    http_response r={};bool ok=http_get_impl("10.1.2.3",47989,false,"/serverinfo",NULL,duration,&r,true,&cancelled);
    if(ok){assert(r.status==200 && !strcmp(r.body,"<root/>"));free(r.body);}
    return ok;
}
int main(){
    response="HTTP/1.1 200 OK\r\nContent-Length: 7\r\n\r\n<root/>";
    assert(request(0) && write_calls>1 && closed==1);
    assert(!request(1) && tick-begin_tick==1200 && !read_calls);
    assert(!request(2) && tick-begin_tick==1200 && !read_calls);
    assert(!request(3) && cancelled && tick-begin_tick<=40);
    assert(!request(4) && tick-begin_tick<=1500 && read_calls<=4);
    tick=UINT32_MAX-500;assert(!request(1) && tick-begin_tick==1200);
    response="HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n7\r\n<root/>\r\n0\r\n\r\n";assert(request(0));
    assert(closed==7);
    puts("PASS: actual wake HTTP nonblocking connect/send/read, partial writes, Content-Length/chunks, total deadline, continuous drip, cancellation and tick wrap; no real network I/O");
}
