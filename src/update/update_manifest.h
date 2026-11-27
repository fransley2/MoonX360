#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UPDATE_CHANNEL_STABLE 0
#define UPDATE_CHANNEL_BETA   1

#define UPDATE_MAX_NOTE_BYTES 1200

struct update_manifest {
    int schema;
    char product[32];
    char version[48];
    int build_id;
    char channel[16];
    int min_launcher_version;
    uint64_t payload_size;
    char sha256[65];
    char url[512];
    char notes_en[UPDATE_MAX_NOTE_BYTES + 1];
    char notes_pt_br[UPDATE_MAX_NOTE_BYTES + 1];
    char notes_es[UPDATE_MAX_NOTE_BYTES + 1];
};

struct update_parsed_url {
    bool is_https;
    char host[128];
    int port;
    char path[512]; /* path + optional ?query */
};

/* Strict manifest parser: schema == 1, product == "moonx360", all required
 * fields present, no unknown/duplicate keys, valid URL & SHA-256. */
bool update_manifest_parse(const char *raw, size_t raw_len, struct update_manifest *out);

const char *update_manifest_notes_for_lang(const struct update_manifest *m, int config_lang);
const char *update_channel_name(int channel);
int update_channel_from_name(const char *name);

/* Validates and splits a URL. Rejects userinfo, control chars, traversal, and
 * fragments. When allow_query is false, also rejects '?'. */
bool update_url_parse(const char *url, bool allow_query, bool allow_test_port,
                      struct update_parsed_url *out);

/* Allowlist checks for manifest origin and release asset origin/redirects. */
bool update_url_allowed_manifest(const char *url);
bool update_url_allowed_asset(const char *url, bool is_redirect);

#ifdef MOONX_UPDATE_HOST_TEST
/* Host unit-test hook to point test origin checks at a local test server. */
void update_test_set_base_url(const char *base_url);
#endif

#ifdef __cplusplus
}
#endif
