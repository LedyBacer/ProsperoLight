// ps5-homebrew-ui - Component: CoachMark.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/coach_mark.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <utility>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

constexpr float kPi = 3.14159265f;
constexpr float kTitleLine = 1.2f;
constexpr int kArcSteps = 6;

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
        return {P::left, P::right, P::below, P::above};
    case P::right:
        return {P::right, P::left, P::below, P::above};
    default:
        return {P::above, P::below, P::right, P::left};
    }
}

// The bubble's pointer: a triangle whose tip is at (x, y), or two steps of
// squares in a pixel theme, where a diagonal would be the only smooth edge.
void draw_pointer(gfx::DrawList &list, const Theme &theme, TooltipPlacement placement, float x,
                  float y, float size, Color color)
{
    if (size <= 0.0f)
        return;
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
    const float length = size + 1.0f;
    const float cx = x + ux * length * 0.5f;
    const float cy = y + uy * length * 0.5f;
    list.triangle({cx - size, cy - length * 0.5f, 2.0f * size, length}, color, 0.0f, angle);
}

} // namespace

void CoachMark::start(std::vector<CoachStep> steps, Feedback &feedback)
{
    if (steps.empty())
        return;
    steps_ = std::move(steps);
    step_ = 0;
    open_ = true;
    // The spotlight closes in on the first target from the whole screen.
    hole_.snap(style.reduced_motion ? hole_for(0) : bounds_);
    retarget(false);
    play_cue(feedback, style, style.sounds.open, hole_for(0).cx());
}

void CoachMark::stop(Feedback &feedback)
{
    if (!open_)
        return;
    open_ = false;
    play_cue(feedback, style, style.sounds.close, hole_for(step_).cx());
}

void CoachMark::set_target(int index, const Rect &target)
{
    if (index < 0 || index >= count())
        return;
    steps_[static_cast<std::size_t>(index)].target = target;
    if (index == step_)
        hole_.target(hole_for(step_));
}

Rect CoachMark::hole_for(int index) const
{
    if (index < 0 || index >= count())
        return bounds_;
    return steps_[static_cast<std::size_t>(index)].target.inset(-style.spot_padding);
}

void CoachMark::retarget(bool snap)
{
    const Rect hole = hole_for(step_);
    hole_.target(hole);
    if (snap)
        hole_.snap(hole);
    words_.snap(0.0f);
    words_.target = 1.0f;
}

Rect CoachMark::spotlight() const
{
    return hole_.value();
}

Event CoachMark::handle(const InputFrame &input, Feedback &feedback)
{
    if (!open_ || steps_.empty())
        return Event::none;
    const float x = hole_for(step_).cx();
    const bool right = style.nav_steps && input.nav == Direction::right;
    const bool left = style.nav_steps && input.nav == Direction::left;
    const float along =
        count() > 1 ? static_cast<float>(step_) / static_cast<float>(count() - 1) : 0.0f;
    if (input.is_pressed(Action::confirm) || right)
    {
        if (step_ + 1 < count())
        {
            ++step_;
            retarget(false);
            // Each step sounds a little higher: the tour is getting somewhere.
            play_cue(feedback, style, style.sounds.page, hole_for(step_).cx(),
                     tween::lerp(1.0f, 1.12f, along));
            return Event::changed;
        }
        // The end is a decision: a direction alone does not finish the tour.
        if (right)
            return refuse(feedback, style, input, refusal_, x);
        open_ = false;
        play_cue(feedback, style, style.sounds.activate, x);
        if (style.sounds.rumble > 0.0f)
            feedback.rumble(0.3f * style.sounds.rumble, 0.04f);
        return Event::activated;
    }
    if (input.is_pressed(Action::back) || left)
    {
        if (step_ > 0)
        {
            --step_;
            retarget(false);
            play_cue(feedback, style, style.sounds.page, hole_for(step_).cx(),
                     tween::lerp(0.94f, 1.06f, along));
            return Event::changed;
        }
        if (left)
            return refuse(feedback, style, input, refusal_, x);
        open_ = false;
        play_cue(feedback, style, style.sounds.cancel, x);
        return Event::cancelled;
    }
    return Event::none;
}

void CoachMark::update(float dt)
{
    const float omega = style.omega();
    amount_.target = open_ ? 1.0f : 0.0f;
    amount_.update(dt, open_ ? std::max(omega, 12.0f) : std::max(omega, 12.0f) * 1.5f);
    // A glide across the screen is a large move: slower than a focus ring,
    // whatever the theme's pace, but never sluggish.
    hole_.update(dt, style.reduced_motion ? 60.0f : std::clamp(omega * 0.7f, 9.0f, 16.0f));
    words_.update(dt, style.reduced_motion ? 60.0f : 9.0f);
    refusal_.update(dt, 9.0f);
}

CoachMark::Laid CoachMark::lay(const Fonts &fonts) const
{
    Laid out;
    if (steps_.empty())
        return out;
    const Theme &theme = style.theme;
    gfx::DrawList scratch;
    const Canvas canvas{scratch, fonts, 0, 0.0f};
    const Painter paint(scratch, fonts, theme, 0);
    const CoachStep &now = steps_[static_cast<std::size_t>(step_)];
    const Rect area = bounds_.inset(style.screen_margin);
    const float w = std::min(style.width, area.w);
    const float column = std::max(w - 2.0f * style.padding, 40.0f);

    out.title = wrap_heading(canvas, theme, now.title, style.title_size, column, 2);
    out.text = wrap_body(paint, now.text, style.text_size, column, std::max(style.text_lines, 1));
    float y = style.padding;
    if (style.counter && count() > 1)
        y += style.counter_size * 1.6f;
    out.title_top = y;
    y += static_cast<float>(out.title.size()) * style.title_size * kTitleLine;
    if (!out.title.empty() && !out.text.empty())
        y += 6.0f;
    out.text_top = y;
    y += static_cast<float>(out.text.size()) * style.text_size * style.text_line;
    if (style.hints)
    {
        y += 18.0f;
        out.hints_cy = y + style.hint_size * 0.5f;
        y += style.hint_size;
    }
    const float h = y + style.padding;

    // Placed against where the spotlight will rest, not where it is now, so
    // the side does not flip while it travels.
    const Rect a = hole_for(step_);
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
    out.at.placement = now.placement;
    for (const TooltipPlacement candidate : order(now.placement))
    {
        if (fits(candidate, box(candidate)))
        {
            out.at.placement = candidate;
            break;
        }
    }
    Rect r = box(out.at.placement);
    r.x = std::clamp(r.x, area.x, std::max(area.x, area.x + area.w - w));
    r.y = std::clamp(r.y, area.y, std::max(area.y, area.y + area.h - h));
    const float corner = std::min(theme.radius_card, std::min(w, h) * 0.5f);
    const float keep = corner + std::max(style.pointer, 0.0f) + 4.0f;
    if (vertical(out.at.placement))
    {
        out.at.tip_x =
            std::clamp(a.cx(), r.x + std::min(keep, w * 0.5f), r.x + w - std::min(keep, w * 0.5f));
        out.at.tip_y = out.at.placement == TooltipPlacement::above ? r.y + r.h + style.pointer
                                                                   : r.y - style.pointer;
    }
    else
    {
        out.at.tip_y =
            std::clamp(a.cy(), r.y + std::min(keep, h * 0.5f), r.y + h - std::min(keep, h * 0.5f));
        out.at.tip_x = out.at.placement == TooltipPlacement::left ? r.x + r.w + style.pointer
                                                                  : r.x - style.pointer;
    }
    out.at.bubble = r;
    return out;
}

Tooltip::Placed CoachMark::bubble(const Fonts &fonts) const
{
    return lay(fonts).at;
}

// Everything but the spotlight goes dark: four rectangles around the hole,
// and in each of its corners the sliver between the hole's square corner and
// its round (or cut, or notched) one.
void CoachMark::draw_dim(Canvas &canvas, const Rect &hole, float radius, float fade) const
{
    gfx::DrawList &list = canvas.list;
    const Theme &theme = style.theme;
    const Color dim = style.scrim_color.with_alpha(style.scrim * fade);
    const Rect &b = bounds_;
    const float x0 = std::clamp(hole.x, b.x, b.x + b.w);
    const float x1 = std::clamp(hole.x + hole.w, x0, b.x + b.w);
    const float y0 = std::clamp(hole.y, b.y, b.y + b.h);
    const float y1 = std::clamp(hole.y + hole.h, y0, b.y + b.h);
    // Drawn as plain polygons, not as shapes: a shape has a soft edge, and
    // two soft edges side by side leave a hairline across the screen.
    const auto block = [&](float left, float top, float right, float bottom)
    {
        if (right <= left || bottom <= top)
            return;
        const float xy[8] = {left, top, right, top, right, bottom, left, bottom};
        list.polygon(xy, 4, dim);
    };
    block(b.x, b.y, b.x + b.w, y0);
    block(b.x, y1, b.x + b.w, b.y + b.h);
    block(b.x, y0, x0, y1);
    block(x1, y0, b.x + b.w, y1);

    const float r = std::min(radius, std::min(x1 - x0, y1 - y0) * 0.5f);
    if (r < 1.0f)
        return;
    // sx, sy: which way the corner faces (-1 left or top, 1 right or bottom).
    const auto corner = [&](float px, float py, float sx, float sy)
    {
        if (theme.corner == Corner::pixel)
        {
            const float n = std::min(theme.border, r);
            const float left = sx < 0.0f ? px : px - n;
            const float top = sy < 0.0f ? py : py - n;
            block(left, top, left + n, top + n);
            return;
        }
        float xy[2 * (kArcSteps + 2)];
        int count = 0;
        xy[count++] = px;
        xy[count++] = py;
        if (theme.corner == Corner::chamfer)
        {
            xy[count++] = px - sx * r;
            xy[count++] = py;
            xy[count++] = px;
            xy[count++] = py - sy * r;
        }
        else
        {
            const float cx = px - sx * r;
            const float cy = py - sy * r;
            for (int i = 0; i <= kArcSteps; ++i)
            {
                const float t = static_cast<float>(i) / static_cast<float>(kArcSteps) * kPi * 0.5f;
                xy[count++] = cx + sx * r * std::sin(t);
                xy[count++] = cy + sy * r * std::cos(t);
            }
        }
        list.polygon(xy, count / 2, dim);
    };
    corner(x0, y0, -1.0f, -1.0f);
    corner(x1, y0, 1.0f, -1.0f);
    corner(x0, y1, -1.0f, 1.0f);
    corner(x1, y1, 1.0f, 1.0f);
}

void CoachMark::draw(Canvas &canvas) const
{
    if (!visible() || steps_.empty())
        return;
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    const float fade = tween::clamp01(amount_.value);
    Rect hole = hole_.value();
    hole.x += shake(refusal_.value, canvas.time, 8.0f);
    const float wanted =
        style.spot_radius >= 0.0f
            ? style.spot_radius
            : (theme.radius <= 0.0f ? 0.0f
                                    : std::min(theme.radius + style.spot_padding * 0.5f, 26.0f));
    const float radius = std::min(wanted, std::min(hole.w, hole.h) * 0.5f);
    draw_dim(canvas, hole, radius, fade);

    // No glass for the ring and the bubble: they must read against anything.
    Painter paint(list, canvas.fonts, theme, 0);
    list.push_opacity(fade);
    const Color light{theme.focus.r, theme.focus.g, theme.focus.b, std::max(theme.focus.a, 0.9f)};
    if (style.ring_width > 0.0f)
    {
        // Centred on the hole's edge, so it also covers the dim's own edge.
        const float half = style.ring_width * 0.5f;
        const Rect ring = hole.inset(-half);
        const float ring_radius = radius > 0.0f ? radius + half : 0.0f;
        if (style.ring_pulse && theme.corner == Corner::round)
        {
            const float idle = style.reduced_motion ? 0.5f : breathe(canvas.time);
            paint.halo(ring, ring_radius, 8.0f + 10.0f * idle,
                       light.with_alpha(0.22f + 0.2f * idle));
        }
        paint.stroke(ring, ring_radius, style.ring_width, light);
    }

    const Laid laid = lay(canvas.fonts);
    // The bubble travels with the spotlight: it rests where the layout says
    // and is offset by however far the spotlight still has to go.
    const Rect rest = hole_for(step_);
    const Rect area = bounds_.inset(style.screen_margin);
    Rect box = laid.at.bubble;
    const float want_x = box.x + hole.cx() - rest.cx();
    const float want_y = box.y + hole.cy() - rest.cy();
    const float dx = std::clamp(want_x, area.x, std::max(area.x, area.x + area.w - box.w)) - box.x;
    const float dy = std::clamp(want_y, area.y, std::max(area.y, area.y + area.h - box.h)) - box.y;
    box.x += dx;
    box.y += dy;

    const Color fill = opaque_over(theme.page, theme.surface);
    const float line = std::max(theme.border, 0.0f);
    const float tip_x = laid.at.tip_x + dx;
    const float tip_y = laid.at.tip_y + dy;
    if (line > 0.0f)
        draw_pointer(list, theme, laid.at.placement, tip_x, tip_y, style.pointer,
                     opaque_over(fill, theme.outline));
    draw_overlay_panel(canvas, theme, box, false);
    {
        // The pointer again in the panel's colour, moved in by the stroke.
        const float inward = line * 1.6f;
        float tx = tip_x;
        float ty = tip_y;
        if (laid.at.placement == TooltipPlacement::above)
            ty -= inward;
        else if (laid.at.placement == TooltipPlacement::below)
            ty += inward;
        else if (laid.at.placement == TooltipPlacement::left)
            tx -= inward;
        else
            tx += inward;
        draw_pointer(list, theme, laid.at.placement, tx, ty, style.pointer, fill);
    }

    list.push_opacity(tween::clamp01(words_.value));
    const float x = box.x + style.padding;
    if (style.counter && count() > 1)
    {
        char text[32];
        std::snprintf(text, sizeof(text), "%d of %d", step_ + 1, count());
        paint.label(text, x, box.y + style.padding + style.counter_size * 0.9f, style.counter_size,
                    theme.text_muted);
    }
    for (std::size_t i = 0; i < laid.title.size(); ++i)
    {
        const float top =
            box.y + laid.title_top + static_cast<float>(i) * style.title_size * kTitleLine;
        paint.heading(laid.title[i], x, top + style.title_size * 0.9f, style.title_size,
                      theme.text);
    }
    for (std::size_t i = 0; i < laid.text.size(); ++i)
    {
        const float top =
            box.y + laid.text_top + static_cast<float>(i) * style.text_size * style.text_line;
        paint.body(laid.text[i], x, top + style.text_size * 0.95f, style.text_size,
                   theme.text_muted);
    }
    list.pop_opacity();

    if (style.hints)
    {
        const bool first = step_ == 0;
        const bool last = step_ + 1 >= count();
        const Hint hints[2] = {
            {style.confirm_glyph, last ? style.done_label.c_str() : style.next_label.c_str()},
            {style.back_glyph, first ? style.skip_label.c_str() : style.back_label.c_str()},
        };
        GlyphStyle glyphs = Painter::on(fill).r < 0.5f ? GlyphStyle::light() : GlyphStyle::dark();
        glyphs.label = theme.text;
        HintLayout row;
        row.size = style.hint_size;
        row.text_size = style.hint_text;
        row.cy = box.y + laid.hints_cy;
        row.item_gap = 28.0f;
        draw_hints(list, canvas.fonts, glyphs, hints, 2, x, false, row);
    }
    list.pop_opacity();
}

} // namespace hui::ui
