/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef MOONLIGHT_BACKEND_HPP
#define MOONLIGHT_BACKEND_HPP

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOONLIGHT_BACKEND_MAX_APPS 64
#define MOONLIGHT_BACKEND_PAIR_TIMEOUT_SECONDS 120

typedef enum moonlight_backend_pair_state {
    MOONLIGHT_BACKEND_PAIR_IDLE = 0,
    MOONLIGHT_BACKEND_PAIR_PREPARING,
    MOONLIGHT_BACKEND_PAIR_WAITING,
    MOONLIGHT_BACKEND_PAIR_SUCCEEDED,
    MOONLIGHT_BACKEND_PAIR_FAILED
} moonlight_backend_pair_state_t;

typedef struct moonlight_backend_app {
    int id;
    uint32_t hdr_supported;
    uint32_t app_collector_game;
    char name[64];
} moonlight_backend_app_t;

typedef struct moonlight_backend_snapshot {
    int result;
    uint32_t online;
    uint32_t paired;
    uint16_t https_port;
    uint16_t http_port;
    int current_app_id;
    uint32_t hevc_supported;
    uint32_t main10_supported;
    uint32_t app_count;
    char host[128];
    char name[64];
    char unique_id[48];
    char server_version[32];
    char error[128];
    moonlight_backend_app_t apps[MOONLIGHT_BACKEND_MAX_APPS];
} moonlight_backend_snapshot_t;

/* http_port is the PC's Sunshine port; 0 selects the default. */
int moonlight_backend_refresh(const char *host, uint16_t http_port,
                              moonlight_backend_snapshot_t *snapshot);
int moonlight_backend_pair_start(const char *host, uint16_t http_port);
moonlight_backend_pair_state_t moonlight_backend_pair_poll(
    moonlight_backend_snapshot_t *snapshot, char pin[5]);
int moonlight_backend_unpair(const char *host, uint16_t http_port,
                             moonlight_backend_snapshot_t *snapshot);
int moonlight_backend_stop_app(const char *host, uint16_t http_port,
                               moonlight_backend_snapshot_t *snapshot);
int moonlight_backend_fetch_app_artwork(const char *host,
                                        uint16_t https_port, int app_id);
const unsigned char *moonlight_backend_find_app_artwork(int app_id,
                                                        size_t *size);
void moonlight_backend_clear_app_artwork(void);

#ifdef __cplusplus
}
#endif

#endif
