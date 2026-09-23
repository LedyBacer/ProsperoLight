/*
 * ps5-native-app-boilerplate / ProsperoLight - Bounded UDP adapter counters.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ps5_network_metrics {
    uint64_t packets, bytes, receive_errors, poll_calls, heap_polls;
    uint64_t buffer_requests, buffer_failures, last_buffer_requested, last_buffer_actual;
} ps5_network_metrics_t;
void ps5_network_metrics_begin(int enabled);
ps5_network_metrics_t ps5_network_metrics_read(void);
#ifdef __cplusplus
}
#endif
