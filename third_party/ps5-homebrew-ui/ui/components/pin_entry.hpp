// ps5-homebrew-ui - Component: PinEntry, a row of boxes for a numeric code.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/card.hpp"
#include "ui/components/component.hpp"

#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace hui::ui
{

enum class PinState : std::uint8_t
{
    neutral,
    error,   // the code was rejected: cleared, shaken, outlined in the danger colour
    success, // the code was accepted: the boxes fill with the success colour in turn
};

struct PinEntryStyle : ComponentStyle
{
    // ---- geometry ----
    int length = 4; // boxes, 1 to 12
    float box_width = 64.0f;
    float box_height = 76.0f;
    float gap = 12.0f;       // between boxes
    int group = 0;           // more than 0: a wider gap after every `group` boxes
    float group_gap = 18.0f; // ... this much wider
    float label_gap = 12.0f; // between the label line and the boxes
    float message_gap = 14.0f;
    bool centered = false; // the row is centred in the bounds instead of leading
    // ---- type ----
    float digit_size = 36.0f;
    float label_size = 20.0f;
    float message_size = 20.0f;
    // ---- look ----
    bool masked = false;        // a digit turns into a dot ...
    float mask_delay = 0.7f;    // ... this long after it was set (0: at once)
    bool chevrons = true;       // arrows over and under the current box (spin = true)
    bool on_page = false;       // label and message sit on the page, not on a panel
    float success_step = 0.07f; // seconds between boxes filling on accept()
    // ---- behaviour ----
    bool spin = true;                       // up / down change the current digit, 9 wraps to 0
    bool auto_advance = true;               // an inserted digit moves on to the next box
    EdgeExits exits;                        // left / right (and up / down when spin is off)
    audio::Cue enter = audio::Cue::type;    // a digit was typed
    audio::Cue erase = audio::Cue::erase;   // a digit was removed
    audio::Cue success = audio::Cue::saved; // accept()
};

// N boxes for a numeric code, usable with a pad alone (up and down spin the
// current digit, left and right change box, confirm moves on and submits) and
// with a keyboard (insert() and backspace()).
//
//   ui::PinEntry pin;
//   pin.style.theme = theme;
//   pin.style.length = 4;
//   pin.style.masked = true;
//   pin.set_label("Parental code");
//   pin.set_bounds({760, 480, 400, pin.preferred_height()});
//   ...
//   pin.set_active(focused);
//   if (pin.handle(input, feedback) == ui::Event::activated)   // every box is set
//   {
//       if (pin.value() == secret) pin.accept(feedback);
//       else pin.reject(feedback, "That is not the code");
//   }
//   // from a keyboard:  pin.insert('7', feedback);  pin.backspace(feedback);
//   pin.update(dt);
//   pin.draw(canvas);
class PinEntry
{
  public:
    static constexpr int kMaxLength = 12;

    PinEntryStyle style;

    void set_label(std::string label)
    {
        label_ = std::move(label);
    }
    // A quiet line under the boxes; reject() replaces it until the next edit.
    void set_message(std::string message)
    {
        message_ = std::move(message);
    }

    // ---- the value ----
    // The digits that are set, in box order (empty boxes are left out).
    std::string value() const;
    // The digit of one box, or -1 while it is empty.
    int digit(int index) const;
    bool complete() const;
    // Silent. Characters that are not digits are skipped.
    void set_value(std::string_view digits);
    void clear();
    int cursor() const
    {
        return cursor_;
    }
    void set_cursor(int index);
    PinState state() const
    {
        return state_;
    }

    // Typing, for a keyboard. A digit goes into the current box and the
    // cursor moves on: changed, or activated when that completed the code.
    // Anything that is not a digit is refused.
    Event insert(char digit, Feedback &feedback);
    Event insert(std::string_view digits, Feedback &feedback);
    Event backspace(Feedback &feedback);

    // The owner's verdict on a complete code.
    void reject(Feedback &feedback, std::string message = {});
    void accept(Feedback &feedback);
    // Back to neutral and empty, without a sound.
    void reset();

    // ---- the five rules ----
    void set_bounds(const gfx::Rect &bounds);
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    float preferred_width() const;
    float preferred_height() const;
    gfx::Rect box_rect(int index) const;
    void set_active(bool active)
    {
        active_ = active;
    }
    // The edge the focus left through when handle() returned none on a
    // direction (style.exits).
    Direction exit() const
    {
        return exit_;
    }

    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    int count() const;
    float row_width() const;
    float boxes_top() const;
    void set_digit(int index, int value);
    void retarget(bool snap);
    void edited();

    std::string label_;
    std::string message_;
    std::string error_;
    gfx::Rect bounds_{0.0f, 0.0f, 400.0f, 140.0f};
    std::array<int, kMaxLength> digits_{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    std::array<float, kMaxLength> age_{}; // seconds since each box was set
    std::array<Pulse, kMaxLength> pop_;
    int cursor_ = 0;
    PinState state_ = PinState::neutral;
    bool active_ = false;
    Direction exit_ = Direction::none;
    // The digit that is rolling out of the current box, and which way.
    int previous_ = -1;
    int roll_box_ = -1;
    int roll_way_ = 1;
    tween::Spring roll_;
    float clock_ = 0.0f;
    float success_age_ = 0.0f;
    tween::Spring focus_;
    tween::Spring error_amount_;
    Highlight highlight_;
    Pulse shake_;
    Pulse press_up_;
    Pulse press_down_;
};

} // namespace hui::ui
