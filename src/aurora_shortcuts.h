#pragma once
#include "config.h"
#include "aurora_launch.h"
#include "aurora_metadata.h"
#define AURORA_SHORTCUT_MAX 64
struct aurora_shortcut_info { struct moonx_launch_data link; char folder[240]; };
#ifdef __cplusplus
extern "C" {
#endif
int aurora_shortcut_count(void);
const struct aurora_shortcut_info *aurora_shortcut_at(int index);
int aurora_shortcut_find(const char *host_id,int app_id);
bool aurora_shortcut_create(const struct config *cfg,const char *host_id,int app_id,
                            const char *name,const char *profile_id,char *error,size_t capacity);
bool aurora_shortcut_delete(int index,char *error,size_t capacity);
bool aurora_shortcut_recreate(int index,const struct config *cfg,const char *profile_id,
                              char *error,size_t capacity);
bool aurora_shortcut_exists(int index);
bool aurora_shortcut_metadata(int index,struct moonx_aurora_metadata *out);
bool aurora_shortcut_refresh_metadata(int index,uint32_t metadata_title_id,
                                      char *error,size_t capacity);
bool aurora_shortcut_update_metadata(int index,const char *title,const char *description,
                                    uint32_t metadata_title_id,char *error,size_t capacity);
int aurora_shortcut_edit_metadata_id(char text[9]);
int aurora_shortcut_edit_metadata_text(char *text,size_t capacity,bool description);
int aurora_shortcut_clean_missing(void);
/* 1 candidate found, 0 not found, -1 cancelled by B. */
int aurora_shortcut_detect_aurora(char *path,size_t capacity);
bool aurora_shortcut_valid_aurora(const char *folder);
bool aurora_shortcut_install_sync(const char *aurora_folder,char *error,size_t capacity);
int aurora_shortcut_edit_path(char *path,size_t capacity,const char *title);
enum { AURORA_BROWSER_FOLDER=0, AURORA_BROWSER_XEX=1, AURORA_BROWSER_AURORA_XEX=2, AURORA_BROWSER_IMAGE=3, AURORA_BROWSER_MAX=192 };
struct aurora_browser_entry { char name[64]; int folder; };
int aurora_browser_roots(struct aurora_browser_entry *items,int capacity);
int aurora_browser_list(const char *folder,int mode,struct aurora_browser_entry *items,int capacity);
bool aurora_browser_create_folder(const char *parent,const char *name,char *out,size_t capacity);
bool aurora_shortcut_valid_xex(const char *path);
bool aurora_shortcut_is_current_xex(const char *path);
bool aurora_shortcut_folder_ready(const char *path);
bool aurora_shortcut_autodetect_main(char *path,size_t capacity,const char *saved,const char *shortcuts_folder);
int aurora_browser_edit_name(char *name,size_t capacity);
#ifdef __cplusplus
}
#endif
