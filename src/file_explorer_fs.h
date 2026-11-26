#pragma once
/* MoonX360 File Explorer 2.0 — FileSystem contracts, metadata classification,
 * formatting helpers, path safety guards, and optional host backend for tests. */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FE_PATH_MAX          260
#define FE_NAME_MAX          128
#define FE_EXT_MAX           16
#define FE_TYPE_MAX          32
#define FE_MAX_ROOTS         24
#define FE_MAX_ENTRIES       2048
#define FE_PREVIEW_TEXT_MAX  8192
#define FE_PREVIEW_IMG_MAX_W 320
#define FE_PREVIEW_IMG_MAX_H 240
#define FE_COPY_CHUNK        65536u
#define FE_CLIPBOARD_MAX     64

enum fe_error {
    FE_OK = 0,
    FE_ERR_INVALID = -1,
    FE_ERR_NOT_FOUND = -2,
    FE_ERR_ACCESS = -3,
    FE_ERR_EXISTS = -4,
    FE_ERR_DIR_NOT_EMPTY = -5,
    FE_ERR_PROTECTED = -6,
    FE_ERR_IO = -7,
    FE_ERR_CANCELLED = -8,
    FE_ERR_UNAVAILABLE = -9,
    FE_ERR_NOMEM = -10,
    FE_ERR_TOO_LONG = -11
};

enum fe_icon_kind {
    FE_ICON_FOLDER = 0,
    FE_ICON_PARENT,
    FE_ICON_DRIVE_HDD,
    FE_ICON_DRIVE_USB,
    FE_ICON_DRIVE_MU,
    FE_ICON_XEX,
    FE_ICON_IMAGE,
    FE_ICON_TEXT,
    FE_ICON_CONFIG,
    FE_ICON_ARCHIVE,
    FE_ICON_AUDIO,
    FE_ICON_VIDEO,
    FE_ICON_UNKNOWN,
    FE_ICON_COUNT
};

enum fe_view_mode {
    FE_VIEW_DETAILS = 0,
    FE_VIEW_COMPACT = 1,
    FE_VIEW_ICONS = 2,
    FE_VIEW_COUNT = 3
};

enum fe_sort_by {
    FE_SORT_NAME = 0,
    FE_SORT_TYPE = 1,
    FE_SORT_SIZE = 2,
    FE_SORT_DATE = 3,
    FE_SORT_COUNT = 4
};

enum fe_sort_dir {
    FE_SORT_ASC = 0,
    FE_SORT_DESC = 1
};

enum fe_mode {
    FE_MODE_MANAGER = 0,
    FE_MODE_PICK_FOLDER = 1,
    FE_MODE_PICK_XEX = 2,
    FE_MODE_PICK_AURORA_XEX = 3,
    FE_MODE_PICK_IMAGE = 4
};

struct fe_settings {
    int view_mode;       /* enum fe_view_mode */
    int sort_by;         /* enum fe_sort_by */
    int sort_dir;        /* enum fe_sort_dir */
    bool folders_first;
    bool show_extensions;
    bool show_hidden;
    bool show_sizes;
    bool show_preview_pane;
    bool confirm_delete;
    bool remember_last_dir;
    bool thumbnail_cache;
    char last_dir[160];
};

struct fe_root {
    char name[32];
    char path[FE_PATH_MAX];
    char label[48];
    int icon_kind;
    uint64_t total_bytes;
    uint64_t free_bytes;
    bool accessible;
};

struct fe_entry {
    char name[FE_NAME_MAX];
    char ext[FE_EXT_MAX];
    char type_label[FE_TYPE_MAX];
    uint64_t size;
    int64_t mtime_sec;
    uint32_t attrs;
    int icon_kind;
    bool is_dir;
    bool is_hidden;
    bool is_readonly;
    bool is_system;
    bool marked;
};

struct fe_xex_info {
    bool valid;
    bool is_xex2;
    uint32_t module_flags;
    uint32_t pe_offset;
    uint32_t header_size;
    uint64_t file_size;
    char title_hint[64];
};

struct fe_op_progress {
    int current_item;
    int total_items;
    uint64_t item_done;
    uint64_t item_total;
    uint64_t total_done;
    uint64_t total_bytes;
    bool cancel_requested;
    const char *current_name;
};

typedef bool (*fe_progress_fn)(const struct fe_op_progress *progress, void *user);

static int fe_tolower_ascii(int ch)
{
    return (ch >= 'A' && ch <= 'Z') ? ch + ('a' - 'A') : ch;
}

static int fe_toupper_ascii(int ch)
{
    return (ch >= 'a' && ch <= 'z') ? ch - ('a' - 'A') : ch;
}

static int fe_stricmp(const char *a, const char *b)
{
    if (!a || !b) return a == b ? 0 : (a ? 1 : -1);
    while (*a && *b) {
        int da = fe_tolower_ascii((unsigned char)*a);
        int db = fe_tolower_ascii((unsigned char)*b);
        if (da != db) return da - db;
        a++; b++;
    }
    return fe_tolower_ascii((unsigned char)*a) - fe_tolower_ascii((unsigned char)*b);
}

static int fe_strnicmp(const char *a, const char *b, size_t n)
{
    if (!n) return 0;
    while (n-- && *a && *b) {
        int da = fe_tolower_ascii((unsigned char)*a);
        int db = fe_tolower_ascii((unsigned char)*b);
        if (da != db) return da - db;
        if (!n) return 0;
        a++; b++;
    }
    return fe_tolower_ascii((unsigned char)*a) - fe_tolower_ascii((unsigned char)*b);
}

static void fe_settings_defaults(struct fe_settings *s)
{
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->view_mode = FE_VIEW_DETAILS;
    s->sort_by = FE_SORT_NAME;
    s->sort_dir = FE_SORT_ASC;
    s->folders_first = true;
    s->show_extensions = true;
    s->show_hidden = false;
    s->show_sizes = true;
    s->show_preview_pane = true;
    s->confirm_delete = true;
    s->remember_last_dir = true;
    s->thumbnail_cache = true;
}

static void fe_settings_sanitize(struct fe_settings *s)
{
    if (!s) return;
    if (s->view_mode < 0 || s->view_mode >= FE_VIEW_COUNT) s->view_mode = FE_VIEW_DETAILS;
    if (s->sort_by < 0 || s->sort_by >= FE_SORT_COUNT) s->sort_by = FE_SORT_NAME;
    if (s->sort_dir != FE_SORT_DESC) s->sort_dir = FE_SORT_ASC;
    s->last_dir[sizeof(s->last_dir) - 1] = '\0';
}

static void fe_extract_ext(const char *name, bool is_dir, char *ext, size_t cap)
{
    if (!ext || !cap) return;
    ext[0] = '\0';
    if (!name || !*name || is_dir) return;
    const char *slash = strrchr(name, '\\');
    const char *fslash = strrchr(name, '/');
    if (fslash && (!slash || fslash > slash)) slash = fslash;
    const char *base = slash ? slash + 1 : name;
    const char *dot = strrchr(base, '.');
    if (!dot || dot == base || !dot[1]) return;
    size_t i = 0;
    for (const char *p = dot + 1; *p && i + 1 < cap; p++)
        ext[i++] = (char)fe_tolower_ascii((unsigned char)*p);
    ext[i] = '\0';
}

static bool fe_ext_in(const char *ext, const char *const *list, int n)
{
    for (int i = 0; i < n; i++)
        if (!fe_stricmp(ext, list[i])) return true;
    return false;
}

static void fe_classify_entry(const char *name, bool is_dir, int *out_icon, char *out_type, size_t type_cap)
{
    if (is_dir) {
        if (out_icon) *out_icon = FE_ICON_FOLDER;
        if (out_type && type_cap) snprintf(out_type, type_cap, "Folder");
        return;
    }
    char ext[FE_EXT_MAX];
    fe_extract_ext(name, false, ext, sizeof(ext));

    static const char *const imgs[] = {"png", "jpg", "jpeg", "bmp", "tga", "dds", "gif"};
    static const char *const txts[] = {"txt", "log", "md", "csv", "nfo", "readme"};
    static const char *const cfgs[] = {"ini", "cfg", "conf", "json", "xml", "yaml", "yml", "tsv", "dat", "link", "meta", "bak"};
    static const char *const arcs[] = {"zip", "rar", "7z", "tar", "gz", "iso", "god"};
    static const char *const auds[] = {"mp3", "ogg", "wav", "flac", "wma", "xma"};
    static const char *const vids[] = {"mp4", "avi", "mkv", "wmv", "mov", "webm"};

    int kind = FE_ICON_UNKNOWN;
    const char *label = "File";
    char custom[FE_TYPE_MAX];

    if (!fe_stricmp(ext, "xex")) { kind = FE_ICON_XEX; label = "Xbox 360 XEX"; }
    else if (fe_ext_in(ext, imgs, (int)(sizeof(imgs) / sizeof(imgs[0])))) { kind = FE_ICON_IMAGE; label = "Image"; }
    else if (fe_ext_in(ext, txts, (int)(sizeof(txts) / sizeof(txts[0])))) { kind = FE_ICON_TEXT; label = "Text"; }
    else if (fe_ext_in(ext, cfgs, (int)(sizeof(cfgs) / sizeof(cfgs[0])))) { kind = FE_ICON_CONFIG; label = "Config"; }
    else if (fe_ext_in(ext, arcs, (int)(sizeof(arcs) / sizeof(arcs[0])))) { kind = FE_ICON_ARCHIVE; label = "Archive"; }
    else if (fe_ext_in(ext, auds, (int)(sizeof(auds) / sizeof(auds[0])))) { kind = FE_ICON_AUDIO; label = "Audio"; }
    else if (fe_ext_in(ext, vids, (int)(sizeof(vids) / sizeof(vids[0])))) { kind = FE_ICON_VIDEO; label = "Video"; }
    else if (ext[0]) {
        char up[8];
        size_t i = 0;
        for (; ext[i] && i < 6; i++) up[i] = (char)fe_toupper_ascii((unsigned char)ext[i]);
        up[i] = '\0';
        snprintf(custom, sizeof(custom), "%s file", up);
        label = custom;
    }

    if (out_icon) *out_icon = kind;
    if (out_type && type_cap) snprintf(out_type, type_cap, "%s", label);
}

static bool fe_is_text_previewable_ext(const char *ext)
{
    static const char *const exts[] = {
        "txt", "log", "md", "csv", "nfo", "readme", "ini", "cfg", "conf",
        "json", "xml", "yaml", "yml", "tsv", "lua"
    };
    return ext && ext[0] && fe_ext_in(ext, exts, (int)(sizeof(exts) / sizeof(exts[0])));
}

static bool fe_is_image_previewable_ext(const char *ext)
{
    static const char *const exts[] = {"png", "jpg", "jpeg"};
    return ext && ext[0] && fe_ext_in(ext, exts, (int)(sizeof(exts) / sizeof(exts[0])));
}

static void fe_format_size(uint64_t bytes, char *out, size_t cap)
{
    if (!out || !cap) return;
    if (bytes < 1024ULL) {
        snprintf(out, cap, "%llu B", (unsigned long long)bytes);
    } else if (bytes < 1024ULL * 1024ULL) {
        unsigned kb10 = (unsigned)((bytes * 10ULL + 512ULL) / 1024ULL);
        if (kb10 >= 1000u) snprintf(out, cap, "%u KB", (kb10 + 5u) / 10u);
        else snprintf(out, cap, "%u.%u KB", kb10 / 10u, kb10 % 10u);
    } else if (bytes < 1024ULL * 1024ULL * 1024ULL) {
        unsigned mb10 = (unsigned)((bytes * 10ULL + (1024ULL * 512ULL)) / (1024ULL * 1024ULL));
        snprintf(out, cap, "%u.%u MB", mb10 / 10u, mb10 % 10u);
    } else {
        unsigned gb100 = (unsigned)((bytes * 100ULL + (1024ULL * 1024ULL * 512ULL)) / (1024ULL * 1024ULL * 1024ULL));
        snprintf(out, cap, "%u.%02u GB", gb100 / 100u, gb100 % 100u);
    }
}

static void fe_format_date(int64_t unix_sec, char *out, size_t cap)
{
    if (!out || !cap) return;
    if (unix_sec <= 0) {
        snprintf(out, cap, "--");
        return;
    }
    time_t ts = (time_t)unix_sec;
    struct tm *lt = localtime(&ts);
    if (!lt) {
        snprintf(out, cap, "--");
        return;
    }
    snprintf(out, cap, "%04d-%02d-%02d %02d:%02d",
             lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday, lt->tm_hour, lt->tm_min);
}

static void fe_format_display_name(const struct fe_entry *e, bool show_extensions, char *out, size_t cap)
{
    if (!out || !cap) return;
    if (!e) { out[0] = '\0'; return; }
    snprintf(out, cap, "%s", e->name);
    if (!e->is_dir && !show_extensions) {
        char *dot = strrchr(out, '.');
        if (dot && dot > out && dot[1] != '\0')
            *dot = '\0';
    }
}

static bool fe_valid_filename(const char *name)
{
    if (!name || !*name) return false;
    if (!strcmp(name, ".") || !strcmp(name, "..")) return false;
    size_t len = strlen(name);
    if (len >= FE_NAME_MAX) return false;
    if (name[0] == ' ' || name[len - 1] == ' ' || name[len - 1] == '.') return false;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c < 32 || c == '\\' || c == '/' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            return false;
    }
    return true;
}

static bool fe_path_is_drive_root(const char *path)
{
    if (!path || !*path) return false;
    const char *colon = strchr(path, ':');
    if (!colon || colon == path) return false;
    return colon[1] == '\0' || ((colon[1] == '\\' || colon[1] == '/') && colon[2] == '\0');
}

static bool fe_path_join(char *out, size_t cap, const char *parent, const char *child)
{
    if (!out || !cap || !parent || !*parent || !child || !*child) return false;
    size_t plen = strlen(parent);
    bool slash = parent[plen - 1] == '\\' || parent[plen - 1] == '/';
    int n = snprintf(out, cap, "%s%s%s", parent, slash ? "" : "\\", child);
    return n > 0 && (size_t)n < cap;
}

static bool fe_path_parent(char *path)
{
    if (!path || !*path || fe_path_is_drive_root(path)) return false;
    size_t len = strlen(path);
    while (len > 0 && (path[len - 1] == '\\' || path[len - 1] == '/')) {
        if (len >= 2 && path[len - 2] == ':') return false;
        path[--len] = '\0';
    }
    char *sep = strrchr(path, '\\');
    char *fsep = strrchr(path, '/');
    if (fsep && (!sep || fsep > sep)) sep = fsep;
    if (!sep) return false;
    if (sep > path && sep[-1] == ':')
        sep[1] = '\0';
    else
        sep[1] = '\0';
    return true;
}

static void fe_path_normalize_dir(char *path, size_t cap)
{
    if (!path || !*path || !cap) return;
    size_t len = strlen(path);
    for (size_t i = 0; i < len; i++)
        if (path[i] == '/') path[i] = '\\';
    while (len > 1 && path[len - 1] == '\\' && !(len >= 2 && path[len - 2] == ':')) {
        if (len >= 3 && path[len - 2] == '\\')
            path[--len] = '\0';
        else
            break;
    }
    if (fe_path_is_drive_root(path) && len + 1 < cap && path[len - 1] == ':') {
        path[len] = '\\';
        path[len + 1] = '\0';
    }
}

static bool fe_path_is_protected(const char *path)
{
    if (!path || !*path) return true;
    if (fe_path_is_drive_root(path)) return true;
    char norm[FE_PATH_MAX];
    snprintf(norm, sizeof(norm), "%s", path);
    size_t len = strlen(norm);
    for (size_t i = 0; i < len; i++) if (norm[i] == '/') norm[i] = '\\';
    while (len > 0 && norm[len - 1] == '\\') norm[--len] = '\0';

    if (!fe_strnicmp(norm, "Flash:", 6) || !fe_strnicmp(norm, "game:", 5))
        return true;

    const char *base = strrchr(norm, '\\');
    base = base ? base + 1 : norm;
    if (!fe_stricmp(base, "$SystemUpdate") ||
        !fe_stricmp(base, "client.pem") ||
        !fe_stricmp(base, "key.pem"))
        return true;

    /* Protect MoonX360 core runtime directory / root default.xex inside MoonX360. */
    if (!fe_stricmp(base, "MoonX360") && fe_strnicmp(norm, "Hdd1:\\Apps\\MoonX360\\", 19) != 0)
        return true;
    size_t blen = strlen(norm);
    if (blen >= 21 && !fe_stricmp(norm + blen - 21, "\\MoonX360\\default.xex"))
        return true;
    return false;
}

static const char *fe_error_text(int err)
{
    switch (err) {
    case FE_OK:               return "OK";
    case FE_ERR_NOT_FOUND:    return "Item or folder was not found.";
    case FE_ERR_ACCESS:       return "Access denied by the storage device.";
    case FE_ERR_EXISTS:       return "An item with that name already exists.";
    case FE_ERR_DIR_NOT_EMPTY:return "Folder is not empty. Remove its contents first.";
    case FE_ERR_PROTECTED:    return "This system or app folder is protected.";
    case FE_ERR_IO:           return "Storage read/write error.";
    case FE_ERR_CANCELLED:    return "Operation cancelled.";
    case FE_ERR_UNAVAILABLE:  return "Storage device is no longer available.";
    case FE_ERR_NOMEM:        return "Not enough memory to complete this step.";
    case FE_ERR_TOO_LONG:     return "Path exceeds maximum length.";
    default:                  return "Invalid path or file name.";
    }
}

/* Aspect-preserving box-filter downsample from decoded ARGB to bounded preview pixels.
 * Caller frees the source buffer immediately after this returns. */
static uint32_t *fe_downsample_argb(const uint32_t *src, int src_w, int src_h,
                                    int max_w, int max_h, int *out_w, int *out_h)
{
    if (!src || src_w <= 0 || src_h <= 0 || max_w <= 0 || max_h <= 0) return NULL;
    int dst_w = src_w, dst_h = src_h;
    if (dst_w > max_w) {
        dst_h = (int)(((uint64_t)dst_h * (uint64_t)max_w) / (uint64_t)dst_w);
        dst_w = max_w;
    }
    if (dst_h > max_h) {
        dst_w = (int)(((uint64_t)dst_w * (uint64_t)max_h) / (uint64_t)dst_h);
        dst_h = max_h;
    }
    if (dst_w < 1) dst_w = 1;
    if (dst_h < 1) dst_h = 1;

    uint32_t *dst = (uint32_t *)malloc((size_t)dst_w * (size_t)dst_h * 4u);
    if (!dst) return NULL;

    for (int y = 0; y < dst_h; y++) {
        int y0 = (int)(((uint64_t)y * (uint64_t)src_h) / (uint64_t)dst_h);
        int y1 = (int)(((uint64_t)(y + 1) * (uint64_t)src_h) / (uint64_t)dst_h);
        if (y1 <= y0) y1 = y0 + 1;
        if (y1 > src_h) y1 = src_h;
        for (int x = 0; x < dst_w; x++) {
            int x0 = (int)(((uint64_t)x * (uint64_t)src_w) / (uint64_t)dst_w);
            int x1 = (int)(((uint64_t)(x + 1) * (uint64_t)src_w) / (uint64_t)dst_w);
            if (x1 <= x0) x1 = x0 + 1;
            if (x1 > src_w) x1 = src_w;
            uint32_t sa = 0, sr = 0, sg = 0, sb = 0, cnt = 0;
            for (int sy = y0; sy < y1; sy++) {
                const uint32_t *row = src + (size_t)sy * (size_t)src_w;
                for (int sx = x0; sx < x1; sx++) {
                    uint32_t p = row[sx];
                    sa += (p >> 24) & 0xFFu;
                    sr += (p >> 16) & 0xFFu;
                    sg += (p >> 8) & 0xFFu;
                    sb += p & 0xFFu;
                    cnt++;
                }
            }
            if (!cnt) cnt = 1;
            dst[(size_t)y * (size_t)dst_w + (size_t)x] =
                ((sa / cnt) << 24) | ((sr / cnt) << 16) | ((sg / cnt) << 8) | (sb / cnt);
        }
    }
    if (out_w) *out_w = dst_w;
    if (out_h) *out_h = dst_h;
    return dst;
}

#ifdef __cplusplus
extern "C" {
#endif

int fe_fs_list_roots(struct fe_root *roots, int capacity);
int fe_fs_list_dir(const char *dir, int mode, bool show_hidden,
                   struct fe_entry **out_items, int *out_count, bool *out_truncated);
bool fe_fs_exists(const char *path, bool *out_is_dir);
int fe_fs_mkdir(const char *parent, const char *name, char *out_path, size_t out_cap);
int fe_fs_rename(const char *old_path, const char *new_name, char *out_path, size_t out_cap);
int fe_fs_copy_file(const char *src_path, const char *dst_path, bool allow_overwrite,
                    uint64_t expected_size, fe_progress_fn progress_cb, void *user);
int fe_fs_move_file(const char *src_path, const char *dst_path, bool allow_overwrite,
                    uint64_t expected_size, fe_progress_fn progress_cb, void *user);
int fe_fs_delete_item(const char *path);
int fe_fs_probe_xex(const char *path, struct fe_xex_info *out);
int fe_fs_read_text_preview(const char *path, char *out_buf, size_t cap,
                            size_t *out_len, bool *out_truncated, bool *out_is_binary);
int fe_fs_decode_image_preview(const char *path, int max_w, int max_h,
                               uint32_t **out_pixels, int *out_w, int *out_h,
                               int *out_orig_w, int *out_orig_h);
bool fe_fs_launch_xex(const char *path, char *err, size_t err_cap);

#ifdef __cplusplus
}
#endif

/* Portable Windows host backend for automated desktop unit & integration tests. */
#if defined(FE_HOST_BACKEND) && !defined(_XBOX)
#include <windows.h>
#ifdef FE_WITH_IMAGE_PREVIEW
#include "wallpaper.h"
#endif

static struct fe_root fe_host_roots[FE_MAX_ROOTS];
static int fe_host_root_count;

static void fe_fs_set_host_roots(const struct fe_root *roots, int count)
{
    fe_host_root_count = count < 0 ? 0 : count > FE_MAX_ROOTS ? FE_MAX_ROOTS : count;
    if (fe_host_root_count && roots)
        memcpy(fe_host_roots, roots, (size_t)fe_host_root_count * sizeof(roots[0]));
}

static int64_t fe_host_filetime_to_unix(FILETIME ft)
{
    uint64_t v = ((uint64_t)ft.dwHighDateTime << 32) | (uint64_t)ft.dwLowDateTime;
    const uint64_t epoch = 116444736000000000ULL;
    if (v <= epoch) return 0;
    return (int64_t)((v - epoch) / 10000000ULL);
}

static bool fe_host_mode_match(int mode, const char *name, bool is_dir)
{
    if (is_dir) return true;
    if (mode == FE_MODE_MANAGER) return true;
    if (mode == FE_MODE_PICK_FOLDER) return false;
    if (mode == FE_MODE_PICK_IMAGE) {
        char ext[FE_EXT_MAX];
        fe_extract_ext(name, false, ext, sizeof(ext));
        return !fe_stricmp(ext, "png") || !fe_stricmp(ext, "jpg") || !fe_stricmp(ext, "jpeg");
    }
    return strlen(name) >= 4 && !fe_stricmp(name + strlen(name) - 4, ".xex");
}

int fe_fs_list_roots(struct fe_root *roots, int capacity)
{
    if (!roots || capacity < 1) return FE_ERR_INVALID;
    int n = fe_host_root_count < capacity ? fe_host_root_count : capacity;
    for (int i = 0; i < n; i++) {
        roots[i] = fe_host_roots[i];
        DWORD attr = GetFileAttributesA(roots[i].path);
        roots[i].accessible = attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
    return n;
}

bool fe_fs_exists(const char *path, bool *out_is_dir)
{
    if (!path || !*path) return false;
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) return false;
    if (out_is_dir) *out_is_dir = (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
    return true;
}

int fe_fs_list_dir(const char *dir, int mode, bool show_hidden,
                   struct fe_entry **out_items, int *out_count, bool *out_truncated)
{
    if (!dir || !*dir || !out_items || !out_count) return FE_ERR_INVALID;
    *out_items = NULL;
    *out_count = 0;
    if (out_truncated) *out_truncated = false;
    bool is_dir = false;
    if (!fe_fs_exists(dir, &is_dir) || !is_dir) return FE_ERR_UNAVAILABLE;

    char pattern[FE_PATH_MAX + 8];
    size_t dlen = strlen(dir);
    int pn = snprintf(pattern, sizeof(pattern), "%s%s*", dir, (dlen && (dir[dlen - 1] == '\\' || dir[dlen - 1] == '/')) ? "" : "\\");
    if (pn < 0 || pn >= (int)sizeof(pattern)) return FE_ERR_TOO_LONG;

    WIN32_FIND_DATAA data;
    HANDLE h = FindFirstFileA(pattern, &data);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        return (err == ERROR_FILE_NOT_FOUND || err == ERROR_NO_MORE_FILES) ? FE_OK : FE_ERR_UNAVAILABLE;
    }

    int cap = 64, count = 0;
    struct fe_entry *items = (struct fe_entry *)malloc((size_t)cap * sizeof(*items));
    if (!items) { FindClose(h); return FE_ERR_NOMEM; }

    do {
        const char *name = data.cFileName;
        if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
        if (strlen(name) >= FE_NAME_MAX) continue;
        bool dir_flag = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        bool hidden = (data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
        if (hidden && !show_hidden) continue;
        if (!fe_host_mode_match(mode, name, dir_flag)) continue;

        if (count >= FE_MAX_ENTRIES) {
            if (out_truncated) *out_truncated = true;
            break;
        }
        if (count == cap) {
            int ncap = cap * 2;
            if (ncap > FE_MAX_ENTRIES) ncap = FE_MAX_ENTRIES;
            struct fe_entry *grown = (struct fe_entry *)realloc(items, (size_t)ncap * sizeof(*items));
            if (!grown) { free(items); FindClose(h); return FE_ERR_NOMEM; }
            items = grown;
            cap = ncap;
        }
        struct fe_entry *e = &items[count++];
        memset(e, 0, sizeof(*e));
        snprintf(e->name, sizeof(e->name), "%s", name);
        fe_extract_ext(name, dir_flag, e->ext, sizeof(e->ext));
        fe_classify_entry(name, dir_flag, &e->icon_kind, e->type_label, sizeof(e->type_label));
        e->is_dir = dir_flag;
        e->is_hidden = hidden;
        e->is_readonly = (data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
        e->is_system = (data.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0;
        e->attrs = (uint32_t)data.dwFileAttributes;
        e->size = dir_flag ? 0ULL : (((uint64_t)data.nFileSizeHigh << 32) | (uint64_t)data.nFileSizeLow);
        e->mtime_sec = fe_host_filetime_to_unix(data.ftLastWriteTime);
    } while (FindNextFileA(h, &data));

    FindClose(h);
    *out_items = items;
    *out_count = count;
    return FE_OK;
}

int fe_fs_mkdir(const char *parent, const char *name, char *out_path, size_t out_cap)
{
    if (!parent || !fe_valid_filename(name)) return FE_ERR_INVALID;
    char full[FE_PATH_MAX];
    if (!fe_path_join(full, sizeof(full), parent, name)) return FE_ERR_TOO_LONG;
    if (fe_fs_exists(full, NULL)) return FE_ERR_EXISTS;
    if (!CreateDirectoryA(full, NULL)) return GetLastError() == ERROR_ALREADY_EXISTS ? FE_ERR_EXISTS : FE_ERR_IO;
    if (out_path && out_cap) snprintf(out_path, out_cap, "%s", full);
    return FE_OK;
}

int fe_fs_rename(const char *old_path, const char *new_name, char *out_path, size_t out_cap)
{
    if (!old_path || !*old_path || !fe_valid_filename(new_name)) return FE_ERR_INVALID;
    if (fe_path_is_protected(old_path)) return FE_ERR_PROTECTED;
    char parent[FE_PATH_MAX];
    snprintf(parent, sizeof(parent), "%s", old_path);
    if (!fe_path_parent(parent)) return FE_ERR_INVALID;
    char next[FE_PATH_MAX];
    if (!fe_path_join(next, sizeof(next), parent, new_name)) return FE_ERR_TOO_LONG;
    if (!fe_stricmp(old_path, next)) return FE_OK;
    if (fe_fs_exists(next, NULL)) return FE_ERR_EXISTS;
    if (!MoveFileA(old_path, next)) return FE_ERR_IO;
    if (out_path && out_cap) snprintf(out_path, out_cap, "%s", next);
    return FE_OK;
}

int fe_fs_copy_file(const char *src_path, const char *dst_path, bool allow_overwrite,
                    uint64_t expected_size, fe_progress_fn progress_cb, void *user)
{
    if (!src_path || !dst_path || !fe_stricmp(src_path, dst_path)) return FE_ERR_INVALID;
    if (!allow_overwrite && fe_fs_exists(dst_path, NULL)) return FE_ERR_EXISTS;
    if (allow_overwrite && fe_path_is_protected(dst_path)) return FE_ERR_PROTECTED;

    HANDLE in = CreateFileA(src_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (in == INVALID_HANDLE_VALUE) return FE_ERR_NOT_FOUND;
    LARGE_INTEGER li;
    if (!GetFileSizeEx(in, &li)) { CloseHandle(in); return FE_ERR_IO; }
    uint64_t total = expected_size ? expected_size : (uint64_t)li.QuadPart;

    HANDLE out = CreateFileA(dst_path, GENERIC_WRITE, 0, NULL, allow_overwrite ? CREATE_ALWAYS : CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (out == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        CloseHandle(in);
        return (err == ERROR_FILE_EXISTS || err == ERROR_ALREADY_EXISTS) ? FE_ERR_EXISTS : FE_ERR_IO;
    }

    unsigned char *buf = (unsigned char *)malloc(FE_COPY_CHUNK);
    if (!buf) { CloseHandle(out); CloseHandle(in); DeleteFileA(dst_path); return FE_ERR_NOMEM; }

    uint64_t done = 0;
    int status = FE_OK;
    DWORD rd = 0;
    for (;;) {
        if (!ReadFile(in, buf, FE_COPY_CHUNK, &rd, NULL)) { status = FE_ERR_IO; break; }
        if (rd == 0) break;
        DWORD wr = 0;
        if (!WriteFile(out, buf, rd, &wr, NULL) || wr != rd) { status = FE_ERR_IO; break; }
        done += wr;
        if (progress_cb) {
            struct fe_op_progress p;
            memset(&p, 0, sizeof(p));
            p.item_done = done; p.item_total = total;
            p.total_done = done; p.total_bytes = total;
            if (!progress_cb(&p, user)) { status = FE_ERR_CANCELLED; break; }
        }
    }
    free(buf);
    CloseHandle(in);
    CloseHandle(out);
    if (status == FE_OK && done != (uint64_t)li.QuadPart) status = FE_ERR_IO;
    if (status != FE_OK) DeleteFileA(dst_path);
    return status;
}

int fe_fs_move_file(const char *src_path, const char *dst_path, bool allow_overwrite,
                    uint64_t expected_size, fe_progress_fn progress_cb, void *user)
{
    if (!src_path || !dst_path || !fe_stricmp(src_path, dst_path)) return FE_ERR_INVALID;
    if (fe_path_is_protected(src_path)) return FE_ERR_PROTECTED;
    if (!allow_overwrite && fe_fs_exists(dst_path, NULL)) return FE_ERR_EXISTS;
    if (!allow_overwrite && MoveFileA(src_path, dst_path)) return FE_OK;
    int rc = fe_fs_copy_file(src_path, dst_path, allow_overwrite, expected_size, progress_cb, user);
    if (rc != FE_OK) return rc;
    if (!DeleteFileA(src_path)) return FE_ERR_IO;
    return FE_OK;
}

int fe_fs_delete_item(const char *path)
{
    if (!path || !*path) return FE_ERR_INVALID;
    if (fe_path_is_protected(path)) return FE_ERR_PROTECTED;
    bool is_dir = false;
    if (!fe_fs_exists(path, &is_dir)) return FE_ERR_NOT_FOUND;
    if (is_dir) {
        if (!RemoveDirectoryA(path)) {
            DWORD err = GetLastError();
            return err == ERROR_DIR_NOT_EMPTY ? FE_ERR_DIR_NOT_EMPTY : FE_ERR_IO;
        }
        return FE_OK;
    }
    return DeleteFileA(path) ? FE_OK : FE_ERR_IO;
}

int fe_fs_probe_xex(const char *path, struct fe_xex_info *out)
{
    if (!path || !out) return FE_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    FILE *f = fopen(path, "rb");
    if (!f) return FE_ERR_NOT_FOUND;
    if (fseek(f, 0, SEEK_END) == 0) {
        long sz = ftell(f);
        if (sz > 0) out->file_size = (uint64_t)sz;
        rewind(f);
    }
    unsigned char hdr[24];
    size_t n = fread(hdr, 1, sizeof(hdr), f);
    fclose(f);
    if (n >= 4 && memcmp(hdr, "XEX2", 4) == 0) {
        out->valid = true;
        out->is_xex2 = true;
        if (n >= 24) {
            out->module_flags = ((uint32_t)hdr[4] << 24) | ((uint32_t)hdr[5] << 16) | ((uint32_t)hdr[6] << 8) | hdr[7];
            out->pe_offset    = ((uint32_t)hdr[8] << 24) | ((uint32_t)hdr[9] << 16) | ((uint32_t)hdr[10] << 8) | hdr[11];
            out->header_size  = ((uint32_t)hdr[16] << 24) | ((uint32_t)hdr[17] << 16) | ((uint32_t)hdr[18] << 8) | hdr[19];
        }
        return FE_OK;
    }
    return FE_ERR_INVALID;
}

int fe_fs_read_text_preview(const char *path, char *out_buf, size_t cap,
                            size_t *out_len, bool *out_truncated, bool *out_is_binary)
{
    if (!path || !out_buf || cap < 2) return FE_ERR_INVALID;
    out_buf[0] = '\0';
    if (out_len) *out_len = 0;
    if (out_truncated) *out_truncated = false;
    if (out_is_binary) *out_is_binary = false;
    FILE *f = fopen(path, "rb");
    if (!f) return FE_ERR_NOT_FOUND;
    size_t max_read = cap - 1;
    if (max_read > FE_PREVIEW_TEXT_MAX) max_read = FE_PREVIEW_TEXT_MAX;
    size_t n = fread(out_buf, 1, max_read, f);
    int extra = fgetc(f);
    fclose(f);
    bool truncated = (extra != EOF);
    size_t binary_hits = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)out_buf[i];
        if (c == '\0' || (c < 32 && c != '\n' && c != '\r' && c != '\t'))
            binary_hits++;
    }
    if (binary_hits > 0 && binary_hits * 20 > n) {
        out_buf[0] = '\0';
        if (out_is_binary) *out_is_binary = true;
        return FE_OK;
    }
    for (size_t i = 0; i < n; i++)
        if (out_buf[i] == '\0') out_buf[i] = ' ';
    out_buf[n] = '\0';
    if (out_len) *out_len = n;
    if (out_truncated) *out_truncated = truncated;
    return FE_OK;
}

int fe_fs_decode_image_preview(const char *path, int max_w, int max_h,
                               uint32_t **out_pixels, int *out_w, int *out_h,
                               int *out_orig_w, int *out_orig_h)
{
    if (!path || !out_pixels) return FE_ERR_INVALID;
    *out_pixels = NULL;
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    if (out_orig_w) *out_orig_w = 0;
    if (out_orig_h) *out_orig_h = 0;
#ifdef FE_WITH_IMAGE_PREVIEW
    size_t bytes = 0;
    unsigned char *encoded = wallpaper_read_file(path, &bytes);
    if (!encoded) return FE_ERR_IO;
    int orig_w = 0, orig_h = 0;
    uint32_t *full = wallpaper_decode(encoded, bytes, &orig_w, &orig_h);
    free(encoded);
    if (!full) return FE_ERR_INVALID;
    uint32_t *small_px = fe_downsample_argb(full, orig_w, orig_h,
                                            max_w > 0 ? max_w : FE_PREVIEW_IMG_MAX_W,
                                            max_h > 0 ? max_h : FE_PREVIEW_IMG_MAX_H,
                                            out_w, out_h);
    free(full);
    if (!small_px) return FE_ERR_NOMEM;
    *out_pixels = small_px;
    if (out_orig_w) *out_orig_w = orig_w;
    if (out_orig_h) *out_orig_h = orig_h;
    return FE_OK;
#else
    (void)max_w; (void)max_h;
    return FE_ERR_UNAVAILABLE;
#endif
}

bool fe_fs_launch_xex(const char *path, char *err, size_t err_cap)
{
    (void)path;
    if (err && err_cap) snprintf(err, err_cap, "XEX execution is only supported on Xbox 360 hardware.");
    return false;
}
#endif
