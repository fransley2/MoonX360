#pragma once
#include "../file_explorer_model.h"

#ifdef __cplusplus
extern "C" {
#endif

void fe_view_metrics(bool wide, int view_mode, bool show_preview, int *out_rows, int *out_cols);
void fe_view_draw(const char *title, const struct fe_model *m);
void fe_view_draw_preview_modal(const struct fe_model *m, int text_scroll_line);
void fe_view_draw_properties_modal(const struct fe_model *m, const struct fe_entry *e, const char *full_path);
void fe_view_draw_progress_modal(const char *action, const char *item_name, const struct fe_op_progress *p);

#ifdef __cplusplus
}
#endif
