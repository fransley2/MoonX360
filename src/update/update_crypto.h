#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UPDATE_MAX_MANIFEST_BYTES 65536u
#define UPDATE_MIN_XEX_BYTES      4096u
#define UPDATE_MAX_XEX_BYTES      (64u * 1024u * 1024u)

typedef void (*update_hash_progress_fn)(uint64_t done, uint64_t total, void *ctx);

/* Decodes exact lowercase hexadecimal string into out[expected_bytes].
 * When allow_trailing_ws is true, ignores trailing \r, \n, space, or tab. */
bool update_hex_decode(const char *hex, size_t hex_len, size_t expected_bytes,
                       uint8_t *out, bool allow_trailing_ws);

void update_hex_encode(const uint8_t *in, size_t len, char *out_hex);

/* Verifies an Ed25519 signature (128-hex + optional trailing newline) over
 * exact message bytes using a 64-hex public key via Monocypher 4.0.3. */
bool update_ed25519_verify(const uint8_t *msg, size_t msg_len,
                           const char *sig_hex, size_t sig_len,
                           const char *pub_hex);

bool update_sha256_bytes(const uint8_t *data, size_t len, char out_hex[65]);

bool update_sha256_file(const char *path, char out_hex[65], uint64_t *out_size,
                          update_hash_progress_fn progress_cb, void *ctx,
                          volatile int *cancel_flag);

/* Verifies that path is a regular file starting with "XEX2" within size limits. */
bool update_check_xex2_header(const char *path, uint64_t *out_size);

#ifdef __cplusplus
}
#endif
