/*
 * ps5-native-app-boilerplate / ProsperoLight - Bounded source-clock presentation timing.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include <algorithm>
#include <cstdint>

namespace moonlight
{
// RTP is a relative 90 kHz source clock; never subtract it from local time.
// Validate forward movement and unwrap rollover before estimating cadence.
class SourceTimestamp
{
  public:
    uint64_t update(uint32_t rtp, uint64_t fallback_us)
    {
        if (!initialized_)
        {
            initialized_ = true;
            last_ = rtp;
            ticks_ = rtp;
            return rtp ? ticks_ * 1000000 / 90000 : fallback_us;
        }
        const uint32_t delta = rtp - last_;
        last_ = rtp;
        if (delta && delta < 900000)
        {
            ticks_ += delta;
            return ticks_ * 1000000 / 90000;
        }
        if (!delta)
            return fallback_us;
        ticks_ = rtp;
        return fallback_us;
    }

  private:
    uint64_t ticks_{};
    uint32_t last_{};
    bool initialized_{};
};

// A target is a submission deadline. GPU completion and physical display
// completion remain distinct measurements owned by the presentation backend.
class FramePacing
{
  public:
    struct Stats
    {
        uint64_t misses{}, resets{}, submissions{}, reserve_us{}, period_us{};
        uint64_t wait_total_us{}, late_max_us{}, spacing_error_max_us{};
    } stats;

    void reset(unsigned fps)
    {
        *this = FramePacing{};
        period_q16_ = (UINT64_C(1000000) << 16) / std::max(1u, fps);
        period_ = period_q16_ >> 16;
        stats.period_us = period_;
    }

    uint64_t target(int32_t frame, uint64_t source_us, uint64_t ready_us,
                    uint32_t fixed_refresh_x100 = 0, uint64_t flip_anchor_us = 0,
                    uint32_t display_ceiling_x100 = 0, uint64_t preparation_lead_us = 1000)
    {
        if (!initialized_)
        {
            initialized_ = true;
            last_frame_ = frame;
            last_source_ = source_us;
            slot_ = ready_us;
        }
        else if (frame != last_frame_)
        {
            bool rate_changed = false;
            const int64_t frames = int64_t(frame) - last_frame_;
            uint64_t advance = frames > 0 && frames < 240 ? uint64_t(frames) * period_ : 0;
            if (advance && source_us > last_source_ && last_source_)
            {
                const uint64_t delta = source_us - last_source_;
                const uint64_t sample = delta / uint64_t(frames);
                // Arrival-derived fallback PTS can be bursty. Reject impossible
                // rates and require persistent evidence before changing cadence.
                if (sample >= 8000 && sample <= 40000)
                {
                    if (sample > period_ * 112 / 100 || sample < period_ * 88 / 100)
                    {
                        if (candidate_ && sample > candidate_ * 95 / 100 &&
                            sample < candidate_ * 105 / 100)
                            ++candidate_count_;
                        else
                        {
                            candidate_ = sample;
                            candidate_count_ = 1;
                        }
                        if (candidate_count_ >= 6)
                        {
                            period_q16_ = (delta << 16) / uint64_t(frames);
                            period_ = std::clamp<uint64_t>(sample, 8333, 33333);
                            candidate_count_ = 0;
                            rate_changed = true;
                        }
                    }
                    else
                    {
                        period_q16_ = (period_q16_ * 31 + (delta << 16) / uint64_t(frames)) / 32;
                        period_ = std::clamp<uint64_t>(period_q16_ >> 16, 8333, 33333);
                        candidate_count_ = 0;
                    }
                    period_q16_ = std::clamp<uint64_t>(period_q16_, (UINT64_C(1000000) << 16) / 120,
                                                       (UINT64_C(1000000) << 16) / 30);
                }
            }
            if (advance)
            {
                const uint64_t scaled = uint64_t(frames) * period_q16_ + fractional_;
                advance = scaled >> 16;
                fractional_ = scaled & 65535;
            }
            if (rate_changed || !advance || advance > 1000000 ||
                ready_us > slot_ + advance + 250000)
            {
                slot_ = ready_us;
                reserve_ = 1500;
                ++stats.resets;
            }
            else
                slot_ += advance;
            last_frame_ = frame;
            last_source_ = source_us;
        }
        // Initial GPU/decoder setup must not become a permanent playout delay.
        if (stats.submissions < 3)
            slot_ = ready_us;
        const uint64_t cap = std::min<uint64_t>(10000, period_);
        const uint64_t demand = ready_us > slot_ ? ready_us - slot_ : 0;
        if (demand > reserve_)
        {
            reserve_ = std::min(cap, demand + 250);
            ++stats.misses;
            clean_ = 0;
        }
        else if (++clean_ >= 120 && reserve_ > 500)
        {
            reserve_ -= std::min<uint64_t>(reserve_ - 500, 25);
        }
        reserve_ = std::min(reserve_, cap);
        uint64_t deadline = std::min(std::max(ready_us, slot_ + reserve_), ready_us + cap);
        // A bounded interval guard prevents a late frame followed by a rush.
        // If fixed display output is slower, its own interval is the floor.
        uint64_t minimum = period_ * 98 / 100;
        if (fixed_refresh_x100)
            minimum = (UINT64_C(100000000) / fixed_refresh_x100) * 98 / 100;
        if (display_ceiling_x100)
            minimum = std::max(minimum, UINT64_C(100000000) / display_ceiling_x100);
        if (submitted_)
            deadline = std::max(deadline, submitted_ + minimum);
        if (fixed_refresh_x100 && flip_anchor_us)
        {
            const uint64_t display_period = UINT64_C(100000000) / fixed_refresh_x100;
            const uint64_t lead = std::min<uint64_t>(preparation_lead_us, display_period / 4);
            const uint64_t required = deadline + lead;
            const uint64_t ticks =
                required > flip_anchor_us
                    ? (required - flip_anchor_us + display_period - 1) / display_period
                    : 0;
            deadline = flip_anchor_us + ticks * display_period - lead;
        }
        stats.reserve_us = reserve_;
        stats.period_us = period_;
        return deadline;
    }

    void submitted(uint64_t planned_us, uint64_t actual_us, uint64_t wait_us)
    {
        if (submitted_)
        {
            const uint64_t spacing = actual_us > submitted_ ? actual_us - submitted_ : 0;
            const uint64_t error = spacing > period_ ? spacing - period_ : period_ - spacing;
            stats.spacing_error_max_us = std::max(stats.spacing_error_max_us, error);
        }
        if (actual_us > planned_us)
        {
            const uint64_t late = actual_us - planned_us;
            stats.late_max_us = std::max(stats.late_max_us, late);
            wake_lead_ = std::clamp<uint64_t>((wake_lead_ * 7 + late) / 8, 50, 250);
        }
        else
            wake_lead_ = std::max<uint64_t>(50, wake_lead_ - 1);
        submitted_ = actual_us;
        stats.wait_total_us += wait_us;
        ++stats.submissions;
    }

    uint64_t wake_lead_us() const
    {
        return wake_lead_;
    }

    uint64_t stale_limit_us() const
    {
        return std::max(period_ * 2, period_ + reserve_);
    }

  private:
    uint64_t period_q16_ = (UINT64_C(1000000) << 16) / 60, fractional_{};
    uint64_t period_ = 16666, slot_{}, submitted_{}, last_source_{};
    uint64_t reserve_ = 1500, candidate_{}, wake_lead_ = 100;
    unsigned candidate_count_{}, clean_{};
    int32_t last_frame_{};
    bool initialized_{};
};
} // namespace moonlight
