/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "moonlight_config.hpp"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CONFIG_MAGIC UINT32_C(0x504c4346)
#define CONFIG_VERSION 7U
#define CONFIG_PATH "/download0/prosperolight-config.bin"
#define CONFIG_TEMP_PATH "/download0/prosperolight-config.tmp"
#define OPEN_READ_ONLY 0x0000
#define OPEN_WRITE_CREATE_TRUNCATE 0x0601
#define FILE_MODE_0666 0x01b6

typedef struct config_file
{
    uint32_t magic;
    uint32_t version;
    uint32_t checksum;
    uint32_t reserved;
    moonlight_config_t config;
} config_file_t;

typedef struct legacy_config_v1
{
    uint32_t host_count;
    uint32_t selected_host;
    uint32_t bitrate_mbps;
    uint32_t display_area;
    moonlight_config_host_t hosts[MOONLIGHT_CONFIG_MAX_HOSTS];
} legacy_config_v1_t;

typedef struct legacy_config_file_v1
{
    uint32_t magic;
    uint32_t version;
    uint32_t checksum;
    uint32_t reserved;
    legacy_config_v1_t config;
} legacy_config_file_v1_t;

typedef struct legacy_config_v2
{
    uint32_t host_count;
    uint32_t selected_host;
    uint32_t bitrate_mbps;
    uint32_t display_area;
    uint32_t video_codec;
    uint32_t stream_resolution;
    moonlight_config_host_t hosts[MOONLIGHT_CONFIG_MAX_HOSTS];
} legacy_config_v2_t;

typedef struct legacy_config_file_v2
{
    uint32_t magic;
    uint32_t version;
    uint32_t checksum;
    uint32_t reserved;
    legacy_config_v2_t config;
} legacy_config_file_v2_t;

typedef struct legacy_config_v3
{
    uint32_t host_count;
    uint32_t selected_host;
    uint32_t bitrate_mbps;
    uint32_t display_area;
    uint32_t video_codec;
    uint32_t stream_resolution;
    uint32_t hdr_enabled;
    moonlight_config_host_t hosts[MOONLIGHT_CONFIG_MAX_HOSTS];
} legacy_config_v3_t;

typedef struct legacy_config_file_v3
{
    uint32_t magic;
    uint32_t version;
    uint32_t checksum;
    uint32_t reserved;
    legacy_config_v3_t config;
} legacy_config_file_v3_t;

typedef struct legacy_config_v4
{
    uint32_t host_count;
    uint32_t selected_host;
    uint32_t bitrate_mbps;
    uint32_t display_area;
    uint32_t video_codec;
    uint32_t stream_resolution;
    uint32_t stream_fps;
    uint32_t hdr_enabled;
    moonlight_config_host_t hosts[MOONLIGHT_CONFIG_MAX_HOSTS];
} legacy_config_v4_t;

typedef struct legacy_config_file_v4
{
    uint32_t magic;
    uint32_t version;
    uint32_t checksum;
    uint32_t reserved;
    legacy_config_v4_t config;
} legacy_config_file_v4_t;

typedef struct legacy_config_v5
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
    moonlight_config_host_t hosts[MOONLIGHT_CONFIG_MAX_HOSTS];
} legacy_config_v5_t;

typedef struct legacy_config_file_v5
{
    uint32_t magic;
    uint32_t version;
    uint32_t checksum;
    uint32_t reserved;
    legacy_config_v5_t config;
} legacy_config_file_v5_t;

typedef struct legacy_config_v6
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
} legacy_config_v6_t;

typedef struct legacy_config_file_v6 {
    uint32_t magic, version, checksum, reserved;
    legacy_config_v6_t config;
} legacy_config_file_v6_t;

extern "C"
{
    int sceKernelOpen(const char *path, int flags, uint16_t mode);
    extern int sceKernelClose(int descriptor);
    extern int64_t sceKernelRead(int descriptor, void *buffer, size_t length);
    extern int64_t sceKernelWrite(int descriptor, const void *buffer, size_t length);
    extern int sceKernelRename(const char *from, const char *to);
    int sceKernelUnlink(const char *path);
}

static uint32_t checksum(const void *data, size_t size)
{
    const auto *bytes = static_cast<const uint8_t *>(data);
    uint32_t value = UINT32_C(2166136261);
    size_t index;

    for (index = 0; index < size; ++index)
        value = (value ^ bytes[index]) * UINT32_C(16777619);
    return value;
}

static bool read_exact(const char *path, void *data, size_t size)
{
    size_t done = 0;
    int descriptor = sceKernelOpen(path, OPEN_READ_ONLY, 0);

    if (descriptor < 0)
        return false;
    while (done < size)
    {
        int64_t count = sceKernelRead(descriptor, (uint8_t *)data + done, size - done);
        if (count <= 0)
            break;
        done += (size_t)count;
    }
    sceKernelClose(descriptor);
    return done == size;
}

static bool write_atomic(const void *data, size_t size)
{
    size_t done = 0;
    int descriptor = sceKernelOpen(CONFIG_TEMP_PATH, OPEN_WRITE_CREATE_TRUNCATE, FILE_MODE_0666);

    if (descriptor < 0)
        return false;
    while (done < size)
    {
        int64_t count = sceKernelWrite(descriptor, (const uint8_t *)data + done, size - done);
        if (count <= 0)
            break;
        done += (size_t)count;
    }
    sceKernelClose(descriptor);
    if (done != size)
    {
        sceKernelUnlink(CONFIG_TEMP_PATH);
        return false;
    }
    if (sceKernelRename(CONFIG_TEMP_PATH, CONFIG_PATH) < 0)
    {
        sceKernelUnlink(CONFIG_PATH);
        if (sceKernelRename(CONFIG_TEMP_PATH, CONFIG_PATH) < 0)
        {
            sceKernelUnlink(CONFIG_TEMP_PATH);
            return false;
        }
    }
    return true;
}

static void copy_text(char *destination, size_t capacity, const char *source)
{
    if (!capacity)
        return;
    snprintf(destination, capacity, "%s", source ? source : "");
}

void moonlight_config_defaults(moonlight_config_t *config)
{
    if (!config)
        return;
    memset(config, 0, sizeof(*config));
    config->bitrate_mbps = 20;
    config->display_area = MOONLIGHT_DISPLAY_AREA_FULL;
    config->video_codec = MOONLIGHT_VIDEO_CODEC_H264;
    config->stream_resolution = MOONLIGHT_STREAM_RESOLUTION_1080P;
    config->stream_fps = MOONLIGHT_STREAM_FPS_60;
    config->hdr_enabled = 0;
    config->audio_configuration = MOONLIGHT_AUDIO_STEREO;
    config->vsync_enabled = 1;
    config->decoder_pipeline = MOONLIGHT_DECODER_PIPELINE_CLASSIC;
    config->decoder_cores = MOONLIGHT_DECODER_CORES_DEFAULT;
}

bool moonlight_config_load(moonlight_config_t *config)
{
    config_file_t file;
    legacy_config_file_v6_t legacy_v6;
    legacy_config_file_v5_t legacy_v5;
    legacy_config_file_v4_t legacy_v4;
    legacy_config_file_v3_t legacy_v3;
    legacy_config_file_v2_t legacy_v2;
    legacy_config_file_v1_t legacy;
    uint32_t index;

    if (!config)
        return false;
    moonlight_config_defaults(config);
    if (read_exact(CONFIG_PATH, &file, sizeof(file)) && file.magic == CONFIG_MAGIC &&
        file.version == CONFIG_VERSION &&
        file.checksum == checksum(&file.config, sizeof(file.config)) &&
        file.config.host_count <= MOONLIGHT_CONFIG_MAX_HOSTS)
    {
        *config = file.config;
    }
    else if (read_exact(CONFIG_PATH, &legacy_v6, sizeof(legacy_v6)) &&
             legacy_v6.magic == CONFIG_MAGIC && legacy_v6.version == 6U &&
             legacy_v6.checksum == checksum(&legacy_v6.config, sizeof(legacy_v6.config)) &&
             legacy_v6.config.host_count <= MOONLIGHT_CONFIG_MAX_HOSTS)
    {
        // The v6 scalar prefix is unchanged; hosts move past the new field.
        memcpy(config, &legacy_v6.config, offsetof(legacy_config_v6_t, hosts));
        memcpy(config->hosts, legacy_v6.config.hosts, sizeof(config->hosts));
    }
    else if (read_exact(CONFIG_PATH, &legacy_v5, sizeof(legacy_v5)) &&
             legacy_v5.magic == CONFIG_MAGIC && legacy_v5.version == 5U &&
             legacy_v5.checksum == checksum(&legacy_v5.config, sizeof(legacy_v5.config)) &&
             legacy_v5.config.host_count <= MOONLIGHT_CONFIG_MAX_HOSTS)
    {
        config->host_count = legacy_v5.config.host_count;
        config->selected_host = legacy_v5.config.selected_host;
        config->bitrate_mbps = legacy_v5.config.bitrate_mbps;
        config->display_area = legacy_v5.config.display_area;
        config->video_codec = legacy_v5.config.video_codec;
        config->stream_resolution = legacy_v5.config.stream_resolution;
        config->stream_fps = legacy_v5.config.stream_fps;
        config->hdr_enabled = legacy_v5.config.hdr_enabled;
        config->audio_configuration = legacy_v5.config.audio_configuration;
        memcpy(config->hosts, legacy_v5.config.hosts, sizeof(config->hosts));
    }
    else if (read_exact(CONFIG_PATH, &legacy_v4, sizeof(legacy_v4)) &&
             legacy_v4.magic == CONFIG_MAGIC && legacy_v4.version == 4U &&
             legacy_v4.checksum == checksum(&legacy_v4.config, sizeof(legacy_v4.config)) &&
             legacy_v4.config.host_count <= MOONLIGHT_CONFIG_MAX_HOSTS)
    {
        config->host_count = legacy_v4.config.host_count;
        config->selected_host = legacy_v4.config.selected_host;
        config->bitrate_mbps = legacy_v4.config.bitrate_mbps;
        config->display_area = legacy_v4.config.display_area;
        config->video_codec = legacy_v4.config.video_codec;
        config->stream_resolution = legacy_v4.config.stream_resolution;
        config->stream_fps = legacy_v4.config.stream_fps;
        config->hdr_enabled = legacy_v4.config.hdr_enabled;
        memcpy(config->hosts, legacy_v4.config.hosts, sizeof(config->hosts));
    }
    else if (read_exact(CONFIG_PATH, &legacy_v3, sizeof(legacy_v3)) &&
             legacy_v3.magic == CONFIG_MAGIC && legacy_v3.version == 3U &&
             legacy_v3.checksum == checksum(&legacy_v3.config, sizeof(legacy_v3.config)) &&
             legacy_v3.config.host_count <= MOONLIGHT_CONFIG_MAX_HOSTS)
    {
        config->host_count = legacy_v3.config.host_count;
        config->selected_host = legacy_v3.config.selected_host;
        config->bitrate_mbps = legacy_v3.config.bitrate_mbps;
        config->display_area = legacy_v3.config.display_area;
        config->video_codec = legacy_v3.config.video_codec;
        config->stream_resolution = legacy_v3.config.stream_resolution;
        config->hdr_enabled = legacy_v3.config.hdr_enabled;
        memcpy(config->hosts, legacy_v3.config.hosts, sizeof(config->hosts));
    }
    else if (read_exact(CONFIG_PATH, &legacy_v2, sizeof(legacy_v2)) &&
             legacy_v2.magic == CONFIG_MAGIC && legacy_v2.version == 2U &&
             legacy_v2.checksum == checksum(&legacy_v2.config, sizeof(legacy_v2.config)) &&
             legacy_v2.config.host_count <= MOONLIGHT_CONFIG_MAX_HOSTS)
    {
        config->host_count = legacy_v2.config.host_count;
        config->selected_host = legacy_v2.config.selected_host;
        config->bitrate_mbps = legacy_v2.config.bitrate_mbps;
        config->display_area = legacy_v2.config.display_area;
        config->video_codec = legacy_v2.config.video_codec;
        config->stream_resolution = legacy_v2.config.stream_resolution;
        memcpy(config->hosts, legacy_v2.config.hosts, sizeof(config->hosts));
    }
    else
    {
        if (!read_exact(CONFIG_PATH, &legacy, sizeof(legacy)) || legacy.magic != CONFIG_MAGIC ||
            legacy.version != 1U ||
            legacy.checksum != checksum(&legacy.config, sizeof(legacy.config)) ||
            legacy.config.host_count > MOONLIGHT_CONFIG_MAX_HOSTS)
            return false;
        config->host_count = legacy.config.host_count;
        config->selected_host = legacy.config.selected_host;
        config->bitrate_mbps = legacy.config.bitrate_mbps;
        config->display_area = legacy.config.display_area;
        memcpy(config->hosts, legacy.config.hosts, sizeof(config->hosts));
    }
    for (index = 0; index < config->host_count; ++index)
    {
        config->hosts[index].address[MOONLIGHT_CONFIG_ADDRESS_SIZE - 1] = 0;
        config->hosts[index].name[MOONLIGHT_CONFIG_NAME_SIZE - 1] = 0;
        config->hosts[index].unique_id[MOONLIGHT_CONFIG_UNIQUE_ID_SIZE - 1] = 0;
    }
    if (config->host_count == 0)
        config->selected_host = 0;
    else if (config->selected_host >= config->host_count)
        config->selected_host = config->host_count - 1;
    if (config->bitrate_mbps < 1 || config->bitrate_mbps > 1000)
        config->bitrate_mbps = 20;
    if (config->display_area > MOONLIGHT_DISPLAY_AREA_FULL)
        config->display_area = MOONLIGHT_DISPLAY_AREA_FULL;
    if (config->video_codec > MOONLIGHT_VIDEO_CODEC_PYROWAVE)
        config->video_codec = MOONLIGHT_VIDEO_CODEC_H264;
    if (config->stream_resolution > MOONLIGHT_STREAM_RESOLUTION_2160P)
        config->stream_resolution = MOONLIGHT_STREAM_RESOLUTION_1080P;
    if (config->stream_fps != MOONLIGHT_STREAM_FPS_60 &&
        config->stream_fps != MOONLIGHT_STREAM_FPS_90 &&
        config->stream_fps != MOONLIGHT_STREAM_FPS_120)
        config->stream_fps = MOONLIGHT_STREAM_FPS_60;
    if (config->hdr_enabled > 1U)
        config->hdr_enabled = 0;
    if (config->audio_configuration > MOONLIGHT_AUDIO_51_SURROUND)
        config->audio_configuration = MOONLIGHT_AUDIO_STEREO;
    if (config->vsync_enabled > 1U)
        config->vsync_enabled = 1;
    if (config->decoder_pipeline > MOONLIGHT_DECODER_PIPELINE_ADAPTIVE)
        config->decoder_pipeline = MOONLIGHT_DECODER_PIPELINE_CLASSIC;
    if (config->decoder_cores < MOONLIGHT_DECODER_CORES_MIN ||
        config->decoder_cores > MOONLIGHT_DECODER_CORES_MAX)
        config->decoder_cores = MOONLIGHT_DECODER_CORES_DEFAULT;
    if (config->chroma_sampling > MOONLIGHT_CHROMA_444)
        config->chroma_sampling = MOONLIGHT_CHROMA_420;
    if (config->video_codec == MOONLIGHT_VIDEO_CODEC_H264)
        config->hdr_enabled = 0;
    return true;
}

bool moonlight_config_save(const moonlight_config_t *config)
{
    config_file_t file;

    if (!config || config->host_count > MOONLIGHT_CONFIG_MAX_HOSTS)
        return false;
    memset(&file, 0, sizeof(file));
    file.magic = CONFIG_MAGIC;
    file.version = CONFIG_VERSION;
    file.config = *config;
    file.checksum = checksum(&file.config, sizeof(file.config));
    return write_atomic(&file, sizeof(file));
}

int moonlight_config_upsert_host(moonlight_config_t *config, const char *address, const char *name,
                                 const char *unique_id, bool manual)
{
    uint32_t index;
    uint32_t duplicate;
    char input_address[MOONLIGHT_CONFIG_ADDRESS_SIZE];
    char input_name[MOONLIGHT_CONFIG_NAME_SIZE];
    char input_unique_id[MOONLIGHT_CONFIG_UNIQUE_ID_SIZE];
    moonlight_config_host_t *host;

    if (!config || !address || !address[0])
        return -1;
    copy_text(input_address, sizeof(input_address), address);
    copy_text(input_name, sizeof(input_name), name);
    copy_text(input_unique_id, sizeof(input_unique_id), unique_id);
    for (index = 0; index < config->host_count; ++index)
    {
        host = &config->hosts[index];
        if ((input_unique_id[0] && host->unique_id[0] &&
             strcmp(host->unique_id, input_unique_id) == 0) ||
            strcmp(host->address, input_address) == 0)
            goto update;
    }
    if (config->host_count >= MOONLIGHT_CONFIG_MAX_HOSTS)
        return -1;
    index = config->host_count++;
    host = &config->hosts[index];
    memset(host, 0, sizeof(*host));

update:
    copy_text(host->address, sizeof(host->address), input_address);
    if (input_name[0])
        copy_text(host->name, sizeof(host->name), input_name);
    else if (!host->name[0])
        copy_text(host->name, sizeof(host->name), "Sunshine PC");
    if (input_unique_id[0])
        copy_text(host->unique_id, sizeof(host->unique_id), input_unique_id);
    host->manual = host->manual || manual;

    duplicate = 0;
    while (duplicate < config->host_count)
    {
        moonlight_config_host_t *other = &config->hosts[duplicate];
        const bool same_address = duplicate != index && strcmp(other->address, host->address) == 0;
        const bool same_identity = duplicate != index && host->unique_id[0] &&
                                   other->unique_id[0] &&
                                   strcmp(other->unique_id, host->unique_id) == 0;
        if (!same_address && !same_identity)
        {
            ++duplicate;
            continue;
        }
        host->manual = host->manual || other->manual;
        if (config->selected_host == duplicate || config->selected_host == index)
            config->selected_host = index - (duplicate < index ? 1U : 0U);
        else if (config->selected_host > duplicate)
            --config->selected_host;
        memmove(other, other + 1, (config->host_count - duplicate - 1) * sizeof(*other));
        --config->host_count;
        if (duplicate < index)
            --index;
        host = &config->hosts[index];
    }
    return (int)index;
}

bool moonlight_config_remove_host(moonlight_config_t *config, uint32_t index)
{
    if (!config || index >= config->host_count || config->host_count > MOONLIGHT_CONFIG_MAX_HOSTS)
        return false;
    memmove(&config->hosts[index], &config->hosts[index + 1],
            (config->host_count - index - 1) * sizeof(config->hosts[0]));
    memset(&config->hosts[--config->host_count], 0, sizeof(config->hosts[0]));
    if (!config->host_count)
        config->selected_host = 0;
    else if (config->selected_host > index)
        --config->selected_host;
    else if (config->selected_host >= config->host_count)
        config->selected_host = config->host_count - 1;
    return true;
}
