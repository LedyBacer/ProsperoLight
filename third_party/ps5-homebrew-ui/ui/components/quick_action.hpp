// ps5-homebrew-ui - Components: QuickAction and QuickActionBar.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// An action that belongs to a face button instead of to the focus: "Triangle:
// Search", "Square: Sort". It floats in a corner as a pill showing the glyph
// and a label, and answers its button wherever the focus is.

#pragma once

#include "ui/components/button.hpp"
#include "ui/components/button_group.hpp"
#include "ui/components/toast.hpp"

#include <string>
#include <vector>

namespace hui::ui
{

// Where in its bounds a quick action sits: the six places a toast can.
using QuickAnchor = ToastAnchor;

struct QuickActionStyle : ComponentStyle
{
    QuickAnchor anchor = QuickAnchor::bottom_right;
    ButtonRole role = ButtonRole::secondary;
    // ---- geometry ----
    float margin = 0.0f; // from the edges of the bounds
    float height = 56.0f;
    float padding = 12.0f;      // before the glyph
    float text_padding = 22.0f; // after the label
    float gap = 10.0f;          // between the glyph and the label
    float glyph_size = 32.0f;
    float text_size = 22.0f;
    bool pill = true;         // fully round where the theme has round corners
    bool glyph_tinted = true; // the glyph keeps its DualSense colours; false: the ink
    // ---- behaviour ----
    float collapse_after = 0.0f; // seconds until the label folds away; 0: never
    bool expand_on_press = true; // pressing it unfolds the label again
    // ---- badge ----
    Status badge_kind = Status::danger;
    float badge_height = 24.0f;
    float badge_text = 15.0f;
    int badge_max = 99;
    // ---- feel ----
    float press_scale = 0.06f;
    float ripple = 18.0f; // how far the ring of light travels on a press; 0 for none
    float rumble = 0.3f;
};

// One floating action.
//
//   ui::QuickAction search;
//   search.label = "Search";
//   search.glyph = ui::Button::triangle;
//   search.action = Action::north;
//   search.set_bounds(page_area);              // it places itself in a corner
//   ...
//   if (search.handle(input, feedback) == ui::Event::activated) open_search();
//   search.update(dt);
//   search.draw(canvas);
//
// Call handle() every frame, whatever has the focus: it only looks at its
// own action.
class QuickAction
{
  public:
    QuickActionStyle style;
    std::string label;
    Button glyph = Button::triangle;
    Action action = Action::north;

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    // Disabled: dimmed, and its button is refused.
    void set_enabled(bool enabled)
    {
        enabled_ = enabled;
    }
    bool enabled() const
    {
        return enabled_;
    }
    // Hidden: faded out, and its button is ignored (a dialog is open).
    void set_visible(bool visible, bool snap = false);
    bool visible() const
    {
        return visible_;
    }
    // A count on its corner; 0 hides it.
    void set_badge(int count);
    int badge() const
    {
        return badge_.count();
    }
    // Unfolds the label and restarts the collapse timer.
    void expand();
    bool collapsed() const
    {
        return collapse_.target > 0.5f;
    }

    // Its width now (the fold is animated) and where it is drawn.
    float width(const Fonts &fonts) const;
    gfx::Rect rect(const Fonts &fonts) const;

    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    gfx::Rect bounds_{0.0f, 0.0f, gfx::kVirtualWidth, gfx::kVirtualHeight};
    bool enabled_ = true;
    bool visible_ = true;
    float age_ = 0.0f;
    tween::Spring collapse_;
    tween::Spring shown_{1.0f, 0.0f, 1.0f};
    Pulse press_;
    Pulse refusal_;
    Badge badge_;
};

struct QuickActionItem
{
    std::string label;
    Button glyph = Button::triangle;
    Action action = Action::north;
};

struct QuickActionBarStyle : QuickActionStyle
{
    GroupLayout layout = GroupLayout::row; // side by side, or stacked
    float spacing = 12.0f;                 // between the actions
};

// Several quick actions kept together in one corner.
//
//   ui::QuickActionBar bar;
//   bar.set_actions({{"Search", ui::Button::triangle, Action::north},
//                    {"Sort", ui::Button::square, Action::west}});
//   bar.set_bounds(page_area);
//   ...
//   if (bar.handle(input, feedback) == ui::Event::activated) run(bar.fired());
//   bar.update(dt);
//   bar.draw(canvas);
class QuickActionBar
{
  public:
    QuickActionBarStyle style;

    void set_actions(const std::vector<QuickActionItem> &items);
    int count() const
    {
        return static_cast<int>(actions_.size());
    }
    // One of its actions, for set_enabled(), set_badge(), expand().
    QuickAction &at(int index)
    {
        return actions_[static_cast<std::size_t>(index)];
    }
    const QuickAction &at(int index) const
    {
        return actions_[static_cast<std::size_t>(index)];
    }
    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    void set_visible(bool visible, bool snap = false);
    // The action the last Event::activated or Event::refused came from.
    int fired() const
    {
        return fired_;
    }
    // Where an action is drawn.
    gfx::Rect rect(const Fonts &fonts, int index) const;

    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    void apply_style();

    std::vector<QuickAction> actions_;
    gfx::Rect bounds_{0.0f, 0.0f, gfx::kVirtualWidth, gfx::kVirtualHeight};
    int fired_ = -1;
};

} // namespace hui::ui
