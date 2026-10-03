// ps5-homebrew-ui - Component: ChoicePicker, one value from a list between two arrows.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/component.hpp"

#include <functional>
#include <string>
#include <vector>

namespace hui::ui
{

struct ChoiceStyle : ComponentStyle
{
    // ---- geometry ----
    float arrow_size = 9.0f;   // half the height of an arrow
    float arrow_width = 3.0f;  // its stroke
    float arrow_inset = 22.0f; // from the bounds' edge to an arrow's centre
    float dot_size = 6.0f;     // the position dots (dots = true)
    // ---- type ----
    float value_size = 24.0f;
    // ---- look ----
    bool boxed = true;                      // a sunken well behind the control
    bool focus_ring = true;                 // the theme's ring around the bounds when focused
    bool dots = false;                      // one dot per option under the value
    bool dim_at_limit = true;               // an arrow that leads nowhere fades (wrap = false)
    float idle_arrows = 0.4f;               // the arrows' opacity without the focus
    bool on_page = false;                   // drawn straight on the page, not on a panel
    gfx::Color ink{0.0f, 0.0f, 0.0f, 0.0f}; // the value's colour; alpha 0: the theme's
    // ---- behaviour ----
    bool wrap = true;           // past the last option comes the first
    bool confirm_cycles = true; // confirm picks the next option; false: it returns activated
    float slide = 26.0f;        // how far the value travels when it changes
    float nudge = 6.0f;         // how far an arrow jumps when it is used
    bool pitch_by_direction = true;
};

// A value from a short list, shown as "<  Value  >". Left and right step
// through the options: the arrow that was used jumps, the old value leaves
// in the direction of travel and the new one follows it in.
//
//   ui::ChoicePicker difficulty;
//   difficulty.style.theme = theme;
//   difficulty.set_options({"Story", "Balanced", "Hard"});
//   difficulty.set_index(1);
//   difficulty.set_bounds({1200, 480, 360, 56});
//   ...
//   difficulty.set_active(focused);
//   if (focused && difficulty.handle(input, feedback) == ui::Event::changed)
//       apply(difficulty.index());
//   difficulty.update(dt);
//   difficulty.draw(canvas);
class ChoicePicker
{
  public:
    // area is where the value goes (between the arrows); focus is 0..1.
    using Slot = std::function<void(Canvas &canvas, const gfx::Rect &area, int index, float focus)>;

    ChoiceStyle style;
    Slot option; // draws an option instead of its text (a swatch, an icon)

    void set_options(std::vector<std::string> options);
    const std::vector<std::string> &options() const
    {
        return options_;
    }
    // Silent and without animation: for loading a stored value.
    void set_index(int index);
    int index() const
    {
        return index_;
    }
    // The selected option's text; empty when there are no options.
    const std::string &value() const;

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    void set_active(bool active)
    {
        active_ = active;
    }

    // Left and right pick (changed / refused), confirm picks the next option
    // or returns activated (style.confirm_cycles), back is cancelled.
    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

    // ---- for a host component -------------------------------------------
    // ui::Form keeps one picker per row and styles them all alike, so these
    // take the style, the place and the focus from the host. direction is -1
    // or +1; `refusal` is the pulse a refused input shakes.
    Event pick(int direction, bool may_wrap, const InputFrame &input, Feedback &feedback,
               const ChoiceStyle &look, float x, Pulse &refusal);
    void update(float dt, const ComponentStyle &look);
    void draw(Canvas &canvas, const ChoiceStyle &look, const gfx::Rect &rect, float focus) const;

  private:
    std::vector<std::string> options_;
    gfx::Rect bounds_{0.0f, 0.0f, 360.0f, 56.0f};
    int index_ = 0;
    int previous_ = 0;
    int direction_ = 1;
    bool active_ = false;
    tween::Spring focus_;
    tween::Spring slide_;  // 1 just after a change, easing to 0
    tween::Spring marker_; // the lit dot's position
    Pulse nudge_left_;
    Pulse nudge_right_;
    Pulse refusal_;
};

} // namespace hui::ui
