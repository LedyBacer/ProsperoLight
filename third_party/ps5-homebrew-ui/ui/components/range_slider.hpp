// ps5-homebrew-ui - Components: Slider and RangeSlider, one or two thumbs on a track.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/component.hpp"

#include <functional>
#include <string>
#include <utility>

namespace hui::ui
{

// When the value bubble over a thumb is shown.
enum class BubbleMode : std::uint8_t
{
    always,
    focused, // it pops up while the slider has the focus
    never,
};

// Both sliders share one style, so a screen can give them the same look.
struct SliderStyle : ComponentStyle
{
    // ---- geometry ----
    float track_height = 34.0f; // the thumb's size; the bar is the theme's
    float label_gap = 8.0f;     // between the label line and what is under it
    float bubble_height = 32.0f;
    float bubble_padding = 12.0f; // left and right of the bubble's text
    float pointer_size = 7.0f;    // the bubble's pointer
    float bubble_gap = 4.0f;      // between the pointer's tip and the thumb
    float tick_height = 8.0f;
    // ---- type ----
    float label_size = 20.0f;
    float value_size = 20.0f;     // in the bubble and on the label line
    float end_label_size = 18.0f; // the minimum and maximum under the track
    // ---- look ----
    BubbleMode bubble = BubbleMode::always;
    bool value_in_label = true; // the value at the end of the label line when there is no
                                // bubble (BubbleMode::never)
    int ticks = 0;              // marks under the track, both ends included; 0 for none
    bool end_labels = false;    // the minimum and the maximum under the track's ends
    bool on_page = false;       // labels sit on the page, not on a panel
    // ---- behaviour ----
    int continuous_steps = 50;  // presses from one end to the other when the step is 0
    int fast_after = 6;         // held repeats before the stride grows; 0 never
    int fast_factor = 4;        // steps taken at once from then on
    bool pitch_by_value = true; // the step cue rises with the value
    // RangeSlider only:
    bool confirm_switches = true;   // confirm changes which thumb left and right move
    bool vertical_switches = false; // ... and so do up and down
    bool merge_bubbles = true;      // thumbs too close for two bubbles share one ("18 - 23")
};

// One value on a track, with what the slider row of a Form leaves out: a
// label, a bubble that follows the thumb, tick marks and end labels.
//
//   ui::Slider volume;
//   volume.style.theme = theme;
//   volume.set_label("Voice volume");
//   volume.set_range(0, 100, 5);
//   volume.set_unit(" %");
//   volume.set_value(70);
//   volume.set_bounds({96, 300, 480, volume.preferred_height()});
//   ...
//   volume.set_active(focused);
//   if (focused && volume.handle(input, feedback) == ui::Event::changed) apply(volume.value());
//   volume.update(dt);
//   volume.draw(canvas);
class Slider
{
  public:
    using Format = std::function<std::string(float value)>;

    SliderStyle style;
    Format format; // the text of a value (bubble, label line, end labels)

    void set_label(std::string label)
    {
        label_ = std::move(label);
    }
    // step 0 is continuous (style.continuous_steps presses end to end).
    void set_range(float minimum, float maximum, float step = 1.0f);
    // Appended to the number when there is no format slot.
    void set_unit(std::string unit)
    {
        unit_ = std::move(unit);
    }
    // Silent; the thumb eases to the new value.
    void set_value(float value);
    float value() const
    {
        return value_;
    }
    float fraction() const;
    std::string text() const
    {
        return text_for(value_);
    }

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    float preferred_height() const;
    void set_active(bool active)
    {
        active_ = active;
    }

    // Left / right step (changed / refused); back is cancelled. Up, down and
    // confirm are none: they are the screen's.
    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    std::string text_for(float value) const;

    std::string label_;
    std::string unit_;
    gfx::Rect bounds_{0.0f, 0.0f, 480.0f, 110.0f};
    float min_ = 0.0f;
    float max_ = 100.0f;
    float step_ = 1.0f;
    float value_ = 0.0f;
    int held_ = 0;
    bool active_ = false;
    bool placed_ = false;
    tween::Spring shown_; // the thumb, 0..1
    tween::Spring focus_;
    Pulse press_;
    Pulse refusal_;
};

// Two thumbs on one track: a low and a high value with the range between
// them filled. Left and right move the active thumb, confirm switches thumb,
// and a thumb stops at the other one (less the minimum gap) with a soft refusal.
//
//   ui::RangeSlider hours;
//   hours.set_label("Play window");
//   hours.set_range(0, 24, 1);
//   hours.set_min_gap(1);
//   hours.set_values(18, 23);
//   hours.format = [](float v) { return std::to_string(static_cast<int>(v)) + ":00"; };
class RangeSlider
{
  public:
    using Format = std::function<std::string(float value)>;

    SliderStyle style;
    Format format;

    void set_label(std::string label)
    {
        label_ = std::move(label);
    }
    void set_range(float minimum, float maximum, float step = 1.0f);
    void set_unit(std::string unit)
    {
        unit_ = std::move(unit);
    }
    // The least distance the two values keep.
    void set_min_gap(float gap);
    // Silent; put in order and kept the minimum gap apart.
    void set_values(float low, float high);
    float low() const
    {
        return value_[0];
    }
    float high() const
    {
        return value_[1];
    }
    // "18 - 23", with the format slot applied to both.
    std::string text() const;
    // The thumb left and right move: 0 the low one, 1 the high one.
    int thumb() const
    {
        return thumb_;
    }
    void set_thumb(int thumb)
    {
        thumb_ = thumb == 1 ? 1 : 0;
    }

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    float preferred_height() const;
    void set_active(bool active)
    {
        active_ = active;
    }

    // Left / right move the active thumb (changed / refused); confirm (and up
    // / down with style.vertical_switches) switches thumb (moved); back is
    // cancelled.
    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    std::string text_for(float value) const;
    float fraction_of(float value) const;

    std::string label_;
    std::string unit_;
    gfx::Rect bounds_{0.0f, 0.0f, 480.0f, 110.0f};
    float min_ = 0.0f;
    float max_ = 100.0f;
    float step_ = 1.0f;
    float gap_ = 0.0f;
    float value_[2] = {25.0f, 75.0f};
    int thumb_ = 0;
    int held_ = 0;
    bool active_ = false;
    bool placed_ = false;
    tween::Spring shown_[2];
    tween::Spring focus_[2]; // how lit each thumb is
    Pulse press_;
    Pulse refusal_;
};

} // namespace hui::ui
