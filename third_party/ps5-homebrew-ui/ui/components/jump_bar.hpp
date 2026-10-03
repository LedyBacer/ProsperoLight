// ps5-homebrew-ui - Component: JumpBar, an alphabet index that jumps a long list to a letter.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/card.hpp"
#include "ui/components/component.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace hui::ui
{

struct JumpEntry
{
    std::string label;   // a letter, or any short label ("0-9", "New")
    bool enabled = true; // false: nothing in the list starts with it; dimmed and skipped
    int tag = 0;         // yours
};

// Where the bubble with the enlarged label appears, relative to the strip.
enum class JumpBubble : std::uint8_t
{
    before, // left of a vertical strip, above a horizontal one
    after,  // right of a vertical strip, below a horizontal one
    none,
};

struct JumpBarStyle : ComponentStyle
{
    // ---- geometry ----
    bool vertical = true;    // a column of labels; false lays them out in a row
    float item_size = 30.0f; // room per label along the strip (less when they do not fit)
    float thickness = 44.0f; // of the strip, across its direction
    float magnify = 1.3f;    // the current label is drawn this much larger
    // ---- type ----
    float text_size = 18.0f;
    // ---- the bubble ----
    JumpBubble bubble = JumpBubble::before;
    float bubble_size = 76.0f; // its side; it widens for a long label
    float bubble_text = 36.0f;
    float bubble_gap = 16.0f; // between the strip and the bubble
    float bubble_hold = 0.9f; // seconds it stays after a step() while the bar is not focused
    // ---- look ----
    bool track = true;      // a well behind the labels
    bool focus_ring = true; // the theme's ring round the strip while focused
    bool on_page = false;   // without a track: labels use the page's text colours
    // ---- behaviour ----
    bool wrap = false; // past the last label comes the first
    EdgeExits exits;   // edges that hand the focus back instead of refusing
    bool pitch_by_position = true;
};

// A strip of letters beside (or above) a long list. The current one sits on a
// marker that glides and is shown enlarged in a bubble. Letters that have no
// entries are dimmed and stepped over. It reports the chosen label; scrolling
// the list there is the screen's job.
//
//   ui::JumpBar index;
//   index.style.theme = theme;
//   index.set_entries(ui::JumpBar::alphabet());
//   index.set_enabled(index.find("Q"), false);       // nothing under Q
//   index.set_bounds({1700, 240, 44, 700});
//   ...
//   if (index.handle(input, feedback) == ui::Event::changed)      // it has the focus
//       list.set_focus(first_row_of(index.label()), false);
//   if (input.is_pressed(Action::jump_next))                      // or a shortcut
//       index.step(1, input, feedback);
//   index.set_current(index.find(letter_of(list.focus())));       // follow the list
//   index.update(dt);
//   index.draw(canvas);
class JumpBar
{
  public:
    JumpBarStyle style;

    // "A" to "Z".
    static std::vector<JumpEntry> alphabet();

    void set_entries(std::vector<JumpEntry> entries);
    const std::vector<JumpEntry> &entries() const
    {
        return entries_;
    }
    void set_enabled(int index, bool enabled);
    // The index of a label, or -1.
    int find(std::string_view label) const;
    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }

    int current() const
    {
        return current_;
    }
    // The current label ("" when there are no entries).
    const std::string &label() const;
    // Makes a label the current one without sound (the list moved by itself).
    // snap skips the glide.
    void set_current(int index, bool snap = false);
    // Whether the bar has the screen's focus: it shows the ring and the bubble.
    void set_focused(bool focused)
    {
        focused_ = focused;
    }
    // The edge the last handle() left through, or Direction::none.
    Direction exit() const
    {
        return exit_;
    }

    // Goes to the next enabled label in a direction (1 or -1) with a cue, for
    // a shortcut button. Event::changed, or Event::refused at an end.
    Event step(int direction, const InputFrame &input, Feedback &feedback);
    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

    // The strip, and where one label sits in it.
    gfx::Rect strip_rect() const;
    gfx::Rect item_rect(int index) const;

  private:
    float pitch() const;
    float along(float position) const;
    int next_enabled(int from, int direction) const;

    std::vector<JumpEntry> entries_;
    gfx::Rect bounds_{0.0f, 0.0f, 44.0f, 600.0f};
    int current_ = 0;
    bool focused_ = false;
    Direction exit_ = Direction::none;
    float linger_ = 0.0f;    // seconds the bubble still shows after a step
    tween::Bounce position_; // the current label as an animated number
    tween::Bounce bubble_;   // 0 hidden .. 1 shown
    tween::Spring focus_amount_;
    Pulse refusal_;
    Pulse tick_; // the marker pops on every step
};

} // namespace hui::ui
