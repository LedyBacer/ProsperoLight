/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef MOONLIGHT_STREAM_HPP
#define MOONLIGHT_STREAM_HPP

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct moonlight_stream_options {
    const char *host;
    uint16_t host_port; /* Sunshine port of that PC; 0 selects the default */
    const char *app_name;
    int app_id;
    uint32_t bitrate_kbps;
    uint32_t synthetic_motion;
    uint32_t display_area;
    uint32_t video_codec;
    uint32_t stream_resolution;
    uint32_t stream_fps;
    uint32_t hdr_enabled;
    uint32_t audio_configuration;
    uint32_t vsync_enabled;
    uint32_t decoder_pipeline; /* MOONLIGHT_DECODER_PIPELINE_* */
    uint32_t decoder_cores;    /* 3-5 physical cores for Videodec2 workers */
} moonlight_stream_options_t;

typedef struct moonlight_stream_metrics {
    int result;
    uint32_t presented_frames;
    uint32_t access_units;
    uint32_t pending_frames;
    uint32_t audio_packets;
    uint32_t audio_overruns;
    uint32_t controller_polls;
    uint32_t controller_errors;
    uint32_t hdr_active;
    uint32_t hdr_transitions;
    uint64_t callback_to_flip_average_us;
    char error[192];
} moonlight_stream_metrics_t;

int moonlight_stream_run(const moonlight_stream_options_t *options,
                         moonlight_stream_metrics_t *metrics);

#ifdef __cplusplus
}
#endif

#endif
