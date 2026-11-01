#pragma once

#include <stdbool.h>
#include <Limelight.h>
#include "crypto.h"
#include "host_compat.h"

struct gs_app {
    int id;
    char name[64];
    char uuid[80]; /* Optional Apollo-compatible <UUID>; numeric id stays on wire. */
};

struct gs_server {
    char address[64];
    char uuid[80];
    struct gs_host_metadata metadata;
    volatile int *cancel; /* worker-owned cancellation; artwork supplies its own */
    char mac[18];
    int info_timeout_ms; /* probe only; zero retains the existing timeout */
    volatile int *wake_cancel; /* only for the bounded plain-HTTP wake probe */
    char hostname[64];          /* PC name from serverinfo, may be empty */
    unsigned short http_port, https_port;
    bool paired;
    int current_game;
    bool launch_resumed; /* actual /resume decision after refreshed serverinfo */
    int server_major_version;
    bool is_nvidia;
    char app_version[32];
    char gfe_version[32];
    char *rtsp_session_url;     /* from launch/resume, freed by gs_server_free */
    int codec_mode_support;
    SERVER_INFORMATION info;    /* filled for LiStartConnection */
};

/* Last human-readable error from any gs_* call. */
const char *gs_error(void);
enum gs_error_code gs_error_kind(void);
void gs_set_request_cancel(volatile int *cancel);

bool gs_init(const struct client_identity *id);
bool gs_server_info(struct gs_server *srv, const char *address);
bool gs_server_info_port(struct gs_server *srv,const char *address,unsigned short port,int timeout_ms);
bool gs_wake_probe(struct gs_server *srv,const char *address,unsigned short port,int timeout_ms,volatile int *cancel);
bool gs_pair(struct gs_server *srv, const char *pin);
bool gs_unpair(struct gs_server *srv);
int  gs_applist(struct gs_server *srv, struct gs_app *apps, int max_apps);
bool gs_launch(struct gs_server *srv, STREAM_CONFIGURATION *cfg, int app_id, bool sops, bool local_audio);
bool gs_quit(struct gs_server *srv);
/* Fetches an app's box art (PNG) over HTTPS; *data is malloc'd. */
bool gs_app_art(struct gs_server *srv, int app_id, unsigned char **data, size_t *len);
void gs_server_free(struct gs_server *srv);
/* After transport joins: release session URL/tokens, preserve host/app identity. */
void gs_stream_reset(struct gs_server *srv);
