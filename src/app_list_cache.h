#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
/* UI/worker publication only. Entries remain the existing bounded apps array. */
struct app_list_cache {bool valid;uint32_t fetched;char host[80];};
static bool app_list_cache_valid(const struct app_list_cache *cache,const char *host,uint32_t now)
{
    return cache->valid && host && !strcmp(cache->host,host) && (uint32_t)(now-cache->fetched)<30000;
}
static void app_list_cache_store(struct app_list_cache *cache,const char *host,uint32_t now)
{
    cache->valid=host && strlen(host)<sizeof(cache->host);
    if(cache->valid)snprintf(cache->host,sizeof(cache->host),"%s",host);
    cache->fetched=now;
}
