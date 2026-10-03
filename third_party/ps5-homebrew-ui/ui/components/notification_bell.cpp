// ps5-homebrew-ui - Component: NotificationBell.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/notification_bell.hpp"

#include <algorithm>
#include <cmath>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

constexpr float kTwoPi = 6.2831853f;

// The corner a bell button of this shape gets (negative: the theme's own).
float bell_radius(const Theme &theme, IconButtonShape shape, float size)
{
    if (shape == IconButtonShape::square)
        return -1.0f;
    if (theme.corner == Corner::chamfer)
        return size * 0.29f; // an octagon: a full cut would leave a diamond
    // Bevels, notches and pen strokes have no circle: they keep their corners.
    if (theme.corner == Corner::pixel || theme.style == SurfaceStyle::bevel ||
        theme.style == SurfaceStyle::sketch)
        return -1.0f;
    return size * 0.5f;
}

} // namespace

void draw_bell(Canvas &canvas, const Rect &box, Color ink, float swing, float waves)
{
    gfx::DrawList &list = canvas.list;
    const float s = std::min(box.w, box.h);
    if (s <= 0.0f)
        return;
    const float cx = box.cx();
    const float cy = box.cy();
    // The kit turns rounded rectangles about their own centre only, so the
    // bell swings by moving each part's centre around the hook it hangs from.
    const float pivot_y = -0.42f * s;
    const float sine = std::sin(swing);
    const float cosine = std::cos(swing);
    const auto turned = [&](float x, float y, float *out_x, float *out_y)
    {
        const float dy = y - pivot_y;
        *out_x = cx + x * cosine - dy * sine;
        *out_y = cy + pivot_y + x * sine + dy * cosine;
    };
    const auto disc = [&](float x, float y, float radius)
    {
        float px = 0.0f;
        float py = 0.0f;
        turned(x, y, &px, &py);
        list.circle(px, py, radius, ink);
    };
    const auto plate = [&](float x, float y, float w, float h, float radius)
    {
        float px = 0.0f;
        float py = 0.0f;
        turned(x, y, &px, &py);
        list.rotated_rect({px - w * 0.5f, py - h * 0.5f, w, h}, radius, swing, ink);
    };

    disc(0.0f, -0.36f * s, 0.06f * s);                         // the hook
    disc(0.0f, -0.08f * s, 0.25f * s);                         // the dome
    plate(0.0f, 0.06f * s, 0.5f * s, 0.28f * s, 0.02f * s);    // the skirt
    plate(0.0f, 0.215f * s, 0.74f * s, 0.11f * s, 0.055f * s); // the rim
    disc(-0.11f * sine * s, 0.36f * s, 0.085f * s);            // the clapper lags behind

    if (waves > 0.01f)
    {
        const Color line = ink.with_alpha(ink.a * tween::clamp01(waves));
        const float pen = std::max(2.0f, 0.055f * s);
        // Angles run clockwise from 12 o'clock: three and nine o'clock.
        for (int side = 0; side < 2; ++side)
        {
            const float at = side == 0 ? 1.5708f : 4.7124f;
            list.arc(cx, cy - 0.04f * s, 0.6f * s, pen, at - 0.36f, 0.72f, line);
            list.arc(cx, cy - 0.04f * s, 0.74f * s, pen, at - 0.3f, 0.6f,
                     line.with_alpha(line.a * 0.55f));
        }
    }
}

Rect NotificationBell::rect() const
{
    const float size = style.size > 0.0f ? style.size : std::min(bounds_.w, bounds_.h);
    return {bounds_.cx() - size * 0.5f, bounds_.cy() - size * 0.5f, size, size};
}

float NotificationBell::radius() const
{
    return bell_radius(style.theme, style.shape, rect().w);
}

// The badge is a component of its own: it takes the bell's theme and place
// whenever either may have changed.
void NotificationBell::sync()
{
    const Rect r = rect();
    BadgeStyle &badge = badge_.style;
    badge.theme = style.theme;
    badge.reduced_motion = style.reduced_motion;
    badge.kind = style.badge_kind;
    badge.height = style.badge_height;
    badge.text_size = style.badge_text;
    badge.max_count = style.badge_max;
    badge.cutout = style.badge_cutout;
    badge.padding = 7.0f;
    // On a circle the badge sits on the rim at half past one; on a box, on
    // its corner.
    const bool circle = radius() >= r.w * 0.5f - 0.5f;
    const float cx = circle ? r.cx() + r.w * 0.354f : r.x + r.w - 2.0f;
    const float cy = circle ? r.cy() - r.w * 0.354f : r.y + 2.0f;
    badge_.set_bounds({cx - 40.0f, cy - style.badge_height * 0.5f, 80.0f, style.badge_height});
}

void NotificationBell::set_count(int count)
{
    sync();
    const int before = badge_.count();
    badge_.set_count(count);
    if (style.ring_on_increase && badge_.count() > before)
        ring();
}

void NotificationBell::ring()
{
    ring_ = 0.0f;
}

Event NotificationBell::handle(const InputFrame &input, Feedback &feedback)
{
    if (!input.is_pressed(Action::confirm))
        return Event::none;
    press_.trigger();
    play_cue(feedback, style, style.sounds.activate, bounds_.cx());
    if (style.sounds.rumble > 0.0f && style.rumble > 0.0f)
        feedback.rumble(style.rumble * style.sounds.rumble, 0.04f);
    return Event::activated;
}

void NotificationBell::update(float dt)
{
    sync();
    focus_.target = active_ ? 1.0f : 0.0f;
    focus_.update(dt, std::max(style.omega(), 18.0f));
    press_.update(dt, 9.0f);
    ring_ = std::min(ring_ + dt, 1000.0f);
    badge_.update(dt);
}

void NotificationBell::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    const Rect r = rect();
    const float press = tween::clamp01(press_.value);
    const float scale = style.reduced_motion ? 1.0f : 1.0f - style.press_scale * press;

    list.push_transform(scale, r.cx(), r.cy(), 0.0f, 0.0f);
    Look look;
    look.focus = tween::clamp01(focus_.value);
    look.press = press;
    const ButtonFace face =
        draw_button_face(canvas, theme, r, style.role, look, radius(), style.on_page);

    // The swing dies away like a struck bell: widest at once, still at the end.
    const float span = std::max(style.ring_seconds, 0.01f);
    const float t = tween::clamp01(ring_ / span);
    const float fade = (1.0f - t) * (1.0f - t);
    const bool rings = ring_ < span;
    const float angle = rings && !style.reduced_motion
                            ? style.swing * fade * std::sin(ring_ * style.swing_hz * kTwoPi)
                            : 0.0f;
    const float waves = rings && style.waves ? tween::ping(t) * 0.8f : 0.0f;
    const float size = r.w * style.icon_scale;
    draw_bell(canvas,
              {face.content.cx() - size * 0.5f, face.content.cy() - size * 0.5f, size, size},
              face.ink, angle, waves);
    list.pop_transform();
    badge_.draw(canvas);
}

} // namespace hui::ui
