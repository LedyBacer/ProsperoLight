// ps5-homebrew-ui - Component: UnlockPopup.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/unlock_popup.hpp"

#include "ui/components/overlay.hpp"
#include "ui/components/progress.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

const Color kWhite = Color::rgb(0xffffff);
const Color kBlack = Color::rgb(0x000000);
constexpr float kMedalDelay = 0.12f; // the medal lands a beat after the plate
constexpr float kSparkTime = 0.9f;

bool stroke_only(const Theme &theme)
{
    return theme.style == SurfaceStyle::outline || theme.style == SurfaceStyle::glow;
}

bool square_theme(const Theme &theme)
{
    return theme.corner != Corner::round || theme.radius < 2.0f;
}

// Themes built from hard edges have no soft light to spend on a glow.
bool lit_theme(const Theme &theme)
{
    return theme.style != SurfaceStyle::hard && theme.style != SurfaceStyle::pixel &&
           theme.style != SurfaceStyle::bevel && theme.style != SurfaceStyle::sketch &&
           theme.style != SurfaceStyle::neumorphic;
}

// A repeatable 0..1 per spark, so draw() needs no particle state.
float scatter(int index, float salt)
{
    const float value = std::sin(static_cast<float>(index) * 12.9898f + salt * 78.233f) * 43758.5f;
    return value - std::floor(value);
}

bool on_left(PopupAnchor anchor)
{
    return anchor == PopupAnchor::top_left || anchor == PopupAnchor::bottom_left;
}
bool on_right(PopupAnchor anchor)
{
    return anchor == PopupAnchor::top_right || anchor == PopupAnchor::bottom_right;
}
bool on_top(PopupAnchor anchor)
{
    return anchor == PopupAnchor::top_left || anchor == PopupAnchor::top_center ||
           anchor == PopupAnchor::top_right;
}

} // namespace

void UnlockPopup::push(Unlock unlock)
{
    queue_.push_back(std::move(unlock));
}

void UnlockPopup::push(std::string title, std::string subtitle, UnlockTier tier, int points)
{
    Unlock unlock;
    unlock.title = std::move(title);
    unlock.subtitle = std::move(subtitle);
    unlock.tier = tier;
    unlock.points = points;
    queue_.push_back(std::move(unlock));
}

void UnlockPopup::clear(bool now)
{
    queue_.clear();
    if (now)
    {
        active_ = false;
        wait_ = 0.0f;
    }
    else if (active_)
    {
        leaving_ = true;
    }
}

Color UnlockPopup::tier_color(UnlockTier tier) const
{
    const Color color = style.tier_colors[static_cast<std::size_t>(tier)];
    if (color.a > 0.0f)
        return color;
    return {style.theme.primary.r, style.theme.primary.g, style.theme.primary.b, 1.0f};
}

Rect UnlockPopup::rect() const
{
    const float width = std::min(style.width, bounds_.w - 2.0f * style.margin);
    float x = bounds_.cx() - width * 0.5f;
    if (on_left(style.anchor))
        x = bounds_.x + style.margin;
    else if (on_right(style.anchor))
        x = bounds_.x + bounds_.w - style.margin - width;
    const float y = on_top(style.anchor) ? bounds_.y + style.margin
                                         : bounds_.y + bounds_.h - style.margin - style.height;
    return {x, y, width, style.height};
}

void UnlockPopup::update(float dt, Feedback &feedback)
{
    if (!active_)
    {
        wait_ -= dt;
        if (queue_.empty() || wait_ > 0.0f)
            return;
        current_ = std::move(queue_.front());
        queue_.erase(queue_.begin());
        active_ = true;
        leaving_ = false;
        age_ = 0.0f;
        enter_.snap(0.0f);
        leave_.snap(0.0f);
        medal_.snap(0.0f);

        // The better the medal, the brighter the sound.
        const float x = rect().cx();
        const int tier = static_cast<int>(current_.tier);
        audio::Cue cue = style.sounds.notify;
        if (current_.tier == UnlockTier::gold && style.gold_cue != audio::Cue::count)
            cue = style.gold_cue;
        else if (current_.tier == UnlockTier::special && style.special_cue != audio::Cue::count)
            cue = style.special_cue;
        const bool plain = cue == style.sounds.notify;
        play_cue(feedback, style, cue, x, plain ? 1.0f + 0.07f * static_cast<float>(tier) : 1.0f);
        if (tier >= static_cast<int>(UnlockTier::gold) && style.sounds.rumble > 0.0f)
            feedback.rumble(0.3f * style.sounds.rumble, 0.08f);
        return;
    }

    age_ += dt;
    const bool calm = style.reduced_motion;
    const float omega = std::max(style.omega(), 10.0f);
    enter_.target = 1.0f;
    enter_.update(dt, omega, calm ? 1.0f : std::max(style.damping(), 0.68f));
    if (age_ >= kMedalDelay)
        medal_.target = 1.0f;
    medal_.update(dt, 17.0f, calm ? 1.0f : 0.48f);

    const float hold = current_.seconds > 0.0f ? current_.seconds : style.hold;
    if (age_ >= hold)
        leaving_ = true;
    leave_.target = leaving_ ? 1.0f : 0.0f;
    leave_.update(dt, omega * 1.3f);
    if (leaving_ && leave_.value > 0.985f)
    {
        active_ = false;
        wait_ = style.gap;
    }
}

// A medal drawn from shapes: a rim lit from above, a groove lit the other
// way, a face and an engraved emblem. One emblem per tier, so the tier reads
// without colour. Stroke-only themes get an outlined medal, square themes a
// square one.
void UnlockPopup::draw_medal(Canvas &canvas, float cx, float cy, float size, UnlockTier tier) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, 0);
    const Color base = tier_color(tier);
    const Color light = gfx::mix(base, kWhite, 0.55f);
    const Color dark = gfx::mix(base, kBlack, 0.36f);
    const Color deep = gfx::mix(base, kBlack, 0.62f);
    const float half = size * 0.5f;
    const Rect plate{cx - half, cy - half, size, size};
    Color mark = deep;
    bool engraved = true;

    if (stroke_only(theme))
    {
        const float pen = std::max(3.0f, size * 0.05f);
        if (square_theme(theme))
            paint.stroke(plate, std::min(theme.radius, half), pen, base);
        else
            list.ring(cx, cy, half, pen, base);
        mark = base;
        engraved = false;
    }
    else if (square_theme(theme))
    {
        const float radius = std::min(theme.radius, half);
        const float rim = std::max(size * 0.07f, theme.border);
        paint.fill(plate, radius, dark);
        paint.fill(plate.inset(rim), std::max(radius - rim, 0.0f), base);
        list.rounded_rect({plate.x + rim, plate.y + rim, size - 2.0f * rim, rim}, 0.0f, light);
    }
    else
    {
        list.shadow({plate.x, plate.y + size * 0.08f, size, size}, half, size * 0.18f,
                    kBlack.with_alpha(0.4f));
        list.gradient_rect(plate, half, light, dark);
        const Rect groove = plate.inset(size * 0.06f);
        list.gradient_rect(groove, groove.w * 0.5f, dark, gfx::mix(base, light, 0.4f));
        const Rect face = plate.inset(size * 0.11f);
        list.gradient_rect(face, face.w * 0.5f, gfx::mix(base, light, 0.38f),
                           gfx::mix(base, dark, 0.3f));
        // The rim catches the lamp at its upper left.
        list.arc(cx, cy, half * 0.985f, size * 0.035f, -1.15f, 1.0f, kWhite.with_alpha(0.6f));
    }

    const float e = size * 0.2f;
    const float pen = std::max(3.0f, size * 0.075f);
    const auto emblem = [&](float dy, Color color)
    {
        const float y = cy + dy;
        const auto chevron = [&](float at)
        {
            list.line(cx - e, at + e * 0.45f, cx, at - e * 0.45f, pen, color);
            list.line(cx, at - e * 0.45f, cx + e, at + e * 0.45f, pen, color);
        };
        switch (tier)
        {
        case UnlockTier::bronze:
            chevron(y);
            break;
        case UnlockTier::silver:
            chevron(y - e * 0.42f);
            chevron(y + e * 0.42f);
            break;
        case UnlockTier::gold:
            list.star(cx, y + size * 0.01f, e * 1.3f, color);
            break;
        case UnlockTier::special:
        {
            const float d = e * 1.5f;
            list.rotated_rect({cx - d * 0.5f, y - d * 0.5f, d, d}, 1.5f, 0.7853982f, color);
            break;
        }
        }
    };
    // Engraved: a light lip below the cut, the cut itself in the dark tone.
    if (engraved)
        emblem(std::max(1.5f, size * 0.025f), light.with_alpha(0.8f));
    emblem(0.0f, mark);
}

void UnlockPopup::draw(Canvas &canvas) const
{
    if (!active_)
        return;
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const bool calm = style.reduced_motion;
    const Rect r = rect();
    const float in = enter_.value;
    const float out = tween::clamp01(leave_.value);
    const Color tier = tier_color(current_.tier);

    // It comes from the nearest edge and leaves the way it came.
    const float away = calm ? 0.0f : (1.0f - in) + out;
    float dx = 0.0f;
    float dy = 0.0f;
    if (on_left(style.anchor))
        dx = -(r.w + style.margin + 40.0f) * away;
    else if (on_right(style.anchor))
        dx = (r.w + style.margin + 40.0f) * away;
    else
        dy = (on_top(style.anchor) ? -1.0f : 1.0f) * (r.h + style.margin + 40.0f) * away;
    list.push_opacity(tween::clamp01(in * 2.5f) * (1.0f - out));
    list.push_transform(1.0f, 0.0f, 0.0f, dx, dy);

    const float radius = std::min(theme.radius_card, r.h * 0.5f);
    if (style.tier_glow && lit_theme(theme))
    {
        // Brightest as it lands, then a steady rim of light.
        const float flash = calm ? 0.0f : std::exp(-2.2f * age_);
        paint.halo(r, theme.corner == Corner::round ? radius : 0.0f, 22.0f,
                   tier.with_alpha(0.22f + 0.4f * flash));
    }
    draw_overlay_panel(canvas, theme, r, style.frosted, style.frost);

    // ---- the medal, or the caller's icon ----
    const float size = std::min(style.medal_size, r.h - 16.0f);
    const float mx = r.x + style.padding + size * 0.5f;
    const float my = r.cy();
    const float landed = calm ? tween::clamp01(medal_.value) : std::max(medal_.value, 0.0f);
    if (current_.icon && icon)
    {
        list.push_transform(calm ? 1.0f : landed, mx, my, 0.0f, 0.0f);
        list.push_opacity(tween::clamp01(landed * 2.0f));
        icon(canvas, {mx - size * 0.5f, my - size * 0.5f, size, size}, current_,
             tween::clamp01(landed));
        list.pop_opacity();
        list.pop_transform();
    }
    else if (landed > 0.01f)
    {
        list.push_transform(calm ? 1.0f : landed, mx, my, 0.0f, 0.0f);
        list.push_opacity(tween::clamp01(landed * 2.0f));
        draw_medal(canvas, mx, my, size, current_.tier);
        list.pop_opacity();
        list.pop_transform();
    }

    // ---- the words ----
    const float left = mx + size * 0.5f + 18.0f;
    float right = r.x + r.w - style.padding;
    if (current_.points > 0)
    {
        char text[16];
        std::snprintf(text, sizeof(text), "+%d", current_.points);
        const float width = paint.label_width(text, style.points_size) + 26.0f;
        const float height = style.points_size + 16.0f;
        const Rect pill{right - width, r.cy() - height * 0.5f, width, height};
        const float corner =
            theme.pill_chips ? height * 0.5f : std::min(theme.radius, height * 0.5f);
        if (stroke_only(theme))
            paint.stroke(pill, corner, 2.0f, tier);
        else
            paint.fill(pill, corner, tier);
        paint.label(text, pill.cx(), pill.cy() + style.points_size * 0.35f, style.points_size,
                    stroke_only(theme) ? tier : Painter::on(tier), gfx::Align::center);
        right = pill.x - 16.0f;
    }
    const float room = std::max(right - left, 40.0f);
    const std::string &kicker = current_.kicker.empty() ? style.kicker : current_.kicker;
    const bool has_kicker = !kicker.empty();
    const bool has_subtitle = !current_.subtitle.empty();
    const float block = (has_kicker ? style.kicker_size + 8.0f : 0.0f) + style.title_size +
                        (has_subtitle ? style.subtitle_size + 8.0f : 0.0f);
    float y = r.cy() - block * 0.5f;
    if (has_kicker)
    {
        paint.label(fit_label(paint, upper(kicker), style.kicker_size, room), left,
                    y + style.kicker_size * 0.84f, style.kicker_size, theme.text_muted);
        y += style.kicker_size + 8.0f;
    }
    paint.label(fit_label(paint, current_.title, style.title_size, room), left,
                y + style.title_size * 0.82f, style.title_size, theme.text);
    y += style.title_size + 8.0f;
    if (has_subtitle)
        paint.body(fit_body(paint, current_.subtitle, style.subtitle_size, room), left,
                   y + style.subtitle_size * 0.82f, style.subtitle_size, theme.text_muted);

    // ---- the shine: one narrow band of light crossing the plate ----
    if (style.shine && !calm && style.shine_time > 0.0f)
    {
        const float run = (age_ - style.shine_delay) / style.shine_time;
        if (run > 0.0f && run < 1.0f)
        {
            // It fades in and out at the ends, so it never shows a hard edge
            // against the plate's corners.
            const float strength = std::sin(run * 3.14159265f);
            const float inset =
                theme.corner == Corner::round ? std::min(radius * 0.3f, 5.0f) : 4.0f;
            const float cx = tween::lerp(r.x - 40.0f, r.x + r.w + 40.0f, tween::smoothstep(run));
            draw_sweep(list, cx, 80.0f, r.x + inset, r.x + r.w - inset, r.y + 3.0f, r.h - 6.0f,
                       gfx::mix(tier, kWhite, 0.6f).with_alpha(0.36f * strength));
        }
    }

    // ---- sparks: thrown from the medal as it lands, pulled down a little ----
    const float t = (age_ - kMedalDelay) / kSparkTime;
    if (style.sparks && !calm && t > 0.0f && t < 1.0f)
    {
        const float travel = tween::cubic_out(t);
        const int count = std::clamp(style.spark_count, 0, 64);
        for (int i = 0; i < count; ++i)
        {
            const float angle = static_cast<float>(i) * 2.399963f + scatter(i, 1.0f);
            const float reach = size * (0.75f + 0.9f * scatter(i, 2.0f));
            const float x = mx + std::sin(angle) * reach * travel;
            const float sy = my - std::cos(angle) * reach * travel + 46.0f * t * t;
            const float dot = (2.5f + 3.5f * scatter(i, 3.0f)) * (1.0f - 0.5f * t);
            const Color color =
                gfx::mix(tier, kWhite, 0.25f + 0.5f * scatter(i, 4.0f)).with_alpha(1.0f - t * t);
            if (i % 3 == 0)
                list.star(x, sy, dot * 1.7f, color);
            else if (square_theme(theme))
                list.rounded_rect({x - dot, sy - dot, dot * 2.0f, dot * 2.0f}, 0.0f, color);
            else
                list.circle(x, sy, dot, color);
        }
    }

    list.pop_transform();
    list.pop_opacity();
}

} // namespace hui::ui
