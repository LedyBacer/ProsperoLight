/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef MOONLIGHT_CONFIG_HPP
#define MOONLIGHT_CONFIG_HPP

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define MOONLIGHT_CONFIG_MAX_HOSTS 8U
#define MOONLIGHT_CONFIG_ADDRESS_SIZE 64U
#define MOONLIGHT_CONFIG_NAME_SIZE 64U
#define MOONLIGHT_CONFIG_UNIQUE_ID_SIZE 48U
#define MOONLIGHT_DISPLAY_AREA_TV_SAFE 0U
#define MOONLIGHT_DISPLAY_AREA_FULL 1U
#define MOONLIGHT_VIDEO_CODEC_H264 0U
#define MOONLIGHT_VIDEO_CODEC_HEVC 1U
#define MOONLIGHT_STREAM_RESOLUTION_1080P 0U
#define MOONLIGHT_STREAM_RESOLUTION_1440P 1U
#define MOONLIGHT_STREAM_RESOLUTION_2160P 2U
#define MOONLIGHT_STREAM_FPS_60 60U
#define MOONLIGHT_STREAM_FPS_90 90U
#define MOONLIGHT_STREAM_FPS_120 120U
#define MOONLIGHT_AUDIO_STEREO 0U
#define MOONLIGHT_AUDIO_51_SURROUND 1U
#define MOONLIGHT_DECODER_PIPELINE_ADAPTIVE 0U
#define MOONLIGHT_DECODER_PIPELINE_CLASSIC 1U
#define MOONLIGHT_DECODER_CORES_MIN 3U
#define MOONLIGHT_DECODER_CORES_DEFAULT 5U
#define MOONLIGHT_DECODER_CORES_MAX 5U

    typedef struct moonlight_config_host
    {
        char address[MOONLIGHT_CONFIG_ADDRESS_SIZE];
        char name[MOONLIGHT_CONFIG_NAME_SIZE];
        char unique_id[MOONLIGHT_CONFIG_UNIQUE_ID_SIZE];
        uint32_t manual;
    } moonlight_config_host_t;

    typedef struct moonlight_config
    {
        uint32_t host_count;
        uint32_t selected_host;
        uint32_t bitrate_mbps;
        uint32_t display_area;
        uint32_t video_codec;
        uint32_t stream_resolution;
        uint32_t stream_fps;
        uint32_t hdr_enabled;
        uint32_t audio_configuration;
        uint32_t vsync_enabled;
        uint32_t decoder_pipeline;
        uint32_t decoder_cores;
        moonlight_config_host_t hosts[MOONLIGHT_CONFIG_MAX_HOSTS];
    } moonlight_config_t;

    void moonlight_config_defaults(moonlight_config_t *config);
    bool moonlight_config_load(moonlight_config_t *config);
    bool moonlight_config_save(const moonlight_config_t *config);
    int moonlight_config_upsert_host(moonlight_config_t *config, const char *address,
                                     const char *name, const char *unique_id, bool manual);
    bool moonlight_config_remove_host(moonlight_config_t *config, uint32_t index);

#ifdef __cplusplus
}
#endif

#endif
