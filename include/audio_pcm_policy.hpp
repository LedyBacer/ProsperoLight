/*
 * ps5-native-app-boilerplate / ProsperoLight - Clock-continuous PCM grain policy.
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
inline AudioGrainDecision audio_grain_decision(uint32_t depth, uint32_t grain, uint32_t target,
                                               uint32_t packet, bool priming)
{
    AudioGrainDecision answer{};
    answer.priming = priming;
    // Packetization may be 5 or 10 ms. One packet alone can leave less
    // than one grain before its successor, even on a clean LAN. Bank one
    // packet plus a grain at startup/re-prime; do not impose this on short reads.
    if (priming && depth < std::max(target, grain + packet))
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

// Adapted conceptually from Punktfunk JitterPolicy's consecutive-short-read
// hysteresis and hollow-ring detection (MIT/Apache-2.0, see reference document).
// One late packet must not manufacture a complete target's worth of silence.
// Count consumed PCM frames, not scheduler time: AudioOut may occasionally stall.
class AudioContinuityPolicy
{
  public:
    AudioGrainDecision decide(uint32_t depth, uint32_t grain, uint32_t target, uint32_t packet)
    {
        auto out = audio_grain_decision(depth, grain, target, packet, priming_);
        if (!grain)
            return out;
        if (priming_)
        {
            if (!out.take)
                return out;
            depth_q8_ = uint64_t(depth) << 8;
            priming_ = false;
            short_run_frames_ = short_run_grains_ = 0;
        }
        else
        {
            const int64_t error = (int64_t(depth) << 8) - int64_t(depth_q8_);
            // One-second EWMA at 48 kHz, weighted by the actual device grain.
            depth_q8_ = uint64_t(std::max<int64_t>(
                0, int64_t(depth_q8_) + error * std::min<uint32_t>(grain, 48000) / 48000));
        }
        if (out.underrun)
        {
            ++short_reads_;
            concealed_frames_ += out.silence;
            short_run_frames_ = std::min<uint64_t>(UINT32_MAX, uint64_t(short_run_frames_) + grain);
            ++short_run_grains_;
            const bool hollow = (depth_q8_ >> 8) + 480 < target;
            if (hollow || (short_run_frames_ >= 1920 && short_run_grains_ >= 2))
            {
                priming_ = true;
                ++reprimes_;
            }
        }
        else
            short_run_frames_ = short_run_grains_ = 0;
        out.priming = priming_;
        return out;
    }

    uint64_t concealed_frames() const
    {
        return concealed_frames_;
    }
    uint32_t reprimes() const
    {
        return reprimes_;
    }
    uint32_t short_reads() const
    {
        return short_reads_;
    }

  private:
    uint64_t depth_q8_{}, concealed_frames_{};
    uint32_t short_run_frames_{}, short_run_grains_{}, reprimes_{}, short_reads_{};
    bool priming_{true};
};

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
            const int64_t mix =
                int64_t(old_sample) * int64_t(n - i) + int64_t(new_sample) * int64_t(i + 1);
            grain[i * channels + channel] = static_cast<int16_t>(
                std::clamp<int64_t>(mix / int64_t(n + 1), INT16_MIN, INT16_MAX));
        }
}

// The Opus callback may skip stale PCM while the AudioOut ring still has
// earlier samples buffered. Blend the *next enqueued* packet at that exact
// discontinuity, not the next hardware grain (which may be tens of ms earlier).
inline bool audio_rejoin_after_skip(bool &pending, int16_t *pcm, uint32_t frames, uint32_t channels,
                                    const int16_t *preceding, uint32_t fade_frames)
{
    if (!pending || !pcm || !frames || !channels || !preceding)
        return false;
    audio_crossfade_from_sample(pcm, frames, channels, preceding, fade_frames);
    pending = false;
    return true;
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
            grain[index] =
                static_cast<int16_t>(int32_t(grain[index]) * int32_t(n - i - 1) / int32_t(n));
        }
}
} // namespace moonlight
