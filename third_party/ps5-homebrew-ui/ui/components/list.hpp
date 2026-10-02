// ps5-homebrew-ui - Component: ListView, a scrolling list with a gliding highlight.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/component.hpp"

#include <functional>
#include <string>
#include <vector>

namespace hui::ui
{

struct ListItem
{
    std::string title;
    std::string subtitle;                      // second line; empty for a one-line row
    std::string value;                         // right-aligned text
    std::string badge;                         // a small pill before the value ("NEW", "3")
    gfx::Color swatch{0.0f, 0.0f, 0.0f, 0.0f}; // a leading dot, when alpha > 0
    bool chevron = false;                      // "this opens something"
    bool disabled = false;                     // focusable, but refuses confirm
    bool header = false;                       // a section title: skipped by the focus
    int tag = 0;                               // yours
};

struct ListStyle : ComponentStyle
{
    // ---- geometry ----
    float row_height = 76.0f;
    float header_height = 52.0f;
    float gap = 6.0f;            // between rows
    float padding = 26.0f;       // inside a row, left and right
    float leading_width = 0.0f;  // room reserved for the `leading` slot
    float panel_padding = 14.0f; // between the panel and the rows (panel = true)
    // ---- type ----
    float title_size = 28.0f;
    float subtitle_size = 20.0f;
    float value_size = 24.0f;
    float header_size = 18.0f;
    // ---- look ----
    HighlightStyle highlight;
    bool panel = false;       // a themed panel behind the whole list
    bool cards = false;       // every row is a themed surface of its own
    bool dividers = false;    // hairlines between rows
    bool scroll_thumb = true; // shown only when the list overflows
    // ---- behaviour ----
    bool wrap = false;           // past the last row comes the first
    float focus_shift = 8.0f;    // the focused row's text moves this far right
    float edge_fade = 0.8f;      // rows fade over this share of a row at the clip edges
    float entrance_step = 0.03f; // seconds between rows arriving; 0 for none
    bool pitch_by_position = true;
};

// A vertical list: one focus that glides, spring scrolling that follows it,
// rows that fade at the edges, soft refusals at both ends.
//
//   ui::ListView list;
//   list.style.theme = theme;                       // any ui::Theme
//   list.style.highlight.kind = ui::HighlightKind::bar;
//   list.set_items({{"Continue"}, {"Options", "Sound, display, controller"}});
//   list.set_bounds({96, 240, 720, 600});
//   ...
//   if (list.handle(input, feedback) == ui::Event::activated) open(list.focus());
//   list.update(dt);
//   list.draw(canvas);
class ListView
{
  public:
    // row is the row's rectangle on screen; focus is 0..1.
    using Slot = std::function<void(Canvas &canvas, const gfx::Rect &row, const ListItem &item,
                                    int index, float focus)>;

    ListStyle style;
    Slot leading;  // draws into the first style.leading_width pixels of a row
    Slot trailing; // replaces the value, badge and chevron
    Slot content;  // replaces everything inside a row (the highlight stays)

    void set_items(std::vector<ListItem> items);
    const std::vector<ListItem> &items() const
    {
        return items_;
    }
    ListItem &item(int index)
    {
        return items_[static_cast<std::size_t>(index)];
    }
    void set_bounds(const gfx::Rect &bounds);
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }

    int focus() const
    {
        return focus_;
    }
    // Moves the focus without sound; snap skips the glide (use it on open).
    void set_focus(int index, bool snap = true);
    // An inactive list keeps a faint highlight and ignores nothing by itself:
    // simply do not call handle() while another component has the focus.
    void set_active(bool active)
    {
        active_ = active;
    }
    // Replays the entrance animation.
    void enter();

    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

    // Where a row is on screen right now (scroll applied).
    gfx::Rect row_rect(int index) const;

  private:
    gfx::Rect inner() const;
    float row_top(int index) const;
    float row_size(int index) const;
    float content_height() const;
    int step(int from, int direction) const;
    void retarget(bool snap);

    std::vector<ListItem> items_;
    std::vector<float> tops_;
    gfx::Rect bounds_{0.0f, 0.0f, 600.0f, 400.0f};
    int focus_ = 0;
    bool active_ = true;
    float age_ = 10.0f;
    Highlight highlight_;
    Scroller scroll_;
    tween::Spring active_amount_{1.0f, 0.0f, 1.0f};
    Pulse press_;
};

} // namespace hui::ui
