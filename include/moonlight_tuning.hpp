/*
 * ps5-native-app-boilerplate / ProsperoLight - Bounded development policies.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include <cstddef>
#include <cstdint>

#ifndef VIDEO_SLICES_PER_FRAME
#define VIDEO_SLICES_PER_FRAME 4
#endif
#ifndef DECODER_PIPELINE_DEPTH
#define DECODER_PIPELINE_DEPTH 1
#endif
#ifndef DECODER_CPU_AFFINITY
#define DECODER_CPU_AFFINITY 0x3f
#endif
#ifndef DECODER_CPU_PRIORITY
#define DECODER_CPU_PRIORITY 700
#endif
#ifndef PRESENT_EVERY_N
#define PRESENT_EVERY_N 1
#endif
#ifndef FRAME_PACING
#define FRAME_PACING 0
#endif
#ifndef INPUT_POLL_US
#define INPUT_POLL_US 4000
#endif

static_assert(VIDEO_SLICES_PER_FRAME >= 1 && VIDEO_SLICES_PER_FRAME <= 8);
static_assert(DECODER_PIPELINE_DEPTH >= 1 && DECODER_PIPELINE_DEPTH <= 2);
static_assert(DECODER_CPU_AFFINITY > 0 && (DECODER_CPU_AFFINITY & ~0x3f) == 0);
static_assert(DECODER_CPU_PRIORITY >= 700 && DECODER_CPU_PRIORITY <= 767);
static_assert(PRESENT_EVERY_N == 1 || PRESENT_EVERY_N == 2 || PRESENT_EVERY_N == 4);
static_assert(FRAME_PACING == 0 || FRAME_PACING == 1);
static_assert(INPUT_POLL_US >= 1000 && INPUT_POLL_US <= 4000);

namespace moonlight
{
// Poll work is part of the interval, not extra latency added to it. Always
// yield after an overrun, never spin or replay obsolete input samples.
inline uint32_t input_poll_delay(uint64_t start, uint64_t now, uint32_t interval)
{
    if (now < start)
        return interval;
    const uint64_t spent = now - start;
    return spent < interval ? static_cast<uint32_t>(interval - spent) : 100u;
}

// Header-only observation, no compressed picture retention. Counts Annex-B
// VCL NALs (ordinary H.264/HEVC slices); not a WPP/tile capability claim.
inline unsigned count_video_slices(const uint8_t *data, size_t bytes, bool hevc)
{
    unsigned count = 0;
    if (!data)
        return 0;
    for (size_t i = 0; i + 3 < bytes; ++i)
    {
        if (data[i] || data[i + 1] || data[i + 2] != 1)
            continue;
        const unsigned type = hevc ? (data[i + 3] >> 1) & 63u : data[i + 3] & 31u;
        if (hevc ? (i + 4 < bytes && type <= 31) : (type == 1 || type == 5))
            ++count;
        i += 2;
    }
    return count;
}

// One-owner, no extra queue. Cap the wait at one interval; abandon obsolete
// deadlines rather than sleeping to repay missed slots or catch up in bursts.
struct FramePacer
{
    uint64_t next_us{};
    unsigned rate{}, remainder{};
    uint32_t delay(uint64_t now, unsigned fps, bool behind, unsigned refresh_x100 = 0)
    {
        if (!fps || fps > 120)
        {
            *this = {};
            return 0;
        }
        unsigned selected = fps * 100u;
        if (refresh_x100 && refresh_x100 < selected)
            selected = refresh_x100;
        if (selected < 1000)
            selected = fps * 100u;
        if (selected != rate)
        {
            *this = {};
            rate = selected;
        }
        const uint64_t numerator = 100000000u + remainder;
        const uint64_t interval = numerator / rate;
        remainder = numerator % rate;
        if (behind || !next_us || now >= next_us || next_us - now > interval)
        {
            next_us = now + interval;
            return 0;
        }
        const auto wait = static_cast<uint32_t>(next_us - now);
        next_us += interval;
        return wait;
    }
};
} // namespace moonlight
