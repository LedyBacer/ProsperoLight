// ps5-homebrew-ui - Frame-time histogram for pacing diagnostics.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace hui
{

// Accumulates frame times and formats a one-line summary for the app log.
// Bucket upper edges (ms): 8.4 | 17.5 | 25 | 34 | 50 | 100 | above. At 60 Hz
// a healthy frame lands in bucket 1; anything later is a visible hitch.
class FrameStats
{
  public:
    static constexpr std::size_t kBuckets = 7;

    void add(double milliseconds);
    void reset();

    std::uint64_t count() const
    {
        return count_;
    }
    double mean_ms() const;
    double max_ms() const
    {
        return max_ms_;
    }
    const std::array<std::uint64_t, kBuckets> &buckets() const
    {
        return buckets_;
    }

    // "frames=600 mean=16.67ms max=17.10ms hist=0,598,2,0,0,0,0"; returns the
    // snprintf result.
    int format(char *buffer, std::size_t size) const;

    static std::size_t bucket_for(double milliseconds);

  private:
    std::array<std::uint64_t, kBuckets> buckets_{};
    std::uint64_t count_ = 0;
    double total_ms_ = 0.0;
    double max_ms_ = 0.0;
};

} // namespace hui
