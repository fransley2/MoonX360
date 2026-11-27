#include "update_manifest.h"
#include "update_crypto.h"
#include "update_public_key.h"
#include "../version_info.h"
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER) && !defined(_XBOX)
#ifndef strcasecmp
#define strcasecmp _stricmp
#endif
#ifndef strncasecmp
#define strncasecmp _strnicmp
#endif
#endif

#ifdef MOONX_UPDATE_HOST_TEST
static char g_test_base_override[256];
void update_test_set_base_url(const char *base_url)
{
    if (!base_url) {
        g_test_base_override[0] = '\0';
        return;
    }
    snprintf(g_test_base_override, sizeof(g_test_base_override), "%s", base_url);
}
#endif

static const char *active_test_base(void)
{
#ifdef MOONX_UPDATE_HOST_TEST
    if (g_test_base_override[0])
        return g_test_base_override;
#endif
#ifdef MOONX_UPDATE_TEST_CONFIG
    return MOONX_UPDATE_TEST_BASE_URL;
#else
    return NULL;
#endif
}

const char *update_channel_name(int channel)
{
    return channel == UPDATE_CHANNEL_BETA ? "beta" : "stable";
}

int update_channel_from_name(const char *name)
{
    if (!name)
        return -1;
    if (strcmp(name, "stable") == 0)
        return UPDATE_CHANNEL_STABLE;
    if (strcmp(name, "beta") == 0)
        return UPDATE_CHANNEL_BETA;
    return -1;
}

static bool ascii_clean_url_chars(const char *s)
{
    size_t i;
    if (!s || !*s)
        return false;
    for (i = 0; s[i]; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 33 || c > 126)
            return false;
        if (c == '\\' || c == '#')
            return false;
    }
    return true;
}

static bool has_traversal_or_encoded_dot(const char *path)
{
    if (strstr(path, "/../") || strstr(path, "/./"))
        return true;
    size_t n = strlen(path);
    if (n >= 3 && strcmp(path + n - 3, "/..") == 0)
        return true;
    if (n >= 2 && strcmp(path + n - 2, "/.") == 0)
        return true;
    return false;
}

bool update_url_parse(const char *url, bool allow_query, bool allow_test_port,
                      struct update_parsed_url *out)
{
    const char *p, *slash, *colon, *qmark;
    size_t host_len;
    bool https = false;

    if (!url || !out || !ascii_clean_url_chars(url))
        return false;
    memset(out, 0, sizeof(*out));
    if (strncmp(url, "https://", 8) == 0) {
        https = true;
        p = url + 8;
    } else if (allow_test_port && strncmp(url, "http://", 7) == 0) {
        https = false;
        p = url + 7;
    } else {
        return false;
    }

    slash = strchr(p, '/');
    if (!slash || slash == p)
        return false;

    /* Reject userinfo anywhere in authority. */
    for (const char *c = p; c < slash; c++) {
        if (*c == '@')
            return false;
    }

    colon = NULL;
    for (const char *c = p; c < slash; c++) {
        if (*c == ':') {
            if (colon)
                return false;
            colon = c;
        }
    }

    if (colon) {
        if (!allow_test_port)
            return false;
        char *end = NULL;
        long port = strtol(colon + 1, &end, 10);
        if (colon + 1 == slash || end != slash || port < 1 || port > 65535)
            return false;
        out->port = (int)port;
        host_len = (size_t)(colon - p);
    } else {
        out->port = https ? 443 : 80;
        host_len = (size_t)(slash - p);
    }

    if (host_len == 0 || host_len >= sizeof(out->host))
        return false;
    memcpy(out->host, p, host_len);
    out->host[host_len] = '\0';
    for (size_t i = 0; i < host_len; i++) {
        char c = out->host[i];
        if (!(isalnum((unsigned char)c) || c == '.' || c == '-'))
            return false;
    }

    qmark = strchr(slash, '?');
    if (qmark && !allow_query)
        return false;

    if (strlen(slash) >= sizeof(out->path))
        return false;
    snprintf(out->path, sizeof(out->path), "%s", slash);

    /* Check path portion (before '?') for traversal or percent-encoding. */
    {
        size_t path_only_len = qmark ? (size_t)(qmark - slash) : strlen(slash);
        char path_only[512];
        if (path_only_len >= sizeof(path_only))
            return false;
        memcpy(path_only, slash, path_only_len);
        path_only[path_only_len] = '\0';
        if (has_traversal_or_encoded_dot(path_only))
            return false;
        if (!allow_query && strchr(path_only, '%'))
            return false;
    }

    out->is_https = https;
    return true;
}

static bool is_private_or_loopback_ipv4(const char *host)
{
    unsigned a, b, c, d;
    char extra;
    if (strcmp(host, "localhost") == 0)
        return true;
    if (sscanf(host, "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra) != 4)
        return false;
    if (a > 255 || b > 255 || c > 255 || d > 255)
        return false;
    if (a == 127)
        return true;
    if (a == 10)
        return true;
    if (a == 192 && b == 168)
        return true;
    if (a == 172 && b >= 16 && b <= 31)
        return true;
    return false;
}

static bool matches_test_base(const char *url, bool allow_query)
{
    const char *base = active_test_base();
    struct update_parsed_url pb, pu;
    size_t blen;
    if (!base || !*base)
        return false;
    blen = strlen(base);
    if (strncmp(url, base, blen) != 0)
        return false;
    if (!update_url_parse(base, false, true, &pb) || !update_url_parse(url, allow_query, true, &pu))
        return false;
    if (!is_private_or_loopback_ipv4(pb.host) || !is_private_or_loopback_ipv4(pu.host))
        return false;
    return pb.is_https == pu.is_https && strcmp(pb.host, pu.host) == 0 && pb.port == pu.port;
}

bool update_url_allowed_manifest(const char *url)
{
    struct update_parsed_url pu;
    if (matches_test_base(url, false))
        return true;
    if (!update_url_parse(url, false, false, &pu))
        return false;
    if (!pu.is_https)
        return false;
    if (strcasecmp(pu.host, "raw.githubusercontent.com") != 0)
        return false;
    if (strncmp(pu.path, "/fransley2/MoonX360/updates/", 28) != 0)
        return false;
    return !strchr(pu.path, '%');
}

bool update_url_allowed_asset(const char *url, bool is_redirect)
{
    struct update_parsed_url pu;
    if (matches_test_base(url, true))
        return true;
    if (!update_url_parse(url, is_redirect, false, &pu))
        return false;
    if (!pu.is_https)
        return false;
    if (!is_redirect) {
        if (strcasecmp(pu.host, "github.com") != 0)
            return false;
        if (strncmp(pu.path, "/fransley2/MoonX360/releases/download/", 38) != 0)
            return false;
        return !strchr(pu.path, '%');
    }
    if (strcasecmp(pu.host, "github.com") == 0)
        return strncmp(pu.path, "/fransley2/MoonX360/releases/", 29) == 0;
    if (strcasecmp(pu.host, "objects.githubusercontent.com") == 0 ||
        strcasecmp(pu.host, "release-assets.githubusercontent.com") == 0 ||
        strcasecmp(pu.host, "github-releases.githubusercontent.com") == 0) {
        return pu.path[0] == '/' && pu.path[1] != '\0';
    }
    return false;
}

/* ---- Strict bounded JSON parser for MoonX360 update manifest ---- */

struct jcur {
    const char *p;
    const char *end;
};

static void jskip(struct jcur *c)
{
    while (c->p < c->end && (*c->p == ' ' || *c->p == '\t' || *c->p == '\r' || *c->p == '\n'))
        c->p++;
}

static bool jconsume(struct jcur *c, char ch)
{
    jskip(c);
    if (c->p >= c->end || *c->p != ch)
        return false;
    c->p++;
    return true;
}

static int jhex4(const char *p, unsigned *out)
{
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        char ch = p[i];
        unsigned d;
        if (ch >= '0' && ch <= '9') d = (unsigned)(ch - '0');
        else if (ch >= 'a' && ch <= 'f') d = 10u + (unsigned)(ch - 'a');
        else if (ch >= 'A' && ch <= 'F') d = 10u + (unsigned)(ch - 'A');
        else return -1;
        v = (v << 4) | d;
    }
    *out = v;
    return 0;
}

static bool jappend_utf8(char *out, size_t cap, size_t *len, unsigned cp)
{
    if (cp == 0 || (cp < 32 && cp != '\n') || cp == 127 || (cp >= 0xD800 && cp <= 0xDFFF))
        return false;
    if (cp < 0x80) {
        if (*len + 1 >= cap) return false;
        out[(*len)++] = (char)cp;
    } else if (cp < 0x800) {
        if (*len + 2 >= cap) return false;
        out[(*len)++] = (char)(0xC0 | (cp >> 6));
        out[(*len)++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        if (*len + 3 >= cap) return false;
        out[(*len)++] = (char)(0xE0 | (cp >> 12));
        out[(*len)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[(*len)++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp <= 0x10FFFF) {
        if (*len + 4 >= cap) return false;
        out[(*len)++] = (char)(0xF0 | (cp >> 18));
        out[(*len)++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[(*len)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[(*len)++] = (char)(0x80 | (cp & 0x3F));
    } else {
        return false;
    }
    return true;
}

static bool jread_string(struct jcur *c, char *out, size_t cap, bool allow_newline)
{
    size_t len = 0;
    if (!jconsume(c, '"'))
        return false;
    while (c->p < c->end) {
        unsigned char ch = (unsigned char)*c->p++;
        if (ch == '"') {
            out[len] = '\0';
            return true;
        }
        if (ch < 32 || ch == 127)
            return false;
        if (ch == '\\') {
            if (c->p >= c->end)
                return false;
            char esc = *c->p++;
            if (esc == '"' || esc == '\\' || esc == '/') {
                if (len + 1 >= cap) return false;
                out[len++] = esc;
            } else if (esc == 'n') {
                if (!allow_newline || len + 1 >= cap) return false;
                out[len++] = '\n';
            } else if (esc == 't') {
                if (!allow_newline || len + 1 >= cap) return false;
                out[len++] = ' ';
            } else if (esc == 'u') {
                unsigned cp = 0;
                if (c->p + 4 > c->end || jhex4(c->p, &cp) != 0)
                    return false;
                c->p += 4;
                if (cp == '\n' && !allow_newline)
                    return false;
                if (!jappend_utf8(out, cap, &len, cp))
                    return false;
            } else {
                return false;
            }
        } else {
            if (len + 1 >= cap)
                return false;
            out[len++] = (char)ch;
        }
    }
    return false;
}

static bool jread_u64(struct jcur *c, uint64_t *out)
{
    uint64_t val = 0;
    size_t digits = 0;
    jskip(c);
    if (c->p >= c->end || !isdigit((unsigned char)*c->p))
        return false;
    if (*c->p == '0' && c->p + 1 < c->end && isdigit((unsigned char)c->p[1]))
        return false;
    while (c->p < c->end && isdigit((unsigned char)*c->p)) {
        uint64_t d = (uint64_t)(*c->p - '0');
        if (val > (UINT64_MAX - d) / 10u)
            return false;
        val = val * 10u + d;
        c->p++;
        digits++;
    }
    if (digits == 0)
        return false;
    if (c->p < c->end && (*c->p == '.' || *c->p == 'e' || *c->p == 'E'))
        return false;
    *out = val;
    return true;
}

static bool jread_notes(struct jcur *c, struct update_manifest *m)
{
    unsigned seen = 0;
    if (!jconsume(c, '{'))
        return false;
    for (;;) {
        char key[32];
        if (!jread_string(c, key, sizeof(key), false) || !jconsume(c, ':'))
            return false;
        if (strcmp(key, "en") == 0) {
            if (seen & 1u) return false;
            seen |= 1u;
            if (!jread_string(c, m->notes_en, sizeof(m->notes_en), true) || !m->notes_en[0])
                return false;
        } else if (strcmp(key, "pt-BR") == 0) {
            if (seen & 2u) return false;
            seen |= 2u;
            if (!jread_string(c, m->notes_pt_br, sizeof(m->notes_pt_br), true) || !m->notes_pt_br[0])
                return false;
        } else if (strcmp(key, "es") == 0) {
            if (seen & 4u) return false;
            seen |= 4u;
            if (!jread_string(c, m->notes_es, sizeof(m->notes_es), true) || !m->notes_es[0])
                return false;
        } else {
            return false;
        }
        jskip(c);
        if (c->p < c->end && *c->p == ',') {
            c->p++;
            continue;
        }
        if (jconsume(c, '}'))
            break;
        return false;
    }
    return seen == 7u;
}

static bool valid_version_str(const char *s)
{
    size_t n = strlen(s);
    if (n == 0 || n > 40)
        return false;
    for (size_t i = 0; i < n; i++) {
        char ch = s[i];
        if (!(isalnum((unsigned char)ch) || ch == '.' || ch == '-' || ch == '+'))
            return false;
    }
    return true;
}

static bool valid_sha256_hex(const char *s)
{
    if (strlen(s) != 64)
        return false;
    for (size_t i = 0; i < 64; i++) {
        char ch = s[i];
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')))
            return false;
    }
    return true;
}

bool update_manifest_parse(const char *raw, size_t raw_len, struct update_manifest *out)
{
    struct jcur c;
    unsigned seen = 0;
    uint64_t num = 0;

    if (!raw || !out || raw_len == 0 || raw_len > UPDATE_MAX_MANIFEST_BYTES)
        return false;
    memset(out, 0, sizeof(*out));
    c.p = raw;
    c.end = raw + raw_len;

    if (!jconsume(&c, '{'))
        return false;

    for (;;) {
        char key[48];
        if (!jread_string(&c, key, sizeof(key), false) || !jconsume(&c, ':'))
            return false;

        if (strcmp(key, "schema") == 0) {
            if (seen & (1u << 0)) return false;
            seen |= (1u << 0);
            if (!jread_u64(&c, &num) || num != 1u) return false;
            out->schema = 1;
        } else if (strcmp(key, "product") == 0) {
            if (seen & (1u << 1)) return false;
            seen |= (1u << 1);
            if (!jread_string(&c, out->product, sizeof(out->product), false)) return false;
            if (strcmp(out->product, MOONX_PRODUCT_ID) != 0) return false;
        } else if (strcmp(key, "version") == 0) {
            if (seen & (1u << 2)) return false;
            seen |= (1u << 2);
            if (!jread_string(&c, out->version, sizeof(out->version), false) ||
                !valid_version_str(out->version)) return false;
        } else if (strcmp(key, "build_id") == 0) {
            if (seen & (1u << 3)) return false;
            seen |= (1u << 3);
            if (!jread_u64(&c, &num) || num == 0 || num > (uint64_t)INT_MAX) return false;
            out->build_id = (int)num;
        } else if (strcmp(key, "channel") == 0) {
            if (seen & (1u << 4)) return false;
            seen |= (1u << 4);
            if (!jread_string(&c, out->channel, sizeof(out->channel), false) ||
                update_channel_from_name(out->channel) < 0) return false;
        } else if (strcmp(key, "min_launcher_version") == 0) {
            if (seen & (1u << 5)) return false;
            seen |= (1u << 5);
            if (!jread_u64(&c, &num) || num == 0 || num > 65535u) return false;
            out->min_launcher_version = (int)num;
        } else if (strcmp(key, "payload_size") == 0) {
            if (seen & (1u << 6)) return false;
            seen |= (1u << 6);
            if (!jread_u64(&c, &num) || num < UPDATE_MIN_XEX_BYTES || num > UPDATE_MAX_XEX_BYTES)
                return false;
            out->payload_size = num;
        } else if (strcmp(key, "sha256") == 0) {
            if (seen & (1u << 7)) return false;
            seen |= (1u << 7);
            if (!jread_string(&c, out->sha256, sizeof(out->sha256), false) ||
                !valid_sha256_hex(out->sha256)) return false;
        } else if (strcmp(key, "url") == 0) {
            if (seen & (1u << 8)) return false;
            seen |= (1u << 8);
            if (!jread_string(&c, out->url, sizeof(out->url), false) ||
                !update_url_allowed_asset(out->url, false)) return false;
        } else if (strcmp(key, "notes") == 0) {
            if (seen & (1u << 9)) return false;
            seen |= (1u << 9);
            if (!jread_notes(&c, out)) return false;
        } else {
            return false;
        }

        jskip(&c);
        if (c.p < c.end && *c.p == ',') {
            c.p++;
            continue;
        }
        if (jconsume(&c, '}'))
            break;
        return false;
    }

    jskip(&c);
    if (c.p != c.end)
        return false;

    return seen == ((1u << 10) - 1u);
}

const char *update_manifest_notes_for_lang(const struct update_manifest *m, int config_lang)
{
    if (!m)
        return "";
    if (config_lang == 2 && m->notes_pt_br[0])
        return m->notes_pt_br;
    if (config_lang == 1 && m->notes_es[0])
        return m->notes_es;
    return m->notes_en;
}
