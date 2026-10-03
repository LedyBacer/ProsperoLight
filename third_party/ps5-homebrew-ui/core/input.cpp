// ps5-homebrew-ui - Controller input model: raw pad samples to logical actions.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/input.hpp"

#include <cmath>

namespace hui
{

namespace
{

// Analog triggers count as pressed past the midpoint (digital L2/R2 bits are
// not verified on hardware yet).
constexpr std::uint8_t kTriggerThreshold = 128;
// ProsperoLight's digital stick threshold: an axis must leave 64..192.
constexpr int kStickLow = 64;
constexpr int kStickHigh = 192;

Direction direction_from_actions(std::uint32_t actions)
{
    if ((actions & action_bit(Action::up)) != 0)
        return Direction::up;
    if ((actions & action_bit(Action::down)) != 0)
        return Direction::down;
    if ((actions & action_bit(Action::left)) != 0)
        return Direction::left;
    if ((actions & action_bit(Action::right)) != 0)
        return Direction::right;
    return Direction::none;
}

float axis(std::uint8_t value)
{
    return (static_cast<float>(value) - 127.5f) / 127.5f;
}

} // namespace

std::uint32_t InputTracker::map_buttons(const PadSample &sample, bool swap_confirm)
{
    const std::uint32_t b = sample.buttons;
    std::uint32_t actions = 0;
    auto map = [&](std::uint32_t bit, Action action)
    {
        if ((b & bit) != 0)
            actions |= action_bit(action);
    };
    map(pad_bits::kUp, Action::up);
    map(pad_bits::kDown, Action::down);
    map(pad_bits::kLeft, Action::left);
    map(pad_bits::kRight, Action::right);
    map(pad_bits::kCross, swap_confirm ? Action::back : Action::confirm);
    map(pad_bits::kCircle, swap_confirm ? Action::confirm : Action::back);
    map(pad_bits::kTriangle, Action::north);
    map(pad_bits::kSquare, Action::west);
    map(pad_bits::kL1, Action::page_prev);
    map(pad_bits::kR1, Action::page_next);
    map(pad_bits::kL2, Action::jump_prev);
    map(pad_bits::kR2, Action::jump_next);
    map(pad_bits::kOptions, Action::menu);
    map(pad_bits::kTouchpad, Action::touch);
    map(pad_bits::kL3, Action::l3);
    map(pad_bits::kR3, Action::r3);
    if (sample.l2 >= kTriggerThreshold)
        actions |= action_bit(Action::jump_prev);
    if (sample.r2 >= kTriggerThreshold)
        actions |= action_bit(Action::jump_next);
    return actions;
}

Direction InputTracker::stick_direction(const PadSample &sample)
{
    const int dx = static_cast<int>(sample.left_x) - 128;
    const int dy = static_cast<int>(sample.left_y) - 128;
    const bool x_out = sample.left_x < kStickLow || sample.left_x > kStickHigh;
    const bool y_out = sample.left_y < kStickLow || sample.left_y > kStickHigh;
    if (!x_out && !y_out)
        return Direction::none;
    if (std::abs(dx) >= std::abs(dy))
        return dx < 0 ? Direction::left : Direction::right;
    return dy < 0 ? Direction::up : Direction::down;
}

InputFrame InputTracker::update(std::span<const PadSample> samples, std::uint64_t now_us)
{
    InputFrame frame;
    bool lost = false;
    for (const PadSample &sample : samples)
    {
        const bool usable = sample.connected && (sample.buttons & pad_bits::kIntercepted) == 0;
        if (!usable)
        {
            // Disconnected or the system overlay owns input: all released.
            // Reported once on the transition, not every frame it lasts.
            if (usable_ || held_ != 0)
                lost = true;
            frame.released |= held_;
            held_ = 0;
            connected_ = sample.connected;
            usable_ = false;
            continue;
        }
        usable_ = true;
        const std::uint32_t actions = map_buttons(sample, settings_.swap_confirm);
        frame.pressed |= actions & ~held_;
        frame.released |= held_ & ~actions;
        held_ = actions;
        connected_ = true;
        last_ = sample;
        have_state_ = true;
    }
    frame.held = held_;
    frame.connected = connected_;
    frame.focus_lost = lost;

    // Analog sticks for pointer and dial modes (radial deadzone, rescaled to 0..1).
    if (connected_ && have_state_ && usable_)
    {
        const auto stick = [&](std::uint8_t raw_x, std::uint8_t raw_y, float *out_x, float *out_y)
        {
            const float x = axis(raw_x);
            const float y = axis(raw_y);
            const float length = std::sqrt(x * x + y * y);
            if (length <= settings_.stick_deadzone)
                return;
            const float scaled = std::fmin(1.0f, (length - settings_.stick_deadzone) /
                                                     (1.0f - settings_.stick_deadzone));
            *out_x = x / length * scaled;
            *out_y = y / length * scaled;
        };
        stick(last_.left_x, last_.left_y, &frame.stick_x, &frame.stick_y);
        stick(last_.right_x, last_.right_y, &frame.stick2_x, &frame.stick2_y);
        frame.trigger_l = static_cast<float>(last_.l2) / 255.0f;
        frame.trigger_r = static_cast<float>(last_.r2) / 255.0f;
    }

    // Navigation: a new direction fires at once; holding it repeats. A tap
    // shorter than a frame still fires through the accumulated press edge.
    Direction current = direction_from_actions(held_);
    bool from_stick = false;
    if (current == Direction::none && connected_ && have_state_)
    {
        current = stick_direction(last_);
        from_stick = current != Direction::none;
    }
    const Direction tapped = direction_from_actions(frame.pressed);
    if (tapped != Direction::none && tapped != current)
    {
        frame.nav = tapped;
    }
    else if (current != nav_held_ && current != Direction::none)
    {
        frame.nav = current;
        nav_next_us_ = now_us + settings_.repeat_delay_us;
    }
    else if (current != Direction::none && now_us >= nav_next_us_)
    {
        frame.nav = current;
        frame.nav_repeat = true;
        nav_next_us_ = now_us + settings_.repeat_interval_us;
    }
    nav_held_ = current;
    frame.nav_from_stick = from_stick && frame.nav == current && tapped == Direction::none;
    return frame;
}

} // namespace hui
