#include "xdk.h"
#include "aurora_mount_xdk.h"
#include "../../src/file_explorer_fs.h"
#include "../../src/aurora_paths.h"
#include "../../src/aurora_shortcuts.h"
#include "../../src/wallpaper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int64_t xdk_filetime_to_unix(FILETIME ft)
{
    uint64_t v = ((uint64_t)ft.dwHighDateTime << 32) | (uint64_t)ft.dwLowDateTime;
    const uint64_t epoch = 116444736000000000ULL;
    if (v <= epoch) return 0;
    return (int64_t)((v - epoch) / 10000000ULL);
}

static bool xdk_is_directory(const char *path)
{
    if (!moonx_mount_for_path(path)) return false;
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static bool xdk_mode_match(int mode, const char *name, bool is_dir)
{
    if (is_dir) return true;
    if (mode == FE_MODE_MANAGER) return true;
    if (mode == FE_MODE_PICK_FOLDER) return false;
    if (mode == FE_MODE_PICK_IMAGE) return aurora_path_image_name(name);
    return aurora_path_xex_name(name);
}

int fe_fs_list_roots(struct fe_root *roots, int capacity)
{
    if (!roots || capacity < 1) return FE_ERR_INVALID;
    static const struct { const char *name; const char *label; int icon; } specs[] = {
        {"Hdd1",   "Internal HDD (Hdd1)",      FE_ICON_DRIVE_HDD},
        {"IntMu",  "Internal Memory (IntMu)",  FE_ICON_DRIVE_MU},
        {"Usb0",   "USB Drive 0 (Usb0)",       FE_ICON_DRIVE_USB},
        {"Usb1",   "USB Drive 1 (Usb1)",       FE_ICON_DRIVE_USB},
        {"Usb2",   "USB Drive 2 (Usb2)",       FE_ICON_DRIVE_USB},
        {"Usb3",   "USB Drive 3 (Usb3)",       FE_ICON_DRIVE_USB},
        {"Usb4",   "USB Drive 4 (Usb4)",       FE_ICON_DRIVE_USB},
        {"Usb5",   "USB Drive 5 (Usb5)",       FE_ICON_DRIVE_USB},
        {"Usb6",   "USB Drive 6 (Usb6)",       FE_ICON_DRIVE_USB},
        {"Usb7",   "USB Drive 7 (Usb7)",       FE_ICON_DRIVE_USB},
        {"Usb8",   "USB Drive 8 (Usb8)",       FE_ICON_DRIVE_USB},
        {"Usb9",   "USB Drive 9 (Usb9)",       FE_ICON_DRIVE_USB},
        {"UsbMu0", "USB Memory Unit 0",        FE_ICON_DRIVE_MU},
        {"UsbMu1", "USB Memory Unit 1",        FE_ICON_DRIVE_MU},
        {"UsbMu2", "USB Memory Unit 2",        FE_ICON_DRIVE_MU},
        {"UsbMu3", "USB Memory Unit 3",        FE_ICON_DRIVE_MU},
        {"UsbMu4", "USB Memory Unit 4",        FE_ICON_DRIVE_MU},
        {"Mu0",    "Memory Unit 0 (Mu0)",      FE_ICON_DRIVE_MU},
        {"Mu1",    "Memory Unit 1 (Mu1)",      FE_ICON_DRIVE_MU}
    };
    int count = 0;
    for (unsigned i = 0; i < sizeof(specs) / sizeof(specs[0]) && count < capacity; i++) {
        char path[32];
        snprintf(path, sizeof(path), "%s:\\", specs[i].name);
        if (!xdk_is_directory(path)) continue;
        struct fe_root *r = &roots[count++];
        memset(r, 0, sizeof(*r));
        snprintf(r->name, sizeof(r->name), "%s", specs[i].name);
        snprintf(r->path, sizeof(r->path), "%s", path);
        snprintf(r->label, sizeof(r->label), "%s", specs[i].label);
        r->icon_kind = specs[i].icon;
        r->accessible = true;
        ULARGE_INTEGER free_avail, total_bytes, total_free;
        if (GetDiskFreeSpaceExA(path, &free_avail, &total_bytes, &total_free)) {
            r->total_bytes = total_bytes.QuadPart;
            r->free_bytes = free_avail.QuadPart;
        }
    }
    return count;
}

bool fe_fs_exists(const char *path, bool *out_is_dir)
{
    if (!path || !*path) return false;
    if (!moonx_mount_for_path(path)) return false;
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
    if (!xdk_is_directory(dir)) return FE_ERR_UNAVAILABLE;

    char pattern[FE_PATH_MAX + 8];
    size_t dlen = strlen(dir);
    int pn = snprintf(pattern, sizeof(pattern), "%s%s*", dir, (dlen && dir[dlen - 1] == '\\') ? "" : "\\");
    if (pn < 0 || pn >= (int)sizeof(pattern)) return FE_ERR_TOO_LONG;

    WIN32_FIND_DATAA data;
    HANDLE search = FindFirstFileA(pattern, &data);
    if (search == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        return (err == ERROR_FILE_NOT_FOUND || err == ERROR_NO_MORE_FILES) ? FE_OK : FE_ERR_UNAVAILABLE;
    }

    int cap = 64, count = 0;
    struct fe_entry *items = (struct fe_entry *)malloc((size_t)cap * sizeof(*items));
    if (!items) { FindClose(search); return FE_ERR_NOMEM; }

    do {
        const char *name = data.cFileName;
        if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
        if (strlen(name) >= FE_NAME_MAX) continue;
        bool is_dir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        bool is_hidden = (data.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0;
        if (is_hidden && !show_hidden) continue;
        if (!xdk_mode_match(mode, name, is_dir)) continue;

        if (count >= FE_MAX_ENTRIES) {
            if (out_truncated) *out_truncated = true;
            break;
        }
        if (count == cap) {
            int ncap = cap * 2;
            if (ncap > FE_MAX_ENTRIES) ncap = FE_MAX_ENTRIES;
            struct fe_entry *grown = (struct fe_entry *)realloc(items, (size_t)ncap * sizeof(*items));
            if (!grown) { free(items); FindClose(search); return FE_ERR_NOMEM; }
            items = grown;
            cap = ncap;
        }
        struct fe_entry *e = &items[count++];
        memset(e, 0, sizeof(*e));
        snprintf(e->name, sizeof(e->name), "%s", name);
        fe_extract_ext(name, is_dir, e->ext, sizeof(e->ext));
        fe_classify_entry(name, is_dir, &e->icon_kind, e->type_label, sizeof(e->type_label));
        e->is_dir = is_dir;
        e->is_hidden = is_hidden;
        e->is_readonly = (data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
        e->is_system = (data.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0;
        e->attrs = (uint32_t)data.dwFileAttributes;
        e->size = is_dir ? 0ULL : (((uint64_t)data.nFileSizeHigh << 32) | (uint64_t)data.nFileSizeLow);
        e->mtime_sec = xdk_filetime_to_unix(data.ftLastWriteTime);
    } while (FindNextFileA(search, &data));

    FindClose(search);
    *out_items = items;
    *out_count = count;
    return FE_OK;
}

int fe_fs_mkdir(const char *parent, const char *name, char *out_path, size_t out_cap)
{
    if (!parent || !fe_valid_filename(name) || !xdk_is_directory(parent)) return FE_ERR_INVALID;
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
    if (!moonx_mount_for_path(old_path)) return FE_ERR_UNAVAILABLE;
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
    if (!moonx_mount_for_path(src_path) || !moonx_mount_for_path(dst_path)) return FE_ERR_UNAVAILABLE;
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
    if (!moonx_mount_for_path(src_path) || !moonx_mount_for_path(dst_path)) return FE_ERR_UNAVAILABLE;
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
    if (!moonx_mount_for_path(path)) return FE_ERR_UNAVAILABLE;
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
    if (!moonx_mount_for_path(path)) return FE_ERR_UNAVAILABLE;
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
    if (!moonx_mount_for_path(path)) return FE_ERR_UNAVAILABLE;
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
    if (!moonx_mount_for_path(path)) return FE_ERR_UNAVAILABLE;

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
}

bool fe_fs_launch_xex(const char *path, char *err, size_t err_cap)
{
    struct fe_xex_info info;
    if (!path || !*path) {
        if (err && err_cap) snprintf(err, err_cap, "Invalid XEX path.");
        return false;
    }
    if (fe_fs_probe_xex(path, &info) != FE_OK || !info.is_xex2) {
        if (err && err_cap) snprintf(err, err_cap, "Selected file is not a readable XEX2 executable.");
        return false;
    }
    XLaunchNewImage(path, 0);
    return true;
}
