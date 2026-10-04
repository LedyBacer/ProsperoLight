/*
 * ps5-native-app-boilerplate - Persistent presentation and endpoint policies.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include <cstdint>

namespace moonlight
{
// Account for 59.94/119.88 fixed modes without turning a VRR sample into a cap.
inline uint32_t fixed_cadence_rate(uint32_t fps, uint32_t refresh_x100)
{
    const uint32_t requested = fps * 100;
    if (!requested || !refresh_x100)
        return requested;
    const uint32_t divisor = (refresh_x100 + requested / 2) / requested;
    if (!divisor)
        return requested;
    const uint32_t matched = refresh_x100 / divisor;
    return matched <= requested && requested - matched <= requested / 100
               ? matched : requested;
}
// Absolute monotonic deadlines with fractional periods carried forward.
// An overdue frame is eligible immediately; never add another period after
// a blocking flip or late decoder completion. Rebase to avoid catch-up bursts.
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
        deadline_ += period;
        remainder_ += frequency_ % rate_;
        if (remainder_ >= rate_)
        {
            ++deadline_;
            remainder_ -= rate_;
        }
        if (deadline_ < now)
        {
            deadline_ = now;
            remainder_ = 0;
        }
        return deadline_;
    }

  private:
    uint64_t deadline_ = 0, frequency_ = 0, remainder_ = 0;
    uint32_t rate_ = 0;
};
} // namespace moonlight
