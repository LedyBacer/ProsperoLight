// ps5-homebrew-ui - Component: Form, a scrolling column of rows that edit values.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/choice.hpp"
#include "ui/components/component.hpp"
#include "ui/components/stepper.hpp"

#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace hui::ui
{

enum class FormRowKind : std::uint8_t
{
    header,  // a section title: never focused
    toggle,  // a switch
    choice,  // one value from a list, "<  Value  >"
    slider,  // a number on a track
    stepper, // a whole number, "-  12  +"
    action,  // a button-like row that returns Event::activated
    value,   // read-only: a label and a text
};

// One row. The add_* builders of Form fill it in and return it, so the
// optional parts are set on the result:
//   form.add_slider(kMusic, "Music", 70, 0, 100, 5).unit = " %";
//   form.add_action(kErase, "Erase saved data").danger = true;
struct FormRow
{
    FormRowKind kind = FormRowKind::header;
    int id = 0; // yours; what changed_id() and the getters use
    std::string label;
    std::string description; // shown while the row is focused, and by help_text()
    bool visible = true;     // hidden rows take no space and cannot receive focus
    bool disabled = false;   // focusable and dimmed; every input is refused

    // ---- toggle ----
    bool on = false;
    // ---- slider ----
    float value = 0.0f;
    float minimum = 0.0f;
    float maximum = 1.0f;
    float step = 0.0f; // 0: continuous, moved by style.slider_steps per press
    int decimals = 0;  // digits after the point in the number shown
    std::string unit;  // appended to the number (" %", " ms")
    std::function<std::string(float value)> format; // replaces number and unit
    // ---- choice and stepper: the standalone components, styled by the form ----
    ChoicePicker choice;
    Stepper stepper;
    // ---- action ----
    bool danger = false;  // drawn in the theme's danger colour
    bool chevron = false; // "this opens something"
    // ---- value, and the trailing text of an action ----
    std::string text;

    // Animation, owned by the form.
    tween::Bounce on_amount; // the switch's thumb
    tween::Spring shown;     // the slider's thumb, 0..1
    tween::Spring open;      // how far the description line is unfolded
    Pulse press;
};

struct FormStyle : ComponentStyle
{
    // ---- geometry ----
    float row_height = 72.0f;
    float header_height = 58.0f;
    float gap = 6.0f;             // between rows
    float padding = 26.0f;        // inside a row, left and right
    float panel_padding = 14.0f;  // between the panel and the rows (panel = true)
    float label_ratio = 0.44f;    // the label column's share of a row ...
    float label_width = 0.0f;     // ... or its width in pixels, when above 0
    float control_width = 320.0f; // choices and sliders
    float stepper_width = 196.0f;
    float toggle_width = 76.0f;
    float toggle_height = 40.0f;
    float slider_height = 34.0f;
    float number_width = 86.0f; // room kept for a slider's number, so the track never moves
    // ---- type ----
    float label_size = 26.0f;
    float value_size = 24.0f;
    float header_size = 18.0f;
    float description_size = 21.0f;
    // ---- look ----
    HighlightStyle highlight;
    bool panel = false;             // a themed panel behind the whole form
    bool on_page = true;            // no panel: the rows use the page's text colours
    bool dividers = false;          // hairlines between rows
    bool header_rule = true;        // a hairline after a section title
    bool compact = false;           // tighter rows and smaller type, for dense screens
    bool values_right = true;       // controls end at the row's right edge; false: they
                                    // start where the label column ends
    bool toggle_text = true;        // "On" / "Off" beside a switch
    bool description_inline = true; // the focused row unfolds to show its description
    bool scroll_thumb = true;       // shown only when the form overflows
    StepperButtons stepper_buttons = StepperButtons::surface;
    const char *on_text = "On";
    const char *off_text = "Off";
    // ---- behaviour ----
    bool wrap = false;           // past the last row comes the first
    bool wrap_choices = true;    // a choice goes round at its ends
    bool focus_values = false;   // read-only rows take the focus and are dimmed; false:
                                 // the focus skips them
    float focus_shift = 6.0f;    // the focused row's label moves this far right
    float edge_fade = 0.9f;      // a row cut by the top or bottom edge fades: gone when half
                                 // of it is cut, solid when this share shows; 0: no fade
    float entrance_step = 0.03f; // seconds between rows arriving; 0 for none
    int slider_steps = 50;       // presses from one end to the other of a continuous slider
    int fast_after = 6;          // held repeats before sliders and steppers speed up
    int fast_factor = 4;         // ... to this many steps at once
    bool pitch_by_position = true;
};

// A settings form: typed rows in sections, one highlight that glides between
// them, spring scrolling with faded edges. Up and down move; left, right and
// confirm edit the focused row.
//
//   enum { kHdr, kResolution, kMusic, kReset };
//   ui::Form form;
//   form.style.theme = theme;
//   form.add_header("Display");
//   form.add_toggle(kHdr, "HDR", true).description = "For televisions that support it";
//   form.add_choice(kResolution, "Resolution", {"1080p", "1440p", "2160p"}, 2);
//   form.add_header("Sound");
//   form.add_slider(kMusic, "Music", 70, 0, 100, 5).unit = " %";
//   form.add_action(kReset, "Restore defaults");
//   form.set_bounds({96, 240, 900, 640});
//   ...
//   switch (form.handle(input, feedback))
//   {
//   case ui::Event::changed:   apply(form.changed_id()); break;
//   case ui::Event::activated: run(form.changed_id());   break;
//   default: break;
//   }
//   form.update(dt);
//   form.draw(canvas);
class Form
{
  public:
    // control is where the row's control goes; focus is 0..1.
    using Slot = std::function<void(Canvas &canvas, const gfx::Rect &control, const FormRow &row,
                                    float focus)>;

    FormStyle style;
    Slot trailing; // draws the control of action and value rows instead of text and chevron

    // ---- building ----
    // Each returns the new row for further tweaks (description, disabled,
    // unit, danger...). The reference stays valid until clear().
    FormRow &add_header(std::string label);
    FormRow &add_toggle(int id, std::string label, bool on = false);
    FormRow &add_choice(int id, std::string label, std::vector<std::string> options, int index = 0);
    FormRow &add_slider(int id, std::string label, float value, float minimum, float maximum,
                        float step = 0.0f);
    FormRow &add_stepper(int id, std::string label, int value, int minimum, int maximum,
                         int step = 1);
    FormRow &add_action(int id, std::string label);
    FormRow &add_value(int id, std::string label, std::string text);
    void clear();

    int row_count() const
    {
        return static_cast<int>(rows_.size());
    }
    const FormRow &row_at(int index) const
    {
        return rows_[static_cast<std::size_t>(index)];
    }
    // The row with this id, or null.
    FormRow *row(int id);
    const FormRow *row(int id) const;

    // ---- values, by row id (silent; an unknown id reads as 0) ----
    bool toggle_value(int id) const;
    int choice_index(int id) const;
    const std::string &choice_text(int id) const;
    float slider_value(int id) const;
    int stepper_value(int id) const;
    const std::string &value_text(int id) const;
    void set_toggle(int id, bool on);
    void set_choice(int id, int index);
    void set_slider(int id, float value);
    void set_stepper(int id, int value);
    void set_value_text(int id, std::string text);
    void set_disabled(int id, bool disabled);
    void set_visible(int id, bool visible);

    // ---- the five rules ----
    void set_bounds(const gfx::Rect &bounds);
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    // changed: a value changed; activated: an action row was confirmed
    // (changed_id() names the row for both); moved, refused, cancelled as
    // everywhere. Left and right on a row that has no use for them return
    // Event::none, so a screen can give them a meaning of its own.
    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

    // ---- focus ----
    // The focused row's index and id (-1 when the form has no focusable row).
    int focus() const
    {
        return focus_;
    }
    int focus_id() const;
    // Moves the focus without sound; snap skips the glide (use it on open).
    void set_focus(int index, bool snap = true);
    void focus_row(int id, bool snap = true);
    // The id of the row the last changed or activated event was about.
    int changed_id() const
    {
        return changed_id_;
    }
    // True when the focused row answers left and right itself (a switch, a
    // choice, a slider, a stepper).
    bool uses_horizontal() const;
    // The focused row's description, for a help area of the screen's own.
    const std::string &help_text() const;
    void set_active(bool active)
    {
        active_ = active;
    }
    // Replays the entrance animation.
    void enter();
    // Where a row is on screen right now (scroll applied).
    gfx::Rect row_rect(int index) const;
    // What a slider row shows for its value ("70 %").
    std::string slider_text(const FormRow &row) const;

  private:
    FormRow &add(FormRowKind kind, int id, std::string label);
    gfx::Rect inner() const;
    bool focusable(int index) const;
    int next(int from, int direction) const;
    float scale() const;
    float type(float size) const;
    float base_size(int index) const;
    float fold_size(int index) const;
    float target_top(int index) const;
    float target_height() const;
    void layout();
    void retarget(bool snap);
    Event edit(FormRow &row, int direction, const InputFrame &input, Feedback &feedback);
    Event flip(FormRow &row, bool on, Feedback &feedback, float x);
    ChoiceStyle choice_style() const;
    StepperStyle stepper_style() const;
    gfx::Rect control_rect(const gfx::Rect &row, float width) const;
    void draw_row(Canvas &canvas, Painter &paint, const FormRow &row, const gfx::Rect &rect,
                  float focus, ChoiceStyle &choice_look, StepperStyle &stepper_look) const;

    std::deque<FormRow> rows_; // a deque: add_* must not move the rows already returned
    std::vector<float> tops_;  // where each row is now, description folds included
    gfx::Rect bounds_{0.0f, 0.0f, 800.0f, 600.0f};
    int focus_ = -1;
    int changed_id_ = 0;
    int held_ = 0;
    bool active_ = true;
    bool settle_ = false; // rows were added: the next update snaps instead of gliding
    float age_ = 10.0f;
    Highlight highlight_;
    Scroller scroll_;
    tween::Spring active_amount_{1.0f, 0.0f, 1.0f};
};

} // namespace hui::ui
