// ps5-homebrew-ui - Component: KeyBinder.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/key_binder.hpp"

#include "ui/components/action_glyph.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

constexpr float kTwoPi = 6.2831853f;
constexpr float kRing = 17.0f; // radius of the countdown ring

Action action_for(Direction direction)
{
    switch (direction)
    {
    case Direction::up:
        return Action::up;
    case Direction::down:
        return Action::down;
    case Direction::left:
        return Action::left;
    case Direction::right:
        return Action::right;
    default:
        return Action::count;
    }
}

// "Put it back": most of a circle and an arrow head on its end.
void draw_reset(gfx::DrawList &list, float cx, float cy, float r, Color ink)
{
    constexpr float kStart = 0.9f;
    constexpr float kSweep = 4.5f;
    const float pen = std::max(2.5f, r * 0.24f);
    list.arc(cx, cy, r, pen, kStart, kSweep, ink, false);
    const float end = kStart + kSweep;
    const float mid = r - pen * 0.5f;
    const float ex = cx + mid * std::sin(end);
    const float ey = cy - mid * std::cos(end);
    const float head = pen * 3.6f;
    // The tangent of a clockwise arc at angle `end` points at `end` + 90 degrees.
    list.triangle({ex - head * 0.5f, ey - head * 0.5f, head, head}, ink, 0.0f, end + 1.5708f);
}

} // namespace

void KeyBinder::set_bindings(std::vector<KeyBinding> bindings)
{
    rows_ = std::move(bindings);
    for (KeyBinding &row : rows_)
    {
        if (row.fallback == Action::count)
            row.fallback = row.action;
    }
    flashes_.assign(rows_.size(), Pulse{});
    listening_ = -1;
    focus_ = std::clamp(focus_, 0, std::max(count() - 1, 0));
    retarget(true);
}

Action KeyBinder::action_of(int id) const
{
    for (const KeyBinding &row : rows_)
    {
        if (row.id == id)
            return row.action;
    }
    return Action::count;
}

bool KeyBinder::set_action(int id, Action action)
{
    for (KeyBinding &row : rows_)
    {
        if (row.id == id)
        {
            row.action = action;
            return true;
        }
    }
    return false;
}

bool KeyBinder::is_default() const
{
    for (const KeyBinding &row : rows_)
    {
        if (row.action != row.fallback)
            return false;
    }
    return true;
}

bool KeyBinder::restore_defaults()
{
    bool changed = false;
    for (std::size_t i = 0; i < rows_.size(); ++i)
    {
        if (rows_[i].action == rows_[i].fallback)
            continue;
        rows_[i].action = rows_[i].fallback;
        flash(static_cast<int>(i));
        changed = true;
    }
    return changed;
}

int KeyBinder::count() const
{
    return static_cast<int>(rows_.size()) + (style.reset_row && !rows_.empty() ? 1 : 0);
}

float KeyBinder::listen_left() const
{
    return listening() ? std::max(style.listen_seconds - listen_time_, 0.0f) : 0.0f;
}

Rect KeyBinder::inner() const
{
    return style.panel ? bounds_.inset(style.panel_padding) : bounds_;
}

float KeyBinder::row_top(int index) const
{
    return static_cast<float>(index) * (style.row_height + style.gap);
}

Rect KeyBinder::row_rect(int index) const
{
    const Rect in = inner();
    return {in.x, in.y + row_top(index) - scroll_.offset(), in.w, style.row_height};
}

float KeyBinder::preferred_height() const
{
    const float rows = static_cast<float>(count());
    return rows * style.row_height + std::max(rows - 1.0f, 0.0f) * style.gap +
           (style.panel ? 2.0f * style.panel_padding : 0.0f);
}

void KeyBinder::set_bounds(const Rect &bounds)
{
    const bool changed = bounds.x != bounds_.x || bounds.y != bounds_.y || bounds.w != bounds_.w ||
                         bounds.h != bounds_.h;
    bounds_ = bounds;
    if (changed)
        retarget(true);
}

void KeyBinder::set_focus(int index, bool snap)
{
    if (count() == 0)
        return;
    focus_ = std::clamp(index, 0, count() - 1);
    retarget(snap);
}

void KeyBinder::retarget(bool snap)
{
    const int n = count();
    if (n == 0)
        return;
    focus_ = std::clamp(focus_, 0, n - 1);
    const Rect in = inner();
    const float top = row_top(focus_);
    scroll_.reveal(top, top + style.row_height, in.h, style.row_height * 0.5f,
                   row_top(n - 1) + style.row_height);
    if (snap)
        scroll_.position.snap(scroll_.position.target);
    // The highlight lives in content space: scrolling must not make it lag.
    const Rect target{0.0f, top, in.w, style.row_height};
    highlight_.target(target);
    if (snap)
        highlight_.snap(target);
}

void KeyBinder::flash(int index)
{
    if (index >= 0 && index < static_cast<int>(flashes_.size()))
        flashes_[static_cast<std::size_t>(index)].trigger();
}

// A row is waiting for a button: every input is its answer.
Event KeyBinder::listen(const InputFrame &input, Feedback &feedback)
{
    const float x = bounds_.cx();
    const bool timed_out = listen_time_ >= style.listen_seconds;
    if (timed_out || (style.cancel_with_back && input.is_pressed(Action::back)))
    {
        listening_ = -1;
        play_cue(feedback, style, style.sounds.cancel, x);
        return Event::cancelled;
    }

    Action captured = Action::count;
    const std::uint32_t bits = input.pressed & style.allowed;
    for (unsigned action = 0; action < static_cast<unsigned>(Action::count); ++action)
    {
        if ((bits & (1u << action)) != 0)
        {
            captured = static_cast<Action>(action);
            break;
        }
    }
    // A D-pad press arrives as an action bit; a scripted input may carry the
    // direction alone. The stick is not the D-pad, and a repeat is not a press.
    if (captured == Action::count && input.nav != Direction::none && !input.nav_repeat &&
        !input.nav_from_stick)
    {
        const Action direction = action_for(input.nav);
        if ((style.allowed & action_bit(direction)) != 0)
            captured = direction;
    }
    if (captured == Action::count)
    {
        // A button the application keeps for itself.
        if (input.pressed != 0)
            return refuse(feedback, style, input, shake_, x);
        return Event::none;
    }

    KeyBinding &row = rows_[static_cast<std::size_t>(listening_)];
    if (row.action != captured)
    {
        int other = -1;
        for (std::size_t i = 0; i < rows_.size(); ++i)
        {
            if (static_cast<int>(i) != listening_ && rows_[i].action == captured)
                other = static_cast<int>(i);
        }
        if (other >= 0 && style.conflict != BindConflict::allow)
        {
            KeyBinding &holder = rows_[static_cast<std::size_t>(other)];
            if (style.conflict == BindConflict::refuse || holder.locked)
            {
                // The row that has the button answers for it.
                flash(other);
                return refuse(feedback, style, input, shake_, x);
            }
            holder.action = row.action;
            flash(other);
        }
        row.action = captured;
    }
    flash(listening_);
    changed_id_ = row.id;
    listening_ = -1;
    play_cue(feedback, style, style.sounds.change, x);
    if (style.sounds.rumble > 0.0f)
        feedback.rumble(0.3f * style.sounds.rumble, 0.04f);
    return Event::changed;
}

Event KeyBinder::handle(const InputFrame &input, Feedback &feedback)
{
    exit_ = Direction::none;
    const int n = count();
    if (n == 0)
        return Event::none;
    focus_ = std::clamp(focus_, 0, n - 1);
    if (listening())
        return listen(input, feedback);

    const float x = bounds_.cx();
    if (input.nav == Direction::up || input.nav == Direction::down)
    {
        int next = focus_ + (input.nav == Direction::down ? 1 : -1);
        if (next < 0 || next >= n)
        {
            if (style.exits.allows(input.nav))
            {
                exit_ = input.nav;
                return Event::none;
            }
            if (!style.wrap || input.nav_repeat || n < 2)
                return refuse(feedback, style, input, highlight_.refusal(), x);
            next = next < 0 ? n - 1 : 0;
        }
        focus_ = next;
        retarget(false);
        const float along = n > 1 ? static_cast<float>(focus_) / static_cast<float>(n - 1) : 0.0f;
        play_cue(feedback, style, style.sounds.move, x, tween::lerp(1.05f, 0.95f, along));
        return Event::moved;
    }
    if (input.nav == Direction::left || input.nav == Direction::right)
    {
        // The rows have no use for left and right: a screen may take them.
        if (style.exits.allows(input.nav))
            exit_ = input.nav;
        return Event::none;
    }
    if (input.is_pressed(Action::confirm))
    {
        const bool reset = focus_ >= static_cast<int>(rows_.size());
        if (reset)
        {
            if (!restore_defaults())
                return refuse(feedback, style, input, highlight_.refusal(), x);
            changed_id_ = kResetId;
            press_.trigger();
            play_cue(feedback, style, style.sounds.change, x, 0.94f);
            return Event::changed;
        }
        if (rows_[static_cast<std::size_t>(focus_)].locked)
            return refuse(feedback, style, input, highlight_.refusal(), x);
        listening_ = focus_;
        listen_time_ = 0.0f;
        press_.trigger();
        play_cue(feedback, style, style.sounds.activate, x);
        return Event::activated;
    }
    if (input.is_pressed(Action::back))
    {
        play_cue(feedback, style, style.sounds.cancel, x);
        return Event::cancelled;
    }
    return Event::none;
}

void KeyBinder::update(float dt)
{
    clock_ += dt;
    if (listening())
    {
        listen_time_ += dt;
        // handle() ends an unanswered wait with its cue. If the screen
        // stopped calling it, the row must not listen for ever.
        if (listen_time_ > style.listen_seconds + 0.5f)
            listening_ = -1;
    }
    for (Pulse &pulse : flashes_)
        pulse.update(dt, 4.5f);
    retarget(false);
    highlight_.update(dt, style);
    scroll_.update(dt, std::max(style.omega(), 14.0f));
    active_amount_.target = active_ ? 1.0f : 0.0f;
    active_amount_.update(dt, 18.0f);
    listen_amount_.target = listening() ? 1.0f : 0.0f;
    listen_amount_.update(dt, 16.0f);
    press_.update(dt, 10.0f);
    shake_.update(dt, 8.0f);
}

void KeyBinder::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    if (style.panel)
        paint.panel(bounds_);
    const int n = count();
    if (n == 0)
        return;

    const Rect in = inner();
    const bool still = style.reduced_motion;
    const float scroll = scroll_.offset();
    const float content = row_top(n - 1) + style.row_height;
    const bool overflow = content > in.h + 0.5f;
    const bool on_page = !style.panel && style.on_page;
    const Color base_ink = on_page ? paint.page_text() : theme.text;
    const Color base_quiet = on_page ? paint.page_text_muted() : theme.text_muted;
    const Color lit{theme.accent.r, theme.accent.g, theme.accent.b, 1.0f};
    const float listen = tween::clamp01(listen_amount_.value);
    const float active = active_amount_.value;
    const int rows = static_cast<int>(rows_.size());

    list.push_clip({in.x - 12.0f, in.y - 10.0f, in.w + 24.0f, in.h + 20.0f});

    if (style.dividers)
    {
        for (int i = 0; i + 1 < n; ++i)
        {
            const Rect row = row_rect(i);
            list.rounded_rect({row.x + style.padding, row.y + row.h + style.gap * 0.5f - 0.75f,
                               row.w - 2.0f * style.padding, 1.5f},
                              0.0f, base_quiet.with_alpha(0.22f));
        }
    }

    // The highlight steps back while a row listens: that row has a look of
    // its own, and two frames around one row would argue.
    HighlightStyle look = style.highlight;
    look.grow -= 2.0f * press_.value;
    list.push_transform(1.0f, 0.0f, 0.0f, in.x, in.y - scroll);
    highlight_.draw(canvas, style, look, (0.35f + 0.65f * active) * (1.0f - listen));
    list.pop_transform();

    for (int i = 0; i < n; ++i)
    {
        Rect row = row_rect(i);
        if (row.y > in.y + in.h || row.y + row.h < in.y)
            continue;
        const bool hears = i == listening_;
        if (hears && !still)
            row.x += shake(shake_.value, canvas.time, 9.0f);
        const float radius = paint.control_radius(row);
        const float cy = row.cy();
        const float focus =
            highlight_.coverage({0.0f, row_top(i), in.w, row.h}) * active * (1.0f - listen);
        const Color ink = Highlight::text_color(style, look, focus, base_ink);
        const Color quiet = Highlight::text_color(style, look, focus, base_quiet);
        const float flashed =
            i < rows ? tween::clamp01(flashes_[static_cast<std::size_t>(i)].value) : 0.0f;
        if (flashed > 0.01f)
            paint.fill(row, radius, lit.with_alpha(0.4f * flashed));

        const float left = row.x + style.padding;
        float right = row.x + row.w - style.padding;

        if (i >= rows)
        {
            // The reset row is dimmed while there is nothing to restore.
            const float strength = is_default() ? 0.5f : 1.0f;
            draw_reset(list, right - 14.0f, cy, 13.0f, quiet.with_alpha(strength));
            right -= 44.0f;
            paint.bounded_label(style.reset_label, left, cy + style.label_size * 0.35f, style.label_size, ink.with_alpha(strength), right - left);
            continue;
        }

        const KeyBinding &binding = rows_[static_cast<std::size_t>(i)];
        if (hears && listen > 0.01f)
        {
            // The listening row: a plate and an outline that pulse, a ring
            // that empties as the time runs out, and what to do.
            const float pulse = still ? 0.5f : breathe(clock_, 1.0f);
            paint.fill(row, radius, lit.with_alpha((0.12f + 0.12f * pulse) * listen));
            paint.stroke(row, radius, std::max(theme.border, 2.0f),
                         lit.with_alpha((0.55f + 0.45f * pulse) * listen));
            const float cx = right - kRing;
            const float left_time = listen_left();
            const float share = style.listen_seconds > 0.0f
                                    ? tween::clamp01(left_time / style.listen_seconds)
                                    : 0.0f;
            list.push_opacity(listen);
            list.ring(cx, cy, kRing, 3.5f, base_quiet.with_alpha(0.25f));
            if (share > 0.005f)
                list.arc(cx, cy, kRing, 3.5f, 0.0f, kTwoPi * share, lit);
            char number[8];
            std::snprintf(number, sizeof(number), "%d", static_cast<int>(std::ceil(left_time)));
            paint.label(number, cx, cy + 16.0f * 0.35f, 16.0f, base_ink, gfx::Align::center);
            right -= 2.0f * kRing + 14.0f;
            const float label_room =
                paint.label_width(binding.label, style.label_size) + style.padding;
            const float room = right - left - label_room;
            if (room > 60.0f)
                paint.bounded_body(style.listening_text, right, cy + style.hint_size * 0.35f, style.hint_size, base_quiet, room, gfx::Align::right);
            list.pop_opacity();
            paint.bounded_label(binding.label, left, cy + style.label_size * 0.35f, style.label_size, base_ink, right - left);
            continue;
        }

        list.push_opacity(binding.locked ? 0.45f : 1.0f);
        if (binding.action == Action::count)
        {
            right -= paint.body(style.unbound_text, right, cy + style.hint_size * 0.35f,
                                style.hint_size, quiet, gfx::Align::right);
        }
        else
        {
            // A binding that just changed pops, so the eye finds what moved.
            const float width =
                action_glyph_width(binding.action, style.glyph_size, style.swap_confirm);
            list.push_transform(still ? 1.0f : 1.0f + 0.22f * flashed, right - width * 0.5f, cy,
                                0.0f, 0.0f);
            draw_action_glyph(canvas, theme, binding.action, right - width, cy, style.glyph_size,
                              style.swap_confirm);
            list.pop_transform();
            right -= width;
        }
        right -= 18.0f;
        paint.bounded_label(binding.label, left, cy + style.label_size * 0.35f, style.label_size, ink, right - left);
        list.pop_opacity();
    }
    list.pop_clip();

    if (overflow)
    {
        const float track = in.h - 16.0f;
        const float size = std::max(track * in.h / content, 36.0f);
        const float at = scroll / std::max(content - in.h, 1.0f);
        const float x = in.x + in.w + 8.0f;
        list.rounded_rect({x, in.y + 8.0f, 4.0f, track}, 2.0f, base_quiet.with_alpha(0.16f));
        list.rounded_rect({x, in.y + 8.0f + (track - size) * tween::clamp01(at), 4.0f, size}, 2.0f,
                          base_quiet.with_alpha(0.7f));
    }
}

} // namespace hui::ui
