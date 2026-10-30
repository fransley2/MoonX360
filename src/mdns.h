#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MDNS_SERVICE "_nvstream._tcp.local."
#define MDNS_MAX 16
#define MDNS_NAME 256
/* Fixed-size DNS graph. Never use sender IP as a discovered host. */
struct mdns_service {
    char instance[MDNS_NAME], target[MDNS_NAME], txt[128];
    unsigned short port;
    bool ptr;
};
struct mdns_address { char name[MDNS_NAME]; unsigned char ip[4]; bool valid; };
struct mdns_state { struct mdns_service services[MDNS_MAX]; struct mdns_address addresses[MDNS_MAX]; };
struct mdns_candidate { char ip[16]; unsigned short port; };
bool mdns_name(const unsigned char *packet,size_t size,size_t *offset,char *out,size_t capacity);
bool mdns_parse(struct mdns_state *state,const unsigned char *packet,size_t size);
int mdns_candidates(const struct mdns_state *state,struct mdns_candidate *out,int capacity);
size_t mdns_query(unsigned char *out,size_t capacity,const char *name,unsigned short type,bool unicast);
/* Socket lifecycle belongs to the menu. Poll is nonblocking and bounded. */
bool mdns_open(void);
void mdns_close(void);
bool mdns_send(const char *name,unsigned short type);
bool mdns_poll(struct mdns_state *state);
