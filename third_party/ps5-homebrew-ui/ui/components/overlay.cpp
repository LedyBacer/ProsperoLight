// ps5-homebrew-ui - Components: what the overlay components share.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/overlay.hpp"

#include "ui/pixel_font.hpp"

#include <algorithm>
#include <cmath>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

bool stroke_only(const Theme &theme)
{
    return theme.style == SurfaceStyle::outline || theme.style == SurfaceStyle::glow;
}

// Frost needs a rounded shape (the blurred copy cannot be chamfered or
// notched) and a surface construction that is a plain fill.
bool can_frost(const Canvas &canvas, const Theme &theme)
{
    if (canvas.glass == 0 || theme.corner != Corner::round)
        return false;
    return theme.style == SurfaceStyle::glass || theme.style == SurfaceStyle::flat ||
           theme.style == SurfaceStyle::soft || theme.style == SurfaceStyle::outline ||
           theme.style == SurfaceStyle::glow;
}

// Greedy word wrap. Measuring is the caller's, so one routine serves every
// face a theme may pick for its body, label and heading.
template <typename Measure>
std::vector<std::string> wrap(std::string_view text, float width, int max_lines, Measure measure)
{
    std::vector<std::string> lines;
    if (max_lines <= 0)
        return lines;
    const std::size_t limit = static_cast<std::size_t>(max_lines);
    std::string line;
    bool cut = false; // text was left over after the last line
    std::size_t at = 0;
    while (at < text.size())
    {
        const char c = text[at];
        if (c == ' ')
        {
            ++at;
            continue;
        }
        if (c == '\n')
        {
            if (lines.size() + 1 >= limit)
            {
                cut = at + 1 < text.size();
                break;
            }
            lines.push_back(line);
            line.clear();
            ++at;
            continue;
        }
        std::size_t end = text.find_first_of(" \n", at);
        if (end == std::string_view::npos)
            end = text.size();
        const std::string word(text.substr(at, end - at));
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (line.empty() || measure(candidate) <= width)
        {
            line = candidate;
        }
        else
        {
            if (lines.size() + 1 >= limit)
            {
                cut = true;
                break;
            }
            lines.push_back(line);
            line = word;
        }
        at = end;
    }
    if (!line.empty() || cut)
        lines.push_back(line);
    if (cut && !lines.empty()) {
        std::string tail(text.substr(at));
        std::replace(tail.begin(), tail.end(), '\n', ' ');
        lines.back() += " " + tail;
    }
    return lines;
}

} // namespace

Color status_color(const Theme &theme, StatusKind kind)
{
    switch (kind)
    {
    case StatusKind::success:
        return theme.success;
    case StatusKind::warning:
        return theme.warning;
    case StatusKind::danger:
        return theme.danger;
    default:
        return Color{theme.primary.r, theme.primary.g, theme.primary.b, 1.0f};
    }
}

Color opaque_over(Color base, Color color)
{
    const Color mixed = gfx::mix(base, color, tween::clamp01(color.a));
    return {mixed.r, mixed.g, mixed.b, 1.0f};
}

void draw_status_icon(Canvas &canvas, const Theme &theme, StatusKind kind, float cx, float cy,
                      float size)
{
    if (kind == StatusKind::none || size <= 0.0f)
        return;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, 0);
    const Color color = status_color(theme, kind);
    const bool strokes = stroke_only(theme);
    const float half = size * 0.5f;
    const float pen = std::max(3.0f, size * 0.085f);
    const float edge = std::max(2.5f, size * 0.06f);
    // On a filled plate the symbol takes whichever of black and white reads;
    // on an outlined one it is the status colour itself.
    const Color ink = strokes ? color : Painter::on(color);

    if (kind == StatusKind::warning)
    {
        // The triangle's centre of mass sits low: the mark moves with it.
        const Rect plate{cx - half, cy - size * 0.46f, size, size * 0.92f};
        list.triangle(plate, color, strokes ? edge : 0.0f);
        const float y = cy + size * 0.07f;
        list.line(cx, y - size * 0.16f, cx, y + size * 0.07f, pen, ink);
        list.circle(cx, y + size * 0.23f, pen * 0.62f, ink);
        return;
    }

    const bool square = theme.corner != Corner::round || theme.radius < 2.0f;
    if (square)
    {
        const Rect plate{cx - half, cy - half, size, size};
        const float radius = std::min(theme.radius, half);
        if (strokes)
            paint.stroke(plate, radius, edge, color);
        else
            paint.fill(plate, radius, color);
    }
    else if (strokes)
    {
        list.ring(cx, cy, half, edge, color);
    }
    else
    {
        list.circle(cx, cy, half, color);
    }

    switch (kind)
    {
    case StatusKind::info:
        list.circle(cx, cy - size * 0.21f, pen * 0.66f, ink);
        list.line(cx, cy - size * 0.03f, cx, cy + size * 0.23f, pen, ink);
        break;
    case StatusKind::success:
        list.line(cx - size * 0.21f, cy + size * 0.02f, cx - size * 0.06f, cy + size * 0.17f, pen,
                  ink);
        list.line(cx - size * 0.06f, cy + size * 0.17f, cx + size * 0.22f, cy - size * 0.15f, pen,
                  ink);
        break;
    case StatusKind::danger:
    {
        const float reach = size * 0.17f;
        list.line(cx - reach, cy - reach, cx + reach, cy + reach, pen, ink);
        list.line(cx + reach, cy - reach, cx - reach, cy + reach, pen, ink);
        break;
    }
    case StatusKind::question:
    {
        // A hook from nine o'clock round to five, a short stem and a dot.
        const float hook = size * 0.15f;
        const float hook_cy = cy - size * 0.11f;
        list.arc(cx, hook_cy, hook + pen * 0.5f, pen, 4.712389f, 4.1f, ink);
        list.line(cx + hook * 0.45f, hook_cy + hook * 0.9f, cx, cy + size * 0.11f, pen, ink);
        list.circle(cx, cy + size * 0.28f, pen * 0.66f, ink);
        break;
    }
    default:
        break;
    }
}

void draw_overlay_panel(Canvas &canvas, const Theme &theme, const Rect &r, bool frosted, float tint,
                        float radius)
{
    gfx::DrawList &list = canvas.list;
    const float corner =
        std::min(radius < 0.0f ? theme.radius_card : radius, std::min(r.w, r.h) * 0.5f);
    const bool frost = frosted && can_frost(canvas, theme);

    if (frost && theme.style == SurfaceStyle::glass)
    {
        // The theme's own surface already is frosted glass.
        Painter paint(list, canvas.fonts, theme, canvas.glass);
        paint.surface(r, corner, theme.surface, theme.outline, 1.0f);
        return;
    }

    Painter paint(list, canvas.fonts, theme, 0);
    const Color solid = opaque_over(theme.page, theme.surface);
    if (frost)
    {
        if (theme.shadow.a > 0.0f)
            list.shadow({r.x, r.y + theme.shadow_offset, r.w, r.h}, corner,
                        std::max(theme.shadow_blur, 24.0f), theme.shadow);
        list.glass(canvas.glass, r, corner, Color{1.0f, 1.0f, 1.0f, 1.0f});
        paint.fill(r, corner, solid.with_alpha(tween::clamp01(tint)));
        paint.stroke(r, corner, std::max(theme.border, 1.5f), theme.outline);
        return;
    }

    // Flat languages keep their buttons flat but may still float a card
    // (Painter::panel does the same).
    if (theme.style == SurfaceStyle::flat && theme.shadow.a > 0.0f && theme.shadow_blur > 0.0f)
        list.shadow({r.x, r.y + theme.shadow_offset, r.w, r.h}, corner, theme.shadow_blur,
                    theme.shadow);
    paint.surface(r, corner, solid, theme.outline, 1.0f);
}

float heading_width(const Canvas &canvas, const Theme &theme, std::string_view text, float size)
{
    // Mirrors Painter::heading, face by face.
    const Painter paint(canvas.list, canvas.fonts, theme, 0);
    if (theme.heading == FontRole::pixel)
    {
        if (canvas.fonts.pixel.font == nullptr)
            return pixel_text_width(text, size * 0.66f);
        return canvas.fonts.pixel.measure(text, Painter::pixel_em(size * 0.7f));
    }
    if (theme.heading == FontRole::hand)
        return paint.font(theme.heading).measure(text, size * 1.16f);
    return paint.font(theme.heading).measure(text, size);
}

std::vector<std::string> wrap_body(const Painter &paint, std::string_view text, float size,
                                   float width, int max_lines)
{
    return wrap(text, width, max_lines,
                [&](std::string_view s) { return paint.body_width(s, size); });
}

std::vector<std::string> wrap_heading(const Canvas &canvas, const Theme &theme,
                                      std::string_view text, float size, float width, int max_lines)
{
    return wrap(text, width, max_lines,
                [&](std::string_view s) { return heading_width(canvas, theme, s, size); });
}

} // namespace hui::ui
