// ps5-homebrew-ui - Component: TabBar, a row of tabs with one gliding indicator.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/component.hpp"

#include <functional>
#include <string>
#include <vector>

namespace hui::ui
{

struct TabItem
{
    std::string label;
    int badge = 0;         // a count after the label; 0 shows none, over 99 shows "99+"
    bool disabled = false; // dimmed and skipped by the focus
    int tag = 0;           // yours
};

enum class TabKind : std::uint8_t
{
    pill,      // a plate in the primary colour glides behind the active label
    underline, // text on a rule, with a bar that glides under the active label
    segmented, // one bordered control divided into segments, a raised piece inside
    boxed,     // every tab is a button surface of its own; the active one is filled
};

enum class TabWidth : std::uint8_t
{
    fit,   // every tab is as wide as its content
    equal, // every tab is as wide as the widest one
    fill,  // the tabs share the width of the bounds
};

struct TabBarStyle : ComponentStyle
{
    // ---- geometry ----
    TabKind kind = TabKind::pill;
    TabWidth width = TabWidth::fit;
    float height = 56.0f;     // of a tab; the bar is centred in its bounds
    float gap = 8.0f;         // between tabs (segments touch: they ignore it)
    float padding = 24.0f;    // inside a tab, left and right
    float min_width = 0.0f;   // a tab is never narrower than this
    float glyph_width = 0.0f; // room reserved for the `glyph` slot before the label
    float glyph_gap = 10.0f;  // between the glyph and the label
    float thickness = 4.0f;   // of the underline bar
    float track_inset = 5.0f; // between the track and the plate (pill and segmented)
    // ---- type ----
    float text_size = 24.0f;
    float badge_size = 17.0f;
    // ---- look ----
    bool track = true;      // pill: a well behind the row; underline: the rule
    bool dividers = true;   // segmented: hairlines between the segments
    bool focus_ring = true; // the theme's ring around the active tab while focused
    bool on_page = false;   // labels with no surface under them use the page's text colours
    float edge_fade = 0.6f; // tabs fade over this share of their width where the bar is cut
    // ---- behaviour ----
    bool wrap = false; // past the last tab comes the first
    bool pitch_by_position = true;
};

// A row of tabs. One indicator glides between them; the row scrolls when the
// tabs do not fit. It changes tab on left and right by itself, and a screen
// that drives it from the shoulder buttons calls step() or set_active().
//
//   ui::TabBar tabs;
//   tabs.style.theme = theme;
//   tabs.style.kind = ui::TabKind::underline;
//   tabs.set_tabs({{"Overview"}, {"Saves", 3}, {"Trophies"}});
//   tabs.set_bounds({400, 280, 900, 56});
//   ...
//   if (tabs.handle(input, feedback) == ui::Event::changed) show(tabs.active());
//   if (input.is_pressed(Action::jump_next)) tabs.step(1, input, feedback);
//   tabs.update(dt);
//   tabs.draw(canvas);
//   content_x = -tabs.active_value() * page_width;   // slide pages with it
class TabBar
{
  public:
    // box is the room reserved by style.glyph_width; active is 0..1; ink is
    // the colour the label is drawn in.
    using Glyph = std::function<void(Canvas &canvas, const gfx::Rect &box, const TabItem &item,
                                     int index, float active, gfx::Color ink)>;

    TabBarStyle style;
    Glyph glyph; // draws before the label, in style.glyph_width pixels

    void set_tabs(std::vector<TabItem> tabs);
    const std::vector<TabItem> &tabs() const
    {
        return tabs_;
    }
    TabItem &tab(int index)
    {
        return tabs_[static_cast<std::size_t>(index)];
    }
    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }

    int active() const
    {
        return active_;
    }
    // The active tab as an animated number: 1.4 is between tabs 1 and 2.
    // Slide content with it.
    float active_value() const
    {
        return value_.value;
    }
    // Selects a tab without sound. snap skips the glide (use it on open).
    void set_active(int index, bool snap = false);
    // One tab to the left (-1) or right (1), with the cue or the refusal:
    // what handle() does, for screens that turn tabs with L2 and R2.
    Event step(int direction, const InputFrame &input, Feedback &feedback);
    // Whether the bar has the screen's focus (it shows its ring then).
    void set_focused(bool focused)
    {
        focused_ = focused;
    }

    // Left and right change the tab. Everything else is the screen's.
    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

    // Where a tab is on screen right now (scroll applied). Tabs are sized by
    // their labels, so this needs the fonts the bar is drawn with.
    gfx::Rect tab_rect(const Fonts &fonts, int index) const;

  private:
    struct Layout
    {
        std::vector<gfx::Rect> tabs; // on screen, scroll applied
        gfx::Rect row;               // every tab, before clipping
        gfx::Rect indicator;         // the gliding plate, bar or piece
        bool overflow = false;
    };
    Layout layout(const Painter &paint) const;
    float content_width(const Painter &paint, const TabItem &item) const;
    int next(int from, int direction) const;
    float x_of(int index) const;

    std::vector<TabItem> tabs_;
    gfx::Rect bounds_{0.0f, 0.0f, 600.0f, 56.0f};
    int active_ = 0;
    bool focused_ = true;
    tween::Bounce value_;
    tween::Spring focus_amount_{1.0f, 0.0f, 1.0f};
    Pulse refusal_;
    Pulse change_;
};

} // namespace hui::ui
