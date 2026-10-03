// ps5-homebrew-ui - Components: Badge, Chip, Avatar and AvatarStack.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/badge.hpp"

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

const Color kWhite{1.0f, 1.0f, 1.0f, 1.0f};

bool stroke_only(const Theme &theme)
{
    return theme.style == SurfaceStyle::outline || theme.style == SurfaceStyle::glow;
}

// A pop: the value is thrown above 1 and springs back, ringing a little.
// With reduced motion nothing moves, so nothing is thrown.
void pop(tween::Bounce &scale, const ComponentStyle &style, float strength)
{
    if (style.reduced_motion || strength <= 0.0f)
        return;
    scale.value = 1.0f + 0.3f * strength;
    scale.velocity = 0.0f;
}

void settle(tween::Bounce &scale, float dt, const ComponentStyle &style)
{
    scale.update(dt, std::clamp(style.omega(), 14.0f, 40.0f),
                 style.reduced_motion ? 1.0f : std::min(style.theme.damping, 0.5f));
}

float aligned_x(const Rect &bounds, float width, gfx::Align align)
{
    if (align == gfx::Align::left)
        return bounds.x;
    if (align == gfx::Align::right)
        return bounds.x + bounds.w - width;
    return bounds.cx() - width * 0.5f;
}

Color hsv(float hue, float saturation, float value)
{
    const float h = hue / 60.0f;
    const float c = value * saturation;
    const float x = c * (1.0f - std::fabs(std::fmod(h, 2.0f) - 1.0f));
    const float m = value - c;
    float r = 0.0f, g = 0.0f, b = 0.0f;
    switch (static_cast<int>(h) % 6)
    {
    case 0:
        r = c, g = x;
        break;
    case 1:
        r = x, g = c;
        break;
    case 2:
        g = c, b = x;
        break;
    case 3:
        g = x, b = c;
        break;
    case 4:
        r = x, b = c;
        break;
    default:
        r = c, b = x;
        break;
    }
    return {r + m, g + m, b + m, 1.0f};
}

// One avatar, shared by Avatar and AvatarStack.
void draw_avatar(Canvas &canvas, Painter &paint, const AvatarStyle &style, const Rect &r,
                 std::string_view initials, Color color, std::uint32_t image, const Rect &uv)
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    const bool circle = style.shape == AvatarShape::circle;
    const float radius = circle ? r.w * 0.5f : std::min(theme.radius_card, r.w * 0.28f);
    const Color backing = style.backing.a > 0.0f ? style.backing : solid_surface(theme);
    if (style.cutout > 0.0f)
    {
        const Rect rim = r.inset(-style.cutout);
        if (circle)
            list.circle(rim.cx(), rim.cy(), rim.w * 0.5f, backing);
        else
            paint.fill(rim, radius + style.cutout, backing);
    }
    if (image != 0)
    {
        list.image(image, r, uv, kWhite, radius);
    }
    else
    {
        if (circle)
            list.circle(r.cx(), r.cy(), radius, color);
        else
            paint.fill(r, radius, color);
        const float size = r.w * style.text_scale;
        paint.label(initials, r.cx(), r.cy() + size * 0.35f, size, Painter::on(color),
                    gfx::Align::center);
    }
    const float line = std::min(theme.border, 3.0f);
    if (line > 0.0f && theme.outline.a > 0.0f)
    {
        if (circle)
            list.ring(r.cx(), r.cy(), radius, line, theme.outline);
        else
            paint.stroke(r, radius, line, theme.outline);
    }
}

} // namespace

// ---- Badge -----------------------------------------------------------------

void Badge::bump()
{
    pop(scale_, style, style.pop);
}

void Badge::set_count(int count)
{
    count = std::max(count, 0);
    if (count == count_)
        return;
    count_ = count;
    if (text_.empty())
        bump();
}

void Badge::set_text(std::string_view text)
{
    if (text == text_)
        return;
    text_ = std::string(text);
    bump();
}

std::string Badge::text() const
{
    if (!text_.empty())
        return text_;
    char number[16];
    if (count_ > style.max_count)
        std::snprintf(number, sizeof(number), "%d+", style.max_count);
    else
        std::snprintf(number, sizeof(number), "%d", count_);
    return number;
}

bool Badge::visible() const
{
    return style.dot || !text_.empty() || count_ > 0 || !style.hide_zero;
}

float Badge::width(const Painter &paint) const
{
    if (style.dot)
        return style.dot_size;
    return std::max(style.height,
                    paint.label_width(text(), style.text_size) + 2.0f * style.padding);
}

void Badge::update(float dt)
{
    settle(scale_, dt, style);
    shown_.target = visible() ? 1.0f : 0.0f;
    // The first frame shows the badge as it is; only later changes animate.
    if (!started_)
        shown_.snap(shown_.target);
    started_ = true;
    shown_.update(dt, std::max(style.omega(), 16.0f));
    phase_ = std::fmod(phase_ + dt, 3600.0f);
}

void Badge::draw(Canvas &canvas) const
{
    const float shown = started_ ? tween::clamp01(shown_.value) : (visible() ? 1.0f : 0.0f);
    if (shown <= 0.01f)
        return;
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const Color tone = status_color(theme, style.kind);
    const Color backing = style.backing.a > 0.0f ? style.backing : solid_surface(theme);
    const bool neutral = style.kind == Status::neutral;

    const float w = width(paint);
    const float h = style.dot ? style.dot_size : style.height;
    const Rect pill{aligned_x(bounds_, w, style.align), bounds_.cy() - h * 0.5f, w, h};
    // A dot is round wherever the theme has round corners at all.
    const bool round_dot = style.dot && theme.corner == Corner::round && theme.radius >= 2.0f;
    const float radius =
        theme.pill_chips || round_dot ? h * 0.5f : std::min(theme.radius, h * 0.5f);

    // It grows from nothing when it appears and bounces when its value changes.
    const float scale =
        std::max(scale_.value, 0.0f) * (style.reduced_motion ? 1.0f : 0.5f + 0.5f * shown);
    list.push_opacity(shown);
    list.push_transform(scale, pill.cx(), pill.cy(), 0.0f, 0.0f);

    if (style.dot && style.pulse && !style.reduced_motion)
    {
        const float t = phase_ / 1.6f - std::floor(phase_ / 1.6f);
        const float reach = h * 0.5f * (1.0f + 1.3f * tween::cubic_out(t));
        list.circle(pill.cx(), pill.cy(), reach, tone.with_alpha(0.45f * (1.0f - t)));
    }
    if (style.cutout > 0.0f)
        paint.fill(pill.inset(-style.cutout), radius + style.cutout, backing);
    if (theme.style == SurfaceStyle::glow && !neutral)
        list.glow(pill, radius, 10.0f, tone.with_alpha(0.45f));

    Color ink = theme.text;
    const float line = std::min(theme.border, 3.0f);
    switch (style.fill)
    {
    case BadgeFill::solid:
        paint.fill(pill, radius, neutral ? theme.surface_high : tone);
        paint.stroke(pill, radius, line, theme.outline);
        ink = neutral ? theme.text : status_ink(theme, style.kind);
        break;
    case BadgeFill::tinted:
        paint.fill(pill, radius, gfx::mix(backing, tone, 0.22f));
        paint.stroke(pill, radius, std::max(line, 1.5f), tone);
        break;
    case BadgeFill::outline:
        paint.fill(pill, radius, backing);
        paint.stroke(pill, radius, std::max(line, 2.0f), tone);
        break;
    }
    if (!style.dot)
        paint.label(text(), pill.cx(), pill.cy() + style.text_size * 0.35f, style.text_size, ink,
                    gfx::Align::center);

    list.pop_transform();
    list.pop_opacity();
}

// ---- Chip ------------------------------------------------------------------

namespace
{
constexpr float kChipDot = 10.0f;
constexpr float kChipCross = 10.0f;
constexpr float kChipGap = 9.0f;
} // namespace

void Chip::set_selected(bool selected, bool snap)
{
    selected_.target = selected ? 1.0f : 0.0f;
    if (snap)
        selected_.snap(selected_.target);
}

void Chip::set_focused(bool focused)
{
    focus_.target = focused ? 1.0f : 0.0f;
}

float Chip::width(const Painter &paint) const
{
    float width = paint.label_width(label, style.text_size) + 2.0f * style.padding;
    if (style.leading_dot)
        width += kChipDot + kChipGap;
    if (style.removable)
        width += kChipCross + kChipGap + 2.0f;
    return width;
}

void Chip::update(float dt)
{
    selected_.update(dt, std::min(style.omega(), 60.0f), style.damping());
    focus_.update(dt, std::max(style.omega(), 18.0f));
}

void Chip::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const float selected = tween::clamp01(selected_.value);
    const float w = width(paint);
    const Rect r{aligned_x(bounds_, w, style.align), bounds_.cy() - style.height * 0.5f, w,
                 style.height};
    const float radius = theme.pill_chips ? r.h * 0.5f : paint.control_radius(r);
    const bool strokes = stroke_only(theme);
    const Color body =
        strokes ? theme.surface_high : gfx::mix(theme.surface_high, theme.accent, selected);
    const Color edge = strokes ? gfx::mix(theme.outline, theme.accent, selected) : theme.outline;
    // Pills stay round whatever corner type the language uses, as the
    // painter's own chips do.
    if (theme.pill_chips)
    {
        list.rounded_rect(r, radius, body);
        if (theme.border > 0.0f)
            list.bordered_rect(r, radius, Color{0.0f, 0.0f, 0.0f, 0.0f}, theme.border, edge);
    }
    else
    {
        paint.fill(r, radius, body);
        paint.stroke(r, radius, std::min(theme.border, 3.0f), edge);
    }
    const Color ink = strokes ? gfx::mix(theme.text_muted, theme.accent, selected)
                              : gfx::mix(theme.text, Painter::on(theme.accent), selected);

    float x = r.x + style.padding;
    if (style.leading_dot)
    {
        const Color dot = status_color(theme, style.dot);
        if (theme.corner == Corner::round && theme.radius >= 2.0f)
            list.circle(x + kChipDot * 0.5f, r.cy(), kChipDot * 0.5f, dot);
        else
            list.rounded_rect({x, r.cy() - kChipDot * 0.5f, kChipDot, kChipDot}, 0.0f, dot);
        x += kChipDot + kChipGap;
    }
    x += paint.label(label, x, r.cy() + style.text_size * 0.35f, style.text_size, ink);
    if (style.removable)
    {
        // The cross is drawn, not typed: not every face has the glyph.
        const float cx = x + kChipGap + kChipCross * 0.5f;
        const float half = kChipCross * 0.5f - 1.0f;
        const Color quiet = ink.with_alpha(0.7f);
        list.line(cx - half, r.cy() - half, cx + half, r.cy() + half, 2.2f, quiet);
        list.line(cx - half, r.cy() + half, cx + half, r.cy() - half, 2.2f, quiet);
    }
    paint.focus_ring(r, radius, focus_.value);
}

// ---- Avatar ----------------------------------------------------------------

std::string Avatar::initials_of(std::string_view name)
{
    // The first character of the first and of the last word, whole UTF-8
    // characters, ASCII letters in capitals.
    const auto letter = [&](std::size_t at)
    {
        std::string out(1, name[at]);
        for (std::size_t i = at + 1;
             i < name.size() && (static_cast<unsigned char>(name[i]) & 0xc0) == 0x80; ++i)
            out.push_back(name[i]);
        if (out[0] >= 'a' && out[0] <= 'z')
            out[0] = static_cast<char>(out[0] - 'a' + 'A');
        return out;
    };
    std::size_t first = std::string_view::npos;
    std::size_t last = std::string_view::npos;
    for (std::size_t i = 0; i < name.size(); ++i)
    {
        if (name[i] != ' ' && (i == 0 || name[i - 1] == ' '))
        {
            if (first == std::string_view::npos)
                first = i;
            else
                last = i;
        }
    }
    if (first == std::string_view::npos)
        return "?";
    std::string out = letter(first);
    if (last != std::string_view::npos)
        out += letter(last);
    return out;
}

Color Avatar::color_of(std::string_view name, float saturation, float brightness)
{
    // FNV-1a: small, stable across platforms, and spreads similar names.
    std::uint32_t hash = 2166136261u;
    for (char c : name)
    {
        hash ^= static_cast<unsigned char>(c);
        hash *= 16777619u;
    }
    return hsv(static_cast<float>(hash % 360u), saturation, brightness);
}

void Avatar::set_name(std::string_view name)
{
    name_ = std::string(name);
    initials_ = initials_of(name);
}

void Avatar::set_image(std::uint32_t texture, const Rect &uv)
{
    image_ = texture;
    uv_ = uv;
}

void Avatar::set_presence(Presence presence)
{
    if (presence == presence_)
        return;
    presence_ = presence;
    pop(dot_, style, 1.0f);
}

void Avatar::update(float dt)
{
    settle(dot_, dt, style);
}

void Avatar::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const float size = style.size > 0.0f ? style.size : std::min(bounds_.w, bounds_.h);
    if (size < 4.0f)
        return;
    const Rect r{bounds_.cx() - size * 0.5f, bounds_.cy() - size * 0.5f, size, size};
    draw_avatar(canvas, paint, style, r, initials_,
                color_of(name_, style.saturation, style.brightness), image_, uv_);
    if (presence_ == Presence::none)
        return;

    // On a circle the dot sits on the rim at 45 degrees; on a square, in the
    // corner.
    const float dot = std::max(size * style.status_scale, 10.0f) * std::max(dot_.value, 0.0f);
    const float reach =
        style.shape == AvatarShape::circle ? size * 0.5f * 0.7071f : size * 0.5f - dot * 0.2f;
    const float cx = r.cx() + reach;
    const float cy = r.cy() + reach;
    const Color backing = style.backing.a > 0.0f ? style.backing : solid_surface(theme);
    Color tone = theme.text_muted;
    if (presence_ == Presence::online)
        tone = theme.success;
    else if (presence_ == Presence::away)
        tone = theme.warning;
    else if (presence_ == Presence::busy)
        tone = theme.danger;
    list.circle(cx, cy, dot * 0.5f + 3.0f, backing);
    if (presence_ == Presence::offline)
        list.ring(cx, cy, dot * 0.5f, std::max(dot * 0.2f, 2.0f), tone);
    else
        list.circle(cx, cy, dot * 0.5f, tone);
}

// ---- AvatarStack -----------------------------------------------------------

void AvatarStack::set_people(std::vector<AvatarPerson> people)
{
    const int before = hidden();
    people_ = std::move(people);
    if (hidden() != before)
        pop(more_, style, 1.0f);
}

int AvatarStack::hidden() const
{
    return std::max(static_cast<int>(people_.size()) - std::max(style.max_shown, 0), 0);
}

float AvatarStack::diameter() const
{
    return style.size > 0.0f ? style.size : bounds_.h;
}

float AvatarStack::width() const
{
    const int extra = hidden();
    const int slots = static_cast<int>(people_.size()) - extra + (extra > 0 ? 1 : 0);
    if (slots <= 0)
        return 0.0f;
    const float size = diameter();
    return size + size * (1.0f - style.overlap) * static_cast<float>(slots - 1);
}

void AvatarStack::update(float dt)
{
    settle(more_, dt, style);
}

void AvatarStack::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const float size = diameter();
    if (size < 4.0f || people_.empty())
        return;
    const float step = size * (1.0f - style.overlap);
    const int extra = hidden();
    const int shown = static_cast<int>(people_.size()) - extra;
    // Every avatar wears a rim in the backing colour, so each one reads as
    // cut out of its neighbour.
    AvatarStyle each = style;
    each.cutout = std::max(style.cutout, 3.0f);
    float x = bounds_.x;
    const float y = bounds_.cy() - size * 0.5f;
    for (int i = 0; i < shown; ++i)
    {
        const AvatarPerson &person = people_[static_cast<std::size_t>(i)];
        draw_avatar(canvas, paint, each, {x, y, size, size}, Avatar::initials_of(person.name),
                    Avatar::color_of(person.name, style.saturation, style.brightness), person.image,
                    person.uv);
        x += step;
    }
    if (extra <= 0)
        return;
    char more[16];
    std::snprintf(more, sizeof(more), "+%d", std::min(extra, 99));
    const Rect r{x, y, size, size};
    const bool circle = style.shape == AvatarShape::circle;
    const float radius = circle ? size * 0.5f : std::min(theme.radius_card, size * 0.28f);
    const Color backing = style.backing.a > 0.0f ? style.backing : solid_surface(theme);
    list.push_transform(std::max(more_.value, 0.0f), r.cx(), r.cy(), 0.0f, 0.0f);
    if (circle)
    {
        list.circle(r.cx(), r.cy(), radius + each.cutout, backing);
        list.circle(r.cx(), r.cy(), radius, theme.surface_high);
        list.ring(r.cx(), r.cy(), radius, std::clamp(theme.border, 1.5f, 3.0f),
                  theme.text_muted.with_alpha(0.5f));
    }
    else
    {
        paint.fill(r.inset(-each.cutout), radius + each.cutout, backing);
        paint.fill(r, radius, theme.surface_high);
        paint.stroke(r, radius, std::clamp(theme.border, 1.5f, 3.0f),
                     theme.text_muted.with_alpha(0.5f));
    }
    const float text = size * 0.34f;
    paint.label(more, r.cx(), r.cy() + text * 0.35f, text, theme.text, gfx::Align::center);
    list.pop_transform();
}

} // namespace hui::ui
