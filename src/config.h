#pragma once

#include <stdbool.h>
#include <stddef.h>

enum config_language { CONFIG_LANGUAGE_ENGLISH, CONFIG_LANGUAGE_SPANISH,
                       CONFIG_LANGUAGE_PT_BR, CONFIG_LANGUAGE_COUNT };

struct config {
    char host[64];
    char host_uuid[80]; /* selected host identity; never owned by stream profiles */
    int width, height, fps;
    int bitrate_kbps;
    int packet_size;
    int slice_count;    /* diagnostic override: 0 Auto, 8, 12 or 16; next session */
    bool fast_decode;
    int renderer;       /* enum video_renderer: 0 GPU, 1 CPU, 2 CPU + display colour matrix */
    bool vsync;
    bool sops;          /* let the host change its resolution to match */
    int quit_combo;     /* 0: LB+RB+L3+R3; 1: legacy combo, both held 2 s */
    int mouse_speed,mouse_acceleration,mouse_scroll_ms; /* global input options */
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
    int adaptive_quality; /* 0 manual, 1 bounded automatic reconnect on sustained pressure */
    int display_mode; /* render target: 0 auto, 1 480p, 2 720p, 3 1080p; next app start */
    int mouse_hud,stream_menu_style; /* global: compact/text/hidden HUD; compact/sidebar menu */
    bool ui_motion; /* bounded local menu focus animation; global */
    int ui_background,ui_accent; /* global appearance, never owned by stream profiles */
    int language; /* 0 English, 1 Spanish, 2 Brazilian Portuguese; persisted */
    int thread_layout; /* 0 legacy, 1 balanced; next connection */
    int decode_workers; /* 3 or 4; requires rebuilt FFmpeg mapping */
    int left_min, left_max, right_min, right_max; /* axial activation/saturation percentages */
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
#define CONFIG_PROFILE_COUNT 6
const char *config_profile_key(int profile);
const char *config_preset_name(int preset);
int config_step_bitrate(int bitrate, int direction);
bool config_slices_supported(int slices);
int config_cycle_slices(int slices, int direction);

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
