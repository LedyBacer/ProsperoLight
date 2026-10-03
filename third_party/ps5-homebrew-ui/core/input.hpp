// ps5-homebrew-ui - Controller input model: raw pad samples to logical actions.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <span>

namespace hui
{

// Raw DualSense state as the platform reads it (button bits follow the
// scePad layout used by ProsperoLight).
namespace pad_bits
{
constexpr std::uint32_t kL3 = 0x00000002;
constexpr std::uint32_t kR3 = 0x00000004;
constexpr std::uint32_t kOptions = 0x00000008;
constexpr std::uint32_t kUp = 0x00000010;
constexpr std::uint32_t kRight = 0x00000020;
constexpr std::uint32_t kDown = 0x00000040;
constexpr std::uint32_t kLeft = 0x00000080;
constexpr std::uint32_t kL2 = 0x00000100;
constexpr std::uint32_t kR2 = 0x00000200;
constexpr std::uint32_t kL1 = 0x00000400;
constexpr std::uint32_t kR1 = 0x00000800;
constexpr std::uint32_t kTriangle = 0x00001000;
constexpr std::uint32_t kCircle = 0x00002000;
constexpr std::uint32_t kCross = 0x00004000;
constexpr std::uint32_t kSquare = 0x00008000;
constexpr std::uint32_t kTouchpad = 0x00100000;
constexpr std::uint32_t kIntercepted = 0x80000000;
} // namespace pad_bits

struct PadSample
{
    std::uint32_t buttons = 0;
    std::uint8_t left_x = 128;
    std::uint8_t left_y = 128;
    std::uint8_t right_x = 128;
    std::uint8_t right_y = 128;
    std::uint8_t l2 = 0;
    std::uint8_t r2 = 0;
    bool connected = false;
    std::uint64_t timestamp_us = 0;
};

// Logical actions the UI and games consume.
enum class Action : std::uint8_t
{
    up,
    down,
    left,
    right,
    confirm,   // Cross (Circle when swapped)
    back,      // Circle (Cross when swapped)
    north,     // Triangle
    west,      // Square
    page_prev, // L1
    page_next, // R1
    jump_prev, // L2
    jump_next, // R2
    menu,      // Options
    touch,     // touchpad click
    l3,
    r3,
    count,
};

constexpr std::uint32_t action_bit(Action action)
{
    return 1u << static_cast<unsigned>(action);
}

enum class Direction : std::uint8_t
{
    none,
    up,
    down,
    left,
    right,
};

struct InputFrame
{
    std::uint32_t pressed = 0;  // went down during the frame (taps are kept)
    std::uint32_t released = 0; // went up during the frame
    std::uint32_t held = 0;     // down at the end of the frame
    // Navigation from the D-pad or left stick: fires on press, then repeats.
    Direction nav = Direction::none;
    bool nav_repeat = false;
    // The step came from the left stick, not the D-pad. Screens that also use
    // the stick as an analog control ignore those steps.
    bool nav_from_stick = false;
    // Left stick after a radial deadzone, -1..1 (right and down positive).
    float stick_x = 0.0f;
    float stick_y = 0.0f;
    // Right stick, same deadzone and range.
    float stick2_x = 0.0f;
    float stick2_y = 0.0f;
    // Analog triggers, 0..1.
    float trigger_l = 0.0f;
    float trigger_r = 0.0f;
    bool connected = false;
    bool focus_lost = false; // disconnected or the system took the input

    bool is_pressed(Action action) const
    {
        return (pressed & action_bit(action)) != 0;
    }
    bool is_held(Action action) const
    {
        return (held & action_bit(action)) != 0;
    }
};

struct InputSettings
{
    bool swap_confirm = false; // Circle confirms, Cross goes back
    std::uint32_t repeat_delay_us = 350000;
    std::uint32_t repeat_interval_us = 110000;
    float stick_deadzone = 0.16f; // radial, fraction of full deflection
};

// Folds every pad sample read this frame into one InputFrame. Edges are
// accumulated across samples so a press and release between two frames still
// registers.
class InputTracker
{
  public:
    InputFrame update(std::span<const PadSample> samples, std::uint64_t now_us);

    void set_settings(const InputSettings &settings)
    {
        settings_ = settings;
    }
    const InputSettings &settings() const
    {
        return settings_;
    }

    // Maps raw button bits (and analog triggers) to logical action bits.
    static std::uint32_t map_buttons(const PadSample &sample, bool swap_confirm);
    // Dominant-axis direction of the left stick beyond a digital threshold.
    static Direction stick_direction(const PadSample &sample);

  private:
    InputSettings settings_;
    std::uint32_t held_ = 0;
    bool connected_ = false;
    bool have_state_ = false;
    bool usable_ = false; // last sample was ours (connected, not intercepted)
    PadSample last_{};
    Direction nav_held_ = Direction::none;
    std::uint64_t nav_next_us_ = 0;
};

} // namespace hui
