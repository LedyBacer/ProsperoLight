/* ps5-native-app-boilerplate - bounded developer-only unique output evidence.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include <array>
#include <cstdint>
#include <limits>
namespace prosperolight::test_lab
{
// Exact shown-argument matching, independent of the single-slot pacing feedback.
// A lagging VideoOut poll may skip requests; skipped pictures are never counted.
class OutputEvidence
{
  public:
    void submitted(uint64_t argument, uint32_t picture)
    {
        pending_[next_] = {argument, picture, true};
        next_ = (next_ + 1) % pending_.size();
    }
    bool observe(uint64_t shown, uint64_t flips)
    {
        if (shown == std::numeric_limits<uint64_t>::max() || !flips || flips <= last_flips_)
            return false;
        last_flips_ = flips;
        for (auto &entry : pending_)
        {
            if (!entry.valid || entry.argument != shown)
                continue;
            entry.valid = false;
            if (have_picture_ && entry.picture <= last_picture_)
                return false;
            last_picture_ = entry.picture;
            have_picture_ = true;
            return true;
        }
        return false;
    }

  private:
    struct Entry
    {
        uint64_t argument{};
        uint32_t picture{};
        bool valid{};
    };
    std::array<Entry, 32> pending_{};
    unsigned next_{};
    uint64_t last_flips_{};
    uint32_t last_picture_{};
    bool have_picture_{};
};
} // namespace prosperolight::test_lab
