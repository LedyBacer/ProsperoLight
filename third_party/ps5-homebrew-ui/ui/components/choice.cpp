// ps5-homebrew-ui - Component: ChoicePicker.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/choice.hpp"

#include "ui/components/focus_frame.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

void ChoicePicker::set_options(std::vector<std::string> options)
{
    options_ = std::move(options);
    set_index(index_);
}

void ChoicePicker::set_index(int index)
{
    const int count = static_cast<int>(options_.size());
    index_ = std::clamp(index, 0, std::max(count - 1, 0));
    previous_ = index_;
    slide_.snap(0.0f);
    marker_.snap(static_cast<float>(index_));
}

const std::string &ChoicePicker::value() const
{
    static const std::string kNone;
    return options_.empty() ? kNone : options_[static_cast<std::size_t>(index_)];
}

Event ChoicePicker::pick(int direction, bool may_wrap, const InputFrame &input, Feedback &feedback,
                         const ChoiceStyle &look, float x, Pulse &refusal)
{
    const int count = static_cast<int>(options_.size());
    int next = index_ + direction;
    if (next < 0 || next >= count)
    {
        // A held direction stops at the end even when a press would go round.
        if (!may_wrap || input.nav_repeat || count < 2)
            return refuse(feedback, look, input, refusal, x);
        next = direction > 0 ? 0 : count - 1;
    }
    previous_ = index_;
    index_ = next;
    direction_ = direction;
    slide_.value = look.reduced_motion ? 0.0f : 1.0f;
    slide_.velocity = 0.0f;
    slide_.target = 0.0f;
    marker_.target = static_cast<float>(index_);
    if (!look.reduced_motion)
        (direction > 0 ? nudge_right_ : nudge_left_).trigger();
    const float pitch = look.pitch_by_direction ? (direction > 0 ? 1.05f : 0.95f) : 1.0f;
    play_cue(feedback, look, look.sounds.change, x, pitch);
    return Event::changed;
}

Event ChoicePicker::handle(const InputFrame &input, Feedback &feedback)
{
    const float x = bounds_.cx();
    if (input.nav == Direction::left || input.nav == Direction::right)
        return pick(input.nav == Direction::right ? 1 : -1, style.wrap, input, feedback, style, x,
                    refusal_);
    if (input.is_pressed(Action::confirm))
    {
        if (style.confirm_cycles)
            return pick(1, true, input, feedback, style, x, refusal_);
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

void ChoicePicker::update(float dt, const ComponentStyle &look)
{
    focus_.target = active_ ? 1.0f : 0.0f;
    focus_.update(dt, 18.0f);
    const float omega = std::max(look.omega(), 14.0f);
    slide_.update(dt, omega * 1.2f);
    marker_.update(dt, omega * 1.2f);
    nudge_left_.update(dt, 10.0f);
    nudge_right_.update(dt, 10.0f);
    refusal_.update(dt, 9.0f);
}

void ChoicePicker::update(float dt)
{
    update(dt, style);
}

void ChoicePicker::draw(Canvas &canvas) const
{
    draw(canvas, style, bounds_, focus_.value);
}

void ChoicePicker::draw(Canvas &canvas, const ChoiceStyle &look, const Rect &rect,
                        float focus) const
{
    const Theme &theme = look.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const float shift = look.reduced_motion ? 0.0f : shake(refusal_.value, canvas.time, 8.0f);
    const Rect box{rect.x + shift, rect.y, rect.w, rect.h};
    const float radius = paint.control_radius(box);
    const bool ring_first = focus_frame_goes_under(theme);
    if (look.focus_ring && ring_first)
        focus_frame(canvas, theme, box, radius, focus, look.boxed);
    if (look.boxed)
        paint.well(box, radius, theme.surface_high);

    Color ink = look.ink;
    if (ink.a <= 0.0f)
        ink = look.on_page && !look.boxed ? paint.page_text() : theme.text;
    const int count = static_cast<int>(options_.size());
    const bool with_dots = look.dots && count >= 2 && count <= 12;
    const float cy = box.cy() - (with_dots ? look.dot_size * 0.5f + 2.0f : 0.0f);

    // ---- the arrows ----
    const float arrows = tween::lerp(look.idle_arrows, 1.0f, tween::clamp01(focus));
    const auto arrow = [&](int direction)
    {
        const bool open =
            look.wrap || !look.dim_at_limit || (direction > 0 ? index_ < count - 1 : index_ > 0);
        const float jump = (direction > 0 ? nudge_right_.value : nudge_left_.value) * look.nudge;
        const float x = direction > 0 ? box.x + box.w - look.arrow_inset + jump
                                      : box.x + look.arrow_inset - jump;
        const float a = look.arrow_size;
        const float tip = x + static_cast<float>(direction) * a * 0.5f;
        const float back = x - static_cast<float>(direction) * a * 0.5f;
        const Color color = ink.with_alpha(arrows * (open && count > 1 ? 1.0f : 0.3f));
        list.line(back, cy - a, tip, cy, look.arrow_width, color);
        list.line(tip, cy, back, cy + a, look.arrow_width, color);
    };
    arrow(-1);
    arrow(1);

    // ---- the value ----
    const float margin = look.arrow_inset * 2.0f;
    const Rect area{box.x + margin, box.y, std::max(box.w - 2.0f * margin, 8.0f), box.h};
    const float baseline = cy + look.value_size * 0.35f;
    const auto value = [&](int index, float dx, float opacity)
    {
        if (opacity <= 0.01f || index < 0 || index >= count)
            return;
        list.push_opacity(opacity);
        list.push_transform(1.0f, 0.0f, 0.0f, dx, 0.0f);
        if (option)
            option(canvas, {area.x, area.y, area.w, 2.0f * (cy - area.y)}, index, focus);
        else
            paint.bounded_label(options_[static_cast<std::size_t>(index)], area.cx(), baseline, look.value_size, ink, area.w, gfx::Align::center);
        list.pop_transform();
        list.pop_opacity();
    };
    const float t = tween::clamp01(std::fabs(slide_.value));
    if (t > 0.01f)
    {
        // Clipped only while it moves: every clip rectangle is a draw call.
        const float travel = look.slide * static_cast<float>(direction_);
        list.push_clip(area);
        value(previous_, -travel * (1.0f - t), t);
        value(index_, travel * t, 1.0f - t);
        list.pop_clip();
    }
    else
    {
        value(index_, 0.0f, 1.0f);
    }

    // ---- the dots ----
    if (with_dots)
    {
        const float size = look.dot_size;
        const float pitch = size * 2.0f;
        const float left = box.cx() - pitch * static_cast<float>(count - 1) * 0.5f;
        const float y = box.y + box.h - size - 7.0f;
        const float round = size * 0.5f;
        for (int i = 0; i < count; ++i)
            paint.fill({left + pitch * static_cast<float>(i) - size * 0.5f, y, size, size}, round,
                       ink.with_alpha(0.28f));
        const float at = std::clamp(marker_.value, 0.0f, static_cast<float>(count - 1));
        paint.fill({left + pitch * at - size, y, 2.0f * size, size}, round, theme.accent);
    }

    if (look.focus_ring && !ring_first)
        focus_frame(canvas, theme, box, radius, focus, look.boxed);
}

} // namespace hui::ui
