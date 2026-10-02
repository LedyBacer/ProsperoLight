// ps5-homebrew-ui - Component: Transition.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/transition.hpp"

#include <algorithm>

namespace hui::ui
{

void Transition::start(TransitionKind kind, Direction direction)
{
    kind_ = kind;
    direction_ = direction;
    running_ = true;
    leaving_ = false;
    hidden_ = false;
    elapsed_ = 0.0f;
}

void Transition::leave(TransitionKind kind, Direction direction)
{
    if (hidden_ && !running_)
        return; // already gone
    // A push has two contents; an exit has one. Leaving "by push" is a slide.
    kind_ = kind == TransitionKind::push ? TransitionKind::slide : kind;
    direction_ = direction;
    running_ = true;
    leaving_ = true;
    elapsed_ = 0.0f;
}

void Transition::finish()
{
    if (!running_)
        return;
    running_ = false;
    hidden_ = leaving_;
    leaving_ = false;
}

void Transition::show()
{
    running_ = false;
    leaving_ = false;
    hidden_ = false;
}

void Transition::hide()
{
    running_ = false;
    leaving_ = false;
    hidden_ = true;
}

float Transition::seconds() const
{
    // A critically damped spring is within one percent of its target after
    // about 4.8 / omega seconds: the same pace as everything else in a theme.
    float time = style.duration > 0.0f ? style.duration : 4.8f / std::max(style.omega(), 1.0f);
    if (style.reduced_motion)
        time = std::min(time, 0.16f);
    time = std::clamp(time, 0.1f, 0.8f);
    return leaving_ ? time / std::max(style.exit_speed, 0.1f) : time;
}

float Transition::progress() const
{
    return running_ ? tween::clamp01(elapsed_ / seconds()) : 1.0f;
}

void Transition::update(float dt)
{
    if (!running_)
        return;
    elapsed_ += dt;
    if (elapsed_ >= seconds())
        finish();
}

float Transition::opacity(TransitionPhase phase) const
{
    const bool incoming = phase == TransitionPhase::incoming;
    if (!running_)
        return incoming && !hidden_ ? 1.0f : 0.0f;
    const float t = progress();
    if (kind_ == TransitionKind::push)
    {
        // The old content is gone by half-way and the new one starts there,
        // so the two are never read at once.
        return incoming ? tween::smoothstep(t * 2.0f - 1.0f) : 1.0f - tween::smoothstep(t * 2.0f);
    }
    if (!incoming)
        return 0.0f;
    return leaving_ ? 1.0f - tween::smoothstep(t) : tween::cubic_out(t);
}

// How far along the direction of travel the phase is from its place: negative
// is "has not arrived yet", positive "has gone past".
float Transition::travel(TransitionPhase phase) const
{
    if (!running_ || style.reduced_motion || direction_ == Direction::none)
        return 0.0f;
    if (kind_ != TransitionKind::slide && kind_ != TransitionKind::push)
        return 0.0f;
    const float t = progress();
    if (phase == TransitionPhase::outgoing || leaving_)
        return style.distance * tween::cubic_in(t);
    // A theme that bounces lets an arrival overshoot its place a little.
    const float arrived = style.damping() < 0.8f ? tween::back_out(t) : tween::cubic_out(t);
    return -style.distance * (1.0f - arrived);
}

float Transition::offset_x(TransitionPhase phase) const
{
    if (direction_ == Direction::right)
        return travel(phase);
    if (direction_ == Direction::left)
        return -travel(phase);
    return 0.0f;
}

float Transition::offset_y(TransitionPhase phase) const
{
    if (direction_ == Direction::down)
        return travel(phase);
    if (direction_ == Direction::up)
        return -travel(phase);
    return 0.0f;
}

float Transition::scale(TransitionPhase phase) const
{
    if (!running_ || style.reduced_motion || kind_ != TransitionKind::scale ||
        phase == TransitionPhase::outgoing)
        return 1.0f;
    const float t = progress();
    if (leaving_)
        return tween::lerp(1.0f, style.scale, tween::cubic_in(t));
    const float arrived = style.damping() < 0.8f ? tween::back_out(t) : tween::cubic_out(t);
    return tween::lerp(style.scale, 1.0f, arrived);
}

void Transition::begin(Canvas &canvas, const gfx::Rect &region, TransitionPhase phase) const
{
    if (style.clip)
        canvas.list.push_clip(region.inset(-style.clip_bleed));
    canvas.list.push_opacity(opacity(phase));
    canvas.list.push_transform(scale(phase), region.cx(), region.cy(), offset_x(phase),
                               offset_y(phase));
}

void Transition::end(Canvas &canvas) const
{
    canvas.list.pop_transform();
    canvas.list.pop_opacity();
    if (style.clip)
        canvas.list.pop_clip();
}

} // namespace hui::ui
