#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define HOST_CACHE_MAX 16
struct cached_host {
    char uuid[80],name[64],ip[64];
    unsigned short port;
    bool online,paired;
    char mac[18];
    bool mac_manual;
    char alias[64];
    bool auto_wake,is_default;
    uint32_t last_seen; /* UTC seconds; zero means unavailable. */
    bool pairing_saved; /* Historical only; HTTPS validates current pairing. */
};
struct host_cache { int count; struct cached_host hosts[HOST_CACHE_MAX]; };
int host_cache_upsert(struct host_cache *cache,const struct cached_host *host);
void host_cache_decode(struct host_cache *cache,char *text);
size_t host_cache_encode(const struct host_cache *cache,char *out,size_t capacity);
