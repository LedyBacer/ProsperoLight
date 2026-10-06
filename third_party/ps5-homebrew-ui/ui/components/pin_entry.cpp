// ps5-homebrew-ui - Component: PinEntry.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/pin_entry.hpp"

#include "ui/components/focus_frame.hpp"

#include <algorithm>
#include <cmath>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

constexpr float kChevronRoom = 26.0f; // kept free over and under the boxes for the arrows
constexpr float kRoll = 14.0f;        // how far a spun digit travels
constexpr float kMaskFade = 0.16f;    // seconds a digit takes to become a dot

} // namespace

int PinEntry::count() const
{
    return std::clamp(style.length, 1, kMaxLength);
}

int PinEntry::digit(int index) const
{
    if (index < 0 || index >= count())
        return -1;
    return digits_[static_cast<std::size_t>(index)];
}

std::string PinEntry::value() const
{
    std::string out;
    for (int i = 0; i < count(); ++i)
    {
        if (digit(i) >= 0)
            out.push_back(static_cast<char>('0' + digit(i)));
    }
    return out;
}

bool PinEntry::complete() const
{
    for (int i = 0; i < count(); ++i)
    {
        if (digit(i) < 0)
            return false;
    }
    return true;
}

void PinEntry::set_digit(int index, int value)
{
    digits_[static_cast<std::size_t>(index)] = value;
    age_[static_cast<std::size_t>(index)] = 0.0f;
}

void PinEntry::set_value(std::string_view digits)
{
    clear();
    int at = 0;
    for (char c : digits)
    {
        if (c < '0' || c > '9' || at >= count())
            continue;
        set_digit(at++, c - '0');
    }
    // A value set by code is not something the player just typed: mask it.
    age_.fill(style.mask_delay + kMaskFade);
    cursor_ = std::min(at, count() - 1);
    retarget(true);
}

void PinEntry::clear()
{
    digits_.fill(-1);
    cursor_ = 0;
    roll_box_ = -1;
    retarget(false);
}

void PinEntry::set_cursor(int index)
{
    cursor_ = std::clamp(index, 0, count() - 1);
    retarget(true);
}

void PinEntry::reset()
{
    state_ = PinState::neutral;
    error_.clear();
    clear();
}

// Any edit answers an error: the danger colour and its message go away.
void PinEntry::edited()
{
    if (state_ != PinState::error)
        return;
    state_ = PinState::neutral;
    error_.clear();
}

void PinEntry::reject(Feedback &feedback, std::string message)
{
    state_ = PinState::error;
    error_ = std::move(message);
    clear();
    if (!style.reduced_motion)
        shake_.trigger();
    play_cue(feedback, style, style.sounds.refuse, bounds_.cx());
    if (style.sounds.rumble > 0.0f)
        feedback.rumble(0.4f * style.sounds.rumble, 0.08f);
}

void PinEntry::accept(Feedback &feedback)
{
    state_ = PinState::success;
    error_.clear();
    success_age_ = 0.0f;
    play_cue(feedback, style, style.success, bounds_.cx());
}

void PinEntry::set_bounds(const Rect &bounds)
{
    const bool changed = bounds.x != bounds_.x || bounds.y != bounds_.y || bounds.w != bounds_.w ||
                         bounds.h != bounds_.h;
    bounds_ = bounds;
    if (changed)
        retarget(true);
}

float PinEntry::row_width() const
{
    const int n = count();
    float width = static_cast<float>(n) * style.box_width + static_cast<float>(n - 1) * style.gap;
    if (style.group > 0)
        width += static_cast<float>((n - 1) / style.group) * style.group_gap;
    return width;
}

float PinEntry::preferred_width() const
{
    return row_width();
}

float PinEntry::boxes_top() const
{
    float top = bounds_.y;
    if (!label_.empty())
        top += style.label_size + style.label_gap;
    if (style.spin && style.chevrons)
        top += kChevronRoom;
    return top;
}

float PinEntry::preferred_height() const
{
    float height = boxes_top() - bounds_.y + style.box_height;
    if (style.spin && style.chevrons)
        height += kChevronRoom;
    if (!message_.empty() || !error_.empty())
        height += style.message_gap + style.message_size * 1.2f;
    return height;
}

Rect PinEntry::box_rect(int index) const
{
    const float start = style.centered ? bounds_.cx() - row_width() * 0.5f : bounds_.x;
    float x = start + static_cast<float>(index) * (style.box_width + style.gap);
    if (style.group > 0)
        x += static_cast<float>(index / style.group) * style.group_gap;
    return {x, boxes_top(), style.box_width, style.box_height};
}

void PinEntry::retarget(bool snap)
{
    cursor_ = std::clamp(cursor_, 0, count() - 1);
    const Rect target = box_rect(cursor_);
    highlight_.target(target);
    if (snap)
        highlight_.snap(target);
}

Event PinEntry::insert(char c, Feedback &feedback)
{
    const int n = count();
    cursor_ = std::clamp(cursor_, 0, n - 1);
    const float x = box_rect(cursor_).cx();
    if (c < '0' || c > '9' || state_ == PinState::success)
        return refuse(feedback, style, InputFrame{}, highlight_.refusal(), x);
    edited();
    set_digit(cursor_, c - '0');
    roll_box_ = -1;
    if (!style.reduced_motion)
        pop_[static_cast<std::size_t>(cursor_)].trigger();
    // The code climbs as it fills.
    const float along = n > 1 ? static_cast<float>(cursor_) / static_cast<float>(n - 1) : 0.0f;
    play_cue(feedback, style, style.enter, x, tween::lerp(0.96f, 1.08f, along));
    if (complete())
        return Event::activated;
    if (style.auto_advance && cursor_ < n - 1)
    {
        ++cursor_;
        retarget(false);
    }
    return Event::changed;
}

Event PinEntry::insert(std::string_view digits, Feedback &feedback)
{
    Event result = Event::none;
    for (char c : digits)
    {
        result = insert(c, feedback);
        if (result == Event::activated)
            break;
    }
    return result;
}

Event PinEntry::backspace(Feedback &feedback)
{
    cursor_ = std::clamp(cursor_, 0, count() - 1);
    const bool here = digit(cursor_) >= 0;
    if (state_ == PinState::success || (!here && cursor_ == 0))
        return refuse(feedback, style, InputFrame{}, highlight_.refusal(), box_rect(cursor_).cx());
    // An empty box has nothing to delete: the one before it goes.
    if (!here)
        --cursor_;
    edited();
    set_digit(cursor_, -1);
    roll_box_ = -1;
    retarget(false);
    play_cue(feedback, style, style.erase, box_rect(cursor_).cx());
    return Event::changed;
}

Event PinEntry::handle(const InputFrame &input, Feedback &feedback)
{
    exit_ = Direction::none;
    const int n = count();
    cursor_ = std::clamp(cursor_, 0, n - 1);
    const float x = box_rect(cursor_).cx();

    if (input.is_pressed(Action::back))
    {
        play_cue(feedback, style, style.sounds.cancel, x);
        return Event::cancelled;
    }
    // An accepted code is settled: nothing edits it until reset().
    if (state_ == PinState::success)
        return Event::none;

    if (input.nav == Direction::left || input.nav == Direction::right)
    {
        const int next = cursor_ + (input.nav == Direction::right ? 1 : -1);
        if (next < 0 || next >= n)
        {
            if (style.exits.allows(input.nav))
            {
                exit_ = input.nav;
                return Event::none;
            }
            return refuse(feedback, style, input, highlight_.refusal(), x);
        }
        cursor_ = next;
        retarget(false);
        play_cue(feedback, style, style.sounds.move, box_rect(cursor_).cx());
        return Event::moved;
    }
    if (input.nav == Direction::up || input.nav == Direction::down)
    {
        if (!style.spin)
        {
            if (style.exits.allows(input.nav))
            {
                exit_ = input.nav;
                return Event::none;
            }
            return refuse(feedback, style, input, highlight_.refusal(), x);
        }
        const bool up = input.nav == Direction::up;
        const int old = digit(cursor_);
        // An empty box starts at 0 going up and at 9 going down.
        const int next = old < 0 ? (up ? 0 : 9) : (old + (up ? 1 : 9)) % 10;
        edited();
        previous_ = old;
        roll_box_ = cursor_;
        roll_way_ = up ? 1 : -1;
        roll_.snap(1.0f);
        roll_.target = 0.0f;
        set_digit(cursor_, next);
        (up ? press_up_ : press_down_).trigger();
        play_cue(feedback, style, style.sounds.step, x, 0.92f + 0.016f * static_cast<float>(next));
        return Event::changed;
    }
    if (input.is_pressed(Action::confirm))
    {
        if (complete())
        {
            play_cue(feedback, style, style.sounds.activate, x);
            if (style.sounds.rumble > 0.0f)
                feedback.rumble(0.3f * style.sounds.rumble, 0.04f);
            return Event::activated;
        }
        // Confirm is "next" until the code is whole.
        if (digit(cursor_) < 0 || cursor_ >= n - 1)
            return refuse(feedback, style, input, highlight_.refusal(), x);
        ++cursor_;
        retarget(false);
        play_cue(feedback, style, style.sounds.move, box_rect(cursor_).cx());
        return Event::moved;
    }
    return Event::none;
}

void PinEntry::update(float dt)
{
    clock_ += dt;
    success_age_ += dt;
    for (float &age : age_)
        age += dt;
    for (Pulse &pop : pop_)
        pop.update(dt, 11.0f);
    // The knobs may have changed since the last frame: follow them.
    retarget(false);
    highlight_.update(dt, style);
    focus_.target = active_ ? 1.0f : 0.0f;
    focus_.update(dt, 18.0f);
    error_amount_.target = state_ == PinState::error ? 1.0f : 0.0f;
    error_amount_.update(dt, 14.0f);
    roll_.update(dt, std::max(style.omega(), 14.0f) * 1.3f);
    shake_.update(dt, 7.0f);
    press_up_.update(dt, 10.0f);
    press_down_.update(dt, 10.0f);
}

void PinEntry::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const int n = count();
    const bool still = style.reduced_motion;
    const float focus = focus_.value;
    const float wrong = tween::clamp01(error_amount_.value);
    const bool done = state_ == PinState::success;
    const Color page_ink = style.on_page ? paint.page_text() : theme.text;
    const Color page_quiet = style.on_page ? paint.page_text_muted() : theme.text_muted;
    // The boxes are small text fields and take the field look of the theme:
    // a line where fields are lines, a white face in the bevel and hard looks.
    const bool line = theme.underline_fields;
    const bool white =
        !line && (theme.style == SurfaceStyle::bevel || theme.style == SurfaceStyle::hard);
    const Color face = white ? theme.light : theme.surface_high;
    const Color ink = line ? page_ink : (white ? Painter::on(face) : theme.text);
    const float radius = std::min(theme.radius, 12.0f);
    const bool square = theme.corner != Corner::round || theme.radius < 2.0f;

    list.push_transform(1.0f, 0.0f, 0.0f, still ? 0.0f : shake(shake_.value, canvas.time, 10.0f),
                        0.0f);

    if (!label_.empty())
        paint.bounded_label(label_, bounds_.x, bounds_.y + style.label_size * 0.82f, style.label_size, gfx::mix(page_quiet, page_ink, focus), bounds_.w);

    // How far the success colour has reached a box.
    const auto filled = [&](int index)
    {
        if (!done)
            return 0.0f;
        if (still || style.success_step <= 0.0f)
            return tween::cubic_out(success_age_ / 0.2f);
        return tween::stagger(success_age_, index, style.success_step, 0.28f);
    };

    const Rect ring = highlight_.rect(canvas.time);
    const bool under = !line && focus_frame_goes_under(theme);
    const float ring_amount = done ? 0.0f : focus;
    if (under)
        focus_frame(canvas, theme, ring, radius, ring_amount, true);

    for (int i = 0; i < n; ++i)
    {
        const Rect box = box_rect(i);
        const float here = highlight_.coverage(box) * focus;
        if (line)
        {
            const float thick = 3.0f + 1.5f * here;
            list.rounded_rect({box.x, box.y + box.h - thick, box.w, thick}, 0.0f,
                              gfx::mix(page_quiet, theme.primary, here));
        }
        else
        {
            paint.well(box, radius, face);
        }
        const float fill = filled(i);
        if (fill > 0.01f)
            paint.fill(box, radius, theme.success.with_alpha(fill));
        if (wrong > 0.01f)
            paint.stroke(box, radius, std::max(theme.border, 2.5f), theme.danger.with_alpha(wrong));
    }

    if (!under)
        focus_frame(canvas, theme, ring, radius, ring_amount, !line);

    for (int i = 0; i < n; ++i)
    {
        const Rect box = box_rect(i);
        const int value = digit(i);
        const Color colour = gfx::mix(ink, Painter::on(theme.success), filled(i));
        if (value < 0)
        {
            // An empty box shows where the next digit goes: a caret under
            // the cursor, a faint dot elsewhere.
            const float here = highlight_.coverage(box) * focus;
            const float blink = still ? 1.0f : 0.45f + 0.55f * breathe(clock_, 1.1f);
            list.rounded_rect({box.cx() - 11.0f, box.y + box.h - 18.0f, 22.0f, 3.0f},
                              square ? 0.0f : 1.5f, colour.with_alpha(here * blink));
            if (square)
                list.rounded_rect({box.cx() - 3.0f, box.cy() - 3.0f, 6.0f, 6.0f}, 0.0f,
                                  colour.with_alpha(0.28f * (1.0f - here)));
            else
                list.circle(box.cx(), box.cy(), 3.5f, colour.with_alpha(0.28f * (1.0f - here)));
            continue;
        }
        const float age = age_[static_cast<std::size_t>(i)];
        const float hidden =
            style.masked ? tween::smoothstep((age - style.mask_delay) / kMaskFade) : 0.0f;
        const float pop = pop_[static_cast<std::size_t>(i)].value;
        list.push_transform(1.0f + 0.22f * pop, box.cx(), box.cy(), 0.0f, 0.0f);
        if (hidden < 0.99f)
        {
            const float baseline = box.cy() + style.digit_size * 0.35f;
            const auto number = [&](int shown, float dy, float alpha)
            {
                if (shown < 0 || alpha <= 0.01f)
                    return;
                const char text[2] = {static_cast<char>('0' + shown), '\0'};
                paint.label(text, box.cx(), baseline + dy, style.digit_size,
                            colour.with_alpha(alpha * (1.0f - hidden)), gfx::Align::center);
            };
            // A spun digit rolls like a counter: up when it grows.
            const float t = i == roll_box_ ? tween::clamp01(std::fabs(roll_.value)) : 0.0f;
            const float travel = still ? 0.0f : kRoll * static_cast<float>(roll_way_);
            if (t > 0.01f)
                number(previous_, -travel * (1.0f - t), t);
            number(value, travel * t, 1.0f - t);
        }
        if (hidden > 0.01f)
        {
            const float dot = style.digit_size * 0.2f;
            if (square)
                list.rounded_rect({box.cx() - dot, box.cy() - dot, 2.0f * dot, 2.0f * dot}, 0.0f,
                                  colour.with_alpha(hidden));
            else
                list.circle(box.cx(), box.cy(), dot, colour.with_alpha(hidden));
        }
        list.pop_transform();
    }

    if (style.spin && style.chevrons && !done && focus > 0.01f)
    {
        // The arrows say "up and down change this digit"; each one jumps
        // when its direction is pressed.
        const float lift = still ? 0.0f : 4.0f;
        const Color arrow = page_quiet.with_alpha(focus);
        // Clear of the focus ring, whatever its reach in this theme.
        const float away = theme.focus_gap + theme.focus_width + 6.0f;
        list.triangle(
            {ring.cx() - 8.0f, ring.y - away - 9.0f - lift * press_up_.value, 16.0f, 9.0f}, arrow);
        list.triangle(
            {ring.cx() - 8.0f, ring.y + ring.h + away + lift * press_down_.value, 16.0f, 9.0f},
            arrow, 0.0f, 3.14159265f);
    }

    const float baseline = boxes_top() + style.box_height +
                           (style.spin && style.chevrons ? kChevronRoom : 0.0f) +
                           style.message_gap + style.message_size * 0.82f;
    if (!error_.empty())
        paint.bounded_body(error_, bounds_.x, baseline, style.message_size, theme.danger.with_alpha(wrong), bounds_.w);
    if (!message_.empty() && wrong < 0.99f)
        paint.bounded_body(message_, bounds_.x, baseline, style.message_size, page_quiet.with_alpha(1.0f - wrong), bounds_.w);

    list.pop_transform();
}

} // namespace hui::ui
