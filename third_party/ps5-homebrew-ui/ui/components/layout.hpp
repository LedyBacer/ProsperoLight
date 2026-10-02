// ps5-homebrew-ui - Components: layout helpers that compute rectangles.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The kit is immediate mode: a screen computes rectangles and draws into
// them. These helpers do the arithmetic, and nothing else: they draw nothing,
// keep nothing and know no theme. Give them a rectangle and a description of
// the children; they answer with one rectangle per child, in order.
//
//   ui::Column column{16.0f};                       // gap 16
//   const ui::LayoutChild rows[] = {ui::LayoutChild::fixed(56.0f),   // a toolbar
//                                   ui::LayoutChild::flexible(),     // the content
//                                   ui::LayoutChild::fixed(40.0f)};  // a footer
//   const std::vector<gfx::Rect> r = column.layout(page, rows);
//
//   ui::GridLayout grid;                            // cards, three across
//   grid.columns = 3;
//   const std::vector<gfx::Rect> cells = grid.layout(area, 12);
//
// Because a layout is a pure function of its inputs it can run every frame,
// and feeding its result to a ui::SpringLayout animates every change of it.

#pragma once

#include "ui/components/component.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace hui::ui
{

// ---- small arithmetic (constexpr) --------------------------------------------

// Where something sits in the room it is given, along one axis.
enum class LayoutAlign : std::uint8_t
{
    start,   // left or top
    center,  // in the middle
    end,     // right or bottom
    stretch, // takes all the room (along a stack: the room goes between the children)
};

// Space on the four sides of a rectangle.
struct Edges
{
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    static constexpr Edges all(float amount)
    {
        return {amount, amount, amount, amount};
    }
    static constexpr Edges symmetric(float horizontal, float vertical)
    {
        return {horizontal, vertical, horizontal, vertical};
    }
    constexpr float horizontal() const
    {
        return left + right;
    }
    constexpr float vertical() const
    {
        return top + bottom;
    }
};

enum class Side : std::uint8_t
{
    left,
    top,
    right,
    bottom,
};

// A size, for children that know their own (ui::Wrap).
struct LayoutSize
{
    float w = 0.0f;
    float h = 0.0f;
};

namespace layout_detail
{
constexpr float positive(float value)
{
    return value > 0.0f ? value : 0.0f;
}
constexpr float smaller(float a, float b)
{
    return a < b ? a : b;
}
// The offset of something `size` long in `room`, by alignment.
constexpr float place(float room, float size, LayoutAlign align)
{
    if (align == LayoutAlign::center)
        return (room - size) * 0.5f;
    if (align == LayoutAlign::end)
        return room - size;
    return 0.0f;
}
} // namespace layout_detail

// The rectangle left inside `r` after taking `edges` off its sides. It never
// turns inside out: a rectangle too small for its padding has no size.
constexpr gfx::Rect inset(const gfx::Rect &r, const Edges &edges)
{
    return {r.x + edges.left, r.y + edges.top, layout_detail::positive(r.w - edges.horizontal()),
            layout_detail::positive(r.h - edges.vertical())};
}
constexpr gfx::Rect inset(const gfx::Rect &r, float horizontal, float vertical)
{
    return inset(r, Edges::symmetric(horizontal, vertical));
}

constexpr gfx::Rect translate(const gfx::Rect &r, float dx, float dy)
{
    return {r.x + dx, r.y + dy, r.w, r.h};
}

// What split() answers: the strip that was cut off and what remains.
struct SplitRects
{
    gfx::Rect strip;
    gfx::Rect rest;
};

// Cuts a strip `amount` thick off one side of `r`, leaving `gap` between the
// strip and the rest: a header off the top, a side bar off the left, a button
// row off the bottom. An amount larger than the rectangle takes all of it.
constexpr SplitRects split(const gfx::Rect &r, Side side, float amount, float gap = 0.0f)
{
    const bool across = side == Side::left || side == Side::right;
    const float room = across ? r.w : r.h;
    const float cut = layout_detail::smaller(layout_detail::positive(amount), room);
    const float rest = layout_detail::positive(room - cut - gap);
    switch (side)
    {
    case Side::left:
        return {{r.x, r.y, cut, r.h}, {r.x + r.w - rest, r.y, rest, r.h}};
    case Side::right:
        return {{r.x + r.w - cut, r.y, cut, r.h}, {r.x, r.y, rest, r.h}};
    case Side::top:
        return {{r.x, r.y, r.w, cut}, {r.x, r.y + r.h - rest, r.w, rest}};
    case Side::bottom:
        break;
    }
    return {{r.x, r.y + r.h - cut, r.w, cut}, {r.x, r.y, r.w, rest}};
}

// A rectangle of the given size placed inside `outer` by two alignments
// (stretch takes the whole of that axis).
constexpr gfx::Rect align_in(const gfx::Rect &outer, float w, float h, LayoutAlign horizontal,
                             LayoutAlign vertical)
{
    const float width = horizontal == LayoutAlign::stretch ? outer.w : w;
    const float height = vertical == LayoutAlign::stretch ? outer.h : h;
    return {outer.x + layout_detail::place(outer.w, width, horizontal),
            outer.y + layout_detail::place(outer.h, height, vertical), width, height};
}
constexpr gfx::Rect center_in(const gfx::Rect &outer, float w, float h)
{
    return align_in(outer, w, h, LayoutAlign::center, LayoutAlign::center);
}

// The largest rectangle of this aspect (width / height) that fits inside
// `outer`, centred: a picture shown whole, with bars.
constexpr gfx::Rect aspect_fit(const gfx::Rect &outer, float aspect)
{
    if (aspect <= 0.0f || outer.w <= 0.0f || outer.h <= 0.0f)
        return outer;
    const bool wide = outer.w > outer.h * aspect; // outer is wider than the shape
    return wide ? center_in(outer, outer.h * aspect, outer.h)
                : center_in(outer, outer.w, outer.w / aspect);
}
// The smallest rectangle of this aspect that covers `outer`, centred: a
// picture that fills its frame and is cropped by a clip.
constexpr gfx::Rect aspect_fill(const gfx::Rect &outer, float aspect)
{
    if (aspect <= 0.0f || outer.w <= 0.0f || outer.h <= 0.0f)
        return outer;
    const bool wide = outer.w > outer.h * aspect;
    return wide ? center_in(outer, outer.w, outer.w / aspect)
                : center_in(outer, outer.h * aspect, outer.h);
}

// The smallest rectangle around all of them (a content size for a
// ui::ScrollArea, for instance). Empty input gives an empty rectangle.
gfx::Rect bounds_of(std::span<const gfx::Rect> rects);

// ---- Row and Column ----------------------------------------------------------

// One child of a Row or a Column.
struct LayoutChild
{
    float size = -1.0f;  // along the stack; negative: flexible, it shares what is left
    float flex = 1.0f;   // a flexible child's share of what is left
    float min = 0.0f;    // limits of a flexible child (a fixed one is clamped too)
    float max = 1.0e9f;  // ...
    float cross = -1.0f; // across the stack; negative: the stack's `cross` decides

    static constexpr LayoutChild fixed(float size, float cross = -1.0f)
    {
        return {size, 0.0f, 0.0f, 1.0e9f, cross};
    }
    static constexpr LayoutChild flexible(float weight = 1.0f, float min = 0.0f, float max = 1.0e9f)
    {
        return {-1.0f, weight, min, max, -1.0f};
    }
};

enum class LayoutAxis : std::uint8_t
{
    horizontal,
    vertical,
};

// Children one after another along an axis. Fixed children take their size;
// flexible ones share what is left by weight, within their limits. What no
// child takes is placed by `main`: before (end), around (center), after
// (start) or between the children (stretch).
struct Stack
{
    LayoutAxis axis = LayoutAxis::horizontal;
    float gap = 0.0f;                         // between children
    Edges padding;                            // inside the bounds
    LayoutAlign main = LayoutAlign::start;    // spare room along the axis
    LayoutAlign cross = LayoutAlign::stretch; // children across the axis

    // One rectangle per child, written into `out` (as many as fit in it).
    void layout(const gfx::Rect &bounds, std::span<const LayoutChild> children,
                std::span<gfx::Rect> out) const;
    std::vector<gfx::Rect> layout(const gfx::Rect &bounds,
                                  std::span<const LayoutChild> children) const;
    // `count` equal flexible children.
    std::vector<gfx::Rect> layout(const gfx::Rect &bounds, int count) const;
    // The length the fixed sizes, the minimums, the gaps and the padding need
    // along the axis: what to give the stack so that nothing is squeezed.
    float content_size(std::span<const LayoutChild> children) const;
};

struct Row : Stack
{
    constexpr explicit Row(float spacing = 0.0f, Edges inside = {})
        : Stack{LayoutAxis::horizontal, spacing, inside, LayoutAlign::start, LayoutAlign::stretch}
    {
    }
};

struct Column : Stack
{
    constexpr explicit Column(float spacing = 0.0f, Edges inside = {})
        : Stack{LayoutAxis::vertical, spacing, inside, LayoutAlign::start, LayoutAlign::stretch}
    {
    }
};

// ---- GridLayout ---------------------------------------------------------------

// How many cells a child of a grid covers.
struct GridSpan
{
    int columns = 1;
    int rows = 1;
};

// Equal columns; children fill the cells row by row, each into the first
// free place its span fits (so a wide child never leaves a hole a later,
// smaller one could fill).
struct GridLayout
{
    int columns = 3;
    float gap_x = 16.0f;
    float gap_y = 16.0f;
    Edges padding;
    float row_height = 0.0f;  // above 0: every row is this tall
    float cell_aspect = 1.0f; // else a cell's width / height; 0: rows share the bounds' height

    void layout(const gfx::Rect &bounds, std::span<const GridSpan> children,
                std::span<gfx::Rect> out) const;
    std::vector<gfx::Rect> layout(const gfx::Rect &bounds,
                                  std::span<const GridSpan> children) const;
    // `count` children of one cell each.
    std::vector<gfx::Rect> layout(const gfx::Rect &bounds, int count) const;
    // The width of one column in bounds this wide.
    float cell_width(float width) const;
};

// ---- Wrap ----------------------------------------------------------------------

// A flow layout: children keep their own sizes and fill a line; the one that
// does not fit starts the next line. Tags, chips, buttons of different widths.
struct Wrap
{
    float gap_x = 12.0f;
    float gap_y = 12.0f;
    Edges padding;
    // The spare room of a line: after (start), around (center), before (end)
    // the children, or shared out among them so every line is full (stretch).
    LayoutAlign line = LayoutAlign::start;
    // A child shorter than its line: top, middle, bottom or as tall as the line.
    LayoutAlign cross = LayoutAlign::start;

    void layout(const gfx::Rect &bounds, std::span<const LayoutSize> children,
                std::span<gfx::Rect> out) const;
    std::vector<gfx::Rect> layout(const gfx::Rect &bounds,
                                  std::span<const LayoutSize> children) const;
};

// ---- SpringLayout --------------------------------------------------------------

// Rectangles that ease toward new places when a layout changes: give it the
// result of a layout every frame and draw rect(i) instead. Children are keyed
// by index, so "child 3" glides from where it was to where it now belongs.
//
//   springs_.target(grid.layout(area, spans));   // update(): as often as you like
//   springs_.update(dt, style);
//   draw_card(springs_.rect(i));                 // draw()
//
// The first target() snaps; a child that appears later starts in its place.
class SpringLayout
{
  public:
    // Seconds between one child starting to move and the next (a re-flow that
    // ripples through the children instead of moving as one block).
    float stagger = 0.0f;

    void snap(std::span<const gfx::Rect> rects);
    void target(std::span<const gfx::Rect> rects);
    // Moves at the theme's pace and bounce; reduced motion snaps.
    void update(float dt, const ComponentStyle &style);
    void update(float dt, float omega, float damping = 1.0f);

    int count() const
    {
        return static_cast<int>(current_.size());
    }
    // Where child `index` is now.
    const gfx::Rect &rect(int index) const
    {
        return current_[static_cast<std::size_t>(index)];
    }
    std::span<const gfx::Rect> rects() const
    {
        return current_;
    }
    // Where it is going.
    const gfx::Rect &target_rect(int index) const
    {
        return targets_[static_cast<std::size_t>(index)];
    }
    bool settled() const;

  private:
    struct Item
    {
        tween::Bounce x, y, w, h;
        float delay = 0.0f; // seconds until it starts toward its target
    };
    static void aim(Item &item, const gfx::Rect &r);

    std::vector<Item> items_;
    std::vector<gfx::Rect> targets_;
    std::vector<gfx::Rect> current_;
};

} // namespace hui::ui
