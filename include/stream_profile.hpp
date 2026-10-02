// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "moonlight_config.hpp"
#include "Limelight.h"
namespace moonlight {
enum class DecoderBackend { VideoDec2, PyroWaveRadv };
struct ResolvedStreamProfile {
    uint32_t codec;
    int video_format;
    uint32_t chroma, bit_depth, hdr;
    int capability;
    DecoderBackend decoder_backend;
    const char *name;
    const char *format_name;
};
inline ResolvedStreamProfile resolve_stream_profile(uint32_t codec, uint32_t chroma, bool hdr) {
    if (codec == MOONLIGHT_VIDEO_CODEC_PYROWAVE) {
        const bool full = chroma == MOONLIGHT_CHROMA_444;
        return {codec, hdr ? (full ? VIDEO_FORMAT_PYROWAVE_HDR10_444 : VIDEO_FORMAT_PYROWAVE_HDR10)
                          : (full ? VIDEO_FORMAT_PYROWAVE_444 : VIDEO_FORMAT_PYROWAVE),
            full ? MOONLIGHT_CHROMA_444 : MOONLIGHT_CHROMA_420, hdr ? 10u : 8u, hdr ? 1u : 0u,
            hdr ? (full ? SCM_PYROWAVE_HDR10_444 : SCM_PYROWAVE_HDR10) : (full ? SCM_PYROWAVE_444 : SCM_PYROWAVE),
            DecoderBackend::PyroWaveRadv,
            hdr ? (full ? "PyroWave / 4:4:4 / HDR10" : "PyroWave / 4:2:0 / HDR10")
                : (full ? "PyroWave / 4:4:4 / SDR" : "PyroWave / 4:2:0 / SDR"),
            hdr ? (full ? "VIDEO_FORMAT_PYROWAVE_HDR10_444" : "VIDEO_FORMAT_PYROWAVE_HDR10")
                : (full ? "VIDEO_FORMAT_PYROWAVE_444" : "VIDEO_FORMAT_PYROWAVE")};
    }
    if (codec == MOONLIGHT_VIDEO_CODEC_HEVC)
        return {codec, hdr ? VIDEO_FORMAT_H265_MAIN10 : VIDEO_FORMAT_H265, MOONLIGHT_CHROMA_420,
            hdr ? 10u : 8u, hdr ? 1u : 0u, hdr ? SCM_HEVC_MAIN10 : SCM_HEVC,
            DecoderBackend::VideoDec2, hdr ? "HEVC Main10 / 4:2:0 / HDR10" : "HEVC Main / 4:2:0 / SDR",
            hdr ? "VIDEO_FORMAT_H265_MAIN10" : "VIDEO_FORMAT_H265"};
    return {MOONLIGHT_VIDEO_CODEC_H264, VIDEO_FORMAT_H264, MOONLIGHT_CHROMA_420, 8, 0, SCM_MASK_H264,
        DecoderBackend::VideoDec2, "H.264 High / 4:2:0 / SDR", "VIDEO_FORMAT_H264"};
}
}
