// ps5-homebrew-ui - Components: Panel, Divider, SectionHeader and Spacer.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The furniture of a screen: the surface content sits on, the line between
// two groups, the title above one. None of them takes input; they are placed
// by the layout helpers (layout.hpp) and made focusable, when a screen wants
// that, by a ui::FocusGroup.
//
//   ui::Panel panel;
//   panel.style.kind = ui::PanelKind::titled;
//   panel.title = "Storage";
//   panel.set_bounds(rect);
//   panel.draw(canvas);
//   draw_my_content(panel.content_rect());
//
// A panel, a divider and a section header can also be drawn "at" a rectangle
// without set_bounds(), so one object serves a whole column of them.

#pragma once

#include "ui/components/badge.hpp"
#include "ui/components/component.hpp"
#include "ui/components/layout.hpp"
#include "ui/glyphs.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace hui::ui
{

// ---- Divider -----------------------------------------------------------------

enum class DividerLine : std::uint8_t
{
    solid,
    dashed,
    inset, // a groove: a dark line beside a light one, as if pressed into the surface
};

struct DividerStyle : ComponentStyle
{
    bool vertical = false;
    DividerLine line = DividerLine::solid;
    float thickness = 0.0f;                   // 0: the theme's stroke (1.5 at least, 4 at most)
    float dash = 12.0f;                       // length of a dash ...
    float dash_gap = 8.0f;                    // ... and of the space after it
    float inset = 0.0f;                       // the line stops this far short of both ends
    gfx::Color color{0.0f, 0.0f, 0.0f, 0.0f}; // alpha 0: the theme's stroke, or a muted text tone
    bool on_panel = false;                    // it lies on a themed surface, not on the page
    float label_size = 18.0f;
    float label_gap = 14.0f; // between the label and the line either side of it
};

// The width the line takes across its direction in this style and theme.
float divider_thickness(const DividerStyle &style);
// A line through the middle of `bounds`, along its length; a label breaks it
// in the centre. The theme decides what a line is: a pen stroke in the sketch
// theme, square dashes in the pixel theme, a groove where surfaces are
// bevelled or soft, a lit stroke on the sci-fi console.
void draw_divider(Canvas &canvas, const DividerStyle &style, const gfx::Rect &bounds,
                  std::string_view label = {});

// A line between two groups, with an optional word in the middle ("OR").
class Divider
{
  public:
    DividerStyle style;
    std::string label;

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    void draw(Canvas &canvas) const
    {
        draw_divider(canvas, style, bounds_, label);
    }
    void draw(Canvas &canvas, const gfx::Rect &bounds) const
    {
        draw_divider(canvas, style, bounds, label);
    }

  private:
    gfx::Rect bounds_{0.0f, 0.0f, 400.0f, 16.0f};
};

// ---- Panel -------------------------------------------------------------------

enum class PanelKind : std::uint8_t
{
    plain,  // the theme's panel
    titled, // the same with a title bar
    well,   // sunk into what is behind it: a region inside a panel, a quiet group
};

struct PanelStyle : ComponentStyle
{
    PanelKind kind = PanelKind::plain;
    // ---- geometry ----
    float padding_x = 24.0f;    // between the panel's edge and its content
    float padding_y = 20.0f;    // ...
    float radius = -1.0f;       // negative: the theme's card radius
    float header_height = 0.0f; // the title bar; 0 fits the title and the subtitle
    float footer_height = 0.0f; // room for the `footer` slot; 0: no footer
    float header_slot = 120.0f; // width kept at the right of the title bar for `header_right`
    // ---- type ----
    float title_size = 24.0f;
    float subtitle_size = 19.0f;
    // ---- look ----
    bool surface = true;     // false: no surface, only the title bar, rules and slots
                             // (the panel is laid on a surface drawn by something else)
    bool header_rule = true; // a line under the title bar (and over the footer)
    DividerLine rule = DividerLine::solid;
    bool accent_bar = false; // a short bar in the accent colour before the title
};

// A themed surface for a group of content: plain, with a title bar, or sunk.
// It draws itself and tells you where the content goes.
class Panel
{
  public:
    // area is the room the slot may draw in.
    using Slot = std::function<void(Canvas &canvas, const gfx::Rect &area)>;

    PanelStyle style;
    std::string title;    // PanelKind::titled
    std::string subtitle; // a quieter line under the title
    Slot header_right;    // drawn at the right end of the title bar (a count, a switch)
    Slot footer;          // drawn in the footer (style.footer_height > 0)

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }

    // Where the content goes: inside the padding, under the title bar, above
    // the footer.
    gfx::Rect content_rect() const
    {
        return content_rect(bounds_);
    }
    gfx::Rect header_rect() const
    {
        return header_rect(bounds_);
    }
    gfx::Rect footer_rect() const
    {
        return footer_rect(bounds_);
    }
    void draw(Canvas &canvas) const
    {
        draw(canvas, bounds_);
    }

    // The same for a rectangle given on the spot: one Panel object can draw
    // every panel of a list.
    gfx::Rect content_rect(const gfx::Rect &bounds) const;
    gfx::Rect header_rect(const gfx::Rect &bounds) const;
    gfx::Rect footer_rect(const gfx::Rect &bounds) const;
    void draw(Canvas &canvas, const gfx::Rect &bounds) const;

  private:
    float header_size() const;
    gfx::Rect bounds_{0.0f, 0.0f, 480.0f, 320.0f};
};

// ---- SectionHeader -----------------------------------------------------------

enum class SectionRule : std::uint8_t
{
    none,
    trailing, // a line from the title to the action: the header rules its own row
    under,    // a line along the bottom of the bounds
};

struct SectionHeaderStyle : ComponentStyle
{
    float title_size = 26.0f;
    bool caps = false; // the title in capitals, whatever the theme does with labels
    float gap = 14.0f; // between the title, the count, the line and the action
    // ---- count ----
    Status count_kind = Status::neutral;
    BadgeFill count_fill = BadgeFill::tinted;
    float count_height = 28.0f;
    float count_size = 17.0f;
    int count_max = 99;     // counts above this value show a plus suffix
    bool hide_zero = false; // a count of 0 hides the badge (a negative one always does)
    // ---- action ----
    float hint_size = 20.0f;  // the action's words
    float glyph_size = 30.0f; // the controller glyph before them
    // ---- look ----
    SectionRule rule = SectionRule::trailing;
    DividerLine line = DividerLine::solid;
    bool on_panel = false; // it lies on a themed surface, not on the page
};

// The title of a group: a name, how many things are in it, and what a button
// does with them ("Triangle: sort").
//
//   ui::SectionHeader header;
//   header.title = "Recently played";
//   header.set_count(12);                    // pops when it changes
//   header.action = "Sort";
//   header.action_button = ui::Button::triangle;
//   header.set_bounds({96, 240, 800, 44});
class SectionHeader
{
  public:
    SectionHeaderStyle style;
    std::string title;
    std::string action;                  // empty: no action hint
    Button action_button = Button::none; // the glyph before the action

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    // A negative count hides the badge.
    void set_count(int count);
    int count() const
    {
        return count_;
    }
    void update(float dt);
    void draw(Canvas &canvas) const
    {
        draw(canvas, bounds_);
    }
    void draw(Canvas &canvas, const gfx::Rect &bounds) const;

  private:
    gfx::Rect bounds_{0.0f, 0.0f, 600.0f, 44.0f};
    int count_ = -1;
    Badge badge_;
};

// ---- Spacer ------------------------------------------------------------------

// Empty children for a Row or a Column, named for what they are for:
//
//   const ui::LayoutChild bar[] = {ui::LayoutChild::fixed(160),   // a button
//                                  ui::Spacer::flex(),            // pushes the rest right
//                                  ui::LayoutChild::fixed(160)};
struct Spacer
{
    // A gap of a fixed size.
    static constexpr LayoutChild fixed(float size)
    {
        return LayoutChild::fixed(size);
    }
    // A gap that takes what is left (several share it by weight).
    static constexpr LayoutChild flex(float weight = 1.0f)
    {
        return LayoutChild::flexible(weight);
    }
};

// The kit's spacing grid: multiples of 8 (and 4 for half a step), so gaps
// between related things and between groups come from one scale.
constexpr float space(float steps)
{
    return 8.0f * steps;
}

} // namespace hui::ui
