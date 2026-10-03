// ps5-homebrew-ui - Component: Stepper, a whole number between a minus and a plus.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/component.hpp"

#include <functional>
#include <string>
#include <utility>

namespace hui::ui
{

enum class StepperButtons : std::uint8_t
{
    surface, // two small buttons in the theme's surface style
    plain,   // bare minus and plus signs
};

struct StepperStyle : ComponentStyle
{
    // ---- geometry ----
    float button_size = 44.0f; // the square minus and plus buttons (capped by the height)
    float sign_size = 9.0f;    // half the length of a sign's stroke
    float sign_width = 3.0f;   // its thickness
    float box_padding = 5.0f;  // between the well and the buttons (boxed = true)
    // ---- type ----
    float value_size = 26.0f;
    // ---- look ----
    StepperButtons buttons = StepperButtons::surface;
    bool boxed = false;                     // a sunken well behind the whole control
    bool focus_ring = true;                 // the theme's ring around the bounds when focused
    bool dim_at_limit = true;               // the sign that can go no further fades
    bool on_page = false;                   // drawn straight on the page, not on a panel
    gfx::Color ink{0.0f, 0.0f, 0.0f, 0.0f}; // the number's colour; alpha 0: the theme's
    // ---- behaviour ----
    bool wrap = false;          // past the maximum comes the minimum
    int fast_after = 8;         // held repeats before the stride grows; 0: never
    int fast_factor = 5;        // ... to this many steps at once
    float roll = 14.0f;         // how far the number travels when it changes
    bool pitch_by_value = true; // the step cue rises with the value
};

// An integer with a minimum, a maximum and a step, drawn as "-  12  +". Left
// and right change it; holding a direction speeds up; the limits refuse softly.
//
//   ui::Stepper players;
//   players.style.theme = theme;
//   players.set_range(1, 4);
//   players.set_value(2);
//   players.set_bounds({1200, 400, 220, 56});
//   ...
//   players.set_active(focused);
//   if (focused && players.handle(input, feedback) == ui::Event::changed)
//       apply(players.value());
//   players.update(dt);
//   players.draw(canvas);
class Stepper
{
  public:
    using Format = std::function<std::string(int value)>;

    StepperStyle style;
    Format format; // the text of a value; without it, the number and the suffix

    void set_range(int minimum, int maximum, int step = 1);
    // Silent and without animation: for loading a stored value.
    void set_value(int value);
    void set_suffix(std::string suffix)
    {
        suffix_ = std::move(suffix);
    }
    int value() const
    {
        return value_;
    }
    int minimum() const
    {
        return min_;
    }
    int maximum() const
    {
        return max_;
    }
    int step() const
    {
        return step_;
    }
    // Where the value sits between the limits, 0..1.
    float fraction() const;
    // What is shown for the current value.
    std::string text() const
    {
        return text_for(value_);
    }

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    // Focused or not; the amount eases, so the ring fades in and out.
    void set_active(bool active)
    {
        active_ = active;
    }

    // Left and right change the value (changed / refused); back is cancelled.
    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

    // ---- for a host component -------------------------------------------
    // ui::Form keeps one Stepper per row and styles them all alike, so these
    // take the style, the place and the focus from the host instead of from
    // the members above. `refusal` is the pulse a refused input shakes.
    Event step(const InputFrame &input, Feedback &feedback, const StepperStyle &look, float x,
               Pulse &refusal);
    void update(float dt, const ComponentStyle &look);
    void draw(Canvas &canvas, const StepperStyle &look, const gfx::Rect &rect, float focus) const;

  private:
    std::string text_for(int value) const;

    gfx::Rect bounds_{0.0f, 0.0f, 220.0f, 56.0f};
    std::string suffix_;
    int value_ = 0;
    int previous_ = 0;
    int min_ = 0;
    int max_ = 10;
    int step_ = 1;
    int direction_ = 1;
    int held_ = 0;
    bool active_ = false;
    tween::Spring focus_;
    tween::Spring roll_; // 1 just after a change, easing to 0
    Pulse press_minus_;
    Pulse press_plus_;
    Pulse refusal_;
};

} // namespace hui::ui
