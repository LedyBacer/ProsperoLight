// ps5-homebrew-ui - Component: Tooltip, a small label bubble pointing at something.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/overlay.hpp"

#include <string>

namespace hui::ui
{

enum class TooltipPlacement : std::uint8_t
{
    above,
    below,
    left,
    right,
};

struct TooltipStyle : ComponentStyle
{
    // ---- geometry ----
    TooltipPlacement placement = TooltipPlacement::above; // preferred; flips to stay on screen
    float gap = 8.0f;      // between the anchor and the pointer's tip
    float pointer = 10.0f; // the pointer's length; 0 for none
    float padding_x = 18.0f;
    float padding_y = 11.0f;
    float max_width = 440.0f;    // longer text wraps
    float screen_margin = 24.0f; // kept free at the edges of the bounds
    // ---- type ----
    float text_size = 20.0f;
    int max_lines = 3;
    // ---- look ----
    bool inverted = true; // a bubble in the text colour; false: a themed surface
    // ---- motion ----
    float delay = 0.0f;  // seconds between show() and the bubble appearing
    float slide = 10.0f; // it arrives from this far nearer the anchor
};

// A label that explains the thing it points at. It takes no input and plays
// no sound. Calling show() again with another anchor cross-fades to it, so
// it can simply follow the focus.
//
//   ui::Tooltip tip;
//   tip.style.theme = theme;
//   tip.style.delay = 0.4f;
//   if (focus_changed) tip.show(focused_rect, "Opens the save menu");
//   ...
//   tip.update(dt);
//   tip.draw(canvas); // after the things it may cover
class Tooltip
{
  public:
    // Where a bubble ends up: its box, the side it took and its pointer's tip.
    struct Placed
    {
        gfx::Rect bubble;
        TooltipPlacement placement = TooltipPlacement::above;
        float tip_x = 0.0f;
        float tip_y = 0.0f;
    };

    TooltipStyle style;

    // The area the bubble must stay inside: the whole canvas by default.
    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }

    // Shows text at an anchor after style.delay. The same text again only
    // moves the anchor, so it is safe to call every frame.
    void show(const gfx::Rect &anchor, std::string text);
    void hide();
    // Asked to show (it may still be waiting for its delay).
    bool is_shown() const
    {
        return shown_;
    }
    // Something of it is on screen.
    bool visible() const;
    const std::string &text() const
    {
        return now_.text;
    }

    void update(float dt);
    void draw(Canvas &canvas) const;

    // Where the current bubble rests. Its size follows the text: needs fonts.
    Placed place(const Canvas &canvas) const;

  private:
    struct Bubble
    {
        gfx::Rect anchor;
        std::string text;
    };

    Placed place(const Canvas &canvas, const Bubble &bubble) const;
    void draw_bubble(Canvas &canvas, const Bubble &bubble, float amount, bool arriving) const;

    gfx::Rect bounds_{0.0f, 0.0f, gfx::kVirtualWidth, gfx::kVirtualHeight};
    Bubble now_;
    Bubble before_; // the bubble being replaced, fading out
    bool shown_ = false;
    float wait_ = 0.0f;
    tween::Spring amount_;
    tween::Spring before_amount_;
};

} // namespace hui::ui
