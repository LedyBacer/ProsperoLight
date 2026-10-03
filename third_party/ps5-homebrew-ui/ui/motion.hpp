// ps5-homebrew-ui - Reusable motion helpers built on the springs in core/tween.hpp.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/tween.hpp"
#include "gfx/draw_list.hpp"

#include <algorithm>
#include <cmath>

namespace hui::ui
{

// A rectangle that glides to wherever the focus is: the moving highlight
// behind a list row, the ring around a card. Snap it when a screen opens,
// then set target every frame.
struct SpringRect
{
    tween::Spring x, y, w, h;

    void snap(const gfx::Rect &r)
    {
        x.snap(r.x);
        y.snap(r.y);
        w.snap(r.w);
        h.snap(r.h);
    }
    void target(const gfx::Rect &r)
    {
        x.target = r.x;
        y.target = r.y;
        w.target = r.w;
        h.target = r.h;
    }
    void update(float dt, float omega = 18.0f)
    {
        x.update(dt, omega);
        y.update(dt, omega);
        w.update(dt, omega);
        h.update(dt, omega);
    }
    gfx::Rect value() const
    {
        return {x.value, y.value, w.value, h.value};
    }
};

// A colour that eases toward a target: backdrops and accents that follow
// the focused item instead of snapping to it.
struct SpringColor
{
    tween::Spring r, g, b;

    void snap(gfx::Color c)
    {
        r.snap(c.r);
        g.snap(c.g);
        b.snap(c.b);
    }
    void target(gfx::Color c)
    {
        r.target = c.r;
        g.target = c.g;
        b.target = c.b;
    }
    void update(float dt, float omega = 5.0f)
    {
        r.update(dt, omega);
        g.update(dt, omega);
        b.update(dt, omega);
    }
    gfx::Color value(float alpha = 1.0f) const
    {
        return {r.value, g.value, b.value, alpha};
    }
};

// Scroll position that follows the focus: call reveal() with the focused
// item's span and the view's size; draw content shifted by -offset().
struct Scroller
{
    tween::Spring position;

    // Keeps [start, end) inside the view with `margin` to spare, moving the
    // least distance needed. limit is the content size (0 = unbounded).
    void reveal(float start, float end, float view, float margin, float limit = 0.0f)
    {
        float wanted = position.target;
        if (start - margin < wanted)
            wanted = start - margin;
        if (end + margin > wanted + view)
            wanted = end + margin - view;
        if (limit > 0.0f)
            wanted = std::min(wanted, std::max(0.0f, limit - view));
        position.target = std::max(0.0f, wanted);
    }
    void update(float dt, float omega = 14.0f)
    {
        position.update(dt, omega);
    }
    float offset() const
    {
        return position.value;
    }
};

// A value that flashes to 1 and decays: press feedback, error shakes,
// "something happened here" glints.
struct Pulse
{
    float value = 0.0f;

    void trigger(float strength = 1.0f)
    {
        value = strength;
    }
    // rate ~ how fast it fades (1/s): 6 slow, 12 quick.
    void update(float dt, float rate = 8.0f)
    {
        value *= std::exp(-rate * dt);
        if (value < 1e-3f)
            value = 0.0f;
    }
};

// Horizontal offset of a decaying shake (refused input): x += shake(pulse, t).
inline float shake(float pulse, float time, float amplitude = 14.0f, float hz = 9.0f)
{
    return std::sin(time * hz * 6.2831853f) * amplitude * pulse;
}

// A slow breathing value in 0..1 (idle glows, "press to continue").
inline float breathe(float time, float period = 2.4f)
{
    return 0.5f - 0.5f * std::cos(time * 6.2831853f / period);
}

// Stereo position for a sound made at virtual x (0..1920): subtle on purpose.
inline float pan_for_x(float x)
{
    return std::clamp((x - 960.0f) / 960.0f, -1.0f, 1.0f) * 0.55f;
}

} // namespace hui::ui
