/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef MOONLIGHT_DISCOVERY_HPP
#define MOONLIGHT_DISCOVERY_HPP

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOONLIGHT_DISCOVERY_MAX_HOSTS 8U
#define MOONLIGHT_DISCOVERY_MAX_SERVICES 4U

typedef struct moonlight_discovered_host {
    char address[64];
    char name[64];
    uint16_t http_port; /* advertised Sunshine port; 0 when the reply carried none */
} moonlight_discovered_host_t;

/* One Sunshine instance named in an mDNS reply. */
typedef struct moonlight_discovered_service {
    char instance[128];
    char name[64];
    uint16_t http_port;
} moonlight_discovered_service_t;

uint32_t moonlight_discover_hosts(moonlight_discovered_host_t *hosts,
                                  uint32_t capacity);
uint32_t moonlight_discovery_parse_response(const uint8_t *packet, size_t length,
                                            moonlight_discovered_service_t *services,
                                            uint32_t capacity);

#ifdef __cplusplus
}
#endif

#endif
