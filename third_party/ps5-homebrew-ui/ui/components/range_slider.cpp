// ps5-homebrew-ui - Components: Slider and RangeSlider.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/range_slider.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

constexpr float kPi = 3.14159265f;

// Where the parts of a slider are, top to bottom, inside its bounds.
struct Parts
{
    float label_baseline = 0.0f; // 0 when there is no label line
    float bubble_tip = 0.0f;     // where a bubble's pointer ends
    Rect track;                  // what Painter::slider is given
    float ticks_top = 0.0f;
    float ends_baseline = 0.0f;
    float height = 0.0f;
};

bool has_label_line(const SliderStyle &look, const std::string &label)
{
    return !label.empty() || (look.bubble == BubbleMode::never && look.value_in_label);
}

Parts measure(const SliderStyle &look, const Rect &bounds, const std::string &label)
{
    Parts out;
    float y = bounds.y;
    if (has_label_line(look, label))
    {
        out.label_baseline = y + look.label_size * 0.82f;
        y += look.label_size + look.label_gap;
    }
    if (look.bubble != BubbleMode::never)
        y += look.bubble_height + look.pointer_size + look.bubble_gap;
    out.bubble_tip = y - look.bubble_gap;
    out.track = {bounds.x, y, bounds.w, look.track_height};
    y += look.track_height;
    if (look.ticks > 1)
    {
        out.ticks_top = y + 2.0f;
        y += 2.0f + look.tick_height;
    }
    if (look.end_labels)
    {
        // Far enough down that a focus ring around a thumb at either end
        // does not cross the words.
        out.ends_baseline = y + 14.0f + look.end_label_size * 0.82f;
        y += 14.0f + look.end_label_size;
    }
    out.height = y - bounds.y;
    return out;
}

// The same geometry Painter::slider uses: a thumb no larger than 34, and a
// bar that stops half a thumb short of both ends.
float thumb_size(const Rect &track)
{
    return std::min(track.h, 34.0f);
}

float thumb_x(const Rect &track, float fraction)
{
    const float size = thumb_size(track);
    return track.x + size * 0.5f + (track.w - size) * tween::clamp01(fraction);
}

std::string number_text(float value, float step, float span, const std::string &unit)
{
    int decimals = 0;
    if (step <= 0.0f)
        decimals = span <= 10.0f ? 2 : 0;
    else if (step < 0.1f)
        decimals = 2;
    else if (step < 1.0f || std::fabs(step - std::round(step)) > 1e-4f)
        decimals = 1;
    char text[32];
    std::snprintf(text, sizeof(text), "%.*f", decimals, static_cast<double>(value));
    return std::string(text) + unit;
}

// A small plate over a thumb with the value in it. `lit` is how much it takes
// the primary colour (the thumb that has the focus); `shown` fades and grows
// it in. It stays between `left` and `right`; its pointer stays on the thumb.
void draw_bubble(Canvas &canvas, const SliderStyle &look, float cx, float tip,
                 const std::string &text, float lit, float shown, float left, float right)
{
    if (shown <= 0.01f)
        return;
    const Theme &theme = look.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const float width =
        std::max(paint.label_width(text, look.value_size) + 2.0f * look.bubble_padding,
                 look.bubble_height * 1.5f);
    const float x = std::clamp(cx - width * 0.5f, left, std::max(left, right - width));
    const Rect plate{x, tip - look.pointer_size - look.bubble_height, width, look.bubble_height};
    const float radius = paint.control_radius(plate);
    lit = tween::clamp01(lit);
    const Color fill = gfx::mix(theme.surface_high, theme.primary, lit);
    const Color ink = gfx::mix(theme.text, theme.on_primary, lit);

    list.push_opacity(tween::clamp01(shown));
    list.push_transform(look.reduced_motion ? 1.0f : tween::lerp(0.8f, 1.0f, shown), cx, tip, 0.0f,
                        0.0f);
    paint.fill(plate, radius, fill);
    paint.stroke(plate, radius, std::min(theme.border, 3.0f), theme.outline);
    const float p = look.pointer_size;
    if (p > 0.5f)
    {
        const float reach = std::min(radius, plate.w * 0.5f - p) + p;
        const float px = std::clamp(cx, plate.x + reach, plate.x + plate.w - reach);
        // Pixel and bevel looks have no diagonals: a short stem instead.
        if (theme.corner == Corner::pixel || theme.style == SurfaceStyle::bevel)
            list.rounded_rect({px - 3.0f, plate.y + plate.h - 1.0f, 6.0f, p + 1.0f}, 0.0f, fill);
        else
            list.triangle({px - p, plate.y + plate.h - 1.0f, 2.0f * p, p + 1.0f}, fill, 0.0f, kPi);
    }
    paint.label(text, plate.cx(), plate.cy() + look.value_size * 0.35f, look.value_size, ink,
                gfx::Align::center);
    list.pop_transform();
    list.pop_opacity();
}

void draw_ticks(Canvas &canvas, const SliderStyle &look, const Parts &parts, Color color)
{
    if (look.ticks < 2)
        return;
    const float size = thumb_size(parts.track);
    const float x0 = parts.track.x + size * 0.5f;
    const float run = parts.track.w - size;
    for (int i = 0; i < look.ticks; ++i)
    {
        const float x = x0 + run * static_cast<float>(i) / static_cast<float>(look.ticks - 1);
        canvas.list.rounded_rect({x - 1.0f, parts.ticks_top, 2.0f, look.tick_height}, 0.0f, color);
    }
}

// A thumb as Painter::slider builds it. The painter draws track, fill and
// one thumb in a single call, which a track with two thumbs cannot use, so
// the construction is repeated here theme by theme.
void draw_thumb(gfx::DrawList &list, Painter &paint, const Rect &thumb, float press)
{
    const Theme &theme = paint.theme();
    const float radius = std::min(theme.radius, thumb.w * 0.5f);
    const Color white{1.0f, 1.0f, 1.0f, 1.0f};
    switch (theme.style)
    {
    case SurfaceStyle::outline:
    case SurfaceStyle::glow:
        paint.fill(thumb, radius, theme.accent);
        break;
    case SurfaceStyle::hard:
    case SurfaceStyle::pixel:
    case SurfaceStyle::sketch:
        paint.fill(thumb, radius, white);
        paint.stroke(thumb, radius, std::min(theme.border, 4.0f), theme.outline);
        break;
    case SurfaceStyle::neumorphic:
    case SurfaceStyle::bevel:
    case SurfaceStyle::gloss:
        paint.surface(thumb, radius, theme.surface, theme.outline, 1.0f - press);
        break;
    default:
        list.shadow({thumb.x, thumb.y + 3.0f, thumb.w, thumb.h}, radius, 7.0f,
                    Color{0.0f, 0.0f, 0.0f, 0.28f});
        paint.fill(thumb, radius, white);
        paint.stroke(thumb, radius, 3.0f, theme.accent);
        break;
    }
}

} // namespace

// ---- Slider -------------------------------------------------------------------

void Slider::set_range(float minimum, float maximum, float step)
{
    min_ = std::min(minimum, maximum);
    max_ = std::max(minimum, maximum);
    step_ = std::max(step, 0.0f);
    value_ = std::clamp(value_, min_, max_);
}

void Slider::set_value(float value)
{
    value_ = std::clamp(value, min_, max_);
}

float Slider::fraction() const
{
    return max_ > min_ ? (value_ - min_) / (max_ - min_) : 0.0f;
}

std::string Slider::text_for(float value) const
{
    return format ? format(value) : number_text(value, step_, max_ - min_, unit_);
}

float Slider::preferred_height() const
{
    return measure(style, bounds_, label_).height;
}

Event Slider::handle(const InputFrame &input, Feedback &feedback)
{
    const Parts parts = measure(style, bounds_, label_);
    const float x = thumb_x(parts.track, fraction());
    if (input.nav == Direction::left || input.nav == Direction::right)
    {
        const float direction = input.nav == Direction::right ? 1.0f : -1.0f;
        held_ = input.nav_repeat ? held_ + 1 : 0;
        const float span = max_ - min_;
        float stride =
            step_ > 0.0f ? step_ : span / static_cast<float>(std::max(style.continuous_steps, 1));
        if (style.fast_after > 0 && held_ >= style.fast_after)
            stride *= static_cast<float>(std::max(style.fast_factor, 1));
        float next = value_ + direction * stride;
        // Stay on the step grid, so 0.1 added ten times is exactly 1.
        if (step_ > 0.0f)
            next = min_ + std::round((next - min_) / step_) * step_;
        next = std::clamp(next, min_, max_);
        if (next == value_)
            return refuse(feedback, style, input, refusal_, x);
        value_ = next;
        press_.trigger();
        play_cue(feedback, style, style.sounds.step, x,
                 style.pitch_by_value ? 0.9f + 0.3f * fraction() : 1.0f);
        return Event::changed;
    }
    if (input.is_pressed(Action::back))
    {
        play_cue(feedback, style, style.sounds.cancel, x);
        return Event::cancelled;
    }
    return Event::none;
}

void Slider::update(float dt)
{
    focus_.target = active_ ? 1.0f : 0.0f;
    focus_.update(dt, 18.0f);
    shown_.target = fraction();
    if (!placed_)
        shown_.snap(shown_.target);
    placed_ = true;
    shown_.update(dt, std::max(style.omega(), 16.0f) * 1.3f);
    press_.update(dt, 10.0f);
    refusal_.update(dt, 9.0f);
}

void Slider::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    Painter paint(canvas.list, canvas.fonts, theme, canvas.glass);
    const Color resting = style.on_page ? paint.page_text() : theme.text;
    const Color quiet = style.on_page ? paint.page_text_muted() : theme.text_muted;
    const Parts parts = measure(style, bounds_, label_);
    const float focus = focus_.value;

    if (parts.label_baseline > 0.0f)
    {
        float room = bounds_.w;
        if (style.bubble == BubbleMode::never && style.value_in_label)
            room -= paint.label(text(), bounds_.x + bounds_.w, parts.label_baseline,
                                style.value_size, resting, gfx::Align::right) +
                    16.0f;
        paint.label(fit_label(paint, label_, style.label_size, std::max(room, 40.0f)), bounds_.x,
                    parts.label_baseline, style.label_size, gfx::mix(quiet, resting, focus * 0.5f));
    }

    Rect track = parts.track;
    track.x += style.reduced_motion ? 0.0f : shake(refusal_.value, canvas.time, 6.0f);
    Look look;
    look.focus = focus;
    look.press = press_.value;
    paint.slider(track, shown_.value, look);
    draw_ticks(canvas, style, parts, quiet.with_alpha(0.6f));

    if (parts.ends_baseline > 0.0f)
    {
        paint.body(text_for(min_), bounds_.x, parts.ends_baseline, style.end_label_size, quiet);
        paint.body(text_for(max_), bounds_.x + bounds_.w, parts.ends_baseline, style.end_label_size,
                   quiet, gfx::Align::right);
    }
    if (style.bubble != BubbleMode::never)
        draw_bubble(canvas, style, thumb_x(track, shown_.value), parts.bubble_tip, text(), focus,
                    style.bubble == BubbleMode::always ? 1.0f : focus, bounds_.x,
                    bounds_.x + bounds_.w);
}

// ---- RangeSlider ----------------------------------------------------------------

void RangeSlider::set_range(float minimum, float maximum, float step)
{
    min_ = std::min(minimum, maximum);
    max_ = std::max(minimum, maximum);
    step_ = std::max(step, 0.0f);
    set_values(value_[0], value_[1]);
}

void RangeSlider::set_min_gap(float gap)
{
    gap_ = std::clamp(gap, 0.0f, max_ - min_);
    set_values(value_[0], value_[1]);
}

void RangeSlider::set_values(float low, float high)
{
    if (low > high)
        std::swap(low, high);
    low = std::clamp(low, min_, max_ - gap_);
    high = std::clamp(high, low + gap_, max_);
    value_[0] = low;
    value_[1] = high;
}

float RangeSlider::fraction_of(float value) const
{
    return max_ > min_ ? (value - min_) / (max_ - min_) : 0.0f;
}

std::string RangeSlider::text_for(float value) const
{
    return format ? format(value) : number_text(value, step_, max_ - min_, unit_);
}

std::string RangeSlider::text() const
{
    return text_for(value_[0]) + " - " + text_for(value_[1]);
}

float RangeSlider::preferred_height() const
{
    return measure(style, bounds_, label_).height;
}

Event RangeSlider::handle(const InputFrame &input, Feedback &feedback)
{
    const Parts parts = measure(style, bounds_, label_);
    const float x = thumb_x(parts.track, fraction_of(value_[thumb_]));
    const bool vertical = input.nav == Direction::up || input.nav == Direction::down;
    if (input.nav == Direction::left || input.nav == Direction::right)
    {
        const float direction = input.nav == Direction::right ? 1.0f : -1.0f;
        held_ = input.nav_repeat ? held_ + 1 : 0;
        const float span = max_ - min_;
        float stride =
            step_ > 0.0f ? step_ : span / static_cast<float>(std::max(style.continuous_steps, 1));
        if (style.fast_after > 0 && held_ >= style.fast_after)
            stride *= static_cast<float>(std::max(style.fast_factor, 1));
        float &value = value_[thumb_];
        float next = value + direction * stride;
        if (step_ > 0.0f)
            next = min_ + std::round((next - min_) / step_) * step_;
        // The other thumb, less the gap, is this one's wall.
        const float wall_low = thumb_ == 0 ? min_ : value_[0] + gap_;
        const float wall_high = thumb_ == 0 ? value_[1] - gap_ : max_;
        next = std::clamp(next, wall_low, wall_high);
        if (next == value)
            return refuse(feedback, style, input, refusal_, x);
        value = next;
        press_.trigger();
        play_cue(feedback, style, style.sounds.step, x,
                 style.pitch_by_value ? 0.9f + 0.3f * fraction_of(value) : 1.0f);
        return Event::changed;
    }
    if ((style.confirm_switches && input.is_pressed(Action::confirm)) ||
        (style.vertical_switches && vertical))
    {
        thumb_ = 1 - thumb_;
        play_cue(feedback, style, style.sounds.move,
                 thumb_x(parts.track, fraction_of(value_[thumb_])), thumb_ == 1 ? 1.05f : 0.95f);
        return Event::moved;
    }
    if (input.is_pressed(Action::back))
    {
        play_cue(feedback, style, style.sounds.cancel, x);
        return Event::cancelled;
    }
    return Event::none;
}

void RangeSlider::update(float dt)
{
    const float omega = std::max(style.omega(), 16.0f) * 1.3f;
    for (int i = 0; i < 2; ++i)
    {
        focus_[i].target = active_ && thumb_ == i ? 1.0f : 0.0f;
        focus_[i].update(dt, 18.0f);
        shown_[i].target = fraction_of(value_[i]);
        if (!placed_)
            shown_[i].snap(shown_[i].target);
        shown_[i].update(dt, omega);
    }
    placed_ = true;
    press_.update(dt, 10.0f);
    refusal_.update(dt, 9.0f);
}

void RangeSlider::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const Color resting = style.on_page ? paint.page_text() : theme.text;
    const Color quiet = style.on_page ? paint.page_text_muted() : theme.text_muted;
    const Parts parts = measure(style, bounds_, label_);
    const float active = std::max(focus_[0].value, focus_[1].value);

    if (parts.label_baseline > 0.0f)
    {
        float room = bounds_.w;
        if (style.bubble == BubbleMode::never && style.value_in_label)
            room -= paint.label(text(), bounds_.x + bounds_.w, parts.label_baseline,
                                style.value_size, resting, gfx::Align::right) +
                    16.0f;
        paint.label(fit_label(paint, label_, style.label_size, std::max(room, 40.0f)), bounds_.x,
                    parts.label_baseline, style.label_size,
                    gfx::mix(quiet, resting, active * 0.5f));
    }

    // ---- the track, as Painter::slider builds it ----
    const Rect r = parts.track;
    const bool chunky = theme.style == SurfaceStyle::hard || theme.style == SurfaceStyle::bevel ||
                        theme.style == SurfaceStyle::pixel;
    const float bar_h = chunky ? 2.0f * theme.border + 8.0f : 8.0f;
    const float size = thumb_size(r);
    const Rect bar{r.x + size * 0.5f, r.cy() - bar_h * 0.5f, r.w - size, bar_h};
    const float bar_radius = std::min(theme.radius, bar_h * 0.5f);
    const float nudge = style.reduced_motion ? 0.0f : shake(refusal_.value, canvas.time, 6.0f);
    const float x0 = thumb_x(r, shown_[0].value) + (thumb_ == 0 ? nudge : 0.0f);
    const float x1 = thumb_x(r, shown_[1].value) + (thumb_ == 1 ? nudge : 0.0f);
    if (theme.style == SurfaceStyle::sketch)
    {
        // A pen line for the track and a marker stroke for the chosen part.
        list.line(bar.x, r.cy() - 1.0f, bar.x + bar.w, r.cy() + 1.0f, theme.border, theme.outline);
        if (x1 - x0 > 1.0f)
            list.line(x0, r.cy(), x1, r.cy(), 9.0f, theme.accent.with_alpha(0.85f));
    }
    else
    {
        paint.well(bar, bar_radius, theme.surface_high);
        const float inset = chunky ? theme.border : 0.0f;
        if (x1 - x0 > 1.0f)
            list.rounded_rect({x0, bar.y + inset, x1 - x0, bar.h - 2.0f * inset}, 0.0f,
                              theme.accent);
    }
    draw_ticks(canvas, style, parts, quiet.with_alpha(0.6f));

    // The active thumb is drawn last, so it is the one on top when they meet.
    const float thumb_radius = std::min(theme.radius, size * 0.5f);
    const int order[2] = {1 - thumb_, thumb_};
    for (const int i : order)
    {
        const float x = i == 0 ? x0 : x1;
        const Rect thumb{x - size * 0.5f, r.cy() - size * 0.5f, size, size};
        draw_thumb(list, paint, thumb, thumb_ == i ? press_.value : 0.0f);
        paint.focus_ring(thumb, thumb_radius, focus_[i].value);
    }

    if (parts.ends_baseline > 0.0f)
    {
        paint.body(text_for(min_), bounds_.x, parts.ends_baseline, style.end_label_size, quiet);
        paint.body(text_for(max_), bounds_.x + bounds_.w, parts.ends_baseline, style.end_label_size,
                   quiet, gfx::Align::right);
    }

    if (style.bubble == BubbleMode::never)
        return;
    const float shown = style.bubble == BubbleMode::always ? 1.0f : active;
    const float left = bounds_.x;
    const float right = bounds_.x + bounds_.w;
    const std::string low = text_for(value_[0]);
    const std::string high = text_for(value_[1]);
    const auto width_of = [&](const std::string &text)
    {
        return std::max(paint.label_width(text, style.value_size) + 2.0f * style.bubble_padding,
                        style.bubble_height * 1.5f);
    };
    // Two bubbles that would touch become one over the middle of the range.
    const bool merged =
        style.merge_bubbles && x1 - x0 < (width_of(low) + width_of(high)) * 0.5f + 6.0f;
    if (merged)
    {
        draw_bubble(canvas, style, (x0 + x1) * 0.5f, parts.bubble_tip, low + " - " + high, active,
                    shown, left, right);
        return;
    }
    // Apart, each keeps to its own side so they never cross.
    const float middle = (x0 + x1) * 0.5f;
    draw_bubble(canvas, style, x0, parts.bubble_tip, low, focus_[0].value, shown, left, middle);
    draw_bubble(canvas, style, x1, parts.bubble_tip, high, focus_[1].value, shown, middle, right);
}

} // namespace hui::ui
