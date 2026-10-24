/*
 * ProsperoLight - Bounded, clock-continuous PS5 PCM grain policy.
 * Copyright (C) 2026 LedyBacer and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <algorithm>
#include <cstdint>

namespace moonlight
{
struct AudioGrainDecision
{
    uint32_t take{}, silence{};
    bool underrun{}, priming{}, near_miss{};
};

// Invariant: always produce exactly one hardware grain after AudioOut starts.
// Unlike blocking the output worker, an empty PCM ring must not suspend the
// hardware's own clock. During re-prime, retain buffered real PCM, not an
// arbitrarily old zero-padded prefix which would make the resume stale.
inline AudioGrainDecision audio_grain_decision(uint32_t depth, uint32_t grain,
                                               uint32_t target, uint32_t packet,
                                               bool priming)
{
    AudioGrainDecision answer{};
    answer.priming = priming;
    if (priming && depth < std::max(target, grain))
    {
        answer.silence = grain;
        return answer;
    }
    answer.take = std::min(depth, grain);
    answer.silence = grain - answer.take;
    answer.underrun = answer.silence != 0;
    answer.priming = answer.underrun;
    answer.near_miss = !answer.underrun && depth - answer.take < packet;
    return answer;
}

// Fade to the next grain after a ring trim, or after synthetic silence. This
// is a saturating interleaved-S16 operation; it does not mix channel indices.
inline void audio_crossfade_from_sample(int16_t *grain, uint32_t frames, uint32_t channels,
                                        const int16_t *last, uint32_t fade_frames)
{
    if (!grain || !last || !channels)
        return;
    const uint32_t n = std::min(frames, fade_frames);
    for (uint32_t i = 0; i < n; ++i)
        for (uint32_t channel = 0; channel < channels; ++channel)
        {
            const int32_t old_sample = last[channel];
            const int32_t new_sample = grain[i * channels + channel];
            const int64_t mix = int64_t(old_sample) * int64_t(n - i) +
                                int64_t(new_sample) * int64_t(i + 1);
            grain[i * channels + channel] = static_cast<int16_t>(
                std::clamp<int64_t>(mix / int64_t(n + 1), INT16_MIN, INT16_MAX));
        }
}

// A short read should not cut straight from a non-zero sample to zero. Fade
// the last decoded samples away before the zero-padded tail starts.
inline void audio_fade_to_silence(int16_t *grain, uint32_t valid_frames, uint32_t channels,
                                  uint32_t fade_frames)
{
    if (!grain || !channels || !valid_frames)
        return;
    const uint32_t n = std::min(valid_frames, fade_frames);
    const uint32_t start = valid_frames - n;
    for (uint32_t i = 0; i < n; ++i)
        for (uint32_t channel = 0; channel < channels; ++channel)
        {
            const uint32_t index = (start + i) * channels + channel;
            grain[index] = static_cast<int16_t>(
                int32_t(grain[index]) * int32_t(n - i - 1) / int32_t(n));
        }
}
} // namespace moonlight
