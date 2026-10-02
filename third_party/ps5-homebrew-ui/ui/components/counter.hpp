// ps5-homebrew-ui - Component: Counter, a number that counts to its target.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/component.hpp"

#include <cstdint>
#include <string>

namespace hui::ui
{

enum class CounterFormat : std::uint8_t
{
    integer, // 1,234,567
    decimal, // 1,234.5 (style.decimals digits after the point)
    percent, // 64.2%
    time,    // seconds as mm:ss, or h:mm:ss from one hour up
};

enum class CounterFace : std::uint8_t
{
    label,   // the theme's label face
    heading, // the theme's heading face: big figures
};

struct CounterStyle : ComponentStyle
{
    CounterFormat format = CounterFormat::integer;
    int decimals = 1;     // decimal and percent: digits after the point
    char separator = ','; // between thousands; 0 for none
    char point = '.';     // the decimal mark
    std::string prefix;   // drawn before the number, smaller ("$")
    std::string suffix;   // drawn after it, smaller ("pts")
    // ---- look ----
    float size = 48.0f;
    CounterFace face = CounterFace::heading;
    gfx::Color color{0.0f, 0.0f, 0.0f, 0.0f}; // alpha 0: the theme's text colour
    float affix_scale = 0.5f;                 // prefix and suffix, as a share of size
    gfx::Align align = gfx::Align::left;      // where the number sits in the bounds
    // ---- behaviour ----
    bool rolling = false; // digits roll vertically, like an odometer
    float rate = 0.35f;   // how fast it counts, as a share of the theme's speed
};

// A figure that never jumps and never jitters: it counts toward the value
// you set, and every digit has a cell as wide as the widest digit, so the
// number keeps its width while it changes.
//
//   ui::Counter score;
//   score.style.rolling = true;
//   score.set_bounds({96, 300, 400, 60});
//   score.set_value(12480);   // counts there
class Counter
{
  public:
    CounterStyle style;

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    // snap shows the value at once (use it when the counter appears).
    void set_value(double value, bool snap = false);
    double value() const
    {
        return to_;
    }
    // The number on screen right now, on its way to value().
    double shown() const;
    // The settled text ("1,234", "03:25") for a value in this style's format.
    std::string format(double value) const;
    // What is drawn right now, without prefix and suffix (the rolling
    // variant shows the digit it is leaving).
    std::string text() const;
    // The width draw() will use at its settled value, in this theme.
    float width(const Painter &paint) const;

    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    double units(double value) const;
    std::string format_units(long long units) const;

    gfx::Rect bounds_{0.0f, 0.0f, 300.0f, 60.0f};
    // A critically damped spring in double precision: a float one loses its
    // last digits on large numbers. A counter is often given a new target
    // while it still moves, and the spring keeps its speed when that happens.
    double shown_ = 0.0;
    double velocity_ = 0.0;
    double to_ = 0.0;
};

} // namespace hui::ui
