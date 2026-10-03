// ps5-homebrew-ui - Component: DetailList, labelled values in three layouts.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/card.hpp"
#include "ui/components/data_common.hpp"

#include <functional>
#include <string>
#include <vector>

namespace hui::ui
{

struct DetailItem
{
    std::string label;
    std::string value;
    bool header = false;                      // a section title: only `label` is shown
    bool focusable = false;                   // takes the focus; confirm returns activated
    bool numeric = false;                     // the value is set in the number face (one line)
    gfx::Color color{0.0f, 0.0f, 0.0f, 0.0f}; // the value's colour; alpha 0: the text colour
    int tag = 0;                              // yours
};

enum class DetailLayout : std::uint8_t
{
    rows,    // label left, value right, one pair per row
    stacked, // label above its value
    grid,    // `columns` columns of stacked pairs
};

struct DetailListStyle : ComponentStyle
{
    DetailLayout layout = DetailLayout::rows;
    // ---- geometry ----
    int columns = 2;             // DetailLayout::grid
    float row_height = 46.0f;    // the least height of a pair in the rows layout
    float header_height = 44.0f; // a section title
    float padding = 16.0f;       // inside a pair, left and right
    float padding_y = 9.0f;      // above and below the text of a pair
    float gap = 2.0f;            // between pairs
    float column_gap = 20.0f;    // between grid columns
    float panel_padding = 12.0f; // between the panel and the pairs (panel = true)
    float label_share = 0.4f;    // rows layout: the label's share of the width
    float stack_gap = 4.0f;      // stacked and grid: between a label and its value
    // ---- type ----
    float label_size = 19.0f;
    float value_size = 22.0f;
    float header_size = 17.0f;
    int max_lines = 2; // of a value: 1 cuts it with "...", more wraps it
    // ---- look ----
    HighlightStyle highlight;
    bool panel = true;        // a themed panel behind the list
    bool dividers = true;     // hairlines between pairs (rows and stacked layouts)
    bool scroll_thumb = true; // shown only when the pairs overflow
    // ---- behaviour ----
    EdgeExits exits; // edges that hand the focus back to the screen
};

// Key and value pairs: an "About" page, a spec sheet, the details of a save.
// Sections have titles, long values wrap or are cut, and rows marked
// focusable can be confirmed (to copy a value, to open what it names).
//
//   ui::DetailList about;
//   about.style.layout = ui::DetailLayout::grid;
//   about.set_items({{"System", "", true}, {"Version", "1.4.2"}, {"Build", "8841", false, true}});
//   about.set_bounds({96, 300, 640, 320});
//   ...
//   if (about.handle(input, feedback) == ui::Event::activated) copy(about.focus());
class DetailList
{
  public:
    // area is where the value goes; focus is 0..1.
    using Slot = std::function<void(Canvas &canvas, const gfx::Rect &area, const DetailItem &item,
                                    int index, float focus)>;

    DetailListStyle style;
    Slot value; // draws a value instead of its text (the slot gets one line's height)

    void set_items(std::vector<DetailItem> items);
    const std::vector<DetailItem> &items() const
    {
        return items_;
    }
    DetailItem &item(int index)
    {
        return items_[static_cast<std::size_t>(index)];
    }
    void set_bounds(const gfx::Rect &bounds);
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }

    // The focused item, or -1 when nothing is focusable.
    int focus() const
    {
        return focus_;
    }
    void set_focus(int index, bool snap = true);
    void set_active(bool active)
    {
        active_ = active;
    }
    // Replays the entrance animation.
    void enter();
    // Lays the list out for these fonts now. Optional: wrapped values are
    // measured whenever the list is drawn and the layout follows on the next
    // update, so only a list that must be right on its very first frame (or
    // that is laid out without being drawn) needs this.
    void measure(const Fonts &fonts);

    Event handle(const InputFrame &input, Feedback &feedback);
    // The edge the focus left through on the last handle(), or Direction::none.
    Direction exit() const
    {
        return exit_;
    }
    void update(float dt);
    void draw(Canvas &canvas) const;

    // Where an item is on screen right now (scroll applied).
    gfx::Rect item_rect(int index) const;

  private:
    gfx::Rect inner() const;
    void relayout();
    void retarget(bool snap);
    int neighbour(Direction direction) const;
    float line_height() const;
    float value_room(int index) const;
    int count_lines(const Painter &paint, int index) const;

    std::vector<DetailItem> items_;
    std::vector<gfx::Rect> rects_; // in content space
    // How many lines each value takes in the theme's face. Only drawing has
    // the fonts, so draw() notes the counts and the next update() lays the
    // list out with them. A measurement, not state.
    mutable std::vector<int> lines_;
    gfx::Rect bounds_{0.0f, 0.0f, 560.0f, 360.0f};
    float content_ = 0.0f;
    int focus_ = -1;
    bool active_ = true;
    bool settle_ = true; // the next layout places the highlight without a glide
    float age_ = 10.0f;
    Direction exit_ = Direction::none;
    Highlight highlight_;
    Scroller scroll_;
    tween::Spring active_amount_{1.0f, 0.0f, 1.0f};
    Pulse press_;
};

} // namespace hui::ui
