// ps5-homebrew-ui - Component: HoldButton.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/hold_button.hpp"

#include <algorithm>
#include <cmath>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

constexpr float kTau = 6.2831853f;

float baseline_for(float cy, float size)
{
    return cy + size * 0.35f;
}

} // namespace

Rect HoldButton::rect() const
{
    const ButtonMetrics m =
        button_metrics(style.size, style.height, style.text_size, style.padding);
    return {bounds_.x, bounds_.cy() - m.height * 0.5f, bounds_.w, m.height};
}

void HoldButton::set_active(bool active)
{
    active_ = active;
    if (!active)
        armed_ = false;
}

void HoldButton::reset()
{
    armed_ = false;
    renewed_ = false;
    progress_ = 0.0f;
    held_for_ = 0.0f;
    hint_left_ = 0.0f;
    ticks_ = 0;
}

Event HoldButton::handle(const InputFrame &input, Feedback &feedback)
{
    const float x = bounds_.cx();
    if (input.is_pressed(style.action))
    {
        if (disabled_)
            return refuse(feedback, style, input, refusal_, x);
        if (!armed_)
        {
            armed_ = true;
            held_for_ = 0.0f;
            hint_left_ = 0.0f;
            // A hold that restarts before it drained continues from there.
            ticks_ = static_cast<int>(progress_ * 4.0f);
        }
    }
    if (!armed_)
        return Event::none;
    if (!input.is_held(style.action) || input.focus_lost)
    {
        armed_ = false;
        if (held_for_ < style.tap_seconds && !input.focus_lost)
        {
            // A tap: say what the button wants instead of doing nothing.
            hint_left_ = style.hint_seconds;
            return refuse(feedback, style, input, refusal_, x);
        }
        return Event::none;
    }
    renewed_ = true;
    if (progress_ >= 1.0f)
    {
        armed_ = false;
        renewed_ = false;
        done_.trigger();
        play_cue(feedback, style, style.complete_cue, x);
        if (style.sounds.rumble > 0.0f)
            feedback.rumble(style.rumble_done * style.sounds.rumble, 0.14f);
        return Event::activated;
    }
    // The pad tightens as the button fills.
    if (style.sounds.rumble > 0.0f)
        feedback.rumble(tween::lerp(style.rumble_from, style.rumble_to, progress_) *
                            style.sounds.rumble,
                        0.05f);
    const int quarter = static_cast<int>(progress_ * 4.0f);
    if (quarter > ticks_)
    {
        ticks_ = quarter;
        if (style.ticks)
            play_cue(feedback, style, style.sounds.step, x,
                     0.92f + 0.1f * static_cast<float>(quarter), 0.7f);
    }
    return Event::none;
}

void HoldButton::update(float dt)
{
    const float seconds = std::max(style.hold_seconds, 0.05f);
    if (armed_ && renewed_)
    {
        held_for_ += dt;
        progress_ = std::min(1.0f, progress_ + dt / seconds);
    }
    else
    {
        // No handle() this frame: the focus went elsewhere, the hold is over.
        armed_ = false;
        progress_ = std::max(0.0f, progress_ - dt * std::max(style.drain_speed, 0.1f) / seconds);
    }
    renewed_ = false;

    hint_left_ = std::max(0.0f, hint_left_ - dt);
    const float omega = std::max(style.omega(), 18.0f);
    hint_amount_.target = hint_left_ > 0.0f ? 1.0f : 0.0f;
    hint_amount_.update(dt, omega);
    focus_.target = active_ ? 1.0f : 0.0f;
    focus_.update(dt, omega);
    down_.target = armed_ ? 1.0f : 0.0f;
    down_.update(dt, std::max(style.omega(), 26.0f));
    done_.update(dt, 4.0f);
    refusal_.update(dt, 9.0f);
}

void HoldButton::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const ButtonMetrics m =
        button_metrics(style.size, style.height, style.text_size, style.padding);
    Rect r = rect();
    r.x += shake(refusal_.value, canvas.time, 8.0f);
    const float focus = tween::clamp01(focus_.value);
    const float down = tween::clamp01(down_.value);
    const float done = tween::clamp01(done_.value);
    const float p = tween::clamp01(progress_);
    const float scale =
        style.reduced_motion ? 1.0f : 1.0f - style.press_scale * down + 0.05f * done;

    list.push_transform(scale, r.cx(), r.cy(), 0.0f, 0.0f);
    const ButtonPaint tone = button_paint(theme, style.role, style.on_page);
    // The moment it completes, light leaves the button.
    if (done > 0.01f)
    {
        const Color glow = tone.fill.a > 0.5f && theme.style != SurfaceStyle::glow
                               ? Color{tone.fill.r, tone.fill.g, tone.fill.b, 1.0f}
                               : tone.ink;
        paint.halo(r, paint.control_radius(r), 10.0f + 22.0f * (1.0f - done),
                   glow.with_alpha(0.7f * done));
    }
    Look look;
    look.focus = focus;
    look.press = down;
    look.disabled = disabled_;
    const ButtonFace face =
        draw_button_face(canvas, theme, r, style.role, look, -1.0f, style.on_page);
    const Rect &content = face.content;
    const Color ink = face.ink;
    const float line = std::max(tone.border, 0.0f);
    const Rect inner = content.inset(line);
    const float inner_radius = std::max(face.radius - line, 0.0f);
    const bool round = theme.corner == Corner::round && theme.radius >= 2.0f;

    list.push_opacity(disabled_ ? 0.4f : 1.0f);
    if (style.variant == HoldVariant::fill && p > 0.001f)
    {
        // The sweep keeps the body's corners: it starts as wide as they need
        // and fades in, so a pill fills as a pill and nothing pokes out.
        const float least = std::min(std::max(2.0f * inner_radius, 8.0f), inner.w);
        const Rect sweep{inner.x, inner.y, tween::lerp(least, inner.w, p), inner.h};
        const Color wash = ink.with_alpha(style.wash * tween::clamp01(p * 10.0f));
        // A hand-drawn fill tilts by its size: one that grows would wobble.
        if (theme.style == SurfaceStyle::sketch)
            list.rounded_rect(sweep, inner_radius, wash);
        else
            paint.fill(sweep, inner_radius, wash);
    }
    if (done > 0.01f)
        paint.fill(inner, inner_radius, ink.with_alpha(0.3f * done));

    // The label and, after a tap, the hint: both start where the glyph ends.
    const float hinting = tween::clamp01(hint_amount_.value);
    const float glyph_size = style.glyph_size > 0.0f ? style.glyph_size : m.text_size * 1.3f;
    const bool ring = style.variant == HoldVariant::ring;
    const float ring_reach = ring ? style.ring_width + 4.0f : 0.0f;
    const bool has_glyph = style.glyph != Button::none || ring;
    const float glyph_w = style.glyph != Button::none ? button_width(style.glyph, glyph_size)
                                                      : (ring ? glyph_size : 0.0f);
    const float lead = has_glyph ? glyph_w + 2.0f * ring_reach : 0.0f;
    const float lead_gap = has_glyph ? style.gap : 0.0f;
    const float room = std::max(content.w - 2.0f * m.padding - lead - lead_gap, 0.0f);
    const std::string &text = label;
    const std::string &nudge = hint;
    const float text_w = std::min(tween::lerp(paint.label_width(text, m.text_size),
                                     paint.label_width(nudge, m.text_size), hinting), room);
    const float x = content.cx() - (lead + lead_gap + text_w) * 0.5f;
    const bool lined = style.variant == HoldVariant::underline;
    const float cy = content.cy() - (lined ? style.line_height * 0.5f + 1.0f : 0.0f);

    if (has_glyph)
    {
        const float gx = x + ring_reach;
        if (style.glyph != Button::none)
            draw_button(list, canvas.fonts, glyph_style_on(face, style.glyph_tinted), style.glyph,
                        gx, cy, glyph_size);
        else
            list.circle(gx + glyph_w * 0.5f, cy, glyph_size * 0.14f, ink);
        if (ring)
        {
            const float outer = glyph_w * 0.5f + ring_reach;
            const float ccx = gx + glyph_w * 0.5f;
            list.ring(ccx, cy, outer, style.ring_width, ink.with_alpha(0.25f));
            if (p > 0.001f)
                list.arc(ccx, cy, outer, style.ring_width, 0.0f, kTau * p, ink, round);
        }
    }
    const float text_x = x + lead + lead_gap;
    const float baseline = baseline_for(cy, m.text_size);
    if (hinting < 0.99f)
        paint.bounded_label(text, text_x, baseline, m.text_size, ink.with_alpha(1.0f - hinting), room);
    if (hinting > 0.01f)
        paint.bounded_label(nudge, text_x, baseline, m.text_size, ink.with_alpha(hinting), room);

    if (lined)
    {
        const float margin = std::max(face.radius * 0.75f, 14.0f) + line;
        const float y = content.y + content.h - line - 8.0f - style.line_height;
        const Rect track{content.x + margin, y, std::max(content.w - 2.0f * margin, 0.0f),
                         style.line_height};
        const float cap = round ? style.line_height * 0.5f : 0.0f;
        list.rounded_rect(track, cap, ink.with_alpha(0.22f));
        if (p > 0.001f)
            list.rounded_rect({track.x, track.y, std::max(track.w * p, 2.0f * cap), track.h}, cap,
                              ink);
    }
    list.pop_opacity();
    list.pop_transform();
}

} // namespace hui::ui
