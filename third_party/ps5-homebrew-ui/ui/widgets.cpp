// ps5-homebrew-ui - Themeable widgets: one set of controls, many design languages.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/widgets.hpp"

#include "core/tween.hpp"
#include "ui/pixel_font.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace hui::ui
{

namespace
{

using gfx::Color;
using gfx::Rect;

const Color kClear{0.0f, 0.0f, 0.0f, 0.0f};
const Color kWhite{1.0f, 1.0f, 1.0f, 1.0f};

// amount > 0 lightens toward white, < 0 darkens toward black.
Color shade(Color c, float amount)
{
    const Color target =
        amount > 0.0f ? Color{1.0f, 1.0f, 1.0f, c.a} : Color{0.0f, 0.0f, 0.0f, c.a};
    return gfx::mix(c, target, std::fabs(amount));
}

// Baseline that centres a line of text of the given size on cy.
float baseline_for(float cy, float size)
{
    return cy + size * 0.35f;
}

// A repeatable "random" offset in -1..1 for hand-drawn lines. It depends on
// the size of the shape only, so a control keeps its wobble while it moves.
float wobble(const Rect &r, int index)
{
    const float seed = r.w * 12.9898f + r.h * 78.233f + static_cast<float>(index) * 37.719f;
    const float value = std::sin(seed) * 43758.5453f;
    return (value - std::floor(value)) * 2.0f - 1.0f;
}

bool stroke_only(SurfaceStyle style)
{
    return style == SurfaceStyle::outline || style == SurfaceStyle::glow;
}

} // namespace

Painter::Painter(gfx::DrawList &list, const Fonts &fonts, const Theme &theme,
                 std::uint32_t glass_texture)
    : list_(list), fonts_(fonts), theme_(theme), glass_(glass_texture)
{
}

gfx::Color Painter::on(gfx::Color background)
{
    const float luminance = 0.299f * background.r + 0.587f * background.g + 0.114f * background.b;
    return luminance > 0.58f ? Color::rgb(0x000000) : Color::rgb(0xffffff);
}

// A bitmap face looks right when each of its 8 rows covers a whole number
// of pixels. Sizes snap to steps of 4 (half a pixel per row at 1080p, a whole
// one at 2160p) and never go below 12, the smallest that reads from a sofa.
float Painter::pixel_em(float size)
{
    return std::max(12.0f, std::round(size / 4.0f) * 4.0f);
}

const FontRef &Painter::font(FontRole role) const
{
    switch (role)
    {
    case FontRole::semibold:
        return fonts_.semibold;
    case FontRole::display:
        return fonts_.display;
    case FontRole::mono:
        return fonts_.mono;
    case FontRole::pixel:
        return fonts_.pixel.font != nullptr ? fonts_.pixel : fonts_.mono;
    case FontRole::hand:
        return fonts_.hand.font != nullptr ? fonts_.hand : fonts_.regular;
    default:
        return fonts_.regular;
    }
}

float Painter::heading(std::string_view value, float x, float baseline, float size,
                       gfx::Align align)
{
    return heading(value, x, baseline, size, theme_.text, align);
}

float Painter::heading(std::string_view value, float x, float baseline, float size,
                       gfx::Color color, gfx::Align align)
{
    if (theme_.heading == FontRole::pixel)
    {
        if (fonts_.pixel.font == nullptr)
            return pixel_text(list_, value, x, baseline, size * 0.66f, color, align);
        return text(list_, fonts_.pixel, value, x, baseline, pixel_em(size * 0.7f), color, align);
    }
    if (theme_.heading == FontRole::hand)
        return text(list_, font(theme_.heading), value, x, baseline, size * 1.16f, color, align);
    return text(list_, font(theme_.heading), value, x, baseline, size, color, align);
}

float Painter::heading_width(std::string_view value, float size) const
{
    if (theme_.heading == FontRole::pixel)
    {
        if (fonts_.pixel.font == nullptr)
            return pixel_text_width(value, size * 0.66f);
        return fonts_.pixel.measure(value, pixel_em(size * 0.7f));
    }
    if (theme_.heading == FontRole::hand)
        return font(theme_.heading).measure(value, size * 1.16f);
    return font(theme_.heading).measure(value, size);
}

float Painter::label(std::string_view value, float x, float baseline, float size, gfx::Color color,
                     gfx::Align align)
{
    if (theme_.label == FontRole::pixel)
    {
        if (fonts_.pixel.font == nullptr)
            return pixel_text(list_, value, x, baseline - size * 0.04f, size * 0.62f, color, align);
        return text(list_, fonts_.pixel, value, x, baseline - size * 0.04f, pixel_em(size * 0.62f),
                    color, align);
    }
    if (theme_.label == FontRole::hand)
        return text(list_, font(theme_.label), value, x, baseline, size * 1.18f, color, align);
    // Capitals read larger than mixed case: shrink them a little.
    if (theme_.caps)
        return text(list_, font(theme_.label), upper(value), x, baseline, size * 0.86f, color,
                    align, theme_.tracking);
    return text(list_, font(theme_.label), value, x, baseline, size, color, align, theme_.tracking);
}

float Painter::label_width(std::string_view value, float size) const
{
    if (theme_.label == FontRole::pixel)
    {
        if (fonts_.pixel.font == nullptr)
            return pixel_text_width(value, size * 0.62f);
        return fonts_.pixel.measure(value, pixel_em(size * 0.62f));
    }
    if (theme_.label == FontRole::hand)
        return font(theme_.label).measure(value, size * 1.18f);
    if (theme_.caps)
        return font(theme_.label).measure(upper(value), size * 0.86f, theme_.tracking);
    return font(theme_.label).measure(value, size, theme_.tracking);
}

float Painter::body(std::string_view value, float x, float baseline, float size, gfx::Color color,
                    gfx::Align align)
{
    if (theme_.label == FontRole::pixel)
    {
        if (fonts_.pixel.font == nullptr)
            return pixel_text(list_, value, x, baseline - size * 0.04f, size * 0.58f, color, align);
        return text(list_, fonts_.pixel, value, x, baseline - size * 0.04f, pixel_em(size * 0.62f),
                    color, align);
    }
    if (theme_.label == FontRole::hand)
        return text(list_, font(theme_.label), value, x, baseline, size * 1.14f, color, align);
    if (theme_.label == FontRole::mono)
        return text(list_, fonts_.mono, value, x, baseline, size * 0.92f, color, align);
    return text(list_, fonts_.regular, value, x, baseline, size, color, align);
}

float Painter::body_width(std::string_view value, float size) const
{
    if (theme_.label == FontRole::pixel)
    {
        if (fonts_.pixel.font == nullptr)
            return pixel_text_width(value, size * 0.58f);
        return fonts_.pixel.measure(value, pixel_em(size * 0.62f));
    }
    if (theme_.label == FontRole::hand)
        return font(theme_.label).measure(value, size * 1.14f);
    if (theme_.label == FontRole::mono)
        return fonts_.mono.measure(value, size * 0.92f);
    return fonts_.regular.measure(value, size);
}

float Painter::control_radius(const gfx::Rect &r) const
{
    return std::min(theme_.radius, std::min(r.w, r.h) * 0.5f);
}

void Painter::fill(const gfx::Rect &r, float radius, gfx::Color color)
{
    if (color.a <= 0.0f)
        return;
    if (theme_.corner == Corner::chamfer)
    {
        list_.chamfer_rect(r, radius, color);
    }
    else if (theme_.corner == Corner::pixel)
    {
        // A plus-shaped fill leaves one notch in every corner.
        const float n = std::min(theme_.border, std::min(r.w, r.h) * 0.25f);
        list_.rounded_rect({r.x + n, r.y, r.w - 2 * n, r.h}, 0, color);
        list_.rounded_rect({r.x, r.y + n, n, r.h - 2 * n}, 0, color);
        list_.rounded_rect({r.x + r.w - n, r.y + n, n, r.h - 2 * n}, 0, color);
    }
    else if (theme_.style == SurfaceStyle::sketch)
    {
        // Paper cut by hand is never quite square to the page.
        list_.rotated_rect(r, radius, wobble(r, 0) * 0.006f, color);
    }
    else
    {
        list_.rounded_rect(r, radius, color);
    }
}

void Painter::stroke(const gfx::Rect &r, float radius, float width, gfx::Color color)
{
    if (color.a <= 0.0f || width <= 0.0f)
        return;
    if (theme_.corner == Corner::chamfer)
    {
        list_.chamfer_rect(r, radius, kClear, width, color);
    }
    else if (theme_.corner == Corner::pixel)
    {
        // Four bars that stop short of the corners: the outline of a
        // pixel-art box, one dot missing at each corner.
        const float n = std::min(width, std::min(r.w, r.h) * 0.25f);
        list_.rounded_rect({r.x + n, r.y, r.w - 2 * n, width}, 0, color);
        list_.rounded_rect({r.x + n, r.y + r.h - width, r.w - 2 * n, width}, 0, color);
        list_.rounded_rect({r.x, r.y + n, width, r.h - 2 * n}, 0, color);
        list_.rounded_rect({r.x + r.w - width, r.y + n, width, r.h - 2 * n}, 0, color);
    }
    else if (theme_.style == SurfaceStyle::sketch)
    {
        // Four pen strokes. Each end misses its corner by a pixel or two and
        // overshoots a little, the way ruled-by-hand lines cross.
        const float small = std::min(r.w, r.h);
        const float j = std::min(2.2f, small * 0.05f);
        const float over = std::min(3.0f, small * 0.07f);
        const float x0 = r.x, y0 = r.y, x1 = r.x + r.w, y1 = r.y + r.h;
        list_.line(x0 - over, y0 + wobble(r, 1) * j, x1 + over, y0 + wobble(r, 2) * j, width,
                   color);
        list_.line(x1 + wobble(r, 3) * j, y0 - over, x1 + wobble(r, 4) * j, y1 + over, width,
                   color);
        list_.line(x1 + over, y1 + wobble(r, 5) * j, x0 - over, y1 + wobble(r, 6) * j, width,
                   color);
        list_.line(x0 + wobble(r, 7) * j, y1 + over, x0 + wobble(r, 8) * j, y0 - over, width,
                   color);
    }
    else
    {
        list_.bordered_rect(r, radius, Color{color.r, color.g, color.b, 0.0f}, width, color);
    }
}

gfx::Rect Painter::surface(const gfx::Rect &r, float radius, gfx::Color fill_color, gfx::Color edge,
                           float raise, float border)
{
    raise = tween::clamp01(raise);
    const float offset = theme_.shadow_offset;
    const float line = border < 0.0f ? theme_.border : border;
    switch (theme_.style)
    {
    case SurfaceStyle::flat:
        fill(r, radius, shade(fill_color, -0.12f * (1.0f - raise)));
        stroke(r, radius, line, edge);
        return r;

    case SurfaceStyle::soft:
        // Elevation: the shadow slides out from under the surface as it rises.
        if (theme_.shadow.a > 0.0f)
            list_.shadow({r.x, r.y + offset * raise, r.w, r.h}, radius,
                         theme_.shadow_blur * (0.35f + 0.65f * raise), theme_.shadow);
        fill(r, radius, shade(fill_color, -0.08f * (1.0f - raise)));
        stroke(r, radius, line, edge);
        return r;

    case SurfaceStyle::hard:
    {
        // The body sits up-left of a solid shadow and travels into it.
        const float travel = offset * (1.0f - raise);
        const Rect body{r.x + travel, r.y + travel, r.w, r.h};
        fill({r.x + offset, r.y + offset, r.w, r.h}, radius, theme_.shadow);
        fill(body, radius, fill_color);
        stroke(body, radius, line, edge);
        return body;
    }

    case SurfaceStyle::neumorphic:
    {
        // Raised: lit from the top-left, shaded bottom-right. Pressed: the
        // outer shadows fade and the face darkens toward the light source.
        list_.shadow({r.x - offset, r.y - offset, r.w, r.h}, radius, theme_.shadow_blur,
                     theme_.light.with_alpha(raise));
        list_.shadow({r.x + offset, r.y + offset, r.w, r.h}, radius, theme_.shadow_blur,
                     theme_.shadow.with_alpha(raise));
        const Color dark{theme_.shadow.r, theme_.shadow.g, theme_.shadow.b, fill_color.a};
        list_.gradient_rect(r, radius, gfx::mix(fill_color, dark, 0.45f * (1.0f - raise)),
                            shade(fill_color, 0.25f * (1.0f - raise)));
        return r;
    }

    case SurfaceStyle::bevel:
    {
        // Two edges catch the light, two are in shade; pressing swaps them.
        const bool down = raise < 0.5f;
        const float b = theme_.border;
        const Color top_left = down ? theme_.shadow : theme_.light;
        const Color bottom_right = down ? theme_.light : theme_.shadow;
        list_.rounded_rect(r, 0, fill_color);
        list_.rounded_rect({r.x, r.y, r.w, b}, 0, top_left);
        list_.rounded_rect({r.x, r.y, b, r.h}, 0, top_left);
        list_.rounded_rect({r.x, r.y + r.h - b, r.w, b}, 0, bottom_right);
        list_.rounded_rect({r.x + r.w - b, r.y, b, r.h}, 0, bottom_right);
        // The black outer line on the shaded side gives the classic chisel.
        list_.rounded_rect({r.x, r.y + r.h - 1.5f, r.w, 1.5f}, 0, down ? theme_.light : edge);
        list_.rounded_rect({r.x + r.w - 1.5f, r.y, 1.5f, r.h}, 0, down ? theme_.light : edge);
        const float nudge = down ? 2.0f : 0.0f;
        return {r.x + nudge, r.y + nudge, r.w, r.h};
    }

    case SurfaceStyle::gloss:
    {
        list_.shadow({r.x, r.y + offset * raise, r.w, r.h}, radius, theme_.shadow_blur,
                     theme_.shadow.with_alpha(0.4f + 0.6f * raise));
        const Color base = shade(fill_color, -0.14f * (1.0f - raise));
        list_.gradient_rect(r, radius, shade(base, 0.2f), shade(base, -0.16f));
        // The glassy highlight: a lighter band over the top half.
        list_.gradient_rect({r.x + 2, r.y + 2, r.w - 4, r.h * 0.48f}, std::max(0.0f, radius - 2),
                            theme_.light, theme_.light.with_alpha(0.12f));
        stroke(r, radius, line, shade(fill_color, -0.5f));
        return r;
    }

    case SurfaceStyle::glass:
        list_.shadow({r.x, r.y + offset * raise, r.w, r.h}, radius, theme_.shadow_blur,
                     theme_.shadow.with_alpha(0.5f * raise));
        if (glass_ != 0)
            list_.glass(glass_, r, radius, kWhite);
        list_.rounded_rect(r, radius, shade(fill_color, -0.2f * (1.0f - raise)));
        stroke(r, radius, line, theme_.light);
        return r;

    case SurfaceStyle::outline:
        fill(r, radius, shade(fill_color, -0.2f * (1.0f - raise)));
        stroke(r, radius, line, edge);
        return r;

    case SurfaceStyle::glow:
        list_.glow(r, radius, 16.0f, edge.with_alpha(0.3f * raise));
        fill(r, radius, shade(fill_color, -0.2f * (1.0f - raise)));
        stroke(r, radius, line, edge);
        return r;

    case SurfaceStyle::pixel:
    {
        // A darker band inside the bottom and right edges is the whole depth
        // cue of 8-bit buttons; pressed, the band jumps to the top and left.
        const float b = theme_.border;
        const Color band = shade(fill_color, -0.3f);
        fill(r, radius, fill_color);
        if (raise >= 0.5f)
        {
            list_.rounded_rect({r.x + b, r.y + r.h - 2 * b, r.w - 2 * b, b}, 0, band);
            list_.rounded_rect({r.x + r.w - 2 * b, r.y + b, b, r.h - 2 * b}, 0, band);
        }
        else
        {
            list_.rounded_rect({r.x + b, r.y + b, r.w - 2 * b, b}, 0, band);
            list_.rounded_rect({r.x + b, r.y + b, b, r.h - 2 * b}, 0, band);
        }
        stroke(r, radius, b, edge);
        return r;
    }

    case SurfaceStyle::sketch:
    {
        // A soft shadow that leans down and right, as under a lifted sheet.
        list_.shadow({r.x + 4.0f, r.y + offset * raise * 0.7f, r.w - 8.0f, r.h}, 6.0f,
                     theme_.shadow_blur, theme_.shadow.with_alpha(0.7f * raise));
        const Rect body{r.x, r.y + 2.0f * (1.0f - raise), r.w, r.h};
        fill(body, radius, fill_color);
        stroke(body, radius, line, edge);
        return body;
    }
    }
    return r;
}

void Painter::well(const gfx::Rect &r, float radius, gfx::Color fill_color)
{
    switch (theme_.style)
    {
    case SurfaceStyle::neumorphic:
    {
        // Pressed into the page: dark at the top, light at the bottom.
        const Color dark{theme_.shadow.r, theme_.shadow.g, theme_.shadow.b, 1.0f};
        list_.gradient_rect(r, radius, gfx::mix(fill_color, dark, 0.5f), shade(fill_color, 0.35f));
        return;
    }
    case SurfaceStyle::bevel:
    {
        const float b = theme_.border;
        list_.rounded_rect(r, 0, fill_color);
        list_.rounded_rect({r.x, r.y, r.w, b}, 0, theme_.shadow);
        list_.rounded_rect({r.x, r.y, b, r.h}, 0, theme_.shadow);
        list_.rounded_rect({r.x, r.y + r.h - b, r.w, b}, 0, theme_.light);
        list_.rounded_rect({r.x + r.w - b, r.y, b, r.h}, 0, theme_.light);
        return;
    }
    case SurfaceStyle::gloss:
        // An inner shadow: darker at the top, where the rim would shade it.
        list_.gradient_rect(r, radius, shade(fill_color, -0.22f), shade(fill_color, 0.1f));
        stroke(r, radius, theme_.border, shade(fill_color, -0.45f));
        return;
    case SurfaceStyle::glass:
        list_.rounded_rect(r, radius, fill_color);
        stroke(r, radius, theme_.border, theme_.outline);
        return;
    default:
        fill(r, radius, fill_color);
        stroke(r, radius, theme_.border, theme_.outline);
        return;
    }
}

// Light that spreads outward from a shape's edge and leaves its inside
// alone (DrawList::glow fills the inside too, which washes out a control
// when the ring is drawn after it). Built from strokes of falling opacity.
void Painter::halo(const gfx::Rect &r, float radius, float spread, gfx::Color color)
{
    constexpr int kSteps = 5;
    const float width = spread / static_cast<float>(kSteps);
    for (int i = 0; i < kSteps; ++i)
    {
        const float out = width * static_cast<float>(i + 1);
        const float fall = 1.0f - static_cast<float>(i) / static_cast<float>(kSteps);
        list_.bordered_rect(r.inset(-out), radius > 0.0f ? radius + out : 0.0f,
                            gfx::Color{0.0f, 0.0f, 0.0f, 0.0f}, width + 0.75f,
                            color.with_alpha(fall * fall));
    }
}

void Painter::focus_ring(const gfx::Rect &r, float radius, float amount)
{
    if (amount <= 0.01f)
        return;
    list_.push_opacity(tween::clamp01(amount));
    const float reach = theme_.focus_gap + theme_.focus_width;
    const float ring_radius = radius > 0.0f ? radius + reach : 0.0f;
    switch (theme_.style)
    {
    case SurfaceStyle::bevel:
    {
        // The dotted rectangle inside the control.
        const Rect in = r.inset(7.0f);
        const int across = static_cast<int>(std::ceil((in.w - 3.0f) / 6.0f));
        for (int i = 0; i < across; ++i)
        {
            const float x = in.x + static_cast<float>(i) * 6.0f;
            list_.rounded_rect({x, in.y, 3, 1.5f}, 0, theme_.focus);
            list_.rounded_rect({x, in.y + in.h - 1.5f, 3, 1.5f}, 0, theme_.focus);
            // A light dot in every gap: the pair reads on any fill.
            list_.rounded_rect({x + 3.0f, in.y, 3, 1.5f}, 0, theme_.light.with_alpha(0.7f));
            list_.rounded_rect({x + 3.0f, in.y + in.h - 1.5f, 3, 1.5f}, 0,
                               theme_.light.with_alpha(0.7f));
        }
        const int down = static_cast<int>(std::ceil((in.h - 3.0f) / 6.0f));
        for (int i = 0; i < down; ++i)
        {
            const float y = in.y + static_cast<float>(i) * 6.0f;
            list_.rounded_rect({in.x, y, 1.5f, 3}, 0, theme_.focus);
            list_.rounded_rect({in.x + in.w - 1.5f, y, 1.5f, 3}, 0, theme_.focus);
            list_.rounded_rect({in.x, y + 3.0f, 1.5f, 3}, 0, theme_.light.with_alpha(0.7f));
            list_.rounded_rect({in.x + in.w - 1.5f, y + 3.0f, 1.5f, 3}, 0,
                               theme_.light.with_alpha(0.7f));
        }
        break;
    }
    case SurfaceStyle::hard:
    {
        // The ring wraps the body and its shadow together.
        const float gap = theme_.border + 3.0f;
        const float o = theme_.shadow_offset;
        stroke({r.x - gap, r.y - gap, r.w + 2 * gap + o, r.h + 2 * gap + o}, radius, theme_.border,
               theme_.focus);
        break;
    }
    case SurfaceStyle::glow:
        halo(r.inset(-5.0f), radius + 4.0f, 20.0f, theme_.focus.with_alpha(0.5f));
        stroke(r.inset(-5.0f), radius + 4.0f, 2.0f, theme_.focus);
        break;
    case SurfaceStyle::soft:
    case SurfaceStyle::gloss:
    case SurfaceStyle::glass:
        // Opaque rings get a little light around them; translucent ones
        // (the web's "focus halo") already are that light.
        if (theme_.focus.a > 0.9f)
            halo(r.inset(-reach), ring_radius, 12.0f, theme_.focus.with_alpha(0.32f));
        stroke(r.inset(-reach), ring_radius, theme_.focus_width, theme_.focus);
        break;
    default:
        stroke(r.inset(-reach), ring_radius, theme_.focus_width, theme_.focus);
        break;
    }
    list_.pop_opacity();
}

void Painter::panel(const gfx::Rect &r)
{
    const float radius = std::min(theme_.radius_card, std::min(r.w, r.h) * 0.5f);
    // Flat languages keep their buttons flat but may still float a card.
    if (theme_.style == SurfaceStyle::flat && theme_.shadow.a > 0.0f && theme_.shadow_blur > 0.0f)
        list_.shadow({r.x, r.y + theme_.shadow_offset, r.w, r.h}, radius, theme_.shadow_blur,
                     theme_.shadow);
    surface(r, radius, theme_.surface, theme_.outline, 1.0f);
}

void Painter::button(const gfx::Rect &r, std::string_view value, ButtonKind kind, const Look &look)
{
    if (look.disabled)
        list_.push_opacity(0.4f);
    const float radius = control_radius(r);
    const float line = theme_.button_border < 0.0f ? theme_.border : theme_.button_border;
    const bool framed = theme_.style == SurfaceStyle::hard || theme_.style == SurfaceStyle::pixel ||
                        theme_.style == SurfaceStyle::bevel;
    Rect content = r;
    Color ink = theme_.text;
    if (kind == ButtonKind::primary)
    {
        if (theme_.style == SurfaceStyle::glow)
        {
            // On lit consoles the edge carries the colour, not the fill.
            content = surface(r, radius, theme_.surface, theme_.primary, 1.0f - look.press);
            ink = theme_.primary;
        }
        else
        {
            const Color edge = framed ? theme_.outline
                                      : (theme_.style == SurfaceStyle::sketch ? theme_.on_primary
                                                                              : theme_.primary);
            content =
                surface(r, radius, theme_.primary, edge, 1.0f - look.press,
                        framed || theme_.style == SurfaceStyle::sketch ? theme_.border : line);
            ink = theme_.on_primary;
        }
    }
    else if (kind == ButtonKind::secondary)
    {
        // A transparent secondary is an outline button in its text colour.
        const bool hollow = theme_.secondary.a <= 0.01f;
        content = surface(r, radius, theme_.secondary,
                          hollow ? theme_.on_secondary : theme_.outline, 1.0f - look.press,
                          hollow ? std::max(theme_.border, 1.5f) : (framed ? theme_.border : line));
        ink = theme_.on_secondary;
    }
    else
    {
        // A ghost has no body; pressing dims it instead.
        ink = theme_.style == SurfaceStyle::bevel || theme_.style == SurfaceStyle::pixel
                  ? theme_.text
                  : (theme_.style == SurfaceStyle::sketch ? theme_.on_primary : theme_.primary);
        ink = ink.with_alpha(1.0f - 0.35f * look.press);
        const float w = label_width(value, 24);
        list_.rounded_rect({r.cx() - w * 0.5f, r.y + r.h - 13.0f, w, 2.0f}, 0.0f,
                           ink.with_alpha(0.45f));
    }
    label(value, content.cx(), baseline_for(content.cy(), 24), 24, ink, gfx::Align::center);
    focus_ring(r, radius, look.focus);
    if (look.disabled)
        list_.pop_opacity();
}

void Painter::toggle(const gfx::Rect &r, float value, const Look &look)
{
    value = tween::clamp01(value);
    const float radius = theme_.pill_switches ? r.h * 0.5f : control_radius(r);
    const bool strokes = stroke_only(theme_.style);
    well(r, radius,
         strokes ? theme_.surface_high : gfx::mix(theme_.surface_high, theme_.accent, value));
    const bool framed = theme_.style == SurfaceStyle::hard || theme_.style == SurfaceStyle::bevel ||
                        theme_.style == SurfaceStyle::pixel;
    const float pad = framed ? theme_.border + 2.0f : 5.0f;
    const float size = r.h - 2.0f * pad;
    const Rect thumb{r.x + pad + (r.w - 2.0f * pad - size) * value, r.y + pad, size, size};
    const float thumb_radius =
        theme_.pill_switches ? size * 0.5f : std::min(theme_.radius, size * 0.5f);
    switch (theme_.style)
    {
    case SurfaceStyle::outline:
    case SurfaceStyle::glow:
        fill(thumb, thumb_radius, gfx::mix(theme_.text_muted, theme_.accent, value));
        break;
    case SurfaceStyle::hard:
    case SurfaceStyle::pixel:
    case SurfaceStyle::sketch:
        fill(thumb, thumb_radius, kWhite);
        stroke(thumb, thumb_radius, std::min(theme_.border, 4.0f), theme_.outline);
        break;
    case SurfaceStyle::neumorphic:
    case SurfaceStyle::bevel:
    case SurfaceStyle::gloss:
        surface(thumb, thumb_radius, theme_.surface, theme_.outline, 1.0f);
        break;
    default:
        // The white thumb on a small shadow that every web switch has.
        list_.shadow({thumb.x, thumb.y + 2, thumb.w, thumb.h}, thumb_radius, 5,
                     Color::rgb(0x000000, 0.28f));
        fill(thumb, thumb_radius, kWhite);
        break;
    }
    focus_ring(r, radius, look.focus);
}

void Painter::checkbox(const gfx::Rect &r, float value, const Look &look)
{
    value = tween::clamp01(value);
    const float radius = std::min(theme_.radius, 8.0f);
    const bool plain = stroke_only(theme_.style) || theme_.style == SurfaceStyle::bevel ||
                       theme_.style == SurfaceStyle::pixel || theme_.style == SurfaceStyle::sketch;
    const Color box =
        plain ? theme_.surface_high : gfx::mix(theme_.surface_high, theme_.accent, value);
    well(r, radius, box);
    if (value > 0.01f)
    {
        // The check mark draws itself: the short stroke, then the long one.
        Color ink = on(box);
        if (stroke_only(theme_.style))
            ink = theme_.accent;
        else if (plain)
            ink = theme_.text;
        const float s = r.w;
        const float x0 = r.x + s * 0.24f, y0 = r.y + s * 0.52f;
        const float x1 = r.x + s * 0.43f, y1 = r.y + s * 0.70f;
        const float x2 = r.x + s * 0.77f, y2 = r.y + s * 0.31f;
        const float first = tween::clamp01(value * 2.5f);
        const float second = tween::clamp01((value - 0.4f) / 0.6f);
        const float width = std::max(3.0f, s * 0.11f);
        list_.line(x0, y0, x0 + (x1 - x0) * first, y0 + (y1 - y0) * first, width, ink);
        if (second > 0.0f)
            list_.line(x1, y1, x1 + (x2 - x1) * second, y1 + (y2 - y1) * second, width, ink);
    }
    focus_ring(r, radius, look.focus);
}

void Painter::radio(const gfx::Rect &r, float value, const Look &look)
{
    value = tween::clamp01(value);
    // Square languages keep square radio buttons; the dot says "one of many".
    const bool square = theme_.radius < 2.0f;
    const float radius = square ? 0.0f : r.w * 0.5f;
    const bool plain = stroke_only(theme_.style) || theme_.style == SurfaceStyle::bevel ||
                       theme_.style == SurfaceStyle::pixel || theme_.style == SurfaceStyle::sketch;
    const Color box =
        plain ? theme_.surface_high : gfx::mix(theme_.surface_high, theme_.accent, value);
    // A round well in a square-notched language would lose its notches.
    if (!square && theme_.style != SurfaceStyle::sketch && theme_.corner == Corner::round &&
        theme_.style != SurfaceStyle::neumorphic && theme_.style != SurfaceStyle::gloss)
    {
        list_.circle(r.cx(), r.cy(), radius, box);
        if (theme_.border > 0.0f)
            list_.ring(r.cx(), r.cy(), radius, theme_.border, theme_.outline);
    }
    else
    {
        well(r, radius, box);
    }
    const float dot = r.w * 0.22f * tween::back_out(value);
    if (dot > 0.2f)
    {
        Color ink = on(box);
        if (stroke_only(theme_.style))
            ink = theme_.accent;
        else if (plain)
            ink = theme_.text;
        if (square || theme_.corner == Corner::pixel)
            list_.rounded_rect({r.cx() - dot, r.cy() - dot, dot * 2, dot * 2}, 0, ink);
        else
            list_.circle(r.cx(), r.cy(), dot, ink);
    }
    focus_ring(r, radius, look.focus);
}

void Painter::slider(const gfx::Rect &r, float value, const Look &look)
{
    value = tween::clamp01(value);
    const bool chunky = theme_.style == SurfaceStyle::hard || theme_.style == SurfaceStyle::bevel ||
                        theme_.style == SurfaceStyle::pixel;
    const float track_h = chunky ? 2.0f * theme_.border + 8.0f : 8.0f;
    const float thumb_size = std::min(r.h, 34.0f);
    const Rect track{r.x + thumb_size * 0.5f, r.cy() - track_h * 0.5f, r.w - thumb_size, track_h};
    const float track_radius = std::min(theme_.radius, track_h * 0.5f);
    if (theme_.style == SurfaceStyle::sketch)
    {
        // A pen line for the track and a marker stroke for the filled part.
        list_.line(track.x, r.cy() + wobble(r, 1) * 1.5f, track.x + track.w,
                   r.cy() + wobble(r, 2) * 1.5f, theme_.border, theme_.outline);
        if (value > 0.01f)
            list_.line(track.x, r.cy(), track.x + track.w * value, r.cy(), 9.0f,
                       theme_.accent.with_alpha(0.85f));
    }
    else
    {
        well(track, track_radius, theme_.surface_high);
        const float inset = chunky ? theme_.border : 0.0f;
        const Rect filled{track.x + inset, track.y + inset, (track.w - 2 * inset) * value,
                          track.h - 2 * inset};
        if (filled.w > 1.0f)
            list_.rounded_rect(filled, chunky ? 0.0f : track_radius, theme_.accent);
    }
    const Rect thumb{track.x + track.w * value - thumb_size * 0.5f, r.cy() - thumb_size * 0.5f,
                     thumb_size, thumb_size};
    const float thumb_radius = std::min(theme_.radius, thumb_size * 0.5f);
    switch (theme_.style)
    {
    case SurfaceStyle::outline:
    case SurfaceStyle::glow:
        fill(thumb, thumb_radius, theme_.accent);
        break;
    case SurfaceStyle::hard:
    case SurfaceStyle::pixel:
    case SurfaceStyle::sketch:
        fill(thumb, thumb_radius, kWhite);
        stroke(thumb, thumb_radius, std::min(theme_.border, 4.0f), theme_.outline);
        break;
    case SurfaceStyle::neumorphic:
    case SurfaceStyle::bevel:
    case SurfaceStyle::gloss:
        surface(thumb, thumb_radius, theme_.surface, theme_.outline, 1.0f - look.press);
        break;
    default:
        // White with a ring in the accent colour, on a small shadow.
        list_.shadow({thumb.x, thumb.y + 3, thumb.w, thumb.h}, thumb_radius, 7,
                     Color::rgb(0x000000, 0.28f));
        fill(thumb, thumb_radius, kWhite);
        stroke(thumb, thumb_radius, 3.0f, theme_.accent);
        break;
    }
    focus_ring(thumb, thumb_radius, look.focus);
}

void Painter::progress(const gfx::Rect &r, float value)
{
    value = tween::clamp01(value);
    const float radius = std::min(theme_.radius, r.h * 0.5f);
    well(r, radius, theme_.surface_high);
    if (theme_.style == SurfaceStyle::bevel || theme_.corner == Corner::pixel)
    {
        // Blocks, as on old installers and pixel-art health bars.
        // A thin bar keeps a visible block: the inset gives way first.
        const float inset = std::min(theme_.border + 2.0f, r.h * 0.25f);
        const float block = std::max(r.h - 2.0f * inset, 2.0f);
        const float step = block * 0.7f + 3.0f;
        const int count = static_cast<int>((r.w - 2.0f * inset) * value / step);
        for (int i = 0; i < count; ++i)
            list_.rounded_rect(
                {r.x + inset + static_cast<float>(i) * step, r.y + inset, block * 0.7f, block}, 0,
                theme_.style == SurfaceStyle::bevel ? theme_.primary : theme_.accent);
        return;
    }
    const float inset = theme_.style == SurfaceStyle::hard ? theme_.border : 0.0f;
    const Rect filled{r.x + inset, r.y + inset, (r.w - 2.0f * inset) * value, r.h - 2.0f * inset};
    if (filled.w < 1.0f)
        return;
    if (theme_.style == SurfaceStyle::gloss)
    {
        list_.gradient_rect(filled, radius, shade(theme_.accent, 0.25f),
                            shade(theme_.accent, -0.15f));
        list_.gradient_rect({filled.x + 1, filled.y + 1, filled.w - 2, filled.h * 0.45f}, radius,
                            theme_.light, theme_.light.with_alpha(0.1f));
    }
    else if (theme_.style == SurfaceStyle::glow)
    {
        list_.glow(filled, radius, 12.0f, theme_.accent.with_alpha(0.5f));
        fill(filled, radius, theme_.accent);
    }
    else
    {
        list_.rounded_rect(filled, std::max(0.0f, radius - inset), theme_.accent);
    }
}

void Painter::tabs(const gfx::Rect &r, std::span<const char *const> labels, float active,
                   const Look &look)
{
    const int count = static_cast<int>(labels.size());
    if (count == 0)
        return;
    const float seg = r.w / static_cast<float>(count);
    const float radius = control_radius(r);
    const int nearest = static_cast<int>(active + 0.5f);
    const bool boxed = theme_.style == SurfaceStyle::hard || theme_.style == SurfaceStyle::bevel ||
                       theme_.style == SurfaceStyle::pixel;
    const bool segmented = theme_.style == SurfaceStyle::neumorphic ||
                           theme_.style == SurfaceStyle::glass ||
                           theme_.style == SurfaceStyle::gloss ||
                           (theme_.style == SurfaceStyle::soft && theme_.radius >= 5.0f);
    if (segmented)
    {
        // A segmented control: a well with a raised piece that slides.
        well(r, radius, theme_.surface_high);
        const Rect piece{r.x + 4.0f + active * seg, r.y + 4.0f, seg - 8.0f, r.h - 8.0f};
        const bool tinted = theme_.style != SurfaceStyle::neumorphic;
        surface(piece, std::max(0.0f, radius - 4.0f), tinted ? theme_.primary : theme_.surface,
                theme_.outline, 1.0f, 0.0f);
        for (int i = 0; i < count; ++i)
        {
            const Color ink =
                i == nearest ? (tinted ? theme_.on_primary : theme_.primary) : theme_.text_muted;
            label(labels[static_cast<std::size_t>(i)], r.x + (static_cast<float>(i) + 0.5f) * seg,
                  baseline_for(r.cy(), 22), 22, ink, gfx::Align::center);
        }
    }
    else if (boxed)
    {
        // Separate tabs; the selected one is pushed in.
        for (int i = 0; i < count; ++i)
        {
            const bool selected = i == nearest;
            const Rect tab{r.x + static_cast<float>(i) * seg, r.y, seg - 12.0f, r.h};
            const bool tint = selected && theme_.style != SurfaceStyle::bevel;
            const Rect content = surface(tab, radius, tint ? theme_.accent : theme_.secondary,
                                         theme_.outline, selected ? 0.0f : 1.0f);
            label(labels[static_cast<std::size_t>(i)], content.cx(), baseline_for(content.cy(), 22),
                  22, tint ? on(theme_.accent) : theme_.on_secondary, gfx::Align::center);
        }
    }
    else
    {
        // Text on a rule, with a bar that slides under the selection.
        const Color active_ink =
            theme_.style == SurfaceStyle::sketch ? theme_.on_primary : theme_.primary;
        list_.rounded_rect({r.x, r.y + r.h - 2.0f, r.w, 2.0f}, 0,
                           theme_.text_muted.with_alpha(0.3f));
        const float bar = std::max(4.0f, theme_.border + 2.0f);
        const Rect under{r.x + active * seg + 12.0f, r.y + r.h - bar, seg - 24.0f, bar};
        if (theme_.style == SurfaceStyle::glow)
            list_.glow(under, 0, 10.0f, active_ink.with_alpha(0.6f));
        list_.rounded_rect(under, theme_.radius > 4.0f ? bar * 0.5f : 0.0f, active_ink);
        for (int i = 0; i < count; ++i)
            label(labels[static_cast<std::size_t>(i)], r.x + (static_cast<float>(i) + 0.5f) * seg,
                  baseline_for(r.cy() - 4.0f, 22), 22,
                  i == nearest ? (stroke_only(theme_.style) ? theme_.text : active_ink)
                               : theme_.text_muted,
                  gfx::Align::center);
    }
    focus_ring(r, boxed ? 0.0f : radius, look.focus);
}

void Painter::field(const gfx::Rect &r, std::string_view value, bool caret, const Look &look)
{
    const float radius = std::min(theme_.radius, 12.0f);
    const float baseline = baseline_for(r.cy(), 24);
    if (theme_.underline_fields)
    {
        // A line that thickens and takes the primary colour when focused.
        const float thick = 2.0f + 2.0f * look.focus;
        list_.rounded_rect({r.x, r.y + r.h - thick, r.w, thick}, 0,
                           gfx::mix(theme_.text_muted, theme_.primary, look.focus));
        const float end = body(value, r.x + 2.0f, baseline, 24, theme_.text);
        if (caret)
            list_.rounded_rect({r.x + 5.0f + end, r.cy() - 15.0f, 2.5f, 30.0f}, 0, theme_.primary);
        return;
    }
    const bool white = theme_.style == SurfaceStyle::bevel || theme_.style == SurfaceStyle::hard;
    const Color face = white ? theme_.light : theme_.surface_high;
    well(r, radius, face);
    const Color ink = white ? on(face) : theme_.text;
    const float end = body(value, r.x + 20.0f, baseline, 24, ink);
    if (caret)
        list_.rounded_rect({r.x + 23.0f + end, r.cy() - 15.0f, 2.5f, 30.0f}, 0,
                           white || theme_.style == SurfaceStyle::pixel ? ink : theme_.accent);
    focus_ring(r, radius, look.focus);
}

void Painter::chip(const gfx::Rect &r, std::string_view value, float selected, const Look &look)
{
    selected = tween::clamp01(selected);
    const float radius = theme_.pill_chips ? r.h * 0.5f : control_radius(r);
    const bool strokes = stroke_only(theme_.style);
    const Color body_color =
        strokes ? theme_.surface_high : gfx::mix(theme_.surface_high, theme_.accent, selected);
    // Pills are always round, whatever corner type the language uses.
    if (theme_.pill_chips)
    {
        list_.rounded_rect(r, radius, body_color);
        if (theme_.border > 0.0f)
            list_.bordered_rect(r, radius, kClear, theme_.border,
                                gfx::mix(theme_.outline, theme_.accent, selected));
    }
    else
    {
        fill(r, radius, body_color);
        stroke(r, radius, theme_.style == SurfaceStyle::hard ? theme_.border - 1.0f : theme_.border,
               strokes ? gfx::mix(theme_.outline, theme_.accent, selected) : theme_.outline);
    }
    const Color ink = strokes ? gfx::mix(theme_.text_muted, theme_.accent, selected)
                              : gfx::mix(theme_.text, on(theme_.accent), selected);
    label(value, r.cx(), baseline_for(r.cy(), 19), 19, ink, gfx::Align::center);
    focus_ring(r, radius, look.focus);
}

void Painter::row(const gfx::Rect &r, std::string_view value, std::string_view detail,
                  float selected, const Look &look)
{
    selected = tween::clamp01(selected);
    const float radius = std::min(theme_.radius, 10.0f);
    const bool strokes = stroke_only(theme_.style);
    if (selected > 0.01f)
    {
        if (strokes)
        {
            list_.rounded_rect({r.x, r.y + 6.0f, 5.0f, r.h - 12.0f}, 0,
                               theme_.accent.with_alpha(selected));
            list_.rounded_rect(r, 0, theme_.accent.with_alpha(0.14f * selected));
        }
        else
        {
            list_.rounded_rect(r, radius, theme_.primary.with_alpha(theme_.primary.a * selected));
        }
    }
    // A hairline between rows keeps a list readable without boxes.
    list_.rounded_rect({r.x + 16.0f, r.y + r.h - 1.0f, r.w - 32.0f, 1.5f}, 0,
                       theme_.text_muted.with_alpha(0.22f * (1.0f - selected)));
    const Color ink = strokes ? theme_.text : gfx::mix(theme_.text, theme_.on_primary, selected);
    body(value, r.x + 22.0f, baseline_for(r.cy(), 23), 23, ink);
    body(detail, r.x + r.w - 22.0f, baseline_for(r.cy(), 21), 21,
         strokes ? theme_.text_muted : gfx::mix(theme_.text_muted, theme_.on_primary, selected),
         gfx::Align::right);
    focus_ring(r, radius, look.focus);
}

} // namespace hui::ui
