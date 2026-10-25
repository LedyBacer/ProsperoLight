/*
 * ProsperoLight - bounded playout accounting and VideoOut feedback.
 * Copyright (C) 2026 LedyBacer and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <algorithm>
#include <cstdint>

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
