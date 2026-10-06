// ps5-homebrew-ui - Components: ProgressBar, ProgressRing, Spinner and Meter.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/progress.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

constexpr float kTau = 6.2831853f;
const Color kWhite{1.0f, 1.0f, 1.0f, 1.0f};

// A value should arrive a little slower than a focus ring does, and a level
// that overshoots its target would be a lie: both are bounded here.
void ease(tween::Bounce &value, float dt, const ComponentStyle &style)
{
    value.update(dt, std::min(style.omega() * 0.7f, 60.0f), std::max(style.damping(), 0.85f));
}

float fract(float value)
{
    return value - std::floor(value);
}

Color lighten(Color c, float amount)
{
    return gfx::mix(c, Color{1.0f, 1.0f, 1.0f, c.a}, amount);
}

Color darken(Color c, float amount)
{
    return gfx::mix(c, Color{0.0f, 0.0f, 0.0f, c.a}, amount);
}

// Themes whose wells have a thick frame: the fill sits inside it.
bool framed(const Theme &theme)
{
    return theme.style == SurfaceStyle::hard || theme.style == SurfaceStyle::bevel ||
           theme.style == SurfaceStyle::pixel;
}

// Square languages get square dots and flat arc ends.
bool rounded(const Theme &theme)
{
    return theme.corner == Corner::round && theme.radius >= 2.0f;
}

float bar_radius(const Theme &theme, RadiusSource source, float custom, float height)
{
    // A bevelled well is always square; a round fill would not sit in it.
    if (theme.style == SurfaceStyle::bevel)
        return 0.0f;
    switch (source)
    {
    case RadiusSource::pill:
        return height * 0.5f;
    case RadiusSource::square:
        return 0.0f;
    case RadiusSource::custom:
        return std::min(custom, height * 0.5f);
    case RadiusSource::theme:
        break;
    }
    return std::min(theme.radius, height * 0.5f);
}

void draw_track(Painter &paint, TrackStyle track, const Rect &r, float radius)
{
    if (track == TrackStyle::well)
        paint.well(r, radius, paint.theme().surface_high);
    else if (track == TrackStyle::flat)
        paint.fill(r, radius, paint.theme().text_muted.with_alpha(0.2f));
}

float fill_inset(const Theme &theme, TrackStyle track)
{
    return track == TrackStyle::well && framed(theme) ? theme.border : 0.0f;
}

// The filled part of a bar, in the theme's material.
void draw_fill(Canvas &canvas, Painter &paint, const Rect &r, float radius, Color color)
{
    if (r.w < 1.0f || r.h < 1.0f)
        return;
    const Theme &theme = paint.theme();
    // A short fill keeps round ends by shrinking its radius with its width.
    const float corner = std::min(radius, std::min(r.w, r.h) * 0.5f);
    if (theme.style == SurfaceStyle::gloss)
    {
        canvas.list.gradient_rect(r, corner, lighten(color, 0.25f), darken(color, 0.15f));
        canvas.list.gradient_rect({r.x + 1.0f, r.y + 1.0f, r.w - 2.0f, r.h * 0.45f}, corner,
                                  theme.light.with_alpha(color.a),
                                  theme.light.with_alpha(0.1f * color.a));
        return;
    }
    if (theme.style == SurfaceStyle::glow)
        canvas.list.glow(r, corner, 12.0f, color.with_alpha(0.5f));
    paint.fill(r, corner, color);
}

// A dot in the theme's shape.
void draw_dot(Canvas &canvas, const Theme &theme, float cx, float cy, float radius, Color color)
{
    if (rounded(theme))
        canvas.list.circle(cx, cy, radius, color);
    else
        canvas.list.rounded_rect({cx - radius, cy - radius, radius * 2.0f, radius * 2.0f}, 0.0f,
                                 color);
}

// The track of a ring. A theme with borders outlines its wells: for an arc
// that is a slightly larger arc in the outline colour underneath.
void draw_ring_track(Canvas &canvas, const Theme &theme, TrackStyle track, float cx, float cy,
                     float outer, float thickness, float start, float sweep, bool round)
{
    if (track == TrackStyle::none)
        return;
    const bool full = sweep >= kTau - 0.01f;
    const auto stroke = [&](float radius, float width, Color color)
    {
        if (full)
            canvas.list.ring(cx, cy, radius, width, color);
        else
            canvas.list.arc(cx, cy, radius, width, start, sweep, color, round);
    };
    if (track == TrackStyle::flat)
    {
        stroke(outer, thickness, theme.text_muted.with_alpha(0.2f));
        return;
    }
    const float line = std::min(theme.border, 3.0f);
    if (line > 0.0f && theme.outline.a > 0.0f)
        stroke(outer + line, thickness + 2.0f * line, theme.outline);
    stroke(outer, thickness, theme.surface_high);
}

float snap_or_set(tween::Bounce &spring, float value, bool snap)
{
    value = tween::clamp01(value);
    if (snap)
        spring.snap(value);
    else
        spring.target = value;
    return value;
}

} // namespace

Color status_color(const Theme &theme, Status status)
{
    switch (status)
    {
    case Status::primary:
        return theme.primary;
    case Status::accent:
        return theme.accent;
    case Status::success:
        return theme.success;
    case Status::warning:
        return theme.warning;
    case Status::danger:
        return theme.danger;
    case Status::neutral:
        break;
    }
    return theme.text_muted;
}

Color status_ink(const Theme &theme, Status status)
{
    if (status == Status::primary)
        return theme.on_primary;
    return Painter::on(status_color(theme, status));
}

Color solid_surface(const Theme &theme)
{
    const Color mixed = gfx::mix(theme.page, theme.surface, tween::clamp01(theme.surface.a));
    return {mixed.r, mixed.g, mixed.b, 1.0f};
}

void draw_sweep(gfx::DrawList &list, float cx, float half_width, float lo, float hi, float y,
                float height, Color color)
{
    if (half_width <= 0.0f)
        return;
    // Each half is a gradient; where the track cuts it, the alpha at the cut
    // is the one the uncut gradient would have had there.
    const auto piece = [&](float x0, float x1, float a0, float a1)
    {
        const float from = std::max(x0, lo);
        const float to = std::min(x1, hi);
        if (to - from < 0.5f)
            return;
        const float t0 = (from - x0) / (x1 - x0);
        const float t1 = (to - x0) / (x1 - x0);
        list.gradient_rect_h({from, y, to - from, height}, 0.0f,
                             color.with_alpha(tween::lerp(a0, a1, t0)),
                             color.with_alpha(tween::lerp(a0, a1, t1)));
    };
    piece(cx - half_width, cx, 0.0f, 1.0f);
    piece(cx, cx + half_width, 1.0f, 0.0f);
}

// ---- ProgressBar -----------------------------------------------------------

void ProgressBar::set_value(float value, bool snap)
{
    const float before = value_.target;
    const float now = snap_or_set(value_, value, snap);
    if (!snap && now >= 1.0f && before < 1.0f)
        flash_.trigger();
}

void ProgressBar::set_buffer(float value, bool snap)
{
    snap_or_set(buffer_, value, snap);
}

float ProgressBar::shown() const
{
    return tween::clamp01(value_.value);
}

void ProgressBar::update(float dt)
{
    ease(value_, dt, style);
    ease(buffer_, dt, style);
    flash_.update(dt, 2.6f);
    // Wrapped where every period divides it, so the travel never jumps.
    phase_ = std::fmod(phase_ + dt, 3600.0f);
}

void ProgressBar::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const float value = shown();
    const bool busy = style.mode == ProgressMode::indeterminate;
    const Color ink = style.color.a > 0.0f ? style.color : status_color(theme, style.status);
    const Color on_ink =
        style.color.a > 0.0f ? Painter::on(style.color) : status_ink(theme, style.status);

    char number[8] = "";
    if (style.percent && !busy)
        std::snprintf(number, sizeof(number), "%d%%", static_cast<int>(std::lround(value * 100)));

    const float size = style.text_size;
    Rect bar{bounds_.x, bounds_.cy() - style.height * 0.5f, bounds_.w, style.height};
    if (style.placement == LabelPlacement::above)
    {
        bar.y = bounds_.y + bounds_.h - style.height;
        const float baseline = bar.y - style.text_gap;
        float room = bounds_.w;
        if (number[0] != '\0')
            room -= paint.label(number, bounds_.x + bounds_.w, baseline, size, theme.text,
                                gfx::Align::right) +
                    16.0f;
        if (!label.empty())
            paint.bounded_label(label, bounds_.x, baseline, size, theme.text_muted, std::max(room, 40.0f));
    }
    else if (style.placement == LabelPlacement::right)
    {
        // The room is measured with the widest number, so the bar does not
        // change length as the digits do.
        const float baseline = bounds_.cy() + size * 0.35f;
        float right = bounds_.x + bounds_.w;
        float reserve = 0.0f;
        if (number[0] != '\0')
        {
            paint.label(number, right, baseline, size, theme.text, gfx::Align::right);
            reserve = paint.label_width("100%", size);
            right -= reserve + 12.0f;
        }
        if (!label.empty())
        {
            const float width =
                paint.label(label, right, baseline, size, theme.text_muted, gfx::Align::right);
            reserve += width + (reserve > 0.0f ? 12.0f : 0.0f);
        }
        if (reserve > 0.0f)
            bar.w = std::max(bounds_.w - reserve - style.text_gap, 8.0f);
    }

    const float radius = bar_radius(theme, style.radius_source, style.radius, bar.h);
    const float inset = fill_inset(theme, style.track);
    const float inner_radius = std::max(radius - inset, 0.0f);
    if (style.finish_flash && flash_.value > 0.01f)
        list.glow(bar, radius, 16.0f, ink.with_alpha(0.55f * flash_.value));

    float split = bar.x; // where the fill ends, for the text inside the bar
    if (style.mode == ProgressMode::segmented)
    {
        const int count = std::max(style.segments, 1);
        const float width =
            (bar.w - style.segment_gap * static_cast<float>(count - 1)) / static_cast<float>(count);
        for (int i = 0; i < count; ++i)
        {
            const Rect cell{bar.x + static_cast<float>(i) * (width + style.segment_gap), bar.y,
                            width, bar.h};
            draw_track(paint, style.track, cell, radius);
            const float part =
                tween::clamp01(value * static_cast<float>(count) - static_cast<float>(i));
            Rect filled = cell.inset(inset);
            filled.w *= part;
            draw_fill(canvas, paint, filled, inner_radius, ink);
            if (part > 0.0f)
                split = filled.x + filled.w;
        }
    }
    else
    {
        draw_track(paint, style.track, bar, radius);
        const Rect in = bar.inset(inset);
        if (busy && style.reduced_motion)
        {
            // No travel: the whole track breathes instead.
            draw_fill(canvas, paint, in, inner_radius,
                      ink.with_alpha(0.3f + 0.45f * breathe(phase_)));
        }
        else if (busy)
        {
            const float period = std::max(style.travel_period, 0.1f);
            const float half = style.travel_width * 0.5f;
            // Half eased, half linear: a fully eased segment would spend
            // most of its time out of sight at the two ends.
            const float t = fract(phase_ / period);
            const float centre =
                tween::lerp(-half, 1.0f + half, 0.5f * (t + tween::cubic_in_out(t)));
            const float from = tween::clamp01(centre - half);
            const float to = tween::clamp01(centre + half);
            draw_fill(canvas, paint, {in.x + in.w * from, in.y, in.w * (to - from), in.h},
                      inner_radius, ink);
        }
        else
        {
            if (style.mode == ProgressMode::buffered)
            {
                const Rect loaded{in.x, in.y, in.w * tween::clamp01(buffer_.value), in.h};
                if (loaded.w >= 1.0f)
                    paint.fill(loaded, std::min(inner_radius, loaded.w * 0.5f),
                               ink.with_alpha(style.buffer_alpha));
            }
            const Rect filled{in.x, in.y, in.w * value, in.h};
            draw_fill(canvas, paint, filled, inner_radius, ink);
            split = filled.x + filled.w;
            if (style.sheen && !style.reduced_motion && filled.w > 24.0f)
            {
                const float half = std::max(36.0f, bar.w * 0.12f);
                const float t = fract(phase_ / std::max(style.sheen_period, 0.1f));
                const float centre = tween::lerp(filled.x - half, filled.x + filled.w + half, t);
                // Stay clear of the round ends: a band has square corners.
                const float edge = std::min(inner_radius, filled.w * 0.5f) * 0.6f;
                draw_sweep(list, centre, half, filled.x + edge, filled.x + filled.w - edge,
                           filled.y, filled.h, kWhite.with_alpha(0.34f));
            }
        }
    }

    if (style.placement == LabelPlacement::inside && bar.h >= 18.0f)
    {
        std::string text = label;
        if (number[0] != '\0')
            text += (text.empty() ? "" : "  ") + std::string(number);
        const float fitted = std::min(size, bar.h - 6.0f);
        const float baseline = bar.cy() + fitted * 0.35f;
        split = std::clamp(split, bar.x, bar.x + bar.w);
        // The same text twice, each copy cut to its side of the fill's edge,
        // so every letter reads on what is under it.
        if (split - bar.x > 0.5f)
        {
            list.push_clip({bar.x, bar.y - 4.0f, split - bar.x, bar.h + 8.0f});
            paint.label(text, bar.cx(), baseline, fitted, on_ink, gfx::Align::center);
            list.pop_clip();
        }
        if (bar.x + bar.w - split > 0.5f)
        {
            list.push_clip({split, bar.y - 4.0f, bar.x + bar.w - split, bar.h + 8.0f});
            paint.label(text, bar.cx(), baseline, fitted, theme.text, gfx::Align::center);
            list.pop_clip();
        }
    }
}

// ---- ProgressRing ----------------------------------------------------------

void ProgressRing::set_value(float value, bool snap)
{
    snap_or_set(value_, value, snap);
}

void ProgressRing::set_buffer(float value, bool snap)
{
    snap_or_set(buffer_, value, snap);
}

float ProgressRing::shown() const
{
    return tween::clamp01(value_.value);
}

void ProgressRing::update(float dt)
{
    ease(value_, dt, style);
    ease(buffer_, dt, style);
    phase_ = std::fmod(phase_ + dt, 3600.0f);
}

void ProgressRing::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const float diameter = std::min(bounds_.w, bounds_.h);
    if (diameter < 8.0f)
        return;
    const float cx = bounds_.cx();
    const float cy = bounds_.cy();
    const float outer = diameter * 0.5f;
    const float thickness = std::min(style.thickness, outer * 0.6f);
    const float total = std::clamp(style.sweep, 0.1f, kTau);
    const bool full = total >= kTau - 0.01f;
    const bool round =
        style.caps == RingCaps::round || (style.caps == RingCaps::theme && rounded(theme));
    const float value = shown();
    const Color ink = style.color.a > 0.0f ? style.color : status_color(theme, style.status);
    const bool lit = theme.style == SurfaceStyle::glow;
    const auto fill_arc = [&](float start, float sweep, Color color, bool caps)
    {
        if (sweep <= 0.002f)
            return;
        if (lit)
            list.arc(cx, cy, outer + 5.0f, thickness + 10.0f, start, sweep, color.with_alpha(0.2f),
                     caps);
        list.arc(cx, cy, outer, thickness, start, sweep, color, caps);
    };

    if (style.mode == ProgressMode::segmented)
    {
        const int count = std::max(style.segments, 1);
        const float gap = style.segment_gap / std::max(outer - thickness * 0.5f, 1.0f);
        const float each = (total - gap * static_cast<float>(full ? count : count - 1)) /
                           static_cast<float>(count);
        for (int i = 0; i < count; ++i)
        {
            const float start = style.start_angle + (full ? gap * 0.5f : 0.0f) +
                                static_cast<float>(i) * (each + gap);
            // Short segments keep flat ends: round caps would close the gaps.
            draw_ring_track(canvas, theme, style.track, cx, cy, outer, thickness, start, each,
                            false);
            const float part =
                tween::clamp01(value * static_cast<float>(count) - static_cast<float>(i));
            fill_arc(start, each * part, ink, false);
        }
    }
    else
    {
        draw_ring_track(canvas, theme, style.track, cx, cy, outer, thickness, style.start_angle,
                        total, round);
        if (style.mode == ProgressMode::indeterminate)
        {
            if (style.reduced_motion)
            {
                fill_arc(style.start_angle, total * 0.3f,
                         ink.with_alpha(0.35f + 0.65f * breathe(phase_)), round);
            }
            else
            {
                // The head runs ahead, then the tail catches up: the arc
                // stretches and shrinks while the whole thing turns.
                const float turns = phase_ / std::max(style.spin_period, 0.1f);
                const float cycle = turns * 0.7f;
                const float k = fract(cycle);
                const float head = tween::cubic_in_out(std::min(1.0f, k * 2.0f));
                const float tail = tween::cubic_in_out(std::max(0.0f, k * 2.0f - 1.0f));
                const float base = fract(turns + 0.72f * std::floor(cycle));
                fill_arc(style.start_angle + (base + 0.72f * tail) * kTau,
                         (0.08f + 0.72f * (head - tail)) * kTau, ink, round);
            }
        }
        else
        {
            if (style.mode == ProgressMode::buffered)
                fill_arc(style.start_angle, total * tween::clamp01(buffer_.value),
                         ink.with_alpha(style.buffer_alpha), round);
            const float sweep = total * value;
            if (style.two_tone)
            {
                // Two arcs read as a gradient: the second colour over the
                // whole value, then the first over its trailing half, so its
                // round end laps onto the second.
                fill_arc(style.start_angle, sweep,
                         style.color_to.a > 0.0f ? style.color_to
                                                 : status_color(theme, style.status_to),
                         round);
                fill_arc(style.start_angle, sweep * 0.5f, ink, round);
            }
            else
            {
                fill_arc(style.start_angle, sweep, ink, round);
            }
        }
    }

    if (style.ticks > 0)
    {
        const int marks = full ? style.ticks : style.ticks + 1;
        const float far = outer - thickness - 5.0f;
        const float near = far - std::max(5.0f, diameter * 0.05f);
        for (int i = 0; i < marks; ++i)
        {
            const float along = static_cast<float>(i) / static_cast<float>(style.ticks);
            const float angle = style.start_angle + total * along;
            const float sx = std::sin(angle);
            const float sy = -std::cos(angle);
            const bool passed = along <= value + 0.001f && value > 0.0f;
            list.line(cx + sx * near, cy + sy * near, cx + sx * far, cy + sy * far, 2.0f,
                      passed ? ink : theme.text_muted.with_alpha(0.45f));
        }
    }

    const float hole = outer - thickness - (style.ticks > 0 ? 12.0f : 0.0f);
    if (center)
    {
        const float side = std::max(hole * 1.414f - 8.0f, 0.0f);
        center(canvas, {cx - side * 0.5f, cy - side * 0.5f, side, side}, value);
        return;
    }
    const bool numbered = style.percent && style.mode != ProgressMode::indeterminate;
    if (!numbered && label.empty())
        return;
    float size = style.text_size > 0.0f ? style.text_size : diameter * 0.25f;
    const float small =
        style.label_size > 0.0f ? style.label_size : std::max(diameter * 0.13f, 14.0f);
    char number[8] = "";
    if (numbered)
    {
        std::snprintf(number, sizeof(number), "%d%%", static_cast<int>(std::lround(value * 100)));
        // Measured with the widest text so the size does not change at 100.
        const float widest = paint.label_width("100%", size);
        const float room = hole * 1.7f;
        if (widest > room)
            size *= room / widest;
    }
    // The label's line is below the centre, where the hole is narrower. A
    // label that does not fit there whole is left out: three dots under a
    // number say nothing.
    const float drop = size * 0.12f + small * 1.35f;
    const float chord = 2.0f * std::sqrt(std::max(hole * hole - drop * drop, 0.0f)) - 6.0f;
    if (numbered && !label.empty() && paint.label_width(label, small) <= chord)
    {
        paint.label(number, cx, cy + size * 0.12f, size, theme.text, gfx::Align::center);
        paint.label(label, cx, cy + size * 0.12f + small * 1.35f, small, theme.text_muted,
                    gfx::Align::center);
    }
    else if (numbered)
    {
        paint.label(number, cx, cy + size * 0.35f, size, theme.text, gfx::Align::center);
    }
    else
    {
        paint.bounded_label(label, cx, cy + small * 0.35f, small, theme.text_muted, hole * 1.7f, gfx::Align::center);
    }
}

// ---- Spinner ---------------------------------------------------------------

void Spinner::set_spinning(bool spinning, bool snap)
{
    spinning_ = spinning;
    shown_.target = spinning ? 1.0f : 0.0f;
    if (snap)
        shown_.snap(shown_.target);
}

void Spinner::update(float dt)
{
    shown_.update(dt, std::min(style.omega(), 30.0f));
    phase_ = std::fmod(phase_ + dt * style.speed, 3600.0f);
}

void Spinner::draw(Canvas &canvas) const
{
    if (shown_.value <= 0.01f)
        return;
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    const float size = style.size > 0.0f ? style.size : std::min(bounds_.w, bounds_.h);
    if (size < 4.0f)
        return;
    const float cx = bounds_.cx();
    const float cy = bounds_.cy();
    const Color ink = style.color.a > 0.0f ? style.color : status_color(theme, style.status);
    const bool still = style.reduced_motion;
    // Standing still, it says "working" with a slow pulse instead.
    list.push_opacity(
        tween::clamp01(shown_.value) *
        (still ? 0.4f + 0.6f * breathe(phase_ / std::max(style.speed, 0.01f)) : 1.0f));

    switch (style.kind)
    {
    case SpinnerKind::arc:
    {
        const float thickness =
            style.thickness > 0.0f ? style.thickness : std::max(size * 0.12f, 3.0f);
        const float outer = size * 0.5f;
        const bool round = rounded(theme);
        if (style.track)
            list.ring(cx, cy, outer, thickness, ink.with_alpha(0.18f));
        if (still)
        {
            list.arc(cx, cy, outer, thickness, 0.0f, kTau * 0.3f, ink, round);
            break;
        }
        const float turns = phase_ * 0.9f;
        const float cycle = phase_ * 0.65f;
        const float k = fract(cycle);
        const float head = tween::cubic_in_out(std::min(1.0f, k * 2.0f));
        const float tail = tween::cubic_in_out(std::max(0.0f, k * 2.0f - 1.0f));
        const float base = fract(turns + 0.72f * std::floor(cycle));
        list.arc(cx, cy, outer, thickness, (base + 0.72f * tail) * kTau,
                 (0.06f + 0.72f * (head - tail)) * kTau, ink, round);
        break;
    }
    case SpinnerKind::dots:
    {
        const float radius = size * 0.13f;
        const float pitch = size * 0.36f;
        for (int i = 0; i < 3; ++i)
        {
            // Each dot is a ball that jumps a beat after its neighbour.
            const float beat = fract(phase_ * 1.25f - static_cast<float>(i) * 0.16f);
            const float jump = still ? 0.0f : std::max(0.0f, std::sin(beat * kTau));
            draw_dot(canvas, theme, cx + static_cast<float>(i - 1) * pitch,
                     cy + size * 0.16f - jump * size * 0.32f, radius,
                     ink.with_alpha(0.55f + 0.45f * jump));
        }
        break;
    }
    case SpinnerKind::bars:
    {
        constexpr int kBars = 4;
        constexpr float kRates[kBars] = {5.1f, 7.3f, 4.2f, 6.4f};
        constexpr float kRest[kBars] = {0.55f, 0.9f, 0.4f, 0.7f};
        const float width = size * 0.16f;
        const float pitch = size * 0.26f;
        const float floor_y = cy + size * 0.42f;
        for (int i = 0; i < kBars; ++i)
        {
            const float wave =
                0.5f + 0.5f * std::sin(phase_ * kRates[i] + static_cast<float>(i) * 1.9f);
            const float height = size * 0.84f * (still ? kRest[i] : 0.22f + 0.78f * wave);
            const Rect bar{cx + (static_cast<float>(i) - 1.5f) * pitch - width * 0.5f,
                           floor_y - height, width, height};
            list.rounded_rect(bar, rounded(theme) ? std::min(theme.radius, width * 0.5f) : 0.0f,
                              ink);
        }
        break;
    }
    case SpinnerKind::orbit:
    {
        constexpr int kDots = 8;
        const float orbit = size * 0.38f;
        for (int i = 0; i < kDots; ++i)
        {
            const float along = static_cast<float>(i) / static_cast<float>(kDots);
            // How long ago the lit position passed this dot, 0..1 of a turn.
            const float behind = still ? 0.5f : fract(phase_ * 1.1f - along);
            const float glow = (1.0f - behind) * (1.0f - behind);
            const float angle = along * kTau;
            draw_dot(canvas, theme, cx + std::sin(angle) * orbit, cy - std::cos(angle) * orbit,
                     size * (0.06f + 0.04f * glow), ink.with_alpha(0.16f + 0.84f * glow));
        }
        break;
    }
    }
    list.pop_opacity();
}

// ---- Meter -----------------------------------------------------------------

Status Meter::zone(float level) const
{
    if (level >= style.danger_at)
        return Status::danger;
    if (level >= style.warning_at)
        return Status::warning;
    return Status::success;
}

void Meter::set_value(float value, bool snap)
{
    value = snap_or_set(value_, value, snap);
    if (snap)
    {
        peak_ = value;
        peak_age_ = 0.0f;
        ink_.snap(status_color(style.theme, zone(value)));
        ink_set_ = true;
    }
}

float Meter::shown() const
{
    return tween::clamp01(value_.value);
}

void Meter::update(float dt)
{
    // A meter follows a live signal: it moves faster than a progress bar.
    value_.update(dt, std::min(style.omega() * 1.2f, 60.0f), std::max(style.damping(), 0.9f));
    const float level = shown();
    if (level >= peak_)
    {
        peak_ = level;
        peak_age_ = 0.0f;
    }
    else
    {
        peak_age_ += dt;
        if (peak_age_ > style.peak_seconds)
            peak_ = std::max(level, peak_ - style.peak_fall * dt);
    }
    const Color target = status_color(style.theme, zone(level));
    if (!ink_set_)
        ink_.snap(target);
    ink_set_ = true;
    ink_.target(target);
    ink_.update(dt, style.reduced_motion ? 60.0f : 14.0f);
}

void Meter::draw(Canvas &canvas) const
{
    Painter paint(canvas.list, canvas.fonts, style.theme, canvas.glass);
    if (style.shape == MeterShape::radial)
        draw_radial(canvas, paint);
    else
        draw_linear(canvas, paint);
}

void Meter::draw_linear(Canvas &canvas, Painter &paint) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    const float level = shown();
    const Color ink = ink_set_ ? ink_.value() : status_color(theme, zone(level));
    const float strip = style.zone_strip ? 7.0f : 0.0f;

    // From the bottom of the bounds up: the zone strip, the bar, the text.
    Rect bar{bounds_.x, bounds_.y + bounds_.h - strip - style.height, bounds_.w, style.height};
    const bool text = !label.empty() || style.show_value;
    if (!text)
        bar.y = bounds_.cy() - (style.height + strip) * 0.5f;
    else
    {
        const float baseline = bar.y - style.text_gap;
        float room = bounds_.w;
        if (style.show_value)
        {
            char number[24];
            std::snprintf(number, sizeof(number), "%d%s",
                          static_cast<int>(std::lround(level * style.value_scale)),
                          style.unit.c_str());
            room -= paint.label(number, bounds_.x + bounds_.w, baseline, style.text_size,
                                theme.text, gfx::Align::right) +
                    16.0f;
        }
        if (!label.empty())
            paint.bounded_label(label, bounds_.x, baseline, style.text_size, theme.text_muted, std::max(room, 40.0f));
    }

    const float radius = bar_radius(theme, style.radius_source, style.radius, bar.h);
    const float inset = fill_inset(theme, style.track);
    if (style.segments > 0)
    {
        // Lamps: each one has the colour of the zone it stands in.
        const int count = style.segments;
        const float width =
            (bar.w - style.segment_gap * static_cast<float>(count - 1)) / static_cast<float>(count);
        const int peak_lamp =
            style.peak_hold ? static_cast<int>(std::ceil(peak_ * static_cast<float>(count))) - 1
                            : -1;
        for (int i = 0; i < count; ++i)
        {
            const Rect lamp{bar.x + static_cast<float>(i) * (width + style.segment_gap), bar.y,
                            width, bar.h};
            const float at = (static_cast<float>(i) + 0.5f) / static_cast<float>(count);
            const Color own = status_color(theme, zone(at));
            const float corner = std::min(radius, std::min(width, bar.h) * 0.3f);
            const bool on = at <= level + 0.5f / static_cast<float>(count) && level > 0.001f;
            if (on && theme.style == SurfaceStyle::glow)
                list.glow(lamp, corner, 8.0f, own.with_alpha(0.45f));
            paint.fill(lamp, corner,
                       on ? own : (i == peak_lamp ? own.with_alpha(0.6f) : own.with_alpha(0.16f)));
        }
    }
    else
    {
        draw_track(paint, style.track, bar, radius);
        const Rect in = bar.inset(inset);
        draw_fill(canvas, paint, {in.x, in.y, in.w * level, in.h}, std::max(radius - inset, 0.0f),
                  ink);
        if (style.peak_hold && peak_ > 0.01f)
        {
            // The marker fades once the level has caught up with it.
            const float apart = tween::clamp01((peak_ - level) * 30.0f);
            const float x = in.x + in.w * peak_;
            list.rounded_rect({std::min(x, in.x + in.w - 3.0f), bar.y - 3.0f, 3.0f, bar.h + 6.0f},
                              rounded(theme) ? 1.5f : 0.0f,
                              status_color(theme, zone(peak_)).with_alpha(apart));
        }
    }

    if (style.zone_strip)
    {
        const float y = bar.y + bar.h + 4.0f;
        const float warn = bar.x + bar.w * tween::clamp01(style.warning_at);
        const float stop = bar.x + bar.w * tween::clamp01(style.danger_at);
        const float end = bar.x + bar.w;
        list.rounded_rect({bar.x, y, std::max(warn - bar.x - 2.0f, 0.0f), 3.0f}, 0.0f,
                          theme.success.with_alpha(0.75f));
        list.rounded_rect({warn, y, std::max(stop - warn - 2.0f, 0.0f), 3.0f}, 0.0f,
                          theme.warning.with_alpha(0.75f));
        list.rounded_rect({stop, y, std::max(end - stop, 0.0f), 3.0f}, 0.0f,
                          theme.danger.with_alpha(0.75f));
    }
}

void Meter::draw_radial(Canvas &canvas, Painter &paint) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    const float diameter = std::min(bounds_.w, bounds_.h);
    if (diameter < 16.0f)
        return;
    const float cx = bounds_.cx();
    const float cy = bounds_.cy();
    const float level = shown();
    const Color ink = ink_set_ ? ink_.value() : status_color(theme, zone(level));
    // Three quarters of a turn, open at the bottom, like a dial.
    constexpr float kStart = -0.75f * 3.14159265f;
    constexpr float kSweep = 1.5f * 3.14159265f;
    const bool round = rounded(theme);
    float outer = diameter * 0.5f;

    if (style.zone_strip)
    {
        const float warn = kSweep * tween::clamp01(style.warning_at);
        const float stop = kSweep * tween::clamp01(style.danger_at);
        const float gap = 2.0f / outer;
        list.arc(cx, cy, outer, 3.0f, kStart, std::max(warn - gap, 0.0f),
                 theme.success.with_alpha(0.75f), false);
        list.arc(cx, cy, outer, 3.0f, kStart + warn, std::max(stop - warn - gap, 0.0f),
                 theme.warning.with_alpha(0.75f), false);
        list.arc(cx, cy, outer, 3.0f, kStart + stop, kSweep - stop, theme.danger.with_alpha(0.75f),
                 false);
        outer -= 8.0f;
    }
    const float thickness = std::min(style.thickness, outer * 0.6f);
    draw_ring_track(canvas, theme, style.track, cx, cy, outer, thickness, kStart, kSweep, round);
    if (level > 0.002f)
    {
        if (theme.style == SurfaceStyle::glow)
            list.arc(cx, cy, outer + 5.0f, thickness + 10.0f, kStart, kSweep * level,
                     ink.with_alpha(0.2f), round);
        list.arc(cx, cy, outer, thickness, kStart, kSweep * level, ink, round);
    }
    if (style.peak_hold && peak_ > 0.01f)
    {
        const float apart = tween::clamp01((peak_ - level) * 30.0f);
        const float width = 4.0f / std::max(outer, 1.0f);
        list.arc(cx, cy, outer + 2.0f, thickness + 4.0f,
                 kStart + std::min(kSweep * peak_, kSweep - width), width,
                 status_color(theme, zone(peak_)).with_alpha(apart), false);
    }

    const float hole = outer - thickness;
    if (style.show_value)
    {
        char number[24];
        std::snprintf(number, sizeof(number), "%d",
                      static_cast<int>(std::lround(level * style.value_scale)));
        float size = style.value_size > 0.0f ? style.value_size : diameter * 0.26f;
        // Three digits are the widest this shows at the default scale.
        const float widest = paint.label_width("000", size);
        if (widest > hole * 1.5f)
            size *= hole * 1.5f / widest;
        paint.label(number, cx, cy + size * 0.3f, size, theme.text, gfx::Align::center);
        if (!style.unit.empty())
            paint.label(style.unit, cx, cy + size * 0.3f + style.text_size * 1.2f,
                        style.text_size * 0.9f, theme.text_muted, gfx::Align::center);
    }
    // The label sits in the opening at the bottom of the dial.
    if (!label.empty())
        paint.bounded_label(label, cx, cy + diameter * 0.5f - 2.0f, style.text_size, theme.text_muted, diameter * 0.9f, gfx::Align::center);
}

} // namespace hui::ui
