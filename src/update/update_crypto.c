#include "update_crypto.h"
#include "../../third_party/monocypher/monocypher-ed25519.h"
#include <mbedtls/sha256.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return 10 + (c - 'a');
    return -1;
}

bool update_hex_decode(const char *hex, size_t hex_len, size_t expected_bytes,
                       uint8_t *out, bool allow_trailing_ws)
{
    size_t i;
    if (!hex || !out || expected_bytes == 0)
        return false;
    if (allow_trailing_ws) {
        while (hex_len > 0 && (hex[hex_len - 1] == '\n' || hex[hex_len - 1] == '\r' ||
                               hex[hex_len - 1] == ' ' || hex[hex_len - 1] == '\t')) {
            hex_len--;
        }
    }
    if (hex_len != expected_bytes * 2u)
        return false;
    for (i = 0; i < expected_bytes; i++) {
        int hi = hex_nibble(hex[i * 2u]);
        int lo = hex_nibble(hex[i * 2u + 1u]);
        if (hi < 0 || lo < 0)
            return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

void update_hex_encode(const uint8_t *in, size_t len, char *out_hex)
{
    static const char k_hex[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < len; i++) {
        out_hex[i * 2u] = k_hex[(in[i] >> 4) & 0x0Fu];
        out_hex[i * 2u + 1u] = k_hex[in[i] & 0x0Fu];
    }
    out_hex[len * 2u] = '\0';
}

bool update_ed25519_verify(const uint8_t *msg, size_t msg_len,
                           const char *sig_hex, size_t sig_len,
                           const char *pub_hex)
{
    uint8_t sig[64];
    uint8_t pub[32];
    if (!msg || msg_len == 0 || msg_len > UPDATE_MAX_MANIFEST_BYTES || !sig_hex || !pub_hex)
        return false;
    if (!update_hex_decode(sig_hex, sig_len, sizeof(sig), sig, true))
        return false;
    if (!update_hex_decode(pub_hex, strlen(pub_hex), sizeof(pub), pub, false))
        return false;
    return crypto_ed25519_check(sig, pub, msg, msg_len) == 0;
}

bool update_sha256_bytes(const uint8_t *data, size_t len, char out_hex[65])
{
    mbedtls_sha256_context ctx;
    uint8_t digest[32];
    int rc;
    if (!out_hex || (!data && len > 0))
        return false;
    mbedtls_sha256_init(&ctx);
    rc = mbedtls_sha256_starts_ret(&ctx, 0);
    if (rc == 0 && len > 0)
        rc = mbedtls_sha256_update_ret(&ctx, data, len);
    if (rc == 0)
        rc = mbedtls_sha256_finish_ret(&ctx, digest);
    mbedtls_sha256_free(&ctx);
    if (rc != 0)
        return false;
    update_hex_encode(digest, sizeof(digest), out_hex);
    return true;
}

bool update_sha256_file(const char *path, char out_hex[65], uint64_t *out_size,
                          update_hash_progress_fn progress_cb, void *ctx,
                          volatile int *cancel_flag)
{
    FILE *f;
    unsigned char *buf;
    mbedtls_sha256_context sha;
    uint8_t digest[32];
    uint64_t total = 0, done = 0;
    long end_pos;
    int rc;
    const size_t chunk = 65536u;

    if (!path || !out_hex)
        return false;
    f = fopen(path, "rb");
    if (!f)
        return false;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }
    end_pos = ftell(f);
    if (end_pos < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }
    total = (uint64_t)end_pos;
    buf = (unsigned char *)malloc(chunk);
    if (!buf) {
        fclose(f);
        return false;
    }
    mbedtls_sha256_init(&sha);
    rc = mbedtls_sha256_starts_ret(&sha, 0);
    while (rc == 0) {
        size_t n;
        if (cancel_flag && *cancel_flag) {
            rc = -1;
            break;
        }
        n = fread(buf, 1, chunk, f);
        if (n > 0) {
            rc = mbedtls_sha256_update_ret(&sha, buf, n);
            done += (uint64_t)n;
            if (progress_cb && rc == 0)
                progress_cb(done, total, ctx);
        }
        if (n < chunk) {
            if (ferror(f))
                rc = -1;
            break;
        }
    }
    if (rc == 0)
        rc = mbedtls_sha256_finish_ret(&sha, digest);
    mbedtls_sha256_free(&sha);
    free(buf);
    if (fclose(f) != 0 || rc != 0)
        return false;
    if (done != total)
        return false;
    if (out_size)
        *out_size = total;
    update_hex_encode(digest, sizeof(digest), out_hex);
    return true;
}

bool update_check_xex2_header(const char *path, uint64_t *out_size)
{
    FILE *f;
    unsigned char magic[4];
    long size;
    if (!path)
        return false;
    f = fopen(path, "rb");
    if (!f)
        return false;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }
    size = ftell(f);
    if (size < (long)UPDATE_MIN_XEX_BYTES || (uint64_t)size > (uint64_t)UPDATE_MAX_XEX_BYTES ||
        fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }
    if (fread(magic, 1, 4, f) != 4 || ferror(f)) {
        fclose(f);
        return false;
    }
    fclose(f);
    if (memcmp(magic, "XEX2", 4) != 0)
        return false;
    if (out_size)
        *out_size = (uint64_t)size;
    return true;
}
