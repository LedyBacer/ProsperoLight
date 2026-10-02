// ps5-homebrew-ui - Components: what the data components share.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/data_common.hpp"

#include "ui/components/progress.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

bool own_face(const Theme &theme)
{
    return theme.label == FontRole::pixel || theme.label == FontRole::hand;
}

float luminance(Color c)
{
    return 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
}

Color opaque(Color ground, Color c)
{
    const Color mixed = gfx::mix(ground, c, tween::clamp01(c.a));
    return {mixed.r, mixed.g, mixed.b, 1.0f};
}

float distance(Color a, Color b)
{
    return std::max({std::fabs(a.r - b.r), std::fabs(a.g - b.g), std::fabs(a.b - b.b)});
}

// How far apart in lightness a mark and its ground must be to read as a
// shape from the sofa. Hue alone does not carry at that distance.
constexpr float kContrast = 0.17f;

Color pull_to_text(const Theme &theme, Color ground, Color c, bool on_panel)
{
    // Toward the text that reads on this ground: a theme's page may be dark
    // where its panels are light.
    const Color text =
        opaque(ground, !on_panel && theme.page_text.a > 0.0f ? theme.page_text : theme.text);
    for (int i = 0; i < 3 && std::fabs(luminance(c) - luminance(ground)) < kContrast; ++i)
        c = gfx::mix(c, text, 0.4f);
    return c;
}

} // namespace

float number_width(const Painter &paint, std::string_view text, float size)
{
    if (own_face(paint.theme()))
        return paint.label_width(text, size);
    return paint.font(FontRole::mono).measure(text, size * 0.92f);
}

float draw_number(Painter &paint, gfx::DrawList &list, std::string_view text, float x,
                  float baseline, float size, Color color, gfx::Align align)
{
    if (own_face(paint.theme()))
        return paint.label(text, x, baseline, size, color, align);
    return ui::text(list, paint.font(FontRole::mono), text, x, baseline, size * 0.92f, color,
                    align);
}

std::string fit_number(const Painter &paint, std::string_view text, float size, float width)
{
    if (number_width(paint, text, size) <= width)
        return std::string(text);
    std::string cut(text);
    while (!cut.empty())
    {
        do
            cut.pop_back();
        while (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0xc0) == 0x80);
        if (number_width(paint, cut + "...", size) <= width)
            break;
    }
    return cut + "...";
}

std::string format_value(double value, int decimals)
{
    const double size = std::fabs(value);
    double scaled = value;
    const char *suffix = "";
    if (size >= 1.0e6)
    {
        scaled = value / 1.0e6;
        suffix = "M";
    }
    else if (size >= 1.0e3)
    {
        scaled = value / 1.0e3;
        suffix = "k";
    }
    if (decimals < 0)
        decimals = std::fabs(scaled - std::round(scaled)) < 0.05 ? 0 : 1;
    char text[40];
    std::snprintf(text, sizeof(text), "%.*f%s", std::clamp(decimals, 0, 6), scaled, suffix);
    return text;
}

Inks inks(const Painter &paint, bool on_panel)
{
    if (on_panel)
        return {paint.theme().text, paint.theme().text_muted};
    return {paint.page_text(), paint.page_text_muted()};
}

Color ground_color(const Theme &theme, bool on_panel)
{
    if (on_panel)
        return solid_surface(theme);
    return {theme.page.r, theme.page.g, theme.page.b, 1.0f};
}

Color rule_color(const Painter &paint, bool on_panel, float strength)
{
    return inks(paint, on_panel).muted.with_alpha(0.24f * strength);
}

Color visible_on(const Theme &theme, Color color, bool on_panel)
{
    const Color ground = ground_color(theme, on_panel);
    const Color solid = opaque(ground, color);
    // A primary that is a pale tint (a hand-drawn theme's buttons) carries
    // its real colour in the text drawn on it.
    if (distance(color, theme.primary) < 0.01f &&
        std::fabs(luminance(solid) - luminance(ground)) < kContrast)
        return pull_to_text(theme, ground, opaque(ground, theme.on_primary), on_panel);
    return pull_to_text(theme, ground, solid, on_panel);
}

Color series_color(const Theme &theme, int index, bool on_panel)
{
    const Color ground = ground_color(theme, on_panel);
    const Color primary = visible_on(theme, theme.primary, on_panel);
    const Color accent = opaque(ground, theme.accent);
    const Color candidates[] = {
        primary,
        accent,
        theme.success,
        theme.warning,
        theme.danger,
        gfx::mix(primary, opaque(ground, theme.text), 0.5f),
        gfx::mix(accent, theme.danger, 0.5f),
        gfx::mix(theme.success, primary, 0.5f),
    };
    constexpr int kMax = static_cast<int>(std::size(candidates));
    Color chosen[kMax];
    int count = 0;
    for (const Color &candidate : candidates)
    {
        const Color c = pull_to_text(theme, ground, opaque(ground, candidate), on_panel);
        bool fresh = true;
        for (int i = 0; i < count; ++i)
            fresh = fresh && distance(chosen[i], c) > 0.25f;
        if (fresh)
            chosen[count++] = c;
    }
    if (count == 0)
        return opaque(ground, theme.text);
    const int wanted = std::max(index, 0);
    const Color base = chosen[wanted % count];
    // Past the end of the palette the colours come round again, paler.
    const int lap = wanted / count;
    return lap == 0 ? base : gfx::mix(base, ground, std::min(0.3f * static_cast<float>(lap), 0.6f));
}

void draw_focus(Canvas &canvas, const ComponentStyle &style, const Highlight &highlight,
                HighlightStyle look, float active, float amount)
{
    active = tween::clamp01(active);
    if (look.kind != HighlightKind::fill)
    {
        highlight.draw(canvas, style, look, (0.3f + 0.7f * active) * amount);
        return;
    }
    highlight.draw(canvas, style, look, active * amount);
    look.kind = HighlightKind::tint;
    highlight.draw(canvas, style, look, (1.0f - active) * 0.6f * amount);
}

float row_visibility(const Rect &row, const Rect &view, float hidden)
{
    if (hidden <= 0.0f || row.h <= 0.0f)
        return 1.0f;
    const float shown =
        std::min(row.y + row.h - view.y, view.y + view.h - row.y) / std::min(row.h, view.h);
    const float from = std::min(hidden, 0.9f);
    return tween::clamp01((shown - from) / (0.97f - from));
}

void draw_block(Canvas &canvas, const Theme &theme, const Rect &r, float radius, Color color)
{
    if (r.w < 0.5f || r.h < 0.5f || color.a <= 0.0f)
        return;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, 0);
    const float corner = std::min(radius, std::min(r.w, r.h) * 0.5f);
    if (theme.style == SurfaceStyle::gloss)
    {
        const Color light = gfx::mix(color, Color{1.0f, 1.0f, 1.0f, color.a}, 0.25f);
        const Color dark = gfx::mix(color, Color{0.0f, 0.0f, 0.0f, color.a}, 0.15f);
        list.gradient_rect(r, corner, light, dark);
        list.gradient_rect({r.x + 1.0f, r.y + 1.0f, r.w - 2.0f, r.h * 0.45f}, corner,
                           theme.light.with_alpha(color.a), theme.light.with_alpha(0.1f * color.a));
        return;
    }
    if (theme.style == SurfaceStyle::glow)
        list.glow(r, corner, 10.0f, color.with_alpha(0.45f));
    paint.fill(r, corner, color);
    const bool framed = theme.style == SurfaceStyle::hard || theme.style == SurfaceStyle::pixel ||
                        theme.style == SurfaceStyle::sketch;
    const float line = std::min(theme.border, 3.0f);
    if (framed && std::min(r.w, r.h) > 2.0f * line + 2.0f)
        paint.stroke(r, corner, line, theme.outline.with_alpha(color.a));
}

void draw_marker(Canvas &canvas, const Theme &theme, float cx, float cy, float radius, Color color)
{
    if (theme.corner == Corner::round && theme.radius >= 2.0f)
        canvas.list.circle(cx, cy, radius, color);
    else
        canvas.list.rounded_rect({cx - radius, cy - radius, radius * 2.0f, radius * 2.0f}, 0.0f,
                                 color);
}

void draw_readout(Canvas &canvas, const ComponentStyle &style, std::string_view text, float cx,
                  float bottom, float size, const Rect &keep, float amount)
{
    if (amount <= 0.01f || text.empty())
        return;
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, 0);
    // The text colour made opaque is the one fill certain to stand out from
    // whatever the chart drew under the bubble, in any theme.
    const Color fill = opaque(Color{theme.page.r, theme.page.g, theme.page.b, 1.0f}, theme.text);
    const Color ink = Painter::on(fill);
    const float pad = size * 0.6f;
    const float width = paint.label_width(text, size) + 2.0f * pad;
    const float height = size + 16.0f;
    constexpr float kPointer = 7.0f;
    Rect bubble{cx - width * 0.5f, bottom - kPointer - height, width, height};
    bubble.x = std::clamp(bubble.x, keep.x, std::max(keep.x, keep.x + keep.w - width));
    bubble.y = std::max(bubble.y, keep.y);
    const float rise = style.reduced_motion ? 0.0f : 8.0f * (1.0f - amount);
    list.push_opacity(tween::clamp01(amount));
    list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, rise);
    paint.fill(bubble, std::min(theme.radius, 8.0f), fill);
    const float tip = std::clamp(cx, bubble.x + 12.0f, bubble.x + bubble.w - 12.0f);
    list.triangle({tip - kPointer, bubble.y + bubble.h - 1.0f, 2.0f * kPointer, kPointer + 1.0f},
                  fill, 0.0f, 3.14159265f);
    paint.label(text, bubble.cx(), bubble.cy() + size * 0.35f, size, ink, gfx::Align::center);
    list.pop_transform();
    list.pop_opacity();
}

} // namespace hui::ui
