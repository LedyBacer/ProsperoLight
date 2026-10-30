/* ps5-native-app-boilerplate / ProsperoLight - Bounded native queue recovery, not a throughput optimization.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include <cstdint>

namespace moonlight::video
{
enum class OverloadAction
{
    Continue,
    Resync,
    Stop
};

// Decode-owner only. Queue age is measured on Limelight's monotonic clock;
// budget/health windows use the owner's clock. No requested-FPS/encoder
// heuristic. The 15-AU transport queue remains the hard storage bound during
// blocking calls.
struct OverloadGuard
{
    static constexpr uint64_t healthy_reset_us = 30000000;
    static constexpr unsigned resync_budget = 2;
    uint64_t healthy_since{}, previous_us{}, peak_age_us{}, action_age_us{};
    unsigned attempts{}, resyncs{}, stops{};

    OverloadAction observe(uint64_t now, uint64_t queue_age, bool timestamp_valid, int pending,
                           uint64_t limit_us, bool usable)
    {
        if (!limit_us)
            return OverloadAction::Continue;
        // Unknown ages/initialization/recovery cannot prove sustained health.
        if (!usable || !timestamp_valid || pending < 0 || (previous_us && now < previous_us))
        {
            healthy_since = 0;
            previous_us = now;
            return OverloadAction::Continue;
        }
        // A long observation gap/source pause does not reset a recovery budget.
        if (previous_us && now - previous_us > 1000000)
            healthy_since = 0;
        previous_us = now;
        if (queue_age > peak_age_us)
            peak_age_us = queue_age;
        // A WAN/Wi-Fi burst can complete many frames at once even when the
        // native decoder is healthy. Near-capacity alone is not evidence of
        // a persistent decoder backlog: give a newly filled queue one short
        // decode opportunity before requesting an expensive IDR/reset.
        // At an 80 ms limit this is 15 ms; slow 4K calls (20+ ms) still
        // reach the early guard before the transport's 15-AU hard limit.
        const uint64_t near_full_grace_us = limit_us / 4 < 15000 ? limit_us / 4 : 15000;
        const bool pressure = (pending >= 2 && queue_age >= limit_us) ||
                              (pending >= 12 && queue_age >= near_full_grace_us);
        if (pressure)
        {
            healthy_since = 0;
            action_age_us = queue_age;
            if (attempts >= resync_budget)
            {
                ++stops;
                return OverloadAction::Stop;
            }
            ++attempts;
            ++resyncs;
            return OverloadAction::Resync;
        }
        // Hysteresis: a smaller backlog cannot rearm the budget. Require 30s
        // of observed low queue age and at most one pending AU, without pauses.
        if (pending <= 1 && queue_age < limit_us / 2)
        {
            if (!healthy_since)
                healthy_since = now ? now : 1;
            if (now >= healthy_since && now - healthy_since >= healthy_reset_us)
                attempts = 0;
        }
        else
            healthy_since = 0;
        return OverloadAction::Continue;
    }
};
} // namespace moonlight::video
