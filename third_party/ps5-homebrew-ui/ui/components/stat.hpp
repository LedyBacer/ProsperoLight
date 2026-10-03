// ps5-homebrew-ui - Components: StatTile and EmptyState.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/counter.hpp"
#include "ui/components/progress.hpp"
#include "ui/glyphs.hpp"

#include <functional>
#include <span>
#include <string>
#include <vector>

namespace hui::ui
{

// ---- StatTile --------------------------------------------------------------

struct StatTileStyle : ComponentStyle
{
    // ---- geometry ----
    float padding = 22.0f;
    float label_size = 20.0f;
    float value_size = 48.0f; // shrinks if the figure would not fit
    float delta_size = 20.0f;
    float spark_height = 54.0f;
    float spark_width = 3.0f; // the line's thickness
    // ---- look ----
    bool panel = true;     // a themed panel behind the tile
    bool sparkline = true; // the built-in line, or the `sparkline` slot
    bool spark_dot = true; // a dot on the newest value
    Status spark_status = Status::accent;
    gfx::Color spark_color{0.0f, 0.0f, 0.0f, 0.0f}; // alpha > 0 overrides spark_status
    // ---- value ----
    CounterFormat format = CounterFormat::integer;
    int decimals = 1;
    std::string prefix;
    std::string suffix;
    bool rolling = false;
    // ---- delta ----
    bool show_delta = true;
    bool up_is_good = true; // false for figures that should fall (errors, latency)
    int delta_decimals = 1;
    std::string delta_note; // muted text after the delta ("this week")
};

// One figure and its story: what it is, how much, which way it is going.
//
//   ui::StatTile tile;
//   tile.label = "Players online";
//   tile.set_bounds({96, 300, 280, 230});
//   tile.set_value(12480);
//   tile.set_delta(4.2f);                  // percent
//   tile.set_series(history);              // a std::span<const float>
class StatTile
{
  public:
    // area is where the sparkline goes.
    using Slot = std::function<void(Canvas &canvas, const gfx::Rect &area)>;

    StatTileStyle style;
    std::string label;
    Slot sparkline; // replaces the built-in line

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    void set_value(double value, bool snap = false);
    double value() const
    {
        return counter_.value();
    }
    // The change in percent; its sign picks the arrow and the colour.
    void set_delta(float percent);
    float delta() const
    {
        return delta_;
    }
    // The values of the built-in sparkline, oldest first. A series of the
    // same length as the last one morphs into place; another length redraws.
    void set_series(std::span<const float> values);
    // The figure, for reading its text in tests and tools.
    const Counter &counter() const
    {
        return counter_;
    }

    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    void sync();

    gfx::Rect bounds_{0.0f, 0.0f, 280.0f, 230.0f};
    Counter counter_;
    float delta_ = 0.0f;
    tween::Bounce arrow_{1.0f, 0.0f, 1.0f};
    std::vector<float> series_;
    std::vector<float> previous_;
    tween::Spring morph_{1.0f, 0.0f, 1.0f};
    tween::Spring reveal_{1.0f, 0.0f, 1.0f};
};

// ---- EmptyState ------------------------------------------------------------

struct EmptyStateStyle : ComponentStyle
{
    float icon_size = 96.0f;
    float title_size = 30.0f;
    float body_size = 22.0f;
    float hint_size = 22.0f;
    float gap = 16.0f; // between icon, title, body and hint
    float max_text_width = 520.0f;
    int max_lines = 3; // of the body
    float padding = 24.0f;
    bool panel = false;     // a themed panel behind it
    bool float_icon = true; // the icon drifts a few pixels while idle
};

// "Nothing here yet", said kindly: an icon, a title, a line or two on what
// would fill the space, and the button that does it.
class EmptyState
{
  public:
    // area is a square of style.icon_size.
    using Slot = std::function<void(Canvas &canvas, const gfx::Rect &area)>;

    EmptyStateStyle style;
    std::string title;
    std::string body;
    std::string action;                   // "Add a game"; empty for no hint
    Button action_button = Button::cross; // the glyph before the action
    Slot icon;                            // replaces the built-in icon

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    // Replays the entrance.
    void enter();

    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    gfx::Rect bounds_{0.0f, 0.0f, 560.0f, 360.0f};
    float age_ = 10.0f;
    float phase_ = 0.0f;
};

} // namespace hui::ui
