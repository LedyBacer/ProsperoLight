// ps5-homebrew-ui - Component: TextField.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/text_field.hpp"

#include "ui/components/focus_frame.hpp"

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

// Painter::field sets its text at this size; the field measures with it.
constexpr float kTextSize = 24.0f;

bool continuation(char c)
{
    return (static_cast<unsigned char>(c) & 0xc0) == 0x80;
}

int characters(std::string_view text)
{
    int count = 0;
    for (char c : text)
        count += continuation(c) ? 0 : 1;
    return count;
}

void drop_last(std::string &text)
{
    while (!text.empty() && continuation(text.back()))
        text.pop_back();
    if (!text.empty())
        text.pop_back();
}

void drop_first(std::string &text)
{
    std::size_t next = 1;
    while (next < text.size() && continuation(text[next]))
        ++next;
    text.erase(0, next);
}

// The first code point of a UTF-8 string (0 when it is empty or malformed).
std::uint32_t first_codepoint(std::string_view text)
{
    if (text.empty())
        return 0;
    const auto byte = [&](std::size_t i) {
        return i < text.size() ? static_cast<std::uint32_t>(static_cast<unsigned char>(text[i]))
                               : 0;
    };
    const std::uint32_t lead = byte(0);
    if (lead < 0x80)
        return lead;
    if ((lead & 0xe0) == 0xc0)
        return ((lead & 0x1f) << 6) | (byte(1) & 0x3f);
    if ((lead & 0xf0) == 0xe0)
        return ((lead & 0x0f) << 12) | ((byte(1) & 0x3f) << 6) | (byte(2) & 0x3f);
    if ((lead & 0xf8) == 0xf0)
        return ((lead & 0x07) << 18) | ((byte(1) & 0x3f) << 12) | ((byte(2) & 0x3f) << 6) |
               (byte(3) & 0x3f);
    return 0;
}

// The face Painter::body draws with in this theme, or null for the built-in
// bitmap letters, which have no symbols at all.
const gfx::Font *body_face(const Fonts &fonts, const Theme &theme)
{
    switch (theme.label)
    {
    case FontRole::pixel:
        return fonts.pixel.font;
    case FontRole::hand:
        return fonts.hand.font != nullptr ? fonts.hand.font : fonts.regular.font;
    case FontRole::mono:
        return fonts.mono.font;
    default:
        return fonts.regular.font;
    }
}

} // namespace

void TextField::set_error(std::string error)
{
    const bool fresh = !error.empty() && error != error_;
    error_ = std::move(error);
    if (fresh && !style.reduced_motion)
        shake_.trigger();
}

void TextField::set_text(std::string_view text)
{
    text_.clear();
    insert(text);
    blink_ = 0.0f;
}

int TextField::length() const
{
    return characters(text_);
}

bool TextField::insert(char c)
{
    // Control characters have no place in a one-line field; bytes above 127
    // are only meaningful as part of a UTF-8 string.
    if (c < 0x20 || c == 0x7f || full())
        return false;
    text_.push_back(c);
    blink_ = 0.0f;
    return true;
}

bool TextField::insert(std::string_view utf8)
{
    bool changed = false;
    std::size_t at = 0;
    while (at < utf8.size() && !full())
    {
        std::size_t next = at + 1;
        while (next < utf8.size() && continuation(utf8[next]))
            ++next;
        if (static_cast<unsigned char>(utf8[at]) >= 0x20 && utf8[at] != 0x7f)
        {
            text_.append(utf8.substr(at, next - at));
            changed = true;
        }
        at = next;
    }
    if (changed)
        blink_ = 0.0f;
    return changed;
}

bool TextField::backspace()
{
    if (text_.empty())
        return false;
    drop_last(text_);
    blink_ = 0.0f;
    return true;
}

void TextField::clear()
{
    text_.clear();
    blink_ = 0.0f;
}

Event TextField::insert(char c, Feedback &feedback)
{
    const float x = bounds_.cx();
    if (!insert(c))
        return refuse(feedback, style, InputFrame{}, shake_, x);
    // A little higher with every character: a line being typed sounds like
    // it is going somewhere.
    const float along = style.max_length > 0
                            ? static_cast<float>(length()) / static_cast<float>(style.max_length)
                            : 0.5f;
    play_cue(feedback, style, style.type, x, tween::lerp(0.96f, 1.08f, tween::clamp01(along)));
    return Event::changed;
}

Event TextField::backspace(Feedback &feedback)
{
    const float x = bounds_.cx();
    if (!backspace())
        return refuse(feedback, style, InputFrame{}, shake_, x);
    play_cue(feedback, style, style.erase, x);
    return Event::changed;
}

float TextField::preferred_height() const
{
    float height = style.field_height;
    if (!label_.empty())
        height += style.label_size + style.label_gap;
    if (!helper_.empty() || !error_.empty())
        height += style.helper_gap + style.helper_size * 1.2f;
    return height;
}

Rect TextField::field_rect() const
{
    const float top = label_.empty() ? 0.0f : style.label_size + style.label_gap;
    return {bounds_.x, bounds_.y + top, bounds_.w, style.field_height};
}

void TextField::set_active(bool active)
{
    if (active && !active_)
        blink_ = 0.0f;
    active_ = active;
}

Event TextField::handle(const InputFrame &input, Feedback &feedback)
{
    const float x = bounds_.cx();
    if (input.is_pressed(Action::confirm))
    {
        if (disabled_)
            return refuse(feedback, style, input, shake_, x);
        press_.trigger();
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

void TextField::update(float dt)
{
    blink_ += dt;
    focus_.target = active_ ? 1.0f : 0.0f;
    focus_.update(dt, 18.0f);
    error_amount_.target = error_.empty() ? 0.0f : 1.0f;
    error_amount_.update(dt, 16.0f);
    shake_.update(dt, 7.0f);
    press_.update(dt, 10.0f);
}

// The text as the field shows it: masked when it is a password, and cut from
// the front when it is longer than the field, so the caret stays in sight.
std::string TextField::shown(const Painter &paint, float room, std::string_view mask) const
{
    std::string display = text_;
    if (style.password)
    {
        display.clear();
        for (int i = 0, count = length(); i < count; ++i)
            display += mask;
    }
    while (!display.empty() && paint.body_width(display, kTextSize) > room)
        drop_first(display);
    return display;
}

void TextField::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const float focus = focus_.value;
    const float wrong = error_amount_.value;
    const Color ink = style.on_page ? paint.page_text() : theme.text;
    const Color quiet = style.on_page ? paint.page_text_muted() : theme.text_muted;

    list.push_transform(1.0f, 0.0f, 0.0f, shake(shake_.value, canvas.time, 9.0f), 0.0f);
    list.push_opacity(disabled_ ? 0.45f : 1.0f);

    if (!label_.empty())
    {
        const float baseline = bounds_.y + style.label_size * 0.82f;
        float room = bounds_.w;
        if (style.counter && style.max_length > 0)
        {
            char count[32];
            std::snprintf(count, sizeof(count), "%d / %d", length(), style.max_length);
            const float size = style.label_size * 0.9f;
            const float width = paint.label(count, bounds_.x + bounds_.w, baseline, size,
                                            full() ? gfx::mix(quiet, theme.warning, 0.8f) : quiet,
                                            gfx::Align::right);
            room -= width + 18.0f;
        }
        paint.label(fit_label(paint, label_, style.label_size, room), bounds_.x, baseline,
                    style.label_size, gfx::mix(quiet, ink, focus));
    }

    const Rect field = field_rect();
    const bool line = theme.underline_fields;
    const float room = field.w - (line ? 14.0f : 46.0f);
    // The mask falls back to an asterisk in faces that have no bullet.
    const gfx::Font *face = body_face(canvas.fonts, theme);
    const bool has_mask = face != nullptr && face->has_glyph(first_codepoint(style.mask));
    const std::string value = shown(paint, room, has_mask ? std::string_view(style.mask) : "*");
    const bool lit = style.caret_period <= 0.0f || style.reduced_motion ||
                     std::fmod(blink_, style.caret_period) < style.caret_period * 0.5f;
    Look look;
    look.focus = focus;
    look.press = press_.value;
    if (!line && focus_frame_goes_under(theme))
    {
        // Painter::field draws its ring last; here it must come first.
        focus_frame(canvas, theme, field, std::min(theme.radius, 12.0f), focus, true);
        look.focus = 0.0f;
    }
    paint.field(field, value, active_ && !disabled_ && lit, look);

    if (text_.empty() && !placeholder_.empty())
    {
        // Painter::field puts ink on a white face in the bevel and hard looks.
        const bool white =
            !line && (theme.style == SurfaceStyle::bevel || theme.style == SurfaceStyle::hard);
        const Color faint =
            white ? Painter::on(theme.light).with_alpha(0.5f) : (line ? quiet : theme.text_muted);
        // It steps aside for the caret instead of sitting under it.
        const float x = field.x + (line ? 2.0f : 20.0f) + 12.0f * focus;
        paint.body(fit_body(paint, placeholder_, kTextSize, room - 12.0f), x,
                   field.cy() + kTextSize * 0.35f, kTextSize, faint);
    }

    if (wrong > 0.01f)
    {
        const Color danger = theme.danger.with_alpha(wrong);
        if (line)
            list.rounded_rect({field.x, field.y + field.h - 4.0f, field.w, 4.0f}, 0.0f, danger);
        else
            paint.stroke(field, std::min(theme.radius, 12.0f), std::max(theme.border, 2.5f),
                         danger);
    }

    const float baseline = field.y + field.h + style.helper_gap + style.helper_size * 0.82f;
    if (!error_.empty())
        paint.body(fit_body(paint, error_, style.helper_size, bounds_.w), bounds_.x, baseline,
                   style.helper_size, theme.danger.with_alpha(wrong));
    if (!helper_.empty() && wrong < 0.99f)
        paint.body(fit_body(paint, helper_, style.helper_size, bounds_.w), bounds_.x, baseline,
                   style.helper_size, quiet.with_alpha(1.0f - wrong));

    list.pop_opacity();
    list.pop_transform();
}

} // namespace hui::ui
