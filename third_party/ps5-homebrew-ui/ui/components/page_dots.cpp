// ps5-homebrew-ui - Component: PageDots.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/page_dots.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

// Where a run of the given width starts inside the bounds.
float aligned(const Rect &bounds, float width, gfx::Align align)
{
    if (align == gfx::Align::left)
        return bounds.x;
    if (align == gfx::Align::right)
        return bounds.x + bounds.w - width;
    return bounds.cx() - width * 0.5f;
}

} // namespace

void PageDots::set_count(int count)
{
    count_ = std::max(count, 0);
    page_ = std::clamp(page_, 0, std::max(count_ - 1, 0));
    value_.snap(static_cast<float>(page_));
}

void PageDots::set_page(int page, bool snap)
{
    if (count_ <= 0)
        return;
    const int next = std::clamp(page, 0, count_ - 1);
    if (next != page_)
        progress_ = 0.0f;
    page_ = next;
    value_.target = static_cast<float>(page_);
    if (snap)
        value_.snap(value_.target);
}

bool PageDots::take_advanced()
{
    const bool advanced = advanced_;
    advanced_ = false;
    return advanced;
}

Event PageDots::handle(const InputFrame &input, Feedback &feedback)
{
    if (input.nav != Direction::left && input.nav != Direction::right)
        return Event::none;
    const float x = bounds_.cx();
    int next = page_ + (input.nav == Direction::right ? 1 : -1);
    if ((next < 0 || next >= count_) && style.wrap && !input.nav_repeat && count_ > 1)
        next = (next + count_) % count_;
    if (next < 0 || next >= count_ || next == page_)
        return refuse(feedback, style, input, refusal_, x);
    set_page(next);
    const float along =
        count_ > 1 ? static_cast<float>(page_) / static_cast<float>(count_ - 1) : 0.0f;
    play_cue(feedback, style, style.sounds.page, x,
             style.pitch_by_position ? tween::lerp(0.96f, 1.06f, along) : 1.0f);
    return Event::changed;
}

void PageDots::update(float dt)
{
    if (style.auto_advance > 0.0f && !paused_ && count_ > 1)
    {
        progress_ += dt / style.auto_advance;
        if (progress_ >= 1.0f)
        {
            if (page_ + 1 < count_ || style.wrap)
            {
                set_page((page_ + 1) % count_);
                progress_ = 0.0f;
                advanced_ = true;
            }
            else
            {
                progress_ = 1.0f; // the last page stays, full
            }
        }
    }
    value_.target = static_cast<float>(page_);
    value_.update(dt, std::max(style.omega(), 14.0f), std::max(style.damping(), 0.72f));
    focus_amount_.target = focused_ ? 1.0f : 0.0f;
    focus_amount_.update(dt, 18.0f);
    refusal_.update(dt, 9.0f);
}

void PageDots::draw_marks(Canvas &canvas, Painter &paint, Color idle, Color active) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    const bool dashes = style.kind == PageDotsKind::dashes;
    const bool timed = style.show_progress && (style.auto_advance > 0.0f || progress_ > 0.0f);
    const float value = value_.value;

    // With more pages than fit, a window of marks follows the active one and
    // the marks at its ends shrink: "there is more this way".
    const int visible = std::max(style.max_visible, 3);
    const bool windowed = count_ > visible;
    const float half = static_cast<float>(visible - 1) * 0.5f;
    const float last = static_cast<float>(count_ - 1);
    const float centre = windowed ? std::clamp(value, half, last - half) : last * 0.5f;

    struct Mark
    {
        float w, h, scale, weight;
    };
    std::vector<Mark> marks(static_cast<std::size_t>(count_));
    float total = 0.0f;
    float tallest = 0.0f;
    for (int i = 0; i < count_; ++i)
    {
        Mark &mark = marks[static_cast<std::size_t>(i)];
        const float position = static_cast<float>(i);
        const float distance = std::fabs(position - centre);
        mark.scale = 1.0f;
        if (windowed)
        {
            mark.scale = tween::clamp01(half + 1.0f - distance);
            const bool more =
                position < centre ? centre - half > 0.01f : centre + half < last - 0.01f;
            if (more)
                mark.scale *= tween::lerp(1.0f, 0.55f, tween::clamp01(distance - (half - 1.0f)));
        }
        mark.weight = tween::clamp01(1.0f - std::fabs(value - position));
        if (dashes)
        {
            mark.w = tween::lerp(style.dash_width, style.active_width, mark.weight);
            mark.h = style.dash_thickness;
        }
        else
        {
            const float rest = style.dot_size * style.idle_scale;
            mark.w = tween::lerp(rest, timed ? style.active_width : style.dot_size, mark.weight);
            mark.h = tween::lerp(rest, style.dot_size, mark.weight);
        }
        mark.w *= mark.scale;
        mark.h *= mark.scale;
        total += mark.w;
        if (i > 0)
            total += style.gap * std::min(mark.scale, marks[static_cast<std::size_t>(i - 1)].scale);
        tallest = std::max(tallest, mark.h);
    }

    const float start =
        aligned(bounds_, total, style.align) + shake(refusal_.value, canvas.time, 8.0f);
    const float cy = bounds_.cy();
    // The ring goes under the marks: a ring may light what it surrounds.
    if (style.focus_ring)
    {
        const float height = std::max(tallest, dashes ? style.dash_thickness : style.dot_size);
        const Rect ring{start - 14.0f, cy - height * 0.5f - 10.0f, total + 28.0f, height + 20.0f};
        paint.focus_ring(ring, paint.control_radius(ring), focus_amount_.value);
    }
    float x = start;
    for (int i = 0; i < count_; ++i)
    {
        const Mark &mark = marks[static_cast<std::size_t>(i)];
        if (i > 0)
            x += style.gap * std::min(mark.scale, marks[static_cast<std::size_t>(i - 1)].scale);
        if (mark.scale > 0.01f)
        {
            const Rect r{x, cy - mark.h * 0.5f, mark.w, mark.h};
            // Square design languages keep square marks.
            const float radius = theme.radius < 2.0f
                                     ? 0.0f
                                     : std::min(std::max(theme.radius, 100.0f), mark.h * 0.5f);
            const bool filling = timed && i == page_;
            const Color body = filling ? gfx::mix(idle, active.with_alpha(0.38f), mark.weight)
                                       : gfx::mix(idle, active, mark.weight);
            paint.fill(r, radius, body);
            if (filling && progress_ > 0.0f)
            {
                // The same mark again, cut where the time has reached: the
                // fill keeps the mark's corners whatever the theme's are.
                list.push_clip({r.x - 1.0f, r.y - 2.0f, (r.w + 2.0f) * progress_, r.h + 4.0f});
                paint.fill(r, radius, active.with_alpha(mark.weight));
                list.pop_clip();
            }
        }
        x += mark.w;
    }
}

void PageDots::draw_numbers(Canvas &canvas, Painter &paint, Color strong, Color muted,
                            Color active) const
{
    gfx::DrawList &list = canvas.list;
    const float size = style.text_size;
    char of_text[16];
    char widest[16];
    std::snprintf(of_text, sizeof(of_text), " / %d", count_);
    std::snprintf(widest, sizeof(widest), "%d", count_);
    // The widest number reserves the room, so the text does not shift as the
    // current one changes.
    const float number_w = paint.label_width(widest, size);
    const float of_w = paint.label_width(of_text, size);
    const float total = number_w + of_w;
    const float start =
        aligned(bounds_, total, style.align) + shake(refusal_.value, canvas.time, 8.0f);
    const float cy = bounds_.cy();
    const float baseline = cy + size * 0.35f;
    const float split = start + number_w;
    if (style.focus_ring)
    {
        const Rect ring{start - 14.0f, cy - size * 0.85f, total + 28.0f, size * 1.7f};
        paint.focus_ring(ring, paint.control_radius(ring), focus_amount_.value);
    }

    // The current number rolls: the old one leaves upward as the new one
    // arrives from below. With reduced motion they only cross-fade.
    const float value = std::clamp(value_.value, 0.0f, static_cast<float>(count_ - 1));
    const int low = std::clamp(static_cast<int>(std::floor(value)), 0, count_ - 1);
    const float t = value - static_cast<float>(low);
    const float line = style.reduced_motion ? 0.0f : size * 1.15f;
    char number[16];
    list.push_clip({start - 6.0f, cy - size * 0.72f, number_w + 12.0f, size * 1.44f});
    std::snprintf(number, sizeof(number), "%d", low + 1);
    paint.label(number, split, baseline - t * line, size, strong.with_alpha(1.0f - t),
                gfx::Align::right);
    if (t > 0.01f && low + 1 < count_)
    {
        std::snprintf(number, sizeof(number), "%d", low + 2);
        paint.label(number, split, baseline + (1.0f - t) * line, size, strong.with_alpha(t),
                    gfx::Align::right);
    }
    list.pop_clip();
    paint.label(of_text, split, baseline, size, muted);

    const bool timed = style.show_progress && (style.auto_advance > 0.0f || progress_ > 0.0f);
    if (timed)
    {
        const Rect track{start, cy + size * 0.78f, total, 3.0f};
        list.rounded_rect(track, 0.0f, muted.with_alpha(0.35f));
        list.rounded_rect({track.x, track.y, track.w * progress_, track.h}, 0.0f, active);
    }
}

void PageDots::draw(Canvas &canvas) const
{
    if (count_ <= 0)
        return;
    const Theme &theme = style.theme;
    Painter paint(canvas.list, canvas.fonts, theme, canvas.glass);
    const Color strong = style.on_page ? paint.page_text() : theme.text;
    const Color muted = style.on_page ? paint.page_text_muted() : theme.text_muted;
    // Stroke-drawn languages keep their primary for strokes; their marks are
    // read by the text colour, like everything else filled there.
    const Color active = theme.style == SurfaceStyle::sketch ? strong : theme.primary;
    if (style.kind == PageDotsKind::numbers)
        draw_numbers(canvas, paint, strong, muted, active);
    else
        draw_marks(canvas, paint, muted.with_alpha(0.45f), active);
}

} // namespace hui::ui
