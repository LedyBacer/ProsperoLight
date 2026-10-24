/*
 * ps5-native-app-boilerplate / ProsperoLight - Bounded audio prebuffer policy.
 * Copyright (C) 2026 LedyBacer and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <cstdint>

namespace moonlight
{
// Only the audio-output worker owns this controller. The Opus network callback
// never sleeps or changes its state. We increase recovery depth after repeated
// underruns, then slowly give latency back when the link has stayed healthy.
// These are PCM frames at 48 kHz (20..50 ms), not milliseconds of network RTT.
class AudioPrebufferPolicy
{
  public:
    static constexpr uint32_t kMinimumFrames = 960;
    static constexpr uint32_t kMaximumFrames = 2400;
    static constexpr uint32_t kStepFrames = 480;

    uint32_t target_frames() const
    {
        return target_frames_;
    }
    uint32_t peak_frames() const
    {
        return peak_frames_;
    }
    uint32_t increases() const
    {
        return increases_;
    }
    uint32_t decreases() const
    {
        return decreases_;
    }
    uint32_t underrun_events() const
    {
        return underrun_events_;
    }

    void on_underrun(uint64_t now_us)
    {
        ++underrun_events_;
        // Two underruns within 10 s provide stronger evidence of recurring
        // jitter than an isolated scheduler hiccup. Escalation is capped.
        if (recent_underrun_us_ && now_us >= recent_underrun_us_ &&
            now_us - recent_underrun_us_ <= UINT64_C(10000000) && target_frames_ < kMaximumFrames)
        {
            target_frames_ += kStepFrames;
            if (target_frames_ > peak_frames_)
                peak_frames_ = target_frames_;
            ++increases_;
        }
        recent_underrun_us_ = now_us;
        last_relax_us_ = 0;
    }

    void on_output(uint64_t now_us)
    {
        if (target_frames_ == kMinimumFrames || !recent_underrun_us_ ||
            now_us < recent_underrun_us_ || now_us - recent_underrun_us_ < UINT64_C(45000000))
            return;
        if (last_relax_us_ &&
            (now_us < last_relax_us_ || now_us - last_relax_us_ < UINT64_C(30000000)))
            return;
        target_frames_ -= kStepFrames;
        last_relax_us_ = now_us;
        ++decreases_;
    }

  private:
    uint32_t target_frames_{kMinimumFrames}, peak_frames_{kMinimumFrames};
    uint32_t increases_{}, decreases_{}, underrun_events_{};
    uint64_t recent_underrun_us_{}, last_relax_us_{};
};
} // namespace moonlight
