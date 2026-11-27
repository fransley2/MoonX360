#include "update_journal.h"
#include "update_crypto.h"
#include "../version_info.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _XBOX
#include <xtl.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <sys/stat.h>
#endif

static void emit_trace(update_trace_fn fn, void *ctx, const char *msg)
{
    if (fn && msg)
        fn(msg, ctx);
}

static bool file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    fclose(f);
    return true;
}

static bool ensure_dir(const char *path)
{
#if defined(_XBOX) || defined(_WIN32)
    if (CreateDirectoryA(path, NULL))
        return true;
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat st;
    if (mkdir(path, 0755) == 0)
        return true;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static bool fs_rename(const char *src, const char *dst)
{
#if defined(_XBOX) || defined(_WIN32)
    return MoveFileA(src, dst) != 0;
#else
    return rename(src, dst) == 0;
#endif
}

static bool fs_remove(const char *path)
{
    if (!file_exists(path))
        return true;
#if defined(_XBOX) || defined(_WIN32)
    return DeleteFileA(path) != 0;
#else
    return remove(path) == 0;
#endif
}

void update_join_path(const char *root_dir, const char *rel_path, char *out, size_t cap)
{
    size_t rlen;
    if (!out || cap == 0)
        return;
    if (!root_dir || !*root_dir) {
        snprintf(out, cap, "%s", rel_path ? rel_path : "");
        return;
    }
    rlen = strlen(root_dir);
    if (root_dir[rlen - 1] == '\\' || root_dir[rlen - 1] == '/')
        snprintf(out, cap, "%s%s", root_dir, rel_path ? rel_path : "");
    else
        snprintf(out, cap, "%s\\%s", root_dir, rel_path ? rel_path : "");
}

const char *update_phase_name(enum update_phase phase)
{
    switch (phase) {
    case UPD_PHASE_IDLE:            return "idle";
    case UPD_PHASE_GOOD:            return "good";
    case UPD_PHASE_STAGED:          return "staged";
    case UPD_PHASE_SWAPPING:        return "swapping";
    case UPD_PHASE_TRIAL:           return "trial";
    case UPD_PHASE_ROLLBACK:        return "rollback";
    case UPD_PHASE_MANUAL_ROLLBACK: return "manual_rollback";
    default:                        return "unknown";
    }
}

static int phase_from_name(const char *name)
{
    if (!name) return -1;
    if (strcmp(name, "idle") == 0) return UPD_PHASE_IDLE;
    if (strcmp(name, "good") == 0) return UPD_PHASE_GOOD;
    if (strcmp(name, "staged") == 0) return UPD_PHASE_STAGED;
    if (strcmp(name, "swapping") == 0) return UPD_PHASE_SWAPPING;
    if (strcmp(name, "trial") == 0) return UPD_PHASE_TRIAL;
    if (strcmp(name, "rollback") == 0) return UPD_PHASE_ROLLBACK;
    if (strcmp(name, "manual_rollback") == 0) return UPD_PHASE_MANUAL_ROLLBACK;
    return -1;
}

void update_journal_init(struct update_journal *j)
{
    memset(j, 0, sizeof(*j));
    j->valid = true;
    j->schema = 1;
    j->phase = UPD_PHASE_IDLE;
    j->highest_build = MOONX_BUILD_ID;
    j->active_build = MOONX_BUILD_ID;
    snprintf(j->active_version, sizeof(j->active_version), "%s", MOONX_VERSION);
}

void update_journal_free(struct update_journal *j)
{
    if (!j)
        return;
    free(j->manifest_bytes);
    j->manifest_bytes = NULL;
    j->manifest_len = 0;
}

static char *read_file_bounded(const char *path, size_t max_bytes, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    long size;
    char *buf;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size <= 0 || (size_t)size > max_bytes || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = (char *)malloc((size_t)size + 1u);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)size, f) != (size_t)size || ferror(f)) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    buf[size] = '\0';
    if (out_len)
        *out_len = (size_t)size;
    return buf;
}

/* ---- Bounded JSON serializer/parser for updates/journal.json ---- */

struct jstr {
    const char *p;
    const char *end;
};

static void s_skip(struct jstr *c)
{
    while (c->p < c->end && (*c->p == ' ' || *c->p == '\t' || *c->p == '\r' || *c->p == '\n'))
        c->p++;
}

static bool s_ch(struct jstr *c, char ch)
{
    s_skip(c);
    if (c->p >= c->end || *c->p != ch)
        return false;
    c->p++;
    return true;
}

static bool s_read_str(struct jstr *c, char *out, size_t cap)
{
    size_t len = 0;
    if (!s_ch(c, '"'))
        return false;
    while (c->p < c->end) {
        unsigned char ch = (unsigned char)*c->p++;
        if (ch == '"') {
            out[len] = '\0';
            return true;
        }
        if (ch < 32 || ch == 127 || ch == '\\')
            return false;
        if (len + 1u >= cap)
            return false;
        out[len++] = (char)ch;
    }
    return false;
}

static bool s_read_int(struct jstr *c, int *out, int min_v, int max_v)
{
    long long v = 0;
    size_t digits = 0;
    s_skip(c);
    if (c->p >= c->end || !isdigit((unsigned char)*c->p))
        return false;
    while (c->p < c->end && isdigit((unsigned char)*c->p)) {
        v = v * 10 + (*c->p - '0');
        if (v > max_v)
            return false;
        c->p++;
        digits++;
    }
    if (digits == 0 || v < min_v)
        return false;
    *out = (int)v;
    return true;
}

static bool parse_journal_json(const char *raw, size_t raw_len, const char *pub_hex,
                               int running_build, struct update_journal *out)
{
    struct jstr c;
    char *manifest_hex = NULL;
    size_t manifest_hex_cap = UPDATE_MAX_MANIFEST_BYTES * 2u + 4u;
    unsigned seen = 0;
    char phase_str[32] = {0};

    update_journal_init(out);
    out->existed = true;
    out->valid = false;
    manifest_hex = (char *)malloc(manifest_hex_cap);
    if (!manifest_hex)
        return false;
    manifest_hex[0] = '\0';

    c.p = raw;
    c.end = raw + raw_len;
    if (!s_ch(&c, '{'))
        goto fail;

    for (;;) {
        char key[32];
        if (!s_read_str(&c, key, sizeof(key)) || !s_ch(&c, ':'))
            goto fail;
        if (strcmp(key, "schema") == 0) {
            if ((seen & (1u << 0)) || !s_read_int(&c, &out->schema, 1, 1)) goto fail;
            seen |= (1u << 0);
        } else if (strcmp(key, "phase") == 0) {
            int ph;
            if ((seen & (1u << 1)) || !s_read_str(&c, phase_str, sizeof(phase_str))) goto fail;
            ph = phase_from_name(phase_str);
            if (ph < 0) goto fail;
            out->phase = (enum update_phase)ph;
            seen |= (1u << 1);
        } else if (strcmp(key, "highest_build") == 0) {
            if ((seen & (1u << 2)) || !s_read_int(&c, &out->highest_build, 1, INT_MAX)) goto fail;
            seen |= (1u << 2);
        } else if (strcmp(key, "active_build") == 0) {
            if ((seen & (1u << 3)) || !s_read_int(&c, &out->active_build, 0, INT_MAX)) goto fail;
            seen |= (1u << 3);
        } else if (strcmp(key, "active_version") == 0) {
            if ((seen & (1u << 4)) || !s_read_str(&c, out->active_version, sizeof(out->active_version))) goto fail;
            seen |= (1u << 4);
        } else if (strcmp(key, "previous_build") == 0) {
            if ((seen & (1u << 5)) || !s_read_int(&c, &out->previous_build, 0, INT_MAX)) goto fail;
            seen |= (1u << 5);
        } else if (strcmp(key, "previous_version") == 0) {
            if ((seen & (1u << 6)) || !s_read_str(&c, out->previous_version, sizeof(out->previous_version))) goto fail;
            seen |= (1u << 6);
        } else if (strcmp(key, "restored_notice") == 0) {
            if ((seen & (1u << 7)) || !s_read_int(&c, &out->restored_notice, 0, 1)) goto fail;
            seen |= (1u << 7);
        } else if (strcmp(key, "manifest_hex") == 0) {
            if ((seen & (1u << 8)) || !s_read_str(&c, manifest_hex, manifest_hex_cap)) goto fail;
            seen |= (1u << 8);
        } else if (strcmp(key, "manifest_sig") == 0) {
            if ((seen & (1u << 9)) || !s_read_str(&c, out->manifest_sig, sizeof(out->manifest_sig))) goto fail;
            seen |= (1u << 9);
        } else {
            goto fail;
        }

        s_skip(&c);
        if (c.p < c.end && *c.p == ',') {
            c.p++;
            continue;
        }
        if (s_ch(&c, '}'))
            break;
        goto fail;
    }
    s_skip(&c);
    if (c.p != c.end || seen != ((1u << 10) - 1u))
        goto fail;

    if (manifest_hex[0] != '\0') {
        size_t hex_len = strlen(manifest_hex);
        size_t bin_len = hex_len / 2u;
        struct update_manifest m;
        if ((hex_len & 1u) != 0 || bin_len == 0 || bin_len > UPDATE_MAX_MANIFEST_BYTES)
            goto fail;
        out->manifest_bytes = (char *)malloc(bin_len + 1u);
        if (!out->manifest_bytes)
            goto fail;
        if (!update_hex_decode(manifest_hex, hex_len, bin_len,
                               (uint8_t *)out->manifest_bytes, false))
            goto fail;
        out->manifest_bytes[bin_len] = '\0';
        out->manifest_len = bin_len;

        if (!update_ed25519_verify((const uint8_t *)out->manifest_bytes, out->manifest_len,
                                   out->manifest_sig, strlen(out->manifest_sig), pub_hex))
            goto fail;
        if (!update_manifest_parse(out->manifest_bytes, out->manifest_len, &m))
            goto fail;
        if (out->highest_build < m.build_id)
            goto fail;
    } else if (out->phase == UPD_PHASE_STAGED || out->phase == UPD_PHASE_SWAPPING ||
               out->phase == UPD_PHASE_TRIAL) {
        goto fail;
    }

    if (running_build > out->highest_build)
        out->highest_build = running_build;
    out->valid = true;
    free(manifest_hex);
    return true;

fail:
    free(manifest_hex);
    update_journal_free(out);
    out->valid = false;
    out->existed = true;
    return false;
}

bool update_journal_load(struct update_journal *j, const char *root_dir,
                         const char *pub_hex, int running_build,
                         update_trace_fn trace, void *ctx)
{
    char path[320], bak[320];
    char *raw;
    size_t len = 0;

    update_journal_init(j);
    if (running_build > j->highest_build)
        j->highest_build = running_build;
    update_join_path(root_dir, "updates\\journal.json", path, sizeof(path));
    update_join_path(root_dir, "updates\\journal.json.bak", bak, sizeof(bak));

    if (!file_exists(path) && !file_exists(bak)) {
        emit_trace(trace, ctx, "journal absent: initial state");
        j->valid = true;
        j->existed = false;
        return true;
    }

    raw = read_file_bounded(path, UPDATE_MAX_MANIFEST_BYTES * 3u, &len);
    if (raw) {
        if (parse_journal_json(raw, len, pub_hex, running_build, j)) {
            free(raw);
            return true;
        }
        free(raw);
    }

    raw = read_file_bounded(bak, UPDATE_MAX_MANIFEST_BYTES * 3u, &len);
    if (raw) {
        if (parse_journal_json(raw, len, pub_hex, running_build, j)) {
            free(raw);
            emit_trace(trace, ctx, "journal recovered from .bak");
            return true;
        }
        free(raw);
    }

    update_journal_free(j);
    j->valid = false;
    j->existed = true;
    emit_trace(trace, ctx, "journal corrupt or signature invalid");
    return false;
}

static bool write_verified_file(const char *path, const char *data, size_t len)
{
    char tmp[340], bak[340];
    FILE *f;
    unsigned char buf[512];
    size_t off = 0;
    bool existed;

    if (strlen(path) > 320)
        return false;
    snprintf(tmp, sizeof(tmp), "%s.new", path);
    snprintf(bak, sizeof(bak), "%s.bak", path);

    f = fopen(tmp, "wb");
    if (!f)
        return false;
    if (fwrite(data, 1, len, f) != len || fflush(f) != 0) {
        fclose(f);
        fs_remove(tmp);
        return false;
    }
    if (fclose(f) != 0) {
        fs_remove(tmp);
        return false;
    }

    f = fopen(tmp, "rb");
    if (!f) {
        fs_remove(tmp);
        return false;
    }
    while (off < len) {
        size_t n = len - off;
        if (n > sizeof(buf))
            n = sizeof(buf);
        if (fread(buf, 1, n, f) != n || memcmp(buf, data + off, n) != 0) {
            fclose(f);
            fs_remove(tmp);
            return false;
        }
        off += n;
    }
    if (fgetc(f) != EOF || ferror(f)) {
        fclose(f);
        fs_remove(tmp);
        return false;
    }
    if (fclose(f) != 0) {
        fs_remove(tmp);
        return false;
    }

    existed = file_exists(path);
    if (existed) {
        fs_remove(bak);
        if (!fs_rename(path, bak)) {
            fs_remove(tmp);
            return false;
        }
    }
    if (!fs_rename(tmp, path)) {
        if (existed)
            fs_rename(bak, path);
        fs_remove(tmp);
        return false;
    }
    return true;
}

bool update_journal_save(const struct update_journal *j, const char *root_dir,
                         update_trace_fn trace, void *ctx)
{
    char upd_dir[320], path[320];
    char *manifest_hex = NULL;
    size_t hex_cap, json_cap;
    char *json_buf;
    int n;
    bool ok;

    if (!j || !j->valid)
        return false;
    update_join_path(root_dir, "updates", upd_dir, sizeof(upd_dir));
    if (!ensure_dir(upd_dir))
        return false;
    update_join_path(root_dir, "updates\\journal.json", path, sizeof(path));

    hex_cap = j->manifest_len * 2u + 2u;
    manifest_hex = (char *)malloc(hex_cap);
    if (!manifest_hex)
        return false;
    if (j->manifest_bytes && j->manifest_len > 0)
        update_hex_encode((const uint8_t *)j->manifest_bytes, j->manifest_len, manifest_hex);
    else
        manifest_hex[0] = '\0';

    json_cap = hex_cap + 1024u;
    json_buf = (char *)malloc(json_cap);
    if (!json_buf) {
        free(manifest_hex);
        return false;
    }
    n = snprintf(json_buf, json_cap,
        "{\n"
        "  \"schema\": 1,\n"
        "  \"phase\": \"%s\",\n"
        "  \"highest_build\": %d,\n"
        "  \"active_build\": %d,\n"
        "  \"active_version\": \"%s\",\n"
        "  \"previous_build\": %d,\n"
        "  \"previous_version\": \"%s\",\n"
        "  \"restored_notice\": %d,\n"
        "  \"manifest_hex\": \"%s\",\n"
        "  \"manifest_sig\": \"%s\"\n"
        "}\n",
        update_phase_name(j->phase),
        j->highest_build,
        j->active_build,
        j->active_version,
        j->previous_build,
        j->previous_version,
        j->restored_notice ? 1 : 0,
        manifest_hex,
        j->manifest_sig);
    free(manifest_hex);
    if (n <= 0 || (size_t)n >= json_cap) {
        free(json_buf);
        return false;
    }
    ok = write_verified_file(path, json_buf, (size_t)n);
    free(json_buf);
    if (trace) {
        char msg[128];
        snprintf(msg, sizeof(msg), "journal save phase=%s %s", update_phase_name(j->phase), ok ? "ok" : "failed");
        emit_trace(trace, ctx, msg);
    }
    return ok;
}

static bool verify_xex_matches_manifest(const char *path, const struct update_manifest *m)
{
    uint64_t size = 0;
    char sha[65];
    if (!update_check_xex2_header(path, &size) || size != m->payload_size)
        return false;
    if (!update_sha256_file(path, sha, NULL, NULL, NULL, NULL))
        return false;
    return strcmp(sha, m->sha256) == 0;
}

static bool ensure_runtime_dirs(const char *root_dir)
{
    char upd_dir[320], rt_dir[320];
    update_join_path(root_dir, "updates", upd_dir, sizeof(upd_dir));
    if (!ensure_dir(upd_dir))
        return false;
    update_join_path(root_dir, UPD_REL_RUNTIME_DIR, rt_dir, sizeof(rt_dir));
    return ensure_dir(rt_dir);
}

bool update_journal_stage(const char *root_dir, const char *pub_hex,
                          int running_build, const char *running_version,
                          int launcher_version,
                          const char *manifest_bytes, size_t manifest_len,
                          const char *sig_hex, size_t sig_len,
                          char *err_out, size_t err_cap,
                          update_trace_fn trace, void *ctx)
{
    struct update_journal j;
    struct update_manifest m;
    char next_path[320], app_path[320], sig_clean[132];
    size_t clean_len;

    update_journal_init(&j);
    update_join_path(root_dir, UPD_REL_APP_XEX, app_path, sizeof(app_path));
    update_join_path(root_dir, UPD_REL_NEXT_BIN, next_path, sizeof(next_path));

    if (!file_exists(app_path)) {
        snprintf(err_out, err_cap, "migration_required");
        return false;
    }
    if (!update_ed25519_verify((const uint8_t *)manifest_bytes, manifest_len,
                               sig_hex, sig_len, pub_hex)) {
        snprintf(err_out, err_cap, "signature");
        return false;
    }
    if (!update_manifest_parse(manifest_bytes, manifest_len, &m)) {
        snprintf(err_out, err_cap, "schema");
        return false;
    }
    if (m.min_launcher_version > launcher_version) {
        snprintf(err_out, err_cap, "launcher_required");
        return false;
    }
    if (!update_journal_load(&j, root_dir, pub_hex, running_build, trace, ctx) || !j.valid) {
        update_journal_free(&j);
        snprintf(err_out, err_cap, "journal");
        return false;
    }
    if (j.phase == UPD_PHASE_TRIAL || j.phase == UPD_PHASE_SWAPPING ||
        j.phase == UPD_PHASE_ROLLBACK || j.phase == UPD_PHASE_MANUAL_ROLLBACK) {
        update_journal_free(&j);
        snprintf(err_out, err_cap, "busy_phase");
        return false;
    }
    if (m.build_id <= j.highest_build) {
        update_journal_free(&j);
        snprintf(err_out, err_cap, "version");
        return false;
    }
    if (!verify_xex_matches_manifest(next_path, &m)) {
        update_journal_free(&j);
        snprintf(err_out, err_cap, "hash");
        return false;
    }

    clean_len = sig_len;
    while (clean_len > 0 && (sig_hex[clean_len - 1] == '\n' || sig_hex[clean_len - 1] == '\r' ||
                             sig_hex[clean_len - 1] == ' ' || sig_hex[clean_len - 1] == '\t')) {
        clean_len--;
    }
    if (clean_len != 128) {
        update_journal_free(&j);
        snprintf(err_out, err_cap, "signature");
        return false;
    }
    memcpy(sig_clean, sig_hex, 128);
    sig_clean[128] = '\0';

    free(j.manifest_bytes);
    j.manifest_bytes = (char *)malloc(manifest_len + 1u);
    if (!j.manifest_bytes) {
        update_journal_free(&j);
        snprintf(err_out, err_cap, "memory");
        return false;
    }
    memcpy(j.manifest_bytes, manifest_bytes, manifest_len);
    j.manifest_bytes[manifest_len] = '\0';
    j.manifest_len = manifest_len;
    snprintf(j.manifest_sig, sizeof(j.manifest_sig), "%s", sig_clean);
    j.phase = UPD_PHASE_STAGED;
    j.highest_build = m.build_id;
    j.active_build = running_build;
    snprintf(j.active_version, sizeof(j.active_version), "%s",
             running_version ? running_version : MOONX_VERSION);
    j.restored_notice = 0;

    if (!update_journal_save(&j, root_dir, trace, ctx)) {
        update_journal_free(&j);
        snprintf(err_out, err_cap, "disk");
        return false;
    }
    update_journal_free(&j);
    if (err_out && err_cap)
        err_out[0] = '\0';
    return true;
}

static bool perform_restore_files(const char *root_dir, struct update_journal *j,
                                  bool mark_notice, char *err_out, size_t err_cap,
                                  update_trace_fn trace, void *ctx)
{
    char app_path[320], prev_path[320], fail_path[320], next_path[320];
    update_join_path(root_dir, UPD_REL_APP_XEX, app_path, sizeof(app_path));
    update_join_path(root_dir, UPD_REL_PREV_BIN, prev_path, sizeof(prev_path));
    update_join_path(root_dir, UPD_REL_FAIL_BIN, fail_path, sizeof(fail_path));
    update_join_path(root_dir, UPD_REL_NEXT_BIN, next_path, sizeof(next_path));

    emit_trace(trace, ctx, "rollback begin");
    j->phase = UPD_PHASE_ROLLBACK;
    if (!update_journal_save(j, root_dir, trace, ctx)) {
        snprintf(err_out, err_cap, "journal");
        return false;
    }
    fs_remove(next_path);
    if (file_exists(prev_path) && update_check_xex2_header(prev_path, NULL)) {
        if (file_exists(app_path)) {
            if (!fs_remove(fail_path) || !fs_rename(app_path, fail_path)) {
                snprintf(err_out, err_cap, "move_failed");
                return false;
            }
        }
        if (!fs_rename(prev_path, app_path)) {
            snprintf(err_out, err_cap, "restore");
            return false;
        }
    }
    if (!update_check_xex2_header(app_path, NULL)) {
        snprintf(err_out, err_cap, "missing_backup");
        return false;
    }

    j->phase = UPD_PHASE_GOOD;
    if (j->previous_build > 0) {
        j->active_build = j->previous_build;
        snprintf(j->active_version, sizeof(j->active_version), "%s", j->previous_version);
    }
    j->previous_build = 0;
    j->previous_version[0] = '\0';
    j->restored_notice = mark_notice ? 1 : 0;
    {
        char ini_bak[320], ini_dst[320];
        update_join_path(root_dir, "updates\\moonlight.ini.pre-update", ini_bak, sizeof(ini_bak));
        update_join_path(root_dir, "moonlight\\moonlight.ini", ini_dst, sizeof(ini_dst));
        if (file_exists(ini_bak)) {
            FILE *src = fopen(ini_bak, "rb");
            FILE *dst = src ? fopen(ini_dst, "wb") : NULL;
            if (src && dst) {
                char buf[1024];
                size_t nr;
                while ((nr = fread(buf, 1, sizeof(buf), src)) > 0)
                    fwrite(buf, 1, nr, dst);
                fflush(dst);
                emit_trace(trace, ctx, "restored pre-update moonlight.ini");
            }
            if (src) fclose(src);
            if (dst) fclose(dst);
            fs_remove(ini_bak);
        }
    }
    if (!update_journal_save(j, root_dir, trace, ctx)) {
        snprintf(err_out, err_cap, "journal");
        return false;
    }
    emit_trace(trace, ctx, "rollback complete");
    return true;
}

bool update_journal_prepare_launch(const char *root_dir, const char *pub_hex,
                                   int launcher_version,
                                   char *launch_path_out, size_t launch_cap,
                                   bool *out_rolled_back,
                                   char *err_out, size_t err_cap,
                                   update_trace_fn trace, void *ctx)
{
    struct update_journal j;
    char app_path[320], next_path[320], prev_path[320], recover_path[320];
    bool recover_flagged;

    if (out_rolled_back)
        *out_rolled_back = false;
    if (err_out && err_cap)
        err_out[0] = '\0';

    update_join_path(root_dir, UPD_REL_APP_XEX, app_path, sizeof(app_path));
    update_join_path(root_dir, UPD_REL_NEXT_BIN, next_path, sizeof(next_path));
    update_join_path(root_dir, UPD_REL_PREV_BIN, prev_path, sizeof(prev_path));
    update_join_path(root_dir, "updates\\recover.flag", recover_path, sizeof(recover_path));

    recover_flagged = file_exists(recover_path);
    if (!update_journal_load(&j, root_dir, pub_hex, 1, trace, ctx) || !j.valid) {
        /* Corrupt journal: never promote .next. Prefer existing app or previous backup. */
        update_journal_free(&j);
        if (recover_flagged && update_check_xex2_header(prev_path, NULL)) {
            fs_remove(recover_path);
            snprintf(launch_path_out, launch_cap, "%s", prev_path);
            if (out_rolled_back)
                *out_rolled_back = true;
            return true;
        }
        if (update_check_xex2_header(app_path, NULL)) {
            snprintf(launch_path_out, launch_cap, "%s", app_path);
            return true;
        }
        if (update_check_xex2_header(prev_path, NULL)) {
            snprintf(launch_path_out, launch_cap, "%s", prev_path);
            if (out_rolled_back)
                *out_rolled_back = true;
            return true;
        }
        snprintf(err_out, err_cap, "corrupt_journal_no_app");
        return false;
    }

    if (recover_flagged) {
        fs_remove(recover_path);
        if (update_check_xex2_header(prev_path, NULL)) {
            bool ok = perform_restore_files(root_dir, &j, true, err_out, err_cap, trace, ctx);
            update_journal_free(&j);
            if (ok) {
                if (out_rolled_back) *out_rolled_back = true;
                snprintf(launch_path_out, launch_cap, "%s", app_path);
                return true;
            }
            return false;
        }
    }

    if (j.phase == UPD_PHASE_TRIAL || j.phase == UPD_PHASE_ROLLBACK ||
        j.phase == UPD_PHASE_MANUAL_ROLLBACK) {
        bool ok = perform_restore_files(root_dir, &j, true, err_out, err_cap, trace, ctx);
        update_journal_free(&j);
        if (ok) {
            if (out_rolled_back) *out_rolled_back = true;
            snprintf(launch_path_out, launch_cap, "%s", app_path);
            return true;
        }
        return false;
    }

    if (j.phase == UPD_PHASE_STAGED || j.phase == UPD_PHASE_SWAPPING) {
        struct update_manifest m;
        bool manifest_ok = j.manifest_bytes && j.manifest_len > 0 &&
            update_ed25519_verify((const uint8_t *)j.manifest_bytes, j.manifest_len,
                                  j.manifest_sig, strlen(j.manifest_sig), pub_hex) &&
            update_manifest_parse(j.manifest_bytes, j.manifest_len, &m) &&
            m.min_launcher_version <= launcher_version;

        if (!manifest_ok) {
            emit_trace(trace, ctx, "staged manifest invalid; keeping current executable");
            fs_remove(next_path);
            j.phase = UPD_PHASE_GOOD;
            update_journal_save(&j, root_dir, trace, ctx);
            update_journal_free(&j);
            if (update_check_xex2_header(app_path, NULL)) {
                snprintf(launch_path_out, launch_cap, "%s", app_path);
                return true;
            }
            snprintf(err_out, err_cap, "signature");
            return false;
        }

        if (j.phase == UPD_PHASE_STAGED) {
            if (!update_check_xex2_header(app_path, NULL) || !verify_xex_matches_manifest(next_path, &m)) {
                emit_trace(trace, ctx, "staged verification failed; keeping current executable");
                fs_remove(next_path);
                j.phase = UPD_PHASE_GOOD;
                update_journal_save(&j, root_dir, trace, ctx);
                update_journal_free(&j);
                if (update_check_xex2_header(app_path, NULL)) {
                    snprintf(launch_path_out, launch_cap, "%s", app_path);
                    return true;
                }
                snprintf(err_out, err_cap, "pending_invalid");
                return false;
            }
            if (!fs_remove(prev_path)) {
                update_journal_free(&j);
                snprintf(err_out, err_cap, "remove_previous");
                return false;
            }
            j.previous_build = j.active_build;
            snprintf(j.previous_version, sizeof(j.previous_version), "%s", j.active_version);
            j.phase = UPD_PHASE_SWAPPING;
            if (!update_journal_save(&j, root_dir, trace, ctx)) {
                update_journal_free(&j);
                snprintf(err_out, err_cap, "journal");
                return false;
            }
        }

        /* Recoverable swap transitions (resumes cleanly after interruption between renames). */
        if (file_exists(next_path)) {
            if (!verify_xex_matches_manifest(next_path, &m)) {
                bool ok = perform_restore_files(root_dir, &j, false, err_out, err_cap, trace, ctx);
                update_journal_free(&j);
                if (ok) {
                    snprintf(launch_path_out, launch_cap, "%s", app_path);
                    return true;
                }
                return false;
            }
            if (file_exists(app_path) && !file_exists(prev_path)) {
                emit_trace(trace, ctx, "rename moonx360.xex -> moonx360.previous.xex");
                if (!fs_rename(app_path, prev_path)) {
                    update_journal_free(&j);
                    snprintf(err_out, err_cap, "backup_rename");
                    return false;
                }
            } else if (file_exists(app_path) && file_exists(prev_path)) {
                /* Only possible if prior rename left ambiguous state; never overwrite prev. */
                if (!verify_xex_matches_manifest(app_path, &m)) {
                    bool ok = perform_restore_files(root_dir, &j, false, err_out, err_cap, trace, ctx);
                    update_journal_free(&j);
                    if (ok) {
                        snprintf(launch_path_out, launch_cap, "%s", app_path);
                        return true;
                    }
                    return false;
                }
            }
            if (!file_exists(app_path)) {
                emit_trace(trace, ctx, "rename moonx360.next.xex -> moonx360.xex");
                if (!fs_rename(next_path, app_path)) {
                    bool ok = perform_restore_files(root_dir, &j, false, err_out, err_cap, trace, ctx);
                    update_journal_free(&j);
                    if (ok) {
                        snprintf(launch_path_out, launch_cap, "%s", app_path);
                        return true;
                    }
                    return false;
                }
            } else {
                fs_remove(next_path);
            }
        }

        if (!file_exists(prev_path) || !verify_xex_matches_manifest(app_path, &m)) {
            bool ok = perform_restore_files(root_dir, &j, false, err_out, err_cap, trace, ctx);
            update_journal_free(&j);
            if (ok) {
                if (out_rolled_back) *out_rolled_back = true;
                snprintf(launch_path_out, launch_cap, "%s", app_path);
                return true;
            }
            return false;
        }

        j.phase = UPD_PHASE_TRIAL;
        j.active_build = m.build_id;
        snprintf(j.active_version, sizeof(j.active_version), "%s", m.version);
        j.restored_notice = 0;
        if (!update_journal_save(&j, root_dir, trace, ctx)) {
            bool ok = perform_restore_files(root_dir, &j, false, err_out, err_cap, trace, ctx);
            update_journal_free(&j);
            if (ok) {
                snprintf(launch_path_out, launch_cap, "%s", app_path);
                return true;
            }
            return false;
        }
        update_journal_free(&j);
        snprintf(launch_path_out, launch_cap, "%s", app_path);
        return true;
    }

    update_journal_free(&j);
    if (update_check_xex2_header(app_path, NULL)) {
        snprintf(launch_path_out, launch_cap, "%s", app_path);
        return true;
    }
    if (update_check_xex2_header(prev_path, NULL)) {
        emit_trace(trace, ctx, "main app missing/invalid; falling back to previous.xex");
        if (fs_rename(prev_path, app_path) && update_check_xex2_header(app_path, NULL)) {
            if (out_rolled_back) *out_rolled_back = true;
            snprintf(launch_path_out, launch_cap, "%s", app_path);
            return true;
        }
    }
    snprintf(err_out, err_cap, "missing_app");
    return false;
}

bool update_journal_launch_failed(const char *root_dir, const char *pub_hex,
                                  char *fallback_path_out, size_t fallback_cap,
                                  char *err_out, size_t err_cap,
                                  update_trace_fn trace, void *ctx)
{
    struct update_journal j;
    char app_path[320];
    update_join_path(root_dir, UPD_REL_APP_XEX, app_path, sizeof(app_path));
    if (!update_journal_load(&j, root_dir, pub_hex, 1, trace, ctx) || !j.valid ||
        j.phase != UPD_PHASE_TRIAL) {
        update_journal_free(&j);
        snprintf(err_out, err_cap, "no_trial");
        return false;
    }
    if (!perform_restore_files(root_dir, &j, true, err_out, err_cap, trace, ctx)) {
        update_journal_free(&j);
        return false;
    }
    update_journal_free(&j);
    snprintf(fallback_path_out, fallback_cap, "%s", app_path);
    return true;
}

bool update_journal_confirm_boot(const char *root_dir, const char *pub_hex,
                                 int running_build, const char *running_version,
                                 bool *out_confirmed_now,
                                 update_trace_fn trace, void *ctx)
{
    struct update_journal j;
    struct update_manifest m;
    char app_path[320], ini_bak[320];

    if (out_confirmed_now)
        *out_confirmed_now = false;

    if (!update_journal_load(&j, root_dir, pub_hex, running_build, trace, ctx) || !j.valid) {
        update_journal_free(&j);
        return false;
    }
    if (j.phase != UPD_PHASE_TRIAL) {
        update_journal_free(&j);
        return true;
    }
    if (!j.manifest_bytes || !update_manifest_parse(j.manifest_bytes, j.manifest_len, &m)) {
        update_journal_free(&j);
        return false;
    }
    if (m.build_id != running_build || !running_version || strcmp(m.version, running_version) != 0) {
        update_journal_free(&j);
        return false;
    }
    update_join_path(root_dir, UPD_REL_APP_XEX, app_path, sizeof(app_path));
    if (!verify_xex_matches_manifest(app_path, &m)) {
        update_journal_free(&j);
        return false;
    }

    j.phase = UPD_PHASE_GOOD;
    j.active_build = running_build;
    snprintf(j.active_version, sizeof(j.active_version), "%s", running_version);
    if (!update_journal_save(&j, root_dir, trace, ctx)) {
        update_journal_free(&j);
        return false;
    }
    update_journal_free(&j);

    update_join_path(root_dir, "updates\\moonlight.ini.pre-update", ini_bak, sizeof(ini_bak));
    fs_remove(ini_bak);
    if (out_confirmed_now)
        *out_confirmed_now = true;
    return true;
}

bool update_journal_request_manual_rollback(const char *root_dir, const char *pub_hex,
                                            int running_build,
                                            char *err_out, size_t err_cap,
                                            update_trace_fn trace, void *ctx)
{
    struct update_journal j;
    if (!update_journal_has_previous(root_dir)) {
        snprintf(err_out, err_cap, "no_previous");
        return false;
    }
    if (!update_journal_load(&j, root_dir, pub_hex, running_build, trace, ctx) || !j.valid) {
        update_journal_free(&j);
        snprintf(err_out, err_cap, "journal");
        return false;
    }
    j.phase = UPD_PHASE_MANUAL_ROLLBACK;
    if (!update_journal_save(&j, root_dir, trace, ctx)) {
        update_journal_free(&j);
        snprintf(err_out, err_cap, "disk");
        return false;
    }
    update_journal_free(&j);
    if (err_out && err_cap)
        err_out[0] = '\0';
    return true;
}

bool update_journal_has_previous(const char *root_dir)
{
    char prev_path[320];
    update_join_path(root_dir, UPD_REL_PREV_BIN, prev_path, sizeof(prev_path));
    return update_check_xex2_header(prev_path, NULL);
}

bool update_journal_ensure_embedded_runtime(const char *root_dir, const char *pub_hex,
                                            const struct update_embedded_payload *emb,
                                            bool force_recover,
                                            bool *out_extracted,
                                            char *err_out, size_t err_cap,
                                            update_trace_fn trace, void *ctx)
{
    struct update_journal j;
    char app_path[320], part_path[320], prev_path[320], fail_path[320], next_path[320];
    char sha[65];
    bool runtime_ok;
    bool journal_loaded;
    bool upgrade_from_embedded = false;

    if (out_extracted)
        *out_extracted = false;
    if (err_out && err_cap)
        err_out[0] = '\0';

    if (!root_dir || !emb || !emb->data || emb->data_len == 0 ||
        emb->size < UPDATE_MIN_XEX_BYTES || emb->size > UPDATE_MAX_XEX_BYTES ||
        emb->data_len != (size_t)emb->size || !emb->sha256_hex || strlen(emb->sha256_hex) != 64u) {
        if (err_out && err_cap) snprintf(err_out, err_cap, "no_embedded");
        return false;
    }
    if (memcmp(emb->data, "XEX2", 4) != 0) {
        if (err_out && err_cap) snprintf(err_out, err_cap, "embedded_header");
        return false;
    }

    update_join_path(root_dir, UPD_REL_APP_XEX, app_path, sizeof(app_path));
    update_join_path(root_dir, UPD_REL_EXTRACT_PART, part_path, sizeof(part_path));
    update_join_path(root_dir, UPD_REL_PREV_BIN, prev_path, sizeof(prev_path));
    update_join_path(root_dir, UPD_REL_FAIL_BIN, fail_path, sizeof(fail_path));
    update_join_path(root_dir, UPD_REL_NEXT_BIN, next_path, sizeof(next_path));

    runtime_ok = update_check_xex2_header(app_path, NULL);
    update_journal_init(&j);
    journal_loaded = update_journal_load(&j, root_dir, pub_hex, emb->build_id, trace, ctx) && j.valid;

    if (!force_recover && runtime_ok) {
        if (!journal_loaded) {
            update_journal_free(&j);
            return true;
        }
        /* Keep newer installed build, in-progress staged/trial update, or explicit rollback. */
        if (j.highest_build >= emb->build_id || j.active_build >= emb->build_id ||
            j.phase == UPD_PHASE_STAGED || j.phase == UPD_PHASE_SWAPPING ||
            j.phase == UPD_PHASE_TRIAL || j.phase == UPD_PHASE_MANUAL_ROLLBACK) {
            update_journal_free(&j);
            return true;
        }
        if (j.existed && j.phase == UPD_PHASE_GOOD && emb->build_id > j.highest_build)
            upgrade_from_embedded = true;
        else {
            update_journal_free(&j);
            return true;
        }
    }

    if (!update_sha256_bytes(emb->data, emb->data_len, sha) || strcmp(sha, emb->sha256_hex) != 0) {
        update_journal_free(&j);
        if (err_out && err_cap) snprintf(err_out, err_cap, "embedded_hash");
        emit_trace(trace, ctx, "embedded payload hash verification failed");
        return false;
    }

    if (!ensure_runtime_dirs(root_dir)) {
        update_journal_free(&j);
        if (err_out && err_cap) snprintf(err_out, err_cap, "runtime_dir");
        return false;
    }

    fs_remove(part_path);
    {
        FILE *f = fopen(part_path, "wb");
        size_t off = 0;
        const size_t chunk = 65536u;
        if (!f) {
            update_journal_free(&j);
            if (err_out && err_cap) snprintf(err_out, err_cap, "extract_open");
            return false;
        }
        while (off < emb->data_len) {
            size_t n = emb->data_len - off;
            if (n > chunk) n = chunk;
            if (fwrite(emb->data + off, 1, n, f) != n) {
                fclose(f);
                fs_remove(part_path);
                update_journal_free(&j);
                if (err_out && err_cap) snprintf(err_out, err_cap, "extract_write");
                return false;
            }
            off += n;
        }
        if (fflush(f) != 0 || fclose(f) != 0) {
            fs_remove(part_path);
            update_journal_free(&j);
            if (err_out && err_cap) snprintf(err_out, err_cap, "extract_flush");
            return false;
        }
    }

    if (!update_check_xex2_header(part_path, NULL) ||
        !update_sha256_file(part_path, sha, NULL, NULL, NULL, NULL) ||
        strcmp(sha, emb->sha256_hex) != 0) {
        fs_remove(part_path);
        update_journal_free(&j);
        if (err_out && err_cap) snprintf(err_out, err_cap, "extract_verify");
        return false;
    }

    if (force_recover) {
        fs_remove(next_path);
        if (file_exists(app_path)) {
            fs_remove(fail_path);
            if (!fs_rename(app_path, fail_path))
                fs_remove(app_path);
        }
    } else if (upgrade_from_embedded && file_exists(app_path)) {
        fs_remove(prev_path);
        if (fs_rename(app_path, prev_path)) {
            j.previous_build = j.active_build;
            snprintf(j.previous_version, sizeof(j.previous_version), "%s", j.active_version);
        } else {
            fs_remove(app_path);
        }
    } else if (file_exists(app_path)) {
        fs_remove(app_path);
    }

    if (!fs_rename(part_path, app_path)) {
        fs_remove(part_path);
        update_journal_free(&j);
        if (err_out && err_cap) snprintf(err_out, err_cap, "extract_promote");
        return false;
    }

    if (!journal_loaded) {
        update_journal_free(&j);
        update_journal_init(&j);
    }
    j.phase = UPD_PHASE_GOOD;
    j.active_build = emb->build_id;
    if (emb->version && emb->version[0])
        snprintf(j.active_version, sizeof(j.active_version), "%s", emb->version);
    if (j.highest_build < emb->build_id)
        j.highest_build = emb->build_id;
    if (force_recover)
        j.restored_notice = 1;
    free(j.manifest_bytes);
    j.manifest_bytes = NULL;
    j.manifest_len = 0;
    j.manifest_sig[0] = '\0';

    if (!update_journal_save(&j, root_dir, trace, ctx)) {
        update_journal_free(&j);
        if (err_out && err_cap) snprintf(err_out, err_cap, "journal");
        return false;
    }
    update_journal_free(&j);
    if (out_extracted)
        *out_extracted = true;
    emit_trace(trace, ctx, force_recover ? "restored runtime from embedded payload" : "extracted verified embedded runtime");
    return true;
}

bool update_journal_consume_restored_notice(const char *root_dir, const char *pub_hex,
                                            int running_build,
                                            update_trace_fn trace, void *ctx)
{
    struct update_journal j;
    bool had = false;
    if (!update_journal_load(&j, root_dir, pub_hex, running_build, trace, ctx) || !j.valid) {
        update_journal_free(&j);
        return false;
    }
    if (j.restored_notice != 0) {
        had = true;
        j.restored_notice = 0;
        update_journal_save(&j, root_dir, trace, ctx);
    }
    update_journal_free(&j);
    return had;
}
