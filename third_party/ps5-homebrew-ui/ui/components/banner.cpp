// ps5-homebrew-ui - Component: Banner.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/banner.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

constexpr float kTitleLine = 1.25f; // the title's line height, in title sizes
constexpr float kBarInset = 10.0f;  // the accent bar floats this far inside the panel

Rect lerp(const Rect &a, const Rect &b, float t)
{
    return {tween::lerp(a.x, b.x, t), tween::lerp(a.y, b.y, t), tween::lerp(a.w, b.w, t),
            tween::lerp(a.h, b.h, t)};
}

bool framed(const Theme &theme)
{
    return theme.style == SurfaceStyle::hard || theme.style == SurfaceStyle::bevel ||
           theme.style == SurfaceStyle::pixel || theme.style == SurfaceStyle::sketch;
}

} // namespace

void Banner::set_actions(std::vector<std::string> labels)
{
    if (labels.size() > 2)
        labels.resize(2);
    actions_ = std::move(labels);
    focus_ = std::clamp(focus_, 0, std::max(controls() - 1, 0));
}

void Banner::show(Feedback &feedback)
{
    if (shown_)
        return;
    shown_ = true;
    focus_ = 0;
    glide_.snap(0.0f);
    play_cue(feedback, style, style.sounds.notify, bounds_.cx());
}

void Banner::hide(Feedback &feedback)
{
    if (!shown_)
        return;
    shown_ = false;
    play_cue(feedback, style, style.sounds.close, bounds_.cx());
}

void Banner::set_shown(bool shown, bool snap)
{
    shown_ = shown;
    if (snap)
        amount_.snap(shown ? 1.0f : 0.0f);
}

int Banner::controls() const
{
    return static_cast<int>(actions_.size()) + (style.dismissable ? 1 : 0);
}

void Banner::set_focus(int index)
{
    focus_ = std::clamp(index, 0, std::max(controls() - 1, 0));
}

Banner::Layout Banner::layout(const Fonts &fonts) const
{
    gfx::DrawList scratch;
    const Painter paint(scratch, fonts, style.theme, 0);
    Layout out;
    out.actions = static_cast<int>(actions_.size());
    out.count = controls();

    float left = style.padding;
    if (style.look == BannerLook::accent)
        left += style.bar_width + kBarInset;
    const bool icon = style.icon_size > 0.0f && style.kind != StatusKind::none;
    if (icon)
    {
        out.icon_cx = left + style.icon_size * 0.5f;
        left += style.icon_size + style.gap;
    }

    // The controls are laid from the far edge inward; the text takes the rest.
    float right = bounds_.w - style.padding;
    if (style.dismissable)
    {
        // The cross has no body: half of its box may sit in the padding.
        right += std::min(style.padding, style.close_size) * 0.4f;
        out.controls[out.count - 1] = {right - style.close_size, 0.0f, style.close_size,
                                       style.close_size};
        right -= style.close_size + style.button_gap * 0.5f;
    }
    for (int i = out.actions - 1; i >= 0; --i)
    {
        const std::string &label = actions_[static_cast<std::size_t>(i)];
        const float w = style.button_width > 0.0f ? style.button_width
                                                  : paint.label_width(label, style.button_text) +
                                                        2.0f * style.button_padding;
        out.controls[i] = {right - w, 0.0f, w, style.button_height};
        right -= w + style.button_gap;
    }
    if (out.count > 0)
        right -= style.gap - style.button_gap;

    out.text_x = left;
    const float text_w = std::max(right - left, 60.0f);
    if (!title.empty())
        out.title = fit_label(paint, title, style.title_size, text_w);
    if (!body.empty())
        out.body = wrap_body(paint, body, style.body_size, text_w, std::max(style.body_lines, 1));
    const float text_h = (out.title.empty() ? 0.0f : style.title_size * kTitleLine) +
                         static_cast<float>(out.body.size()) * style.body_size * style.body_line;
    float inner = std::max(text_h, icon ? style.icon_size : 0.0f);
    if (out.actions > 0)
        inner = std::max(inner, style.button_height);
    const float h = std::max(style.min_height, inner + 2.0f * style.padding);
    out.panel = {bounds_.x, bounds_.y, bounds_.w, h};
    out.text_top = (h - text_h) * 0.5f;
    for (int i = 0; i < out.count; ++i)
        out.controls[i].y = (h - out.controls[i].h) * 0.5f;
    return out;
}

float Banner::height(const Fonts &fonts) const
{
    return layout(fonts).panel.h;
}

float Banner::pushed(const Fonts &fonts) const
{
    // A hard shadow is part of the body: the content under it clears that too.
    const float shadow = style.theme.style == SurfaceStyle::hard ? style.theme.shadow_offset : 0.0f;
    return (height(fonts) + shadow) * tween::clamp01(amount_.value);
}

Rect Banner::rect(const Fonts &fonts) const
{
    return layout(fonts).panel;
}

Rect Banner::control_rect(const Fonts &fonts, int index) const
{
    const Layout at = layout(fonts);
    if (index < 0 || index >= at.count)
        return at.panel;
    const Rect &local = at.controls[index];
    return {at.panel.x + local.x, at.panel.y + local.y, local.w, local.h};
}

Event Banner::handle(const InputFrame &input, Feedback &feedback)
{
    exit_ = Direction::none;
    if (!shown_)
        return Event::none;
    const int count = controls();
    // The controls sit at the far end; near enough to pan the cues by.
    const float x = bounds_.x + bounds_.w * 0.8f;
    if (input.nav == Direction::left || input.nav == Direction::right)
    {
        const int next = focus_ + (input.nav == Direction::right ? 1 : -1);
        if (count == 0 || next < 0 || next >= count)
        {
            if (style.exits.allows(input.nav))
            {
                exit_ = input.nav;
                return Event::none;
            }
            return refuse(feedback, style, input, refusal_, x);
        }
        focus_ = next;
        play_cue(feedback, style, style.sounds.move, x);
        return Event::moved;
    }
    if (input.nav != Direction::none)
    {
        if (style.exits.allows(input.nav))
            exit_ = input.nav;
        return Event::none;
    }
    if (input.is_pressed(Action::confirm) && count > 0)
    {
        if (focus_ < static_cast<int>(actions_.size()))
        {
            choice_ = focus_;
            press_.trigger();
            play_cue(feedback, style, style.sounds.activate, x);
            if (style.sounds.rumble > 0.0f)
                feedback.rumble(0.3f * style.sounds.rumble, 0.04f);
            return Event::activated;
        }
        hide(feedback);
        return Event::cancelled;
    }
    if (input.is_pressed(Action::back) && style.dismissable && style.back_dismisses)
    {
        hide(feedback);
        return Event::cancelled;
    }
    return Event::none;
}

void Banner::update(float dt)
{
    const float omega = std::max(style.omega(), 12.0f);
    amount_.target = shown_ ? 1.0f : 0.0f;
    // Leaving is quicker than arriving.
    amount_.update(dt, shown_ ? omega : omega * 1.4f);
    active_amount_.target = active_ && shown_ ? 1.0f : 0.0f;
    active_amount_.update(dt, std::max(omega, 18.0f));
    focus_ = std::clamp(focus_, 0, std::max(controls() - 1, 0));
    glide_.target = static_cast<float>(focus_);
    glide_.update(dt, std::max(omega, 18.0f));
    press_.update(dt, 9.0f);
    refusal_.update(dt, 9.0f);
}

void Banner::draw(Canvas &canvas) const
{
    const float amount = tween::clamp01(amount_.value);
    if (amount <= 0.004f)
        return;
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    const Layout at = layout(canvas.fonts);
    const Rect &panel = at.panel;
    const bool moving = amount < 0.999f;
    const float dy = style.reduced_motion ? 0.0f : -(1.0f - amount) * panel.h * style.slide;
    // It comes out from under the top of its area, not from over what is
    // above it. The clip costs a draw call, so only while it moves.
    if (moving)
        list.push_clip({panel.x - 60.0f, bounds_.y - 28.0f, panel.w + 120.0f, panel.h + 120.0f});
    list.push_opacity(amount);
    list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, dy);

    Painter paint(list, canvas.fonts, theme, canvas.glass);
    // Everything inside the panel is drawn without glass: it would show the
    // screen behind the banner instead of the banner.
    Painter flat(list, canvas.fonts, theme, 0);
    Canvas inside{list, canvas.fonts, 0, canvas.time};
    const Color tone = status_color(theme, style.kind);
    const float radius = std::min(style.radius < 0.0f ? theme.radius_card : style.radius,
                                  std::min(panel.w, panel.h) * 0.5f);
    const bool filled = style.look == BannerLook::filled;
    const bool glassy = theme.style == SurfaceStyle::glass;
    const Color base = glassy ? theme.surface : solid_surface(theme);
    const bool round = theme.corner == Corner::round && theme.radius >= 2.0f;

    Rect body = panel;
    Color ink = theme.text;
    Color quiet = theme.text_muted;
    switch (style.look)
    {
    case BannerLook::filled:
        body = flat.surface(panel, radius, tone, framed(theme) ? theme.outline : tone, 1.0f);
        ink = style.kind == StatusKind::success || style.kind == StatusKind::warning ||
                      style.kind == StatusKind::danger
                  ? Painter::on(tone)
                  : Color{theme.on_primary.r, theme.on_primary.g, theme.on_primary.b, 1.0f};
        quiet = ink.with_alpha(0.86f);
        break;
    case BannerLook::tinted:
        body = paint.surface(
            panel, radius, gfx::mix(base, Color{tone.r, tone.g, tone.b, base.a}, style.tint),
            framed(theme) ? theme.outline : gfx::mix(theme.outline, tone, 0.5f), 1.0f);
        break;
    case BannerLook::outlined:
    {
        body = paint.surface(panel, radius, base, theme.outline, 1.0f);
        // A bevel has edges of its own: the stroke goes inside them.
        const float in = theme.style == SurfaceStyle::bevel ? theme.border : 0.0f;
        flat.stroke(body.inset(in), radius, std::max(theme.border, 2.5f), tone);
        break;
    }
    case BannerLook::accent:
        body = paint.surface(panel, radius, base, theme.outline, 1.0f);
        flat.fill(
            {body.x + kBarInset, body.y + kBarInset, style.bar_width, body.h - 2.0f * kBarInset},
            round ? style.bar_width * 0.5f : 0.0f, tone);
        break;
    }

    if (style.icon_size > 0.0f && style.kind != StatusKind::none)
    {
        if (filled)
        {
            // On its own colour the icon would vanish: draw it in the ink.
            Theme inverted = theme;
            const Color solid{ink.r, ink.g, ink.b, 1.0f};
            inverted.primary = solid;
            inverted.success = solid;
            inverted.warning = solid;
            inverted.danger = solid;
            draw_status_icon(inside, inverted, style.kind, body.x + at.icon_cx, body.cy(),
                             style.icon_size);
        }
        else
        {
            draw_status_icon(inside, theme, style.kind, body.x + at.icon_cx, body.cy(),
                             style.icon_size);
        }
    }

    float y = body.y + at.text_top;
    if (!at.title.empty())
    {
        flat.label(at.title, body.x + at.text_x, y + style.title_size * 0.9f, style.title_size,
                   ink);
        y += style.title_size * kTitleLine;
    }
    for (const std::string &line : at.body)
    {
        flat.body(line, body.x + at.text_x, y + style.body_size * 0.95f, style.body_size, quiet);
        y += style.body_size * style.body_line;
    }

    // ---- controls ----
    const float active = tween::clamp01(active_amount_.value);
    const float glide =
        std::clamp(glide_.value, 0.0f, static_cast<float>(std::max(at.count - 1, 0)));
    const auto placed = [&](int i) -> Rect
    {
        const Rect &local = at.controls[i];
        return {body.x + local.x, body.y + local.y, local.w, local.h};
    };
    const auto focus_of = [&](int i)
    { return tween::clamp01(1.0f - std::fabs(glide - static_cast<float>(i))) * active; };
    const float pen = std::clamp(theme.border, 2.0f, 3.0f);

    for (int i = 0; i < at.actions; ++i)
    {
        const Rect r = placed(i);
        const float focus = focus_of(i);
        const float press = i == choice_ ? tween::clamp01(press_.value) : 0.0f;
        const std::string label =
            fit_label(flat, actions_[static_cast<std::size_t>(i)], style.button_text, r.w - 16.0f);
        const float scale = style.reduced_motion ? 1.0f : 1.0f - 0.04f * press;
        list.push_transform(scale, r.cx(), r.cy(), 0.0f, 0.0f);
        if (filled)
        {
            // On the status colour a themed button could vanish; these are
            // cut from the ink: outlined at rest, solid under the focus.
            const float corner = flat.control_radius(r);
            flat.fill(r, corner, ink.with_alpha(0.12f + 0.88f * focus));
            flat.stroke(r, corner, pen, ink.with_alpha(0.75f));
            flat.label(label, r.cx(), r.cy() + style.button_text * 0.35f, style.button_text,
                       gfx::mix(ink, tone, focus), gfx::Align::center);
        }
        else
        {
            Look look;
            look.press = press;
            const ButtonRole role =
                i == 0 && style.emphasize_first ? ButtonRole::primary : style.action_role;
            const ButtonFace face = draw_button_face(inside, theme, r, role, look, -1.0f, false);
            flat.label(label, face.content.cx(), face.content.cy() + style.button_text * 0.35f,
                       style.button_text, face.ink, gfx::Align::center);
        }
        list.pop_transform();
    }
    if (style.dismissable && at.count > 0)
    {
        const Rect r = placed(at.count - 1);
        const float focus = focus_of(at.count - 1);
        const float corner = flat.control_radius(r);
        Color cross = quiet;
        if (filled)
        {
            flat.fill(r, corner, ink.with_alpha(focus));
            cross = gfx::mix(ink, tone, focus);
        }
        else
        {
            flat.fill(r, corner, ink.with_alpha(0.08f * focus));
            cross = gfx::mix(quiet, ink, focus);
        }
        const float reach = r.w * 0.17f;
        list.line(r.cx() - reach, r.cy() - reach, r.cx() + reach, r.cy() + reach, 3.0f, cross);
        list.line(r.cx() + reach, r.cy() - reach, r.cx() - reach, r.cy() + reach, 3.0f, cross);
    }

    // One ring that glides between the controls. A filled banner needs none:
    // its focused control is the solid one.
    if (!filled && active > 0.01f && at.count > 0)
    {
        const int from = static_cast<int>(glide);
        const int to = std::min(from + 1, at.count - 1);
        Rect ring = lerp(placed(from), placed(to), glide - static_cast<float>(from));
        ring.x += shake(refusal_.value, canvas.time, 8.0f);
        // The ring of the control it is nearest to: the cross has no body.
        const int nearest = static_cast<int>(glide + 0.5f);
        ButtonRole role = style.action_role;
        if (nearest >= at.actions)
            role = ButtonRole::ghost;
        else if (nearest == 0 && style.emphasize_first)
            role = ButtonRole::primary;
        draw_button_ring(inside, theme, ring, role, active, -1.0f, false);
    }

    list.pop_transform();
    list.pop_opacity();
    if (moving)
        list.pop_clip();
}

} // namespace hui::ui
