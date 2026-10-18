/*
 * ps5-native-app-boilerplate / ProsperoLight - Stream-aware native video
 * decoding. Copyright (C) 2026 BlackBearReloaded SPDX-License-Identifier:
 * GPL-3.0-or-later
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
                                 bool vui = true, unsigned dpb = 10)
{
    Writer w;
    w.put(100, 8);
    w.put(0, 8);
    w.put(level, 8);
    w.ue(0);
    w.ue(1);
    w.ue(0);
    w.ue(0);
    w.put(0);
    w.put(0);
    w.ue(0);
    w.ue(0);
    w.ue(0);
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
} // namespace fixture
