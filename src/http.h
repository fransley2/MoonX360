#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "crypto.h"

struct http_response {
    int status;
    char *body;     /* NUL-terminated, caller frees */
    size_t len;
};

/*
 * Performs GET http(s)://host:port/path_and_query. HTTPS presents the
 * client identity. timeout_ms bounds each wait for data (pairing waits on
 * the user typing a PIN, so it needs a long one). Returns false on
 * network/TLS failure; HTTP errors are reported via status.
 */
bool http_get(const char *host, int port, bool https, const char *path,
              const struct client_identity *id, int timeout_ms, struct http_response *out);
/* One absolute connect/TLS/send/body deadline; cooperative cancellation in <=10ms
 * socket waits. The worker owns and closes its socket (no cross-thread fd reuse). */
bool http_get_cancel(const char *host,int port,bool https,const char *path,
                     const struct client_identity *id,int timeout_ms,
                     volatile int *cancel,struct http_response *out);
void http_response_free(struct http_response *r);
/* Wake-only plain HTTP: one absolute deadline covers connect/send/read;
 * cancellation is sampled at most every 10 ms while waiting for the socket. */
bool http_get_plain_probe(const char *host,int port,const char *path,int timeout_ms,
                          volatile int *cancel,struct http_response *out);
