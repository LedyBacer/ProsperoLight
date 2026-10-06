// ps5-homebrew-ui - Component: InputPrompt.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/input_prompt.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

constexpr float kSideMargin = 48.0f;
constexpr float kTitleLine = 1.25f; // line height of the title, in title sizes
constexpr int kCancel = 0;
constexpr int kDone = 1;

} // namespace

// The prompt owns these knobs of its two parts; everything else on them is
// the user's.
void InputPrompt::sync()
{
    field.style.theme = style.theme;
    // The focus is on the keys: the field shows a caret, not a second ring.
    // (TextField has no knob for that; a clear focus colour does it.)
    field.style.theme.focus.a = 0.0f;
    field.style.sounds = style.sounds;
    field.style.reduced_motion = style.reduced_motion;
    field.style.max_length = style.max_length;
    field.style.password = style.password;
    field.style.on_page = false;

    keyboard.style.theme = style.theme;
    keyboard.style.sounds = style.sounds;
    // Done and back are the prompt's to answer: it validates first, then
    // plays one sound instead of two.
    keyboard.style.sounds.activate = audio::Cue::count;
    keyboard.style.sounds.cancel = audio::Cue::count;
    keyboard.style.reduced_motion = style.reduced_motion;
    keyboard.style.panel = false;
    keyboard.style.key_height = style.key_height;
    keyboard.style.gap = style.key_gap;
    keyboard.style.max_length = style.max_length;
    keyboard.style.auto_capital = style.auto_capital;
    keyboard.style.exits = {};
    keyboard.style.exits.down = style.buttons;
}

Rect InputPrompt::panel_rect() const
{
    const float width = std::max(std::min(style.width, bounds_.w - 2.0f * kSideMargin), 200.0f);
    float height =
        2.0f * style.padding + field.preferred_height() + style.gap + keyboard.preferred_height();
    if (!title_.empty())
        height += style.title_size * kTitleLine + style.gap * 0.6f;
    if (style.buttons)
        height += style.gap + style.button_height;
    return {bounds_.cx() - width * 0.5f, bounds_.cy() - height * 0.5f, width, height};
}

void InputPrompt::place()
{
    const Rect panel = panel_rect();
    const float inner = panel.w - 2.0f * style.padding;
    float y = panel.y + style.padding;
    if (!title_.empty())
        y += style.title_size * kTitleLine + style.gap * 0.6f;
    const float field_height = field.preferred_height();
    field.set_bounds({panel.x + style.padding, y, inner, field_height});
    y += field_height + style.gap;
    keyboard.set_bounds({panel.x + style.padding, y, inner, keyboard.preferred_height()});
}

Rect InputPrompt::button_rect(int index) const
{
    const Rect panel = panel_rect();
    const float inner = panel.w - 2.0f * style.padding;
    const float width = std::min(style.button_width, (inner - style.button_gap) * 0.5f);
    const float total = 2.0f * width + style.button_gap;
    // The row ends on the right, where the eye finishes reading.
    return {panel.x + panel.w - style.padding - total +
                static_cast<float>(index) * (width + style.button_gap),
            panel.y + panel.h - style.padding - style.button_height, width, style.button_height};
}

void InputPrompt::open(Feedback &feedback, std::string_view initial)
{
    sync();
    if (!visible())
    {
        fade_.snap(0.0f);
        pop_.snap(0.0f);
    }
    open_ = true;
    on_buttons_ = false;
    button_ = kDone;
    pressed_ = -1;
    press_.value = 0.0f;
    field.set_text(initial);
    field.set_error("");
    keyboard.set_layout(0);
    keyboard.set_shift(KeyboardShift::off);
    keyboard.set_length(field.length());
    place();
    // The middle of the board is the shortest way to anywhere.
    const int rows =
        keyboard.layouts().empty()
            ? 0
            : static_cast<int>(
                  keyboard.layouts()[static_cast<std::size_t>(keyboard.layout())].rows.size());
    keyboard.set_focus(rows / 2, 4, true);
    keyboard.enter();
    highlight_.snap(button_rect(button_));
    play_cue(feedback, style, style.sounds.open, bounds_.cx());
}

void InputPrompt::close(Feedback &feedback)
{
    if (!open_)
        return;
    open_ = false;
    play_cue(feedback, style, style.sounds.close, bounds_.cx());
}

void InputPrompt::dismiss()
{
    open_ = false;
    fade_.snap(0.0f);
    pop_.snap(0.0f);
}

bool InputPrompt::visible() const
{
    return open_ || fade_.value > 0.004f;
}

Event InputPrompt::submit(Feedback &feedback)
{
    if (style.trim)
    {
        while (!field.text().empty() && field.text().back() == ' ')
            field.backspace();
    }
    if (field.text().empty() && !style.allow_empty)
    {
        // The field shakes and says why; the prompt stays open.
        field.set_error(style.empty_error);
        return refuse(feedback, style, InputFrame{}, highlight_.refusal(), bounds_.cx());
    }
    pressed_ = kDone;
    press_.trigger();
    // The answer's own cue is the goodbye: no closing sound on top of it.
    play_cue(feedback, style, style.sounds.activate, bounds_.cx());
    if (style.sounds.rumble > 0.0f)
        feedback.rumble(0.3f * style.sounds.rumble, 0.04f);
    open_ = false;
    return Event::activated;
}

Event InputPrompt::cancel(Feedback &feedback)
{
    pressed_ = kCancel;
    press_.trigger();
    close(feedback);
    return Event::cancelled;
}

Event InputPrompt::handle(const InputFrame &input, Feedback &feedback)
{
    if (!open_)
        return Event::none;
    if (input.is_pressed(Action::back))
        return cancel(feedback);

    if (!on_buttons_)
    {
        keyboard.set_length(field.length());
        const Event event = keyboard.handle(input, feedback);
        bool edited = false;
        for (int i = 0; i < keyboard.erased(); ++i)
            edited = field.backspace() || edited;
        if (!keyboard.typed().empty())
            edited = field.insert(keyboard.typed()) || edited;
        if (edited)
        {
            // Typing answers the complaint about an empty text.
            field.set_error("");
            keyboard.set_length(field.length());
            return Event::changed;
        }
        if (event == Event::activated)
            return submit(feedback);
        if (event == Event::none && keyboard.exit() == Direction::down && style.buttons)
        {
            on_buttons_ = true;
            button_ = kDone;
            highlight_.snap(button_rect(button_));
            play_cue(feedback, style, style.sounds.move, button_rect(button_).cx(), 0.94f);
            return Event::moved;
        }
        return event;
    }

    const float x = button_rect(button_).cx();
    if (input.nav == Direction::left || input.nav == Direction::right)
    {
        const int next = button_ + (input.nav == Direction::right ? 1 : -1);
        if (next < kCancel || next > kDone)
            return refuse(feedback, style, input, highlight_.refusal(), x);
        button_ = next;
        highlight_.target(button_rect(button_));
        play_cue(feedback, style, style.sounds.move, button_rect(button_).cx());
        return Event::moved;
    }
    if (input.nav == Direction::up)
    {
        on_buttons_ = false;
        play_cue(feedback, style, style.sounds.move, x, 1.04f);
        return Event::moved;
    }
    if (input.nav == Direction::down)
        return refuse(feedback, style, input, highlight_.refusal(), x);
    if (input.is_pressed(Action::confirm))
        return button_ == kDone ? submit(feedback) : cancel(feedback);
    return Event::none;
}

void InputPrompt::update(float dt)
{
    sync();
    place();
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
    field.set_active(open_);
    keyboard.set_active(open_ && !on_buttons_);
    field.update(dt);
    keyboard.update(dt);
    buttons_focus_.target = open_ && on_buttons_ ? 1.0f : 0.0f;
    buttons_focus_.update(dt, 18.0f);
    highlight_.target(button_rect(button_));
    highlight_.update(dt, style);
    press_.update(dt, 7.0f);
}

void InputPrompt::draw(Canvas &canvas) const
{
    if (!visible())
        return;
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    const float fade = tween::clamp01(fade_.value);
    list.rounded_rect(bounds_, 0.0f, style.scrim_color.with_alpha(style.scrim * fade));

    const Rect panel = panel_rect();
    const bool still = style.reduced_motion;
    const float pop = pop_.value;
    list.push_opacity(tween::clamp01(fade * 1.5f));
    list.push_transform(still ? 1.0f : tween::lerp(style.enter_scale, 1.0f, pop), panel.cx(),
                        panel.cy(), 0.0f, still ? 0.0f : style.enter_rise * (1.0f - pop));

    draw_overlay_panel(canvas, theme, panel, style.frosted, style.frost);
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const float inner = panel.w - 2.0f * style.padding;
    if (!title_.empty())
    {
        const std::vector<std::string> title =
            wrap_heading(canvas, theme, title_, style.title_size, inner, 1);
        if (!title.empty())
            paint.heading(title[0], panel.x + style.padding,
                          panel.y + style.padding + style.title_size * 0.88f, style.title_size,
                          theme.text);
    }
    field.draw(canvas);
    keyboard.draw(canvas);

    if (style.buttons)
    {
        for (int i = kCancel; i <= kDone; ++i)
        {
            const Rect r = button_rect(i);
            Look look;
            look.press = i == pressed_ ? tween::clamp01(press_.value) : 0.0f;
            const std::string &label = i == kDone ? style.done_label : style.cancel_label;
            paint.button(r, label,
                         i == kDone ? ButtonKind::primary : ButtonKind::secondary, look);
        }
        // One ring that glides between the two answers.
        HighlightStyle ring;
        ring.kind = HighlightKind::ring;
        highlight_.draw(canvas, style, ring, tween::clamp01(buttons_focus_.value));
    }

    list.pop_transform();
    list.pop_opacity();
}

} // namespace hui::ui
