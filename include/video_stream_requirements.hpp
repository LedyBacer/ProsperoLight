/*
 * ps5-native-app-boilerplate - Native video lifecycle.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace moonlight::video
{
enum class Codec
{
    H264,
    Hevc
};
enum class ParseResult
{
    NotParameters,
    Valid,
    Invalid,
    Unsupported
};
struct Requirements
{
    Codec codec{};
    uint32_t profile{}, level{}, width{}, height{}, visible_width{}, visible_height{};
    uint32_t chroma{1}, bit_depth{8}, references{}, buffering{}, reorder{}, standard_dpb{};
    uint32_t parameter_id{}, vps_id{}, poc_type{};
    bool scaling_lists{};
    uint64_t identity{};
    bool verified{}, progressive{true};
};
// No heap allocation or unbounded syntax loops. RBSP never exceeds 16 KiB.
class Bits
{
    std::array<uint8_t, 16384> data{};
    size_t size{}, position{};

  public:
    bool ok{true};
    Bits(const uint8_t *p, size_t n)
    {
        if (!p || n == 0 || n > data.size())
        {
            ok = false;
            return;
        }
        unsigned zeros = 0;
        for (size_t i = 0; i < n; ++i)
        {
            if (zeros >= 2 && p[i] == 3)
            {
                if (i + 1 == n || p[i + 1] > 3)
                {
                    ok = false;
                    return;
                }
                zeros = 0;
                continue;
            }
            data[size++] = p[i];
            zeros = p[i] == 0 ? zeros + 1 : 0;
        }
    }
    uint32_t read(unsigned n)
    {
        if (!ok || n > 32 || position + n > size * 8)
        {
            ok = false;
            return 0;
        }
        uint32_t v = 0;
        while (n--)
        {
            v = (v << 1) | ((data[position / 8] >> (7 - position % 8)) & 1);
            ++position;
        }
        return v;
    }
    void skip(unsigned n)
    {
        while (n > 32 && ok)
        {
            read(32);
            n -= 32;
        }
        read(n);
    }
    uint32_t ue(uint32_t maximum = 0x7ffffffeu)
    {
        unsigned zeros = 0;
        while (ok && read(1) == 0)
        {
            if (++zeros > 30)
            {
                ok = false;
                return 0;
            }
        }
        const uint32_t v = ((uint32_t(1) << zeros) - 1) + read(zeros);
        if (v > maximum)
        {
            ok = false;
            return 0;
        }
        return v;
    }
    void trailing()
    {
        if (read(1) != 1)
            ok = false;
        while (ok && position < size * 8)
            if (read(1) != 0)
                ok = false;
    }
    int32_t se()
    {
        uint32_t v = ue();
        return (v & 1) ? int32_t((v + 1) / 2) : -int32_t(v / 2);
    }
};
inline uint32_t h264_dpb_mbs(uint32_t level)
{
    switch (level)
    {
    case 9:
    case 10:
        return 396;
    case 11:
        return 900;
    case 12:
    case 13:
    case 20:
        return 2376;
    case 21:
        return 4752;
    case 22:
    case 30:
        return 8100;
    case 31:
        return 18000;
    case 32:
        return 20480;
    case 40:
    case 41:
        return 32768;
    case 42:
        return 34816;
    case 50:
        return 110400;
    case 51:
    case 52:
        return 184320;
    case 60:
    case 61:
    case 62:
        return 696320;
    default:
        return 0;
    }
}
inline uint32_t h264_max_fs(uint32_t level)
{
    switch (level)
    {
    case 9:
    case 10:
        return 99;
    case 11:
    case 12:
    case 13:
    case 20:
        return 396;
    case 21:
        return 792;
    case 22:
    case 30:
        return 1620;
    case 31:
        return 3600;
    case 32:
        return 5120;
    case 40:
    case 41:
        return 8192;
    case 42:
        return 8704;
    case 50:
        return 22080;
    case 51:
    case 52:
        return 36864;
    case 60:
    case 61:
    case 62:
        return 139264;
    default:
        return 0;
    }
}
inline uint64_t identity(const uint8_t *p, size_t n)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < n; ++i)
        hash = (hash ^ p[i]) * UINT64_C(1099511628211);
    return hash;
}
inline void h264_hrd(Bits &b)
{
    const auto count = b.ue(31) + 1;
    b.skip(8);
    for (uint32_t i = 0; b.ok && i < count; ++i)
    {
        b.ue();
        b.ue();
        b.skip(1);
    }
    b.skip(20);
}
inline ParseResult h264_sps(const uint8_t *p, size_t n, Requirements &out)
{
    Bits b(p, n);
    Requirements r{};
    r.codec = Codec::H264;
    r.profile = b.read(8);
    const auto constraints = b.read(8);
    r.level = b.read(8);
    if (constraints & 3)
        return ParseResult::Invalid;
    if (r.level == 11 && (constraints & 16) &&
        (r.profile == 66 || r.profile == 77 || r.profile == 88))
        r.level = 9;
    r.parameter_id = b.ue(31);
    switch (r.profile)
    {
    case 100:
    case 110:
    case 122:
    case 244:
    case 44:
    case 83:
    case 86:
    case 118:
    case 128:
    case 138:
    case 139:
    case 134:
    case 135:
        r.chroma = b.ue(3);
        if (r.chroma == 3)
            b.skip(1);
        r.bit_depth = b.ue(6) + 8;
        if (b.ue(6) + 8 != r.bit_depth)
            return ParseResult::Unsupported;
        b.skip(1);
        r.scaling_lists = b.read(1);
        if (r.scaling_lists)
            for (unsigned i = 0; i < (r.chroma == 3 ? 12u : 8u) && b.ok; ++i)
                if (b.read(1))
                {
                    int last = 8, next = 8;
                    for (unsigned j = 0; j < (i < 6 ? 16u : 64u) && b.ok; ++j)
                    {
                        if (next)
                            next = (last + b.se() + 256) % 256;
                        if (next)
                            last = next;
                    }
                }
        break;
    default:
        break;
    }
    b.ue(12); // log2_max_frame_num_minus4
    const auto poc = r.poc_type = b.ue(2);
    if (poc == 0)
        b.ue(12);
    else if (poc == 1)
    {
        b.skip(1);
        b.se();
        b.se();
        const auto count = b.ue(255);
        for (uint32_t i = 0; i < count && b.ok; ++i)
            b.se();
    }
    r.references = b.ue(16);
    b.skip(1);
    const uint32_t mbs_w = b.ue(511) + 1, map_h = b.ue(511) + 1;
    r.progressive = b.read(1) != 0;
    if (!r.progressive)
        b.skip(1);
    b.skip(1);
    r.width = mbs_w * 16;
    r.height = map_h * 16 * (r.progressive ? 1 : 2);
    uint32_t crop_x = 0, crop_y = 0;
    if (b.read(1))
    {
        const auto left = b.ue(8192), right = b.ue(8192), top = b.ue(8192), bottom = b.ue(8192);
        if (left || top)
            return ParseResult::Unsupported;
        crop_x = (left + right) * (r.chroma == 1 || r.chroma == 2 ? 2 : 1);
        crop_y = (top + bottom) * (r.chroma == 1 ? 2 : 1) * (r.progressive ? 1 : 2);
    }
    r.standard_dpb =
        std::min(16u, h264_dpb_mbs(r.level) / (mbs_w * map_h * (r.progressive ? 1 : 2)));
    r.buffering = r.standard_dpb;
    r.reorder = r.standard_dpb;
    if (b.read(1)) // VUI
    {
        if (b.read(1))
        {
            if (b.read(8) == 255)
                b.skip(32);
        }
        if (b.read(1))
            b.skip(1);
        if (b.read(1))
        {
            b.skip(4);
            if (b.read(1))
                b.skip(24);
        }
        if (b.read(1))
        {
            b.ue(5);
            b.ue(5);
        }
        if (b.read(1))
            b.skip(65);
        const bool nal = b.read(1);
        if (nal)
            h264_hrd(b);
        const bool vcl = b.read(1);
        if (vcl)
            h264_hrd(b);
        if (nal || vcl)
            b.skip(1);
        b.skip(1);
        if (b.read(1))
        {
            b.skip(1);
            b.ue(16);
            b.ue(16);
            b.ue(16);
            b.ue(16);
            r.reorder = b.ue(16);
            r.buffering = b.ue(16);
        }
        else if ((constraints & 16) && (r.profile == 44 || r.profile == 86 || r.profile == 100 ||
                                        r.profile == 110 || r.profile == 122 || r.profile == 244))
            r.reorder = 0;
    }
    b.trailing();
    if (!b.ok || mbs_w * map_h * (r.progressive ? 1 : 2) > h264_max_fs(r.level) ||
        crop_x >= r.width || crop_y >= r.height || !r.standard_dpb || r.references > r.buffering ||
        r.buffering > r.standard_dpb || r.reorder > r.buffering)
        return ParseResult::Invalid;
    r.visible_width = r.width - crop_x;
    r.visible_height = r.height - crop_y;
    r.verified = true;
    out = r;
    return ParseResult::Valid;
}
inline void hevc_scaling(Bits &b)
{
    for (unsigned size = 0; size < 4 && b.ok; ++size)
        for (unsigned matrix = 0; matrix < 6 && b.ok; matrix += size == 3 ? 3 : 1)
            if (!b.read(1))
                b.ue(matrix);
            else
            {
                if (size > 1)
                    b.se();
                for (unsigned j = 0; j < (size ? 64u : 16u) && b.ok; ++j)
                    b.se();
            }
}
inline void hevc_hrd(Bits &b, unsigned layers)
{
    const bool nal = b.read(1), vcl = b.read(1);
    bool sub = false;
    if (nal || vcl)
    {
        sub = b.read(1);
        if (sub)
            b.skip(19);
        b.skip(8);
        if (sub)
            b.skip(4);
        b.skip(15);
    }
    for (unsigned i = 0; i <= layers && b.ok; ++i)
    {
        const bool fixed_general = b.read(1);
        const bool fixed = fixed_general || b.read(1);
        bool low_delay = false;
        if (fixed)
            b.ue();
        else
            low_delay = b.read(1);
        const auto count = low_delay ? 1 : b.ue(31) + 1;
        for (unsigned stream = 0; stream < 2; ++stream)
            if (stream ? vcl : nal)
                for (unsigned j = 0; j < count && b.ok; ++j)
                {
                    b.ue();
                    b.ue();
                    if (sub)
                    {
                        b.ue();
                        b.ue();
                    }
                    b.skip(1);
                }
    }
}
inline void hevc_vui(Bits &b, unsigned layers, Requirements &r)
{
    if (b.read(1))
    {
        if (b.read(8) == 255)
            b.skip(32);
    }
    if (b.read(1))
        b.skip(1);
    if (b.read(1))
    {
        b.skip(4);
        if (b.read(1))
            b.skip(24);
    }
    if (b.read(1))
    {
        b.ue(5);
        b.ue(5);
    }
    b.skip(1);
    if (b.read(1))
        r.progressive = false;
    b.skip(1);
    if (b.read(1))
    {
        b.ue();
        b.ue();
        b.ue();
        b.ue();
    }
    if (b.read(1))
    {
        b.skip(64);
        if (b.read(1))
            b.ue();
        if (b.read(1))
            hevc_hrd(b, layers);
    }
    if (b.read(1))
    {
        b.skip(3);
        b.ue();
        b.ue(16);
        b.ue(16);
        b.ue(16);
        b.ue(16);
    }
}
inline void hevc_tail(Bits &b, unsigned layers, unsigned poc_bits, Requirements &r)
{
    b.ue(3);
    b.ue(3);
    b.ue(3);
    b.ue(3);
    b.ue(4);
    b.ue(4);
    r.scaling_lists = b.read(1);
    if (r.scaling_lists && b.read(1))
        hevc_scaling(b);
    b.skip(2);
    if (b.read(1))
    {
        b.skip(8);
        b.ue(3);
        b.ue(3);
        b.skip(1);
    }
    const auto count = b.ue(64);
    std::array<unsigned, 64> deltas{};
    for (unsigned i = 0; i < count && b.ok; ++i)
    {
        if (i && b.read(1))
        {
            b.skip(1);
            b.ue(32767);
            for (unsigned j = 0; j <= deltas[i - 1] && b.ok; ++j)
            {
                const bool used = b.read(1);
                if (used || b.read(1))
                    ++deltas[i];
            }
        }
        else
        {
            const auto negative = b.ue(16), positive = b.ue(16);
            deltas[i] = negative + positive;
            for (unsigned j = 0; j < deltas[i] && b.ok; ++j)
            {
                b.ue(32767);
                b.skip(1);
            }
        }
        if (deltas[i] > 16)
            b.ok = false;
    }
    if (b.read(1))
    {
        const auto long_refs = b.ue(32);
        for (unsigned i = 0; i < long_refs && b.ok; ++i)
            b.skip(poc_bits + 1);
    }
    b.skip(2);
    if (b.read(1))
        hevc_vui(b, layers, r);
    if (b.read(1))
    {
        const auto range = b.read(1), multilayer = b.read(1), three_d = b.read(1),
                   screen = b.read(1), reserved = b.read(4);
        // Main/Main10 do not need multilayer, 3D or screen-content extensions.
        if (multilayer || three_d || screen || reserved)
        {
            b.ok = false;
            return;
        }
        if (range)
            b.skip(9);
    }
    b.trailing();
}
inline ParseResult hevc_sps(const uint8_t *p, size_t n, Requirements &out)
{
    Bits b(p, n);
    Requirements r{};
    r.codec = Codec::Hevc;
    r.vps_id = b.read(4);
    const auto layers = b.read(3);
    if (layers > 6)
        return ParseResult::Invalid;
    b.skip(1);
    b.skip(3);
    r.profile = b.read(5);
    b.skip(32);
    const bool progressive = b.read(1), interlaced = b.read(1);
    r.progressive = progressive || !interlaced;
    b.skip(46);
    r.level = b.read(8);
    bool sub_profile[7]{}, sub_level[7]{};
    for (unsigned i = 0; i < layers; ++i)
    {
        sub_profile[i] = b.read(1);
        sub_level[i] = b.read(1);
    }
    if (layers)
        b.skip((8 - layers) * 2);
    for (unsigned i = 0; i < layers; ++i)
    {
        if (sub_profile[i])
            b.skip(88);
        if (sub_level[i])
            b.skip(8);
    }
    r.parameter_id = b.ue(15);
    r.chroma = b.ue(3);
    if (r.chroma == 3)
        b.skip(1);
    r.width = b.ue(8192);
    r.height = b.ue(8192);
    uint32_t crop_x = 0, crop_y = 0;
    if (b.read(1))
    {
        const auto left = b.ue(8192), right = b.ue(8192), top = b.ue(8192), bottom = b.ue(8192);
        if (left || top)
            return ParseResult::Unsupported;
        crop_x = (left + right) * (r.chroma == 1 || r.chroma == 2 ? 2 : 1);
        crop_y = (top + bottom) * (r.chroma == 1 ? 2 : 1);
    }
    r.bit_depth = b.ue(8) + 8;
    if (b.ue(8) + 8 != r.bit_depth)
        return ParseResult::Unsupported;
    const unsigned poc_bits = b.ue(12) + 4;
    const bool all = b.read(1);
    unsigned previous_buffering = 0, previous_reorder = 0;
    for (unsigned i = all ? 0 : layers; i <= layers && b.ok; ++i)
    {
        const auto buffering = b.ue(15) + 1, reorder = b.ue(15);
        b.ue();
        if (reorder >= buffering || buffering < previous_buffering || reorder < previous_reorder)
            return ParseResult::Invalid;
        previous_buffering = buffering;
        previous_reorder = reorder;
        r.buffering = std::max(r.buffering, buffering);
        r.reorder = std::max(r.reorder, reorder);
    }
    hevc_tail(b, layers, poc_bits, r);
    // HEVC A.4.2 MaxDpbSize; Main/Main10 use MaxDpbPicBuf=6.
    uint64_t max_luma = 0;
    switch (r.level)
    {
    case 30:
        max_luma = 36864;
        break;
    case 60:
        max_luma = 122880;
        break;
    case 63:
        max_luma = 245760;
        break;
    case 90:
        max_luma = 552960;
        break;
    case 93:
        max_luma = 983040;
        break;
    case 120:
    case 123:
        max_luma = 2228224;
        break;
    case 150:
    case 153:
    case 156:
        max_luma = 8912896;
        break;
    case 180:
    case 183:
    case 186:
        max_luma = 35651584;
        break;
    default:
        return ParseResult::Unsupported;
    }
    const uint64_t pixels = uint64_t(r.width) * r.height;
    r.standard_dpb = pixels <= max_luma / 4       ? 16
                     : pixels <= max_luma / 2     ? 12
                     : pixels <= max_luma * 3 / 4 ? 8
                                                  : 6;
    r.references = r.buffering; // conservative bound, not an H.264 ref count
    if (!b.ok || !r.width || !r.height || pixels > max_luma || crop_x >= r.width ||
        crop_y >= r.height || r.buffering > r.standard_dpb)
        return ParseResult::Invalid;
    r.visible_width = r.width - crop_x;
    r.visible_height = r.height - crop_y;
    r.verified = true;
    out = r;
    return ParseResult::Valid;
}
// Normalize framing only, for one bounded SPS/PPS NAL. Parameter-set RBSPs
// end with rbsp_trailing_bits: their final payload byte contains a stop bit and
// cannot be zero. Unlike VCL/CABAC NALs, no cabac_zero_word belongs to these NALs.
// With explicit Annex-B framing, final zero bytes are therefore trailing_zero_8bits.
// Bare NALs retain all bytes; this helper is not general RBSP canonicalization.
inline ParseResult parameter_nal(Codec codec, const uint8_t *&p, size_t &n)
{
    if (!p || !n || n > 16390)
        return ParseResult::Invalid;
    bool annex_b = false;
    if (n >= 4 && !p[0] && !p[1] && !p[2] && p[3] == 1)
    {
        p += 4;
        n -= 4;
        annex_b = true;
    }
    else if (n >= 3 && !p[0] && !p[1] && p[2] == 1)
    {
        p += 3;
        n -= 3;
        annex_b = true;
    }
    const unsigned header = codec == Codec::H264 ? 1 : 2;
    if (n <= header || (p[0] & 128))
        return ParseResult::Invalid;
    const unsigned type = codec == Codec::H264 ? p[0] & 31 : (p[0] >> 1) & 63;
    if (codec == Codec::H264 ? type != 7 && type != 8 : type != 33 && type != 34)
        return ParseResult::NotParameters;
    if (codec == Codec::Hevc && !(p[1] & 7))
        return ParseResult::Invalid;
    if (annex_b)
        while (n > header && p[n - 1] == 0)
            --n;
    return n > header ? ParseResult::Valid : ParseResult::Invalid;
}
// Input is one Annex-B NAL (or a bare NAL), never a whole unbounded frame.
inline ParseResult parse_sps(Codec codec, const uint8_t *p, size_t n, Requirements &out)
{
    const auto normalized = parameter_nal(codec, p, n);
    if (normalized != ParseResult::Valid)
        return normalized;
    const unsigned header = codec == Codec::H264 ? 1 : 2;
    const auto type = codec == Codec::H264 ? p[0] & 31 : (p[0] >> 1) & 63;
    if (type != (codec == Codec::H264 ? 7 : 33))
        return ParseResult::NotParameters;
    if (codec == Codec::Hevc && ((p[0] & 1) || (p[1] >> 3)))
        return ParseResult::Unsupported;
    const auto parsed = codec == Codec::H264 ? h264_sps(p + header, n - header, out)
                                             : hevc_sps(p + header, n - header, out);
    if (parsed == ParseResult::Valid)
        out.identity = identity(p, n); // Preserve the NAL header and all EBSP bytes.
    return parsed;
}
struct Limits
{
    uint32_t width{4096}, height{2304}, dpb{17}, level_h264{52}, level_hevc{186};
    uint32_t bit_depth{8}, depth{1}, output_margin{1};
};
struct Plan
{
    bool supported{};
    uint32_t dpb{}, depth{};
    const char *reason{"unverified parameters"};
};
inline Plan plan(const Requirements &r, const Limits &l)
{
    Plan p{};
    if (!r.verified)
        return p;
    p.reason = "unsupported codec profile, format or dimensions";
    if (!r.progressive || r.chroma != 1 || r.bit_depth != l.bit_depth || !r.width || !r.height ||
        r.width > l.width || r.height > l.height ||
        (r.codec == Codec::H264
             ? (r.profile != 66 && r.profile != 77 && r.profile != 100) || r.level > l.level_h264 ||
                   r.bit_depth != 8
             : (r.profile != 1 && r.profile != 2) || (r.profile != (r.bit_depth == 10 ? 2u : 1u)) ||
                   r.level > l.level_hevc))
        return p;
    p.reason = "stream exceeds native DPB capacity";
    const auto required = std::max(r.references, std::max(r.buffering, r.reorder));
    if (required > r.standard_dpb || l.output_margin > l.dpb || required > l.dpb - l.output_margin)
        return p;
    p.dpb = required + l.output_margin;
    p.depth = std::max(1u, std::min(3u, l.depth));
    p.supported = true;
    p.reason = "supported";
    return p;
}
} // namespace moonlight::video
