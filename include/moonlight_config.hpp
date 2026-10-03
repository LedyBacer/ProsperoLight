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
#define MOONLIGHT_CONFIG_DEFAULT_HTTP_PORT 47989U
#define MOONLIGHT_DISPLAY_AREA_TV_SAFE 0U
#define MOONLIGHT_DISPLAY_AREA_FULL 1U
#define MOONLIGHT_VIDEO_CODEC_H264 0U
#define MOONLIGHT_VIDEO_CODEC_HEVC 1U
#define MOONLIGHT_VIDEO_CODEC_PYROWAVE 2U
#define MOONLIGHT_CHROMA_420 0U
#define MOONLIGHT_CHROMA_444 1U
#define MOONLIGHT_STREAM_RESOLUTION_1080P 0U
#define MOONLIGHT_STREAM_RESOLUTION_1440P 1U
#define MOONLIGHT_STREAM_RESOLUTION_2160P 2U
#define MOONLIGHT_STREAM_FPS_MIN 30U
#define MOONLIGHT_STREAM_FPS_MAX 120U
#define MOONLIGHT_STREAM_FPS_60 60U
#define MOONLIGHT_STREAM_FPS_90 90U
#define MOONLIGHT_STREAM_FPS_120 120U
#define MOONLIGHT_AUDIO_STEREO 0U
#define MOONLIGHT_AUDIO_51_SURROUND 1U
#define MOONLIGHT_DECODER_PIPELINE_CLASSIC 0U
#define MOONLIGHT_DECODER_PIPELINE_ADAPTIVE 1U
#define MOONLIGHT_DECODER_CORES_MIN 3U
#define MOONLIGHT_DECODER_CORES_DEFAULT 3U
#define MOONLIGHT_DECODER_CORES_MAX 5U

    typedef struct moonlight_config_host
    {
        char address[MOONLIGHT_CONFIG_ADDRESS_SIZE];
        char name[MOONLIGHT_CONFIG_NAME_SIZE];
        char unique_id[MOONLIGHT_CONFIG_UNIQUE_ID_SIZE];
        uint32_t manual;
        /* Sunshine's "Port" setting: the HTTP port every other port derives from. */
        uint32_t http_port;
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
        uint32_t chroma_sampling;
        moonlight_config_host_t hosts[MOONLIGHT_CONFIG_MAX_HOSTS];
    } moonlight_config_t;

    void moonlight_config_defaults(moonlight_config_t *config);
    bool moonlight_config_load(moonlight_config_t *config);
    bool moonlight_config_save(const moonlight_config_t *config);
    /* A PC is one Sunshine endpoint: an address and a port. http_port 0 means
       the port is not known: a saved PC at that address keeps its port and a
       new one gets the default. */
    int moonlight_config_upsert_host(moonlight_config_t *config, const char *address,
                                     uint16_t http_port, const char *name, const char *unique_id,
                                     bool manual);
    bool moonlight_config_remove_host(moonlight_config_t *config, uint32_t index);
    /* Returns the PC's new index (a PC already saved at that endpoint is merged), or -1. */
    int moonlight_config_set_host_port(moonlight_config_t *config, uint32_t index,
                                       uint16_t http_port);
    uint16_t moonlight_config_host_port(const moonlight_config_host_t *host);
    /* "192.168.1.50" or "192.168.1.50:48989"; without a port, *http_port is 0. */
    bool moonlight_config_parse_endpoint(const char *text,
                                         char address[MOONLIGHT_CONFIG_ADDRESS_SIZE],
                                         uint16_t *http_port);
    bool moonlight_config_parse_port(const char *text, uint16_t *http_port);

#ifdef __cplusplus
}
#endif

#endif
