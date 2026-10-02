// ps5-homebrew-ui - Frame-time histogram for pacing diagnostics.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/frame_stats.hpp"

#include <cstdio>

namespace hui
{

namespace
{

constexpr double kEdges[FrameStats::kBuckets - 1] = {8.4, 17.5, 25.0, 34.0, 50.0, 100.0};

} // namespace

std::size_t FrameStats::bucket_for(double milliseconds)
{
    for (std::size_t index = 0; index < FrameStats::kBuckets - 1; ++index)
    {
        if (milliseconds < kEdges[index])
            return index;
    }
    return FrameStats::kBuckets - 1;
}

void FrameStats::add(double milliseconds)
{
    if (milliseconds < 0.0)
        milliseconds = 0.0;
    ++buckets_[bucket_for(milliseconds)];
    ++count_;
    total_ms_ += milliseconds;
    if (milliseconds > max_ms_)
        max_ms_ = milliseconds;
}

void FrameStats::reset()
{
    *this = FrameStats{};
}

double FrameStats::mean_ms() const
{
    return count_ != 0 ? total_ms_ / static_cast<double>(count_) : 0.0;
}

int FrameStats::format(char *buffer, std::size_t size) const
{
    const auto &b = buckets_;
    return std::snprintf(
        buffer, size, "frames=%llu mean=%.2fms max=%.2fms hist=%llu,%llu,%llu,%llu,%llu,%llu,%llu",
        static_cast<unsigned long long>(count_), mean_ms(), max_ms_,
        static_cast<unsigned long long>(b[0]), static_cast<unsigned long long>(b[1]),
        static_cast<unsigned long long>(b[2]), static_cast<unsigned long long>(b[3]),
        static_cast<unsigned long long>(b[4]), static_cast<unsigned long long>(b[5]),
        static_cast<unsigned long long>(b[6]));
}

} // namespace hui
