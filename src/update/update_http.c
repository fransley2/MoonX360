#include "update_http.h"
#include "update_manifest.h"
#include "update_tls_roots.h"
#include "../crypto.h"
#include "../platform.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _XBOX
#include <xtl.h>
#include <winsock2.h>
#define upd_sock_close closesocket
#define upd_sock_again() (WSAGetLastError() == WSAEWOULDBLOCK || WSAGetLastError() == WSAETIMEDOUT)
#elif defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#define upd_sock_close closesocket
#define upd_sock_again() (WSAGetLastError() == WSAEWOULDBLOCK || WSAGetLastError() == WSAETIMEDOUT)
#ifndef strcasecmp
#define strcasecmp _stricmp
#endif
#ifndef strncasecmp
#define strncasecmp _strnicmp
#endif
#else
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <unistd.h>
#define upd_sock_close close
#define upd_sock_again() (errno == EAGAIN || errno == EWOULDBLOCK)
#endif

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#define UPD_CHUNK_BYTES (256u * 1024u)
#define UPD_MAX_REDIRECTS 5
#define UPD_MAX_RETRIES   3

static mbedtls_x509_crt g_upd_ca_roots;
static bool g_upd_tls_ready = false;

#ifdef MOONX_UPDATE_HOST_TEST
static mbedtls_x509_crt g_test_ca_extra;
static bool g_test_ca_loaded = false;
static bool g_test_force_verify = true;

bool update_http_test_set_ca_pem(const char *pem, size_t pem_len)
{
    if (g_test_ca_loaded) {
        mbedtls_x509_crt_free(&g_test_ca_extra);
        g_test_ca_loaded = false;
    }
    if (!pem || pem_len == 0)
        return true;
    mbedtls_x509_crt_init(&g_test_ca_extra);
    if (mbedtls_x509_crt_parse(&g_test_ca_extra, (const unsigned char *)pem, pem_len + 1u) != 0) {
        mbedtls_x509_crt_free(&g_test_ca_extra);
        return false;
    }
    g_test_ca_loaded = true;
    return true;
}

void update_http_test_force_tls_verify(bool enforce)
{
    g_test_force_verify = enforce;
}
#endif

bool update_http_global_init(void)
{
    int loaded = 0;
    if (g_upd_tls_ready)
        return true;
    mbedtls_x509_crt_init(&g_upd_ca_roots);
    for (int i = 0; i < g_update_tls_root_count; i++) {
        if (mbedtls_x509_crt_parse_der(&g_upd_ca_roots,
                                       g_update_tls_roots[i].der,
                                       g_update_tls_roots[i].len) == 0) {
            loaded++;
        }
    }
    g_upd_tls_ready = loaded > 0;
    return g_upd_tls_ready;
}

void update_http_global_shutdown(void)
{
    if (g_upd_tls_ready) {
        mbedtls_x509_crt_free(&g_upd_ca_roots);
        g_upd_tls_ready = false;
    }
#ifdef MOONX_UPDATE_HOST_TEST
    if (g_test_ca_loaded) {
        mbedtls_x509_crt_free(&g_test_ca_extra);
        g_test_ca_loaded = false;
    }
#endif
}

/* ---- DNS Cache & Resolution ---- */

struct dns_slot {
    char host[128];
    uint32_t addr_be;
    uint32_t when_ms;
    bool valid;
};

static struct dns_slot g_dns_cache[8];

static bool resolve_ipv4(const char *host, uint32_t *out_addr_be, int timeout_ms,
                         volatile int *cancel_flag, int *out_detail)
{
    uint32_t now = plat_now_ms();
    unsigned long literal = inet_addr(host);
    if (out_detail)
        *out_detail = 0;
    if (literal != INADDR_NONE || strcmp(host, "255.255.255.255") == 0) {
        *out_addr_be = (uint32_t)literal;
        return true;
    }

    for (int i = 0; i < 8; i++) {
        if (g_dns_cache[i].valid && strcmp(g_dns_cache[i].host, host) == 0 &&
            (uint32_t)(now - g_dns_cache[i].when_ms) < 300000u) {
            *out_addr_be = g_dns_cache[i].addr_be;
            return true;
        }
    }

#ifdef _XBOX
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    XNDNS *dns = NULL;
    bool ok = false;
    int dns_rc;
    if (!ev) {
        if (out_detail) *out_detail = -3;
        return false;
    }
    dns_rc = XNetDnsLookup(host, ev, &dns);
    if (dns_rc == 0 && dns) {
        uint32_t start = plat_now_ms();
        while (WaitForSingleObject(ev, 50) == WAIT_TIMEOUT) {
            if ((cancel_flag && plat_atomic_load(cancel_flag)) ||
                (int)(plat_now_ms() - start) >= timeout_ms) {
                break;
            }
        }
        if (dns->iStatus == 0 && dns->cina > 0) {
            *out_addr_be = (uint32_t)dns->aina[0].S_un.S_addr;
            ok = true;
        } else if (out_detail) {
            *out_detail = dns->iStatus ? dns->iStatus : -4;
        }
        XNetDnsRelease(dns);
    } else if (out_detail) {
        *out_detail = dns_rc ? dns_rc : -2;
    }
    CloseHandle(ev);
    if (!ok)
        return false;
#else
    (void)timeout_ms;
    if (cancel_flag && plat_atomic_load(cancel_flag))
        return false;
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    int gai_rc = getaddrinfo(host, NULL, &hints, &res);
    if (gai_rc != 0 || !res) {
        if (out_detail) *out_detail = gai_rc;
        return false;
    }
    *out_addr_be = (uint32_t)((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr;
    freeaddrinfo(res);
#endif

    int oldest = 0;
    for (int i = 0; i < 8; i++) {
        if (!g_dns_cache[i].valid) {
            oldest = i;
            break;
        }
        if ((uint32_t)(now - g_dns_cache[i].when_ms) >
            (uint32_t)(now - g_dns_cache[oldest].when_ms)) {
            oldest = i;
        }
    }
    snprintf(g_dns_cache[oldest].host, sizeof(g_dns_cache[oldest].host), "%s", host);
    g_dns_cache[oldest].addr_be = *out_addr_be;
    g_dns_cache[oldest].when_ms = now;
    g_dns_cache[oldest].valid = true;
    return true;
}

/* ---- Independent Update Socket + TLS Context ---- */

struct upd_conn {
    int fd;
    bool tls;
    uint32_t deadline_ms;
    volatile int *cancel;
    int last_detail;
    uint32_t last_verify;
    uint32_t last_ip_be;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
};

static bool conn_live(const struct upd_conn *c)
{
    if (c->cancel && plat_atomic_load(c->cancel))
        return false;
    return (int32_t)(c->deadline_ms - plat_now_ms()) > 0;
}

static bool conn_wait(struct upd_conn *c, bool writing)
{
    while (conn_live(c)) {
        int32_t rem = (int32_t)(c->deadline_ms - plat_now_ms());
        int wait_ms = rem < 25 ? rem : 25;
        fd_set fds, errfds;
        struct timeval tv;
        if (wait_ms <= 0)
            return false;
        FD_ZERO(&fds);
        FD_ZERO(&errfds);
        FD_SET((unsigned)c->fd, &fds);
        FD_SET((unsigned)c->fd, &errfds);
        tv.tv_sec = 0;
        tv.tv_usec = wait_ms * 1000;
        int n = select(c->fd + 1, writing ? NULL : &fds, writing ? &fds : NULL, &errfds, &tv);
        if (n < 0 || FD_ISSET((unsigned)c->fd, &errfds))
            return false;
        if (n > 0)
            return true;
    }
    return false;
}

static int upd_bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    struct upd_conn *c = (struct upd_conn *)ctx;
    int n = (int)send((unsigned)c->fd, (const char *)buf, (int)len, 0);
    if (n < 0)
        return upd_sock_again() ? MBEDTLS_ERR_SSL_WANT_WRITE : MBEDTLS_ERR_NET_SEND_FAILED;
    return n;
}

static int upd_bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    struct upd_conn *c = (struct upd_conn *)ctx;
    int n = (int)recv((unsigned)c->fd, (char *)buf, (int)len, 0);
    if (n < 0)
        return upd_sock_again() ? MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_NET_RECV_FAILED;
    return n;
}

static void upd_conn_close(struct upd_conn *c)
{
    if (c->tls) {
        if (conn_live(c))
            mbedtls_ssl_close_notify(&c->ssl);
        mbedtls_ssl_free(&c->ssl);
        mbedtls_ssl_config_free(&c->conf);
        c->tls = false;
    }
    if (c->fd >= 0) {
        upd_sock_close((unsigned)c->fd);
        c->fd = -1;
    }
}

static enum update_http_err upd_conn_open(struct upd_conn *c,
                                          const struct update_parsed_url *pu,
                                          int timeout_ms, volatile int *cancel_flag)
{
    uint32_t addr_be = 0;
    struct sockaddr_in sa;
    int rc;

    memset(c, 0, sizeof(*c));
    c->fd = -1;
    c->cancel = cancel_flag;
    c->deadline_ms = plat_now_ms() + (uint32_t)timeout_ms;

    if (!resolve_ipv4(pu->host, &addr_be, timeout_ms < 8000 ? timeout_ms : 8000, cancel_flag, &c->last_detail)) {
        if (cancel_flag && plat_atomic_load(cancel_flag))
            return UPD_HTTP_CANCELLED;
        return UPD_HTTP_DNS;
    }
    c->last_ip_be = addr_be;

#ifdef _XBOX
    c->fd = (int)xdk_socket_wan(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
    c->fd = (int)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#endif
    if (c->fd < 0)
        return UPD_HTTP_CONNECT;

#if defined(_XBOX) || defined(_WIN32)
    {
        u_long nb = 1;
        if (ioctlsocket((unsigned)c->fd, FIONBIO, &nb) != 0) {
            c->last_detail = WSAGetLastError();
            upd_conn_close(c);
            return UPD_HTTP_CONNECT;
        }
    }
#else
    {
        int flags = fcntl(c->fd, F_GETFL, 0);
        if (flags < 0 || fcntl(c->fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            c->last_detail = errno;
            upd_conn_close(c);
            return UPD_HTTP_CONNECT;
        }
    }
#endif

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((unsigned short)pu->port);
    sa.sin_addr.s_addr = addr_be;
    rc = connect((unsigned)c->fd, (struct sockaddr *)&sa, sizeof(sa));
    if (rc < 0) {
#if defined(_XBOX) || defined(_WIN32)
        int werr = WSAGetLastError();
        if (werr != WSAEWOULDBLOCK && werr != WSAEINPROGRESS) {
            c->last_detail = werr;
            upd_conn_close(c);
            return UPD_HTTP_CONNECT;
        }
#else
        if (errno != EINPROGRESS && errno != EWOULDBLOCK) {
            c->last_detail = errno;
            upd_conn_close(c);
            return UPD_HTTP_CONNECT;
        }
#endif
        if (!conn_wait(c, true)) {
            upd_conn_close(c);
            return (cancel_flag && plat_atomic_load(cancel_flag)) ? UPD_HTTP_CANCELLED : UPD_HTTP_CONNECT;
        }
#if !defined(_XBOX)
        {
            int so_err = 0;
            socklen_t so_len = sizeof(so_err);
            if (getsockopt((unsigned)c->fd, SOL_SOCKET, SO_ERROR, (char *)&so_err, &so_len) != 0 || so_err != 0) {
                c->last_detail = so_err;
                upd_conn_close(c);
                return UPD_HTTP_CONNECT;
            }
        }
#endif
    }

    if (!pu->is_https)
        return UPD_HTTP_OK;

    if (!update_http_global_init()) {
        c->last_detail = -1001;
        upd_conn_close(c);
        return UPD_HTTP_TLS;
    }

    c->tls = true;
    mbedtls_ssl_init(&c->ssl);
    mbedtls_ssl_config_init(&c->conf);
    if ((rc = mbedtls_ssl_config_defaults(&c->conf, MBEDTLS_SSL_IS_CLIENT,
                                          MBEDTLS_SSL_TRANSPORT_STREAM,
                                          MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
        c->last_detail = rc;
        upd_conn_close(c);
        return UPD_HTTP_TLS;
    }

    /* Mandatory certificate verification — never disabled in production. */
#ifdef MOONX_UPDATE_HOST_TEST
    mbedtls_ssl_conf_ca_chain(&c->conf, g_test_ca_loaded ? &g_test_ca_extra : &g_upd_ca_roots, NULL);
    mbedtls_ssl_conf_authmode(&c->conf, g_test_force_verify ? MBEDTLS_SSL_VERIFY_REQUIRED : MBEDTLS_SSL_VERIFY_NONE);
#else
    mbedtls_ssl_conf_ca_chain(&c->conf, &g_upd_ca_roots, NULL);
    mbedtls_ssl_conf_authmode(&c->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
#endif
    mbedtls_ssl_conf_rng(&c->conf, crypto_rng_callback, crypto_drbg());

    if ((rc = mbedtls_ssl_setup(&c->ssl, &c->conf)) != 0 ||
        (rc = mbedtls_ssl_set_hostname(&c->ssl, pu->host)) != 0) {
        c->last_detail = rc;
        upd_conn_close(c);
        return UPD_HTTP_TLS;
    }
    mbedtls_ssl_set_bio(&c->ssl, c, upd_bio_send, upd_bio_recv, NULL);

    while ((rc = mbedtls_ssl_handshake(&c->ssl)) != 0) {
        if (!conn_live(c)) {
            c->last_detail = rc;
            c->last_verify = mbedtls_ssl_get_verify_result(&c->ssl);
            upd_conn_close(c);
            return (cancel_flag && plat_atomic_load(cancel_flag)) ? UPD_HTTP_CANCELLED : UPD_HTTP_TLS;
        }
        if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (!conn_wait(c, rc == MBEDTLS_ERR_SSL_WANT_WRITE)) {
                c->last_detail = rc;
                upd_conn_close(c);
                return (cancel_flag && plat_atomic_load(cancel_flag)) ? UPD_HTTP_CANCELLED : UPD_HTTP_TLS;
            }
            continue;
        }
        c->last_detail = rc;
        c->last_verify = mbedtls_ssl_get_verify_result(&c->ssl);
        upd_conn_close(c);
        return UPD_HTTP_TLS;
    }

    if ((c->last_verify = mbedtls_ssl_get_verify_result(&c->ssl)) != 0) {
        c->last_detail = MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
        upd_conn_close(c);
        return UPD_HTTP_TLS;
    }
    return UPD_HTTP_OK;
}

static int upd_conn_write_all(struct upd_conn *c, const char *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        if (!conn_live(c))
            return -1;
        if (!c->tls && !conn_wait(c, true))
            return -1;
        int n = c->tls
            ? mbedtls_ssl_write(&c->ssl, (const unsigned char *)buf + off, len - off)
            : (int)send((unsigned)c->fd, buf + off, (int)(len - off), 0);
        if (c->tls && (n == MBEDTLS_ERR_SSL_WANT_WRITE || n == MBEDTLS_ERR_SSL_WANT_READ)) {
            if (!conn_wait(c, n == MBEDTLS_ERR_SSL_WANT_WRITE))
                return -1;
            continue;
        }
        if (!c->tls && n < 0 && upd_sock_again())
            continue;
        if (n <= 0)
            return -1;
        off += (size_t)n;
    }
    return 0;
}

static int upd_conn_read_some(struct upd_conn *c, char *buf, size_t cap)
{
    for (;;) {
        if (!conn_live(c))
            return -1;
        if (!c->tls && !conn_wait(c, false))
            return -1;
        int n = c->tls
            ? mbedtls_ssl_read(&c->ssl, (unsigned char *)buf, cap)
            : (int)recv((unsigned)c->fd, buf, (int)cap, 0);
        if (!c->tls && n < 0 && upd_sock_again())
            continue;
        if (!c->tls)
            return n;
        if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (!conn_wait(c, n == MBEDTLS_ERR_SSL_WANT_WRITE))
                return -1;
            continue;
        }
        if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || n == 0)
            return 0;
        return n < 0 ? -1 : n;
    }
}

static const char *find_header_value(const char *hdrs, const char *hdr_end,
                                     const char *name, char *out, size_t cap)
{
    size_t nlen = strlen(name);
    const char *p = hdrs;
    while (p < hdr_end) {
        const char *eol = strstr(p, "\r\n");
        if (!eol || eol > hdr_end)
            break;
        if ((size_t)(eol - p) > nlen && strncasecmp(p, name, nlen) == 0 && p[nlen] == ':') {
            const char *v = p + nlen + 1;
            while (v < eol && (*v == ' ' || *v == '\t'))
                v++;
            while (eol > v && (eol[-1] == ' ' || eol[-1] == '\t'))
                eol--;
            size_t vlen = (size_t)(eol - v);
            if (vlen >= cap)
                vlen = cap - 1u;
            memcpy(out, v, vlen);
            out[vlen] = '\0';
            return out;
        }
        p = eol + 2;
    }
    return NULL;
}

static long dechunk_inplace(char *body, size_t len)
{
    char *in = body, *out = body, *end = body + len;
    while (in < end) {
        char *eol = strstr(in, "\r\n");
        char *num_end = NULL;
        unsigned long chunk_len = strtoul(in, &num_end, 16);
        if (!eol || eol == in)
            return -1;
        in = eol + 2;
        if (chunk_len == 0)
            break;
        if (in + chunk_len + 2 > end)
            return -1;
        memmove(out, in, chunk_len);
        out += chunk_len;
        in += chunk_len + 2;
    }
    *out = '\0';
    return (long)(out - body);
}

static bool is_redirect_status(int status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

static enum update_http_err send_one_http(const struct update_parsed_url *pu,
                                          const char *extra_headers,
                                          size_t max_body_bytes,
                                          int timeout_ms,
                                          volatile int *cancel_flag,
                                          int *out_status,
                                          char *out_location, size_t loc_cap,
                                          char *out_content_range, size_t cr_cap,
                                          int64_t *out_content_length,
                                          char **out_body, size_t *out_len,
                                          int *out_detail, uint32_t *out_verify,
                                          uint32_t *out_ip_be)
{
    struct upd_conn c;
    char req[1024];
    char *buf = NULL;
    size_t cap = 16384u, len = 0, hard_limit = max_body_bytes + 8192u;
    ptrdiff_t hdr_off = -1;
    int status = 0;
    int64_t cl = -1;
    bool chunked = false;
    enum update_http_err err;

    *out_status = 0;
    *out_body = NULL;
    *out_len = 0;
    if (out_location && loc_cap) out_location[0] = '\0';
    if (out_content_range && cr_cap) out_content_range[0] = '\0';
    if (out_content_length) *out_content_length = -1;
    if (out_detail) *out_detail = 0;
    if (out_verify) *out_verify = 0;
    if (out_ip_be) *out_ip_be = 0;

    err = upd_conn_open(&c, pu, timeout_ms, cancel_flag);
    if (out_ip_be) *out_ip_be = c.last_ip_be;
    if (err != UPD_HTTP_OK) {
        if (out_detail) *out_detail = c.last_detail;
        if (out_verify) *out_verify = c.last_verify;
        return err;
    }

    {
        int rlen;
        if ((pu->is_https && pu->port == 443) || (!pu->is_https && pu->port == 80)) {
            rlen = snprintf(req, sizeof(req),
                "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: MoonX360-Updater/1.0\r\n"
                "Accept-Encoding: identity\r\n%sConnection: close\r\n\r\n",
                pu->path, pu->host, extra_headers ? extra_headers : "");
        } else {
            rlen = snprintf(req, sizeof(req),
                "GET %s HTTP/1.1\r\nHost: %s:%d\r\nUser-Agent: MoonX360-Updater/1.0\r\n"
                "Accept-Encoding: identity\r\n%sConnection: close\r\n\r\n",
                pu->path, pu->host, pu->port, extra_headers ? extra_headers : "");
        }
        if (rlen <= 0 || (size_t)rlen >= sizeof(req) || upd_conn_write_all(&c, req, (size_t)rlen) != 0) {
            upd_conn_close(&c);
            return (cancel_flag && plat_atomic_load(cancel_flag)) ? UPD_HTTP_CANCELLED : UPD_HTTP_IO;
        }
    }

    if (cap > hard_limit)
        cap = hard_limit;
    buf = (char *)malloc(cap + 1u);
    if (!buf) {
        upd_conn_close(&c);
        return UPD_HTTP_IO;
    }

    for (;;) {
        int n;
        if (len == cap) {
            if (cap >= hard_limit) {
                free(buf);
                upd_conn_close(&c);
                return UPD_HTTP_TOO_LARGE;
            }
            size_t next_cap = cap * 2u;
            if (next_cap > hard_limit)
                next_cap = hard_limit;
            char *nb = (char *)realloc(buf, next_cap + 1u);
            if (!nb) {
                free(buf);
                upd_conn_close(&c);
                return UPD_HTTP_IO;
            }
            buf = nb;
            cap = next_cap;
        }
        n = upd_conn_read_some(&c, buf + len, cap - len);
        if (n < 0) {
            free(buf);
            upd_conn_close(&c);
            return (cancel_flag && plat_atomic_load(cancel_flag)) ? UPD_HTTP_CANCELLED : UPD_HTTP_IO;
        }
        if (n == 0)
            break;
        len += (size_t)n;
        buf[len] = '\0';

        if (hdr_off < 0) {
            char *hdr_end = strstr(buf, "\r\n\r\n");
            if (hdr_end) {
                char val[512];
                hdr_off = hdr_end - buf;
                if (sscanf(buf, "HTTP/%*d.%*d %d", &status) != 1) {
                    free(buf);
                    upd_conn_close(&c);
                    return UPD_HTTP_IO;
                }
                if (find_header_value(buf, hdr_end, "Content-Length", val, sizeof(val))) {
                    int64_t v = 0;
                    const char *p = val;
                    if (!*p) {
                        free(buf);
                        upd_conn_close(&c);
                        return UPD_HTTP_IO;
                    }
                    while (*p) {
                        if (*p < '0' || *p > '9' || v > (INT64_MAX - 9) / 10) {
                            free(buf);
                            upd_conn_close(&c);
                            return UPD_HTTP_IO;
                        }
                        v = v * 10 + (*p - '0');
                        p++;
                    }
                    cl = v;
                    if ((uint64_t)cl > max_body_bytes) {
                        free(buf);
                        upd_conn_close(&c);
                        return UPD_HTTP_TOO_LARGE;
                    }
                }
                if (find_header_value(buf, hdr_end, "Transfer-Encoding", val, sizeof(val)) &&
                    strstr(val, "chunked")) {
                    chunked = true;
                }
                if (out_location && loc_cap)
                    find_header_value(buf, hdr_end, "Location", out_location, loc_cap);
                if (out_content_range && cr_cap)
                    find_header_value(buf, hdr_end, "Content-Range", out_content_range, cr_cap);
                if (is_redirect_status(status))
                    break;
            }
        }
        if (hdr_off >= 0 && !chunked && cl >= 0) {
            size_t hdr_bytes = (size_t)hdr_off + 4u;
            if (len >= hdr_bytes + (size_t)cl)
                break;
        }
    }

    upd_conn_close(&c);

    if (hdr_off < 0) {
        free(buf);
        return UPD_HTTP_IO;
    }

    *out_status = status;
    if (out_content_length)
        *out_content_length = cl;

    if (is_redirect_status(status)) {
        free(buf);
        return UPD_HTTP_OK;
    }

    {
        char *body_ptr = buf + (size_t)hdr_off + 4u;
        size_t body_len = len - ((size_t)hdr_off + 4u);
        if (!chunked && cl >= 0 && (size_t)cl < body_len)
            body_len = (size_t)cl;
        if (!chunked && cl >= 0 && body_len != (size_t)cl) {
            free(buf);
            return UPD_HTTP_IO;
        }
        memmove(buf, body_ptr, body_len);
        buf[body_len] = '\0';
        if (chunked) {
            long dl = dechunk_inplace(buf, body_len);
            if (dl < 0) {
                free(buf);
                return UPD_HTTP_IO;
            }
            body_len = (size_t)dl;
        }
        if (body_len > max_body_bytes) {
            free(buf);
            return UPD_HTTP_TOO_LARGE;
        }
        *out_body = buf;
        *out_len = body_len;
    }
    return UPD_HTTP_OK;
}

static bool follow_redirects(const char *initial_url, bool is_manifest,
                             const char *extra_headers, size_t max_body_bytes,
                             int timeout_ms, volatile int *cancel_flag,
                             int *out_status, char *out_content_range, size_t cr_cap,
                             int64_t *out_content_length,
                             char **out_body, size_t *out_len,
                             enum update_http_err *out_err,
                             int *out_detail, uint32_t *out_verify,
                             uint32_t *out_ip_be)
{
    char current[512];
    snprintf(current, sizeof(current), "%s", initial_url);

    for (int hop = 0; hop <= UPD_MAX_REDIRECTS; hop++) {
        bool allowed = is_manifest
            ? update_url_allowed_manifest(current)
            : update_url_allowed_asset(current, hop > 0);
        struct update_parsed_url pu;
        bool allow_port = false;
        char loc[512];

        if (!allowed) {
            *out_err = UPD_HTTP_BAD_URL;
            return false;
        }
#if defined(MOONX_UPDATE_TEST_CONFIG) || defined(MOONX_UPDATE_HOST_TEST)
        allow_port = true;
#endif
        if (!update_url_parse(current, !is_manifest && hop > 0, allow_port, &pu)) {
            *out_err = UPD_HTTP_BAD_URL;
            return false;
        }

        loc[0] = '\0';
        *out_err = send_one_http(&pu, extra_headers, max_body_bytes, timeout_ms,
                                 cancel_flag, out_status, loc, sizeof(loc),
                                 out_content_range, cr_cap, out_content_length,
                                 out_body, out_len, out_detail, out_verify, out_ip_be);
        if (*out_err != UPD_HTTP_OK)
            return false;

        if (!is_redirect_status(*out_status))
            return true;

        if (hop == UPD_MAX_REDIRECTS || !loc[0]) {
            *out_err = UPD_HTTP_REDIRECT_LIMIT;
            return false;
        }
        snprintf(current, sizeof(current), "%s", loc);
    }

    *out_err = UPD_HTTP_REDIRECT_LIMIT;
    return false;
}

bool update_http_get_buffer(const char *url, bool is_manifest, size_t max_bytes,
                            int timeout_ms, volatile int *cancel_flag,
                            struct update_http_result *res)
{
    memset(res, 0, sizeof(*res));
    if (!url || max_bytes == 0) {
        res->err = UPD_HTTP_BAD_URL;
        return false;
    }
    if (!follow_redirects(url, is_manifest, NULL, max_bytes, timeout_ms, cancel_flag,
                          &res->status, NULL, 0, NULL, &res->body, &res->len, &res->err,
                          &res->detail_err, &res->verify_flags, &res->resolved_ip_be)) {
        return false;
    }
    return true;
}

void update_http_result_free(struct update_http_result *res)
{
    if (!res)
        return;
    free(res->body);
    res->body = NULL;
    res->len = 0;
}

bool update_disk_free_bytes(const char *root_dir, uint64_t *out_free_bytes)
{
#if defined(_XBOX) || defined(_WIN32)
    ULARGE_INTEGER free_avail;
    char dir_buf[320];
    size_t n;
    if (!root_dir || !out_free_bytes)
        return false;
    n = strlen(root_dir);
    if (n + 2 >= sizeof(dir_buf))
        return false;
    memcpy(dir_buf, root_dir, n + 1);
    if (dir_buf[n - 1] != '\\' && dir_buf[n - 1] != '/') {
        dir_buf[n] = '\\';
        dir_buf[n + 1] = '\0';
    }
    if (!GetDiskFreeSpaceExA(dir_buf, &free_avail, NULL, NULL))
        return false;
    *out_free_bytes = (uint64_t)free_avail.QuadPart;
    return true;
#else
    struct statvfs st;
    if (!root_dir || !out_free_bytes || statvfs(root_dir, &st) != 0)
        return false;
    *out_free_bytes = (uint64_t)st.f_bavail * (uint64_t)st.f_frsize;
    return true;
#endif
}

enum update_http_err update_http_download_file(const char *url,
                                               const char *dest_part_path,
                                               uint64_t expected_size,
                                               uint64_t max_size,
                                               int timeout_ms,
                                               volatile int *cancel_flag,
                                               update_dl_progress_fn progress_cb,
                                               void *progress_ctx)
{
    FILE *f;
    uint64_t offset = 0;
    uint32_t start_ms = plat_now_ms();
    enum update_http_err final_err = UPD_HTTP_OK;

    if (!url || !dest_part_path || expected_size == 0 || expected_size > max_size)
        return UPD_HTTP_BAD_URL;

    f = fopen(dest_part_path, "wb");
    if (!f)
        return UPD_HTTP_DISK_WRITE;

    while (offset < expected_size) {
        uint64_t last = offset + UPD_CHUNK_BYTES - 1u;
        if (last >= expected_size)
            last = expected_size - 1u;
        uint64_t want = last - offset + 1u;
        char range_hdr[96];
        bool chunk_ok = false;

        snprintf(range_hdr, sizeof(range_hdr), "Range: bytes=%llu-%llu\r\n",
                 (unsigned long long)offset, (unsigned long long)last);

        for (int attempt = 0; attempt < UPD_MAX_RETRIES; attempt++) {
            int status = 0;
            char cr[128];
            int64_t cl = -1;
            char *body = NULL;
            size_t body_len = 0;
            enum update_http_err err = UPD_HTTP_OK;

            if (cancel_flag && plat_atomic_load(cancel_flag)) {
                final_err = UPD_HTTP_CANCELLED;
                goto done;
            }

            if (follow_redirects(url, false, range_hdr, (size_t)expected_size,
                                 timeout_ms, cancel_flag, &status, cr, sizeof(cr),
                                 &cl, &body, &body_len, &err, NULL, NULL, NULL)) {
                char expected_cr[128];
                snprintf(expected_cr, sizeof(expected_cr), "bytes %llu-%llu/%llu",
                         (unsigned long long)offset, (unsigned long long)last,
                         (unsigned long long)expected_size);
                bool valid_206 = (status == 206 && strcmp(cr, expected_cr) == 0 && body_len == (size_t)want);
                bool valid_200 = (status == 200 && offset == 0 && body_len == (size_t)expected_size);

                if (valid_206 || valid_200) {
                    if (offset == 0 && (body_len < 4u || memcmp(body, "XEX2", 4) != 0)) {
                        free(body);
                        final_err = UPD_HTTP_STATUS;
                        goto done;
                    }
                    if (fwrite(body, 1, body_len, f) != body_len) {
                        free(body);
                        final_err = UPD_HTTP_DISK_WRITE;
                        goto done;
                    }
                    free(body);
                    offset += (uint64_t)body_len;
                    if (progress_cb) {
                        uint32_t elapsed = plat_now_ms() - start_ms;
                        uint32_t bps = elapsed > 0 ? (uint32_t)((offset * 1000u) / elapsed) : 0;
                        progress_cb(offset, expected_size, bps, progress_ctx);
                    }
                    chunk_ok = true;
                    break;
                }
                free(body);
                err = UPD_HTTP_STATUS;
            } else {
                free(body);
            }

            final_err = err;
            if (err == UPD_HTTP_CANCELLED || err == UPD_HTTP_BAD_URL ||
                err == UPD_HTTP_TLS || err == UPD_HTTP_TOO_LARGE ||
                err == UPD_HTTP_REDIRECT_LIMIT) {
                goto done;
            }
            if (attempt + 1 < UPD_MAX_RETRIES)
                plat_sleep_ms(300);
        }

        if (!chunk_ok)
            goto done;
    }

    if (fflush(f) != 0) {
        final_err = UPD_HTTP_DISK_WRITE;
        goto done;
    }
    final_err = UPD_HTTP_OK;

done:
    if (fclose(f) != 0 && final_err == UPD_HTTP_OK)
        final_err = UPD_HTTP_DISK_WRITE;
    return final_err;
}
