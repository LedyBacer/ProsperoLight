/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "moonlight_config.hpp"

#include <cstdint>
#include <vector>

namespace launcher
{

// What the player chose to stream, and how.
struct Selection
{
    char host[MOONLIGHT_CONFIG_ADDRESS_SIZE] = {};
    std::uint16_t host_port = 0;
    char app_name[64] = {};
    int app_id = 0;
    unsigned bitrate_kbps = 20000;
    unsigned display_area = MOONLIGHT_DISPLAY_AREA_TV_SAFE;
    unsigned video_codec = MOONLIGHT_VIDEO_CODEC_H264;
    unsigned stream_resolution = MOONLIGHT_STREAM_RESOLUTION_1080P;
    unsigned stream_fps = MOONLIGHT_STREAM_FPS_60;
    unsigned hdr_enabled = 0;
    unsigned audio_configuration = MOONLIGHT_AUDIO_STEREO;
    unsigned vsync_enabled = 1;
    unsigned decoder_pipeline = MOONLIGHT_DECODER_PIPELINE_CLASSIC;
    unsigned decoder_cores = MOONLIGHT_DECODER_CORES_DEFAULT;
    // The connecting screen as the launcher last drew it, without the fill of
    // its bar (connecting_plate.hpp): the stream keeps showing it. Empty when
    // it could not be read back; the stream then starts from a black screen.
    std::vector<std::uint8_t> connecting_rgba;
    float connecting_bar[4] = {}; // x, y, width, height, in pixels of the picture
    std::uint8_t connecting_fill[3] = {};
    float connecting_progress = 0.0f;
};

enum class Result
{
    failed,       // the display or the fonts could not be opened
    start_stream, // *selection says what to stream
};

// Opens the display, the controller and the sound, runs the launcher until
// the player starts a stream, then closes all three again: the stream owns
// the display, the controllers and the audio port while it runs.
// stream_error is what ended the last stream, or an empty text.
Result Run(Selection *selection, const char *stream_error, bool first_start);

} // namespace launcher
