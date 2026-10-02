// ps5-homebrew-ui - Component: Wizard, the step indicator of a multi-step flow.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/card.hpp"
#include "ui/components/component.hpp"

#include <string>
#include <vector>

namespace hui::ui
{

struct WizardStep
{
    std::string label;
    std::string caption{}; // a quiet second line ("Optional", "2 of 3 chosen")
    bool error = false;    // something in this step needs attention
    int tag = 0;           // yours
};

enum class WizardKind : std::uint8_t
{
    horizontal, // markers in a row, labels under them
    vertical,   // markers in a column, labels beside them
    compact,    // dots and "Step 2 of 5"
};

struct WizardStyle : ComponentStyle
{
    // ---- geometry ----
    WizardKind kind = WizardKind::horizontal;
    float marker_size = 44.0f;   // a step's marker
    float current_scale = 1.16f; // the current step's marker is this much larger
    float track = 6.0f;          // thickness of the line that joins the markers
    float label_gap = 14.0f;     // between a marker and its label
    float step_height = 84.0f;   // vertical: distance between steps (less when they do not fit)
    float dot_size = 12.0f;      // compact: a dot
    float dot_gap = 10.0f;       // compact: between dots
    float active_width = 36.0f;  // compact: the current step's dot is a pill this long
    // ---- type ----
    float label_size = 22.0f;
    float caption_size = 18.0f;
    float number_size = 20.0f;
    bool numbers = true;            // numbers in the markers; false leaves them empty until done
    bool on_page = true;            // labels have no panel under them: use the page's text colours
    std::string step_word = "Step"; // compact: "Step 2 of 5"
    std::string of_word = "of";
    std::string done_text = "All done"; // compact: shown when every step is done
    // ---- the buttons ----
    bool buttons = false; // draw a Back / Next row and let handle() drive the steps
    float button_width = 170.0f;
    float button_height = 56.0f;
    float button_gap = 16.0f;
    std::string back_label = "Back";
    std::string next_label = "Next";
    std::string finish_label = "Finish"; // the next button on the last step
    // ---- behaviour ----
    EdgeExits exits; // with buttons: edges that hand the focus back to the screen
    bool pitch_by_position = true;
};

// Where the player is in a flow of several steps: numbered markers joined by
// a track that fills as steps complete. A step is upcoming, current, done (a
// check mark draws itself) or in error. Drive it yourself with next() and
// back(), or set style.buttons and forward input: it then draws a Back / Next
// row and reports Event::changed for every step taken.
//
//   ui::Wizard wizard;
//   wizard.style.theme = theme;
//   wizard.style.buttons = true;
//   wizard.set_steps({{"Profile"}, {"Display"}, {"Sound"}, {"Finish"}});
//   wizard.set_bounds({400, 300, 900, 200});
//   ...
//   const ui::Event event = wizard.handle(input, feedback);
//   if (event == ui::Event::changed) show(wizard.step());
//   if (event == ui::Event::activated) finish();          // next on the last step
//   wizard.update(dt);
//   wizard.draw(canvas);
class Wizard
{
  public:
    WizardStyle style;

    void set_steps(std::vector<WizardStep> steps);
    const std::vector<WizardStep> &steps() const
    {
        return steps_;
    }
    WizardStep &step_at(int index)
    {
        return steps_[static_cast<std::size_t>(index)];
    }
    int count() const
    {
        return static_cast<int>(steps_.size());
    }
    void set_bounds(const gfx::Rect &bounds);
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }

    // The current step, 0-based; equal to count() once every step is done.
    int step() const
    {
        return step_;
    }
    bool finished() const
    {
        return step_ >= count();
    }
    // Goes to a step without sound. snap skips the animation.
    void set_step(int step, bool snap = false);
    // One step forward with a cue: Event::changed, or Event::activated when
    // that completed the last step, or Event::refused when all were done.
    Event next(Feedback &feedback);
    // One step back: Event::changed, or Event::refused on the first step.
    Event back(Feedback &feedback);
    void set_error(int index, bool error);

    // Whether the button row has the screen's focus (it shows the ring).
    void set_focused(bool focused)
    {
        focused_ = focused;
    }
    // The focused button: 0 is Back, 1 is Next.
    int button() const
    {
        return button_;
    }
    void set_button(int button);
    // The edge the last handle() left through, or Direction::none.
    Direction exit() const
    {
        return exit_;
    }

    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

    // Where a step's marker is on screen (resting size).
    gfx::Rect marker_rect(int index) const;
    // Where a button of the row is.
    gfx::Rect button_rect(int button) const;

  private:
    gfx::Rect indicator() const;
    float pitch() const;
    float centre_x(int index) const;
    float centre_y(int index) const;
    void retarget(bool snap);
    void draw_track(Canvas &canvas, float value) const;
    void draw_marker(Canvas &canvas, int index) const;
    void draw_compact(Canvas &canvas) const;

    std::vector<WizardStep> steps_;
    gfx::Rect bounds_{0.0f, 0.0f, 800.0f, 200.0f};
    int step_ = 0;
    int button_ = 1;
    bool focused_ = false;
    Direction exit_ = Direction::none;
    tween::Bounce position_; // the current step as an animated number
    Pulse bump_;             // the marker that just became current pops
    tween::Spring focus_amount_;
    Highlight highlight_; // the ring that glides between the two buttons
    Pulse press_;
    int pressed_ = 1;
};

} // namespace hui::ui
