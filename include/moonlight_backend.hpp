/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef MOONLIGHT_BACKEND_HPP
#define MOONLIGHT_BACKEND_HPP

#include <stddef.h>
#include <stdint.h>

#include <cstring>
#include <tuple>
#include <vector>

#ifdef __cplusplus
extern "C"
{
#endif

#define MOONLIGHT_BACKEND_PAIR_TIMEOUT_SECONDS 120

    typedef enum moonlight_backend_pair_state
    {
        MOONLIGHT_BACKEND_PAIR_IDLE = 0,
        MOONLIGHT_BACKEND_PAIR_PREPARING,
        MOONLIGHT_BACKEND_PAIR_WAITING,
        MOONLIGHT_BACKEND_PAIR_SUCCEEDED,
        MOONLIGHT_BACKEND_PAIR_FAILED
    } moonlight_backend_pair_state_t;

    typedef struct moonlight_backend_app
    {
        int id;
        uint32_t hdr_supported;
        uint32_t app_collector_game;
        char name[64];

        bool operator==(const moonlight_backend_app &other) const
        {
            return id == other.id && hdr_supported == other.hdr_supported &&
                   app_collector_game == other.app_collector_game &&
                   std::strcmp(name, other.name) == 0;
        }
    } moonlight_backend_app_t;

    typedef struct moonlight_backend_snapshot
    {
        int result;
        uint32_t online;
        uint32_t paired;
        uint16_t https_port;
        uint16_t http_port;
        int current_app_id;
        uint32_t hevc_supported;
        uint32_t main10_supported;
        uint32_t pyrowave_profiles;
        uint32_t host_capabilities;
        uint32_t app_count;
        char host[128];
        char name[64];
        char unique_id[48];
        char server_version[32];
        char error[128];
        // The host's library can contain hundreds of titles. Store the whole
        // parsed list; copying a snapshot owns its entries independently.
        std::vector<moonlight_backend_app_t> apps;

        bool operator==(const moonlight_backend_snapshot &other) const
        {
            return std::tie(result, online, paired, https_port, http_port, current_app_id,
                            hevc_supported, main10_supported, pyrowave_profiles, host_capabilities,
                            app_count) == std::tie(other.result, other.online, other.paired,
                                                   other.https_port, other.http_port,
                                                   other.current_app_id, other.hevc_supported,
                                                   other.main10_supported, other.pyrowave_profiles,
                                                   other.host_capabilities, other.app_count) &&
                   std::strcmp(host, other.host) == 0 && std::strcmp(name, other.name) == 0 &&
                   std::strcmp(unique_id, other.unique_id) == 0 &&
                   std::strcmp(server_version, other.server_version) == 0 &&
                   std::strcmp(error, other.error) == 0 && apps == other.apps;
        }
    } moonlight_backend_snapshot_t;

    /* http_port is the PC's Sunshine port; 0 selects the default. */
    int moonlight_backend_refresh(const char *host, uint16_t http_port,
                                  moonlight_backend_snapshot_t *snapshot);
    int moonlight_backend_pair_start(const char *host, uint16_t http_port);
    moonlight_backend_pair_state_t
    moonlight_backend_pair_poll(moonlight_backend_snapshot_t *snapshot, char pin[5]);
    int moonlight_backend_unpair(const char *host, uint16_t http_port,
                                 moonlight_backend_snapshot_t *snapshot);
    int moonlight_backend_stop_app(const char *host, uint16_t http_port,
                                   moonlight_backend_snapshot_t *snapshot);
    int moonlight_backend_fetch_app_artwork(const char *host, uint16_t https_port, int app_id);
    const unsigned char *moonlight_backend_find_app_artwork(int app_id, size_t *size);
    void moonlight_backend_clear_app_artwork(void);

#ifdef __cplusplus
}
#endif

#endif
