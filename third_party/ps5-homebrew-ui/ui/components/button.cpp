// ps5-homebrew-ui - Components: PushButton and IconButton.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/button.hpp"

#include "ui/components/overlay.hpp"

#include <algorithm>
#include <cmath>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

// Languages whose buttons have a body that really goes down.
bool physical(SurfaceStyle style)
{
    return style == SurfaceStyle::hard || style == SurfaceStyle::bevel ||
           style == SurfaceStyle::pixel;
}

ButtonKind kind_of(ButtonRole role)
{
    if (role == ButtonRole::secondary)
        return ButtonKind::secondary;
    return role == ButtonRole::ghost ? ButtonKind::ghost : ButtonKind::primary;
}

// The theme a role is drawn with: danger is the primary construction in the
// danger colour, so every surface style keeps working; a radius override
// becomes the theme's control radius.
Theme themed(const Theme &theme, ButtonRole role, float radius)
{
    Theme out = theme;
    if (role == ButtonRole::danger && theme.style == SurfaceStyle::sketch)
    {
        // On paper the main button is a pale wash under a coloured pen; a
        // destructive one is the same in red, or its pen would be white.
        const Color paper{theme.surface.r, theme.surface.g, theme.surface.b, 1.0f};
        out.primary = gfx::mix(paper, theme.danger, 0.2f);
        out.on_primary = gfx::mix(theme.danger, theme.text, 0.25f);
    }
    else if (role == ButtonRole::danger)
    {
        out.primary = theme.danger;
        out.on_primary = Painter::on(theme.danger);
    }
    if (radius >= 0.0f)
        out.radius = radius;
    return out;
}

// Mirrors Painter::button: where the content sits and what colour it has.
ButtonFace face_of(const Theme &theme, const Rect &r, ButtonRole role, const Look &look,
                   bool on_page)
{
    ButtonFace face;
    face.content = r;
    face.radius = std::min(theme.radius, std::min(r.w, r.h) * 0.5f);
    const float press = tween::clamp01(look.press);
    const Color page{theme.page.r, theme.page.g, theme.page.b, 1.0f};
    if (role == ButtonRole::ghost)
    {
        const bool plain = theme.style == SurfaceStyle::bevel || theme.style == SurfaceStyle::pixel;
        Color ink = theme.style == SurfaceStyle::sketch ? theme.on_primary : theme.primary;
        if (plain)
            ink = on_page && theme.page_text.a > 0.0f ? theme.page_text : theme.text;
        face.ink = ink.with_alpha(1.0f - 0.35f * press);
        face.fill = on_page ? page : opaque_over(page, theme.surface);
        return face;
    }
    switch (theme.style)
    {
    case SurfaceStyle::hard:
        face.content.x += theme.shadow_offset * press;
        face.content.y += theme.shadow_offset * press;
        break;
    case SurfaceStyle::bevel:
        if (press > 0.5f)
        {
            face.content.x += 2.0f;
            face.content.y += 2.0f;
        }
        break;
    case SurfaceStyle::sketch:
        face.content.y += 2.0f * press;
        break;
    default:
        break;
    }
    if (role == ButtonRole::secondary)
    {
        face.ink = theme.on_secondary;
        face.fill = opaque_over(page, theme.secondary);
    }
    else if (theme.style == SurfaceStyle::glow)
    {
        face.ink = theme.primary;
        face.fill = opaque_over(page, theme.surface);
    }
    else
    {
        face.ink = theme.on_primary;
        face.fill = opaque_over(page, theme.primary);
    }
    return face;
}

// The corner an icon button of this shape gets.
float icon_radius(const Theme &theme, IconButtonShape shape, float size)
{
    if (shape == IconButtonShape::square)
        return -1.0f;
    if (theme.corner == Corner::chamfer)
        return size * 0.29f; // an octagon: a full cut would leave a diamond
    // Bevels, notches and pen strokes have no circle: they keep their corners.
    if (theme.corner == Corner::pixel || theme.style == SurfaceStyle::bevel ||
        theme.style == SurfaceStyle::sketch)
        return -1.0f;
    return size * 0.5f;
}

float baseline_for(float cy, float size)
{
    return cy + size * 0.35f;
}

} // namespace

ButtonMetrics button_metrics(ButtonSize size)
{
    switch (size)
    {
    case ButtonSize::small:
        return {48.0f, 20.0f, 18.0f};
    case ButtonSize::large:
        return {80.0f, 28.0f, 34.0f};
    default:
        return {64.0f, 24.0f, 26.0f};
    }
}

ButtonMetrics button_metrics(ButtonSize size, float height, float text_size, float padding)
{
    ButtonMetrics m = button_metrics(size);
    if (height > 0.0f)
        m.height = height;
    if (text_size > 0.0f)
        m.text_size = text_size;
    if (padding > 0.0f)
        m.padding = padding;
    return m;
}

ButtonPaint button_paint(const Theme &theme, ButtonRole role, bool on_page)
{
    const Theme local = themed(theme, role, -1.0f);
    const ButtonFace face = face_of(local, {}, role, Look{}, on_page);
    const float line = local.button_border < 0.0f ? local.border : local.button_border;
    const bool framed = physical(local.style);
    ButtonPaint out;
    out.ink = face.ink;
    if (role == ButtonRole::ghost)
    {
        out.fill = Color{0.0f, 0.0f, 0.0f, 0.0f};
        out.edge = out.fill;
        return out;
    }
    if (role == ButtonRole::secondary)
    {
        // A transparent secondary is an outline button in its text colour.
        const bool hollow = local.secondary.a <= 0.01f;
        out.fill = local.secondary;
        out.edge = hollow ? local.on_secondary : local.outline;
        out.border = hollow ? std::max(local.border, 1.5f) : (framed ? local.border : line);
        return out;
    }
    if (local.style == SurfaceStyle::glow)
    {
        out.fill = local.surface;
        out.edge = local.primary;
        out.border = local.border;
        return out;
    }
    const bool pen = local.style == SurfaceStyle::sketch;
    out.fill = local.primary;
    out.edge = framed ? local.outline : (pen ? local.on_primary : local.primary);
    out.border = framed || pen ? local.border : line;
    return out;
}

ButtonFace button_face(const Theme &theme, const Rect &r, ButtonRole role, const Look &look,
                       float radius, bool on_page)
{
    const Theme local = themed(theme, role, radius);
    return face_of(local, r, role, look, on_page);
}

void draw_button_ring(Canvas &canvas, const Theme &theme, const Rect &r, ButtonRole role,
                      float amount, float radius, bool on_page)
{
    if (amount <= 0.01f)
        return;
    Theme local = themed(theme, role, radius);
    const ButtonFace face = face_of(local, r, role, Look{}, on_page);
    // A bevel's focus is a dotted line inside the control, in the theme's
    // focus colour. On a filled button it takes the label's colour instead,
    // or dark dots would sit on a dark fill.
    if (local.style == SurfaceStyle::bevel &&
        (role == ButtonRole::primary || role == ButtonRole::danger))
        local.focus = Color{face.ink.r, face.ink.g, face.ink.b, 1.0f};
    Painter paint(canvas.list, canvas.fonts, local, 0);
    if (role == ButtonRole::ghost && local.style == SurfaceStyle::hard)
    {
        // The hard ring also wraps a shadow, and a ghost has none.
        canvas.list.push_opacity(tween::clamp01(amount));
        paint.stroke(r.inset(-(local.border + 3.0f)), face.radius, local.border, local.focus);
        canvas.list.pop_opacity();
        return;
    }
    paint.focus_ring(r, face.radius, amount);
}

ButtonFace draw_button_face(Canvas &canvas, const Theme &theme, const Rect &r, ButtonRole role,
                            const Look &look, float radius, bool on_page)
{
    const Theme local = themed(theme, role, radius);
    // A ghost has no body, so there is nothing for glass to show through.
    Painter paint(canvas.list, canvas.fonts, local, role == ButtonRole::ghost ? 0 : canvas.glass);
    Look body = look;
    body.focus = 0.0f;
    paint.button(r, "", kind_of(role), body);
    draw_button_ring(canvas, theme, r, role, look.focus, radius, on_page);
    return face_of(local, r, role, look, on_page);
}

GlyphStyle glyph_style_on(const ButtonFace &face, bool tinted)
{
    if (!tinted)
        return GlyphStyle::mono(face.ink, face.ink.with_alpha(0.16f));
    // Painter::on is black for a light fill: that is a "light page".
    GlyphStyle style = Painter::on(face.fill).r < 0.5f ? GlyphStyle::light() : GlyphStyle::dark();
    style.label = face.ink;
    return style;
}

// ---- PushButton ------------------------------------------------------------

PushButton::PushButton()
{
    // A spinner starts spinning; a button starts idle.
    spinner_.set_spinning(false, true);
}

void PushButton::set_active(bool active)
{
    active_ = active;
    if (!active)
    {
        armed_ = false;
        held_ = false;
    }
}

void PushButton::set_loading(bool loading)
{
    loading_ = loading;
    spinner_.set_spinning(loading);
}

void PushButton::press()
{
    press_.trigger();
}

float PushButton::preferred_width(const Fonts &fonts) const
{
    gfx::DrawList scratch;
    const Painter paint(scratch, fonts, style.theme, 0);
    const ButtonMetrics m =
        button_metrics(style.size, style.height, style.text_size, style.padding);
    const float icon_size = style.icon_size > 0.0f ? style.icon_size : m.text_size * 1.15f;
    const float glyph_size = style.glyph_size > 0.0f ? style.glyph_size : m.text_size * 1.3f;
    float width = 2.0f * m.padding + paint.label_width(label, m.text_size);
    if (icon)
        width += icon_size + (label.empty() ? 0.0f : style.gap);
    if (glyph != Button::none)
        width += button_width(glyph, glyph_size) + style.gap;
    return std::max(width, style.min_width);
}

Rect PushButton::rect(const Fonts &fonts) const
{
    const ButtonMetrics m =
        button_metrics(style.size, style.height, style.text_size, style.padding);
    const float y = bounds_.cy() - m.height * 0.5f;
    if (style.full_width)
        return {bounds_.x, y, bounds_.w, m.height};
    const float w = std::min(preferred_width(fonts), std::max(bounds_.w, 1.0f));
    float x = bounds_.x;
    if (style.align == gfx::Align::center)
        x = bounds_.cx() - w * 0.5f;
    else if (style.align == gfx::Align::right)
        x = bounds_.x + bounds_.w - w;
    return {x, y, w, m.height};
}

Event PushButton::handle(const InputFrame &input, Feedback &feedback)
{
    const float x = bounds_.cx();
    const bool usable = !disabled_ && !loading_;
    if (!input.is_held(Action::confirm) || input.focus_lost)
        armed_ = false;
    Event event = Event::none;
    if (input.is_pressed(Action::confirm))
    {
        if (!usable)
            return refuse(feedback, style, input, refusal_, x);
        armed_ = true;
        press_.trigger();
        play_cue(feedback, style, style.sounds.activate, x);
        if (style.sounds.rumble > 0.0f && style.rumble > 0.0f)
            feedback.rumble(style.rumble * style.sounds.rumble, 0.04f);
        event = Event::activated;
    }
    held_ = armed_ && usable;
    return event;
}

void PushButton::update(float dt)
{
    focus_.target = active_ ? 1.0f : 0.0f;
    focus_.update(dt, std::max(style.omega(), 18.0f));
    // handle() renews the hold every frame; without it the button comes up.
    down_.target = held_ ? 1.0f : 0.0f;
    held_ = false;
    down_.update(dt, std::max(style.omega(), 26.0f));
    busy_.target = loading_ ? 1.0f : 0.0f;
    busy_.update(dt, std::max(style.omega(), 14.0f));
    press_.update(dt, 9.0f);
    refusal_.update(dt, 9.0f);
    spinner_.update(dt);
}

void PushButton::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const ButtonMetrics m =
        button_metrics(style.size, style.height, style.text_size, style.padding);
    Rect r = rect(canvas.fonts);
    r.x += shake(refusal_.value, canvas.time, 8.0f);
    const float focus = tween::clamp01(focus_.value);
    const float press = tween::clamp01(std::max(press_.value, down_.value));
    const float busy = tween::clamp01(busy_.value);
    const float scale =
        style.reduced_motion ? 1.0f : 1.0f + style.focus_scale * focus - style.press_scale * press;

    list.push_transform(scale, r.cx(), r.cy(), 0.0f, 0.0f);
    Look look;
    look.focus = focus;
    look.press = press;
    look.disabled = disabled_;
    const ButtonFace face =
        draw_button_face(canvas, theme, r, style.role, look, -1.0f, style.on_page);
    const Rect &content = face.content;
    const Color ink = face.ink;

    const float icon_size = style.icon_size > 0.0f ? style.icon_size : m.text_size * 1.15f;
    const float glyph_size = style.glyph_size > 0.0f ? style.glyph_size : m.text_size * 1.3f;
    const bool alone = style.loading_hides_label;
    // The spinner takes the icon's place; with no icon the label makes room
    // for it as it appears, so nothing jumps.
    const float lead = icon ? icon_size : (alone ? 0.0f : icon_size * busy);
    const float lead_gap = label.empty() ? 0.0f : style.gap * (icon ? 1.0f : (alone ? 0.0f : busy));
    const float glyph_w = glyph != Button::none ? button_width(glyph, glyph_size) : 0.0f;
    const float glyph_gap = glyph_w > 0.0f ? style.gap : 0.0f;
    const float room =
        std::max(content.w - 2.0f * m.padding - lead - lead_gap - glyph_w - glyph_gap, 0.0f);
    const std::string &text = label;
    const float text_w = label.empty() ? 0.0f : std::min(paint.label_width(text, m.text_size), room);
    const float total = lead + lead_gap + text_w + glyph_gap + glyph_w;

    float x = content.cx() - total * 0.5f;
    float glyph_x = x + total - glyph_w;
    if (style.justify != ButtonJustify::center)
    {
        x = content.x + m.padding;
        glyph_x = style.justify == ButtonJustify::between
                      ? content.x + content.w - m.padding - glyph_w
                      : x + total - glyph_w;
    }
    const float cy = content.cy();
    const Rect lead_box{x + (lead - icon_size) * 0.5f, cy - icon_size * 0.5f, icon_size, icon_size};

    list.push_opacity(disabled_ ? 0.4f : 1.0f);
    const float words = alone ? 1.0f - busy : 1.0f;
    if (words > 0.01f)
    {
        list.push_opacity(words);
        if (icon && (alone || busy < 0.99f))
        {
            list.push_opacity(alone ? 1.0f : 1.0f - busy);
            icon(canvas, lead_box, ink, focus);
            list.pop_opacity();
        }
        if (!label.empty())
        {
            const float text_x = x + lead + lead_gap;
            const float baseline = baseline_for(cy, m.text_size);
            paint.bounded_label(text, text_x, baseline, m.text_size, ink, room);
            if (style.role == ButtonRole::ghost)
                list.rounded_rect({text_x, baseline + m.text_size * 0.42f, text_w, 2.0f}, 0.0f,
                                  ink.with_alpha(0.45f));
        }
        if (glyph_w > 0.0f)
            draw_button(list, canvas.fonts, glyph_style_on(face, style.glyph_tinted), glyph,
                        glyph_x, cy, glyph_size);
        list.pop_opacity();
    }
    if (busy > 0.01f)
    {
        // The member keeps the phase; the copy takes today's look and place.
        Spinner wheel = spinner_;
        wheel.style.theme = theme;
        wheel.style.reduced_motion = style.reduced_motion;
        wheel.style.kind = style.spinner;
        wheel.style.color = ink;
        wheel.style.size = icon_size;
        wheel.set_bounds(
            alone ? Rect{content.cx() - icon_size * 0.5f, lead_box.y, icon_size, icon_size}
                  : lead_box);
        wheel.draw(canvas);
    }
    list.pop_opacity();
    list.pop_transform();
}

// ---- IconButton ------------------------------------------------------------

void IconButton::set_bounds(const Rect &bounds)
{
    bounds_ = bounds;
    sync();
}

Rect IconButton::rect() const
{
    const float size = style.size > 0.0f ? style.size : std::min(bounds_.w, bounds_.h);
    return {bounds_.cx() - size * 0.5f, bounds_.cy() - size * 0.5f, size, size};
}

void IconButton::set_active(bool active)
{
    active_ = active;
    if (!active)
    {
        armed_ = false;
        held_ = false;
    }
}

void IconButton::set_badge(int count)
{
    sync();
    badge_.set_count(count);
}

void IconButton::set_on(bool on, bool snap)
{
    on_ = on;
    on_amount_.target = on ? 1.0f : 0.0f;
    if (snap)
        on_amount_.snap(on_amount_.target);
}

// The badge and the tooltip are components of their own: they take this
// button's theme and place whenever either may have changed.
void IconButton::sync()
{
    const Rect r = rect();
    BadgeStyle &badge = badge_.style;
    badge.theme = style.theme;
    badge.reduced_motion = style.reduced_motion;
    badge.kind = style.badge_kind;
    badge.height = style.badge_height;
    badge.text_size = style.badge_text;
    badge.max_count = style.badge_max;
    badge.cutout = style.badge_cutout;
    badge.padding = 7.0f;
    // On a circle the badge sits on the rim at half past one; on a box, on
    // its corner.
    const bool circle = icon_radius(style.theme, style.shape, r.w) >= r.w * 0.5f - 0.5f;
    const float cx = circle ? r.cx() + r.w * 0.354f : r.x + r.w - 2.0f;
    const float cy = circle ? r.cy() - r.w * 0.354f : r.y + 2.0f;
    badge_.set_bounds({cx - 40.0f, cy - style.badge_height * 0.5f, 80.0f, style.badge_height});

    TooltipStyle &bubble = tooltip_.style;
    bubble.theme = style.theme;
    bubble.reduced_motion = style.reduced_motion;
    bubble.placement = style.tip_placement;
    bubble.delay = style.tip_delay;
    bubble.text_size = style.tip_size;
    bubble.inverted = style.tip_inverted;
}

Event IconButton::handle(const InputFrame &input, Feedback &feedback)
{
    const float x = bounds_.cx();
    if (!input.is_held(Action::confirm) || input.focus_lost)
        armed_ = false;
    Event event = Event::none;
    if (input.is_pressed(Action::confirm))
    {
        if (disabled_)
            return refuse(feedback, style, input, refusal_, x);
        armed_ = true;
        press_.trigger();
        if (style.sounds.rumble > 0.0f && style.rumble > 0.0f)
            feedback.rumble(style.rumble * style.sounds.rumble, 0.04f);
        if (style.toggle)
        {
            set_on(!on_);
            if (!style.reduced_motion)
                flip_.trigger();
            // On sounds a little higher than off.
            play_cue(feedback, style, style.sounds.change, x, on_ ? 1.06f : 0.94f);
            event = Event::changed;
        }
        else
        {
            play_cue(feedback, style, style.sounds.activate, x);
            event = Event::activated;
        }
    }
    held_ = armed_ && !disabled_;
    return event;
}

void IconButton::update(float dt)
{
    sync();
    focus_.target = active_ ? 1.0f : 0.0f;
    focus_.update(dt, std::max(style.omega(), 18.0f));
    down_.target = held_ ? 1.0f : 0.0f;
    held_ = false;
    down_.update(dt, std::max(style.omega(), 26.0f));
    on_amount_.update(dt, std::clamp(style.omega(), 14.0f, 60.0f), style.damping());
    press_.update(dt, 9.0f);
    flip_.update(dt, 7.0f);
    refusal_.update(dt, 9.0f);
    badge_.update(dt);
    if (active_ && !tip.empty())
        tooltip_.show(rect(), tip);
    else
        tooltip_.hide();
    tooltip_.update(dt);
}

void IconButton::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Rect r = rect();
    r.x += shake(refusal_.value, canvas.time, 8.0f);
    const float focus = tween::clamp01(focus_.value);
    const float on = style.toggle ? tween::clamp01(on_amount_.value) : 0.0f;
    float press = tween::clamp01(std::max(press_.value, down_.value));
    if (style.on_pressed && physical(theme.style))
        press = std::max(press, on);
    const float scale = style.reduced_motion
                            ? 1.0f
                            : 1.0f - style.press_scale * std::max(press_.value, down_.value);

    list.push_transform(scale, r.cx(), r.cy(), 0.0f, 0.0f);
    Look look;
    look.focus = focus;
    look.press = press;
    look.disabled = disabled_;
    const ButtonRole role = on > 0.5f ? style.on_role : style.role;
    const ButtonFace face = draw_button_face(canvas, theme, r, role, look,
                                             icon_radius(theme, style.shape, r.w), style.on_page);
    const float size = r.w * style.icon_scale * (1.0f + 0.22f * flip_.value);
    const Rect box{face.content.cx() - size * 0.5f, face.content.cy() - size * 0.5f, size, size};
    list.push_opacity(disabled_ ? 0.4f : 1.0f);
    if (icon)
        icon(canvas, box, face.ink, focus, on);
    else
        list.circle(box.cx(), box.cy(), size * 0.22f, face.ink);
    list.pop_opacity();
    list.pop_transform();
    badge_.draw(canvas);
}

void IconButton::draw_tooltip(Canvas &canvas) const
{
    tooltip_.draw(canvas);
}

} // namespace hui::ui
