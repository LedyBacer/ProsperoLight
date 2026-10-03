/*
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include <cstdint>

namespace moonlight {
// Absolute monotonic deadlines with fractional periods carried forward.
// Missed deadlines are rebased rather than replayed as a burst of catch-up frames.
class FrameCadence
{
public:
    void reset(uint64_t now, uint64_t frequency, uint32_t rate)
    {
        deadline_ = now;
        frequency_ = frequency;
        rate_ = rate;
        remainder_ = 0;
    }
    uint64_t next(uint64_t now)
    {
        if (!frequency_ || !rate_)
            return now;
        const uint64_t period = frequency_ / rate_;
        if (now > deadline_ && now - deadline_ > period)
        {
            deadline_ = now;
            remainder_ = 0;
        }
        deadline_ += period;
        remainder_ += frequency_ % rate_;
        if (remainder_ >= rate_)
        {
            ++deadline_;
            remainder_ -= rate_;
        }
        return deadline_ > now ? deadline_ : now;
    }
private:
    uint64_t deadline_ = 0, frequency_ = 0, remainder_ = 0;
    uint32_t rate_ = 0;
};
} // namespace moonlight
