#pragma once

#include <stdbool.h>
#include "fast_decode.h"
#include "video_decoder.h"
#include "desktop_options.h"
#include "file_explorer_fs.h"
#include <stddef.h>

enum config_language { CONFIG_LANGUAGE_ENGLISH, CONFIG_LANGUAGE_SPANISH,
                       CONFIG_LANGUAGE_PT_BR, CONFIG_LANGUAGE_COUNT };

/* Legacy 0 becomes Auto. Legacy 1 remains an explicit 4:3 choice. */
enum ui_layout_mode { UI_LAYOUT_AUTO, UI_LAYOUT_4_3, UI_LAYOUT_16_9, UI_LAYOUT_1_1,
                      UI_LAYOUT_5_4, UI_LAYOUT_3_2, UI_LAYOUT_16_10, UI_LAYOUT_21_9,
                      UI_LAYOUT_COUNT };

enum crt_scaling_filter { CRT_SCALE_AUTO = 0, CRT_SCALE_POINT = 1,
                          CRT_SCALE_BILINEAR = 2, CRT_SCALE_SHARP_BILINEAR = 3,
                          CRT_SCALE_COUNT };

enum crt_aspect_mode { CRT_ASPECT_FIT = 0, CRT_ASPECT_ORIGINAL = 1,
                       CRT_ASPECT_16_9 = 2, CRT_ASPECT_STRETCH = 3,
                       CRT_ASPECT_CUSTOM = 4, CRT_ASPECT_PIXEL_1_1 = 5,
                       CRT_ASPECT_CROP_4_3 = 6, CRT_ASPECT_COUNT = 7 };
#define CRT_ASPECT_4_3 CRT_ASPECT_FIT

enum crt_retro_preset { CRT_PRESET_CUSTOM = 0, CRT_PRESET_STANDARD_480 = 1,
                        CRT_PRESET_RETRO_4_3 = 2, CRT_PRESET_INTEGER_SHARP = 3,
                        CRT_PRESET_LOW_LATENCY = 4, CRT_PRESET_COUNT };

struct config {
    char host[64];
    char host_uuid[80]; /* selected host identity; never owned by stream profiles */
    int width, height, fps;
    int bitrate_kbps;
    int packet_size;
    int slice_count;    /* diagnostic override: 0 Auto, 8, 12 or 16; next session */
    int refresh_interval_ms; /* optional periodic IDR: 0, 125, 250, 1000..60000 ms */
    int fast_decode; /* On/Off; format-5 value 2 loads as On */
    int renderer;       /* enum video_renderer: 0 GPU, 1 CPU, 2 CPU + display colour matrix */
    int decoder_backend; /* global next-session preference; absent config = FFmpeg */
    bool vsync;
    bool sops;          /* let the host change its resolution to match */
    int quit_combo;     /* 0: LB+RB+L3+R3; 1: legacy combo, both held 2 s */
    bool pc_guide_enabled; /* Start + LB held 350 ms -> PC SPECIAL_FLAG; default true */
    int mouse_speed,mouse_acceleration,mouse_scroll_ms; /* global input options */
    struct desktop_options desktop;
    bool desktop_start;
    char desktop_host[80];
    int desktop_app; /* explicit host + app ID binding; never inferred from title */
    int stick_deadzone; /* 0..30 percent; default 0 preserves input */
    bool session_report; /* write last-session.txt after stopping, never during video */
    bool stats_overlay;  /* optional SDK telemetry panel, off by default */
    bool diagnostic_log; /* optional RAM log + lifecycle checkpoints, off by default */
    int overlay_mode; /* 0 Off, 1 Basic, 2 Advanced, 3 Developer */
    int image_quality; /* existing Xenos shader: 0 Auto Quality, 1 Fast, 2 Quality */
    bool history_enabled; /* bounded session history, written after stop */
    int audio_mode; /* 0 stereo (default), 1 request 5.1 with output fallback */
    bool local_audio;   /* also play audio on the host PC */
    int stream_profile; /* 0 Custom. Factory selection stores resolution/FPS/bitrate;
                         * Smooth/Performance also have effective fast decoder/GPU defaults.
                         * Custom edits capture those defaults; user profiles own stream_options. */
    char user_profile[64]; /* separate namespace; displayed only while options match */
    bool launch_animation; /* independent of the legacy focus animation */
    int launch_cover_ms; /* global 0..10000; not part of stream profiles */
    bool mdns_discovery;
    int adaptive_quality; /* 0 Off, 1 sustained-evidence recommendations; no automatic reconnect */
    int display_mode; /* render target: 0 auto, 1 widescreen 480p, 2 720p, 3 1080p, 4 640x480; next app start */
    int ui_layout; /* enum ui_layout_mode */
    int ui_safe_area; /* 0..10 percent per edge; UI only, next app start */
    int refresh_smoothing,smoothing_strength,gpu_post_deblock; /* 0 Off / 1 experimental; gpu_post_deblock Off by default */
    bool crt_clarity; /* optional spatial interlace shader; never temporal IDR blending */
    int crt_scaling; /* enum crt_scaling_filter */
    int crt_aspect;  /* enum crt_aspect_mode */
    int crt_integer_scale; /* 0 Off, 1 Integer Scaling */
    int crt_size_x,crt_size_y; /* 80..110 percent (100 = default) */
    int crt_overscan; /* 0..10 percent per edge (game picture only, independent of ui_safe_area) */
    int crt_pos_x,crt_pos_y; /* bounded picture alignment offset (-20..20 px) */
    int crt_origin_preset; /* tracks CRT preset provenance across derived Custom profiles */
    int frame_pacing_mode; /* enum frame_pacing_mode: 0 Baseline default, 1..5 experiments */
    bool crt_display_backup;
    int previous_display_mode,previous_ui_layout,previous_safe_area;
    int mouse_hud,stream_menu_style; /* global: compact/text/hidden HUD; compact/sidebar menu */
    bool ui_motion; /* bounded local menu focus animation; global */
    int ui_background,ui_accent; /* global appearance, never owned by stream profiles */
    bool menu_sounds;
    int menu_volume; /* independent menu PCM volume, 0..100 */
    int language; /* 0 English, 1 Spanish, 2 Brazilian Portuguese; persisted */
    int thread_layout; /* 0 legacy, 1 balanced; next connection */
    int decode_workers; /* 3 or 4; requires rebuilt FFmpeg mapping */
    int left_min, left_max, right_min, right_max; /* axial activation/saturation percentages */
    bool aurora_enabled,aurora_direct_enabled;
    bool artwork_cache_enabled;
    int artwork_cache_mb; /* 32, 64, or 128; disk only */
    int aurora_cache_handling; /* 0 Safe, 1 Manual; neither writes Aurora DB */
    char aurora_shortcuts_folder[160],aurora_main_xex[160],aurora_path[160];
    char aurora_default_profile[80]; /* empty = game/current binding */
    struct fe_settings explorer;
};

/* Finds a writable USB/HDD device and creates <dev>:/moonlight/. */
bool storage_init(void);
/* Directory for config and keys, e.g. "uda0:/moonlight", or NULL. */
const char *storage_dir(void);

/* Reads/writes an entire file; returns NULL/false on failure.
 * Reads set errno: ENOENT only for missing files, other errors block profile saves. */
char *storage_read(const char *name, size_t *len);
bool storage_write(const char *name, const void *data, size_t len);

void config_defaults(struct config *c);
void config_load(struct config *c);
bool config_last_resolution_rejected(int *width,int *height,const char **reason);
bool config_save(const struct config *c);
size_t config_serialize(const struct config *c,char *text,size_t capacity);
/* Reset preferences atomically, preserving the selected PC and pairing files. */
bool config_reset_preferences(struct config *c);

/* Explicit stream profiles; never applied during config_load. */
void config_apply_preset(struct config *c, int preset);
int config_video_preset(const struct config *c);
/* Leave a factory preset without silently changing its effective decoder/shader. */
void config_customize_stream(struct config *c);
/* Request a host rate; this does not interpolate frames or alter bitrate. */
bool config_fps_supported(int fps);
int config_cycle_fps(int fps, int direction);
#define CONFIG_FPS_DEFAULT 60

#define CONFIG_BITRATE_MIN 1000
#define CONFIG_BITRATE_MAX 70000
#define CONFIG_PRESET_720_60 1
#define CONFIG_PROFILE_PERFORMANCE 2
#define CONFIG_PROFILE_QUALITY 3
#define CONFIG_PROFILE_SMOOTH CONFIG_PRESET_720_60
#define CONFIG_PROFILE_STANDARD 4
#define CONFIG_PROFILE_1080_SMOOTH 5
#define CONFIG_PROFILE_WIRELESS 6
#define CONFIG_PROFILE_CRT 7
#define CONFIG_PROFILE_CRT_SMOOTH 8
#define CONFIG_PROFILE_CRT_STABLE 9
#define CONFIG_PROFILE_CRT_QUALITY 10
#define CONFIG_PROFILE_CRT_DISPLAY 11
#define CONFIG_PROFILE_CRT_RETRO_240P 12
#define CONFIG_PROFILE_LOW_LATENCY 13
#define CONFIG_PROFILE_COUNT 14
bool config_profile_is_legacy(int preset);
bool config_profile_is_visible(int preset);
int config_visible_factory_profiles(int *out,int capacity);
bool config_is_crt_factory(int preset);
bool config_is_crt_retro(const struct config *c);
bool config_crt_profile(const struct config *c);
void config_crt_reset_settings(struct config *c);
const char *config_crt_scaling_name(int mode);
const char *config_crt_aspect_name(int mode);
const char *config_crt_preset_name(int preset);
int config_cycle_crt_scaling(int mode,int direction);
int config_cycle_crt_aspect(int mode,int direction);
int config_match_crt_preset(const struct config *c);
void config_apply_crt_preset(struct config *c,int preset);
int config_cycle_crt_preset(int preset,int direction);
void config_crt_display_enable(struct config *c);
bool config_crt_display_restore(struct config *c);
bool config_crt_display_commit(struct config *c,bool enable);
const char *config_profile_key(int profile);
const char *config_preset_name(int preset);
int config_step_bitrate(int bitrate, int direction);
bool config_slices_supported(int slices);
int config_cycle_slices(int slices, int direction);
bool config_refresh_interval_supported(int milliseconds);
int config_cycle_refresh_interval(int milliseconds, int direction);

/* Diagnostic bitrate sweep only changes requested kbps for the next session. */
int config_test_bitrate(const struct config *c,int direction);

int config_overlay_mode(const struct config *c);

/* User profiles own only stream/decoder/presentation options. Factory presets
 * remain unchanged. No host, language, input calibration or diagnostics here. */
#define CONFIG_USER_PROFILE_MAX 8
#define CONFIG_PROFILE_NAME_BYTES 64
#define CONFIG_PROFILES_BYTES 8192
struct stream_options {
    int width,height,fps,bitrate_kbps,packet_size,slice_count;
    int fast_decode,renderer,vsync,sops,local_audio,image_quality;
    int adaptive_quality,thread_layout,decode_workers,audio_mode;
    int refresh_interval_ms,crt_clarity,new_options_owned;
    int refresh_smoothing,smoothing_strength,smoothing_options_owned; /* v1/v2 snapshots leave new options unchanged */
    int crt_scaling,crt_aspect,crt_pos_x,crt_pos_y,crt_options_owned; /* v1..v5 snapshots leave CRT picture options unchanged */
    int gpu_post_deblock,post_deblock_options_owned; /* v1..v6 snapshots leave gpu_post_deblock unchanged */
    int crt_integer_scale,crt_size_x,crt_size_y,crt_overscan,frame_pacing_mode,crt_v2_options_owned; /* v1..v7 leave CRT 2.0/pacing unchanged */
};
struct user_stream_profile { char name[CONFIG_PROFILE_NAME_BYTES]; struct stream_options options; };
struct user_profiles {
    int count,slot; unsigned generation; bool read_only;
    struct user_stream_profile items[CONFIG_USER_PROFILE_MAX];
};
bool config_profile_name_valid(const char *name);
void config_profile_capture(struct stream_options *out,const struct config *c);
bool config_profiles_load(struct user_profiles *profiles);
size_t config_profiles_serialize(const struct user_profiles *profiles,char *text,size_t capacity);
bool config_profiles_save(struct user_profiles *current,const struct user_profiles *next);
bool config_profiles_create(struct user_profiles *profiles,const struct config *c,const char *name);
bool config_profiles_rename(struct user_profiles *profiles,int index,const char *name);
bool config_profiles_delete(struct user_profiles *profiles,int index);
bool config_profiles_apply(const struct user_profiles *profiles,int index,struct config *c);

int config_profiles_selected(const struct user_profiles *p,const struct config *c);
/* Five factories and valid saved profiles; no storage writes. Custom is a
 * derived unmatched state, never a synthetic selectable preset. */
bool config_cycle_profile(const struct user_profiles *p,struct config *c,int direction);
/* Canonical factory comparison for display, independent of legacy preset hint. */
int config_match_factory(const struct config *c);
void config_select_factory(struct config *c,int preset);
