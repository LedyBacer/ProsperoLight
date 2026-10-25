/*
 * ps5-native-app-boilerplate / ProsperoLight - bounded playout accounting and VideoOut feedback.
 * Copyright (C) 2026 LedyBacer and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace moonlight
{
// Frame assembly includes normal wire time, not only FEC waiting. Charge only
// assembly time above a nominal source period, and no more than HALF of the
// observed arrival-jitter cushion. This conservative candidate is gated until
// a paired PS5 hardware test establishes its effect on latency and drop rate.
class VideoPlayoutBudget
{
  public:
    struct Decision
    {
        uint32_t requested_us{}, spent_us{}, remaining_us{};
    };
    struct Stats
    {
        uint64_t observed{}, adjusted{}, saved_us{};
        uint32_t max_saved_us{};
    } stats;

    Decision observe(uint32_t budget_us, uint64_t receive_us, uint64_t enqueue_us,
                     uint32_t source_fps)
    {
        Decision out{budget_us, 0, budget_us};
        ++stats.observed;
        if (!budget_us || !source_fps || !receive_us || enqueue_us <= receive_us)
            return out;
        const uint64_t period = UINT64_C(1000000) / source_fps;
        if (!period)
            return out;
        const uint64_t assembled = enqueue_us - receive_us;
        if (assembled <= period || assembled > UINT64_C(500000))
            return out;
        const uint32_t excess =
            static_cast<uint32_t>(std::min<uint64_t>(assembled - period, budget_us / 2));
        out.spent_us = excess;
        out.remaining_us = budget_us - excess;
        if (excess)
        {
            ++stats.adjusted;
            stats.saved_us += excess;
            stats.max_saved_us = std::max(stats.max_saved_us, excess);
        }
        return out;
    }
};

// Punktfunk-inspired source PTS mapper. Pure clock arithmetic only; use in
// shadow mode until hardware traces establish the decoder/VideoOut phase.
// Smooth the offset, never the source timestamps, and re-anchor on gaps.
class SourceDueClock
{
  public:
    struct Stats
    {
        uint64_t samples{}, late{}, reanchors{};
        uint32_t cushion_us{}, jitter_us{};
    } stats;

    uint64_t observe(uint64_t pts_us, uint64_t ready_us, uint32_t source_period_us)
    {
        if (!pts_us || !ready_us || !source_period_us)
            return ready_us;
        const int64_t raw = static_cast<int64_t>(ready_us) - static_cast<int64_t>(pts_us);
        if (!initialized_ || pts_us < previous_pts_us_ ||
            pts_us - previous_pts_us_ > UINT64_C(500000))
        {
            offset_us_ = raw;
            skew_us_ = 0;
            mad_us_ = 0;
            tail_us_ = 0;
            ++stats.reanchors;
            initialized_ = true;
        }
        else if (pts_us > previous_pts_us_)
        {
            offset_us_ += skew_us_;
            const int64_t error = std::clamp(raw - offset_us_, INT64_C(-20000), INT64_C(20000));
            offset_us_ += error / 32;
            skew_us_ = std::clamp(skew_us_ + error / 1024, INT64_C(-1000), INT64_C(1000));
            mad_us_ = std::max<int64_t>(0, mad_us_ + (std::abs(error) - mad_us_) / 32);
            tail_us_ = error > tail_us_ ? error : tail_us_ - tail_us_ / 128;
        }
        else
            return ready_us; // Duplicate RTP PTS is not a drift observation.

        previous_pts_us_ = pts_us;
        const int64_t cushion = std::clamp(
            std::max(INT64_C(500), std::max(mad_us_ * 2, tail_us_)),
            INT64_C(500), static_cast<int64_t>(source_period_us));
        const int64_t due = static_cast<int64_t>(pts_us) + offset_us_ + cushion;
        stats.cushion_us = static_cast<uint32_t>(cushion);
        stats.jitter_us = static_cast<uint32_t>(mad_us_);
        ++stats.samples;
        if (due < static_cast<int64_t>(ready_us))
            ++stats.late;
        return due <= 0 ? 0 : static_cast<uint64_t>(due);
    }

  private:
    uint64_t previous_pts_us_{};
    int64_t offset_us_{}, skew_us_{}, mad_us_{}, tail_us_{};
    bool initialized_{};
};

// A completed flip observation is NOT a physical HDMI scanout timestamp.
class PresentFeedback
{
  public:
    struct Stats
    {
        uint64_t submitted{}, completed{}, late_submissions{};
        uint64_t submit_service_total_us{}, submit_service_max_us{};
        uint64_t scheduler_late_total_us{}, scheduler_late_max_us{};
        uint64_t observed_flip_interval_max_us{};
        uint64_t flip_counter_advances{}, repeated_flip_counters{};
    } stats;

    void submitted(uint64_t deadline_us, uint64_t start_us, uint64_t end_us)
    {
        ++stats.submitted;
        const uint64_t service = end_us > start_us ? end_us - start_us : 0;
        stats.submit_service_total_us += service;
        stats.submit_service_max_us = std::max(stats.submit_service_max_us, service);
        if (deadline_us && start_us > deadline_us)
        {
            const uint64_t late = start_us - deadline_us;
            ++stats.late_submissions;
            stats.scheduler_late_total_us += late;
            stats.scheduler_late_max_us = std::max(stats.scheduler_late_max_us, late);
        }
    }

    void observed_flip(uint64_t observed_us, uint64_t counter, bool counter_valid)
    {
        ++stats.completed;
        if (last_observation_us_ && observed_us > last_observation_us_)
            stats.observed_flip_interval_max_us = std::max(
                stats.observed_flip_interval_max_us, observed_us - last_observation_us_);
        last_observation_us_ = observed_us;
        if (counter_valid)
        {
            if (have_counter_)
            {
                if (counter > last_counter_)
                    stats.flip_counter_advances += counter - last_counter_;
                else
                    ++stats.repeated_flip_counters;
            }
            have_counter_ = true;
            last_counter_ = counter;
        }
    }

  private:
    uint64_t last_observation_us_{}, last_counter_{};
    bool have_counter_{};
};

// An old frame is eligible for replacement only if its own due time is past,
// and the successor actually supersedes its PTS and was ready on time.
inline bool deadline_mailbox_replacement(uint64_t now_us, uint64_t current_due_us,
                                         uint64_t current_pts_us, uint64_t successor_pts_us,
                                         uint64_t successor_ready_us)
{
    return current_due_us && now_us > current_due_us &&
           successor_pts_us > current_pts_us && successor_ready_us <= now_us;
}
} // namespace moonlight
