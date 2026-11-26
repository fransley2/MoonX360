#pragma once
/* MoonX360 File Explorer 2.0 — Pure Model Layer:
 * Sorting, View Modes, Navigation History with Selection/Scroll Restore,
 * Multi-Select, Clipboard, and Bounded Preview State. */

#include "file_explorer_fs.h"
#include "ui/draw.h"

#define FE_HISTORY_MAX 32

enum fe_clip_op {
    FE_CLIP_NONE = 0,
    FE_CLIP_COPY = 1,
    FE_CLIP_MOVE = 2
};

enum fe_preview_kind {
    FE_PREV_NONE = 0,
    FE_PREV_IMAGE = 1,
    FE_PREV_TEXT = 2,
    FE_PREV_XEX = 3,
    FE_PREV_BINARY = 4,
    FE_PREV_ERROR = 5
};

struct fe_history_frame {
    char dir[FE_PATH_MAX];
    char selected_name[FE_NAME_MAX];
    int scroll_top;
    bool in_roots;
};

struct fe_preview_state {
    char path[FE_PATH_MAX];
    int kind;
    bool loaded;
    uint32_t *pixels;
    int img_w, img_h;
    int orig_w, orig_h;
    uint32_t generation;
    char text[FE_PREVIEW_TEXT_MAX + 1];
    size_t text_len;
    bool text_truncated;
    struct fe_xex_info xex;
    char status[64];
};

struct fe_model {
    int mode;
    bool in_roots;
    char current_dir[FE_PATH_MAX];
    struct fe_root roots[FE_MAX_ROOTS];
    int root_count;
    struct fe_entry *items;
    int count;
    bool truncated;
    int selected;
    int scroll_top;
    int error_code;
    char error_msg[96];
    struct fe_history_frame history[FE_HISTORY_MAX];
    int history_depth;
    int marked_count;
    int clip_op;
    char clip_paths[FE_CLIPBOARD_MAX][FE_PATH_MAX];
    bool clip_is_dir[FE_CLIPBOARD_MAX];
    uint64_t clip_sizes[FE_CLIPBOARD_MAX];
    int clip_count;
    struct fe_settings settings;
    struct fe_preview_state preview;
    uint32_t preview_gen_seq;
};

static void fe_model_clear_preview(struct fe_model *m)
{
    if (!m) return;
    if (m->preview.pixels) {
        ui_draw_retire_image(m->preview.pixels);
        free(m->preview.pixels);
        m->preview.pixels = NULL;
    }
    m->preview.path[0] = '\0';
    m->preview.kind = FE_PREV_NONE;
    m->preview.loaded = false;
    m->preview.img_w = m->preview.img_h = 0;
    m->preview.orig_w = m->preview.orig_h = 0;
    m->preview.text[0] = '\0';
    m->preview.text_len = 0;
    m->preview.text_truncated = false;
    memset(&m->preview.xex, 0, sizeof(m->preview.xex));
    m->preview.status[0] = '\0';
}

static void fe_model_init(struct fe_model *m, int mode, const struct fe_settings *settings)
{
    if (!m) return;
    memset(m, 0, sizeof(*m));
    m->mode = mode;
    m->in_roots = true;
    m->preview_gen_seq = 1;
    if (settings) {
        m->settings = *settings;
        fe_settings_sanitize(&m->settings);
    } else {
        fe_settings_defaults(&m->settings);
    }
}

static void fe_model_free(struct fe_model *m)
{
    if (!m) return;
    fe_model_clear_preview(m);
    free(m->items);
    m->items = NULL;
    m->count = 0;
    m->marked_count = 0;
}

static const char *fe_view_mode_name(int v)
{
    static const char *const names[FE_VIEW_COUNT] = {"Details", "Compact List", "Large Icons"};
    return names[(v >= 0 && v < FE_VIEW_COUNT) ? v : 0];
}

static const char *fe_sort_by_name(int s)
{
    static const char *const names[FE_SORT_COUNT] = {"Name", "Type", "Size", "Date"};
    return names[(s >= 0 && s < FE_SORT_COUNT) ? s : 0];
}

static const char *fe_sort_dir_name(int d)
{
    return d == FE_SORT_DESC ? "Descending" : "Ascending";
}

static int fe_compare_entries(const struct fe_entry *a, const struct fe_entry *b,
                              const struct fe_settings *s)
{
    if (s->folders_first && a->is_dir != b->is_dir)
        return a->is_dir ? -1 : 1;

    int cmp = 0;
    switch (s->sort_by) {
    case FE_SORT_TYPE:
        cmp = fe_stricmp(a->type_label, b->type_label);
        break;
    case FE_SORT_SIZE:
        if (a->size != b->size) cmp = (a->size < b->size) ? -1 : 1;
        break;
    case FE_SORT_DATE:
        if (a->mtime_sec != b->mtime_sec) cmp = (a->mtime_sec < b->mtime_sec) ? -1 : 1;
        break;
    case FE_SORT_NAME:
    default:
        break;
    }
    if (cmp == 0)
        cmp = fe_stricmp(a->name, b->name);
    if (cmp == 0)
        cmp = strcmp(a->name, b->name);
    return (s->sort_dir == FE_SORT_DESC) ? -cmp : cmp;
}

static int fe_model_find_by_name(const struct fe_model *m, const char *name)
{
    if (!m || !name || !*name) return -1;
    if (m->in_roots) {
        for (int i = 0; i < m->root_count; i++)
            if (!fe_stricmp(m->roots[i].name, name) || !fe_stricmp(m->roots[i].path, name)) return i;
        return -1;
    }
    for (int i = 0; i < m->count; i++)
        if (!fe_stricmp(m->items[i].name, name)) return i;
    return -1;
}

static void fe_model_sort(struct fe_model *m, const char *keep_selected_name)
{
    if (!m || m->in_roots || m->count <= 1 || !m->items) return;
    char keep[FE_NAME_MAX] = "";
    if (keep_selected_name && *keep_selected_name)
        snprintf(keep, sizeof(keep), "%s", keep_selected_name);
    else if (m->selected >= 0 && m->selected < m->count)
        snprintf(keep, sizeof(keep), "%s", m->items[m->selected].name);

    /* Shell sort with Knuth gaps: fast O(n^1.25), zero recursion, zero extra heap. */
    int gap = 1;
    while (gap < m->count / 3) gap = gap * 3 + 1;
    while (gap >= 1) {
        for (int i = gap; i < m->count; i++) {
            struct fe_entry tmp = m->items[i];
            int j = i;
            while (j >= gap && fe_compare_entries(&m->items[j - gap], &tmp, &m->settings) > 0) {
                m->items[j] = m->items[j - gap];
                j -= gap;
            }
            m->items[j] = tmp;
        }
        gap /= 3;
    }
    if (keep[0]) {
        int found = fe_model_find_by_name(m, keep);
        if (found >= 0) m->selected = found;
    }
}

static int fe_model_visible_count(const struct fe_model *m)
{
    if (!m) return 0;
    return m->in_roots ? m->root_count : m->count;
}

static void fe_model_clamp_scroll(struct fe_model *m, int visible_rows, int cols)
{
    if (!m) return;
    int total = fe_model_visible_count(m);
    if (cols < 1) cols = 1;
    if (visible_rows < 1) visible_rows = 1;
    if (total <= 0) {
        m->selected = 0;
        m->scroll_top = 0;
        return;
    }
    if (m->selected < 0) m->selected = 0;
    if (m->selected >= total) m->selected = total - 1;

    int sel_row = m->selected / cols;
    int total_rows = (total + cols - 1) / cols;
    int top_row = m->scroll_top / cols;
    if (sel_row < top_row) top_row = sel_row;
    if (sel_row >= top_row + visible_rows) top_row = sel_row - visible_rows + 1;
    if (top_row > total_rows - visible_rows) top_row = total_rows - visible_rows;
    if (top_row < 0) top_row = 0;
    m->scroll_top = top_row * cols;
}

static void fe_model_move(struct fe_model *m, int dx, int dy, int visible_rows, int cols)
{
    if (!m) return;
    int total = fe_model_visible_count(m);
    if (total <= 0) return;
    if (cols < 1) cols = 1;
    int next = m->selected;
    if (cols == 1) {
        if (dy != 0) next = (m->selected + total + dy) % total;
        else if (dx != 0) next = m->selected + dx * visible_rows;
    } else {
        if (dx != 0) next += dx;
        if (dy != 0) next += dy * cols;
        if (next < 0) next = 0;
        if (next >= total) next = total - 1;
    }
    if (next < 0) next = 0;
    if (next >= total) next = total - 1;
    m->selected = next;
    fe_model_clamp_scroll(m, visible_rows, cols);
}

static void fe_model_page(struct fe_model *m, int dir, int visible_rows, int cols)
{
    if (!m) return;
    int total = fe_model_visible_count(m);
    if (total <= 0) return;
    if (cols < 1) cols = 1;
    if (visible_rows < 1) visible_rows = 1;
    int step = visible_rows * cols;
    int next = m->selected + (dir < 0 ? -step : step);
    if (next < 0) next = 0;
    if (next >= total) next = total - 1;
    m->selected = next;
    fe_model_clamp_scroll(m, visible_rows, cols);
}

static void fe_model_push_history(struct fe_model *m)
{
    if (!m) return;
    if (m->history_depth >= FE_HISTORY_MAX) {
        memmove(&m->history[0], &m->history[1], (FE_HISTORY_MAX - 1) * sizeof(m->history[0]));
        m->history_depth = FE_HISTORY_MAX - 1;
    }
    struct fe_history_frame *f = &m->history[m->history_depth++];
    memset(f, 0, sizeof(*f));
    f->in_roots = m->in_roots;
    f->scroll_top = m->scroll_top;
    snprintf(f->dir, sizeof(f->dir), "%s", m->current_dir);
    if (m->in_roots && m->selected >= 0 && m->selected < m->root_count)
        snprintf(f->selected_name, sizeof(f->selected_name), "%s", m->roots[m->selected].name);
    else if (!m->in_roots && m->selected >= 0 && m->selected < m->count)
        snprintf(f->selected_name, sizeof(f->selected_name), "%s", m->items[m->selected].name);
}

static bool fe_model_load_roots(struct fe_model *m, const char *restore_name)
{
    if (!m) return false;
    fe_model_clear_preview(m);
    free(m->items);
    m->items = NULL;
    m->count = 0;
    m->truncated = false;
    m->marked_count = 0;
    m->in_roots = true;
    m->current_dir[0] = '\0';
    m->error_code = FE_OK;
    m->error_msg[0] = '\0';
    int n = fe_fs_list_roots(m->roots, FE_MAX_ROOTS);
    if (n < 0) {
        m->root_count = 0;
        m->error_code = n;
        snprintf(m->error_msg, sizeof(m->error_msg), "%s", fe_error_text(n));
        return false;
    }
    m->root_count = n;
    m->selected = 0;
    m->scroll_top = 0;
    if (restore_name && *restore_name) {
        int idx = fe_model_find_by_name(m, restore_name);
        if (idx >= 0) m->selected = idx;
    }
    return n > 0;
}

static bool fe_model_open_dir(struct fe_model *m, const char *dir, const char *restore_name, bool push_hist)
{
    if (!m || !dir || !*dir) return false;
    if (push_hist) fe_model_push_history(m);

    struct fe_entry *next_items = NULL;
    int next_count = 0;
    bool next_trunc = false;
    int rc = fe_fs_list_dir(dir, m->mode, m->settings.show_hidden, &next_items, &next_count, &next_trunc);
    if (rc != FE_OK) {
        if (push_hist && m->history_depth > 0) m->history_depth--;
        m->error_code = rc;
        snprintf(m->error_msg, sizeof(m->error_msg), "%s", fe_error_text(rc));
        return false;
    }

    fe_model_clear_preview(m);
    free(m->items);
    m->items = next_items;
    m->count = next_count;
    m->truncated = next_trunc;
    m->marked_count = 0;
    m->in_roots = false;
    snprintf(m->current_dir, sizeof(m->current_dir), "%s", dir);
    fe_path_normalize_dir(m->current_dir, sizeof(m->current_dir));
    m->error_code = FE_OK;
    m->error_msg[0] = '\0';
    m->selected = 0;
    m->scroll_top = 0;
    fe_model_sort(m, restore_name);
    return true;
}

static bool fe_model_go_back(struct fe_model *m, int visible_rows, int cols)
{
    if (!m) return false;
    if (m->in_roots) return false;

    if (m->history_depth > 0) {
        struct fe_history_frame prev = m->history[--m->history_depth];
        if (prev.in_roots) {
            fe_model_load_roots(m, prev.selected_name);
            fe_model_clamp_scroll(m, visible_rows, cols);
            return true;
        }
        if (fe_model_open_dir(m, prev.dir, prev.selected_name, false)) {
            m->scroll_top = prev.scroll_top;
            fe_model_clamp_scroll(m, visible_rows, cols);
            return true;
        }
    }

    char cur_name[FE_NAME_MAX] = "";
    size_t len = strlen(m->current_dir);
    while (len > 0 && m->current_dir[len - 1] == '\\') len--;
    const char *slash = NULL;
    for (size_t i = 0; i < len; i++) if (m->current_dir[i] == '\\') slash = m->current_dir + i;
    if (slash && slash + 1 < m->current_dir + len) {
        size_t n = (size_t)(m->current_dir + len - (slash + 1));
        if (n >= sizeof(cur_name)) n = sizeof(cur_name) - 1;
        memcpy(cur_name, slash + 1, n);
        cur_name[n] = '\0';
    } else if (len >= 2 && m->current_dir[len - 1] == ':') {
        snprintf(cur_name, sizeof(cur_name), "%.*s", (int)(len - 1), m->current_dir);
    }

    char parent[FE_PATH_MAX];
    snprintf(parent, sizeof(parent), "%s", m->current_dir);
    char cur_cmp[FE_PATH_MAX];
    snprintf(cur_cmp, sizeof(cur_cmp), "%s", m->current_dir);
    for (size_t cl = strlen(cur_cmp); cl > 0 && cur_cmp[cl - 1] == '\\' && !(cl >= 2 && cur_cmp[cl - 2] == ':'); cl--)
        cur_cmp[cl - 1] = '\0';
    for (int i = 0; i < m->root_count; i++) {
        char rp[FE_PATH_MAX];
        snprintf(rp, sizeof(rp), "%s", m->roots[i].path);
        for (size_t rl = strlen(rp); rl > 0 && rp[rl - 1] == '\\' && !(rl >= 2 && rp[rl - 2] == ':'); rl--)
            rp[rl - 1] = '\0';
        if (!fe_stricmp(cur_cmp, rp)) {
            fe_model_load_roots(m, m->roots[i].name);
            fe_model_clamp_scroll(m, visible_rows, cols);
            return true;
        }
    }
    if (fe_path_parent(parent) && fe_model_open_dir(m, parent, cur_name, false)) {
        fe_model_clamp_scroll(m, visible_rows, cols);
        return true;
    }
    fe_model_load_roots(m, cur_name);
    fe_model_clamp_scroll(m, visible_rows, cols);
    return true;
}

static bool fe_model_toggle_mark(struct fe_model *m, int index)
{
    if (!m || m->in_roots || index < 0 || index >= m->count) return false;
    m->items[index].marked = !m->items[index].marked;
    m->marked_count += m->items[index].marked ? 1 : -1;
    if (m->marked_count < 0) m->marked_count = 0;
    return m->items[index].marked;
}

static void fe_model_clear_marks(struct fe_model *m)
{
    if (!m || !m->items) return;
    for (int i = 0; i < m->count; i++) m->items[i].marked = false;
    m->marked_count = 0;
}

static int fe_model_set_clipboard(struct fe_model *m, int clip_op)
{
    if (!m || m->in_roots || m->count <= 0) return 0;
    m->clip_op = clip_op;
    m->clip_count = 0;
    bool use_marks = (m->marked_count > 0);
    for (int i = 0; i < m->count && m->clip_count < FE_CLIPBOARD_MAX; i++) {
        bool pick = use_marks ? m->items[i].marked : (i == m->selected);
        if (!pick) continue;
        char full[FE_PATH_MAX];
        if (!fe_path_join(full, sizeof(full), m->current_dir, m->items[i].name)) continue;
        snprintf(m->clip_paths[m->clip_count], sizeof(m->clip_paths[0]), "%s", full);
        m->clip_is_dir[m->clip_count] = m->items[i].is_dir;
        m->clip_sizes[m->clip_count] = m->items[i].size;
        m->clip_count++;
    }
    if (!m->clip_count) m->clip_op = FE_CLIP_NONE;
    return m->clip_count;
}

static void fe_model_clear_clipboard(struct fe_model *m)
{
    if (!m) return;
    m->clip_op = FE_CLIP_NONE;
    m->clip_count = 0;
}

static void fe_model_cycle_view(struct fe_model *m)
{
    if (!m) return;
    m->settings.view_mode = (m->settings.view_mode + 1) % FE_VIEW_COUNT;
}

static void fe_model_refresh_preview(struct fe_model *m, bool decode_images)
{
    if (!m) return;
    if (!m->settings.show_preview_pane || m->in_roots || m->count <= 0 ||
        m->selected < 0 || m->selected >= m->count) {
        fe_model_clear_preview(m);
        return;
    }
    const struct fe_entry *e = &m->items[m->selected];
    char full[FE_PATH_MAX];
    if (!fe_path_join(full, sizeof(full), m->current_dir, e->name)) {
        fe_model_clear_preview(m);
        return;
    }
    if (m->preview.loaded && !strcmp(m->preview.path, full))
        return;

    fe_model_clear_preview(m);
    snprintf(m->preview.path, sizeof(m->preview.path), "%s", full);
    m->preview.loaded = true;

    if (e->is_dir) {
        m->preview.kind = FE_PREV_NONE;
        snprintf(m->preview.status, sizeof(m->preview.status), "Folder");
        return;
    }
    if (e->icon_kind == FE_ICON_XEX) {
        m->preview.kind = FE_PREV_XEX;
        if (fe_fs_probe_xex(full, &m->preview.xex) == FE_OK && m->preview.xex.is_xex2)
            snprintf(m->preview.status, sizeof(m->preview.status), "Valid XEX2 header");
        else
            snprintf(m->preview.status, sizeof(m->preview.status), "Invalid / unreadable XEX");
        return;
    }
    if (decode_images && fe_is_image_previewable_ext(e->ext)) {
        uint32_t *px = NULL;
        int w = 0, h = 0, ow = 0, oh = 0;
        int rc = fe_fs_decode_image_preview(full, FE_PREVIEW_IMG_MAX_W, FE_PREVIEW_IMG_MAX_H, &px, &w, &h, &ow, &oh);
        if (rc == FE_OK && px) {
            m->preview.kind = FE_PREV_IMAGE;
            m->preview.pixels = px;
            m->preview.img_w = w;
            m->preview.img_h = h;
            m->preview.orig_w = ow;
            m->preview.orig_h = oh;
            m->preview.generation = m->preview_gen_seq++;
            snprintf(m->preview.status, sizeof(m->preview.status), "%d x %d px", ow, oh);
        } else {
            m->preview.kind = FE_PREV_ERROR;
            snprintf(m->preview.status, sizeof(m->preview.status), "Image preview unavailable");
        }
        return;
    }
    if (fe_is_text_previewable_ext(e->ext)) {
        bool binary = false;
        int rc = fe_fs_read_text_preview(full, m->preview.text, sizeof(m->preview.text),
                                         &m->preview.text_len, &m->preview.text_truncated, &binary);
        if (rc == FE_OK && !binary) {
            m->preview.kind = FE_PREV_TEXT;
            snprintf(m->preview.status, sizeof(m->preview.status),
                     m->preview.text_truncated ? "Text preview (8 KB cap)" : "Text preview");
        } else if (binary) {
            m->preview.kind = FE_PREV_BINARY;
            snprintf(m->preview.status, sizeof(m->preview.status), "Binary content");
        } else {
            m->preview.kind = FE_PREV_ERROR;
            snprintf(m->preview.status, sizeof(m->preview.status), "Could not read file");
        }
        return;
    }
    m->preview.kind = FE_PREV_NONE;
    snprintf(m->preview.status, sizeof(m->preview.status), "No preview available");
}
