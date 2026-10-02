// ps5-homebrew-ui - Component: Rating.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/rating.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

// One glyph of `size`, centred. Every glyph is drawn opaque: the heart is
// three overlapping shapes, and a translucent one would show its seams.
void draw_glyph(Canvas &canvas, const Theme &theme, RatingGlyph glyph, float cx, float cy,
                float size, Color color)
{
    gfx::DrawList &list = canvas.list;
    switch (glyph)
    {
    case RatingGlyph::star:
        list.star(cx, cy + size * 0.02f, size * 0.52f, color);
        break;
    case RatingGlyph::heart:
    {
        // A square stood on its corner, with a disc on each upper edge.
        const float a = size * 0.56f;
        const float y = cy + a * 0.0735f;
        const float off = a * 0.35355f;
        list.rotated_rect({cx - a * 0.5f, y - a * 0.5f, a, a}, a * 0.07f, 0.7853982f, color);
        list.circle(cx - off, y - off, a * 0.5f, color);
        list.circle(cx + off, y - off, a * 0.5f, color);
        break;
    }
    case RatingGlyph::dot:
        if (theme.corner == Corner::round && theme.radius >= 2.0f)
            list.circle(cx, cy, size * 0.34f, color);
        else
            list.rounded_rect({cx - size * 0.3f, cy - size * 0.3f, size * 0.6f, size * 0.6f}, 0.0f,
                              color);
        break;
    }
}

} // namespace

void Rating::set_value(float value, bool snap)
{
    value_ = std::clamp(value, 0.0f, static_cast<float>(std::max(style.count, 0)));
    shown_.target = value_;
    if (snap)
    {
        shown_.snap(value_);
        started_ = false; // glyphs that are already full must not pop
    }
}

float Rating::width() const
{
    const int count = std::max(style.count, 0);
    if (count == 0)
        return 0.0f;
    return static_cast<float>(count) * style.size + static_cast<float>(count - 1) * style.gap;
}

Event Rating::handle(const InputFrame &input, Feedback &feedback)
{
    if (!style.interactive)
        return Event::none;
    const float x = bounds_.x + width() * 0.5f;
    if (input.nav == Direction::left || input.nav == Direction::right)
    {
        const float step = style.step > 0.0f ? style.step : 1.0f;
        const float top = static_cast<float>(std::max(style.count, 0));
        const float lowest = style.allow_zero ? 0.0f : std::min(step, top);
        // Move to the next step on the grid, so a displayed 4.3 goes to 5 or
        // to 4 and never to 5.3.
        const float cells = value_ / step;
        const float next =
            std::clamp(input.nav == Direction::right ? (std::floor(cells + 1e-3f) + 1.0f) * step
                                                     : (std::ceil(cells - 1e-3f) - 1.0f) * step,
                       lowest, top);
        if (std::fabs(next - value_) < 1e-4f)
            return refuse(feedback, style, input, refusal_, x);
        value_ = next;
        shown_.target = value_;
        const float along = top > 0.0f ? value_ / top : 0.0f;
        play_cue(feedback, style, style.sounds.step, bounds_.x + width() * along,
                 style.pitch_by_value ? tween::lerp(0.85f, 1.3f, along) : 1.0f);
        return Event::changed;
    }
    if (input.is_pressed(Action::confirm))
    {
        play_cue(feedback, style, style.sounds.activate, x);
        return Event::activated;
    }
    if (input.is_pressed(Action::back))
    {
        play_cue(feedback, style, style.sounds.cancel, x);
        return Event::cancelled;
    }
    return Event::none;
}

void Rating::update(float dt)
{
    const std::size_t count = static_cast<std::size_t>(std::max(style.count, 0));
    if (pops_.size() != count)
    {
        pops_.assign(count, tween::Bounce{1.0f, 0.0f, 1.0f});
        full_.assign(count, false);
        started_ = false;
        set_value(value_);
    }
    shown_.update(dt, std::min(style.omega(), 60.0f));
    const float damping = style.reduced_motion ? 1.0f : std::min(style.theme.damping, 0.45f);
    for (std::size_t i = 0; i < count; ++i)
    {
        // The eased value sweeps through the glyphs, so they pop in turn.
        const bool now = shown_.value >= static_cast<float>(i) + 0.97f;
        if (now && !full_[i] && started_ && !style.reduced_motion && style.pop > 0.0f)
        {
            pops_[i].value = 1.0f + 0.32f * style.pop;
            pops_[i].velocity = 0.0f;
        }
        full_[i] = now;
        pops_[i].update(dt, std::clamp(style.omega(), 14.0f, 40.0f), damping);
    }
    started_ = true;
    focus_.target = active_ ? 1.0f : 0.0f;
    focus_.update(dt, std::max(style.omega(), 18.0f));
    refusal_.update(dt, 9.0f);
}

void Rating::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const int count = std::max(style.count, 0);
    const float size = style.size;
    const float cy = bounds_.cy();
    const float left = bounds_.x + shake(refusal_.value, canvas.time, 8.0f);
    const Color fill = style.color.a > 0.0f ? style.color : status_color(theme, style.status);
    Color hollow = style.empty;
    if (hollow.a <= 0.0f)
    {
        Color muted = theme.text_muted;
        muted.a = 1.0f;
        hollow = gfx::mix(solid_surface(theme), muted, 0.36f);
    }
    const float shown = started_ ? shown_.value : value_;

    // The ring goes under the glyphs: some themes' rings light their inside.
    const float row = width();
    if (focus_.value > 0.01f)
    {
        const Rect ring{left - 12.0f, cy - size * 0.5f - 9.0f, row + 24.0f, size + 18.0f};
        paint.focus_ring(ring, paint.control_radius(ring), focus_.value);
    }

    for (int i = 0; i < count; ++i)
    {
        const float cx = left + size * 0.5f + static_cast<float>(i) * (size + style.gap);
        draw_glyph(canvas, theme, style.glyph, cx, cy, size, hollow);
        const float part = tween::clamp01(shown - static_cast<float>(i));
        if (part <= 0.004f)
            continue;
        const std::size_t at = static_cast<std::size_t>(i);
        const float scale = at < pops_.size() ? std::max(pops_[at].value, 0.0f) : 1.0f;
        // A fraction is the full glyph seen through a clip of that width.
        const bool partial = part < 0.996f;
        if (partial)
            list.push_clip({cx - size * 0.5f, cy - size, size * part, size * 2.0f});
        list.push_transform(scale, cx, cy, 0.0f, 0.0f);
        if (theme.style == SurfaceStyle::glow && !partial)
            list.glow({cx - size * 0.25f, cy - size * 0.25f, size * 0.5f, size * 0.5f},
                      size * 0.25f, size * 0.4f, fill.with_alpha(0.4f));
        draw_glyph(canvas, theme, style.glyph, cx, cy, size, fill);
        list.pop_transform();
        if (partial)
            list.pop_clip();
    }

    if (style.show_value)
    {
        char number[16];
        std::snprintf(number, sizeof(number), "%.*f", std::clamp(style.value_decimals, 0, 3),
                      static_cast<double>(value_));
        paint.label(number, left + row + style.value_gap, cy + style.value_size * 0.35f,
                    style.value_size, theme.text);
    }
}

} // namespace hui::ui
