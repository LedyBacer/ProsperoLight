// ps5-homebrew-ui - Component: Timeline, a vertical activity feed on a rail.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/card.hpp"
#include "ui/components/data_common.hpp"
#include "ui/components/progress.hpp"

#include <functional>
#include <string>
#include <vector>

namespace hui::ui
{

struct TimelineEntry
{
    std::string time; // "14:02", "Yesterday"
    std::string title;
    std::string body;                         // wraps to style.body_lines lines
    Status status = Status::primary;          // the node's colour
    gfx::Color color{0.0f, 0.0f, 0.0f, 0.0f}; // alpha > 0 overrides status
    float card_height = 0.0f;                 // more than 0 reserves room for the `card` slot
    bool group = false;                       // a group title ("Today"): only `title` is shown
    int tag = 0;                              // yours
};

enum class TimelineTime : std::uint8_t
{
    column, // in a column of its own, left of the rail
    line,   // at the end of the title's line
    none,
};

enum class TimelineNode : std::uint8_t
{
    filled, // a dot in the status colour
    hollow, // a ring in the status colour
};

struct TimelineStyle : ComponentStyle
{
    // ---- geometry ----
    float time_width = 86.0f;    // TimelineTime::column: the column's width
    float node_size = 16.0f;     // the node's diameter
    float rail_width = 3.0f;     // the line's thickness
    float rail_gap = 18.0f;      // between the rail and the text
    float entry_gap = 6.0f;      // between entries
    float padding = 14.0f;       // inside an entry, left and right
    float padding_y = 10.0f;     // above and below an entry's text
    float group_height = 40.0f;  // a group title
    float panel_padding = 12.0f; // between the panel and the entries (panel = true)
    // ---- type ----
    float title_size = 22.0f;
    float body_size = 19.0f;
    float time_size = 18.0f;
    float group_size = 17.0f;
    int body_lines = 2; // 0 leaves the body out
    // ---- look ----
    HighlightStyle highlight;
    TimelineTime time = TimelineTime::column;
    TimelineNode node = TimelineNode::filled;
    bool panel = true;        // a themed panel behind the feed
    bool scroll_thumb = true; // shown only when the entries overflow
    // ---- behaviour ----
    float draw_in = 0.7f; // seconds the rail takes to draw itself on enter(); 0: at once
    EdgeExits exits;      // edges that hand the focus back to the screen
};

// What happened, newest first: a rail with one node per entry, coloured by
// what the entry reports, with the time, a title, a few words and room for a
// card of yours. The rail draws itself when the feed appears.
//
//   ui::Timeline feed;
//   feed.set_entries({{"", "Today", "", {}, {}, 0, true},
//                     {"14:02", "Trophy earned", "First light", ui::Status::success}});
//   feed.set_bounds({96, 300, 560, 420});
//   feed.enter();
//   ...
//   if (feed.handle(input, feedback) == ui::Event::activated) open(feed.focus());
class Timeline
{
  public:
    // node is the square the node occupies; focus is 0..1.
    using IconSlot = std::function<void(Canvas &canvas, const gfx::Rect &node,
                                        const TimelineEntry &entry, int index, float focus)>;
    // area is entry.card_height tall, under the entry's text.
    using CardSlot = std::function<void(Canvas &canvas, const gfx::Rect &area,
                                        const TimelineEntry &entry, int index, float focus)>;

    TimelineStyle style;
    IconSlot icon; // draws a node instead of the dot
    CardSlot card; // draws the attached card of entries that reserve room for one

    void set_entries(std::vector<TimelineEntry> entries);
    const std::vector<TimelineEntry> &entries() const
    {
        return entries_;
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
    void set_focus(int index, bool snap = true);
    void set_active(bool active)
    {
        active_ = active;
    }
    // Replays the entrance: the rail draws itself and the entries follow it.
    void enter();
    // Lays the feed out for these fonts now. Optional: wrapped bodies are
    // measured whenever the feed is drawn and the layout follows on the next
    // update, so only a feed that must be right on its very first frame needs
    // this.
    void measure(const Fonts &fonts);

    Event handle(const InputFrame &input, Feedback &feedback);
    // The edge the focus left through on the last handle(), or Direction::none.
    Direction exit() const
    {
        return exit_;
    }
    void update(float dt);
    void draw(Canvas &canvas) const;

    // Where an entry is on screen right now (scroll applied).
    gfx::Rect entry_rect(int index) const;

  private:
    gfx::Rect inner() const;
    float rail_x() const;
    float title_line() const;
    float body_line() const;
    int step(int from, int direction) const;
    float body_room() const;
    void relayout();
    void retarget(bool snap);

    std::vector<TimelineEntry> entries_;
    std::vector<float> tops_;    // in content space
    std::vector<float> heights_; // of each entry
    // Lines each body takes in the theme's face: noted by draw(), which has
    // the fonts, and used by the next update(). A measurement, not state.
    mutable std::vector<int> lines_;
    gfx::Rect bounds_{0.0f, 0.0f, 560.0f, 420.0f};
    float content_ = 0.0f;
    int focus_ = 0;
    bool active_ = true;
    bool settle_ = true;
    float age_ = 10.0f;
    Direction exit_ = Direction::none;
    Highlight highlight_;
    Scroller scroll_;
    tween::Spring active_amount_{1.0f, 0.0f, 1.0f};
    Pulse press_;
};

} // namespace hui::ui
