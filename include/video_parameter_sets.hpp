/*
 * ps5-native-app-boilerplate - Bounded parameter-set activation tracking.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include "video_stream_requirements.hpp"
namespace moonlight::video
{
// Only the bounded prefix needed for PPS/slice IDs. No full slice decoding.
inline bool parameter_ids(Codec codec, const uint8_t *p, size_t n, bool picture, uint32_t &id,
                          uint32_t &sps)
{
    if (!p || !n)
        return false;
    const size_t limit = std::min<size_t>(n, picture ? 4096 : 16390);
    for (size_t i = 0; i < limit; ++i)
    {
        size_t start = i;
        if (i == 0 && p[0] != 0)
            start = 0;
        else if (i + 3 < limit && !p[i] && !p[i + 1] && p[i + 2] == 1)
            start = i + 3;
        else
            continue;
        const unsigned header = codec == Codec::H264 ? 1 : 2;
        if (start + header >= limit || (p[start] & 128))
            return false;
        const unsigned type = codec == Codec::H264 ? p[start] & 31 : (p[start] >> 1) & 63;
        const bool wanted = picture ? (codec == Codec::H264 ? type == 1 || type == 5 : type <= 31)
                                    : type == (codec == Codec::H264 ? 8u : 34u);
        if (!wanted)
            continue;
        Bits b(p + start + header, std::min<size_t>(128, limit - start - header));
        if (picture)
        {
            if (codec == Codec::H264)
            {
                b.ue();
                b.ue(9);
            }
            else
            {
                b.read(1);
                if (type >= 16 && type <= 23)
                    b.read(1);
            }
            id = b.ue(codec == Codec::H264 ? 255 : 63);
        }
        else
        {
            id = b.ue(codec == Codec::H264 ? 255 : 63);
            sps = b.ue(codec == Codec::H264 ? 31 : 15);
        }
        return b.ok;
    }
    return false;
}
struct ParameterSets
{
    std::array<Requirements, 32> sps{};
    std::array<uint8_t, 256> pps_sps{};
    std::array<bool, 256> pps_valid{};
    bool advertised_change{};
    void clear()
    {
        *this = {};
    }
    void remember(const Requirements &r)
    {
        sps[r.parameter_id] = r;
    }
    bool pps(Codec codec, const uint8_t *p, size_t n)
    {
        uint32_t id{}, parent{};
        if (!parameter_ids(codec, p, n, false, id, parent))
            return false;
        pps_sps[id] = parent;
        pps_valid[id] = true;
        return true;
    }
    const Requirements *picture(Codec codec, const uint8_t *p, size_t n) const
    {
        uint32_t id{}, unused{};
        if (!parameter_ids(codec, p, n, true, id, unused) || !pps_valid[id])
            return nullptr;
        const auto &r = sps[pps_sps[id]];
        return r.verified ? &r : nullptr;
    }
};
} // namespace moonlight::video
