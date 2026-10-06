/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <stdbool.h>

#include <Limelight.h>

#include "certgen.h"
#include "mini_xml.h"
#include "host_launch_options.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define GS_UNIQUE_ID "0123456789ABCDEF"

    typedef struct
    {
        SERVER_INFORMATION server_info;
        char address[128];
        unsigned short http_port;
        unsigned short https_port;
        bool paired;
        int current_game;
        int server_major_version;
        int server_codec_mode_support;
        bool is_nvidia_software;
        unsigned host_capabilities;
        char hostname[64];
        char unique_id[48];
        char app_version[32];
        char gfe_version[32];
        char server_certificate_pem[8192];
        char rtsp_session_url[256];
        client_identity_t *identity;
    } gs_server_t;

    int gs_init(gs_server_t *server, client_identity_t *identity, const char *address,
                unsigned short http_port);
    int gs_pair(gs_server_t *server, const char pin[4]);
    int gs_unpair(gs_server_t *server);
    int gs_applist(gs_server_t *server, app_entry_t **list);
    int gs_start_app(gs_server_t *server, STREAM_CONFIGURATION *configuration, int app_id,
                     bool sops, bool local_audio, int gamepad_mask,
                     const gs_host_options_t *options);
    int gs_quit_app(gs_server_t *server);
    int gs_refresh_status(gs_server_t *server);

#ifdef __cplusplus
}
#endif
