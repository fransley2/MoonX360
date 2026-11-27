#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*update_dl_progress_fn)(uint64_t done, uint64_t total,
                                      uint32_t speed_bps, void *ctx);

enum update_http_err {
    UPD_HTTP_OK = 0,
    UPD_HTTP_CANCELLED,
    UPD_HTTP_BAD_URL,
    UPD_HTTP_REDIRECT_LIMIT,
    UPD_HTTP_DNS,
    UPD_HTTP_CONNECT,
    UPD_HTTP_TLS,
    UPD_HTTP_IO,
    UPD_HTTP_STATUS,
    UPD_HTTP_TOO_LARGE,
    UPD_HTTP_DISK_SPACE,
    UPD_HTTP_DISK_WRITE
};

struct update_http_result {
    int status;
    enum update_http_err err;
    int detail_err;
    uint32_t verify_flags;
    uint32_t resolved_ip_be;
    char *body;
    size_t len;
};

/* Initializes embedded TLS CA root chain (and optional test CA root). */
bool update_http_global_init(void);
void update_http_global_shutdown(void);

#ifdef MOONX_UPDATE_HOST_TEST
/* Host unit-test hook to add a disposable local TLS CA PEM for test servers. */
bool update_http_test_set_ca_pem(const char *pem, size_t pem_len);
void update_http_test_force_tls_verify(bool enforce);
#endif

/* Bounded GET into memory (<= max_bytes, at most 64 KiB) with manual redirect
 * revalidation and TLS certificate validation. Caller frees res->body. */
bool update_http_get_buffer(const char *url, bool is_manifest, size_t max_bytes,
                            int timeout_ms, volatile int *cancel_flag,
                            struct update_http_result *res);

void update_http_result_free(struct update_http_result *res);

/* Checks free bytes on the target filesystem root (e.g., "game:\\"). */
bool update_disk_free_bytes(const char *root_dir, uint64_t *out_free_bytes);

/* Downloads an asset in bounded chunks directly to dest_part_path, verifying
 * Content-Length/Content-Range, max_bytes, and reporting progress + speed. */
enum update_http_err update_http_download_file(const char *url,
                                               const char *dest_part_path,
                                               uint64_t expected_size,
                                               uint64_t max_size,
                                               int timeout_ms,
                                               volatile int *cancel_flag,
                                               update_dl_progress_fn progress_cb,
                                               void *progress_ctx);

#ifdef __cplusplus
}
#endif
