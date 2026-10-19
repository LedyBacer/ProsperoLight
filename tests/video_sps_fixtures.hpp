/*
 * ps5-native-app-boilerplate - Native video lifecycle.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include "video_stream_requirements.hpp"
#include <vector>
namespace fixture
{
using moonlight::video::Codec;
struct Writer
{
    std::vector<uint8_t> bytes;
    unsigned bit{};
    void put(uint32_t value, unsigned n = 1)
    {
        while (n--)
        {
            if (!(bit % 8))
                bytes.push_back(0);
            bytes.back() |= ((value >> n) & 1) << (7 - bit % 8);
            ++bit;
        }
    }
    void zero(unsigned n)
    {
        while (n--)
            put(0);
    }
    void ue(uint32_t value)
    {
        unsigned n = 0;
        for (auto v = value + 1; v > 1; v >>= 1)
            ++n;
        zero(n);
        put(value + 1, n + 1);
    }
    std::vector<uint8_t> nal(Codec codec)
    {
        put(1);
        while (bit % 8)
            put(0);
        std::vector<uint8_t> result = codec == Codec::H264
                                          ? std::vector<uint8_t>{0, 0, 0, 1, 0x67}
                                          : std::vector<uint8_t>{0, 0, 0, 1, 0x42, 1};
        unsigned zeros = 0;
        for (auto v : bytes)
        {
            if (zeros >= 2 && v <= 3)
            {
                result.push_back(3);
                zeros = 0;
            }
            result.push_back(v);
            zeros = v == 0 ? zeros + 1 : 0;
        }
        return result;
    }
};
inline std::vector<uint8_t> h264(unsigned width, unsigned height, unsigned refs, unsigned level,
                                 bool vui = true, unsigned dpb = 10, unsigned parameter_id = 0,
                                 unsigned poc_bits_minus4 = 0)
{
    Writer w;
    w.put(100, 8);
    w.put(0, 8);
    w.put(level, 8);
    w.ue(parameter_id);
    w.ue(1);
    w.ue(0);
    w.ue(0);
    w.put(0);
    w.put(0);
    w.ue(0);
    w.ue(0);
    w.ue(poc_bits_minus4);
    w.ue(refs);
    w.put(0);
    w.ue(width / 16 - 1);
    w.ue((height + 15) / 16 - 1);
    w.put(1);
    w.put(1);
    const unsigned crop = ((height + 15) / 16 * 16 - height) / 2;
    w.put(crop ? 1 : 0);
    if (crop)
    {
        w.ue(0);
        w.ue(0);
        w.ue(0);
        w.ue(crop);
    }
    w.put(vui);
    if (vui)
    {
        w.zero(8);
        w.put(1);
        w.put(1);
        w.ue(0);
        w.ue(0);
        w.ue(10);
        w.ue(10);
        w.ue(0);
        w.ue(dpb);
    }
    return w.nal(Codec::H264);
}
inline std::vector<uint8_t> hevc(unsigned width, unsigned height, unsigned depth,
                                 unsigned buffering, unsigned level = 153)
{
    Writer w;
    w.zero(4);
    w.put(0, 3);
    w.put(1);
    w.zero(3);
    w.put(depth == 10 ? 2 : 1, 5);
    w.zero(32);
    w.zero(48);
    w.put(level, 8);
    w.ue(0);
    w.ue(1);
    w.ue(width);
    w.ue(height);
    w.put(0);
    w.ue(depth - 8);
    w.ue(depth - 8);
    w.ue(4);
    w.put(0);
    w.ue(buffering - 1);
    w.ue(0);
    w.ue(0);
    w.ue(0);
    w.ue(3);
    w.ue(0);
    w.ue(3);
    w.ue(0);
    w.ue(0);
    w.put(0);
    w.zero(2);
    w.put(0);
    w.ue(0);
    w.put(0);
    w.zero(2);
    w.put(0);
    w.put(0);
    return w.nal(Codec::Hevc);
}

// Independently encoded synthetic black frames (libx264/libx265 via FFmpeg).
// Expected syntax was cross-checked with FFmpeg trace_headers; no encoder is
// needed when running these tests. See decoder lifecycle validation notes.
inline constexpr std::array<uint8_t, 26> encoded_h264_9refs{
    0x67, 0x64, 0x00, 0x33, 0xac, 0x56, 0x28, 0x02, 0x80, 0x0b, 0x5b, 0x01, 0x10,
    0x00, 0x00, 0x03, 0x00, 0x10, 0x00, 0x00, 0x07, 0x80, 0xf1, 0x83, 0x18, 0xa8};
inline constexpr std::array<uint8_t, 43> encoded_hevc_main{
    0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, 0x90, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x03, 0x00, 0x7b, 0xa0, 0x03, 0xc0, 0x80, 0x11, 0x07, 0xcb, 0x96, 0x56, 0x54, 0xa4, 0xc2,
    0xf0, 0x16, 0x80, 0x80, 0x00, 0x00, 0x03, 0x00, 0x80, 0x00, 0x00, 0x1e, 0x04};
inline constexpr std::array<uint8_t, 45> encoded_hevc_main10{
    0x42, 0x01, 0x01, 0x02, 0x20, 0x00, 0x00, 0x03, 0x00, 0x90, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x03, 0x00, 0x7b, 0xa0, 0x03, 0xc0, 0x80, 0x11, 0x07, 0xca, 0xd9, 0x65, 0x65, 0x4a, 0x4c,
    0x2f, 0x01, 0x68, 0x08, 0x00, 0x00, 0x03, 0x00, 0x08, 0x00, 0x00, 0x03, 0x01, 0xe0, 0x40};
// FFmpeg 8.1.1 / libx264, six synthetic black 1080p frames; see review validation note.
inline constexpr std::array<uint8_t, 28> encoded_h264_baseline_1ref{
    0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0xc0, 0x2a, 0xda, 0x01, 0xe0, 0x08, 0x9f, 0x97,
    0x01, 0x10, 0x00, 0x00, 0x03, 0x00, 0x10, 0x00, 0x00, 0x07, 0x80, 0xf1, 0x83, 0x2a};
inline constexpr std::array<uint8_t, 31> encoded_h264_high_8refs{
    0x00, 0x00, 0x00, 0x01, 0x67, 0x64, 0x00, 0x33, 0xac, 0x56, 0x24, 0x07, 0x80, 0x22, 0x7e, 0x5c,
    0x04, 0x40, 0x00, 0x00, 0x03, 0x00, 0x40, 0x00, 0x00, 0x1e, 0x03, 0xc6, 0x0c, 0x62, 0x60};
} // namespace fixture
