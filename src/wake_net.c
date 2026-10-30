#include "wake.h"
#ifdef _XBOX
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET (-1)
#define closesocket close
#endif
bool wake_send(const char *mac)
{
    unsigned char packet[102];if(!wake_packet(mac,packet))return false;
    SOCKET s=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(s==INVALID_SOCKET)return false;
    int broadcast=1;bool ok=false;
    if(!setsockopt(s,SOL_SOCKET,SO_BROADCAST,(const char*)&broadcast,sizeof(broadcast))) {
        struct sockaddr_in to;memset(&to,0,sizeof(to));to.sin_family=AF_INET;to.sin_port=htons(9);
        /* Limited LAN broadcast, independent of subnet prefix. Routers need not relay it. */
        to.sin_addr.s_addr=INADDR_BROADCAST;
        ok=sendto(s,(const char*)packet,sizeof(packet),0,(struct sockaddr*)&to,sizeof(to))==sizeof(packet);
    }
    closesocket(s);return ok;
}
