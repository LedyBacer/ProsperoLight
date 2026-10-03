// ps5-homebrew-ui - Controller button glyphs and on-screen hint rows.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/glyphs.hpp"

namespace hui::ui
{

namespace
{

// DualSense face-button colours.
const gfx::Color kCross = gfx::Color::rgb(0x7fa7ff);
const gfx::Color kCircle = gfx::Color::rgb(0xff6b7d);
const gfx::Color kSquare = gfx::Color::rgb(0xe98fd8);
const gfx::Color kTriangle = gfx::Color::rgb(0x46d6b0);

void label(gfx::DrawList &list, const Fonts &fonts, const GlyphStyle &style, const char *value,
           float cx, float cy, float size)
{
    text(list, fonts.semibold, value, cx, cy + size * 0.36f, size, style.ink, gfx::Align::center);
}

void face(gfx::DrawList &list, const GlyphStyle &style, Button button, float cx, float cy,
          float size)
{
    const float half = size * 0.5f;
    const float stroke = size * 0.11f;
    // A disc behind the symbol, like the physical button. Single-colour
    // styles add a rim, or the disc would vanish on a page of its own colour.
    list.circle(cx, cy, half, style.body);
    if (!style.tinted_faces)
        list.ring(cx, cy, half, 1.5f, style.edge);
    const float inner = half * 0.52f;
    const auto colour = [&](gfx::Color tint) { return style.tinted_faces ? tint : style.ink; };
    switch (button)
    {
    case Button::cross:
        list.line(cx - inner, cy - inner, cx + inner, cy + inner, stroke, colour(kCross));
        list.line(cx - inner, cy + inner, cx + inner, cy - inner, stroke, colour(kCross));
        break;
    case Button::circle:
        list.ring(cx, cy, inner + stroke * 0.3f, stroke, colour(kCircle));
        break;
    case Button::square:
    {
        const float s = inner * 1.7f;
        list.bordered_rect({cx - s * 0.5f, cy - s * 0.5f, s, s}, stroke * 0.3f,
                           colour(kSquare).with_alpha(0.0f), stroke, colour(kSquare));
        break;
    }
    default:
    {
        const float w = inner * 2.1f;
        const float h = inner * 1.85f;
        list.triangle({cx - w * 0.5f, cy - h * 0.58f, w, h}, colour(kTriangle), stroke);
        break;
    }
    }
}

// A thumbstick seen from above: cap, grip ring and four direction ticks.
void stick(gfx::DrawList &list, const Fonts &fonts, const GlyphStyle &style, const char *letter,
           float cx, float cy, float size)
{
    const float half = size * 0.5f;
    list.circle(cx, cy, half, style.body);
    list.ring(cx, cy, half - 1.0f, 1.5f, style.edge);
    list.ring(cx, cy, half * 0.6f, size * 0.055f, style.ink.with_alpha(0.85f));
    const float t = size * 0.075f; // tick half-width
    const float r = half * 0.9f;   // tick tip distance
    const float b = r - t * 1.3f;  // tick base distance
    const gfx::Color tick = style.ink.with_alpha(0.75f);
    const float up[] = {cx, cy - r, cx + t, cy - b, cx - t, cy - b};
    const float down[] = {cx, cy + r, cx - t, cy + b, cx + t, cy + b};
    const float left[] = {cx - r, cy, cx - b, cy - t, cx - b, cy + t};
    const float right[] = {cx + r, cy, cx + b, cy + t, cx + b, cy - t};
    list.polygon(up, 3, tick);
    list.polygon(down, 3, tick);
    list.polygon(left, 3, tick);
    list.polygon(right, 3, tick);
    label(list, fonts, style, letter, cx, cy, size * 0.34f);
}

} // namespace

GlyphStyle GlyphStyle::dark()
{
    return {gfx::Color::rgb(0x11131f, 0.92f), gfx::Color::rgb(0x6c7196, 0.9f),
            gfx::Color::rgb(0xe4e3f5), gfx::Color::rgb(0xf5f3ff, 0.86f), true};
}

GlyphStyle GlyphStyle::light()
{
    return {gfx::Color::rgb(0x1b1d2b), gfx::Color::rgb(0x1b1d2b, 0.0f), gfx::Color::rgb(0xf4f1ea),
            gfx::Color::rgb(0x1b1d2b, 0.8f), true};
}

GlyphStyle GlyphStyle::mono(gfx::Color ink, gfx::Color body)
{
    return {body, ink.with_alpha(0.7f), ink, ink.with_alpha(0.85f), false};
}

float button_width(Button button, float size)
{
    switch (button)
    {
    case Button::l1:
    case Button::r1:
        return size * 1.45f;
    case Button::l2:
    case Button::r2:
        return size * 1.3f;
    case Button::options:
        return size * 1.05f;
    case Button::touchpad:
        return size * 1.6f;
    case Button::none:
        return 0.0f;
    default:
        return size;
    }
}

void draw_button(gfx::DrawList &list, const Fonts &fonts, const GlyphStyle &style, Button button,
                 float x, float cy, float size)
{
    const float w = button_width(button, size);
    const float cx = x + w * 0.5f;
    switch (button)
    {
    case Button::none:
        return;
    case Button::cross:
    case Button::circle:
    case Button::square:
    case Button::triangle:
        face(list, style, button, cx, cy, size);
        return;
    case Button::l1:
    case Button::r1:
    {
        // Bumper: a low, wide key with a bright top lip.
        const float h = size * 0.74f;
        const gfx::Rect r{x, cy - h * 0.5f, w, h};
        list.bordered_rect(r, h * 0.34f, style.body, 1.5f, style.edge);
        list.line(x + h * 0.45f, r.y + 3.0f, x + w - h * 0.45f, r.y + 3.0f, 2.0f,
                  style.ink.with_alpha(0.5f));
        label(list, fonts, style, button == Button::l1 ? "L1" : "R1", cx, cy + 1.0f, size * 0.4f);
        return;
    }
    case Button::l2:
    case Button::r2:
    {
        // Trigger: taller, with a lit, rounded top like the DualSense trigger.
        const float h = size * 0.92f;
        const gfx::Rect r{x, cy - h * 0.5f, w, h};
        list.bordered_rect(r, h * 0.32f, style.body, 1.5f, style.edge);
        list.rounded_rect({x + 4, r.y + 4, w - 8, h * 0.24f}, h * 0.12f,
                          style.ink.with_alpha(0.16f));
        label(list, fonts, style, button == Button::l2 ? "L2" : "R2", cx, cy + 3.0f, size * 0.4f);
        return;
    }
    case Button::options:
    {
        // Options: a small pill carrying the three-line menu mark.
        const float h = size * 0.7f;
        list.bordered_rect({x, cy - h * 0.5f, w, h}, h * 0.5f, style.body, 1.5f, style.edge);
        const float half = w * 0.2f;
        const float stroke = size * 0.07f;
        for (int i = -1; i <= 1; ++i)
        {
            const float y = cy + static_cast<float>(i) * size * 0.13f;
            list.line(cx - half, y, cx + half, y, stroke, style.ink);
        }
        return;
    }
    case Button::left_stick:
        stick(list, fonts, style, "L", cx, cy, size);
        return;
    case Button::right_stick:
        stick(list, fonts, style, "R", cx, cy, size);
        return;
    case Button::dpad:
    {
        const float half = size * 0.5f;
        list.circle(cx, cy, half, style.body);
        list.ring(cx, cy, half - 1.0f, 1.5f, style.edge);
        const float arm = size * 0.3f;
        const float thick = size * 0.2f;
        list.rounded_rect({cx - thick * 0.5f, cy - arm, thick, arm * 2}, thick * 0.3f, style.ink);
        list.rounded_rect({cx - arm, cy - thick * 0.5f, arm * 2, thick}, thick * 0.3f, style.ink);
        list.circle(cx, cy, thick * 0.22f, style.body);
        return;
    }
    case Button::touchpad:
    {
        const float h = size * 0.72f;
        list.bordered_rect({x, cy - h * 0.5f, w, h}, h * 0.22f, style.body, 1.5f, style.edge);
        list.line(cx, cy - h * 0.3f, cx, cy + h * 0.3f, 1.5f, style.edge);
        return;
    }
    }
}

namespace
{
constexpr float kIconGap = 12.0f;
constexpr float kPairGap = 6.0f;
} // namespace

float measure_hints(const Fonts &fonts, const Hint *hints, int count, const HintLayout &layout)
{
    const FontRef &face = layout.font != nullptr ? *layout.font : fonts.regular;
    float total = 0.0f;
    for (int i = 0; i < count; ++i)
    {
        const Hint &h = hints[i];
        total += button_width(h.button, layout.size) + kIconGap +
                 face.measure(h.label, layout.text_size);
        if (h.second != Button::none)
            total += kPairGap + button_width(h.second, layout.size);
        if (i + 1 < count)
            total += layout.item_gap;
    }
    return total;
}

float draw_hints(gfx::DrawList &list, const Fonts &fonts, const GlyphStyle &style,
                 const Hint *hints, int count, float x, bool right_align, const HintLayout &layout)
{
    const FontRef &face = layout.font != nullptr ? *layout.font : fonts.regular;
    const float total = measure_hints(fonts, hints, count, layout);
    float cursor = right_align ? x - total : x;
    for (int i = 0; i < count; ++i)
    {
        const Hint &h = hints[i];
        draw_button(list, fonts, style, h.button, cursor, layout.cy, layout.size);
        cursor += button_width(h.button, layout.size);
        if (h.second != Button::none)
        {
            cursor += kPairGap;
            draw_button(list, fonts, style, h.second, cursor, layout.cy, layout.size);
            cursor += button_width(h.second, layout.size);
        }
        cursor += kIconGap;
        text(list, face, h.label, cursor, layout.cy + layout.text_size * 0.35f, layout.text_size,
             style.label);
        cursor += face.measure(h.label, layout.text_size) + layout.item_gap;
    }
    return total;
}

} // namespace hui::ui
