// ps5-homebrew-ui - Component: Sheet.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/sheet.hpp"

#include <algorithm>
#include <cmath>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

constexpr float kHandleLength = 56.0f;
constexpr float kHandleThickness = 5.0f;
constexpr float kHandleInset = 12.0f;

} // namespace

void Sheet::open(Feedback &feedback)
{
    if (!visible())
    {
        fade_.snap(0.0f);
        slide_.snap(0.0f);
    }
    open_ = true;
    play_cue(feedback, style, style.sounds.open, panel_rect().cx());
}

void Sheet::close(Feedback &feedback)
{
    if (!open_)
        return;
    open_ = false;
    play_cue(feedback, style, style.sounds.close, panel_rect().cx());
}

void Sheet::dismiss()
{
    open_ = false;
    fade_.snap(0.0f);
    slide_.snap(0.0f);
}

bool Sheet::visible() const
{
    return open_ || fade_.value > 0.004f;
}

Rect Sheet::room() const
{
    return {bounds_.x, bounds_.y, bounds_.w,
            std::max(bounds_.h - std::max(style.footer, 0.0f), 0.0f)};
}

Rect Sheet::panel_rect() const
{
    const float m = std::max(style.margin, 0.0f);
    const Rect b = room();
    switch (style.edge)
    {
    case SheetEdge::left:
        return {b.x + m, b.y + m, std::min(style.size, b.w - 2.0f * m), b.h - 2.0f * m};
    case SheetEdge::right:
    {
        const float width = std::min(style.size, b.w - 2.0f * m);
        return {b.x + b.w - m - width, b.y + m, width, b.h - 2.0f * m};
    }
    case SheetEdge::bottom:
        break;
    }
    const float height = std::min(style.size, b.h - 2.0f * m);
    return {b.x + m, b.y + b.h - m - height, b.w - 2.0f * m, height};
}

float Sheet::header_height() const
{
    float height = 0.0f;
    // The grab bar of a bottom sheet sits above the title.
    if (style.handle && style.edge == SheetEdge::bottom)
        height += kHandleInset + kHandleThickness;
    if (!title_.empty())
        height += style.title_size * 1.25f + style.padding * 0.55f;
    return height;
}

Rect Sheet::content_rect() const
{
    const Rect panel = panel_rect();
    const float top = panel.y + style.padding + header_height();
    return {panel.x + style.padding, top, std::max(panel.w - 2.0f * style.padding, 0.0f),
            std::max(panel.y + panel.h - style.padding - top, 0.0f)};
}

Event Sheet::handle(const InputFrame &input, Feedback &feedback)
{
    if (!open_ || !input.is_pressed(Action::back))
        return Event::none;
    if (!style.dismissable)
        return refuse(feedback, style, input, refusal_, panel_rect().cx());
    close(feedback);
    return Event::cancelled;
}

void Sheet::update(float dt)
{
    const float omega = style.omega();
    if (open_)
    {
        fade_.target = 1.0f;
        fade_.update(dt, omega * 1.2f);
        slide_.target = 1.0f;
        // A drawer is a large thing: a hard bounce on it reads as a glitch.
        slide_.update(dt, omega, std::max(style.damping(), 0.7f));
    }
    else
    {
        const float out = omega * std::max(style.exit_speed, 0.1f);
        fade_.target = 0.0f;
        fade_.update(dt, out);
        slide_.target = 0.0f;
        slide_.update(dt, out, 1.0f);
    }
    refusal_.update(dt, 9.0f);
}

void Sheet::draw(Canvas &canvas) const
{
    if (!visible())
        return;
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    const float fade = tween::clamp01(fade_.value);
    list.rounded_rect(bounds_, 0.0f, style.scrim_color.with_alpha(style.scrim * fade));

    const Rect panel = panel_rect();
    const bool attached = style.margin <= 0.0f;
    const float over = attached ? std::max(style.overhang, 0.0f) : 0.0f;
    Rect shape = panel;
    switch (style.edge)
    {
    case SheetEdge::left:
        shape = {panel.x - over, panel.y - over, panel.w + over, panel.h + 2.0f * over};
        break;
    case SheetEdge::right:
        shape = {panel.x, panel.y - over, panel.w + over, panel.h + 2.0f * over};
        break;
    case SheetEdge::bottom:
        shape = {panel.x - over, panel.y, panel.w + 2.0f * over, panel.h + over};
        break;
    }

    // Closed, the sheet is one whole size (and its shadow) beyond the edge.
    const bool still = style.reduced_motion;
    const float hidden = still ? 0.0f : 1.0f - slide_.value;
    const float travel = (style.edge == SheetEdge::bottom ? panel.h : panel.w) +
                         std::max(style.margin, 0.0f) + 48.0f;
    float dx = shake(refusal_.value, canvas.time, 10.0f);
    float dy = 0.0f;
    if (style.edge == SheetEdge::left)
        dx -= hidden * travel;
    else if (style.edge == SheetEdge::right)
        dx += hidden * travel;
    else
        dy += hidden * travel;

    // The slide carries the motion; the fade only softens its first frames.
    const float opacity = still ? fade : tween::clamp01(fade * 3.0f);
    // Cut at the bounds: an attached panel overhangs them on purpose, and a
    // footer below must stay clear.
    list.push_clip(room());
    list.push_opacity(opacity);
    list.push_transform(1.0f, 0.0f, 0.0f, dx, dy);
    draw_overlay_panel(canvas, theme, shape, style.frosted, style.frost);
    Painter paint(list, canvas.fonts, theme, canvas.glass);

    if (style.handle)
    {
        const Color bar = theme.text_muted.with_alpha(0.45f);
        const float radius = theme.corner == Corner::round ? kHandleThickness * 0.5f : 0.0f;
        if (style.edge == SheetEdge::bottom)
            paint.fill({panel.cx() - kHandleLength * 0.5f, panel.y + kHandleInset, kHandleLength,
                        kHandleThickness},
                       radius, bar);
        else if (style.edge == SheetEdge::left)
            paint.fill({panel.x + panel.w - kHandleInset - kHandleThickness,
                        panel.cy() - kHandleLength * 0.5f, kHandleThickness, kHandleLength},
                       radius, bar);
        else
            paint.fill({panel.x + kHandleInset, panel.cy() - kHandleLength * 0.5f, kHandleThickness,
                        kHandleLength},
                       radius, bar);
    }

    const Rect area = content_rect();
    if (!title_.empty())
    {
        const std::vector<std::string> title =
            wrap_heading(canvas, theme, title_, style.title_size, area.w, 1);
        const float top = area.y - style.title_size * 1.25f - style.padding * 0.55f;
        if (!title.empty())
            paint.heading(title[0], area.x, top + style.title_size * 0.9f, style.title_size,
                          theme.text);
        if (style.divider)
            list.rounded_rect({area.x, area.y - style.padding * 0.3f, area.w, 1.5f}, 0.0f,
                              theme.text_muted.with_alpha(0.25f));
    }
    if (content)
        content(canvas, area, opacity);

    list.pop_transform();
    list.pop_opacity();
    list.pop_clip();
}

} // namespace hui::ui
