// ps5-homebrew-ui - Component: PauseMenu, the overlay a game shows when it stops.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/list.hpp"

#include <functional>
#include <string>
#include <vector>

namespace hui::ui
{

enum class PauseLayout : std::uint8_t
{
    left,   // the menu on the left, the side panel on the right
    center, // the menu in the middle (beside the side panel when there is one)
    right,  // mirrored
};

struct PauseMenuStyle : ComponentStyle
{
    // ---- geometry ----
    PauseLayout layout = PauseLayout::left;
    float width = 560.0f;      // the menu panel
    float margin = 96.0f;      // from the edges of the bounds (left and right layouts)
    float padding = 36.0f;     // inside the panels
    float side_width = 560.0f; // the side panel, drawn only when the slot is set
    float side_gap = 36.0f;    // between the two panels (centre layout)
    float side_height = 0.0f;  // 0: as tall as the menu panel
    float row_height = 66.0f;
    float row_gap = 6.0f;
    // ---- type ----
    float kicker_size = 18.0f; // the small word above the title ("PAUSED")
    float title_size = 46.0f;
    float subtitle_size = 22.0f;
    float item_size = 27.0f;
    // ---- look ----
    HighlightStyle highlight; // the menu's gliding highlight; a filled plate by default
    bool dividers = false;    // hairlines between the rows
    bool frosted = true;      // panels show the blurred game (needs canvas.glass), else solid
    float frost = 0.55f;      // how much surface colour covers the frost
    // The whole screen behind goes out of focus (needs canvas.glass). Turn it
    // off when the game is not in the blurred layer: the blur would hide it.
    bool backdrop_blur = true;
    float scrim = 0.6f; // opacity of the veil over the game
    gfx::Color scrim_color{0.0f, 0.0f, 0.0f, 1.0f};
    // ---- motion ----
    float entrance_step = 0.05f; // seconds between the rows arriving
    float travel = 90.0f;        // how far the panels slide in from their side
    float exit_speed = 1.8f;     // leaving is this much quicker than arriving
    // ---- behaviour ----
    bool wrap = true;          // past the last row comes the first
    bool close_on_back = true; // back resumes: closes and reports `cancelled`

    PauseMenuStyle()
    {
        highlight.kind = HighlightKind::fill;
    }
};

// The standard pause overlay: a scrim over the game, a panel with a title and
// a vertical menu (a ui::ListView) whose rows arrive one after another, and
// room beside it for whatever the game wants to show (objective, map, stats).
// While it is open it should receive every input.
//
//   ui::PauseMenu pause;
//   pause.style.theme = theme;
//   pause.title = "Hollow Reach";
//   pause.set_items({{"Resume"}, {"Options"}, {"Quit to title"}});
//   pause.side = [&](ui::Canvas &canvas, const gfx::Rect &area, float) { draw_map(area); };
//   ...
//   if (menu_pressed) pause.open(feedback);
//   if (pause.is_open())
//   {
//       const ui::Event event = pause.handle(input, feedback);
//       if (event == ui::Event::activated) act(pause.focus());
//       if (event == ui::Event::cancelled) resume();
//   }
//   pause.update(dt);
//   pause.draw(canvas); // last, so it is on top
class PauseMenu
{
  public:
    // area is the inside of the side panel; shown is 0..1 as it arrives.
    using Slot = std::function<void(Canvas &canvas, const gfx::Rect &area, float shown)>;

    PauseMenuStyle style;
    std::string kicker = "Paused"; // empty for none
    std::string title;
    std::string subtitle;
    Slot side; // the side panel's content; unset: no side panel

    void set_items(std::vector<ListItem> items);
    const std::vector<ListItem> &items() const
    {
        return list_.items();
    }
    ListItem &item(int index)
    {
        return list_.item(index);
    }
    // The area the scrim covers and the panels are placed in: the whole
    // canvas unless you say otherwise.
    void set_bounds(const gfx::Rect &bounds);
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }

    // Opens on the first row, replays the entrance and plays sounds.open.
    void open(Feedback &feedback);
    // Closes and plays sounds.close. Does nothing when already closed.
    void close(Feedback &feedback);
    // Closes without a sound or an exit animation (a screen change).
    void dismiss();
    bool is_open() const
    {
        return open_;
    }
    // Still on screen: open, or animating out.
    bool visible() const;

    int focus() const
    {
        return list_.focus();
    }
    void set_focus(int index)
    {
        list_.set_focus(index);
    }

    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

    // Where the panels rest.
    gfx::Rect panel_rect() const;
    gfx::Rect side_rect() const;

  private:
    float head_height() const;
    float rows_height() const;
    void sync();

    ListView list_;
    gfx::Rect bounds_{0.0f, 0.0f, gfx::kVirtualWidth, gfx::kVirtualHeight};
    bool open_ = false;
    float age_ = 10.0f;
    float applied_row_ = -1.0f; // the row geometry the list was last laid out with
    float applied_gap_ = -1.0f;
    tween::Spring fade_;
    tween::Bounce pop_;
};

} // namespace hui::ui
