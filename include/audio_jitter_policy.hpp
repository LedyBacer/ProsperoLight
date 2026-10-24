/*
 * ps5-native-app-boilerplate / ProsperoLight - Bounded audio prebuffer policy.
 * Copyright (C) 2026 LedyBacer and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <algorithm>
#include <cstdint>

namespace moonlight
{
// Only the audio-output worker owns these controllers. They are driven by
// output grains, never the Opus callback. Targets are PCM frames at 48 kHz,
// not RTT: a 100 ms RTT on a stable link needs no 100 ms audio buffer.
class AudioPrebufferPolicy
{
  public:
    static constexpr uint32_t kMinimumFrames = 960;
    static constexpr uint32_t kMaximumFrames = 3840;
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
    uint32_t near_miss_events() const
    {
        return near_miss_events_;
    }
    uint32_t failed_shrinks() const
    {
        return failed_shrinks_;
    }

    void on_underrun(uint64_t now_us)
    {
        ++underrun_events_;
        if (probe_until_us_ && now_us < probe_until_us_)
        {
            // The shallower target was unsafe. Restore the last proven one;
            // do not count the same failure again as evidence to grow past it.
            target_frames_ = std::max(target_frames_, probe_previous_frames_);
            ++failed_shrinks_;
            backoff_until_us_ = now_us + shrink_backoff_us_;
            shrink_backoff_us_ = std::min<uint64_t>(shrink_backoff_us_ * 2, UINT64_C(480000000));
            probe_until_us_ = 0;
            last_relax_us_ = 0;
            recent_underrun_us_ = now_us;
            return;
        }
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

    void on_near_miss(uint64_t now_us)
    {
        ++near_miss_events_;
        if (!near_miss_since_us_ || now_us < near_miss_since_us_ ||
            now_us - near_miss_since_us_ > UINT64_C(5000000))
        {
            near_miss_since_us_ = now_us;
            near_miss_streak_ = 0;
        }
        // Several nearly empty grains in a short window predict a dropout.
        // A single ordinary packet boundary must not enlarge latency on LAN.
        if (++near_miss_streak_ >= 3 && (!last_near_miss_growth_us_ ||
                                         now_us - last_near_miss_growth_us_ >= UINT64_C(10000000)))
        {
            grow();
            last_near_miss_growth_us_ = now_us;
            near_miss_streak_ = 0;
            recent_underrun_us_ = now_us;
            last_relax_us_ = 0;
        }
    }

    void on_healthy_grain()
    {
        // Consecutive near-miss evidence, not three scattered isolated reads.
        near_miss_streak_ = 0;
    }

    void on_output(uint64_t now_us)
    {
        if (probe_until_us_ && now_us >= probe_until_us_)
        {
            probe_until_us_ = 0;
            shrink_backoff_us_ = UINT64_C(60000000);
        }
        if (target_frames_ == kMinimumFrames || !recent_underrun_us_ ||
            now_us < recent_underrun_us_ || now_us - recent_underrun_us_ < UINT64_C(45000000))
            return;
        if (backoff_until_us_ && now_us < backoff_until_us_)
            return;
        if (probe_until_us_)
            return;
        if (last_relax_us_ &&
            (now_us < last_relax_us_ || now_us - last_relax_us_ < UINT64_C(30000000)))
            return;
        probe_previous_frames_ = target_frames_;
        target_frames_ -= kStepFrames;
        probe_until_us_ = now_us + UINT64_C(5000000);
        last_relax_us_ = now_us;
        ++decreases_;
    }

  private:
    void grow()
    {
        if (target_frames_ >= kMaximumFrames)
            return;
        target_frames_ = std::min(kMaximumFrames, target_frames_ + kStepFrames);
        peak_frames_ = std::max(peak_frames_, target_frames_);
        ++increases_;
    }

    uint32_t target_frames_{kMinimumFrames}, peak_frames_{kMinimumFrames};
    uint32_t increases_{}, decreases_{}, underrun_events_{}, near_miss_events_{};
    uint32_t near_miss_streak_{}, failed_shrinks_{}, probe_previous_frames_{};
    uint64_t recent_underrun_us_{}, last_relax_us_{}, near_miss_since_us_{};
    uint64_t last_near_miss_growth_us_{}, probe_until_us_{}, backoff_until_us_{};
    uint64_t shrink_backoff_us_{UINT64_C(60000000)};
};

// Slowly sheds only STANDING excess. An instantaneous 100 ms delivery clump
// often bridges the next packet drought; removing it immediately is harmful.
class AudioRingDriftPolicy
{
  public:
    static constexpr uint32_t kHeadroomFrames = 720; // 15 ms
    static constexpr uint32_t kShedFrames = 240;     // one 5 ms packet

    uint32_t observe(uint64_t now_us, uint32_t depth, uint32_t target)
    {
        if (!previous_us_ || now_us <= previous_us_ || now_us - previous_us_ > UINT64_C(1000000))
        {
            average_q8_ = uint64_t(depth) << 8;
            over_since_us_ = 0;
        }
        else
        {
            // EWMA with a one-second time constant, update once per output
            // grain. Integral fixed point; no allocation or float in AudioOut.
            const uint64_t weight =
                std::min<uint64_t>(256, (now_us - previous_us_) * 256 / 1000000);
            const int64_t error = (int64_t(depth) << 8) - int64_t(average_q8_);
            average_q8_ = uint64_t(
                std::max<int64_t>(0, int64_t(average_q8_) + error * int64_t(weight) / 256));
        }
        previous_us_ = now_us;
        if (average_q8_ > (uint64_t(target + kHeadroomFrames) << 8) &&
            depth > target + kHeadroomFrames + kShedFrames)
        {
            if (!over_since_us_)
                over_since_us_ = now_us;
            if (now_us >= over_since_us_ + UINT64_C(2000000))
            {
                over_since_us_ = now_us;
                average_q8_ -= uint64_t(kShedFrames) << 8;
                ++sheds_;
                return kShedFrames;
            }
        }
        else
            over_since_us_ = 0;
        return 0;
    }

    uint32_t mean_frames() const
    {
        return static_cast<uint32_t>(average_q8_ >> 8);
    }
    uint32_t sheds() const
    {
        return sheds_;
    }

  private:
    uint64_t previous_us_{}, over_since_us_{}, average_q8_{};
    uint32_t sheds_{};
};
} // namespace moonlight
