#include "file_explorer_view.h"
#include "ui.h"
#include "draw.h"
#include "font.h"
#include "layout.h"
#include "design_system.h"
#include <stdio.h>
#include <string.h>

static float fe_W, fe_H, fe_S;
static uint32_t fe_ACC, fe_SEL;

#define FE_S(v)  ((v) * fe_S)
#define FE_SI(v) ((int)((v) * fe_S + 0.5f))

static void fe_sync_metrics(void)
{
    ui_surface_metrics(&fe_W, &fe_H, &fe_S, &fe_ACC, &fe_SEL);
    if (fe_W <= 0.0f) fe_W = 1280.0f;
    if (fe_H <= 0.0f) fe_H = 720.0f;
    if (fe_S <= 0.0f) fe_S = 1.0f;
}

static void fe_txt(canvas_t *cv, int face, float px, float x, float y, float max_w, uint32_t col, const char *s)
{
    if (!s || !*s) return;
    if (max_w > 0.0f) font_draw_fit(cv, face, FE_S(px), FE_SI(x), FE_SI(y), FE_SI(max_w), col, s);
    else font_draw(cv, face, FE_S(px), FE_SI(x), FE_SI(y), col, s);
}

static void fe_txt_r(canvas_t *cv, int face, float px, float rx, float y, uint32_t col, const char *s)
{
    if (!s || !*s) return;
    float tw = font_text_width(face, FE_S(px), s) / fe_S;
    font_draw(cv, face, FE_S(px), FE_SI(rx - tw), FE_SI(y), col, s);
}

static void fe_txt_c(canvas_t *cv, int face, float px, float cx, float y, uint32_t col, const char *s)
{
    if (!s || !*s) return;
    float tw = font_text_width(face, FE_S(px), s) / fe_S;
    font_draw(cv, face, FE_S(px), FE_SI(cx - tw * 0.5f), FE_SI(y), col, s);
}

static uint32_t fe_icon_color(int kind)
{
    switch (kind) {
    case FE_ICON_FOLDER:
    case FE_ICON_PARENT:    return HEX(0xF5B94A);
    case FE_ICON_DRIVE_HDD: return HEX(0x72C840);
    case FE_ICON_DRIVE_USB: return HEX(0x4EA3FF);
    case FE_ICON_DRIVE_MU:  return HEX(0xB48AFF);
    case FE_ICON_XEX:       return HEX(0x72C840);
    case FE_ICON_IMAGE:     return HEX(0x3CD0B0);
    case FE_ICON_TEXT:      return HEX(0xD8E2E0);
    case FE_ICON_CONFIG:    return HEX(0xF5B94A);
    case FE_ICON_ARCHIVE:   return HEX(0xE07B53);
    case FE_ICON_AUDIO:     return HEX(0x64B5F6);
    case FE_ICON_VIDEO:     return HEX(0xE57373);
    default:                return DS_TEXT_SECONDARY;
    }
}

/* Crisp resolution-independent vector icon centered at (cx, cy) with half-extent k. */
static void fe_draw_icon(canvas_t *cv, int kind, float cx, float cy, float k, uint32_t tint)
{
    float X = FE_S(cx), Y = FE_S(cy), K = FE_S(k);
    uint32_t col = tint ? tint : fe_icon_color(kind);

    if (kind == FE_ICON_FOLDER || kind == FE_ICON_PARENT) {
        ui_round(cv, X - K * 0.85f, Y - K * 0.65f, K * 0.72f, K * 0.34f, K * 0.12f, col);
        ui_round(cv, X - K * 0.85f, Y - K * 0.42f, K * 1.70f, K * 1.12f, K * 0.16f, col);
        ui_fill(cv, FE_SI(cx - k * 0.72f), FE_SI(cy - k * 0.15f), FE_SI(k * 1.44f), FE_SI(2.0f), ARGB(60, 0, 0, 0));
        if (kind == FE_ICON_PARENT)
            ui_triangle(cv, X, Y - K * 0.15f, X - K * 0.35f, Y + K * 0.30f, X + K * 0.35f, Y + K * 0.30f, DS_BG_BASE);
        return;
    }
    if (kind == FE_ICON_DRIVE_HDD || kind == FE_ICON_DRIVE_USB || kind == FE_ICON_DRIVE_MU) {
        ui_round(cv, X - K * 0.85f, Y - K * 0.56f, K * 1.70f, K * 1.12f, K * 0.18f, DS_BG_ELEVATED);
        ui_outline(cv, X - K * 0.85f, Y - K * 0.56f, K * 1.70f, K * 1.12f, K * 0.18f, FE_S(1.8f), col);
        ui_circle(cv, X + K * 0.48f, Y + K * 0.22f, K * 0.16f, col);
        ui_fill(cv, FE_SI(cx - k * 0.60f), FE_SI(cy - k * 0.24f), FE_SI(k * 0.90f), FE_SI(k * 0.22f), col);
        return;
    }
    /* Document silhouette with folded corner */
    ui_round(cv, X - K * 0.65f, Y - K * 0.82f, K * 1.30f, K * 1.64f, K * 0.14f, DS_BG_ELEVATED);
    ui_outline(cv, X - K * 0.65f, Y - K * 0.82f, K * 1.30f, K * 1.64f, K * 0.14f, FE_S(1.5f), col);
    ui_fill(cv, FE_SI(cx - k * 0.45f), FE_SI(cy + k * 0.28f), FE_SI(k * 0.90f), FE_SI(k * 0.30f), col);

    if (kind == FE_ICON_XEX) {
        ui_line(cv, X - K * 0.30f, Y - K * 0.38f, X + K * 0.30f, Y + K * 0.10f, FE_S(2.0f), col);
        ui_line(cv, X + K * 0.30f, Y - K * 0.38f, X - K * 0.30f, Y + K * 0.10f, FE_S(2.0f), col);
    } else if (kind == FE_ICON_IMAGE) {
        ui_triangle(cv, X - K * 0.40f, Y + K * 0.14f, X - K * 0.10f, Y - K * 0.34f, X + K * 0.18f, Y + K * 0.14f, col);
        ui_circle(cv, X + K * 0.24f, Y - K * 0.42f, K * 0.14f, col);
    } else if (kind == FE_ICON_TEXT || kind == FE_ICON_CONFIG) {
        ui_fill(cv, FE_SI(cx - k * 0.42f), FE_SI(cy - k * 0.46f), FE_SI(k * 0.84f), FE_SI(2.0f), col);
        ui_fill(cv, FE_SI(cx - k * 0.42f), FE_SI(cy - k * 0.16f), FE_SI(k * 0.62f), FE_SI(2.0f), col);
    } else if (kind == FE_ICON_AUDIO || kind == FE_ICON_VIDEO) {
        ui_triangle(cv, X - K * 0.22f, Y - K * 0.42f, X - K * 0.22f, Y + K * 0.12f, X + K * 0.32f, Y - K * 0.15f, col);
    }
}

void fe_view_metrics(bool wide, int view_mode, bool show_preview, int *out_rows, int *out_cols)
{
    int rows = 9, cols = 1;
    if (view_mode == FE_VIEW_COMPACT) {
        rows = 11;
        cols = 1;
    } else if (view_mode == FE_VIEW_ICONS) {
        rows = 3;
        cols = wide ? (show_preview ? 3 : 4) : (show_preview ? 2 : 3);
    } else {
        rows = 8;
        cols = 1;
    }
    if (out_rows) *out_rows = rows;
    if (out_cols) *out_cols = cols;
}

static void fe_draw_scrollbar(canvas_t *cv, float x, float y, float h, int total_rows, int vis_rows, int top_row)
{
    if (total_rows <= vis_rows || vis_rows <= 0) return;
    ui_round(cv, FE_S(x), FE_S(y), FE_S(6), FE_S(h), FE_S(3), WITH_ALPHA(DS_BORDER_SUBTLE, 90));
    float thumb_h = h * ((float)vis_rows / (float)total_rows);
    if (thumb_h < 28.0f) thumb_h = 28.0f;
    float max_top = (float)(total_rows - vis_rows);
    float thumb_y = y + (h - thumb_h) * (max_top > 0.0f ? ((float)top_row / max_top) : 0.0f);
    ui_round(cv, FE_S(x), FE_S(thumb_y), FE_S(6), FE_S(thumb_h), FE_S(3), fe_ACC);
}

static void fe_draw_preview_pane(canvas_t *cv, const struct fe_model *m, float px, float py, float pw, float ph)
{
    ui_round(cv, FE_S(px), FE_S(py), FE_S(pw), FE_S(ph), FE_S(DS_RADIUS_LG), DS_BG_SURFACE);
    ui_outline(cv, FE_S(px), FE_S(py), FE_S(pw), FE_S(ph), FE_S(DS_RADIUS_LG), FE_S(1), WITH_ALPHA(DS_BORDER_SUBTLE, 140));
    ui_fill(cv, FE_SI(px + 12), FE_SI(py), FE_SI(pw - 24), FE_SI(2), fe_ACC);

    fe_txt(cv, FONT_BOLD, 16, px + 16, py + 28, pw - 32, DS_TEXT_MUTED, ui_translate("Details & Preview"));
    ui_fill(cv, FE_SI(px + 14), FE_SI(py + 38), FE_SI(pw - 28), 1, WITH_ALPHA(DS_BORDER_SUBTLE, 110));

    if (m->in_roots) {
        if (m->selected < 0 || m->selected >= m->root_count) return;
        const struct fe_root *r = &m->roots[m->selected];
        fe_draw_icon(cv, r->icon_kind, px + pw * 0.5f, py + 92, 26, 0);
        fe_txt_c(cv, FONT_BOLD, 19, px + pw * 0.5f, py + 142, DS_TEXT_PRIMARY, r->name);
        fe_txt_c(cv, FONT_REGULAR, 15, px + pw * 0.5f, py + 166, DS_TEXT_SECONDARY, ui_translate(r->label));
        char fbuf[32], tbuf[32], line[64];
        fe_format_size(r->free_bytes, fbuf, sizeof(fbuf));
        fe_format_size(r->total_bytes, tbuf, sizeof(tbuf));
        if (r->total_bytes > 0) {
            snprintf(line, sizeof(line), "%s / %s", fbuf, tbuf);
            fe_txt_c(cv, FONT_REGULAR, 15, px + pw * 0.5f, py + 196, DS_TEXT_MUTED, line);
            float used = 1.0f - (float)r->free_bytes / (float)r->total_bytes;
            if (used < 0.0f) used = 0.0f;
            if (used > 1.0f) used = 1.0f;
            ui_round(cv, FE_S(px + 24), FE_S(py + 214), FE_S(pw - 48), FE_S(10), FE_S(5), DS_BG_BASE);
            ui_round(cv, FE_S(px + 24), FE_S(py + 214), FE_S((pw - 48) * used), FE_S(10), FE_S(5), fe_ACC);
        }
        return;
    }

    if (m->count <= 0 || m->selected < 0 || m->selected >= m->count) {
        fe_txt_c(cv, FONT_REGULAR, 16, px + pw * 0.5f, py + ph * 0.5f, DS_TEXT_MUTED, ui_translate("No item selected"));
        return;
    }

    const struct fe_entry *e = &m->items[m->selected];
    char dname[FE_NAME_MAX], sbuf[32], dbuf[32];
    fe_format_display_name(e, m->settings.show_extensions, dname, sizeof(dname));
    if (e->is_dir) snprintf(sbuf, sizeof(sbuf), "--");
    else fe_format_size(e->size, sbuf, sizeof(sbuf));
    fe_format_date(e->mtime_sec, dbuf, sizeof(dbuf));

    float box_y = py + 50;
    float box_h = 152;
    if (m->preview.kind == FE_PREV_IMAGE && m->preview.pixels && m->preview.img_w > 0 && m->preview.img_h > 0) {
        ui_round(cv, FE_S(px + 14), FE_S(box_y), FE_S(pw - 28), FE_S(box_h), FE_S(DS_RADIUS_MD), DS_BG_BASE);
        float max_iw = pw - 36.0f, max_ih = box_h - 8.0f;
        float dw = (float)m->preview.img_w, dh = (float)m->preview.img_h;
        float sc = max_iw / dw;
        if (dh * sc > max_ih) sc = max_ih / dh;
        dw *= sc; dh *= sc;
        ui_picture_region(cv, m->preview.pixels, m->preview.img_w, m->preview.img_h,
                          0, 0, m->preview.img_w, m->preview.img_h,
                          FE_S(px + 18 + (max_iw - dw) * 0.5f),
                          FE_S(box_y + 4 + (max_ih - dh) * 0.5f),
                          FE_S(dw), FE_S(dh), 255, m->preview.generation);
    } else if (m->preview.kind == FE_PREV_TEXT && m->preview.text[0]) {
        ui_round(cv, FE_S(px + 14), FE_S(box_y), FE_S(pw - 28), FE_S(box_h), FE_S(DS_RADIUS_MD), DS_BG_BASE);
        cv_clip(cv, FE_SI(px + 20), FE_SI(box_y + 6), FE_SI(pw - 40), FE_SI(box_h - 12));
        font_draw_wrapped(cv, FONT_REGULAR, FE_S(13), FE_SI(px + 22), FE_SI(box_y + 22), FE_SI(pw - 44), FE_S(17), DS_TEXT_SECONDARY, m->preview.text);
        cv_unclip(cv);
    } else if (m->preview.kind == FE_PREV_XEX) {
        ui_round(cv, FE_S(px + 14), FE_S(box_y), FE_S(pw - 28), FE_S(box_h), FE_S(DS_RADIUS_MD), DS_BG_BASE);
        fe_draw_icon(cv, FE_ICON_XEX, px + pw * 0.5f, box_y + 52, 22, 0);
        fe_txt_c(cv, FONT_BOLD, 15, px + pw * 0.5f, box_y + 98,
                 m->preview.xex.is_xex2 ? DS_STATUS_OK : DS_STATUS_WARN,
                 ui_translate(m->preview.xex.is_xex2 ? "Verified XEX2 executable" : "Invalid XEX2 header"));
        if (m->preview.xex.is_xex2) {
            char flg[48];
            snprintf(flg, sizeof(flg), "Flags 0x%08lX", (unsigned long)m->preview.xex.module_flags);
            fe_txt_c(cv, FONT_REGULAR, 13, px + pw * 0.5f, box_y + 122, DS_TEXT_MUTED, flg);
        }
    } else {
        ui_round(cv, FE_S(px + 14), FE_S(box_y), FE_S(pw - 28), FE_S(box_h), FE_S(DS_RADIUS_MD), DS_BG_BASE);
        fe_draw_icon(cv, e->icon_kind, px + pw * 0.5f, box_y + 64, 28, 0);
        if (m->preview.status[0])
            fe_txt_c(cv, FONT_REGULAR, 14, px + pw * 0.5f, box_y + 126, DS_TEXT_MUTED, ui_translate(m->preview.status));
    }

    float iy = box_y + box_h + 26;
    fe_txt(cv, FONT_BOLD, 18, px + 16, iy, pw - 32, DS_TEXT_PRIMARY, dname);
    iy += 28;
    fe_txt(cv, FONT_REGULAR, 15, px + 16, iy, 88, DS_TEXT_MUTED, ui_translate("Type"));
    fe_txt(cv, FONT_REGULAR, 15, px + 104, iy, pw - 120, DS_TEXT_SECONDARY, ui_translate(e->type_label));
    iy += 24;
    fe_txt(cv, FONT_REGULAR, 15, px + 16, iy, 88, DS_TEXT_MUTED, ui_translate("Size"));
    fe_txt(cv, FONT_REGULAR, 15, px + 104, iy, pw - 120, DS_TEXT_SECONDARY, sbuf);
    iy += 24;
    fe_txt(cv, FONT_REGULAR, 15, px + 16, iy, 88, DS_TEXT_MUTED, ui_translate("Modified"));
    fe_txt(cv, FONT_REGULAR, 15, px + 104, iy, pw - 120, DS_TEXT_SECONDARY, dbuf);
    iy += 24;
    {
        char attr[32];
        snprintf(attr, sizeof(attr), "%s%s%s",
                 e->is_readonly ? "RO " : "RW ",
                 e->is_hidden ? "Hidden " : "",
                 e->is_system ? "Sys" : "");
        fe_txt(cv, FONT_REGULAR, 15, px + 16, iy, 88, DS_TEXT_MUTED, ui_translate("Flags"));
        fe_txt(cv, FONT_REGULAR, 15, px + 104, iy, pw - 120, DS_TEXT_MUTED, attr);
    }
    if (m->preview.kind == FE_PREV_IMAGE && m->preview.orig_w > 0) {
        iy += 24;
        fe_txt(cv, FONT_REGULAR, 14, px + 16, iy, 88, DS_TEXT_MUTED, ui_translate("Image"));
        fe_txt(cv, FONT_REGULAR, 14, px + 104, iy, pw - 120, DS_TEXT_SECONDARY, m->preview.status);
    }
}

void fe_view_draw(const char *title, const struct fe_model *m)
{
    if (!m) return;
    const char *crumb = m->in_roots ? ui_translate("Storage Drives") : m->current_dir;
    ui_surface_begin(title ? ui_translate(title) : ui_translate("File Explorer"), crumb);
    fe_sync_metrics();
    canvas_t *cv = ui_canvas();

    bool wide = fe_W >= 1100.0f;
    bool show_prev = m->settings.show_preview_pane && fe_W >= 900.0f;
    int vis_rows = 8, cols = 1;
    fe_view_metrics(wide, m->settings.view_mode, show_prev, &vis_rows, &cols);

    float top_y = 180.0f;
    float footer_h = 56.0f;
    float content_h = fe_H - top_y - footer_h - 14.0f;
    float left_x = UI_SAFE_X;
    float full_w = fe_W - 2.0f * UI_SAFE_X;
    float prev_w = show_prev ? (wide ? 320.0f : 250.0f) : 0.0f;
    float list_w = show_prev ? (full_w - prev_w - 16.0f) : full_w;
    float prev_x = left_x + list_w + 16.0f;

    /* Status / View / Sort bar */
    {
        char meta[128];
        int total = fe_model_visible_count(m);
        if (m->in_roots) {
            snprintf(meta, sizeof(meta), "%d %s", total, ui_translate(total == 1 ? "drive" : "drives"));
        } else {
            snprintf(meta, sizeof(meta), "%s  |  %s (%s)%s",
                     ui_translate(fe_view_mode_name(m->settings.view_mode)),
                     ui_translate(fe_sort_by_name(m->settings.sort_by)),
                     m->settings.sort_dir == FE_SORT_DESC ? "Z-A" : "A-Z",
                     m->truncated ? "  |  2048+" : "");
        }
        fe_txt(cv, FONT_REGULAR, 14, left_x + 4, 174, list_w - 190, DS_TEXT_MUTED, meta);
        if (total > 0) {
            char pos[32];
            snprintf(pos, sizeof(pos), "%d / %d", m->selected + 1, total);
            fe_txt_r(cv, FONT_BOLD, 15, left_x + list_w - 4, 174, DS_TEXT_SECONDARY, pos);
        }
        if (m->marked_count > 0 || m->clip_count > 0) {
            char badge[64];
            if (m->marked_count > 0 && m->clip_count > 0)
                snprintf(badge, sizeof(badge), "%d %s  |  %s %d", m->marked_count, ui_translate("selected"),
                         ui_translate(m->clip_op == FE_CLIP_MOVE ? "Move" : "Copy"), m->clip_count);
            else if (m->marked_count > 0)
                snprintf(badge, sizeof(badge), "%d %s", m->marked_count, ui_translate("selected"));
            else
                snprintf(badge, sizeof(badge), "%s: %d", ui_translate(m->clip_op == FE_CLIP_MOVE ? "Move" : "Copy"), m->clip_count);
            fe_txt(cv, FONT_BOLD, 14, show_prev ? prev_x + 4 : left_x + 260, 174, 240, fe_ACC, badge);
        }
    }

    /* Main list/grid panel */
    ui_round(cv, FE_S(left_x), FE_S(top_y), FE_S(list_w), FE_S(content_h), FE_S(DS_RADIUS_LG), DS_BG_SURFACE);
    ui_outline(cv, FE_S(left_x), FE_S(top_y), FE_S(list_w), FE_S(content_h), FE_S(DS_RADIUS_LG), FE_S(1), WITH_ALPHA(DS_BORDER_SUBTLE, 140));

    int total = fe_model_visible_count(m);
    if (total == 0) {
        const char *msg = m->error_msg[0] ? ui_translate(m->error_msg)
                                          : ui_translate(m->in_roots ? "No accessible storage devices were found." : "This folder is empty.");
        fe_draw_icon(cv, FE_ICON_FOLDER, left_x + list_w * 0.5f, top_y + content_h * 0.42f, 26, DS_TEXT_MUTED);
        fe_txt_c(cv, FONT_BOLD, 19, left_x + list_w * 0.5f, top_y + content_h * 0.56f, DS_TEXT_SECONDARY, msg);
    } else if (m->in_roots) {
        float row_h = 56.0f;
        int max_vis = (int)((content_h - 16.0f) / row_h);
        if (max_vis < 1) max_vis = 1;
        int first = m->scroll_top;
        if (m->selected < first) first = m->selected;
        if (m->selected >= first + max_vis) first = m->selected - max_vis + 1;
        int end = first + max_vis;
        if (end > m->root_count) end = m->root_count;

        cv_clip(cv, FE_SI(left_x + 6), FE_SI(top_y + 6), FE_SI(list_w - 18), FE_SI(content_h - 12));
        for (int i = first; i < end; i++) {
            float ry = top_y + 8.0f + (float)(i - first) * row_h;
            bool on = (i == m->selected);
            if (on) {
                ui_round(cv, FE_S(left_x + 8), FE_S(ry), FE_S(list_w - 26), FE_S(row_h - 4), FE_S(DS_RADIUS_MD), fe_SEL);
                ui_outline(cv, FE_S(left_x + 8), FE_S(ry), FE_S(list_w - 26), FE_S(row_h - 4), FE_S(DS_RADIUS_MD), FE_S(2), fe_ACC);
            }
            const struct fe_root *r = &m->roots[i];
            fe_draw_icon(cv, r->icon_kind, left_x + 34, ry + row_h * 0.48f, 12, 0);
            fe_txt(cv, on ? FONT_BOLD : FONT_REGULAR, 19, left_x + 60, ry + 26, list_w - 220, on ? DS_TEXT_PRIMARY : DS_TEXT_SECONDARY, r->name);
            fe_txt(cv, FONT_REGULAR, 13, left_x + 60, ry + 44, list_w - 220, DS_TEXT_MUTED, ui_translate(r->label));
            if (r->total_bytes > 0) {
                char fbuf[24];
                fe_format_size(r->free_bytes, fbuf, sizeof(fbuf));
                fe_txt_r(cv, FONT_REGULAR, 14, left_x + list_w - 26, ry + 34, DS_TEXT_SECONDARY, fbuf);
            }
        }
        cv_unclip(cv);
        fe_draw_scrollbar(cv, left_x + list_w - 11, top_y + 10, content_h - 20, m->root_count, max_vis, first);
    } else if (m->settings.view_mode == FE_VIEW_ICONS) {
        float pad = 12.0f, gap = 10.0f;
        float usable_w = list_w - 2.0f * pad - 14.0f;
        float cw = (usable_w - (float)(cols - 1) * gap) / (float)cols;
        float ch = (content_h - 2.0f * pad - (float)(vis_rows - 1) * gap) / (float)vis_rows;
        int top_row = m->scroll_top / cols;
        int total_rows = (m->count + cols - 1) / cols;
        int first_idx = top_row * cols;
        int end_idx = first_idx + vis_rows * cols;
        if (end_idx > m->count) end_idx = m->count;

        cv_clip(cv, FE_SI(left_x + 6), FE_SI(top_y + 6), FE_SI(list_w - 18), FE_SI(content_h - 12));
        for (int i = first_idx; i < end_idx; i++) {
            int rel = i - first_idx;
            int r = rel / cols, c = rel % cols;
            float cx = left_x + pad + (float)c * (cw + gap);
            float cy = top_y + pad + (float)r * (ch + gap);
            bool on = (i == m->selected);
            const struct fe_entry *e = &m->items[i];
            ui_round(cv, FE_S(cx), FE_S(cy), FE_S(cw), FE_S(ch), FE_S(DS_RADIUS_MD), on ? fe_SEL : DS_BG_ELEVATED);
            ui_outline(cv, FE_S(cx), FE_S(cy), FE_S(cw), FE_S(ch), FE_S(DS_RADIUS_MD), FE_S(on ? 3 : 1),
                       on ? fe_ACC : e->marked ? DS_STATUS_WARN : WITH_ALPHA(DS_BORDER_SUBTLE, 120));
            fe_draw_icon(cv, e->icon_kind, cx + cw * 0.5f, cy + ch * 0.38f, 20, 0);
            char dname[FE_NAME_MAX];
            fe_format_display_name(e, m->settings.show_extensions, dname, sizeof(dname));
            fe_txt_c(cv, on ? FONT_BOLD : FONT_REGULAR, 15, cx + cw * 0.5f, cy + ch - 26.0f,
                     on ? DS_TEXT_PRIMARY : DS_TEXT_SECONDARY, dname);
            if (!e->is_dir && m->settings.show_sizes) {
                char sbuf[24];
                fe_format_size(e->size, sbuf, sizeof(sbuf));
                fe_txt_c(cv, FONT_REGULAR, 12, cx + cw * 0.5f, cy + ch - 8.0f, DS_TEXT_MUTED, sbuf);
            }
        }
        cv_unclip(cv);
        fe_draw_scrollbar(cv, left_x + list_w - 11, top_y + 10, content_h - 20, total_rows, vis_rows, top_row);
    } else {
        bool details = (m->settings.view_mode == FE_VIEW_DETAILS);
        float hdr_h = details ? 32.0f : 6.0f;
        float list_top = top_y + hdr_h;
        float row_h = details ? ((content_h - hdr_h - 10.0f) / (float)vis_rows)
                              : ((content_h - hdr_h - 10.0f) / (float)vis_rows);
        if (row_h > 52.0f) row_h = 52.0f;
        bool show_date = details && list_w >= 680.0f;
        bool show_type = details && list_w >= 540.0f;
        float size_rx = show_date ? (left_x + list_w - 154.0f) : (left_x + list_w - 24.0f);
        float type_x  = size_rx - (m->settings.show_sizes ? 200.0f : 120.0f);
        float name_w  = (show_type ? type_x : (size_rx - 86.0f)) - (left_x + 56.0f);
        if (name_w < 160.0f) name_w = 160.0f;

        if (details) {
            fe_txt(cv, FONT_BOLD, 13, left_x + 54, top_y + 22, name_w, DS_TEXT_MUTED, ui_translate("Name"));
            if (show_type) fe_txt(cv, FONT_BOLD, 13, type_x, top_y + 22, 100, DS_TEXT_MUTED, ui_translate("Type"));
            if (m->settings.show_sizes) fe_txt_r(cv, FONT_BOLD, 13, size_rx, top_y + 22, DS_TEXT_MUTED, ui_translate("Size"));
            if (show_date) fe_txt_r(cv, FONT_BOLD, 13, left_x + list_w - 22, top_y + 22, DS_TEXT_MUTED, ui_translate("Modified"));
            ui_fill(cv, FE_SI(left_x + 12), FE_SI(list_top - 2), FE_SI(list_w - 24), 1, WITH_ALPHA(DS_BORDER_SUBTLE, 130));
        }

        int first = m->scroll_top;
        int end = first + vis_rows;
        if (end > m->count) end = m->count;

        cv_clip(cv, FE_SI(left_x + 6), FE_SI(list_top + 2), FE_SI(list_w - 18), FE_SI(content_h - hdr_h - 8));
        for (int i = first; i < end; i++) {
            float ry = list_top + 4.0f + (float)(i - first) * row_h;
            bool on = (i == m->selected);
            const struct fe_entry *e = &m->items[i];
            if (on) {
                ui_round(cv, FE_S(left_x + 8), FE_S(ry), FE_S(list_w - 26), FE_S(row_h - 3), FE_S(DS_RADIUS_SM), fe_SEL);
                ui_outline(cv, FE_S(left_x + 8), FE_S(ry), FE_S(list_w - 26), FE_S(row_h - 3), FE_S(DS_RADIUS_SM), FE_S(2), fe_ACC);
            } else if (e->marked) {
                ui_round(cv, FE_S(left_x + 8), FE_S(ry), FE_S(list_w - 26), FE_S(row_h - 3), FE_S(DS_RADIUS_SM), WITH_ALPHA(DS_STATUS_WARN, 32));
            }
            if (e->marked)
                ui_circle(cv, FE_S(left_x + 18), FE_S(ry + row_h * 0.5f), FE_S(4.0f), DS_STATUS_WARN);
            fe_draw_icon(cv, e->icon_kind, left_x + 34, ry + row_h * 0.48f, details ? 11.0f : 9.0f, 0);

            char dname[FE_NAME_MAX];
            fe_format_display_name(e, m->settings.show_extensions, dname, sizeof(dname));
            float text_y = ry + row_h * 0.62f;
            fe_txt(cv, on ? FONT_BOLD : FONT_REGULAR, (float)(details ? 18 : 16), left_x + 54, text_y,
                   name_w, on ? DS_TEXT_PRIMARY : e->is_hidden ? DS_TEXT_MUTED : DS_TEXT_SECONDARY, dname);

            if (show_type)
                fe_txt(cv, FONT_REGULAR, 14, type_x, text_y, 105, DS_TEXT_MUTED, ui_translate(e->type_label));
            if (m->settings.show_sizes) {
                char sbuf[24];
                if (e->is_dir) snprintf(sbuf, sizeof(sbuf), "--");
                else fe_format_size(e->size, sbuf, sizeof(sbuf));
                fe_txt_r(cv, FONT_REGULAR, 14, size_rx, text_y, DS_TEXT_SECONDARY, sbuf);
            }
            if (show_date) {
                char dbuf[24];
                fe_format_date(e->mtime_sec, dbuf, sizeof(dbuf));
                fe_txt_r(cv, FONT_REGULAR, 13, left_x + list_w - 22, text_y, DS_TEXT_MUTED, dbuf);
            }
        }
        cv_unclip(cv);
        fe_draw_scrollbar(cv, left_x + list_w - 11, list_top + 6, content_h - hdr_h - 14, m->count, vis_rows, first);
    }

    if (show_prev)
        fe_draw_preview_pane(cv, m, prev_x, top_y, prev_w, content_h);

    if (m->mode == FE_MODE_PICK_FOLDER) {
        static const struct ui_hint hints[] = {
            {"A", "Open"}, {"B", "Back"}, {"X", "Actions"}, {"Y", "View"}, {"Start", "Select folder"}, {"LB/RB", "Drives"}
        };
        ui_surface_end(hints, 6, NULL);
    } else if (wide) {
        static const struct ui_hint hints[] = {
            {"A", "Open / Select"}, {"B", "Back"}, {"X", "Actions"}, {"Y", "View"}, {"LB/RB", "Drives"}, {"LT/RT", "Page"}
        };
        ui_surface_end(hints, 6, NULL);
    } else {
        static const struct ui_hint hints[] = {
            {"A", "Open"}, {"B", "Back"}, {"X", "Actions"}, {"Y", "View"}, {"Start", "Menu"}
        };
        ui_surface_end(hints, 5, NULL);
    }
}

void fe_view_draw_preview_modal(const struct fe_model *m, int text_scroll_line)
{
    if (!m) return;
    fe_sync_metrics();
    canvas_t *cv = ui_canvas();
    ui_fill(cv, 0, 0, cv->w, cv->h, ARGB(210, 8, 11, 14));

    float mx = UI_SAFE_X, my = 56.0f, mw = fe_W - 2.0f * UI_SAFE_X, mh = fe_H - 126.0f;
    ui_round(cv, FE_S(mx), FE_S(my), FE_S(mw), FE_S(mh), FE_S(DS_RADIUS_LG), DS_BG_SURFACE);
    ui_outline(cv, FE_S(mx), FE_S(my), FE_S(mw), FE_S(mh), FE_S(DS_RADIUS_LG), FE_S(1), DS_BORDER_STRONG);
    ui_fill(cv, FE_SI(mx + 16), FE_SI(my), FE_SI(mw - 32), FE_SI(3), fe_ACC);

    const char *base = strrchr(m->preview.path, '\\');
    base = base ? base + 1 : m->preview.path;
    fe_txt(cv, FONT_BOLD, 22, mx + 22, my + 38, mw - 220, DS_TEXT_PRIMARY, base);
    fe_txt_r(cv, FONT_REGULAR, 14, mx + mw - 22, my + 38, DS_TEXT_MUTED, m->preview.status);
    ui_fill(cv, FE_SI(mx + 18), FE_SI(my + 52), FE_SI(mw - 36), 1, WITH_ALPHA(DS_BORDER_SUBTLE, 130));

    float body_y = my + 64.0f, body_h = mh - 116.0f;
    if (m->preview.kind == FE_PREV_IMAGE && m->preview.pixels && m->preview.img_w > 0 && m->preview.img_h > 0) {
        float avail_w = mw - 48.0f, avail_h = body_h;
        float dw = (float)m->preview.img_w, dh = (float)m->preview.img_h;
        float sc = avail_w / dw;
        if (dh * sc > avail_h) sc = avail_h / dh;
        dw *= sc; dh *= sc;
        ui_picture_region(cv, m->preview.pixels, m->preview.img_w, m->preview.img_h,
                          0, 0, m->preview.img_w, m->preview.img_h,
                          FE_S(mx + (mw - dw) * 0.5f), FE_S(body_y + (avail_h - dh) * 0.5f),
                          FE_S(dw), FE_S(dh), 255, m->preview.generation);
    } else if (m->preview.kind == FE_PREV_TEXT && m->preview.text[0]) {
        cv_clip(cv, FE_SI(mx + 22), FE_SI(body_y), FE_SI(mw - 44), FE_SI(body_h));
        int line = 0, cur_line = 0;
        const char *p = m->preview.text;
        while (*p && cur_line < 22) {
            const char *nl = strchr(p, '\n');
            size_t len = nl ? (size_t)(nl - p) : strlen(p);
            if (len > 0 && p[len - 1] == '\r') len--;
            if (line >= text_scroll_line) {
                char buf[220];
                if (len >= sizeof(buf)) len = sizeof(buf) - 1;
                memcpy(buf, p, len);
                buf[len] = '\0';
                fe_txt(cv, FONT_REGULAR, 15, mx + 26, body_y + 20.0f + (float)cur_line * 21.0f, mw - 56, DS_TEXT_SECONDARY, buf);
                cur_line++;
            }
            line++;
            p = nl ? nl + 1 : p + strlen(p);
        }
        cv_unclip(cv);
    } else {
        fe_txt_c(cv, FONT_REGULAR, 18, mx + mw * 0.5f, body_y + body_h * 0.5f, DS_TEXT_MUTED, ui_translate(m->preview.status));
    }

    static const struct ui_hint hints[] = {{"Up / Down", "Scroll"}, {"B", "Close preview"}};
    ui_surface_end(hints, 2, NULL);
}

void fe_view_draw_properties_modal(const struct fe_model *m, const struct fe_entry *e, const char *full_path)
{
    if (!m || !e) return;
    fe_sync_metrics();
    canvas_t *cv = ui_canvas();
    ui_fill(cv, 0, 0, cv->w, cv->h, DS_BG_SCRIM);

    float mw = fe_W - 2.0f * UI_SAFE_X;
    if (mw > 680.0f) mw = 680.0f;
    float mh = 390.0f;
    float mx = (fe_W - mw) * 0.5f, my = (fe_H - mh) * 0.5f;

    ui_round(cv, FE_S(mx), FE_S(my), FE_S(mw), FE_S(mh), FE_S(DS_RADIUS_LG), DS_BG_SURFACE);
    ui_outline(cv, FE_S(mx), FE_S(my), FE_S(mw), FE_S(mh), FE_S(DS_RADIUS_LG), FE_S(1), DS_BORDER_STRONG);
    ui_fill(cv, FE_SI(mx + 16), FE_SI(my), FE_SI(mw - 32), FE_SI(3), fe_ACC);

    fe_draw_icon(cv, e->icon_kind, mx + 44, my + 46, 16, 0);
    fe_txt(cv, FONT_BOLD, 22, mx + 74, my + 52, mw - 100, DS_TEXT_PRIMARY, ui_translate("Properties"));
    ui_fill(cv, FE_SI(mx + 20), FE_SI(my + 74), FE_SI(mw - 40), 1, WITH_ALPHA(DS_BORDER_SUBTLE, 120));

    char sbuf[32], dbuf[32], abuf[48];
    if (e->is_dir) snprintf(sbuf, sizeof(sbuf), "--");
    else fe_format_size(e->size, sbuf, sizeof(sbuf));
    fe_format_date(e->mtime_sec, dbuf, sizeof(dbuf));
    snprintf(abuf, sizeof(abuf), "%s%s%s", e->is_readonly ? "Read-only " : "Writable ",
             e->is_hidden ? "| Hidden " : "", e->is_system ? "| System" : "");

    const char *keys[] = {"Name", "Path", "Type", "Size", "Modified", "Attributes"};
    const char *vals[] = {e->name, full_path ? full_path : "", ui_translate(e->type_label), sbuf, dbuf, abuf};
    for (int i = 0; i < 6; i++) {
        float ry = my + 114.0f + (float)i * 38.0f;
        fe_txt(cv, FONT_REGULAR, 16, mx + 26, ry, 130, DS_TEXT_MUTED, ui_translate(keys[i]));
        fe_txt(cv, i == 0 ? FONT_BOLD : FONT_REGULAR, 16, mx + 164, ry, mw - 190, DS_TEXT_PRIMARY, vals[i]);
    }
    static const struct ui_hint hints[] = {{"A", "Close"}, {"B", "Back"}};
    ui_surface_end(hints, 2, NULL);
}

void fe_view_draw_progress_modal(const char *action, const char *item_name, const struct fe_op_progress *p)
{
    fe_sync_metrics();
    canvas_t *cv = ui_canvas();
    ui_fill(cv, 0, 0, cv->w, cv->h, DS_BG_SCRIM);

    float mw = fe_W - 2.0f * UI_SAFE_X;
    if (mw > 580.0f) mw = 580.0f;
    float mh = 210.0f;
    float mx = (fe_W - mw) * 0.5f, my = (fe_H - mh) * 0.5f;

    ui_round(cv, FE_S(mx), FE_S(my), FE_S(mw), FE_S(mh), FE_S(DS_RADIUS_LG), DS_BG_SURFACE);
    ui_outline(cv, FE_S(mx), FE_S(my), FE_S(mw), FE_S(mh), FE_S(DS_RADIUS_LG), FE_S(1), DS_BORDER_STRONG);
    ui_fill(cv, FE_SI(mx + 16), FE_SI(my), FE_SI(mw - 32), FE_SI(3), fe_ACC);

    fe_txt(cv, FONT_BOLD, 21, mx + 24, my + 44, mw - 48, DS_TEXT_PRIMARY, action ? ui_translate(action) : "");
    fe_txt(cv, FONT_REGULAR, 16, mx + 24, my + 78, mw - 48, DS_TEXT_SECONDARY, item_name ? item_name : "");

    float ratio = 0.0f;
    if (p && p->total_bytes > 0) ratio = (float)p->total_done / (float)p->total_bytes;
    else if (p && p->item_total > 0) ratio = (float)p->item_done / (float)p->item_total;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;

    ui_round(cv, FE_S(mx + 24), FE_S(my + 112), FE_S(mw - 48), FE_S(14), FE_S(7), DS_BG_BASE);
    if (ratio > 0.0f)
        ui_round(cv, FE_S(mx + 24), FE_S(my + 112), FE_S((mw - 48) * ratio), FE_S(14), FE_S(7), fe_ACC);

    if (p) {
        char done_s[24], tot_s[24], line[96];
        fe_format_size(p->total_done, done_s, sizeof(done_s));
        fe_format_size(p->total_bytes, tot_s, sizeof(tot_s));
        snprintf(line, sizeof(line), "%d / %d  (%s / %s)",
                 p->current_item, p->total_items > 0 ? p->total_items : 1, done_s, tot_s);
        fe_txt(cv, FONT_REGULAR, 14, mx + 24, my + 150, mw - 48, DS_TEXT_MUTED, line);
    }
    fe_txt_r(cv, FONT_REGULAR, 14, mx + mw - 24, my + 184, DS_TEXT_MUTED, ui_translate("B: Cancel"));
}
