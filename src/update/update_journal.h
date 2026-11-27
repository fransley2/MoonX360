#pragma once
#include "update_manifest.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UPD_REL_RUNTIME_DIR   "updates\\runtime"
#define UPD_REL_APP_XEX       "updates\\runtime\\moonx360.xex"
#define UPD_REL_NEXT_BIN      "updates\\runtime\\moonx360.next.bin"
#define UPD_REL_NEXT_PART     "updates\\runtime\\moonx360.next.part"
#define UPD_REL_EXTRACT_PART  "updates\\runtime\\moonx360.extract.part"
#define UPD_REL_PREV_BIN      "updates\\runtime\\moonx360.previous.bin"
#define UPD_REL_FAIL_BIN      "updates\\runtime\\moonx360.failed.bin"
#define UPD_REL_ROOT_PATH     "updates\\runtime\\root.path"

struct update_embedded_payload {
    int build_id;
    const char *version;
    uint64_t size;
    const char *sha256_hex;
    const uint8_t *data;
    size_t data_len;
};

enum update_phase {
    UPD_PHASE_IDLE = 0,
    UPD_PHASE_GOOD,
    UPD_PHASE_STAGED,
    UPD_PHASE_SWAPPING,
    UPD_PHASE_TRIAL,
    UPD_PHASE_ROLLBACK,
    UPD_PHASE_MANUAL_ROLLBACK
};

struct update_journal {
    bool valid;
    bool existed;
    int schema;
    enum update_phase phase;
    int highest_build;
    int active_build;
    char active_version[48];
    int previous_build;
    char previous_version[48];
    int restored_notice;
    char *manifest_bytes;
    size_t manifest_len;
    char manifest_sig[132];
};

typedef void (*update_trace_fn)(const char *msg, void *ctx);

void update_join_path(const char *root_dir, const char *rel_path, char *out, size_t cap);
void update_journal_init(struct update_journal *j);
void update_journal_free(struct update_journal *j);

bool update_journal_load(struct update_journal *j, const char *root_dir,
                         const char *pub_hex, int running_build,
                         update_trace_fn trace, void *ctx);

bool update_journal_save(const struct update_journal *j, const char *root_dir,
                         update_trace_fn trace, void *ctx);

/* Stages a downloaded & verified moonx360.next.xex candidate. */
bool update_journal_stage(const char *root_dir, const char *pub_hex,
                          int running_build, const char *running_version,
                          int launcher_version,
                          const char *manifest_bytes, size_t manifest_len,
                          const char *sig_hex, size_t sig_len,
                          char *err_out, size_t err_cap,
                          update_trace_fn trace, void *ctx);

/* Called ONLY by the permanent launcher before launching moonx360.xex.
 * Resolves staged/swapping/trial/rollback/recover.flag transitions and outputs
 * the executable path to launch into launch_path_out. */
bool update_journal_prepare_launch(const char *root_dir, const char *pub_hex,
                                   int launcher_version,
                                   char *launch_path_out, size_t launch_cap,
                                   bool *out_rolled_back,
                                   char *err_out, size_t err_cap,
                                   update_trace_fn trace, void *ctx);

/* Called by launcher if launching the trial executable returned/failed. */
bool update_journal_launch_failed(const char *root_dir, const char *pub_hex,
                                  char *fallback_path_out, size_t fallback_cap,
                                  char *err_out, size_t err_cap,
                                  update_trace_fn trace, void *ctx);

/* Called by moonx360.xex after healthy startup criteria pass. */
bool update_journal_confirm_boot(const char *root_dir, const char *pub_hex,
                                 int running_build, const char *running_version,
                                 bool *out_confirmed_now,
                                 update_trace_fn trace, void *ctx);

/* Schedules controlled rollback to moonx360.previous.xex on next launcher run. */
bool update_journal_request_manual_rollback(const char *root_dir, const char *pub_hex,
                                            int running_build,
                                            char *err_out, size_t err_cap,
                                            update_trace_fn trace, void *ctx);

/* Returns true if the verified previous executable backup exists and starts with XEX2. */
bool update_journal_has_previous(const char *root_dir);

/* Extracts and verifies the embedded moonx360.xex payload into updates\runtime\moonx360.xex
 * on first boot, when runtime is missing/invalid, when embedded build is newer than installed,
 * or when force_recover is true. Never overwrites a newer installed/trial/staged build. */
bool update_journal_ensure_embedded_runtime(const char *root_dir, const char *pub_hex,
                                            const struct update_embedded_payload *emb,
                                            bool force_recover,
                                            bool *out_extracted,
                                            char *err_out, size_t err_cap,
                                            update_trace_fn trace, void *ctx);

/* Consumes the one-shot restored_notice flag after rollback. */
bool update_journal_consume_restored_notice(const char *root_dir, const char *pub_hex,
                                            int running_build,
                                            update_trace_fn trace, void *ctx);

const char *update_phase_name(enum update_phase phase);

#ifdef __cplusplus
}
#endif
