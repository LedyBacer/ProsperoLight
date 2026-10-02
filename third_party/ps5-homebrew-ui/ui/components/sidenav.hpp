// ps5-homebrew-ui - Component: SideNav, a vertical rail that collapses to its icons.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/component.hpp"

#include <functional>
#include <string>
#include <vector>

namespace hui::ui
{

struct NavEntry
{
    std::string label;
    std::string badge;      // a pill at the end of the row ("3", "NEW"); a dot when collapsed
    std::string section;    // a section starts here, under this title
    bool separator = false; // a section starts here, without a title
    bool action = false;    // does something instead of going somewhere: never "current"
    bool disabled = false;  // focusable, but refuses confirm
    int tag = 0;            // yours
};

struct SideNavStyle : ComponentStyle
{
    // ---- geometry ----
    float collapsed_width = 92.0f; // icons only
    float expanded_width = 300.0f; // icons and labels
    float row_height = 60.0f;
    float gap = 6.0f;             // between rows
    float padding = 14.0f;        // between the rail's edge and its rows
    float icon_size = 30.0f;      // the square the `icon` slot draws in
    float label_gap = 14.0f;      // between the icon and the label
    float section_height = 46.0f; // room a section start takes above its first row
    // ---- type ----
    float text_size = 24.0f;
    float section_size = 16.0f;
    float badge_size = 16.0f;
    // ---- look ----
    HighlightStyle highlight;      // the focus that glides between entries
    bool panel = true;             // a themed panel behind the rail
    bool current_plate = true;     // a quiet plate under the current entry
    bool current_marker = true;    // a bar on the rail's edge beside the current entry
    float marker_thickness = 4.0f; // ... and how thick it is
    float entrance_step = 0.035f;  // seconds between entries arriving; 0 for none
    // ---- behaviour ----
    bool expanded = true; // the width the rail goes to; set_expanded() changes it
    bool footer = false;  // the last entry is pinned to the bottom of the rail
    bool wrap = false;    // past the last entry comes the first
    bool pitch_by_position = true;
};

// A navigation rail: an icon and a label per entry, one focus that glides,
// a marker for the entry the player is in, and two widths it animates
// between. Its height and position come from set_bounds(); its width is its
// own (rect() says where it is now, so the content beside it can follow).
//
//   ui::SideNav nav;
//   nav.style.theme = theme;
//   nav.style.footer = true;
//   nav.icon = [](ui::Canvas &canvas, const gfx::Rect &box, const ui::NavEntry &entry, int,
//                 float, gfx::Color ink) { draw_my_icon(canvas.list, entry.tag, box, ink); };
//   nav.set_entries({{"Home"}, {"Library"}, {"Store"}, {"Settings"}});
//   nav.set_bounds({96, 240, 0, 700});
//   ...
//   if (nav.handle(input, feedback) == ui::Event::activated) open(nav.focus());
//   nav.update(dt);
//   nav.draw(canvas);
//   content_left = nav.rect().x + nav.rect().w + 32;
class SideNav
{
  public:
    // box is a square of style.icon_size; focus is 0..1; ink is the colour
    // the label is drawn in.
    using Icon = std::function<void(Canvas &canvas, const gfx::Rect &box, const NavEntry &entry,
                                    int index, float focus, gfx::Color ink)>;

    SideNavStyle style;
    Icon icon; // draws an entry's icon; without it an entry shows its initial

    void set_entries(std::vector<NavEntry> entries);
    const std::vector<NavEntry> &entries() const
    {
        return entries_;
    }
    NavEntry &entry(int index)
    {
        return entries_[static_cast<std::size_t>(index)];
    }
    // Position and height. The width is ignored: the style has two.
    void set_bounds(const gfx::Rect &bounds);
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    // Where the rail is right now, its animated width included.
    gfx::Rect rect() const;
    // 0 collapsed .. 1 expanded, animated.
    float expansion() const;

    int focus() const
    {
        return focus_;
    }
    // The entry the player is in: the last one activated.
    int current() const
    {
        return current_;
    }
    // Both move without sound; snap skips the glide (use it on open).
    void set_focus(int index, bool snap = true);
    void set_current(int index, bool snap = false);
    // Whether the rail has the screen's focus. Without it the highlight
    // fades away and waits on the current entry.
    void set_focused(bool focused);
    void set_expanded(bool expanded)
    {
        style.expanded = expanded;
    }
    bool expanded() const
    {
        return style.expanded;
    }
    // Flips between the two widths with the change cue. Returns Event::changed.
    Event toggle(Feedback &feedback);
    // Replays the entrance animation.
    void enter();

    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

    // Where an entry is on screen right now (scroll and width applied).
    gfx::Rect row_rect(int index) const;

  private:
    bool pinned(int index) const;
    float lead(int index) const;
    float row_top(int index) const;
    float content_height() const;
    float view_height() const;
    float target_width() const;
    float width() const;
    gfx::Rect highlight_target() const;
    void retarget(bool snap);

    std::vector<NavEntry> entries_;
    gfx::Rect bounds_{0.0f, 0.0f, 300.0f, 600.0f};
    int focus_ = 0;
    int current_ = 0;
    bool focused_ = true;
    bool sized_ = false; // the width spring has been given its first value
    float age_ = 10.0f;
    Highlight highlight_;
    Scroller scroll_;
    tween::Spring width_;
    tween::Spring marker_; // the current entry's top, in content space
    tween::Spring focus_amount_{1.0f, 0.0f, 1.0f};
    Pulse press_;
};

} // namespace hui::ui
