// ps5-homebrew-ui - Component: ScrollArea.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/scroll_area.hpp"

#include <algorithm>
#include <cmath>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

// Moves `wanted` the least distance that brings [start, start + size) into a
// view of `view`, with `margin` to spare. Something larger than the view
// shows its start.
float revealed(float wanted, float start, float size, float view, float margin)
{
    margin = std::min(margin, std::max((view - size) * 0.5f, 0.0f));
    if (start + size + margin > wanted + view)
        wanted = start + size + margin - view;
    if (start - margin < wanted)
        wanted = start - margin;
    return wanted;
}

// How much of [start, start + size) on screen is inside [low, high), as an
// opacity: it fades over `fade` pixels, and only toward an edge that hides
// more content (`before`: there is content before `low`, `after`: after `high`).
float shown(float start, float size, float low, float high, float fade, bool before, bool after)
{
    if (size <= 0.0f || fade <= 0.0f)
        return 1.0f;
    float amount = 1.0f;
    if (before)
        amount = std::min(amount, tween::clamp01((start + size - low) / fade));
    if (after)
        amount = std::min(amount, tween::clamp01((high - start) / fade));
    return amount;
}

} // namespace

void ScrollArea::set_bounds(const Rect &bounds)
{
    bounds_ = bounds;
    clamp_targets();
}

void ScrollArea::set_content_size(float width, float height)
{
    content_w_ = std::max(width, 0.0f);
    content_h_ = std::max(height, 0.0f);
    clamp_targets();
}

float ScrollArea::max_x() const
{
    return scrolls_x() ? std::max(content_w_ - bounds_.w, 0.0f) : 0.0f;
}

float ScrollArea::max_y() const
{
    return scrolls_y() ? std::max(content_h_ - bounds_.h, 0.0f) : 0.0f;
}

float ScrollArea::progress_x() const
{
    const float limit = max_x();
    return limit > 0.0f ? tween::clamp01(x_.value / limit) : 0.0f;
}

float ScrollArea::progress_y() const
{
    const float limit = max_y();
    return limit > 0.0f ? tween::clamp01(y_.value / limit) : 0.0f;
}

void ScrollArea::clamp_targets()
{
    x_.target = std::clamp(x_.target, 0.0f, max_x());
    y_.target = std::clamp(y_.target, 0.0f, max_y());
}

void ScrollArea::reveal(const Rect &content_rect, bool snap)
{
    if (scrolls_x())
        x_.target = revealed(x_.target, content_rect.x, content_rect.w, bounds_.w, style.margin);
    if (scrolls_y())
        y_.target = revealed(y_.target, content_rect.y, content_rect.h, bounds_.h, style.margin);
    clamp_targets();
    if (snap)
    {
        x_.snap(x_.target);
        y_.snap(y_.target);
    }
}

void ScrollArea::scroll_to(float x, float y, bool snap)
{
    x_.target = x;
    y_.target = y;
    clamp_targets();
    if (snap)
    {
        x_.snap(x_.target);
        y_.snap(y_.target);
    }
}

void ScrollArea::scroll_by(float dx, float dy)
{
    x_.target += dx;
    y_.target += dy;
    clamp_targets();
}

Rect ScrollArea::to_screen(const Rect &content_rect) const
{
    return {content_rect.x + bounds_.x - x_.value, content_rect.y + bounds_.y - y_.value,
            content_rect.w, content_rect.h};
}

Rect ScrollArea::to_content(const Rect &screen_rect) const
{
    return {screen_rect.x - bounds_.x + x_.value, screen_rect.y - bounds_.y + y_.value,
            screen_rect.w, screen_rect.h};
}

Rect ScrollArea::visible() const
{
    return {x_.value, y_.value, bounds_.w, bounds_.h};
}

float ScrollArea::visibility(const Rect &content_rect) const
{
    if (style.edge_fade <= 0.0f)
        return 1.0f;
    const Rect on_screen = to_screen(content_rect);
    float amount = 1.0f;
    if (max_x() > 0.5f)
        amount = std::min(amount, shown(on_screen.x, on_screen.w, bounds_.x, bounds_.x + bounds_.w,
                                        on_screen.w * style.edge_fade, x_.value > 0.5f,
                                        x_.value < max_x() - 0.5f));
    if (max_y() > 0.5f)
        amount = std::min(amount, shown(on_screen.y, on_screen.h, bounds_.y, bounds_.y + bounds_.h,
                                        on_screen.h * style.edge_fade, y_.value > 0.5f,
                                        y_.value < max_y() - 0.5f));
    return amount;
}

Event ScrollArea::handle(const InputFrame &input, Feedback &feedback)
{
    if (!style.stick)
        return Event::none;
    // Squared, sign kept: a small tilt creeps, a full one runs.
    const float sx = scrolls_x() ? input.stick2_x * std::fabs(input.stick2_x) : 0.0f;
    const float sy = scrolls_y() ? input.stick2_y * std::fabs(input.stick2_y) : 0.0f;
    if (sx == 0.0f && sy == 0.0f)
    {
        against_ = false;
        return Event::none;
    }
    if (max_x() <= 0.5f && max_y() <= 0.5f)
        return Event::none; // everything is in view: the stick means nothing here
    const float before_x = x_.target;
    const float before_y = y_.target;
    scroll_by(sx * style.stick_speed * dt_, sy * style.stick_speed * dt_);
    if (x_.target != before_x || y_.target != before_y)
    {
        against_ = false;
        activity_.trigger();
        return Event::changed;
    }
    // Held against an end: it answers once, then stays quiet.
    if (against_)
        return Event::none;
    against_ = true;
    play_cue(feedback, style, style.sounds.refuse, bounds_.cx(), 1.0f, 0.45f);
    if (!style.reduced_motion)
        bump_.trigger();
    return Event::refused;
}

void ScrollArea::update(float dt)
{
    dt_ = dt;
    clamp_targets();
    const float omega = std::max(style.omega(), 14.0f);
    x_.update(dt, omega);
    y_.update(dt, omega);
    if (std::fabs(x_.velocity) > 4.0f || std::fabs(y_.velocity) > 4.0f)
        activity_.trigger();
    activity_.update(dt, 2.5f);
    bump_.update(dt, 8.0f);
}

void ScrollArea::begin(Canvas &canvas) const
{
    canvas.list.push_clip(bounds_.inset(-style.clip_bleed));
    canvas.list.push_transform(1.0f, 0.0f, 0.0f, bounds_.x - x_.value, bounds_.y - y_.value);
}

void ScrollArea::end(Canvas &canvas) const
{
    canvas.list.pop_transform();
    canvas.list.pop_clip();
}

void ScrollArea::draw(Canvas &canvas) const
{
    if (!style.thumb)
        return;
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const Color base = style.on_panel ? theme.text_muted : paint.page_text_muted();
    // The thumb brightens while the content moves and flashes in the focus
    // colour when the stick is pushed against an end.
    const float lit = tween::lerp(style.thumb_idle, 1.0f, tween::clamp01(activity_.value));
    const Color focus{theme.focus.r, theme.focus.g, theme.focus.b, 1.0f};
    const Color thumb = gfx::mix(base.with_alpha(lit), focus, tween::clamp01(bump_.value));
    const float w = style.thumb_width;
    const float radius = theme.corner == Corner::round && theme.radius >= 4.0f ? w * 0.5f : 0.0f;

    if (max_y() > 0.5f)
    {
        const Rect track{bounds_.x + bounds_.w + style.thumb_gap, bounds_.y + 8.0f, w,
                         std::max(bounds_.h - 16.0f, 0.0f)};
        const float size = std::clamp(track.h * bounds_.h / content_h_,
                                      std::min(style.thumb_min, track.h), track.h);
        list.rounded_rect(track, radius, base.with_alpha(0.16f));
        list.rounded_rect({track.x, track.y + (track.h - size) * progress_y(), w, size}, radius,
                          thumb);
    }
    if (max_x() > 0.5f)
    {
        const Rect track{bounds_.x + 8.0f, bounds_.y + bounds_.h + style.thumb_gap,
                         std::max(bounds_.w - 16.0f, 0.0f), w};
        const float size = std::clamp(track.w * bounds_.w / content_w_,
                                      std::min(style.thumb_min, track.w), track.w);
        list.rounded_rect(track, radius, base.with_alpha(0.16f));
        list.rounded_rect({track.x + (track.w - size) * progress_x(), track.y, size, w}, radius,
                          thumb);
    }
}

} // namespace hui::ui
