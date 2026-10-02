// ps5-homebrew-ui - Components: layout helpers that compute rectangles.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/layout.hpp"

#include <algorithm>
#include <cmath>

namespace hui::ui
{

using gfx::Rect;

Rect bounds_of(std::span<const Rect> rects)
{
    if (rects.empty())
        return {};
    float left = rects[0].x;
    float top = rects[0].y;
    float right = rects[0].x + rects[0].w;
    float bottom = rects[0].y + rects[0].h;
    for (const Rect &r : rects)
    {
        left = std::min(left, r.x);
        top = std::min(top, r.y);
        right = std::max(right, r.x + r.w);
        bottom = std::max(bottom, r.y + r.h);
    }
    return {left, top, right - left, bottom - top};
}

// ---- Row and Column ----------------------------------------------------------

void Stack::layout(const Rect &bounds, std::span<const LayoutChild> children,
                   std::span<Rect> out) const
{
    const std::size_t count = std::min(children.size(), out.size());
    if (count == 0)
        return;
    const bool across = axis == LayoutAxis::horizontal;
    const Rect in = inset(bounds, padding);
    const float room = across ? in.w : in.h;
    const float cross_room = across ? in.h : in.w;
    const float available = std::max(room - gap * static_cast<float>(count - 1), 0.0f);

    // The sizes along the axis are worked out in out[i].w; out[i].h < 0 marks
    // a flexible child whose size is still open. Nothing is allocated.
    float taken = 0.0f;
    for (std::size_t i = 0; i < count; ++i)
    {
        const LayoutChild &child = children[i];
        const float low = std::max(child.min, 0.0f);
        const float high = std::max(child.max, low);
        if (child.size >= 0.0f || child.flex <= 0.0f)
        {
            out[i].w = std::clamp(std::max(child.size, 0.0f), low, high);
            out[i].h = 0.0f;
            taken += out[i].w;
        }
        else
        {
            out[i].w = 0.0f;
            out[i].h = -1.0f;
        }
    }
    // Share what is left by weight. A child that hits a limit is fixed at it
    // and the rest is shared again: at most one round per child.
    for (std::size_t round = 0; round < count; ++round)
    {
        float weight = 0.0f;
        for (std::size_t i = 0; i < count; ++i)
        {
            if (out[i].h < 0.0f)
                weight += children[i].flex;
        }
        if (weight <= 0.0f)
            break;
        const float left = std::max(available - taken, 0.0f);
        bool clamped = false;
        for (std::size_t i = 0; i < count; ++i)
        {
            if (out[i].h >= 0.0f)
                continue;
            const LayoutChild &child = children[i];
            const float low = std::max(child.min, 0.0f);
            const float high = std::max(child.max, low);
            const float share = left * child.flex / weight;
            if (share < low || share > high)
            {
                out[i].w = std::clamp(share, low, high);
                out[i].h = 0.0f;
                taken += out[i].w;
                clamped = true;
                break;
            }
        }
        if (clamped)
            continue;
        for (std::size_t i = 0; i < count; ++i)
        {
            if (out[i].h < 0.0f)
            {
                out[i].w = left * children[i].flex / weight;
                out[i].h = 0.0f;
                taken += out[i].w;
            }
        }
        break;
    }

    const float spare = std::max(available - taken, 0.0f);
    float cursor = across ? in.x : in.y;
    float between = gap;
    if (main == LayoutAlign::center)
        cursor += spare * 0.5f;
    else if (main == LayoutAlign::end)
        cursor += spare;
    else if (main == LayoutAlign::stretch && count > 1)
        between += spare / static_cast<float>(count - 1);

    for (std::size_t i = 0; i < count; ++i)
    {
        const LayoutChild &child = children[i];
        const float length = out[i].w;
        // A child with a size across the axis cannot be stretched: it sits at
        // the start instead.
        const bool sized = child.cross >= 0.0f;
        const float breadth = sized ? std::min(child.cross, cross_room) : cross_room;
        const float shift =
            sized ? layout_detail::place(cross_room, breadth,
                                         cross == LayoutAlign::stretch ? LayoutAlign::start : cross)
                  : 0.0f;
        out[i] = across ? Rect{cursor, in.y + shift, length, breadth}
                        : Rect{in.x + shift, cursor, breadth, length};
        cursor += length + between;
    }
}

std::vector<Rect> Stack::layout(const Rect &bounds, std::span<const LayoutChild> children) const
{
    std::vector<Rect> out(children.size());
    layout(bounds, children, out);
    return out;
}

std::vector<Rect> Stack::layout(const Rect &bounds, int count) const
{
    const std::vector<LayoutChild> children(static_cast<std::size_t>(std::max(count, 0)));
    return layout(bounds, children);
}

float Stack::content_size(std::span<const LayoutChild> children) const
{
    const bool across = axis == LayoutAxis::horizontal;
    float total = across ? padding.horizontal() : padding.vertical();
    if (!children.empty())
        total += gap * static_cast<float>(children.size() - 1);
    for (const LayoutChild &child : children)
        total += std::max(child.size >= 0.0f ? child.size : 0.0f, std::max(child.min, 0.0f));
    return total;
}

// ---- GridLayout ---------------------------------------------------------------

float GridLayout::cell_width(float width) const
{
    const int across = std::max(columns, 1);
    const float room = width - padding.horizontal() - gap_x * static_cast<float>(across - 1);
    return std::max(room, 0.0f) / static_cast<float>(across);
}

void GridLayout::layout(const Rect &bounds, std::span<const GridSpan> children,
                        std::span<Rect> out) const
{
    const std::size_t count = std::min(children.size(), out.size());
    if (count == 0)
        return;
    const int across = std::max(columns, 1);
    const Rect in = inset(bounds, padding);

    // Place every child in cells first: out[i] holds column, row, span, span.
    std::vector<char> taken; // rows of `across` cells, grown as children land
    int rows = 0;
    const auto is_free = [&](int column, int row, int w, int h)
    {
        for (int y = row; y < row + h; ++y)
        {
            for (int x = column; x < column + w; ++x)
            {
                const std::size_t at = static_cast<std::size_t>(y * across + x);
                if (at < taken.size() && taken[at] != 0)
                    return false;
            }
        }
        return true;
    };
    for (std::size_t i = 0; i < count; ++i)
    {
        const int w = std::clamp(children[i].columns, 1, across);
        const int h = std::max(children[i].rows, 1);
        bool placed = false;
        for (int row = 0; !placed; ++row)
        {
            for (int column = 0; column + w <= across && !placed; ++column)
            {
                if (!is_free(column, row, w, h))
                    continue;
                const std::size_t need = static_cast<std::size_t>((row + h) * across);
                if (taken.size() < need)
                    taken.resize(need, 0);
                for (int y = row; y < row + h; ++y)
                {
                    for (int x = column; x < column + w; ++x)
                        taken[static_cast<std::size_t>(y * across + x)] = 1;
                }
                out[i] = {static_cast<float>(column), static_cast<float>(row),
                          static_cast<float>(w), static_cast<float>(h)};
                rows = std::max(rows, row + h);
                placed = true;
            }
        }
    }

    const float cell_w = cell_width(bounds.w);
    float cell_h = row_height;
    if (cell_h <= 0.0f)
    {
        if (cell_aspect > 0.0f)
            cell_h = cell_w / cell_aspect;
        else
            cell_h = std::max(in.h - gap_y * static_cast<float>(rows - 1), 0.0f) /
                     static_cast<float>(std::max(rows, 1));
    }
    for (std::size_t i = 0; i < count; ++i)
    {
        const Rect cell = out[i];
        out[i] = {in.x + cell.x * (cell_w + gap_x), in.y + cell.y * (cell_h + gap_y),
                  cell.w * cell_w + (cell.w - 1.0f) * gap_x,
                  cell.h * cell_h + (cell.h - 1.0f) * gap_y};
    }
}

std::vector<Rect> GridLayout::layout(const Rect &bounds, std::span<const GridSpan> children) const
{
    std::vector<Rect> out(children.size());
    layout(bounds, children, out);
    return out;
}

std::vector<Rect> GridLayout::layout(const Rect &bounds, int count) const
{
    const std::vector<GridSpan> children(static_cast<std::size_t>(std::max(count, 0)));
    return layout(bounds, children);
}

// ---- Wrap ----------------------------------------------------------------------

void Wrap::layout(const Rect &bounds, std::span<const LayoutSize> children,
                  std::span<Rect> out) const
{
    const std::size_t count = std::min(children.size(), out.size());
    const Rect in = inset(bounds, padding);
    float y = in.y;
    std::size_t first = 0;
    while (first < count)
    {
        // Take children while they fit; a line always holds at least one.
        float width = 0.0f;
        float height = 0.0f;
        std::size_t last = first;
        while (last < count)
        {
            const float w = std::min(std::max(children[last].w, 0.0f), in.w);
            const float next = width + (last > first ? gap_x : 0.0f) + w;
            if (last > first && next > in.w + 0.01f)
                break;
            width = next;
            height = std::max(height, children[last].h);
            ++last;
        }
        const std::size_t items = last - first;
        const float spare = std::max(in.w - width, 0.0f);
        const float grow = line == LayoutAlign::stretch ? spare / static_cast<float>(items) : 0.0f;
        float x = in.x + layout_detail::place(in.w, width, line);
        for (std::size_t i = first; i < last; ++i)
        {
            const float w = std::min(std::max(children[i].w, 0.0f), in.w) + grow;
            const float h = cross == LayoutAlign::stretch ? height : children[i].h;
            out[i] = {x, y + layout_detail::place(height, h, cross), w, h};
            x += w + gap_x;
        }
        y += height + gap_y;
        first = last;
    }
}

std::vector<Rect> Wrap::layout(const Rect &bounds, std::span<const LayoutSize> children) const
{
    std::vector<Rect> out(children.size());
    layout(bounds, children, out);
    return out;
}

// ---- SpringLayout --------------------------------------------------------------

namespace
{
// tween::Bounce comes to rest by an absolute threshold that suits values near
// 1; a coordinate of several hundred pixels keeps a flicker of velocity in
// its last bits and would never report "settled". A hundredth of a pixel and
// half a pixel a second are rest for a rectangle.
void ease(tween::Bounce &spring, float dt, float omega, float damping)
{
    spring.update(dt, omega, damping);
    if (std::fabs(spring.value - spring.target) < 0.01f && std::fabs(spring.velocity) < 0.5f)
        spring.snap(spring.target);
}
} // namespace

void SpringLayout::aim(Item &item, const Rect &r)
{
    item.x.target = r.x;
    item.y.target = r.y;
    item.w.target = r.w;
    item.h.target = r.h;
}

void SpringLayout::snap(std::span<const Rect> rects)
{
    items_.resize(rects.size());
    targets_.assign(rects.begin(), rects.end());
    current_.assign(rects.begin(), rects.end());
    for (std::size_t i = 0; i < rects.size(); ++i)
    {
        Item &item = items_[i];
        item.x.snap(rects[i].x);
        item.y.snap(rects[i].y);
        item.w.snap(rects[i].w);
        item.h.snap(rects[i].h);
        item.delay = 0.0f;
    }
}

void SpringLayout::target(std::span<const Rect> rects)
{
    if (items_.empty())
    {
        snap(rects);
        return;
    }
    const std::size_t before = items_.size();
    items_.resize(rects.size());
    current_.resize(rects.size());
    targets_.resize(rects.size());
    for (std::size_t i = 0; i < rects.size(); ++i)
    {
        const Rect &r = rects[i];
        Item &item = items_[i];
        if (i >= before)
        {
            // A new child has no "before": it starts where it belongs.
            item.x.snap(r.x);
            item.y.snap(r.y);
            item.w.snap(r.w);
            item.h.snap(r.h);
            current_[i] = r;
            targets_[i] = r;
            continue;
        }
        const Rect &old = targets_[i];
        const bool changed = old.x != r.x || old.y != r.y || old.w != r.w || old.h != r.h;
        targets_[i] = r;
        if (!changed)
            continue;
        // Only a child at rest waits its turn: one already under way (a
        // layout that follows something animated) must not stutter.
        const bool resting = item.delay <= 0.0f && item.x.velocity == 0.0f &&
                             item.y.velocity == 0.0f && item.x.value == item.x.target &&
                             item.y.value == item.y.target;
        if (stagger > 0.0f && resting)
            item.delay = stagger * static_cast<float>(i);
        if (item.delay <= 0.0f)
            aim(item, r);
    }
}

void SpringLayout::update(float dt, const ComponentStyle &style)
{
    if (style.reduced_motion)
    {
        // No travel: every child is simply where it belongs.
        for (std::size_t i = 0; i < items_.size(); ++i)
        {
            Item &item = items_[i];
            item.x.snap(targets_[i].x);
            item.y.snap(targets_[i].y);
            item.w.snap(targets_[i].w);
            item.h.snap(targets_[i].h);
            item.delay = 0.0f;
            current_[i] = targets_[i];
        }
        return;
    }
    update(dt, style.omega(), style.damping());
}

void SpringLayout::update(float dt, float omega, float damping)
{
    for (std::size_t i = 0; i < items_.size(); ++i)
    {
        Item &item = items_[i];
        if (item.delay > 0.0f)
        {
            item.delay -= dt;
            if (item.delay <= 0.0f)
            {
                item.delay = 0.0f;
                aim(item, targets_[i]);
            }
        }
        // A position may overshoot. A size that did would make the text in it
        // jump, so sizes are critically damped at least, and never negative.
        ease(item.x, dt, omega, damping);
        ease(item.y, dt, omega, damping);
        ease(item.w, dt, omega, std::max(damping, 1.0f));
        ease(item.h, dt, omega, std::max(damping, 1.0f));
        current_[i] = {item.x.value, item.y.value, std::max(item.w.value, 0.0f),
                       std::max(item.h.value, 0.0f)};
    }
}

bool SpringLayout::settled() const
{
    for (const Item &item : items_)
    {
        if (item.delay > 0.0f || item.x.value != item.x.target || item.y.value != item.y.target ||
            item.w.value != item.w.target || item.h.value != item.h.target)
            return false;
    }
    return true;
}

} // namespace hui::ui
