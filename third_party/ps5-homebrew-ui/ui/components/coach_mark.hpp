// ps5-homebrew-ui - Component: CoachMark, a first-run tour that points at things.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The screen dims except for a spotlight on one control, and a bubble beside
// it says what the control is for. Confirm goes on, back goes back. Between
// steps the spotlight and the bubble glide, so the eye is led from one
// control to the next instead of searching for the new one.

#pragma once

#include "ui/components/overlay.hpp"
#include "ui/components/tooltip.hpp"
#include "ui/glyphs.hpp"

#include <string>
#include <vector>

namespace hui::ui
{

struct CoachStep
{
    gfx::Rect target; // what the step points at, on screen
    std::string title;
    std::string text;                                     // wraps; '\n' forces a line break
    TooltipPlacement placement = TooltipPlacement::below; // preferred side for the bubble
};

struct CoachMarkStyle : ComponentStyle
{
    // ---- the dim and the spotlight ----
    float scrim = 0.66f; // opacity of the dim
    gfx::Color scrim_color{0.0f, 0.0f, 0.0f, 1.0f};
    float spot_padding = 12.0f; // the spotlight is this much larger than the target
    float spot_radius = -1.0f;  // negative: from the theme's control radius
    float ring_width = 3.0f;    // the ring around the spotlight; 0 for none
    bool ring_pulse = true;     // light breathing around the ring
    // ---- the bubble ----
    float width = 460.0f;
    float padding = 26.0f;
    float gap = 16.0f;           // between the spotlight and the pointer's tip
    float pointer = 14.0f;       // the pointer's length; 0 for none
    float screen_margin = 96.0f; // kept free at the edges of the bounds (the safe area)
    float title_size = 28.0f;
    float text_size = 22.0f;
    float text_line = 1.4f; // line height, in text sizes
    int text_lines = 4;
    bool counter = true; // "2 of 4" over the title
    float counter_size = 18.0f;
    bool hints = true; // the row of button hints under the text
    float hint_size = 30.0f;
    float hint_text = 20.0f;
    // ---- words and glyphs ----
    std::string next_label = "Next";
    std::string done_label = "Done";
    std::string back_label = "Back";
    std::string skip_label = "Skip";
    Button confirm_glyph = Button::cross;
    Button back_glyph = Button::circle;
    // ---- behaviour ----
    bool nav_steps = true; // right and left also step forward and back
};

// A tour over a screen's own controls.
//
//   ui::CoachMark coach;
//   coach.style.theme = theme;
//   coach.start({{play_rect, "Play", "Starts where you left off."},
//                {grid_rect, "Your library", "Everything you own.", above}}, feedback);
//   ...
//   if (coach.is_open())                       // it takes every input
//   {
//       const ui::Event event = coach.handle(input, feedback);
//       if (event == ui::Event::activated || event == ui::Event::cancelled) remember_seen();
//   }
//   coach.update(dt);
//   coach.draw(canvas);                        // last: over the whole screen
class CoachMark
{
  public:
    CoachMarkStyle style;

    // The area the dim covers and the bubble stays inside: the whole canvas
    // by default.
    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }

    // Opens on the first step with the open cue. No steps: nothing happens.
    void start(std::vector<CoachStep> steps, Feedback &feedback);
    // Closes with the close cue.
    void stop(Feedback &feedback);
    // Closes without a sound (the screen is leaving).
    void dismiss()
    {
        open_ = false;
    }
    bool is_open() const
    {
        return open_;
    }
    bool visible() const
    {
        return open_ || amount_.value > 0.004f;
    }
    int step() const
    {
        return step_;
    }
    int count() const
    {
        return static_cast<int>(steps_.size());
    }
    // A target moved (the layout under the tour changed).
    void set_target(int index, const gfx::Rect &target);

    // The spotlight right now, and where the bubble rests for this step.
    gfx::Rect spotlight() const;
    Tooltip::Placed bubble(const Fonts &fonts) const;

    // Event::changed when the step changed, Event::activated when the last
    // step was confirmed, Event::cancelled when the tour was skipped,
    // Event::refused at either end of left and right.
    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    struct Laid
    {
        Tooltip::Placed at;
        std::vector<std::string> title;
        std::vector<std::string> text;
        float title_top = 0.0f; // relative to the bubble
        float text_top = 0.0f;
        float hints_cy = 0.0f;
    };
    Laid lay(const Fonts &fonts) const;
    gfx::Rect hole_for(int index) const;
    void retarget(bool snap);
    void draw_dim(Canvas &canvas, const gfx::Rect &hole, float radius, float fade) const;

    std::vector<CoachStep> steps_;
    gfx::Rect bounds_{0.0f, 0.0f, gfx::kVirtualWidth, gfx::kVirtualHeight};
    int step_ = 0;
    bool open_ = false;
    SpringRect hole_;
    tween::Spring amount_; // 0 closed .. 1 open
    tween::Spring words_;  // the bubble's text fades in after each step
    Pulse refusal_;
};

} // namespace hui::ui
