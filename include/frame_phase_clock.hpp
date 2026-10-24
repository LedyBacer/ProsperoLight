/*
 * ProsperoLight - Experimental source-cadence / network-arrival phase clock.
 * Copyright (C) 2026 LedyBacer and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace moonlight
{
// No cross-machine clock subtraction: source PTS and PS5's network enqueue
// stamp have arbitrary origins, absorbed by offset_us_. This controller only
// learns frame-to-frame residuals. Its optional hold is relative to the local
// presenting thread's 'now', never an absolute timestamp from another clock.
//
// The production FramePacing timeline remains authoritative. This provides an
// independently measured phase lead for controlled A/B tests on fixed output;
// late decoded frames cannot drag the estimator because it samples enqueue,
// not decoder/GPU readiness. Never add this hold to the existing network
// reserve: the presenter uses max(existing deadline, now + hold).
class CadencePhaseClock
{
  public:
    struct Stats
    {
        uint64_t samples{}, reanchors{}, late{};
        int32_t min_slack_us{INT32_MAX};
        uint32_t mad_us{}, tail_us{}, cushion_us{}, hold_us{};
        int32_t skew_us{};
    } stats;

    uint32_t observe(uint32_t frame, uint64_t source_us, uint64_t enqueue_us,
                     uint32_t period_us, uint32_t network_budget_us)
    {
        if (!source_us || !enqueue_us || !period_us)
        {
            anchored_ = false;
            return 0;
        }
        const int64_t offset_sample = difference(enqueue_us, source_us);
        const bool continuous = anchored_ && frame == last_frame_ + 1 &&
                                source_us > last_source_us_ &&
                                source_us - last_source_us_ <= UINT64_C(500000) &&
                                enqueue_us >= last_enqueue_us_ &&
                                enqueue_us - last_enqueue_us_ <= UINT64_C(500000);
        if (!continuous)
        {
            if (anchored_)
                ++stats.reanchors;
            offset_us_ = offset_sample;
            skew_us_ = 0;
            anchored_ = true;
            last_frame_ = frame;
            last_source_us_ = source_us;
            last_enqueue_us_ = enqueue_us;
            return 0;
        }
        offset_us_ += skew_us_;
        const int64_t error = std::clamp<int64_t>(offset_sample - offset_us_, -20000, 20000);
        offset_us_ += error / 32;
        skew_us_ = std::clamp<int64_t>(skew_us_ + error / 1024, -500, 500);
        mad_us_ = std::max<int64_t>(0, mad_us_ + (std::abs(error) - mad_us_) / 32);
        tail_us_ = std::max<int64_t>(error, tail_us_ - tail_us_ / 128);
        last_frame_ = frame;
        last_source_us_ = source_us;
        last_enqueue_us_ = enqueue_us;
        ++stats.samples;

        // The measured arrival tail may request at most one source period;
        // never use a PS5 decoder stall as an excuse to grow the buffer.
        const uint32_t cap = std::min({period_us, network_budget_us, 30000u});
        const int64_t cushion = std::min<int64_t>(cap, std::max<int64_t>(
            500, std::max(mad_us_ * 2, tail_us_ + 500)));
        const int64_t slack = offset_us_ + int64_t(source_us) + cushion -
                              int64_t(enqueue_us);
        if (slack < 0)
            ++stats.late;
        stats.min_slack_us = std::min(
            stats.min_slack_us, static_cast<int32_t>(std::clamp<int64_t>(slack, INT32_MIN, INT32_MAX)));
        stats.mad_us = static_cast<uint32_t>(mad_us_);
        stats.tail_us = static_cast<uint32_t>(std::max<int64_t>(0, tail_us_));
        stats.cushion_us = static_cast<uint32_t>(cushion);
        stats.skew_us = static_cast<int32_t>(skew_us_);
        stats.hold_us = static_cast<uint32_t>(std::clamp<int64_t>(slack, 0, cap));
        return stats.hold_us;
    }

  private:
    static int64_t difference(uint64_t a, uint64_t b)
    {
        if (a >= b)
            return static_cast<int64_t>(std::min<uint64_t>(a - b, INT64_MAX));
        return -static_cast<int64_t>(std::min<uint64_t>(b - a, INT64_MAX));
    }

    int64_t offset_us_{}, skew_us_{}, mad_us_{}, tail_us_{};
    uint64_t last_source_us_{}, last_enqueue_us_{};
    uint32_t last_frame_{};
    bool anchored_{};
};
} // namespace moonlight
