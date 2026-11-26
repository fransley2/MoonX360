#pragma once
/* MoonX360 File Explorer 2.0 — Interactive Controller & Picker Bridge Layer.
 * Compiled into src/app.c for Xbox 360 and directly exercised in host tests. */

#include <Limelight.h>
#include <stdio.h>
#include <string.h>
#include "file_explorer_model.h"
#include "ui/file_explorer_view.h"
#include "ui/ui.h"
#include "aurora_paths.h"

#define FE_KEY_LT 0x04000000
#define FE_KEY_RT 0x08000000

struct fe_ui_ops {
    int (*wait_button)(void);
    uint32_t (*now_ms)(void);
    void (*sleep_ms)(unsigned ms);
    void (*present)(void);
    void (*message)(int icon, const char *title, const char *text);
    bool (*confirm)(const char *title, const char *text, const char *confirm_label);
    bool (*edit_name)(const char *title, const char *subtitle, char *buf, int cap);
    void (*save_settings)(const struct fe_settings *s);
    int (*poll_raw_buttons)(void);
    bool (*valid_xex_contract)(const char *path);
    bool (*current_xex_contract)(const char *path);
    bool (*valid_aurora_contract)(const char *folder);
};

struct fe_paste_ctx {
    const struct fe_ui_ops *ops;
    const char *action;
    const char *item_name;
    int item_index;
    int total_items;
    uint64_t prior_done;
    uint64_t total_bytes;
    uint32_t last_draw_ms;
    bool cancelled;
};

static bool fe_app_progress_cb(const struct fe_op_progress *p, void *user)
{
    struct fe_paste_ctx *ctx = (struct fe_paste_ctx *)user;
    if (!ctx || !ctx->ops) return true;
    if (ctx->ops->poll_raw_buttons && (ctx->ops->poll_raw_buttons() & B_FLAG)) {
        ctx->cancelled = true;
        return false;
    }
    uint32_t now = ctx->ops->now_ms ? ctx->ops->now_ms() : 0;
    if (ctx->ops->present && (!ctx->last_draw_ms || (uint32_t)(now - ctx->last_draw_ms) >= 100u)) {
        struct fe_op_progress agg = *p;
        agg.current_item = ctx->item_index + 1;
        agg.total_items = ctx->total_items;
        agg.total_done = ctx->prior_done + p->item_done;
        agg.total_bytes = ctx->total_bytes;
        fe_view_draw_progress_modal(ctx->action, ctx->item_name, &agg);
        ctx->ops->present();
        ctx->last_draw_ms = now;
    }
    return true;
}

static int fe_app_menu_popup(const struct fe_ui_ops *ops, const char *bg_title,
                             const struct fe_model *m, const char *menu_title,
                             const struct ui_setting *items, int count)
{
    if (!ops || !items || count <= 0) return -1;
    int sel = 0;
    for (;;) {
        fe_view_draw(bg_title, m);
        ui_draw_context_menu(menu_title, items, count, sel, 460.0f, 200.0f);
        if (ops->present) ops->present();
        int key = ops->wait_button();
        if (key & B_FLAG) return -1;
        if (key & UP_FLAG) sel = (sel + count - 1) % count;
        if (key & DOWN_FLAG) sel = (sel + 1) % count;
        if (key & (A_FLAG | PLAY_FLAG)) return sel;
    }
}

static void fe_app_properties_modal(const struct fe_ui_ops *ops, const struct fe_model *m,
                                    const struct fe_entry *e, const char *full_path)
{
    if (!ops || !m || !e) return;
    for (;;) {
        fe_view_draw_properties_modal(m, e, full_path);
        if (ops->present) ops->present();
        int key = ops->wait_button();
        if (key & (A_FLAG | B_FLAG)) return;
    }
}

static void fe_app_preview_modal(const struct fe_ui_ops *ops, struct fe_model *m)
{
    if (!ops || !m) return;
    fe_model_refresh_preview(m, true);
    int total_lines = 1;
    for (const char *p = m->preview.text; *p; p++)
        if (*p == '\n') total_lines++;
    int scroll = 0;
    for (;;) {
        fe_view_draw_preview_modal(m, scroll);
        if (ops->present) ops->present();
        int key = ops->wait_button();
        if (key & (B_FLAG | X_FLAG)) return;
        if (key & UP_FLAG) { if (scroll > 0) scroll--; }
        if (key & DOWN_FLAG) { if (scroll + 10 < total_lines) scroll++; }
        if (key & FE_KEY_LT) { scroll -= 10; if (scroll < 0) scroll = 0; }
        if (key & FE_KEY_RT) { scroll += 10; if (scroll + 10 > total_lines) scroll = total_lines > 10 ? total_lines - 10 : 0; }
    }
}

static void fe_app_settings_menu(const struct fe_ui_ops *ops, struct fe_settings *s)
{
    if (!ops || !s) return;
    int sel = 0;
    const int n = 12;
    for (;;) {
        char vals[12][64];
        snprintf(vals[0], 64, "%s", fe_view_mode_name(s->view_mode));
        snprintf(vals[1], 64, "%s", fe_sort_by_name(s->sort_by));
        snprintf(vals[2], 64, "%s", fe_sort_dir_name(s->sort_dir));
        snprintf(vals[3], 64, "%s", s->folders_first ? "On" : "Off");
        snprintf(vals[4], 64, "%s", s->show_extensions ? "On" : "Off");
        snprintf(vals[5], 64, "%s", s->show_hidden ? "On" : "Off");
        snprintf(vals[6], 64, "%s", s->show_sizes ? "On" : "Off");
        snprintf(vals[7], 64, "%s", s->show_preview_pane ? "On" : "Off");
        snprintf(vals[8], 64, "%s", s->confirm_delete ? "On" : "Off");
        snprintf(vals[9], 64, "%s", s->remember_last_dir ? "On" : "Off");
        snprintf(vals[10], 64, "%s", s->thumbnail_cache ? "On" : "Off");
        snprintf(vals[11], 64, "%s", "Reset");

        struct ui_setting rows[12] = {
            {"Default View", vals[0], "Choose the default File Explorer layout: Details, Compact List or Large Icons."},
            {"Sort By", vals[1], "Default column used to order folders and files."},
            {"Sort Direction", vals[2], "Ascending (A-Z) or Descending (Z-A)."},
            {"Folders First", vals[3], "Always group subfolders ahead of files."},
            {"Show File Extensions", vals[4], "Display filename extensions such as .xex, .ini or .png."},
            {"Show Hidden Files", vals[5], "Include hidden and system entries when browsing storage."},
            {"Show File Sizes", vals[6], "Display formatted file sizes in list and icon views."},
            {"Show Preview Pane", vals[7], "Show the right-hand details and preview panel."},
            {"Confirm Before Deleting", vals[8], "Always prompt before deleting a file or empty folder."},
            {"Remember Last Directory", vals[9], "Reopen the last browsed folder on next launch."},
            {"Image Thumbnail Preview", vals[10], "Automatically decode bounded PNG/JPEG previews while browsing."},
            {"Reset Explorer Defaults", vals[11], "Restore File Explorer view and behavior preferences to defaults."}
        };
        int kinds[12] = {
            UI_OPTION_CYCLE, UI_OPTION_CYCLE, UI_OPTION_CYCLE,
            UI_OPTION_TOGGLE, UI_OPTION_TOGGLE, UI_OPTION_TOGGLE, UI_OPTION_TOGGLE,
            UI_OPTION_TOGGLE, UI_OPTION_TOGGLE, UI_OPTION_TOGGLE, UI_OPTION_TOGGLE,
            UI_OPTION_ACTION
        };
        ui_draw_settings_options("File Explorer Settings", rows, kinds, n, sel);
        if (ops->present) ops->present();
        int key = ops->wait_button();
        if (key & B_FLAG) {
            if (ops->save_settings) ops->save_settings(s);
            return;
        }
        if (key & UP_FLAG) sel = (sel + n - 1) % n;
        if (key & DOWN_FLAG) sel = (sel + 1) % n;
        if (key & (LEFT_FLAG | RIGHT_FLAG | A_FLAG)) {
            int d = (key & LEFT_FLAG) ? -1 : 1;
            switch (sel) {
            case 0: s->view_mode = (s->view_mode + d + FE_VIEW_COUNT) % FE_VIEW_COUNT; break;
            case 1: s->sort_by = (s->sort_by + d + FE_SORT_COUNT) % FE_SORT_COUNT; break;
            case 2: s->sort_dir = (s->sort_dir == FE_SORT_ASC) ? FE_SORT_DESC : FE_SORT_ASC; break;
            case 3: s->folders_first = !s->folders_first; break;
            case 4: s->show_extensions = !s->show_extensions; break;
            case 5: s->show_hidden = !s->show_hidden; break;
            case 6: s->show_sizes = !s->show_sizes; break;
            case 7: s->show_preview_pane = !s->show_preview_pane; break;
            case 8: s->confirm_delete = !s->confirm_delete; break;
            case 9: s->remember_last_dir = !s->remember_last_dir; break;
            case 10: s->thumbnail_cache = !s->thumbnail_cache; break;
            case 11:
                if ((key & A_FLAG) && (!ops->confirm || ops->confirm("Reset Explorer Defaults?", "Restore default view, sort and safety preferences.", "Reset"))) {
                    char kept[160];
                    snprintf(kept, sizeof(kept), "%s", s->last_dir);
                    fe_settings_defaults(s);
                    snprintf(s->last_dir, sizeof(s->last_dir), "%s", kept);
                }
                break;
            }
        }
    }
}

static void fe_app_do_paste(const struct fe_ui_ops *ops, struct fe_model *m)
{
    if (!ops || !m || m->in_roots || m->clip_count <= 0 || m->clip_op == FE_CLIP_NONE) return;
    uint64_t total_bytes = 0;
    for (int i = 0; i < m->clip_count; i++) total_bytes += m->clip_sizes[i];

    struct fe_paste_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.ops = ops;
    ctx.action = m->clip_op == FE_CLIP_MOVE ? "Moving files" : "Copying files";
    ctx.total_items = m->clip_count;
    ctx.total_bytes = total_bytes;

    int done_ok = 0;
    bool overwrite_all = false;
    const char *last_dst_name = NULL;

    for (int i = 0; i < m->clip_count; i++) {
        if (m->clip_is_dir[i]) {
            if (ops->message) ops->message(ICON_INFO, "Folder copy skipped", "Copy and move apply to individual or selected files only.");
            continue;
        }
        const char *base = strrchr(m->clip_paths[i], '\\');
        base = base ? base + 1 : m->clip_paths[i];
        char dst[FE_PATH_MAX];
        if (!fe_path_join(dst, sizeof(dst), m->current_dir, base)) {
            if (ops->message) ops->message(ICON_ERROR, "Operation failed", fe_error_text(FE_ERR_TOO_LONG));
            break;
        }
        if (!fe_stricmp(m->clip_paths[i], dst)) {
            if (ops->message) ops->message(ICON_INFO, "Same destination", "Source and destination paths are identical.");
            continue;
        }

        bool allow_overwrite = overwrite_all;
        if (!allow_overwrite && fe_fs_exists(dst, NULL)) {
            char msg[192];
            snprintf(msg, sizeof(msg), "Replace existing file \"%s\"?", base);
            if (!ops->confirm || !ops->confirm("File already exists", msg, "Overwrite"))
                continue;
            allow_overwrite = true;
        }

        ctx.item_index = i;
        ctx.item_name = base;
        int rc = (m->clip_op == FE_CLIP_MOVE)
            ? fe_fs_move_file(m->clip_paths[i], dst, allow_overwrite, m->clip_sizes[i], fe_app_progress_cb, &ctx)
            : fe_fs_copy_file(m->clip_paths[i], dst, allow_overwrite, m->clip_sizes[i], fe_app_progress_cb, &ctx);
        if (rc == FE_ERR_CANCELLED) {
            if (ops->message) ops->message(ICON_INFO, "Cancelled", "Partial destination files were cleaned up.");
            break;
        }
        if (rc != FE_OK) {
            if (ops->message) ops->message(ICON_ERROR, "Operation failed", fe_error_text(rc));
            break;
        }
        ctx.prior_done += m->clip_sizes[i];
        last_dst_name = base;
        done_ok++;
    }

    if (done_ok > 0 && m->clip_op == FE_CLIP_MOVE)
        fe_model_clear_clipboard(m);
    fe_model_clear_marks(m);
    fe_model_open_dir(m, m->current_dir, last_dst_name, false);
}

static void fe_app_do_delete(const struct fe_ui_ops *ops, struct fe_model *m)
{
    if (!ops || !m || m->in_roots || m->count <= 0) return;
    int target_indices[FE_CLIPBOARD_MAX], ntargets = 0;
    if (m->marked_count > 0) {
        for (int i = 0; i < m->count && ntargets < FE_CLIPBOARD_MAX; i++)
            if (m->items[i].marked) target_indices[ntargets++] = i;
    } else if (m->selected >= 0 && m->selected < m->count) {
        target_indices[ntargets++] = m->selected;
    }
    if (!ntargets) return;

    if (m->settings.confirm_delete) {
        char msg[192];
        if (ntargets == 1)
            snprintf(msg, sizeof(msg), "Delete \"%s\" permanently? Non-empty folders and protected folders will not be removed.",
                     m->items[target_indices[0]].name);
        else
            snprintf(msg, sizeof(msg), "Delete %d selected items permanently? Non-empty folders will not be removed.", ntargets);
        if (!ops->confirm || !ops->confirm("Confirm delete", msg, "Delete"))
            return;
    }

    for (int i = 0; i < ntargets; i++) {
        char full[FE_PATH_MAX];
        if (!fe_path_join(full, sizeof(full), m->current_dir, m->items[target_indices[i]].name)) continue;
        int rc = fe_fs_delete_item(full);
        if (rc != FE_OK) {
            if (ops->message) ops->message(ICON_ERROR, "Delete failed", fe_error_text(rc));
            break;
        }
    }
    fe_model_clear_marks(m);
    fe_model_open_dir(m, m->current_dir, NULL, false);
}

static void fe_app_do_rename(const struct fe_ui_ops *ops, struct fe_model *m)
{
    if (!ops || !m || m->in_roots || m->selected < 0 || m->selected >= m->count || !ops->edit_name) return;
    char full[FE_PATH_MAX];
    if (!fe_path_join(full, sizeof(full), m->current_dir, m->items[m->selected].name)) return;
    if (fe_path_is_protected(full)) {
        if (ops->message) ops->message(ICON_ERROR, "Protected item", fe_error_text(FE_ERR_PROTECTED));
        return;
    }
    char name[FE_NAME_MAX];
    snprintf(name, sizeof(name), "%s", m->items[m->selected].name);
    if (!ops->edit_name("Rename item", "Enter the new name. B cancels.", name, sizeof(name))) return;
    if (!fe_valid_filename(name)) {
        if (ops->message) ops->message(ICON_ERROR, "Invalid name", fe_error_text(FE_ERR_INVALID));
        return;
    }
    char out_path[FE_PATH_MAX];
    int rc = fe_fs_rename(full, name, out_path, sizeof(out_path));
    if (rc != FE_OK) {
        if (ops->message) ops->message(ICON_ERROR, "Rename failed", fe_error_text(rc));
        return;
    }
    fe_model_open_dir(m, m->current_dir, name, false);
}

static void fe_app_do_new_folder(const struct fe_ui_ops *ops, struct fe_model *m)
{
    if (!ops || !m || m->in_roots || !ops->edit_name) return;
    char name[FE_NAME_MAX] = "";
    if (!ops->edit_name("Create Folder", "Enter the new folder name. B cancels.", name, sizeof(name))) return;
    if (!fe_valid_filename(name)) {
        if (ops->message) ops->message(ICON_ERROR, "Invalid name", fe_error_text(FE_ERR_INVALID));
        return;
    }
    char created[FE_PATH_MAX];
    int rc = fe_fs_mkdir(m->current_dir, name, created, sizeof(created));
    if (rc != FE_OK) {
        if (ops->message) ops->message(ICON_ERROR, "Folder not created", fe_error_text(rc));
        return;
    }
    fe_model_open_dir(m, m->current_dir, name, false);
}

static void fe_app_sort_menu(const struct fe_ui_ops *ops, struct fe_model *m)
{
    struct ui_setting items[5] = {
        {"Sort by Name", m->settings.sort_by == FE_SORT_NAME ? (m->settings.sort_dir ? "Z-A" : "A-Z") : "", NULL},
        {"Sort by Type", m->settings.sort_by == FE_SORT_TYPE ? (m->settings.sort_dir ? "Z-A" : "A-Z") : "", NULL},
        {"Sort by Size", m->settings.sort_by == FE_SORT_SIZE ? (m->settings.sort_dir ? "Desc" : "Asc") : "", NULL},
        {"Sort by Date", m->settings.sort_by == FE_SORT_DATE ? (m->settings.sort_dir ? "Newest" : "Oldest") : "", NULL},
        {"Folders First", m->settings.folders_first ? "On" : "Off", NULL}
    };
    int pick = fe_app_menu_popup(ops, "File Explorer", m, "Sort Order", items, 5);
    if (pick < 0) return;
    if (pick == 4) m->settings.folders_first = !m->settings.folders_first;
    else if (pick >= 0 && pick < 4) {
        if (m->settings.sort_by == pick)
            m->settings.sort_dir = (m->settings.sort_dir == FE_SORT_ASC) ? FE_SORT_DESC : FE_SORT_ASC;
        else {
            m->settings.sort_by = pick;
            m->settings.sort_dir = FE_SORT_ASC;
        }
    }
    fe_model_sort(m, NULL);
    if (ops->save_settings) ops->save_settings(&m->settings);
}

static bool fe_app_validate_folder_pick(const struct fe_ui_ops *ops, const char *title,
                                        const char *folder, char *out_path, size_t out_cap)
{
    bool is_dir = false;
    if (!folder || !*folder || !fe_fs_exists(folder, &is_dir) || !is_dir) {
        if (ops->message) ops->message(ICON_ERROR, "Folder unavailable", "The selected folder is no longer accessible.");
        return false;
    }
    if (strlen(folder) >= out_cap) {
        if (ops->message) ops->message(ICON_ERROR, "Path too long", "This path exceeds the shortcut path limit.");
        return false;
    }
    if (title && !strcmp(title, "Select Aurora Folder") && ops->valid_aurora_contract && !ops->valid_aurora_contract(folder)) {
        if (ops->message) ops->message(ICON_ERROR, "Aurora.xex missing", "Select the folder containing a readable Aurora.xex with an XEX2 header.");
        return false;
    }
    snprintf(out_path, out_cap, "%s", folder);
    return true;
}

static bool fe_app_validate_file_pick(const struct fe_ui_ops *ops, int mode,
                                      const struct fe_model *m, const char *candidate,
                                      const char *name, char *out_path, size_t out_cap)
{
    if (strlen(candidate) >= out_cap) {
        if (ops->message) ops->message(ICON_ERROR, "Path too long", "This path exceeds the shortcut path limit.");
        return false;
    }
    if (mode == FE_MODE_PICK_IMAGE) {
        snprintf(out_path, out_cap, "%s", candidate);
        return true;
    }
    if (mode == FE_MODE_PICK_AURORA_XEX) {
        if (fe_stricmp(name, "Aurora.xex") || (ops->valid_aurora_contract && !ops->valid_aurora_contract(m->current_dir))) {
            if (ops->message) ops->message(ICON_ERROR, "Invalid Aurora XEX", "Select Aurora.xex with a readable XEX2 header.");
            return false;
        }
        snprintf(out_path, out_cap, "%s", m->current_dir);
        return true;
    }
    if (!aurora_path_xex_name(name) || strlen(candidate) >= 160) {
        if (ops->message) ops->message(ICON_ERROR, "Select default.xex", "The MoonX360 main XEX must be named default.xex.");
        return false;
    }
    if (ops->valid_xex_contract && !ops->valid_xex_contract(candidate)) {
        if (ops->message) ops->message(ICON_ERROR, "Invalid Xbox 360 XEX", "Selected file could not be opened or has no XEX2 header.");
        return false;
    }
    if (ops->current_xex_contract && !ops->current_xex_contract(candidate)) {
        if (ops->message) ops->message(ICON_ERROR, "Different XEX selected", "Select the default.xex of this running MoonX360 installation.");
        return false;
    }
    snprintf(out_path, out_cap, "%s", candidate);
    return true;
}

static bool fe_app_run(const struct fe_ui_ops *ops, struct fe_settings *settings,
                       const char *title, char *out_path, size_t out_cap, int mode)
{
    if (!ops || !ops->wait_button || !settings) return false;
    struct fe_model m;
    fe_model_init(&m, mode, settings);
    m.root_count = fe_fs_list_roots(m.roots, FE_MAX_ROOTS);
    if (m.root_count < 0) m.root_count = 0;

    char start_dir[FE_PATH_MAX] = "";
    if (out_path && out_path[0]) {
        snprintf(start_dir, sizeof(start_dir), "%s", out_path);
        if (mode == FE_MODE_PICK_XEX || mode == FE_MODE_PICK_AURORA_XEX || mode == FE_MODE_PICK_IMAGE)
            fe_path_parent(start_dir);
    } else if (mode == FE_MODE_MANAGER && settings->remember_last_dir && settings->last_dir[0]) {
        snprintf(start_dir, sizeof(start_dir), "%s", settings->last_dir);
    }

    bool start_is_dir = false;
    if (start_dir[0] && fe_fs_exists(start_dir, &start_is_dir) && start_is_dir) {
        if (!fe_model_open_dir(&m, start_dir, NULL, false))
            fe_model_load_roots(&m, NULL);
    } else {
        fe_model_load_roots(&m, NULL);
    }

    bool accepted = false;
    const char *ui_title = title && *title ? title : "File Explorer";

    for (;;) {
        float w = 1280.0f, h = 720.0f, sc = 1.0f;
        uint32_t acc = 0, sel_col = 0;
        ui_surface_metrics(&w, &h, &sc, &acc, &sel_col);
        bool wide = w >= 1100.0f;
        bool show_prev = m.settings.show_preview_pane && w >= 900.0f;
        int vis_rows = 8, cols = 1;
        fe_view_metrics(wide, m.settings.view_mode, show_prev, &vis_rows, &cols);
        fe_model_clamp_scroll(&m, vis_rows, cols);
        fe_model_refresh_preview(&m, m.settings.thumbnail_cache);

        fe_view_draw(ui_title, &m);
        if (ops->present) ops->present();
        int key = ops->wait_button();

        if (key & B_FLAG) {
            if (m.in_roots) break;
            fe_model_go_back(&m, vis_rows, cols);
            continue;
        }
        if (key & UP_FLAG)    { fe_model_move(&m, 0, -1, vis_rows, cols); continue; }
        if (key & DOWN_FLAG)  { fe_model_move(&m, 0,  1, vis_rows, cols); continue; }
        if (key & LEFT_FLAG)  { fe_model_move(&m, -1, 0, vis_rows, cols); continue; }
        if (key & RIGHT_FLAG) { fe_model_move(&m,  1, 0, vis_rows, cols); continue; }
        if (key & FE_KEY_LT)  { fe_model_page(&m, -1, vis_rows, cols); continue; }
        if (key & FE_KEY_RT)  { fe_model_page(&m,  1, vis_rows, cols); continue; }

        if (key & Y_FLAG) {
            fe_model_cycle_view(&m);
            continue;
        }

        if (key & (LB_FLAG | RB_FLAG)) {
            int rcount = fe_fs_list_roots(m.roots, FE_MAX_ROOTS);
            if (rcount > 0) m.root_count = rcount;
            if (m.root_count <= 0) continue;
            if (m.in_roots) {
                int dir = (key & LB_FLAG) ? -1 : 1;
                m.selected = (m.selected + m.root_count + dir) % m.root_count;
            } else {
                int cur = -1;
                for (int r = 0; r < m.root_count; r++)
                    if (!fe_strnicmp(m.current_dir, m.roots[r].path, strlen(m.roots[r].path))) { cur = r; break; }
                int dir = (key & LB_FLAG) ? -1 : 1;
                int next = cur >= 0 ? (cur + m.root_count + dir) % m.root_count : 0;
                if (!fe_model_open_dir(&m, m.roots[next].path, NULL, true)) {
                    if (ops->message) ops->message(ICON_ERROR, "Device unavailable", m.error_msg);
                    fe_model_load_roots(&m, NULL);
                }
            }
            continue;
        }

        if (key & A_FLAG) {
            if (m.in_roots) {
                if (m.selected >= 0 && m.selected < m.root_count) {
                    if (!fe_model_open_dir(&m, m.roots[m.selected].path, NULL, true))
                        if (ops->message) ops->message(ICON_ERROR, "Folder unavailable", m.error_msg);
                }
                continue;
            }
            if (m.count <= 0 || m.selected < 0 || m.selected >= m.count) continue;
            const struct fe_entry *e = &m.items[m.selected];
            char candidate[FE_PATH_MAX];
            if (!fe_path_join(candidate, sizeof(candidate), m.current_dir, e->name)) {
                if (ops->message) ops->message(ICON_ERROR, "Path too long", fe_error_text(FE_ERR_TOO_LONG));
                continue;
            }
            if (e->is_dir) {
                if (!fe_model_open_dir(&m, candidate, NULL, true))
                    if (ops->message) ops->message(ICON_ERROR, "Folder unavailable", m.error_msg);
                continue;
            }
            if (mode != FE_MODE_MANAGER && mode != FE_MODE_PICK_FOLDER) {
                if (fe_app_validate_file_pick(ops, mode, &m, candidate, e->name, out_path, out_cap)) {
                    accepted = true;
                    break;
                }
                continue;
            }
            if (mode == FE_MODE_MANAGER) {
                if (fe_is_image_previewable_ext(e->ext) || fe_is_text_previewable_ext(e->ext))
                    fe_app_preview_modal(ops, &m);
                else
                    fe_app_properties_modal(ops, &m, e, candidate);
            }
            continue;
        }

        if (key & PLAY_FLAG) {
            if (mode == FE_MODE_PICK_FOLDER && !m.in_roots) {
                if (fe_app_validate_folder_pick(ops, title, m.current_dir, out_path, out_cap)) {
                    accepted = true;
                    break;
                }
                continue;
            }
            struct ui_setting acts[6];
            int aids[6], ac = 0;
            if (!m.in_roots) {
                acts[ac].label = "New Folder"; acts[ac].value = ""; acts[ac].help = NULL; aids[ac++] = 1;
                if (mode == FE_MODE_MANAGER && m.clip_count > 0) {
                    acts[ac].label = "Paste"; acts[ac].value = m.clip_op == FE_CLIP_MOVE ? "Move" : "Copy"; acts[ac].help = NULL; aids[ac++] = 2;
                }
            }
            acts[ac].label = "Sort By..."; acts[ac].value = fe_sort_by_name(m.settings.sort_by); acts[ac].help = NULL; aids[ac++] = 3;
            acts[ac].label = "Storage Drives"; acts[ac].value = ""; acts[ac].help = NULL; aids[ac++] = 4;
            acts[ac].label = "File Explorer Settings"; acts[ac].value = ""; acts[ac].help = NULL; aids[ac++] = 5;
            acts[ac].label = "Refresh"; acts[ac].value = ""; acts[ac].help = NULL; aids[ac++] = 6;
            int ch = fe_app_menu_popup(ops, ui_title, &m, "Quick Actions", acts, ac);
            if (ch >= 0 && ch < ac) {
                switch (aids[ch]) {
                case 1: fe_app_do_new_folder(ops, &m); break;
                case 2: fe_app_do_paste(ops, &m); break;
                case 3: fe_app_sort_menu(ops, &m); break;
                case 4: fe_model_load_roots(&m, NULL); break;
                case 5:
                    fe_app_settings_menu(ops, &m.settings);
                    if (!m.in_roots) fe_model_open_dir(&m, m.current_dir, NULL, false);
                    break;
                case 6:
                    if (m.in_roots) fe_model_load_roots(&m, NULL);
                    else fe_model_open_dir(&m, m.current_dir, NULL, false);
                    break;
                }
            }
            continue;
        }

        if (key & X_FLAG) {
            if (m.in_roots) {
                struct ui_setting r_acts[3] = {
                    {"Open", "", NULL},
                    {"Refresh", "", NULL},
                    {"File Explorer Settings", "", NULL}
                };
                int ch = fe_app_menu_popup(ops, ui_title, &m, "Actions", r_acts, 3);
                if (ch == 0 && m.selected >= 0 && m.selected < m.root_count) {
                    if (!fe_model_open_dir(&m, m.roots[m.selected].path, NULL, true))
                        if (ops->message) ops->message(ICON_ERROR, "Folder unavailable", m.error_msg);
                } else if (ch == 1) fe_model_load_roots(&m, NULL);
                else if (ch == 2) fe_app_settings_menu(ops, &m.settings);
                continue;
            }

            enum {
                ACT_OPEN = 1, ACT_SELECT_THIS_DIR, ACT_SELECT_FILE, ACT_PREVIEW,
                ACT_PROPERTIES, ACT_MARK, ACT_COPY, ACT_MOVE, ACT_PASTE,
                ACT_RENAME, ACT_DELETE, ACT_NEW_FOLDER, ACT_LAUNCH_XEX,
                ACT_SORT, ACT_DRIVES, ACT_SETTINGS
            };
            struct ui_setting c_items[16];
            int c_ids[16], c_n = 0;
            bool has_item = (m.count > 0 && m.selected >= 0 && m.selected < m.count);
            const struct fe_entry *sel_e = has_item ? &m.items[m.selected] : NULL;

            if (mode == FE_MODE_PICK_FOLDER) {
                c_items[c_n].label = "Select this folder"; c_items[c_n].value = ""; c_items[c_n].help = NULL; c_ids[c_n++] = ACT_SELECT_THIS_DIR;
            }
            if (sel_e) {
                if (sel_e->is_dir) {
                    c_items[c_n].label = "Open"; c_items[c_n].value = ""; c_items[c_n].help = NULL; c_ids[c_n++] = ACT_OPEN;
                } else {
                    if (mode != FE_MODE_MANAGER && mode != FE_MODE_PICK_FOLDER) {
                        c_items[c_n].label = "Select"; c_items[c_n].value = ""; c_items[c_n].help = NULL; c_ids[c_n++] = ACT_SELECT_FILE;
                    }
                    if (fe_is_image_previewable_ext(sel_e->ext) || fe_is_text_previewable_ext(sel_e->ext)) {
                        c_items[c_n].label = "Preview"; c_items[c_n].value = ""; c_items[c_n].help = NULL; c_ids[c_n++] = ACT_PREVIEW;
                    }
                    if (mode == FE_MODE_MANAGER && sel_e->icon_kind == FE_ICON_XEX) {
                        c_items[c_n].label = "Launch XEX..."; c_items[c_n].value = "Exit"; c_items[c_n].help = NULL; c_ids[c_n++] = ACT_LAUNCH_XEX;
                    }
                }
                c_items[c_n].label = "Properties"; c_items[c_n].value = ""; c_items[c_n].help = NULL; c_ids[c_n++] = ACT_PROPERTIES;
            }
            if (mode == FE_MODE_MANAGER) {
                if (sel_e) {
                    c_items[c_n].label = sel_e->marked ? "Deselect" : "Select / Mark"; c_items[c_n].value = ""; c_items[c_n].help = NULL; c_ids[c_n++] = ACT_MARK;
                    c_items[c_n].label = "Copy"; c_items[c_n].value = ""; c_items[c_n].help = NULL; c_ids[c_n++] = ACT_COPY;
                    c_items[c_n].label = "Move"; c_items[c_n].value = ""; c_items[c_n].help = NULL; c_ids[c_n++] = ACT_MOVE;
                }
                if (m.clip_count > 0) {
                    c_items[c_n].label = "Paste"; c_items[c_n].value = m.clip_op == FE_CLIP_MOVE ? "Move" : "Copy"; c_items[c_n].help = NULL; c_ids[c_n++] = ACT_PASTE;
                }
                if (sel_e) {
                    c_items[c_n].label = "Rename"; c_items[c_n].value = ""; c_items[c_n].help = NULL; c_ids[c_n++] = ACT_RENAME;
                    c_items[c_n].label = "Delete"; c_items[c_n].value = ""; c_items[c_n].help = NULL; c_ids[c_n++] = ACT_DELETE;
                }
            }
            c_items[c_n].label = "New Folder"; c_items[c_n].value = ""; c_items[c_n].help = NULL; c_ids[c_n++] = ACT_NEW_FOLDER;
            c_items[c_n].label = "Sort By..."; c_items[c_n].value = fe_sort_by_name(m.settings.sort_by); c_items[c_n].help = NULL; c_ids[c_n++] = ACT_SORT;
            c_items[c_n].label = "Storage Drives"; c_items[c_n].value = ""; c_items[c_n].help = NULL; c_ids[c_n++] = ACT_DRIVES;
            c_items[c_n].label = "File Explorer Settings"; c_items[c_n].value = ""; c_items[c_n].help = NULL; c_ids[c_n++] = ACT_SETTINGS;

            int ch = fe_app_menu_popup(ops, ui_title, &m, sel_e ? sel_e->name : "Folder Actions", c_items, c_n);
            if (ch < 0 || ch >= c_n) continue;
            char full[FE_PATH_MAX] = "";
            if (sel_e) fe_path_join(full, sizeof(full), m.current_dir, sel_e->name);

            switch (c_ids[ch]) {
            case ACT_OPEN:
                if (sel_e && sel_e->is_dir && full[0]) {
                    if (!fe_model_open_dir(&m, full, NULL, true))
                        if (ops->message) ops->message(ICON_ERROR, "Folder unavailable", m.error_msg);
                }
                break;
            case ACT_SELECT_THIS_DIR:
                if (fe_app_validate_folder_pick(ops, title, m.current_dir, out_path, out_cap)) {
                    accepted = true;
                    goto done;
                }
                break;
            case ACT_SELECT_FILE:
                if (sel_e && full[0] && fe_app_validate_file_pick(ops, mode, &m, full, sel_e->name, out_path, out_cap)) {
                    accepted = true;
                    goto done;
                }
                break;
            case ACT_PREVIEW:
                if (sel_e) fe_app_preview_modal(ops, &m);
                break;
            case ACT_PROPERTIES:
                if (sel_e && full[0]) fe_app_properties_modal(ops, &m, sel_e, full);
                break;
            case ACT_MARK:
                fe_model_toggle_mark(&m, m.selected);
                break;
            case ACT_COPY:
                fe_model_set_clipboard(&m, FE_CLIP_COPY);
                break;
            case ACT_MOVE:
                fe_model_set_clipboard(&m, FE_CLIP_MOVE);
                break;
            case ACT_PASTE:
                fe_app_do_paste(ops, &m);
                break;
            case ACT_RENAME:
                fe_app_do_rename(ops, &m);
                break;
            case ACT_DELETE:
                fe_app_do_delete(ops, &m);
                break;
            case ACT_NEW_FOLDER:
                fe_app_do_new_folder(ops, &m);
                break;
            case ACT_LAUNCH_XEX:
                if (sel_e && full[0]) {
                    struct fe_xex_info info;
                    if (fe_fs_probe_xex(full, &info) != FE_OK || !info.is_xex2) {
                        if (ops->message) ops->message(ICON_ERROR, "Invalid Xbox 360 XEX", "Selected file does not have a valid XEX2 header.");
                    } else if (!ops->confirm || ops->confirm("Launch Xbox 360 XEX?", "MoonX360 will exit to launch this executable. Unsaved changes will close.", "Launch")) {
                        char lerr[128] = "";
                        if (!fe_fs_launch_xex(full, lerr, sizeof(lerr)) && ops->message)
                            ops->message(ICON_ERROR, "Launch failed", lerr[0] ? lerr : "Could not launch XEX.");
                    }
                }
                break;
            case ACT_SORT:
                fe_app_sort_menu(ops, &m);
                break;
            case ACT_DRIVES:
                fe_model_load_roots(&m, NULL);
                break;
            case ACT_SETTINGS:
                fe_app_settings_menu(ops, &m.settings);
                if (!m.in_roots) fe_model_open_dir(&m, m.current_dir, NULL, false);
                break;
            }
        }
    }

done:
    *settings = m.settings;
    if (m.settings.remember_last_dir && !m.in_roots && strlen(m.current_dir) < sizeof(settings->last_dir))
        snprintf(settings->last_dir, sizeof(settings->last_dir), "%s", m.current_dir);
    if (ops->save_settings) ops->save_settings(settings);
    fe_model_free(&m);
    return accepted;
}
