/*
 * ps5-native-app-boilerplate / ProsperoLight - Stream measurements and catch-up policy.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <ctime>

#ifndef PROSPEROLIGHT_PERFORMANCE_DETAIL
#define PROSPEROLIGHT_PERFORMANCE_DETAIL 0
#endif
static_assert(PROSPEROLIGHT_PERFORMANCE_DETAIL == 0 || PROSPEROLIGHT_PERFORMANCE_DETAIL == 1);

namespace moonlight
{
// Diagnostic-only, single decoder-worker writer. Stop recording when full;
// never allocate or write files in the streaming callback.
struct FrameTrace
{
    struct Sample
    {
        uint64_t receive_us{}, enqueue_us{}, callback_network_us{}, callback_us{}, pts_us{};
        uint64_t decode_us{}, ready_us{}, prior_flip_wait_us{}, submit_us{}, completion_us{};
        uint32_t frame{}, bytes{}, pending{},
            outcome{}; // 0=incomplete, 1=submitted, 2=stale, 3=decimated
    };
    static constexpr size_t capacity = 32768; // About 273 seconds at 120 FPS.
    std::array<Sample, capacity> samples{};
    size_t count{}, omitted{};

    Sample *append()
    {
        if (count == capacity)
        {
            ++omitted;
            return nullptr;
        }
        samples[count] = {};
        return &samples[count++];
    }
};

// Single-writer; read only after that stream worker has joined. Percentiles are
// 0.5 ms bucket upper bounds, with an exact maximum for the overflow bucket.
struct TimingHistogram
{
    std::array<uint64_t, 256> buckets{};
    uint64_t count{}, total_us{}, max_us{};

    void add(uint64_t us)
    {
        ++buckets[us / 500u < buckets.size() ? us / 500u : buckets.size() - 1u];
        ++count;
        total_us += us;
        if (us > max_us)
            max_us = us;
    }

    uint64_t percentile(unsigned percent) const
    {
        if (!count || !percent || percent > 100u)
            return 0;
        const uint64_t rank = (count * percent + 99u) / 100u;
        uint64_t cumulative = 0;
        for (std::size_t i = 0; i < buckets.size(); ++i)
        {
            cumulative += buckets[i];
            if (cumulative >= rank)
            {
                const uint64_t upper = (i + 1u) * 500u - 1u;
                return i == buckets.size() - 1u || upper > max_us ? max_us : upper;
            }
        }
        return max_us;
    }
};

// Optional host-clock measurements only; never call these GPU timestamps.
inline uint64_t performance_now_us()
{
    if (!PROSPEROLIGHT_PERFORMANCE_DETAIL)
        return 0;
    timespec now{};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return static_cast<uint64_t>(now.tv_sec) * 1000000u +
           static_cast<uint64_t>(now.tv_nsec) / 1000u;
}

inline void record_performance_elapsed(TimingHistogram &timing, uint64_t start_us)
{
    if (!PROSPEROLIGHT_PERFORMANCE_DETAIL)
        return;
    const uint64_t end_us = performance_now_us();
    if (start_us && end_us >= start_us)
        timing.add(end_us - start_us);
}

inline bool record_reassembly(TimingHistogram &timing, uint64_t receive_us, uint64_t enqueue_us,
                              uint64_t callback_us)
{
    // These are all upstream local-clock timestamps, not host PTS. Missing or
    // inconsistent samples must not become zero-latency measurements.
    if (!receive_us || !enqueue_us || receive_us > enqueue_us || enqueue_us > callback_us)
        return false;
    timing.add(enqueue_us - receive_us);
    return true;
}

inline uint32_t client_refresh_x100(uint32_t fps, uint32_t output_refresh_x100)
{
    const uint32_t requested = fps == 90u || fps == 120u ? fps : 60u;
    const uint32_t output_fps = output_refresh_x100 == 5994u || output_refresh_x100 == 6000u   ? 60u
                                : output_refresh_x100 == 8991u || output_refresh_x100 == 9000u ? 90u
                                : output_refresh_x100 == 11988u || output_refresh_x100 == 12000u
                                    ? 120u
                                    : 0u;
    // Preserve the requested rate on unknown/slower/non-integral outputs,
    // especially 90 FPS on 119.88 Hz. Do not advertise 120 FPS for that stream.
    if (output_fps >= requested && output_fps % requested == 0u)
        return output_refresh_x100 / (output_fps / requested);
    return requested * 100u;
}

// Sample cumulative counters on the local clock, not host PTS or queued packet
// timestamps: draining old frames in a burst must not masquerade as network FPS.
struct RateWindow
{
    uint64_t start_us{}, previous_count{};
    uint32_t fps_x100{};
    bool initialized{};

    bool update(uint64_t now_us, uint64_t count)
    {
        if (!initialized || now_us < start_us || count < previous_count)
        {
            start_us = now_us;
            previous_count = count;
            fps_x100 = 0;
            initialized = true;
            return false;
        }
        const uint64_t elapsed = now_us - start_us;
        if (elapsed < 1000000u)
            return false;
        fps_x100 = static_cast<uint32_t>((count - previous_count) * 100000000u / elapsed);
        start_us = now_us;
        previous_count = count;
        return true;
    }
};

inline bool drop_stale_presentation(uint64_t age_us, uint32_t fps, int pending_frames,
                                    uint64_t since_last_flip_us)
{
    const uint64_t budget = 2000000u / (fps ? fps : 60u);
    // Only skip when a newer frame exists; force progress at least every 100 ms.
    // Compressed frames are still decoded to preserve reference state.
    return age_us > budget && pending_frames > 0 && since_last_flip_us < 100000u;
}

inline bool discard_audio_backlog(int pending_ms, uint32_t limit_ms)
{
    // Zero disables this A/B experiment. Apply AFTER Opus decoding, before PCM
    // enters AudioOut; never skip compressed packets or reset decoder history.
    return limit_ms != 0u && pending_ms > 0 && static_cast<uint32_t>(pending_ms) > limit_ms;
}
} // namespace moonlight
