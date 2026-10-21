/* ps5-native-app-boilerplate - Effective launch selection and capability checks.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include "moonlight_stream.hpp"
#include "moonlight_backend.hpp"
#include "stream_profile.hpp"
namespace moonlight
{
inline moonlight_stream_options_t saved_stream_options(const moonlight_config_t &c)
{
    moonlight_stream_options_t o{};
    o.bitrate_kbps = c.bitrate_mbps * 1000u;
    o.display_area = c.display_area;
    o.video_codec = c.video_codec;
    o.stream_resolution = c.stream_resolution;
    o.stream_fps = c.stream_fps;
    o.hdr_enabled = c.hdr_enabled;
    o.chroma_sampling = c.chroma_sampling;
    o.audio_configuration = c.audio_configuration;
    o.vsync_enabled = c.vsync_enabled;
    o.decoder_pipeline = c.decoder_pipeline;
    o.decoder_cores = c.decoder_cores;
    return o;
}
struct SelectionError
{
    const char *code{}, *title{}, *body{};
};
inline SelectionError validate_stream_selection(const moonlight_stream_options_t &o,
                                                const moonlight_backend_snapshot_t &host)
{
    if (o.video_codec == MOONLIGHT_VIDEO_CODEC_HEVC && !host.hevc_supported)
        return {"HEVC_UNSUPPORTED", "This PC cannot encode HEVC",
                "Choose H.264 in Settings, or change the encoder on the PC."};
    if (o.video_codec == MOONLIGHT_VIDEO_CODEC_PYROWAVE &&
        !(host.pyrowave_profiles &
          resolve_stream_profile(o.video_codec, o.chroma_sampling, o.hdr_enabled != 0).capability))
        return {"PYROWAVE_PROFILE_UNSUPPORTED", "This PyroWave profile is unavailable",
                "Choose a supported chroma/HDR profile or configure Vibepollo on the PC."};
    if (o.video_codec != MOONLIGHT_VIDEO_CODEC_PYROWAVE && o.hdr_enabled && !host.main10_supported)
        return {"HDR_UNSUPPORTED", "This PC cannot encode HDR",
                "Turn HDR off in Settings, or enable HEVC Main10 on the PC."};
    return {};
}
} // namespace moonlight
