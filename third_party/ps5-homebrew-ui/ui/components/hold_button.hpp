// ps5-homebrew-ui - Component: HoldButton, hold to confirm.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// For actions that should not happen by accident: delete, reset, quit. The
// player keeps the button down while it fills; letting go early drains it
// back. No dialog interrupts, and a stray press does nothing.

#pragma once

#include "ui/components/button.hpp"

#include <cstdint>
#include <string>

namespace hui::ui
{

enum class HoldVariant : std::uint8_t
{
    fill,      // a wash sweeps across the button
    ring,      // a ring closes around the controller glyph
    underline, // a line grows along the bottom of the button
};

struct HoldButtonStyle : ComponentStyle
{
    ButtonRole role = ButtonRole::danger;
    ButtonSize size = ButtonSize::medium;
    HoldVariant variant = HoldVariant::fill;
    // ---- geometry (0: from the size) ----
    float height = 0.0f;
    float text_size = 0.0f;
    float padding = 0.0f;
    float gap = 12.0f;        // between the glyph and the label
    float glyph_size = 0.0f;  // 0: from the text size
    float ring_width = 4.0f;  // HoldVariant::ring
    float line_height = 6.0f; // HoldVariant::underline
    float wash = 0.3f;        // HoldVariant::fill: how strong the sweep is over the body
    // ---- behaviour ----
    Action action = Action::confirm; // the input that is held
    Button glyph = Button::cross;    // the glyph that stands for it (none hides it)
    bool glyph_tinted = true;        // false: the glyph in the label's colour
    float hold_seconds = 1.2f;       // how long a full hold takes
    float drain_speed = 3.5f;        // letting go empties it this many times faster
    float tap_seconds = 0.22f;       // released sooner than this, it was a tap: the hint shows
    float hint_seconds = 1.5f;       // how long the hint stays
    bool on_page = true;             // see PushButtonStyle
    // ---- feel ----
    audio::Cue complete_cue = audio::Cue::launch; // the stronger cue at the end of a hold
    bool ticks = true;                            // a rising tick at each quarter of the hold
    float rumble_from = 0.08f;                    // rumble while holding ramps between these
    float rumble_to = 0.55f;
    float rumble_done = 0.8f;  // the pulse when it completes
    float press_scale = 0.03f; // it sinks this much while held
};

// A button that must be held.
//
//   ui::HoldButton erase;
//   erase.label = "Delete save";
//   erase.hint = "Hold to delete";
//   erase.set_bounds({96, 400, 360, 64});
//   erase.set_active(true);
//   ...
//   if (erase.handle(input, feedback) == ui::Event::activated) erase_save();
//   erase.update(dt);
//   erase.draw(canvas);
//
// handle() must be called every frame while it has the focus: the hold is
// read from input.is_held(), and a frame without handle() releases it.
class HoldButton
{
  public:
    HoldButtonStyle style;
    std::string label;
    std::string hint = "Hold"; // replaces the label for a moment after a tap

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    gfx::Rect rect() const;
    void set_active(bool active);
    void set_disabled(bool disabled)
    {
        disabled_ = disabled;
    }
    bool disabled() const
    {
        return disabled_;
    }
    // How full it is, 0..1, and whether it is being held right now.
    float progress() const
    {
        return progress_;
    }
    bool holding() const
    {
        return armed_;
    }
    // True while the hint is showing.
    bool hinting() const
    {
        return hint_left_ > 0.0f;
    }
    // Empties it and forgets the hold (the screen is leaving).
    void reset();

    // Event::activated once when a hold completes; Event::refused for a tap
    // or a press while disabled.
    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    gfx::Rect bounds_{0.0f, 0.0f, 360.0f, 64.0f};
    bool active_ = false;
    bool disabled_ = false;
    bool armed_ = false;   // the action went down here and is still held
    bool renewed_ = false; // handle() saw the hold this frame
    float progress_ = 0.0f;
    float held_for_ = 0.0f;
    float hint_left_ = 0.0f;
    int ticks_ = 0; // quarters already announced
    tween::Spring focus_;
    tween::Spring down_;
    tween::Spring hint_amount_;
    Pulse done_;
    Pulse refusal_;
};

} // namespace hui::ui
