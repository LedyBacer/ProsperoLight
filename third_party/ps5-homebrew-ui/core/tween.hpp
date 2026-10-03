// ps5-homebrew-ui - Easing curves and springs for UI motion.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cmath>

namespace hui::tween
{

// ---- easing curves: t in 0..1 -> 0..1 -------------------------------------

inline float clamp01(float t)
{
    return std::clamp(t, 0.0f, 1.0f);
}
inline float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}
// Where t sits between a and b, clamped to 0..1.
inline float inverse_lerp(float a, float b, float t)
{
    return a == b ? 0.0f : clamp01((t - a) / (b - a));
}
inline float smoothstep(float t)
{
    t = clamp01(t);
    return t * t * (3.0f - 2.0f * t);
}
// The default "arrive": fast start, gentle stop.
inline float cubic_out(float t)
{
    t = clamp01(t);
    const float u = 1.0f - t;
    return 1.0f - u * u * u;
}
// Things that leave: slow start, fast exit.
inline float cubic_in(float t)
{
    t = clamp01(t);
    return t * t * t;
}
// Moves that stay on screen from start to end.
inline float cubic_in_out(float t)
{
    t = clamp01(t);
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
}
// A sharper arrive for large moves (screen transitions, drawers).
inline float quint_out(float t)
{
    t = clamp01(t);
    const float u = 1.0f - t;
    return 1.0f - u * u * u * u * u;
}
inline float expo_out(float t)
{
    t = clamp01(t);
    return t >= 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
}
// Overshoots slightly before settling (card pops, star bursts).
inline float back_out(float t)
{
    t = clamp01(t);
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.0f;
    const float u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}
// Rings past the target a few times (badges, celebratory pops). Use rarely.
inline float elastic_out(float t)
{
    t = clamp01(t);
    if (t <= 0.0f || t >= 1.0f)
        return t;
    constexpr float c4 = 2.0943951f; // 2 pi / 3
    return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
}
// 0 -> 1 -> 0 over t in 0..1 (a press, a flash).
inline float ping(float t)
{
    t = clamp01(t);
    return std::sin(t * 3.14159265f);
}
// The eased progress of item `index` in a staggered entrance that started
// `elapsed` seconds ago: each item starts `step` seconds after the one before.
inline float stagger(float elapsed, int index, float step = 0.04f, float duration = 0.35f)
{
    return cubic_out((elapsed - static_cast<float>(index) * step) / duration);
}

// ---- springs: values that chase a target ----------------------------------

// Critically damped spring toward a target; frame-rate independent. The
// workhorse of the kit: set target, call update every frame, draw value.
struct Spring
{
    float value = 0.0f;
    float velocity = 0.0f;
    float target = 0.0f;

    void snap(float v)
    {
        value = target = v;
        velocity = 0.0f;
    }
    // omega ~ responsiveness (rad/s): 20 snappy, 12 default, 6 soft.
    void update(float dt, float omega = 12.0f)
    {
        // Exact solution of the critically damped oscillator over dt.
        const float x = value - target;
        const float e = std::exp(-omega * dt);
        const float next_x = (x + (velocity + omega * x) * dt) * e;
        velocity = (velocity - omega * (velocity + omega * x) * dt) * e;
        value = target + next_x;
        // The rest test scales with the value: float noise on a position of
        // several hundred pixels is larger than any fixed threshold.
        const float rest = 1e-4f * std::max(1.0f, std::fabs(target));
        if (std::fabs(value - target) < rest && std::fabs(velocity) < rest * 10.0f)
            snap(target);
    }
    bool settled() const
    {
        return value == target && velocity == 0.0f;
    }
};

// Underdamped spring: overshoots and settles. For playful pops and for
// anything the player flicks. damping 1 is critical, 0.5 is lively.
struct Bounce
{
    float value = 0.0f;
    float velocity = 0.0f;
    float target = 0.0f;

    void snap(float v)
    {
        value = target = v;
        velocity = 0.0f;
    }
    void kick(float impulse)
    {
        velocity += impulse;
    }
    void update(float dt, float omega = 16.0f, float damping = 0.5f)
    {
        // Semi-implicit Euler in small steps keeps it stable on long frames.
        const int steps = std::max(1, static_cast<int>(std::ceil(dt / 0.004f)));
        const float h = dt / static_cast<float>(steps);
        for (int i = 0; i < steps; ++i)
        {
            const float acceleration =
                -omega * omega * (value - target) - 2.0f * damping * omega * velocity;
            velocity += acceleration * h;
            value += velocity * h;
        }
        // The rest test scales with the value: float noise on a position of
        // several hundred pixels is larger than any fixed threshold.
        const float rest = 1e-4f * std::max(1.0f, std::fabs(target));
        if (std::fabs(value - target) < rest && std::fabs(velocity) < rest * 10.0f)
            snap(target);
    }
};

// A one-shot timeline value: 0 -> 1 over duration seconds.
struct Timer
{
    float elapsed = 0.0f;
    float duration = 0.25f;
    bool running = false;

    void start(float seconds)
    {
        elapsed = 0.0f;
        duration = seconds;
        running = true;
    }
    void update(float dt)
    {
        if (!running)
            return;
        elapsed += dt;
        if (elapsed >= duration)
        {
            elapsed = duration;
            running = false;
        }
    }
    float progress() const
    {
        return duration > 0.0f ? clamp01(elapsed / duration) : 1.0f;
    }
};

} // namespace hui::tween
