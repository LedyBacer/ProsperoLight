// ps5-homebrew-ui - Components: Panel, Divider, SectionHeader and Spacer.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/surface.hpp"

#include <algorithm>
#include <cmath>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

// A repeatable "random" offset in -1..1 for hand-drawn lines. It depends on
// the length of the line only, so a divider keeps its wobble while it moves.
float wobble(float length, int index)
{
    const float seed = length * 12.9898f + static_cast<float>(index) * 37.719f;
    const float value = std::sin(seed) * 43758.5453f;
    return (value - std::floor(value)) * 2.0f - 1.0f;
}

// Surfaces that are lit from one side draw their lines as grooves.
bool grooved(const Theme &theme)
{
    return theme.style == SurfaceStyle::bevel || theme.style == SurfaceStyle::neumorphic;
}

Color divider_color(const DividerStyle &style, const Painter &paint)
{
    if (style.color.a > 0.0f)
        return style.color;
    const Theme &theme = style.theme;
    // Themes that draw borders have a colour for lines; the others get a
    // quiet tone of the text that is read where the line lies.
    if (theme.border > 0.0f && theme.outline.a > 0.05f && theme.style != SurfaceStyle::glass)
        return theme.outline;
    return style.on_panel ? theme.text_muted.with_alpha(0.32f)
                          : paint.page_text_muted().with_alpha(0.4f);
}

// One straight run of the line from a0 to a1 along its direction, centred on
// c across it.
void draw_run(Canvas &canvas, const DividerStyle &style, const Painter &paint, float a0, float a1,
              float c)
{
    if (a1 - a0 < 1.0f)
        return;
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    const float t = divider_thickness(style);
    const Color ink = divider_color(style, paint);
    const bool upright = style.vertical;
    const auto bar = [&](float from, float to, float across, float width, Color color)
    {
        const Rect r = upright ? Rect{across - width * 0.5f, from, width, to - from}
                               : Rect{from, across - width * 0.5f, to - from, width};
        // Thin lines keep square ends; a thick one in a round theme is a pill.
        const bool round = theme.corner == Corner::round && theme.radius >= 6.0f && width >= 3.0f;
        list.rounded_rect(r, round ? width * 0.5f : 0.0f, color);
    };
    const auto pen = [&](float from, float to, int seed, Color color)
    {
        // A ruled-by-hand stroke: its ends miss the line by a pixel or so.
        const float lift = std::min(1.6f, (to - from) * 0.04f);
        const float j0 = wobble(a1 - a0, seed) * lift;
        const float j1 = wobble(a1 - a0, seed + 1) * lift;
        if (upright)
            list.line(c + j0, from, c + j1, to, t, color);
        else
            list.line(from, c + j0, to, c + j1, t, color);
    };

    const bool sketch = theme.style == SurfaceStyle::sketch;
    if (style.line == DividerLine::dashed)
    {
        float dash = std::max(style.dash, 2.0f);
        float gap = std::max(style.dash_gap, 1.0f);
        if (theme.corner == Corner::pixel)
        {
            // Dashes on the pixel grid: whole blocks, whole gaps.
            dash = std::max(t, std::round(dash / t) * t);
            gap = std::max(t, std::round(gap / t) * t);
        }
        const float length = a1 - a0;
        const int count = std::max(1, static_cast<int>((length + gap) / (dash + gap)));
        // Centre the pattern, so both ends finish on a whole dash.
        const float used = static_cast<float>(count) * dash + static_cast<float>(count - 1) * gap;
        const float start = a0 + std::max(length - used, 0.0f) * 0.5f;
        for (int i = 0; i < count; ++i)
        {
            const float from = start + static_cast<float>(i) * (dash + gap);
            const float to = std::min(from + dash, a1);
            if (sketch)
                pen(from, to, i * 2, ink);
            else
                bar(from, to, c, t, ink);
        }
        return;
    }
    if (style.line == DividerLine::inset || (grooved(theme) && style.color.a <= 0.0f))
    {
        // Dark above (or left of) light: the edge a groove would shade and
        // the edge it would catch the light on.
        const Color dark{theme.shadow.r, theme.shadow.g, theme.shadow.b,
                         std::clamp(theme.shadow.a, 0.3f, 0.7f)};
        const Color quiet = style.on_panel ? theme.text : paint.page_text();
        const Color light = theme.light.a > 0.05f ? theme.light : quiet.with_alpha(0.14f);
        const float half = std::max(t * 0.5f, 1.25f);
        bar(a0, a1, c - half * 0.5f, half, dark);
        bar(a0, a1, c + half * 0.5f, half, light);
        return;
    }
    if (sketch)
    {
        pen(a0, a1, 0, ink);
        return;
    }
    if (theme.style == SurfaceStyle::glow)
    {
        const Rect lit =
            upright ? Rect{c - t * 0.5f, a0, t, a1 - a0} : Rect{a0, c - t * 0.5f, a1 - a0, t};
        list.glow(lit, 0.0f, 8.0f, ink.with_alpha(0.3f));
    }
    bar(a0, a1, c, t, ink);
}

} // namespace

// ---- Divider -----------------------------------------------------------------

float divider_thickness(const DividerStyle &style)
{
    if (style.thickness > 0.0f)
        return style.thickness;
    const Theme &theme = style.theme;
    if (theme.corner == Corner::pixel)
        return std::max(theme.border, 4.0f); // one pixel of the art, never thinner
    return std::clamp(theme.border, 1.5f, 4.0f);
}

void draw_divider(Canvas &canvas, const DividerStyle &style, const Rect &bounds,
                  std::string_view label)
{
    Painter paint(canvas.list, canvas.fonts, style.theme, canvas.glass);
    const bool upright = style.vertical;
    const float a0 = (upright ? bounds.y : bounds.x) + style.inset;
    const float a1 = (upright ? bounds.y + bounds.h : bounds.x + bounds.w) - style.inset;
    const float c = upright ? bounds.cx() : bounds.cy();
    if (a1 <= a0)
        return;
    if (label.empty())
    {
        draw_run(canvas, style, paint, a0, a1, c);
        return;
    }
    // The label sits in the middle and the line stops either side of it.
    const float size = style.label_size;
    const float room = upright ? size : paint.label_width(label, size);
    const float middle = (a0 + a1) * 0.5f;
    const float hole = room * 0.5f + style.label_gap;
    draw_run(canvas, style, paint, a0, middle - hole, c);
    draw_run(canvas, style, paint, middle + hole, a1, c);
    const Color ink = style.on_panel ? style.theme.text_muted : paint.page_text_muted();
    if (upright)
        paint.label(label, c, middle + size * 0.35f, size, ink, gfx::Align::center);
    else
        paint.label(label, middle, c + size * 0.35f, size, ink, gfx::Align::center);
}

// ---- Panel -------------------------------------------------------------------

float Panel::header_size() const
{
    if (style.kind != PanelKind::titled)
        return 0.0f;
    if (style.header_height > 0.0f)
        return style.header_height;
    const float block =
        style.title_size * 1.15f + (subtitle.empty() ? 0.0f : style.subtitle_size * 1.3f);
    return block + style.padding_y * 1.2f;
}

Rect Panel::header_rect(const Rect &bounds) const
{
    return {bounds.x, bounds.y, bounds.w, std::min(header_size(), bounds.h)};
}

Rect Panel::footer_rect(const Rect &bounds) const
{
    const float height = std::min(std::max(style.footer_height, 0.0f), bounds.h);
    return {bounds.x + style.padding_x, bounds.y + bounds.h - height,
            std::max(bounds.w - 2.0f * style.padding_x, 0.0f), height};
}

Rect Panel::content_rect(const Rect &bounds) const
{
    const float head = header_size();
    const float foot = std::max(style.footer_height, 0.0f);
    const float top = bounds.y + head + style.padding_y;
    const float bottom = bounds.y + bounds.h - foot - style.padding_y;
    return {bounds.x + style.padding_x, top, std::max(bounds.w - 2.0f * style.padding_x, 0.0f),
            std::max(bottom - top, 0.0f)};
}

void Panel::draw(Canvas &canvas, const Rect &bounds) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const float radius = std::min(style.radius >= 0.0f ? style.radius : theme.radius_card,
                                  std::min(bounds.w, bounds.h) * 0.5f);
    if (!style.surface)
    {
        // Someone else drew what this panel lies on.
    }
    else if (style.kind == PanelKind::well)
    {
        paint.well(bounds, radius, theme.surface_high);
    }
    else if (style.radius < 0.0f)
    {
        paint.panel(bounds);
    }
    else
    {
        // Painter::panel with a radius of our own.
        if (theme.style == SurfaceStyle::flat && theme.shadow.a > 0.0f && theme.shadow_blur > 0.0f)
            list.shadow({bounds.x, bounds.y + theme.shadow_offset, bounds.w, bounds.h}, radius,
                        theme.shadow_blur, theme.shadow);
        paint.surface(bounds, radius, theme.surface, theme.outline, 1.0f);
    }

    DividerStyle rule;
    rule.theme = theme;
    rule.line = style.rule;
    rule.on_panel = true;
    const float left = bounds.x + style.padding_x;
    const float width = std::max(bounds.w - 2.0f * style.padding_x, 0.0f);

    if (style.kind == PanelKind::titled)
    {
        const Rect head = header_rect(bounds);
        const float block =
            style.title_size * 1.15f + (subtitle.empty() ? 0.0f : style.subtitle_size * 1.3f);
        const float top = head.y + (head.h - block) * 0.5f;
        float x = left;
        if (style.accent_bar)
        {
            paint.fill({x, top + 2.0f, 5.0f, block - 4.0f},
                       theme.corner == Corner::round && theme.radius >= 6.0f ? 2.5f : 0.0f,
                       theme.accent);
            x += 17.0f;
        }
        const float slot = header_right ? std::min(style.header_slot, width * 0.5f) : 0.0f;
        const float room = std::max(left + width - slot - x - (slot > 0.0f ? 12.0f : 0.0f), 20.0f);
        const float baseline = top + style.title_size * 0.86f;
        paint.bounded_label(title, x, baseline, style.title_size, theme.text, room);
        if (!subtitle.empty())
            paint.bounded_body(subtitle, x, baseline + style.subtitle_size * 1.3f, style.subtitle_size, theme.text_muted, room);
        if (header_right)
            header_right(canvas, {left + width - slot, head.y, slot, head.h});
        if (style.header_rule)
            draw_divider(canvas, rule, {left, head.y + head.h - 1.0f, width, 2.0f});
    }
    if (style.footer_height > 0.0f)
    {
        const Rect foot = footer_rect(bounds);
        if (style.header_rule)
            draw_divider(canvas, rule, {foot.x, foot.y - 1.0f, foot.w, 2.0f});
        if (footer)
            footer(canvas, foot);
    }
}

// ---- SectionHeader -----------------------------------------------------------

void SectionHeader::set_count(int count)
{
    count_ = count;
    badge_.set_count(std::max(count, 0));
}

void SectionHeader::update(float dt)
{
    // The badge animates in the header's own style.
    badge_.style.reduced_motion = style.reduced_motion;
    badge_.style.theme = style.theme;
    badge_.update(dt);
}

void SectionHeader::draw(Canvas &canvas, const Rect &bounds) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const Color ink = style.on_panel ? theme.text : paint.page_text();
    const Color quiet = style.on_panel ? theme.text_muted : paint.page_text_muted();
    const float cy = bounds.cy();
    float right = bounds.x + bounds.w;

    // From the right: the action hint, a controller glyph and its words.
    if (!action.empty())
    {
        const float glyph = action_button == Button::none
                                ? 0.0f
                                : button_width(action_button, style.glyph_size) + 10.0f;
        const float limit = std::max(bounds.w * 0.5f - glyph, 20.0f);
        const std::string words = fit_label(paint, action, style.hint_size, limit);
        const float text = paint.label_width(words, style.hint_size);
        right -= text;
        paint.label(words, right, cy + style.hint_size * 0.35f, style.hint_size, quiet);
        if (action_button != Button::none)
        {
            right -= glyph;
            GlyphStyle glyphs = theme.dark ? GlyphStyle::dark() : GlyphStyle::light();
            draw_button(list, canvas.fonts, glyphs, action_button, right, cy, style.glyph_size);
        }
        right -= style.gap;
    }

    const bool counted = count_ >= 0 && !(count_ == 0 && style.hide_zero);
    Badge badge = badge_;
    badge.style.theme = theme;
    badge.style.reduced_motion = style.reduced_motion;
    badge.style.kind = style.count_kind;
    badge.style.fill = style.count_fill;
    badge.style.height = style.count_height;
    badge.style.text_size = style.count_size;
    badge.style.hide_zero = false;
    badge.style.align = gfx::Align::left;
    if (!style.on_panel)
        badge.style.backing = Color{theme.page.r, theme.page.g, theme.page.b, 1.0f};
    const float badge_width = counted ? badge.width(paint) + style.gap : 0.0f;

    const float room = std::max(right - bounds.x - badge_width, 20.0f);
    const std::string words =
        fit_label(paint, style.caps ? upper(title) : title, style.title_size, room);
    float x = bounds.x;
    x += paint.label(words, x, cy + style.title_size * 0.35f, style.title_size, ink);
    if (counted)
    {
        x += style.gap;
        badge.set_bounds({x, cy - style.count_height * 0.5f, badge_width, style.count_height});
        badge.draw(canvas);
        x += badge_width - style.gap;
    }

    if (style.rule == SectionRule::none)
        return;
    DividerStyle rule;
    rule.theme = theme;
    rule.line = style.line;
    rule.on_panel = style.on_panel;
    if (style.rule == SectionRule::trailing)
    {
        x += style.gap;
        if (right - x > 24.0f)
            draw_divider(canvas, rule, {x, cy - 1.0f, right - x, 2.0f});
    }
    else
    {
        draw_divider(canvas, rule, {bounds.x, bounds.y + bounds.h - 2.0f, bounds.w, 2.0f});
    }
}

} // namespace hui::ui
