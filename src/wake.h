#pragma once
#include <stdbool.h>
#include <string.h>
/* Magic packet: six FF bytes followed by sixteen copies of a unicast MAC. */
static int wake_hex(char c) { return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1; }
static bool wake_mac(const char *text,unsigned char mac[6])
{
    if(!text || strlen(text)!=17)return false;
    unsigned any=0;char separator=text[2];
    if(separator!=':' && separator!='-')return false;
    for(int i=0;i<6;i++) {
        int a=wake_hex(text[i*3]),b=wake_hex(text[i*3+1]);
        if(a<0 || b<0 || (i<5 && text[i*3+2]!=separator))return false;
        mac[i]=(unsigned char)(a*16+b);any|=mac[i];
    }
    return any!=0 && !(mac[0]&1);
}
static bool wake_packet(const char *text,unsigned char packet[102])
{
    unsigned char mac[6];if(!wake_mac(text,mac))return false;
    memset(packet,255,6);for(int i=0;i<16;i++)memcpy(packet+6+i*6,mac,6);return true;
}
bool wake_send(const char *mac);
static bool wake_identity(const char *expected,const char *received)
{
    if(!*expected)return true; /* Legacy IP-only records have no UUID to compare. */
    while(*expected && *received) {
        unsigned char a=(unsigned char)*expected++,b=(unsigned char)*received++;
        if(a>='A'&&a<='Z')a+=32;if(b>='A'&&b<='Z')b+=32;
        if(a!=b)return false;
    }
    return *expected==*received;
}
/* UUIDs become persisted keys. Reject controls/whitespace and truncation before
 * saving a server-provided value in INI/TSV files; display names remain UTF-8. */
static bool wake_identity_valid(const char *identity)
{
    if(!identity || !*identity || strlen(identity)>=80)return false;
    for(const unsigned char *p=(const unsigned char*)identity;*p;p++)if(*p<=32 || *p>126)return false;
    return true;
}
