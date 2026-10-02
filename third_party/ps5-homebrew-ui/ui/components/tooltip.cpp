// ps5-homebrew-ui - Component: Tooltip.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/tooltip.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

constexpr float kLine = 1.3f; // line height, in text sizes
constexpr float kPi = 3.14159265f;

bool vertical(TooltipPlacement placement)
{
    return placement == TooltipPlacement::above || placement == TooltipPlacement::below;
}

// The preferred side, its opposite, then the two sides of the other axis.
std::array<TooltipPlacement, 4> order(TooltipPlacement wanted)
{
    using P = TooltipPlacement;
    switch (wanted)
    {
    case P::below:
        return {P::below, P::above, P::right, P::left};
    case P::left:
        return {P::left, P::right, P::above, P::below};
    case P::right:
        return {P::right, P::left, P::above, P::below};
    default:
        return {P::above, P::below, P::right, P::left};
    }
}

bool same(const Rect &a, const Rect &b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

// The pointer: a triangle whose tip is at (x, y), or two steps of squares in
// a pixel theme, where a diagonal would be the only smooth edge on screen.
void draw_pointer(gfx::DrawList &list, const Theme &theme, TooltipPlacement placement, float x,
                  float y, float size, Color color)
{
    if (size <= 0.0f)
        return;
    // The side the bubble is on, as a direction from the tip.
    float ux = 0.0f;
    float uy = 0.0f;
    float angle = 0.0f;
    switch (placement)
    {
    case TooltipPlacement::above:
        uy = -1.0f;
        angle = kPi;
        break;
    case TooltipPlacement::below:
        uy = 1.0f;
        break;
    case TooltipPlacement::left:
        ux = -1.0f;
        angle = kPi * 0.5f;
        break;
    case TooltipPlacement::right:
        ux = 1.0f;
        angle = kPi * 1.5f;
        break;
    }
    if (theme.corner == Corner::pixel)
    {
        const float step = size * 0.5f;
        for (int i = 0; i < 2; ++i)
        {
            const float along = (static_cast<float>(i) + 0.5f) * step;
            const float half = step * static_cast<float>(i + 1);
            const float cx = x + ux * along;
            const float cy = y + uy * along;
            const float w = ux != 0.0f ? step : 2.0f * half;
            const float h = ux != 0.0f ? 2.0f * half : step;
            list.rounded_rect({cx - w * 0.5f, cy - h * 0.5f, w, h}, 0.0f, color);
        }
        return;
    }
    // One pixel longer, so no seam shows between the pointer and the bubble.
    const float length = size + 1.0f;
    const float cx = x + ux * length * 0.5f;
    const float cy = y + uy * length * 0.5f;
    list.triangle({cx - size, cy - length * 0.5f, 2.0f * size, length}, color, 0.0f, angle);
}

} // namespace

void Tooltip::show(const Rect &anchor, std::string text)
{
    if (text == now_.text && (shown_ || same(anchor, now_.anchor)))
    {
        // The same label: follow the anchor, or come back if it was leaving.
        now_.anchor = anchor;
        if (!shown_)
            wait_ = amount_.value > 0.01f ? 0.0f : std::max(style.delay, 0.0f);
        shown_ = true;
        return;
    }
    // What was showing fades out where it was while the new one arrives.
    if (amount_.value > 0.01f)
    {
        before_ = now_;
        before_amount_.snap(amount_.value);
    }
    now_.anchor = anchor;
    now_.text = std::move(text);
    shown_ = true;
    wait_ = std::max(style.delay, 0.0f);
    amount_.snap(0.0f);
}

void Tooltip::hide()
{
    shown_ = false;
}

bool Tooltip::visible() const
{
    return amount_.value > 0.004f || before_amount_.value > 0.004f;
}

void Tooltip::update(float dt)
{
    if (shown_ && wait_ > 0.0f)
        wait_ -= dt;
    amount_.target = shown_ && wait_ <= 0.0f && !now_.text.empty() ? 1.0f : 0.0f;
    // A label must not trail behind the focus, whatever the theme's pace.
    const float omega = std::max(style.omega(), 16.0f);
    amount_.update(dt, amount_.target > 0.5f ? omega : omega * 1.6f);
    before_amount_.target = 0.0f;
    before_amount_.update(dt, omega * 1.8f);
}

Tooltip::Placed Tooltip::place(const Canvas &canvas) const
{
    return place(canvas, now_);
}

Tooltip::Placed Tooltip::place(const Canvas &canvas, const Bubble &bubble) const
{
    const Painter paint(canvas.list, canvas.fonts, style.theme, 0);
    const float column = std::max(style.max_width - 2.0f * style.padding_x, 40.0f);
    const std::vector<std::string> lines =
        wrap_body(paint, bubble.text, style.text_size, column, std::max(style.max_lines, 1));
    float widest = 0.0f;
    for (const std::string &line : lines)
        widest = std::max(widest, paint.body_width(line, style.text_size));
    const float w = widest + 2.0f * style.padding_x;
    const float h =
        static_cast<float>(std::max<std::size_t>(lines.size(), 1)) * style.text_size * kLine +
        2.0f * style.padding_y;

    const Rect &a = bubble.anchor;
    const Rect area = bounds_.inset(style.screen_margin);
    const float reach = style.gap + std::max(style.pointer, 0.0f);
    const auto box = [&](TooltipPlacement placement) -> Rect
    {
        switch (placement)
        {
        case TooltipPlacement::above:
            return {a.cx() - w * 0.5f, a.y - reach - h, w, h};
        case TooltipPlacement::below:
            return {a.cx() - w * 0.5f, a.y + a.h + reach, w, h};
        case TooltipPlacement::left:
            return {a.x - reach - w, a.cy() - h * 0.5f, w, h};
        case TooltipPlacement::right:
            break;
        }
        return {a.x + a.w + reach, a.cy() - h * 0.5f, w, h};
    };
    const auto fits = [&](TooltipPlacement placement, const Rect &r)
    {
        switch (placement)
        {
        case TooltipPlacement::above:
            return r.y >= area.y;
        case TooltipPlacement::below:
            return r.y + r.h <= area.y + area.h;
        case TooltipPlacement::left:
            return r.x >= area.x;
        case TooltipPlacement::right:
            break;
        }
        return r.x + r.w <= area.x + area.w;
    };

    Placed out;
    out.placement = style.placement;
    for (const TooltipPlacement candidate : order(style.placement))
    {
        if (fits(candidate, box(candidate)))
        {
            out.placement = candidate;
            break;
        }
    }
    Rect r = box(out.placement);
    // Along the other axis the bubble slides to stay inside; the pointer
    // keeps aiming at the anchor as far as the bubble's corners allow.
    const float corner = std::min(std::min(style.theme.radius, 12.0f), h * 0.5f);
    const float keep = corner + std::max(style.pointer, 0.0f);
    if (vertical(out.placement))
    {
        r.x = std::clamp(r.x, area.x, std::max(area.x, area.x + area.w - w));
        out.tip_x =
            std::clamp(a.cx(), r.x + std::min(keep, w * 0.5f), r.x + w - std::min(keep, w * 0.5f));
        out.tip_y =
            out.placement == TooltipPlacement::above ? a.y - style.gap : a.y + a.h + style.gap;
    }
    else
    {
        r.y = std::clamp(r.y, area.y, std::max(area.y, area.y + area.h - h));
        out.tip_y =
            std::clamp(a.cy(), r.y + std::min(keep, h * 0.5f), r.y + h - std::min(keep, h * 0.5f));
        out.tip_x =
            out.placement == TooltipPlacement::left ? a.x - style.gap : a.x + a.w + style.gap;
    }
    out.bubble = r;
    return out;
}

void Tooltip::draw_bubble(Canvas &canvas, const Bubble &bubble, float amount, bool arriving) const
{
    amount = tween::clamp01(amount);
    if (amount <= 0.004f || bubble.text.empty())
        return;
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    const Placed at = place(canvas, bubble);
    const Rect &r = at.bubble;

    // It arrives from a little nearer the thing it describes.
    float dx = 0.0f;
    float dy = 0.0f;
    if (arriving && !style.reduced_motion)
    {
        const float d = style.slide * (1.0f - tween::cubic_out(amount));
        if (at.placement == TooltipPlacement::above)
            dy = d;
        else if (at.placement == TooltipPlacement::below)
            dy = -d;
        else if (at.placement == TooltipPlacement::left)
            dx = d;
        else
            dx = -d;
    }
    list.push_opacity(amount);
    list.push_transform(1.0f, 0.0f, 0.0f, dx, dy);

    // No glass here: a label must read against anything.
    Painter paint(list, canvas.fonts, theme, 0);
    const float radius = std::min(std::min(theme.radius, 12.0f), r.h * 0.5f);
    Color ink = theme.text;
    if (style.inverted)
    {
        const Color fill{theme.text.r, theme.text.g, theme.text.b, 1.0f};
        ink = Painter::on(fill);
        draw_pointer(list, theme, at.placement, at.tip_x, at.tip_y, style.pointer, fill);
        paint.fill(r, radius, fill);
    }
    else
    {
        const Color fill = opaque_over(theme.page, theme.surface_high);
        const Color edge = opaque_over(fill, theme.outline);
        // The pointer is drawn twice: in the stroke's colour under the
        // bubble, then in the fill's over the stroke, moved in by its width.
        const float line = std::max(theme.border, 0.0f);
        if (line > 0.0f)
            draw_pointer(list, theme, at.placement, at.tip_x, at.tip_y, style.pointer, edge);
        const Rect body = paint.surface(r, radius, fill, theme.outline, 1.0f);
        const float inward = line * 1.6f;
        float tx = at.tip_x + (body.x - r.x);
        float ty = at.tip_y + (body.y - r.y);
        if (at.placement == TooltipPlacement::above)
            ty -= inward;
        else if (at.placement == TooltipPlacement::below)
            ty += inward;
        else if (at.placement == TooltipPlacement::left)
            tx -= inward;
        else
            tx += inward;
        draw_pointer(list, theme, at.placement, tx, ty, style.pointer, fill);
    }

    const float column = std::max(style.max_width - 2.0f * style.padding_x, 40.0f);
    const std::vector<std::string> lines =
        wrap_body(paint, bubble.text, style.text_size, column, std::max(style.max_lines, 1));
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        const float top = r.y + style.padding_y + static_cast<float>(i) * style.text_size * kLine;
        paint.body(lines[i], r.x + style.padding_x, top + style.text_size * 0.96f, style.text_size,
                   ink);
    }
    list.pop_transform();
    list.pop_opacity();
}

void Tooltip::draw(Canvas &canvas) const
{
    draw_bubble(canvas, before_, before_amount_.value, false);
    draw_bubble(canvas, now_, amount_.value, true);
}

} // namespace hui::ui
