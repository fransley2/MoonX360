#pragma once
#include "update_manifest.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum update_state {
    UPD_IDLE = 0,
    UPD_CHECKING,
    UPD_UP_TO_DATE,
    UPD_AVAILABLE,
    UPD_DOWNLOADING,
    UPD_VERIFYING,
    UPD_READY,
    UPD_LAUNCHER_REQUIRED,
    UPD_MIGRATION_REQUIRED,
    UPD_ERROR
};

enum update_error {
    UPD_ERR_NONE = 0,
    UPD_ERR_CHECK_FAILED,
    UPD_ERR_DOWNLOAD_FAILED,
    UPD_ERR_INVALID_SIGNATURE,
    UPD_ERR_INSUFFICIENT_STORAGE,
    UPD_ERR_VERIFICATION_FAILED,
    UPD_ERR_INCOMPATIBLE
};

struct update_status {
    enum update_state state;
    enum update_error error;
    uint32_t revision;
    int channel;
    bool auto_check;
    bool has_previous;
    bool restored_notice;
    bool rollback_pending;
    bool migration_mode;
    int installed_build;
    char installed_version[48];
    int available_build;
    char available_version[48];
    uint64_t download_total;
    uint64_t download_done;
    uint32_t speed_bps;
    int progress_pct;
    char notes_en[UPDATE_MAX_NOTE_BYTES + 1];
    char notes_pt_br[UPDATE_MAX_NOTE_BYTES + 1];
    char notes_es[UPDATE_MAX_NOTE_BYTES + 1];
    char status_key[64];
};

bool update_service_init(const char *root_dir, int channel, bool auto_check);
void update_service_shutdown(void);

/* Non-blocking snapshot for UI thread; returns true if status changed since *last_rev. */
bool update_service_poll(struct update_status *out, uint32_t *last_rev);
void update_service_get_status(struct update_status *out);

/* Called from main loop once config, graphics, UI/fonts, input, and stability timer pass. */
void update_service_notify_healthy(void);

/* Suspends any active update network activity while streaming. */
void update_service_set_streaming(bool active);

void update_service_set_channel(int channel);
void update_service_set_auto_check(bool auto_check);

void update_service_check_now(void);
void update_service_start_download(void);
void update_service_cancel_download(void);
bool update_service_request_rollback(char *err_out, size_t err_cap);
void update_service_clear_restored_notice(void);

#ifdef MOONX_UPDATE_HOST_TEST
/* Direct synchronous helpers and test overrides for deterministic host tests. */
void update_service_test_set_base_url(const char *base_url);
void update_service_test_set_public_key(const char *pub_hex);
void update_service_test_force_cooldown_elapsed(void);
#endif

#ifdef __cplusplus
}
#endif
