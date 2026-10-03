// ps5-homebrew-ui - Component: Rating, stars that fill, for display and for input.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/progress.hpp"

#include <cstdint>
#include <vector>

namespace hui::ui
{

enum class RatingGlyph : std::uint8_t
{
    star,
    heart,
    dot,
};

struct RatingStyle : ComponentStyle
{
    RatingGlyph glyph = RatingGlyph::star;
    int count = 5;      // how many glyphs
    float size = 36.0f; // of one glyph
    float gap = 8.0f;   // between glyphs
    // ---- look ----
    Status status = Status::warning;          // the filled colour (stars are gold)
    gfx::Color color{0.0f, 0.0f, 0.0f, 0.0f}; // alpha > 0 overrides status
    gfx::Color empty{0.0f, 0.0f, 0.0f, 0.0f}; // unfilled glyphs; alpha 0: a muted surface tone
    bool show_value = false;                  // "3.5" after the glyphs
    float value_size = 22.0f;
    float value_gap = 18.0f; // between the glyphs and that text
    int value_decimals = 0;  // digits after the point in that text
    float pop = 1.0f;        // how hard a glyph bounces as it fills; 0 for none
    // ---- behaviour ----
    bool interactive = true; // false: handle() ignores everything
    float step = 1.0f;       // what one press changes; 0.5 gives half stars
    bool allow_zero = true;  // may the value go down to none
    bool pitch_by_value = true;
};

// A score out of N. For display it fills fractions ("4.3 of 5"); with the
// focus, left and right change it, each glyph pops as it fills and the cue
// climbs with the value.
//
//   ui::Rating rating;
//   rating.set_bounds({96, 300, 240, 44});
//   rating.set_value(3.0f);
//   if (rating.handle(input, feedback) == ui::Event::changed) save(rating.value());
class Rating
{
  public:
    RatingStyle style;

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    // 0..count; fractions are drawn as partly filled glyphs.
    void set_value(float value, bool snap = false);
    float value() const
    {
        return value_;
    }
    // Shows the theme's focus ring around it while it has the focus.
    void set_active(bool active)
    {
        active_ = active;
    }
    // The width of the glyph row (without the value text).
    float width() const;

    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    gfx::Rect bounds_{0.0f, 0.0f, 240.0f, 44.0f};
    float value_ = 0.0f;
    tween::Spring shown_;
    std::vector<tween::Bounce> pops_;
    std::vector<bool> full_;
    bool active_ = false;
    tween::Spring focus_;
    Pulse refusal_;
    bool started_ = false;
};

} // namespace hui::ui
