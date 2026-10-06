// ps5-homebrew-ui - Component: Stepper.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/stepper.hpp"

#include "ui/components/focus_frame.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

void Stepper::set_range(int minimum, int maximum, int step)
{
    min_ = std::min(minimum, maximum);
    max_ = std::max(minimum, maximum);
    step_ = std::max(step, 1);
    set_value(value_);
}

void Stepper::set_value(int value)
{
    value_ = std::clamp(value, min_, max_);
    previous_ = value_;
    roll_.snap(0.0f);
}

float Stepper::fraction() const
{
    return max_ > min_ ? static_cast<float>(value_ - min_) / static_cast<float>(max_ - min_) : 0.0f;
}

std::string Stepper::text_for(int value) const
{
    if (format)
        return format(value);
    char number[24];
    std::snprintf(number, sizeof(number), "%d", value);
    return std::string(number) + suffix_;
}

Event Stepper::step(const InputFrame &input, Feedback &feedback, const StepperStyle &look, float x,
                    Pulse &refusal)
{
    if (input.nav != Direction::left && input.nav != Direction::right)
        return Event::none;
    const int direction = input.nav == Direction::right ? 1 : -1;
    held_ = input.nav_repeat ? held_ + 1 : 0;
    int stride = step_;
    if (look.fast_after > 0 && held_ >= look.fast_after)
        stride *= std::max(look.fast_factor, 1);

    // A value the limits pushed off the step grid returns to the grid first,
    // so 30 (the maximum) steps down to 28 and not to 26.
    const int offset = (value_ - min_) % step_;
    int next = value_ + direction * stride;
    if (offset != 0)
        next = direction > 0 ? value_ - offset + stride : value_ - offset - (stride - step_);
    // A long stride lands on the limit instead of stopping short of it.
    next = std::clamp(next, min_, max_);
    if (next == value_)
    {
        if (!look.wrap || input.nav_repeat || min_ == max_)
            return refuse(feedback, look, input, refusal, x);
        next = direction > 0 ? min_ : max_;
    }
    previous_ = value_;
    value_ = next;
    direction_ = direction;
    roll_.value = look.reduced_motion ? 0.0f : 1.0f;
    roll_.velocity = 0.0f;
    roll_.target = 0.0f;
    (direction > 0 ? press_plus_ : press_minus_).trigger();
    play_cue(feedback, look, look.sounds.step, x,
             look.pitch_by_value ? tween::lerp(0.9f, 1.2f, fraction()) : 1.0f);
    return Event::changed;
}

Event Stepper::handle(const InputFrame &input, Feedback &feedback)
{
    const float x = bounds_.cx();
    if (input.nav == Direction::left || input.nav == Direction::right)
        return step(input, feedback, style, x, refusal_);
    if (input.is_pressed(Action::back))
    {
        play_cue(feedback, style, style.sounds.cancel, x);
        return Event::cancelled;
    }
    return Event::none;
}

void Stepper::update(float dt, const ComponentStyle &look)
{
    focus_.target = active_ ? 1.0f : 0.0f;
    focus_.update(dt, 18.0f);
    roll_.update(dt, std::max(look.omega(), 14.0f) * 1.3f);
    press_minus_.update(dt, 10.0f);
    press_plus_.update(dt, 10.0f);
    refusal_.update(dt, 9.0f);
}

void Stepper::update(float dt)
{
    update(dt, style);
}

void Stepper::draw(Canvas &canvas) const
{
    draw(canvas, style, bounds_, focus_.value);
}

void Stepper::draw(Canvas &canvas, const StepperStyle &look, const Rect &rect, float focus) const
{
    const Theme &theme = look.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const float nudge = look.reduced_motion ? 0.0f : shake(refusal_.value, canvas.time, 8.0f);
    const Rect box{rect.x + nudge, rect.y, rect.w, rect.h};
    const float radius = paint.control_radius(box);
    const bool ring_first = focus_frame_goes_under(theme);
    if (look.focus_ring && ring_first)
        focus_frame(canvas, theme, box, radius, focus, look.boxed);
    if (look.boxed)
        paint.well(box, radius, theme.surface_high);

    Color ink = look.ink;
    if (ink.a <= 0.0f)
        ink = look.on_page && !look.boxed ? paint.page_text() : theme.text;
    const float inset = look.boxed ? look.box_padding : 0.0f;
    const float size = std::max(std::min(look.button_size, box.h - 2.0f * inset), 8.0f);
    const bool square = theme.corner != Corner::round || theme.radius < 2.0f;

    const auto button = [&](bool plus)
    {
        const Rect body{plus ? box.x + box.w - inset - size : box.x + inset, box.cy() - size * 0.5f,
                        size, size};
        const float press = plus ? press_plus_.value : press_minus_.value;
        const bool open = look.wrap || (plus ? value_ < max_ : value_ > min_);
        list.push_opacity(open || !look.dim_at_limit ? 1.0f : 0.32f);
        Rect content = body;
        Color sign = ink;
        if (look.buttons == StepperButtons::surface)
        {
            // The recipe of Painter::button's secondary kind, without a label.
            const bool hollow = theme.secondary.a <= 0.01f;
            const bool framed = theme.style == SurfaceStyle::hard ||
                                theme.style == SurfaceStyle::pixel ||
                                theme.style == SurfaceStyle::bevel;
            const float line = theme.button_border < 0.0f ? theme.border : theme.button_border;
            content = paint.surface(body, paint.control_radius(body), theme.secondary,
                                    hollow ? theme.on_secondary : theme.outline, 1.0f - press,
                                    hollow ? std::max(theme.border, 1.5f)
                                           : (framed ? theme.border : line));
            sign = theme.on_secondary;
        }
        else
        {
            // No body to push in: the sign leans away from the number instead.
            sign = gfx::mix(ink.with_alpha(0.6f), ink, focus);
            content.x += (plus ? 3.0f : -3.0f) * press;
        }
        const float half = std::min(look.sign_size, size * 0.3f);
        const float thick = look.sign_width;
        const float round = square ? 0.0f : thick * 0.5f;
        list.rounded_rect({content.cx() - half, content.cy() - thick * 0.5f, 2.0f * half, thick},
                          round, sign);
        if (plus)
            list.rounded_rect(
                {content.cx() - thick * 0.5f, content.cy() - half, thick, 2.0f * half}, round,
                sign);
        list.pop_opacity();
    };
    button(false);
    button(true);

    // The number rolls like a counter: up when it grows, down when it shrinks.
    const float baseline = box.cy() + look.value_size * 0.35f;
    const float room = std::max(box.w - 2.0f * (inset + size) - 12.0f, 8.0f);
    const float t = tween::clamp01(std::fabs(roll_.value));
    const float travel = look.roll * static_cast<float>(direction_);
    const auto number = [&](int value, float dy, float opacity)
    {
        list.push_opacity(opacity);
        paint.bounded_label(text_for(value), box.cx(), baseline + dy, look.value_size, ink, room, gfx::Align::center);
        list.pop_opacity();
    };
    if (t > 0.01f)
        number(previous_, -travel * (1.0f - t), t);
    number(value_, travel * t, 1.0f - t);

    if (look.focus_ring && !ring_first)
        focus_frame(canvas, theme, box, radius, focus, look.boxed);
}

} // namespace hui::ui
