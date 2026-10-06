// ps5-homebrew-ui - Component: Dialog.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/dialog.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

constexpr int kMaxButtons = 3;
constexpr float kTitleLine = 1.22f; // line height of the title, in title sizes
constexpr float kSideMargin = 48.0f;

} // namespace

void Dialog::set_content(DialogContent content)
{
    content_ = std::move(content);
    if (content_.buttons.empty())
        content_.buttons.push_back({"OK", ButtonKind::primary, false});
    if (content_.buttons.size() > static_cast<std::size_t>(kMaxButtons))
        content_.buttons.resize(static_cast<std::size_t>(kMaxButtons));
    focus_ = std::clamp(focus_, 0, count() - 1);
    retarget(true);
}

int Dialog::count() const
{
    return std::max(1, static_cast<int>(content_.buttons.size()));
}

bool Dialog::has_icon() const
{
    return content_.icon != StatusKind::none && style.icon_size > 0.0f;
}

float Dialog::panel_width() const
{
    return std::max(std::min(style.width, bounds_.w - 2.0f * kSideMargin), 120.0f);
}

float Dialog::inner_width() const
{
    return std::max(panel_width() - 2.0f * style.padding, 40.0f);
}

Rect Dialog::button_rect(int index) const
{
    const float inner = inner_width();
    const float n = static_cast<float>(count());
    const float at = static_cast<float>(index);
    if (style.buttons == DialogButtons::stacked)
    {
        const float width = style.button_width > 0.0f ? std::min(style.button_width, inner) : inner;
        const float x = style.centered ? (inner - width) * 0.5f : inner - width;
        return {x, at * (style.button_height + style.button_gap), width, style.button_height};
    }
    const float share = (inner - (n - 1.0f) * style.button_gap) / n;
    const float width = style.button_width > 0.0f ? std::min(style.button_width, share) : share;
    const float total = n * width + (n - 1.0f) * style.button_gap;
    // A centred dialog centres its answers; a leading one ends the row on the
    // right, where the eye finishes reading.
    const float x = style.centered ? (inner - total) * 0.5f : inner - total;
    return {x + at * (width + style.button_gap), 0.0f, width, style.button_height};
}

float Dialog::buttons_height() const
{
    if (style.buttons == DialogButtons::row)
        return style.button_height;
    const float n = static_cast<float>(count());
    return n * style.button_height + (n - 1.0f) * style.button_gap;
}

// The default button, unless it destroys something and another does not.
int Dialog::first_focus() const
{
    const int n = static_cast<int>(content_.buttons.size());
    if (n == 0)
        return 0;
    const int wanted = std::clamp(content_.default_button, 0, n - 1);
    if (!content_.buttons[static_cast<std::size_t>(wanted)].destructive)
        return wanted;
    for (int i = 0; i < n; ++i)
    {
        if (!content_.buttons[static_cast<std::size_t>(i)].destructive)
            return i;
    }
    return wanted;
}

void Dialog::retarget(bool snap)
{
    const Rect target = button_rect(focus_);
    highlight_.target(target);
    if (snap)
        highlight_.snap(target);
}

void Dialog::set_focus(int index)
{
    focus_ = std::clamp(index, 0, count() - 1);
    retarget(true);
}

void Dialog::open(DialogContent content, Feedback &feedback)
{
    set_content(std::move(content));
    open(feedback);
}

void Dialog::open(Feedback &feedback)
{
    if (content_.buttons.empty())
        set_content(std::move(content_));
    if (!visible())
    {
        fade_.snap(0.0f);
        pop_.snap(0.0f);
    }
    open_ = true;
    choice_ = -1;
    press_.value = 0.0f;
    focus_ = first_focus();
    retarget(true);
    play_cue(feedback, style, style.sounds.open, bounds_.cx());
}

void Dialog::close(Feedback &feedback)
{
    if (!open_)
        return;
    open_ = false;
    play_cue(feedback, style, style.sounds.close, bounds_.cx());
}

void Dialog::dismiss()
{
    open_ = false;
    fade_.snap(0.0f);
    pop_.snap(0.0f);
}

bool Dialog::visible() const
{
    return open_ || fade_.value > 0.004f;
}

Event Dialog::handle(const InputFrame &input, Feedback &feedback)
{
    if (!open_)
        return Event::none;
    const bool row = style.buttons == DialogButtons::row;
    const Direction before = row ? Direction::left : Direction::up;
    const Direction after = row ? Direction::right : Direction::down;
    const float left = bounds_.cx() - panel_width() * 0.5f + style.padding;
    const float x = left + button_rect(focus_).cx();

    if (input.nav == before || input.nav == after)
    {
        const int next = focus_ + (input.nav == after ? 1 : -1);
        if (next < 0 || next >= count())
            return refuse(feedback, style, input, highlight_.refusal(), x);
        focus_ = next;
        retarget(false);
        play_cue(feedback, style, style.sounds.move, left + button_rect(focus_).cx());
        return Event::moved;
    }
    if (input.is_pressed(Action::confirm))
    {
        choice_ = focus_;
        press_.trigger();
        play_cue(feedback, style, style.sounds.activate, x);
        if (style.sounds.rumble > 0.0f)
            feedback.rumble(0.3f * style.sounds.rumble, 0.04f);
        // The answer's own cue is the goodbye: no second sound on top of it.
        if (style.close_on_activate)
            open_ = false;
        return Event::activated;
    }
    if (input.is_pressed(Action::back))
    {
        if (!style.dismissable)
            return refuse(feedback, style, input, highlight_.refusal(), x);
        choice_ = -1;
        close(feedback);
        return Event::cancelled;
    }
    return Event::none;
}

void Dialog::update(float dt)
{
    const float omega = style.omega();
    if (open_)
    {
        fade_.target = 1.0f;
        fade_.update(dt, omega * 1.2f);
        pop_.target = 1.0f;
        pop_.update(dt, omega, style.damping());
    }
    else
    {
        // Leaving is quicker and never bounces: the player has moved on.
        const float out = omega * std::max(style.exit_speed, 0.1f);
        fade_.target = 0.0f;
        fade_.update(dt, out);
        pop_.target = 0.0f;
        pop_.update(dt, out, 1.0f);
    }
    // The knobs may have changed since the last frame: follow them.
    retarget(false);
    highlight_.update(dt, style);
    press_.update(dt, 7.0f);
}

Dialog::Layout Dialog::layout(const Canvas &canvas) const
{
    const Theme &theme = style.theme;
    const Painter paint(canvas.list, canvas.fonts, theme, 0);
    Layout out;
    const float width = panel_width();
    const float inner = inner_width();
    const bool icon = has_icon();
    const bool leading = icon && !style.centered;
    const float indent = leading ? style.icon_size + style.gap * 1.3f : 0.0f;
    const float text_width = std::max(inner - indent, 40.0f);

    if (!content_.title.empty())
        out.title = wrap_heading(canvas, theme, content_.title, style.title_size, text_width,
                                 style.title_lines);
    if (!content_.body.empty())
        out.body = wrap_body(paint, content_.body, style.body_size, text_width, style.body_lines);

    float y = style.padding;
    if (style.centered)
    {
        out.text_x = width * 0.5f;
        if (icon)
        {
            out.icon_cx = width * 0.5f;
            out.icon_cy = y + style.icon_size * 0.5f;
            y += style.icon_size + style.gap;
        }
    }
    else
    {
        out.text_x = style.padding + indent;
        out.icon_cx = style.padding + style.icon_size * 0.5f;
        out.icon_cy = style.padding + style.icon_size * 0.5f;
    }
    out.title_top = y;
    y += static_cast<float>(out.title.size()) * style.title_size * kTitleLine;
    if (!out.title.empty() && !out.body.empty())
        y += style.gap * 0.55f;
    out.body_top = y;
    y += static_cast<float>(out.body.size()) * style.body_size * style.body_line;
    if (leading)
        y = std::max(y, style.padding + style.icon_size);
    y += style.padding * 0.8f;
    out.buttons_top = y;
    y += buttons_height() + style.padding;

    const float top = style.align == DialogAlign::center
                          ? bounds_.cy() - y * 0.5f
                          : bounds_.y + bounds_.h - style.bottom_margin - y;
    out.panel = {bounds_.cx() - width * 0.5f, top, width, y};
    return out;
}

Rect Dialog::panel_rect(const Canvas &canvas) const
{
    return layout(canvas).panel;
}

void Dialog::draw(Canvas &canvas) const
{
    if (!visible())
        return;
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    const float fade = tween::clamp01(fade_.value);
    list.rounded_rect(bounds_, 0.0f, style.scrim_color.with_alpha(style.scrim * fade));

    const Layout at = layout(canvas);
    const Rect &panel = at.panel;
    const bool still = style.reduced_motion;
    const float pop = pop_.value;
    const float scale = still ? 1.0f : tween::lerp(style.enter_scale, 1.0f, pop);
    const float rise = still ? 0.0f : style.enter_rise * (1.0f - pop);
    // A bottom dialog grows out of the edge it sits on.
    const float origin_y = style.align == DialogAlign::center ? panel.cy() : panel.y + panel.h;
    list.push_opacity(tween::clamp01(fade * 1.5f));
    list.push_transform(scale, panel.cx(), origin_y, 0.0f, rise);

    draw_overlay_panel(canvas, theme, panel, style.frosted, style.frost);
    Painter paint(list, canvas.fonts, theme, canvas.glass);

    if (has_icon())
    {
        // The icon lands a touch after the panel, with the spring's own bounce.
        const float grow = still ? 1.0f : tween::lerp(0.55f, 1.0f, pop);
        list.push_transform(grow, panel.x + at.icon_cx, panel.y + at.icon_cy, 0.0f, 0.0f);
        draw_status_icon(canvas, theme, content_.icon, panel.x + at.icon_cx, panel.y + at.icon_cy,
                         style.icon_size);
        list.pop_transform();
    }

    const gfx::Align align = style.centered ? gfx::Align::center : gfx::Align::left;
    const float text_x = panel.x + at.text_x;
    for (std::size_t i = 0; i < at.title.size(); ++i)
    {
        const float top =
            panel.y + at.title_top + static_cast<float>(i) * style.title_size * kTitleLine;
        paint.bounded_heading(at.title[i], text_x, top + style.title_size * 0.88f, style.title_size,
                      theme.text, inner_width()-(has_icon() && !style.centered ? style.icon_size+style.gap*1.3f : 0.0f), align);
    }
    for (std::size_t i = 0; i < at.body.size(); ++i)
    {
        const float top =
            panel.y + at.body_top + static_cast<float>(i) * style.body_size * style.body_line;
        paint.bounded_body(at.body[i], text_x, top + style.body_size * 0.95f, style.body_size,
                   theme.text_muted, inner_width()-(has_icon() && !style.centered ? style.icon_size+style.gap*1.3f : 0.0f), align);
    }

    // A destructive answer is the same button in a theme whose main colour
    // is the danger colour: every surface construction keeps working.
    Theme alarm = theme;
    alarm.primary = theme.danger;
    alarm.on_primary = Painter::on(theme.danger);
    Painter alarm_paint(list, canvas.fonts, alarm, canvas.glass);

    const float block_x = panel.x + style.padding;
    const float block_y = panel.y + at.buttons_top;
    const int n = static_cast<int>(content_.buttons.size());
    for (int i = 0; i < n; ++i)
    {
        const DialogButton &button = content_.buttons[static_cast<std::size_t>(i)];
        const Rect local = button_rect(i);
        const Rect r{block_x + local.x, block_y + local.y, local.w, local.h};
        Look look;
        look.press = i == choice_ ? tween::clamp01(press_.value) : 0.0f;
        const std::string &label = button.label;
        if (button.destructive)
            alarm_paint.button(r, label, ButtonKind::primary, look);
        else
            paint.button(r, label, button.kind, look);
    }

    // One ring that glides between the answers, kept in the block's space.
    list.push_transform(1.0f, 0.0f, 0.0f, block_x, block_y);
    HighlightStyle ring;
    ring.kind = HighlightKind::ring;
    highlight_.draw(canvas, style, ring, 1.0f);
    list.pop_transform();

    list.pop_transform();
    list.pop_opacity();
}

} // namespace hui::ui
