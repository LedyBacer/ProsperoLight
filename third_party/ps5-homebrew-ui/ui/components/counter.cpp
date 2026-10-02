// ps5-homebrew-ui - Component: Counter.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/counter.hpp"

#include "ui/pixel_font.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string_view>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

// The painter measures labels but not headings; this mirrors how
// Painter::heading() picks a face and a size.
float heading_width(const Painter &paint, std::string_view text, float size)
{
    const Theme &theme = paint.theme();
    if (theme.heading == FontRole::pixel)
    {
        const FontRef &pixel = paint.font(FontRole::pixel);
        // Without the bitmap face the painter falls back to the mono one
        // here, and heading() draws the 5x7 rectangle face instead.
        if (pixel.font == paint.font(FontRole::mono).font)
            return pixel_text_width(text, size * 0.66f);
        return pixel.measure(text, Painter::pixel_em(size * 0.7f));
    }
    if (theme.heading == FontRole::hand)
        return paint.font(theme.heading).measure(text, size * 1.16f);
    return paint.font(theme.heading).measure(text, size);
}

float glyph_width(const Painter &paint, CounterFace face, char c, float size)
{
    if (c == ' ')
        return 0.0f;
    const char text[2] = {c, '\0'};
    return face == CounterFace::heading ? heading_width(paint, text, size)
                                        : paint.label_width(text, size);
}

// A digit's cell is as wide as the widest digit, so "1" and "8" take the
// same room and a changing number keeps its width.
float digit_width(const Painter &paint, CounterFace face, float size)
{
    float widest = 0.0f;
    for (char c = '0'; c <= '9'; ++c)
        widest = std::max(widest, glyph_width(paint, face, c, size));
    return widest;
}

bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

} // namespace

void Counter::set_value(double value, bool snap)
{
    to_ = value;
    if (snap)
    {
        shown_ = value;
        velocity_ = 0.0;
    }
}

double Counter::shown() const
{
    return shown_;
}

double Counter::units(double value) const
{
    if (style.format == CounterFormat::decimal || style.format == CounterFormat::percent)
        return value * std::pow(10.0, std::clamp(style.decimals, 0, 6));
    return value;
}

std::string Counter::format_units(long long units) const
{
    const bool negative = units < 0;
    // Negated as unsigned: the most negative value has no positive twin.
    const unsigned long long n = negative ? 0ULL - static_cast<unsigned long long>(units)
                                          : static_cast<unsigned long long>(units);
    const auto grouped = [&](unsigned long long value)
    {
        char digits[24];
        const int length = std::snprintf(digits, sizeof(digits), "%llu", value);
        std::string out;
        for (int i = 0; i < length; ++i)
        {
            if (i > 0 && (length - i) % 3 == 0 && style.separator != '\0')
                out.push_back(style.separator);
            out.push_back(digits[i]);
        }
        return out;
    };

    std::string out;
    char buffer[48];
    switch (style.format)
    {
    case CounterFormat::integer:
        out = grouped(n);
        break;
    case CounterFormat::decimal:
    case CounterFormat::percent:
    {
        const int decimals = std::clamp(style.decimals, 0, 6);
        unsigned long long scale = 1;
        for (int i = 0; i < decimals; ++i)
            scale *= 10;
        out = grouped(n / scale);
        if (decimals > 0)
        {
            std::snprintf(buffer, sizeof(buffer), "%0*llu", decimals, n % scale);
            out.push_back(style.point);
            out += buffer;
        }
        if (style.format == CounterFormat::percent)
            out.push_back('%');
        break;
    }
    case CounterFormat::time:
        if (n >= 3600)
            std::snprintf(buffer, sizeof(buffer), "%llu:%02llu:%02llu", n / 3600, (n / 60) % 60,
                          n % 60);
        else
            std::snprintf(buffer, sizeof(buffer), "%02llu:%02llu", n / 60, n % 60);
        out = buffer;
        break;
    }
    return negative ? "-" + out : out;
}

std::string Counter::format(double value) const
{
    return format_units(std::llround(units(value)));
}

std::string Counter::text() const
{
    const bool rolls = style.rolling && !style.reduced_motion;
    const double goal = units(to_);
    const double at = units(shown()) + (rolls ? std::round(goal) - goal : 0.0);
    return format_units(rolls ? static_cast<long long>(std::floor(at + 1e-9)) : std::llround(at));
}

float Counter::width(const Painter &paint) const
{
    const float digit = digit_width(paint, style.face, style.size);
    float total = 0.0f;
    for (char c : format(to_))
        total += is_digit(c) ? digit : glyph_width(paint, style.face, c, style.size);
    const float small = style.size * style.affix_scale;
    if (!style.prefix.empty())
        total += paint.label_width(style.prefix, small) + style.size * 0.1f;
    if (!style.suffix.empty())
        total += paint.label_width(style.suffix, small) + style.size * 0.1f;
    return total;
}

void Counter::update(float dt)
{
    // The exact solution of the critically damped oscillator over dt, as in
    // tween::Spring.
    const double omega = std::clamp(style.omega() * style.rate, 2.0f, 60.0f);
    const double step = dt;
    const double x = shown_ - to_;
    const double e = std::exp(-omega * step);
    const double next = (x + (velocity_ + omega * x) * step) * e;
    velocity_ = (velocity_ - omega * (velocity_ + omega * x) * step) * e;
    shown_ = to_ + next;
    // Close enough that no digit can differ: stop, so the text is exact.
    const double unit = 1.0 / std::max(units(1.0), 1.0);
    if (std::fabs(next) < unit * 0.004 && std::fabs(velocity_) < unit * 0.05)
    {
        shown_ = to_;
        velocity_ = 0.0;
    }
}

void Counter::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const Color ink = style.color.a > 0.0f ? style.color : theme.text;
    const float size = style.size;
    const float digit = digit_width(paint, style.face, size);
    const auto cell = [&](char c)
    { return is_digit(c) ? digit : glyph_width(paint, style.face, c, size); };

    // An odometer shows the number it is leaving and the next one, each
    // digit that differs between them part-way through its roll. Formatting
    // both and comparing them serves every format: a carry, a new thousands
    // separator and the seconds of a clock all fall out of it.
    const bool rolls = style.rolling && !style.reduced_motion;
    // Shifted so that the roll ends on the rounded target: a target between
    // two digits must not leave the wheels resting half-way.
    const double goal = units(to_);
    const double at = units(shown()) + (rolls ? std::round(goal) - goal : 0.0);
    const long long whole =
        rolls ? static_cast<long long>(std::floor(at + 1e-9)) : std::llround(at);
    const float part =
        rolls ? tween::clamp01(static_cast<float>(at - static_cast<double>(whole))) : 0.0f;
    const bool moving = part > 0.001f;
    std::string from = format_units(whole);
    std::string to = moving ? format_units(whole + 1) : from;
    if (from.size() < to.size())
        from.insert(0, to.size() - from.size(), ' ');
    if (to.size() < from.size())
        to.insert(0, from.size() - to.size(), ' ');

    const float small = size * style.affix_scale;
    const float affix_gap = size * 0.1f;
    const float prefix =
        style.prefix.empty() ? 0.0f : paint.label_width(style.prefix, small) + affix_gap;
    const float suffix =
        style.suffix.empty() ? 0.0f : paint.label_width(style.suffix, small) + affix_gap;
    float number = 0.0f;
    const auto cell_width = [&](std::size_t i)
    {
        // A cell that appears or disappears grows or shrinks with the roll,
        // so a left-aligned number does not jump when it gains a digit.
        if (from[i] == ' ' || to[i] == ' ')
            return tween::lerp(cell(from[i]), cell(to[i]), tween::smoothstep(part));
        return std::max(cell(from[i]), cell(to[i]));
    };
    for (std::size_t i = 0; i < from.size(); ++i)
        number += cell_width(i);

    const float total = prefix + number + suffix;
    float x = bounds_.x;
    if (style.align == gfx::Align::center)
        x = bounds_.cx() - total * 0.5f;
    else if (style.align == gfx::Align::right)
        x = bounds_.x + bounds_.w - total;
    const float baseline = bounds_.cy() + size * 0.35f;
    const Color quiet = ink.with_alpha(0.7f);

    if (!style.prefix.empty())
        paint.label(style.prefix, x, baseline, small, quiet);
    x += prefix;

    const auto glyph = [&](char c, float cx, float dy, float alpha)
    {
        if (c == ' ' || alpha <= 0.01f)
            return;
        const char text[2] = {c, '\0'};
        if (style.face == CounterFace::heading)
            paint.heading(text, cx, baseline + dy, size, ink.with_alpha(alpha), gfx::Align::center);
        else
            paint.label(text, cx, baseline + dy, size, ink.with_alpha(alpha), gfx::Align::center);
    };
    const float travel = size * 1.05f;
    if (moving)
        list.push_clip({x - 2.0f, baseline - size * 0.9f, number + 4.0f, size * 1.16f});
    for (std::size_t i = 0; i < from.size(); ++i)
    {
        const float width = cell_width(i);
        const float cx = x + width * 0.5f;
        if (from[i] == to[i])
        {
            glyph(from[i], cx, 0.0f, 1.0f);
        }
        else
        {
            glyph(from[i], cx, -part * travel, 1.0f - part);
            glyph(to[i], cx, (1.0f - part) * travel, part);
        }
        x += width;
    }
    if (moving)
        list.pop_clip();

    if (!style.suffix.empty())
        paint.label(style.suffix, x + affix_gap, baseline, small, quiet);
}

} // namespace hui::ui
