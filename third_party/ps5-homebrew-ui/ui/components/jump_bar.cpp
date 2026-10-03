// ps5-homebrew-ui - Component: JumpBar.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/jump_bar.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

constexpr float kEnd = 8.0f;     // between the strip's ends and its first and last label
constexpr float kInset = 5.0f;   // between the strip's sides and the marker
constexpr float kPointer = 9.0f; // how far the bubble's pointer reaches toward the strip

bool chunky(const Theme &theme)
{
    return theme.style == SurfaceStyle::hard || theme.style == SurfaceStyle::bevel ||
           theme.style == SurfaceStyle::pixel;
}

// Languages whose controls are square: a round bubble would not belong.
bool square_language(const Theme &theme)
{
    return theme.corner != Corner::round || theme.radius < 2.0f ||
           theme.style == SurfaceStyle::bevel || theme.style == SurfaceStyle::sketch;
}

// Plain fills: the only surfaces a triangle in the same colour can extend.
bool plain_fill(const Theme &theme)
{
    return theme.corner == Corner::round &&
           (theme.style == SurfaceStyle::flat || theme.style == SurfaceStyle::soft ||
            theme.style == SurfaceStyle::outline);
}

// A filled plate in a colour, built the way the theme builds its main button.
// Returns the colour that reads on it.
Color plate(Painter &paint, const Rect &r, float radius, Color tone)
{
    const Theme &theme = paint.theme();
    if (theme.style == SurfaceStyle::glow)
    {
        paint.surface(r, radius, theme.surface, tone, 1.0f, std::max(theme.border, 2.0f));
        return tone;
    }
    const bool framed = chunky(theme);
    const bool pen = theme.style == SurfaceStyle::sketch;
    const float line = theme.button_border < 0.0f ? theme.border : theme.button_border;
    paint.surface(r, radius, tone, framed ? theme.outline : (pen ? Painter::on(tone) : tone), 1.0f,
                  framed || pen ? theme.border : line);
    return Painter::on(tone);
}

} // namespace

std::vector<JumpEntry> JumpBar::alphabet()
{
    std::vector<JumpEntry> entries;
    for (char letter = 'A'; letter <= 'Z'; ++letter)
    {
        JumpEntry entry;
        entry.label = std::string(1, letter);
        entries.push_back(entry);
    }
    return entries;
}

void JumpBar::set_entries(std::vector<JumpEntry> entries)
{
    entries_ = std::move(entries);
    current_ = std::clamp(current_, 0, std::max(static_cast<int>(entries_.size()) - 1, 0));
    position_.snap(static_cast<float>(current_));
}

void JumpBar::set_enabled(int index, bool enabled)
{
    if (index >= 0 && index < static_cast<int>(entries_.size()))
        entries_[static_cast<std::size_t>(index)].enabled = enabled;
}

int JumpBar::find(std::string_view label) const
{
    for (std::size_t i = 0; i < entries_.size(); ++i)
    {
        if (entries_[i].label == label)
            return static_cast<int>(i);
    }
    return -1;
}

const std::string &JumpBar::label() const
{
    static const std::string kNone;
    return entries_.empty() ? kNone : entries_[static_cast<std::size_t>(current_)].label;
}

void JumpBar::set_current(int index, bool snap)
{
    if (entries_.empty() || index < 0)
        return;
    current_ = std::min(index, static_cast<int>(entries_.size()) - 1);
    if (snap)
        position_.snap(static_cast<float>(current_));
}

// ---- layout ----------------------------------------------------------------

float JumpBar::pitch() const
{
    const float room = (style.vertical ? bounds_.h : bounds_.w) - 2.0f * kEnd;
    const float count = static_cast<float>(std::max<std::size_t>(entries_.size(), 1));
    return std::max(std::min(style.item_size, room / count), 1.0f);
}

Rect JumpBar::strip_rect() const
{
    const float length = pitch() * static_cast<float>(entries_.size()) + 2.0f * kEnd;
    if (style.vertical)
        return {bounds_.cx() - style.thickness * 0.5f, bounds_.cy() - length * 0.5f,
                style.thickness, length};
    return {bounds_.cx() - length * 0.5f, bounds_.cy() - style.thickness * 0.5f, length,
            style.thickness};
}

// The centre of a (fractional) label index, along the strip.
float JumpBar::along(float position) const
{
    const Rect strip = strip_rect();
    return (style.vertical ? strip.y : strip.x) + kEnd + pitch() * (position + 0.5f);
}

Rect JumpBar::item_rect(int index) const
{
    const Rect strip = strip_rect();
    const float centre = along(static_cast<float>(index));
    const float size = pitch();
    if (style.vertical)
        return {strip.x, centre - size * 0.5f, strip.w, size};
    return {centre - size * 0.5f, strip.y, size, strip.h};
}

// ---- behaviour -------------------------------------------------------------

int JumpBar::next_enabled(int from, int direction) const
{
    const int count = static_cast<int>(entries_.size());
    for (int i = from + direction; i >= 0 && i < count; i += direction)
    {
        if (entries_[static_cast<std::size_t>(i)].enabled)
            return i;
    }
    return -1;
}

Event JumpBar::step(int direction, const InputFrame &input, Feedback &feedback)
{
    if (entries_.empty() || direction == 0)
        return Event::none;
    const int count = static_cast<int>(entries_.size());
    direction = direction > 0 ? 1 : -1;
    int next = next_enabled(current_, direction);
    if (next < 0 && style.wrap && !input.nav_repeat)
    {
        next = next_enabled(direction > 0 ? -1 : count, direction);
        if (next == current_)
            next = -1;
    }
    const float x = strip_rect().cx();
    if (next < 0)
        return refuse(feedback, style, input, refusal_, x);
    current_ = next;
    linger_ = style.bubble_hold;
    if (!style.reduced_motion)
        tick_.trigger();
    const float place = count > 1 ? static_cast<float>(next) / static_cast<float>(count - 1) : 0.0f;
    play_cue(feedback, style, style.sounds.step, style.vertical ? x : item_rect(next).cx(),
             style.pitch_by_position ? tween::lerp(1.08f, 0.92f, place) : 1.0f);
    return Event::changed;
}

Event JumpBar::handle(const InputFrame &input, Feedback &feedback)
{
    exit_ = Direction::none;
    if (entries_.empty())
        return Event::none;
    const Direction forward = style.vertical ? Direction::down : Direction::right;
    const Direction backward = style.vertical ? Direction::up : Direction::left;
    if (input.nav == forward || input.nav == backward)
    {
        const int direction = input.nav == forward ? 1 : -1;
        // An end that is an exit hands the focus over instead of wrapping.
        if (next_enabled(current_, direction) < 0 && style.exits.allows(input.nav))
        {
            exit_ = input.nav;
            return Event::none;
        }
        return step(direction, input, feedback);
    }
    if (input.nav != Direction::none)
    {
        if (style.exits.allows(input.nav))
            exit_ = input.nav;
        return Event::none;
    }
    const float x = strip_rect().cx();
    if (input.is_pressed(Action::confirm))
    {
        if (!style.reduced_motion)
            tick_.trigger();
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

void JumpBar::update(float dt)
{
    position_.target = static_cast<float>(current_);
    position_.update(dt, std::max(style.omega(), 18.0f), std::max(style.damping(), 0.78f));
    linger_ = std::max(linger_ - dt, 0.0f);
    bubble_.target = style.bubble != JumpBubble::none && (focused_ || linger_ > 0.0f) ? 1.0f : 0.0f;
    bubble_.update(dt, std::max(style.omega(), 16.0f),
                   style.reduced_motion ? 1.0f : std::max(style.damping(), 0.62f));
    focus_amount_.target = focused_ ? 1.0f : 0.0f;
    focus_amount_.update(dt, 20.0f);
    refusal_.update(dt, 9.0f);
    tick_.update(dt, 9.0f);
}

void JumpBar::draw(Canvas &canvas) const
{
    if (entries_.empty())
        return;
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const Rect strip = strip_rect();
    const float size = pitch();
    const float radius = std::min(theme.radius, style.thickness * 0.5f);
    const int count = static_cast<int>(entries_.size());

    if (style.track)
        paint.well(strip, radius, theme.surface_high);

    // ---- the marker: one plate that glides under the current label ----
    // A refusal nudges it along the strip, toward the end it ran into.
    const float nudge = shake(refusal_.value, canvas.time, 7.0f);
    const float centre = along(position_.value) + nudge;
    const float grow = 2.0f * tick_.value;
    // A framed strip keeps its border clear of the marker.
    const float inset = style.track && (chunky(theme) || theme.style == SurfaceStyle::sketch)
                            ? theme.border + 3.0f
                            : kInset;
    const float across = style.thickness - 2.0f * inset;
    const Rect marker = style.vertical
                            ? Rect{strip.x + inset - grow, centre - size * 0.5f - 3.0f - grow,
                                   across + 2.0f * grow, size + 6.0f + 2.0f * grow}
                            : Rect{centre - size * 0.5f - 3.0f - grow, strip.y + inset - grow,
                                   size + 6.0f + 2.0f * grow, across + 2.0f * grow};
    const float marker_radius = std::min(theme.radius, std::min(marker.w, marker.h) * 0.5f);
    const Color tone = theme.accent;
    Color on_marker = Painter::on(tone);
    if (theme.style == SurfaceStyle::glow)
    {
        // On lit consoles the edge carries the colour, not the fill.
        list.glow(marker, marker_radius, 10.0f, tone.with_alpha(0.4f));
        paint.fill(marker, marker_radius, theme.surface);
        paint.stroke(marker, marker_radius, 2.0f, tone);
        on_marker = tone;
    }
    else
    {
        paint.fill(marker, marker_radius, tone);
        if (chunky(theme) || theme.style == SurfaceStyle::sketch)
            paint.stroke(marker, marker_radius, std::min(theme.border, 3.0f), theme.outline);
    }

    // ---- the labels ----
    const Color resting =
        (style.track || !style.on_page ? theme.text : paint.page_text()).with_alpha(0.8f);
    for (int i = 0; i < count; ++i)
    {
        const JumpEntry &entry = entries_[static_cast<std::size_t>(i)];
        const float near =
            tween::clamp01(1.0f - std::fabs(position_.value - static_cast<float>(i)));
        const float text = style.text_size * (1.0f + (style.magnify - 1.0f) * near);
        Color ink = gfx::mix(resting, on_marker, near);
        if (!entry.enabled)
            ink = ink.with_alpha(0.32f);
        const Rect cell = item_rect(i);
        paint.label(entry.label, cell.cx(), cell.cy() + text * 0.35f, text, ink,
                    gfx::Align::center);
    }
    if (style.focus_ring)
        paint.focus_ring(strip, radius, focus_amount_.value);

    // ---- the bubble: the current label, large, beside the strip ----
    const float shown = tween::clamp01(bubble_.value);
    if (style.bubble == JumpBubble::none || shown <= 0.01f)
        return;
    const std::string &label = entries_[static_cast<std::size_t>(current_)].label;
    const float side = style.bubble_size;
    const float width = std::max(side, paint.label_width(label, style.bubble_text) + 36.0f);
    const bool before = style.bubble == JumpBubble::before;
    const float at = along(position_.value);
    Rect bubble;
    float tip_x = 0.0f;
    float tip_y = 0.0f;
    float turn = 0.0f; // the pointer's direction: clockwise from "up"
    if (style.vertical)
    {
        // The bubble follows the letter but stays inside the bar's bounds.
        const float cy =
            std::clamp(at, bounds_.y + side * 0.5f,
                       std::max(bounds_.y + bounds_.h - side * 0.5f, bounds_.y + side * 0.5f));
        const float x =
            before ? strip.x - style.bubble_gap - width : strip.x + strip.w + style.bubble_gap;
        bubble = {x, cy - side * 0.5f, width, side};
        tip_x = before ? x + width + kPointer * 0.5f - 1.0f : x - kPointer * 0.5f + 1.0f;
        tip_y = std::clamp(at, bubble.y + side * 0.3f, bubble.y + side * 0.7f);
        turn = before ? 1.5707963f : -1.5707963f;
    }
    else
    {
        const float cx =
            std::clamp(at, bounds_.x + width * 0.5f,
                       std::max(bounds_.x + bounds_.w - width * 0.5f, bounds_.x + width * 0.5f));
        const float y =
            before ? strip.y - style.bubble_gap - side : strip.y + strip.h + style.bubble_gap;
        bubble = {cx - width * 0.5f, y, width, side};
        tip_x = std::clamp(at, bubble.x + width * 0.3f, bubble.x + width * 0.7f);
        tip_y = before ? y + side + kPointer * 0.5f - 1.0f : y - kPointer * 0.5f + 1.0f;
        turn = before ? 3.14159265f : 0.0f;
    }
    // It grows out of the strip, from the side that faces it.
    const float scale = style.reduced_motion ? 1.0f : tween::lerp(0.6f, 1.0f, bubble_.value);
    list.push_opacity(shown);
    list.push_transform(scale, tip_x, tip_y, 0.0f, 0.0f);
    const Color fill = theme.primary;
    if (plain_fill(theme))
        list.triangle({tip_x - 9.0f, tip_y - kPointer * 0.5f, 18.0f, kPointer},
                      Color{fill.r, fill.g, fill.b, 1.0f}, 0.0f, turn);
    const float bubble_radius =
        square_language(theme) ? std::min(theme.radius, side * 0.3f) : side * 0.5f;
    const Color ink = plate(paint, bubble, bubble_radius, Color{fill.r, fill.g, fill.b, 1.0f});
    paint.label(label, bubble.cx(), bubble.cy() + style.bubble_text * 0.35f, style.bubble_text, ink,
                gfx::Align::center);
    list.pop_transform();
    list.pop_opacity();
}

} // namespace hui::ui
