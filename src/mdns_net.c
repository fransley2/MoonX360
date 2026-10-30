#include "mdns.h"
#include <string.h>
#ifdef _XBOX
#include <winsock2.h>
#define MDNS_SOCKET SOCKET
#define MDNS_INVALID INVALID_SOCKET
#define mdns_socket_close closesocket
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define MDNS_SOCKET int
#define MDNS_INVALID (-1)
#define mdns_socket_close close
#endif

static MDNS_SOCKET mdns_socket=MDNS_INVALID;
void mdns_close(void) {if(mdns_socket!=MDNS_INVALID)mdns_socket_close(mdns_socket);mdns_socket=MDNS_INVALID;}
bool mdns_open(void)
{
    if(mdns_socket!=MDNS_INVALID)return true;
    mdns_socket=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(mdns_socket==MDNS_INVALID)return false;
    int reuse=1;setsockopt(mdns_socket,SOL_SOCKET,SO_REUSEADDR,(const char*)&reuse,sizeof(reuse));
    struct sockaddr_in local;memset(&local,0,sizeof(local));local.sin_family=AF_INET;local.sin_port=htons(5353);
    if(bind(mdns_socket,(struct sockaddr*)&local,sizeof(local))!=0) {mdns_close();return false;}
#ifdef IP_MULTICAST_TTL
    int ttl=255;setsockopt(mdns_socket,IPPROTO_IP,IP_MULTICAST_TTL,(const char*)&ttl,sizeof(ttl));
#endif
    /* XDK Winsock exposes group membership, but no multicast TTL option. */
    struct ip_mreq group;memset(&group,0,sizeof(group));group.imr_multiaddr.s_addr=inet_addr("224.0.0.251");
    if(setsockopt(mdns_socket,IPPROTO_IP,IP_ADD_MEMBERSHIP,(const char*)&group,sizeof(group))!=0) {mdns_close();return false;}
#ifdef _XBOX
    u_long nonblocking=1;
    if(ioctlsocket(mdns_socket,FIONBIO,&nonblocking)!=0) {mdns_close();return false;}
#else
    if(fcntl(mdns_socket,F_SETFL,O_NONBLOCK)<0) {mdns_close();return false;}
#endif
    return true;
}
bool mdns_send(const char *name,unsigned short type)
{
    unsigned char query[300];size_t n=mdns_query(query,sizeof(query),name,type,false);
    if(mdns_socket==MDNS_INVALID || !n)return false;
    struct sockaddr_in to;memset(&to,0,sizeof(to));to.sin_family=AF_INET;to.sin_port=htons(5353);
    to.sin_addr.s_addr=inet_addr("224.0.0.251");
    return sendto(mdns_socket,(const char*)query,(int)n,0,(struct sockaddr*)&to,sizeof(to))==(int)n;
}
bool mdns_poll(struct mdns_state *state)
{
    unsigned char packet[8192];bool changed=false;
    if(mdns_socket==MDNS_INVALID)return false;
    for(int i=0;i<4;i++) {
        int n=recvfrom(mdns_socket,(char*)packet,sizeof(packet),0,NULL,NULL);
        if(n<=0)break;
        if(mdns_parse(state,packet,(size_t)n))changed=true;
    }
    return changed;
}
