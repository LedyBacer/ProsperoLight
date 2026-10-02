// ps5-homebrew-ui - Component: ToastStack.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/toast.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

constexpr float kIconGap = 16.0f;
constexpr float kTitleLine = 1.15f;
constexpr float kBodyLine = 1.35f;
constexpr float kBodyGap = 4.0f;

bool on_top(ToastAnchor anchor)
{
    return anchor == ToastAnchor::top_left || anchor == ToastAnchor::top_center ||
           anchor == ToastAnchor::top_right;
}

// -1 left, 0 centre, 1 right.
int side(ToastAnchor anchor)
{
    if (anchor == ToastAnchor::top_left || anchor == ToastAnchor::bottom_left)
        return -1;
    if (anchor == ToastAnchor::top_right || anchor == ToastAnchor::bottom_right)
        return 1;
    return 0;
}

float pitch_for(StatusKind kind)
{
    switch (kind)
    {
    case StatusKind::success:
        return 1.06f;
    case StatusKind::warning:
        return 0.96f;
    case StatusKind::danger:
        return 0.9f;
    default:
        return 1.0f;
    }
}

} // namespace

int ToastStack::push(StatusKind kind, std::string title, std::string body, float seconds)
{
    Toast toast;
    toast.id = next_id_++;
    toast.kind = kind;
    toast.title = std::move(title);
    toast.body = std::move(body);
    toast.seconds = seconds;
    queue_.push_back(std::move(toast));
    return queue_.back().id;
}

void ToastStack::dismiss(int id)
{
    for (Toast &toast : shown_)
    {
        if (toast.id == id)
            toast.leaving = true;
    }
    std::erase_if(queue_, [id](const Toast &toast) { return toast.id == id; });
}

void ToastStack::clear(bool now)
{
    queue_.clear();
    if (now)
        shown_.clear();
    for (Toast &toast : shown_)
        toast.leaving = true;
}

int ToastStack::visible_count() const
{
    int count = 0;
    for (const Toast &toast : shown_)
        count += toast.leaving ? 0 : 1;
    return count;
}

float ToastStack::remaining(int id) const
{
    for (const Toast &toast : shown_)
    {
        if (toast.id != id)
            continue;
        return toast.seconds > 0.0f ? tween::clamp01(toast.left / toast.seconds) : 1.0f;
    }
    return -1.0f;
}

float ToastStack::anchor_x() const
{
    const int where = side(style.anchor);
    if (where < 0)
        return bounds_.x + style.margin + style.width * 0.5f;
    if (where > 0)
        return bounds_.x + bounds_.w - style.margin - style.width * 0.5f;
    return bounds_.cx();
}

void ToastStack::update(float dt, Feedback &feedback)
{
    const float omega = style.omega();
    // A toast may overshoot its place a little, never wobble for long.
    const float damping = std::max(style.damping(), 0.62f);
    for (Toast &toast : shown_)
    {
        toast.enter.target = 1.0f;
        toast.enter.update(dt, omega, damping);
        if (!toast.leaving && toast.seconds >= 0.0f)
        {
            toast.left -= dt;
            toast.leaving = toast.left <= 0.0f;
        }
        if (toast.leaving)
        {
            toast.leave.target = 1.0f;
            toast.leave.update(dt, omega * 1.5f);
            // Its room closes once it is mostly gone, so the others do not
            // slide under a toast that is still readable.
            if (toast.leave.value > 0.6f)
                toast.slot.target = 0.0f;
        }
        toast.slot.update(dt, omega, damping);
    }
    std::erase_if(shown_,
                  [](const Toast &toast)
                  {
                      return toast.leaving && toast.leave.value > 0.99f &&
                             toast.slot.target == 0.0f && std::fabs(toast.slot.value) < 0.004f &&
                             std::fabs(toast.slot.velocity) < 0.05f;
                  });

    const int room = std::max(style.max_visible, 1);
    while (!queue_.empty() && visible_count() < room)
    {
        Toast toast = std::move(queue_.front());
        queue_.erase(queue_.begin());
        if (toast.seconds == 0.0f)
            toast.seconds = style.duration;
        toast.left = toast.seconds;
        toast.enter.snap(0.0f);
        toast.leave.snap(0.0f);
        toast.slot.snap(1.0f);
        play_cue(feedback, style, style.sounds.notify, anchor_x(),
                 style.pitch_by_kind ? pitch_for(toast.kind) : 1.0f);
        shown_.push_back(std::move(toast));
    }
}

float ToastStack::height(const Canvas &canvas, const Toast &toast) const
{
    const Painter paint(canvas.list, canvas.fonts, style.theme, 0);
    const float icon = toast.kind != StatusKind::none ? std::max(style.icon_size, 0.0f) : 0.0f;
    const float text_width = std::max(
        style.width - 2.0f * style.padding - (icon > 0.0f ? icon + kIconGap : 0.0f), 40.0f);
    float block = style.title_size * kTitleLine;
    if (!toast.body.empty() && style.body_lines > 0)
    {
        const std::size_t lines =
            wrap_body(paint, toast.body, style.body_size, text_width, style.body_lines).size();
        block += kBodyGap + static_cast<float>(lines) * style.body_size * kBodyLine;
    }
    float total = 2.0f * style.padding + std::max(icon, block);
    if (style.progress && toast.seconds > 0.0f)
        total += style.progress_height + style.padding * 0.4f;
    return total;
}

void ToastStack::draw_toast(Canvas &canvas, const Toast &toast, const Rect &r) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    draw_overlay_panel(canvas, theme, r, style.frosted, style.frost);
    Painter paint(list, canvas.fonts, theme, canvas.glass);

    const bool bar = style.progress && toast.seconds > 0.0f;
    const float bar_room = bar ? style.progress_height + style.padding * 0.4f : 0.0f;
    const float top = r.y + style.padding;
    const float room = r.h - 2.0f * style.padding - bar_room;
    const float icon = toast.kind != StatusKind::none ? std::max(style.icon_size, 0.0f) : 0.0f;
    float x = r.x + style.padding;
    if (icon > 0.0f)
    {
        draw_status_icon(canvas, theme, toast.kind, x + icon * 0.5f, top + room * 0.5f, icon);
        x += icon + kIconGap;
    }
    const float text_width = std::max(r.x + r.w - style.padding - x, 40.0f);

    std::vector<std::string> body;
    if (!toast.body.empty() && style.body_lines > 0)
        body = wrap_body(paint, toast.body, style.body_size, text_width, style.body_lines);
    float block = style.title_size * kTitleLine;
    if (!body.empty())
        block += kBodyGap + static_cast<float>(body.size()) * style.body_size * kBodyLine;
    const float block_top = top + (room - block) * 0.5f;
    paint.label(fit_label(paint, toast.title, style.title_size, text_width), x,
                block_top + style.title_size * 0.88f, style.title_size, theme.text);
    for (std::size_t i = 0; i < body.size(); ++i)
    {
        const float line_top = block_top + style.title_size * kTitleLine + kBodyGap +
                               static_cast<float>(i) * style.body_size * kBodyLine;
        paint.body(body[i], x, line_top + style.body_size * 0.9f, style.body_size,
                   theme.text_muted);
    }

    if (bar)
    {
        const float fraction = tween::clamp01(toast.left / toast.seconds);
        const float radius = theme.corner == Corner::round ? style.progress_height * 0.5f : 0.0f;
        const Rect track{r.x + style.padding,
                         r.y + r.h - style.padding * 0.6f - style.progress_height,
                         r.w - 2.0f * style.padding, style.progress_height};
        list.rounded_rect(track, radius, theme.text_muted.with_alpha(0.2f));
        if (fraction > 0.0f)
            list.rounded_rect({track.x, track.y, std::max(track.w * fraction, track.h), track.h},
                              radius, status_color(theme, toast.kind));
    }
}

void ToastStack::draw(Canvas &canvas) const
{
    if (shown_.empty())
        return;
    gfx::DrawList &list = canvas.list;
    const bool top = on_top(style.anchor);
    const int where = side(style.anchor);
    const bool still = style.reduced_motion;
    const float x = anchor_x() - style.width * 0.5f;

    // The stack is laid out here, from each toast's share of its room: a
    // leaving toast gives its room up with a spring and the rest follow.
    float cursor = 0.0f;
    for (const Toast &toast : shown_)
    {
        const float h = height(canvas, toast);
        const float y = top ? bounds_.y + style.margin + cursor
                            : bounds_.y + bounds_.h - style.margin - cursor - h;
        cursor += (h + style.gap) * toast.slot.value;

        const float in = toast.enter.value;
        const float out = tween::clamp01(toast.leave.value);
        const float opacity = tween::clamp01(in * 1.6f) * (1.0f - out);
        if (opacity <= 0.003f)
            continue;
        float dx = 0.0f;
        float dy = 0.0f;
        if (!still)
        {
            // Corners slide in from their side; the centre drops in (or rises).
            if (where != 0)
                dx = static_cast<float>(where) * ((1.0f - in) * (style.width + style.margin) +
                                                  tween::cubic_in(out) * style.width * 0.35f);
            else
                dy = (top ? -1.0f : 1.0f) *
                     ((1.0f - in) * (h + style.margin) + tween::cubic_in(out) * 36.0f);
        }
        list.push_opacity(opacity);
        list.push_transform(1.0f, 0.0f, 0.0f, dx, dy);
        draw_toast(canvas, toast, {x, y, style.width, h});
        list.pop_transform();
        list.pop_opacity();
    }
}

} // namespace hui::ui
