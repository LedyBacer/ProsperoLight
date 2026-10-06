// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <chrono>
namespace hui::ui
{
inline float text_lane_seconds()
{
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
}
// Two seconds to read each end; 24 virtual pixels per second in either direction.
inline float text_lane_offset(float elapsed, float measured, float available)
{
    const float distance = std::max(measured - available, 0.0f);
    if (distance <= 0.0f)
        return 0.0f;
    const float travel = distance / 24.0f;
    const float phase = std::fmod(std::max(elapsed, 0.0f), 4.0f + 2.0f * travel);
    if (phase < 2.0f)
        return 0.0f;
    if (phase < 2.0f + travel)
        return (phase - 2.0f) * 24.0f;
    if (phase < 4.0f + travel)
        return distance;
    return std::max(0.0f, distance - (phase - 4.0f - travel) * 24.0f);
}
} // namespace hui::ui
