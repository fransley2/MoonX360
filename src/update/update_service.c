#include "update_service.h"
#include "update_crypto.h"
#include "update_http.h"
#include "update_journal.h"
#include "update_public_key.h"
#include "../platform.h"
#include "../version_info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_XBOX)
#include <xtl.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

#define UPD_CHECK_INTERVAL_MS (4u * 60u * 60u * 1000u)
#define UPD_HEADROOM_BYTES    (4u * 1024u * 1024u)
#define UPD_HTTP_TIMEOUT_MS   15000

static char g_root_dir[256];
static char g_pub_hex[68];
static plat_thread *g_worker;
static volatile int g_stop;
static volatile int g_cancel_net;
static volatile int g_streaming;
static volatile int g_healthy;
static volatile int g_req_check;
static volatile int g_req_download;
static volatile int g_channel;
static volatile int g_auto_check;
static volatile int g_channel_changed;
static uint32_t g_next_auto_ms;

static char *g_staged_raw;
static size_t g_staged_len;
static char g_staged_sig[132];
static struct update_manifest g_staged_manifest;
static bool g_has_offer;

#if defined(_XBOX) || defined(_WIN32)
static CRITICAL_SECTION g_lock;
static bool g_lock_ready;
static void lock_init(void) { if (!g_lock_ready) { InitializeCriticalSection(&g_lock); g_lock_ready = true; } }
static void lock_enter(void) { EnterCriticalSection(&g_lock); }
static void lock_leave(void) { LeaveCriticalSection(&g_lock); }
static void lock_free(void) { if (g_lock_ready) { DeleteCriticalSection(&g_lock); g_lock_ready = false; } }
#else
#include <pthread.h>
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static void lock_init(void) {}
static void lock_enter(void) { pthread_mutex_lock(&g_lock); }
static void lock_leave(void) { pthread_mutex_unlock(&g_lock); }
static void lock_free(void) {}
#endif

static struct update_status g_status;

static void svc_log(const char *msg, void *ctx)
{
    char log_path[320];
    FILE *f;
    (void)ctx;
    update_join_path(g_root_dir, "updates\\update.log", log_path, sizeof(log_path));
    f = fopen(log_path, "ab");
    if (f) {
        fprintf(f, "[%u ms] %s\n", (unsigned)plat_now_ms(), msg);
        fclose(f);
    }
    plat_log("[UPDATE] ");
    plat_log(msg);
    plat_log("\n");
}

static void publish_status(const struct update_status *next)
{
    lock_enter();
    uint32_t rev = g_status.revision + 1u;
    g_status = *next;
    g_status.revision = rev;
    lock_leave();
}

#ifdef MOONX_UPDATE_HOST_TEST
static char g_svc_test_base[256];
#endif

static void manifest_base_url(int channel, char *out, size_t cap)
{
    const char *ch = update_channel_name(channel);
#ifdef MOONX_UPDATE_HOST_TEST
    if (g_svc_test_base[0]) {
        snprintf(out, cap, "%s%s/", g_svc_test_base, ch);
        return;
    }
#endif
#ifdef MOONX_UPDATE_TEST_CONFIG
    snprintf(out, cap, "%s%s/", MOONX_UPDATE_TEST_BASE_URL, ch);
#else
    snprintf(out, cap, "https://raw.githubusercontent.com/fransley2/MoonX360/updates/%s/", ch);
#endif
}

static bool has_main_app_xex(void)
{
    char app_path[320];
    update_join_path(g_root_dir, UPD_REL_APP_XEX, app_path, sizeof(app_path));
    return update_check_xex2_header(app_path, NULL);
}

static void populate_from_manifest(struct update_status *st, const struct update_manifest *m)
{
    st->available_build = m->build_id;
    snprintf(st->available_version, sizeof(st->available_version), "%s", m->version);
    st->download_total = m->payload_size;
    snprintf(st->notes_en, sizeof(st->notes_en), "%s", m->notes_en);
    snprintf(st->notes_pt_br, sizeof(st->notes_pt_br), "%s", m->notes_pt_br);
    snprintf(st->notes_es, sizeof(st->notes_es), "%s", m->notes_es);
}

static void do_check(void)
{
    struct update_status st;
    struct update_journal j;
    int ch = plat_atomic_load(&g_channel);
    char base[384], m_url[512], s_url[512];
    struct update_http_result m_res, s_res;
    struct update_manifest m;

    if (plat_atomic_load(&g_streaming))
        return;

    lock_enter();
    st = g_status;
    lock_leave();

    st.channel = ch;
    st.auto_check = plat_atomic_load(&g_auto_check) != 0;
    st.has_previous = update_journal_has_previous(g_root_dir);
    st.migration_mode = !has_main_app_xex();

    if (st.migration_mode) {
        st.state = UPD_MIGRATION_REQUIRED;
        st.error = UPD_ERR_INCOMPATIBLE;
        snprintf(st.status_key, sizeof(st.status_key), "Manual launcher installation required.");
        publish_status(&st);
        return;
    }

    update_journal_load(&j, g_root_dir, g_pub_hex, MOONX_BUILD_ID, svc_log, NULL);
    if (!j.valid) {
        update_journal_free(&j);
        st.state = UPD_ERROR;
        st.error = UPD_ERR_VERIFICATION_FAILED;
        snprintf(st.status_key, sizeof(st.status_key), "Update state is corrupt.");
        publish_status(&st);
        return;
    }
    if (j.phase == UPD_PHASE_STAGED && j.manifest_bytes &&
        update_manifest_parse(j.manifest_bytes, j.manifest_len, &m)) {
        update_journal_free(&j);
        st.state = UPD_READY;
        st.error = UPD_ERR_NONE;
        populate_from_manifest(&st, &m);
        st.download_done = m.payload_size;
        st.progress_pct = 100;
        snprintf(st.status_key, sizeof(st.status_key), "Restart required.");
        publish_status(&st);
        return;
    }
    int high_water = j.highest_build;
    update_journal_free(&j);

    st.state = UPD_CHECKING;
    st.error = UPD_ERR_NONE;
    snprintf(st.status_key, sizeof(st.status_key), "Checking for updates.");
    publish_status(&st);

    manifest_base_url(ch, base, sizeof(base));
    snprintf(m_url, sizeof(m_url), "%smanifest.json", base);
    snprintf(s_url, sizeof(s_url), "%smanifest.json.sig", base);

    if (!update_http_get_buffer(m_url, true, UPDATE_MAX_MANIFEST_BYTES,
                                UPD_HTTP_TIMEOUT_MS, &g_cancel_net, &m_res)) {
        update_http_result_free(&m_res);
        if (plat_atomic_load(&g_cancel_net) || plat_atomic_load(&g_streaming)) {
            st.state = UPD_IDLE;
            st.status_key[0] = '\0';
            publish_status(&st);
            return;
        }
        st.state = UPD_ERROR;
        st.error = UPD_ERR_CHECK_FAILED;
        snprintf(st.status_key, sizeof(st.status_key), "Download failed.");
        {
            char emsg[128];
            const unsigned char *ipb = (const unsigned char *)&m_res.resolved_ip_be;
            snprintf(emsg, sizeof(emsg), "manifest fetch failed err=%d detail=%d ip=%u.%u.%u.%u verify=0x%08lX",
                     (int)m_res.err, m_res.detail_err,
                     (unsigned)ipb[0], (unsigned)ipb[1], (unsigned)ipb[2], (unsigned)ipb[3],
                     (unsigned long)m_res.verify_flags);
            svc_log(emsg, NULL);
        }
        publish_status(&st);
        return;
    }

    if (m_res.status != 200 || m_res.len == 0) {
        char msg[64];
        snprintf(msg, sizeof(msg), "manifest HTTP status %d", m_res.status);
        svc_log(msg, NULL);
        update_http_result_free(&m_res);
        st.state = UPD_ERROR;
        st.error = UPD_ERR_CHECK_FAILED;
        snprintf(st.status_key, sizeof(st.status_key), "Download failed.");
        publish_status(&st);
        return;
    }

    if (!update_http_get_buffer(s_url, true, 512,
                                UPD_HTTP_TIMEOUT_MS, &g_cancel_net, &s_res) ||
        s_res.status != 200 || s_res.len < 128) {
        update_http_result_free(&m_res);
        update_http_result_free(&s_res);
        st.state = UPD_ERROR;
        st.error = UPD_ERR_INVALID_SIGNATURE;
        snprintf(st.status_key, sizeof(st.status_key), "Invalid signature.");
        svc_log("signature fetch failed", NULL);
        publish_status(&st);
        return;
    }

    if (!update_ed25519_verify((const uint8_t *)m_res.body, m_res.len,
                               s_res.body, s_res.len, g_pub_hex)) {
        update_http_result_free(&m_res);
        update_http_result_free(&s_res);
        st.state = UPD_ERROR;
        st.error = UPD_ERR_INVALID_SIGNATURE;
        snprintf(st.status_key, sizeof(st.status_key), "Invalid signature.");
        svc_log("Ed25519 verification failed", NULL);
        publish_status(&st);
        return;
    }

    if (!update_manifest_parse(m_res.body, m_res.len, &m) ||
        strcmp(m.channel, update_channel_name(ch)) != 0) {
        update_http_result_free(&m_res);
        update_http_result_free(&s_res);
        st.state = UPD_ERROR;
        st.error = UPD_ERR_INCOMPATIBLE;
        snprintf(st.status_key, sizeof(st.status_key), "Invalid signature.");
        svc_log("manifest schema or channel mismatch", NULL);
        publish_status(&st);
        return;
    }

    if (m.min_launcher_version > MOONX_LAUNCHER_VERSION) {
        populate_from_manifest(&st, &m);
        update_http_result_free(&m_res);
        update_http_result_free(&s_res);
        st.state = UPD_LAUNCHER_REQUIRED;
        st.error = UPD_ERR_INCOMPATIBLE;
        snprintf(st.status_key, sizeof(st.status_key), "Manual launcher installation required.");
        publish_status(&st);
        return;
    }

    if (m.build_id <= high_water) {
        update_http_result_free(&m_res);
        update_http_result_free(&s_res);
        g_has_offer = false;
        st.state = UPD_UP_TO_DATE;
        st.error = UPD_ERR_NONE;
        st.available_build = 0;
        st.available_version[0] = '\0';
        snprintf(st.status_key, sizeof(st.status_key), "You're up to date.");
        svc_log("up to date", NULL);
        publish_status(&st);
        return;
    }

    free(g_staged_raw);
    g_staged_raw = m_res.body;
    g_staged_len = m_res.len;
    m_res.body = NULL;
    memcpy(g_staged_sig, s_res.body, 128);
    g_staged_sig[128] = '\0';
    g_staged_manifest = m;
    g_has_offer = true;
    update_http_result_free(&s_res);

    populate_from_manifest(&st, &m);
    st.state = UPD_AVAILABLE;
    st.error = UPD_ERR_NONE;
    st.download_done = 0;
    st.speed_bps = 0;
    st.progress_pct = 0;
    snprintf(st.status_key, sizeof(st.status_key), "Update available.");
    {
        char logmsg[128];
        snprintf(logmsg, sizeof(logmsg), "available %s build=%d size=%llu",
                 m.version, m.build_id, (unsigned long long)m.payload_size);
        svc_log(logmsg, NULL);
    }
    publish_status(&st);
}

static void dl_progress_cb(uint64_t done, uint64_t total, uint32_t speed_bps, void *ctx)
{
    struct update_status *st = (struct update_status *)ctx;
    st->download_done = done;
    st->download_total = total;
    st->speed_bps = speed_bps;
    st->progress_pct = total > 0 ? (int)((done * 100u) / total) : 0;
    publish_status(st);
}

static void do_download(void)
{
    struct update_status st;
    uint64_t free_bytes = 0;
    char part_path[320], next_path[320], fail_path[320], err[64];
    char sha[65];
    uint64_t actual_size = 0;
    enum update_http_err herr;

    if (!g_has_offer || !g_staged_raw || plat_atomic_load(&g_streaming))
        return;
    if (!has_main_app_xex())
        return;

    lock_enter();
    st = g_status;
    lock_leave();

    if (update_disk_free_bytes(g_root_dir, &free_bytes) &&
        free_bytes < g_staged_manifest.payload_size + UPD_HEADROOM_BYTES) {
        st.state = UPD_ERROR;
        st.error = UPD_ERR_INSUFFICIENT_STORAGE;
        snprintf(st.status_key, sizeof(st.status_key), "Insufficient storage.");
        svc_log("insufficient disk space", NULL);
        publish_status(&st);
        return;
    }

    {
        char upd_dir[320], rt_dir[320];
        update_join_path(g_root_dir, "updates", upd_dir, sizeof(upd_dir));
        update_join_path(g_root_dir, UPD_REL_RUNTIME_DIR, rt_dir, sizeof(rt_dir));
#if defined(_XBOX) || defined(_WIN32)
        CreateDirectoryA(upd_dir, NULL);
        CreateDirectoryA(rt_dir, NULL);
#else
        mkdir(upd_dir, 0755);
        mkdir(rt_dir, 0755);
#endif
    }
    update_join_path(g_root_dir, UPD_REL_FAIL_BIN, fail_path, sizeof(fail_path));
    update_join_path(g_root_dir, UPD_REL_NEXT_PART, part_path, sizeof(part_path));
    update_join_path(g_root_dir, UPD_REL_NEXT_BIN, next_path, sizeof(next_path));
    remove(fail_path);
    remove(part_path);

    st.state = UPD_DOWNLOADING;
    st.error = UPD_ERR_NONE;
    st.download_done = 0;
    st.speed_bps = 0;
    st.progress_pct = 0;
    snprintf(st.status_key, sizeof(st.status_key), "Downloading.");
    publish_status(&st);

    herr = update_http_download_file(g_staged_manifest.url, part_path,
                                     g_staged_manifest.payload_size,
                                     UPDATE_MAX_XEX_BYTES,
                                     UPD_HTTP_TIMEOUT_MS,
                                     &g_cancel_net,
                                     dl_progress_cb, &st);
    if (herr != UPD_HTTP_OK) {
        remove(part_path);
        if (herr == UPD_HTTP_CANCELLED || plat_atomic_load(&g_cancel_net)) {
            st.state = UPD_AVAILABLE;
            st.error = UPD_ERR_NONE;
            st.download_done = 0;
            st.speed_bps = 0;
            st.progress_pct = 0;
            snprintf(st.status_key, sizeof(st.status_key), "Update available.");
            svc_log("download cancelled", NULL);
        } else {
            st.state = UPD_ERROR;
            st.error = (herr == UPD_HTTP_DISK_SPACE) ? UPD_ERR_INSUFFICIENT_STORAGE : UPD_ERR_DOWNLOAD_FAILED;
            snprintf(st.status_key, sizeof(st.status_key),
                     st.error == UPD_ERR_INSUFFICIENT_STORAGE ? "Insufficient storage." : "Download failed.");
            svc_log("download failed", NULL);
        }
        publish_status(&st);
        return;
    }

    st.state = UPD_VERIFYING;
    st.progress_pct = 100;
    st.speed_bps = 0;
    snprintf(st.status_key, sizeof(st.status_key), "Verifying update.");
    publish_status(&st);

    if (!update_check_xex2_header(part_path, &actual_size) ||
        actual_size != g_staged_manifest.payload_size ||
        !update_sha256_file(part_path, sha, NULL, NULL, NULL, &g_cancel_net) ||
        strcmp(sha, g_staged_manifest.sha256) != 0) {
        remove(part_path);
        st.state = UPD_ERROR;
        st.error = UPD_ERR_VERIFICATION_FAILED;
        snprintf(st.status_key, sizeof(st.status_key), "Download failed.");
        svc_log("payload verification failed", NULL);
        publish_status(&st);
        return;
    }

    remove(next_path);
    if (rename(part_path, next_path) != 0 ||
        !update_journal_stage(g_root_dir, g_pub_hex,
                              MOONX_BUILD_ID, MOONX_VERSION, MOONX_LAUNCHER_VERSION,
                              g_staged_raw, g_staged_len,
                              g_staged_sig, strlen(g_staged_sig),
                              err, sizeof(err), svc_log, NULL)) {
        remove(part_path);
        remove(next_path);
        st.state = UPD_ERROR;
        st.error = (strcmp(err, "signature") == 0) ? UPD_ERR_INVALID_SIGNATURE : UPD_ERR_VERIFICATION_FAILED;
        snprintf(st.status_key, sizeof(st.status_key),
                 st.error == UPD_ERR_INVALID_SIGNATURE ? "Invalid signature." : "Download failed.");
        svc_log("staging failed", NULL);
        publish_status(&st);
        return;
    }

    st.state = UPD_READY;
    st.error = UPD_ERR_NONE;
    snprintf(st.status_key, sizeof(st.status_key), "Restart required.");
    svc_log("update staged and ready", NULL);
    publish_status(&st);
}

static void *worker_main(void *arg)
{
    (void)arg;
#ifdef _XBOX
    XSetThreadProcessor(GetCurrentThread(), 0);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
    bool boot_confirmed = false;
    while (!plat_atomic_load(&g_stop)) {
        if (!boot_confirmed) {
            if (plat_atomic_load(&g_healthy)) {
#if defined(MOONX_UPDATE_TEST_CONFIG) && defined(MOONX_UPDATE_TEST_FAIL_BOOT)
                svc_log("TEST boot confirmation withheld", NULL);
#else
                bool now_confirmed = false;
                if (update_journal_confirm_boot(g_root_dir, g_pub_hex,
                                                MOONX_BUILD_ID, MOONX_VERSION,
                                                &now_confirmed, svc_log, NULL)) {
                    if (now_confirmed)
                        svc_log("boot confirmed good", NULL);
                }
#endif
                if (plat_atomic_load(&g_auto_check))
                    plat_atomic_store(&g_req_check, 1);
                g_next_auto_ms = plat_now_ms() + UPD_CHECK_INTERVAL_MS;
                boot_confirmed = true;
            } else {
                plat_sleep_ms(100);
                continue;
            }
        }

        if (plat_atomic_load(&g_streaming)) {
            plat_sleep_ms(200);
            continue;
        }

        if (InterlockedCompareExchange((volatile LONG *)&g_channel_changed, 0, 1) == 1) {
            g_has_offer = false;
        }

        if (InterlockedCompareExchange((volatile LONG *)&g_req_download, 0, 1) == 1) {
            plat_atomic_store(&g_cancel_net, 0);
            do_download();
        } else if (InterlockedCompareExchange((volatile LONG *)&g_req_check, 0, 1) == 1 ||
                   (plat_atomic_load(&g_auto_check) &&
                    (int32_t)(plat_now_ms() - g_next_auto_ms) >= 0)) {
            plat_atomic_store(&g_cancel_net, 0);
            do_check();
            g_next_auto_ms = plat_now_ms() + UPD_CHECK_INTERVAL_MS;
        }

        plat_sleep_ms(100);
    }
    return NULL;
}

bool update_service_init(const char *root_dir, int channel, bool auto_check)
{
    struct update_journal j;
    struct update_manifest m;

    lock_init();
    snprintf(g_root_dir, sizeof(g_root_dir), "%s", root_dir ? root_dir : "game:\\");
    snprintf(g_pub_hex, sizeof(g_pub_hex), "%s", MOONX_UPDATE_PUBLIC_KEY_HEX);
    plat_atomic_store(&g_stop, 0);
    plat_atomic_store(&g_cancel_net, 0);
    plat_atomic_store(&g_streaming, 0);
    plat_atomic_store(&g_healthy, 0);
    plat_atomic_store(&g_req_check, 0);
    plat_atomic_store(&g_req_download, 0);
    plat_atomic_store(&g_channel, channel == UPDATE_CHANNEL_BETA ? UPDATE_CHANNEL_BETA : UPDATE_CHANNEL_STABLE);
    plat_atomic_store(&g_auto_check, auto_check ? 1 : 0);

    update_http_global_init();

    memset(&g_status, 0, sizeof(g_status));
    g_status.channel = plat_atomic_load(&g_channel);
    g_status.auto_check = auto_check;
    g_status.installed_build = MOONX_BUILD_ID;
    snprintf(g_status.installed_version, sizeof(g_status.installed_version), "%s", MOONX_VERSION);
    g_status.has_previous = update_journal_has_previous(g_root_dir);
    g_status.migration_mode = !has_main_app_xex();

    if (update_journal_load(&j, g_root_dir, g_pub_hex, MOONX_BUILD_ID, svc_log, NULL) && j.valid) {
        if (j.phase == UPD_PHASE_TRIAL) {
            char ini_src[320], ini_bak[320];
            update_join_path(g_root_dir, "moonlight\\moonlight.ini", ini_src, sizeof(ini_src));
            update_join_path(g_root_dir, "updates\\moonlight.ini.pre-update", ini_bak, sizeof(ini_bak));
            FILE *f_check = fopen(ini_bak, "rb");
            if (f_check) {
                fclose(f_check);
            } else {
                FILE *src = fopen(ini_src, "rb");
                FILE *dst = src ? fopen(ini_bak, "wb") : NULL;
                if (src && dst) {
                    char buf[1024];
                    size_t nr;
                    while ((nr = fread(buf, 1, sizeof(buf), src)) > 0)
                        fwrite(buf, 1, nr, dst);
                    fflush(dst);
                }
                if (src) fclose(src);
                if (dst) fclose(dst);
            }
        }
        if (j.restored_notice) {
            g_status.restored_notice = true;
            snprintf(g_status.status_key, sizeof(g_status.status_key), "Previous version restored.");
        }
        if (j.phase == UPD_PHASE_STAGED && j.manifest_bytes &&
            update_manifest_parse(j.manifest_bytes, j.manifest_len, &m)) {
            g_status.state = UPD_READY;
            populate_from_manifest(&g_status, &m);
            g_status.download_done = m.payload_size;
            g_status.progress_pct = 100;
            snprintf(g_status.status_key, sizeof(g_status.status_key), "Restart required.");
        } else if (j.phase == UPD_PHASE_MANUAL_ROLLBACK) {
            g_status.rollback_pending = true;
            g_status.state = UPD_READY;
            snprintf(g_status.status_key, sizeof(g_status.status_key), "Restart required.");
        }
    }
    update_journal_free(&j);
    if (g_status.migration_mode) {
        g_status.state = UPD_MIGRATION_REQUIRED;
        snprintf(g_status.status_key, sizeof(g_status.status_key), "Manual launcher installation required.");
    }
    g_status.revision = 1u;

    g_worker = plat_thread_start(worker_main, NULL);
    return g_worker != NULL;
}

void update_service_shutdown(void)
{
    plat_atomic_store(&g_stop, 1);
    plat_atomic_store(&g_cancel_net, 1);
    if (g_worker) {
        plat_thread_join(g_worker);
        g_worker = NULL;
    }
    free(g_staged_raw);
    g_staged_raw = NULL;
    g_staged_len = 0;
    g_has_offer = false;
    update_http_global_shutdown();
    lock_free();
}

bool update_service_poll(struct update_status *out, uint32_t *last_rev)
{
    bool changed = false;
    if (!out || !g_lock_ready)
        return false;
    lock_enter();
    if (!last_rev || g_status.revision != *last_rev) {
        *out = g_status;
        if (last_rev)
            *last_rev = g_status.revision;
        changed = true;
    }
    lock_leave();
    return changed;
}

void update_service_get_status(struct update_status *out)
{
    if (!out || !g_lock_ready)
        return;
    lock_enter();
    *out = g_status;
    lock_leave();
}

void update_service_notify_healthy(void)
{
    plat_atomic_store(&g_healthy, 1);
}

void update_service_set_streaming(bool active)
{
    plat_atomic_store(&g_streaming, active ? 1 : 0);
    if (active)
        plat_atomic_store(&g_cancel_net, 1);
}

void update_service_set_channel(int channel)
{
    int norm = (channel == UPDATE_CHANNEL_BETA) ? UPDATE_CHANNEL_BETA : UPDATE_CHANNEL_STABLE;
    if (plat_atomic_load(&g_channel) != norm) {
        plat_atomic_store(&g_channel, norm);
        plat_atomic_store(&g_channel_changed, 1);
        lock_enter();
        g_status.channel = norm;
        if (g_status.state == UPD_AVAILABLE || g_status.state == UPD_UP_TO_DATE || g_status.state == UPD_ERROR) {
            g_status.state = UPD_IDLE;
            g_status.available_build = 0;
            g_status.available_version[0] = '\0';
            g_status.status_key[0] = '\0';
        }
        g_status.revision++;
        lock_leave();
    }
}

void update_service_set_auto_check(bool auto_check)
{
    plat_atomic_store(&g_auto_check, auto_check ? 1 : 0);
    lock_enter();
    g_status.auto_check = auto_check;
    g_status.revision++;
    lock_leave();
}

void update_service_check_now(void)
{
    if (plat_atomic_load(&g_streaming))
        return;
    plat_atomic_store(&g_req_check, 1);
}

void update_service_start_download(void)
{
    if (plat_atomic_load(&g_streaming))
        return;
    plat_atomic_store(&g_req_download, 1);
}

void update_service_cancel_download(void)
{
    plat_atomic_store(&g_cancel_net, 1);
}

bool update_service_request_rollback(char *err_out, size_t err_cap)
{
    if (!update_journal_request_manual_rollback(g_root_dir, g_pub_hex,
                                                MOONX_BUILD_ID, err_out, err_cap,
                                                svc_log, NULL)) {
        return false;
    }
    lock_enter();
    g_status.rollback_pending = true;
    g_status.state = UPD_READY;
    snprintf(g_status.status_key, sizeof(g_status.status_key), "Restart required.");
    g_status.revision++;
    lock_leave();
    return true;
}

void update_service_clear_restored_notice(void)
{
    update_journal_consume_restored_notice(g_root_dir, g_pub_hex, MOONX_BUILD_ID, svc_log, NULL);
    lock_enter();
    g_status.restored_notice = false;
    g_status.revision++;
    lock_leave();
}

#ifdef MOONX_UPDATE_HOST_TEST
void update_service_test_set_base_url(const char *base_url)
{
    if (!base_url) {
        g_svc_test_base[0] = '\0';
        update_test_set_base_url(NULL);
        return;
    }
    snprintf(g_svc_test_base, sizeof(g_svc_test_base), "%s", base_url);
    update_test_set_base_url(base_url);
}

void update_service_test_set_public_key(const char *pub_hex)
{
    if (pub_hex)
        snprintf(g_pub_hex, sizeof(g_pub_hex), "%s", pub_hex);
}

void update_service_test_force_cooldown_elapsed(void)
{
    g_next_auto_ms = plat_now_ms() - 10u;
}
#endif
