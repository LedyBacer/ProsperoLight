/*
 * ps5-native-app-boilerplate - Optional Moonlight host launch extensions.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
typedef struct gs_host_options
{
    bool extensions;
    int vrr;             // -1: omit, 0: off, 1: on
    int virtual_display; // -1: omit, 0: physical, 1: virtual
    unsigned scale;
    uint16_t playstation_mask;
} gs_host_options_t;
static inline void gs_host_query(char *out, size_t capacity, const gs_host_options_t *options,
                                 unsigned gamepad_mask)
{
    if (!capacity)
        return;
    out[0] = 0;
    if (!options || !options->extensions)
        return;
    char vrr[40] = {0}, display[40] = {0}, scale[40] = {0};
    if (options->vrr == 0 || options->vrr == 1)
        snprintf(vrr, sizeof(vrr), "&clientVrrRequested=%d", options->vrr);
    if (options->virtual_display == 0 || options->virtual_display == 1)
        snprintf(display, sizeof(display), "&virtualDisplay=%d", options->virtual_display);
    if (options->scale >= 50 && options->scale < 100)
        snprintf(scale, sizeof(scale), "&scaleFactor=%u", options->scale);
    snprintf(out, capacity, "&psmap=%u%s%s%s",
             (unsigned)options->playstation_mask & gamepad_mask & 0xffffu, vrr, display, scale);
}
