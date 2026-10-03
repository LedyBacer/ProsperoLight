// ps5-homebrew-ui - Component: Select, a field that opens a list of options.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/component.hpp"
#include "ui/components/list.hpp"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace hui::ui
{

struct SelectOption
{
    std::string label;
    std::string description;                   // a second line in the list
    gfx::Color swatch{0.0f, 0.0f, 0.0f, 0.0f}; // a leading dot, when alpha > 0
    bool disabled = false;                     // focusable in the list, refuses confirm
    int tag = 0;                               // yours

    // So a list of options can be written {{"One"}, {"Two", "Its second line"}}.
    SelectOption() = default;
    SelectOption(std::string text, std::string second_line = {})
        : label(std::move(text)), description(std::move(second_line))
    {
    }
};

// Where the field's label goes.
enum class SelectLabel : std::uint8_t
{
    above,  // a line over the field
    inside, // at the field's left, the value at its right
    none,
};

struct SelectStyle : ComponentStyle
{
    // ---- the closed field ----
    SelectLabel label = SelectLabel::above;
    float field_height = 60.0f;
    float padding = 20.0f;      // inside the field, left and right
    float label_size = 20.0f;   // the label (SelectLabel::above)
    float label_gap = 10.0f;    // between that label and the field
    float value_size = 24.0f;   // the value, and an inside label
    float chevron_size = 8.0f;  // half the chevron's width
    float chevron_width = 3.0f; // its stroke
    bool focus_ring = true;     // the theme's ring around the field when focused
    bool on_page = false;       // the label above sits on the page, not on a panel
    bool step_closed = false;   // left / right change the value without opening the list
    // ---- the list ----
    int max_rows = 6;               // rows shown at once; more scroll
    float row_height = 54.0f;       // a row without a description
    float described_height = 70.0f; // rows when any option has a description
    float row_gap = 2.0f;           // between rows
    float row_padding = 16.0f;      // inside a row, left and right
    float option_size = 24.0f;      // the option's text
    float description_size = 19.0f; // its second line
    float leading_width = 0.0f;     // room reserved for the `leading` slot
    float popover_padding = 10.0f;  // between the panel and the rows
    float popover_gap = 8.0f;       // between the field and the panel
    float popover_width = 0.0f;     // 0: as wide as the field
    HighlightStyle highlight;       // the list's gliding highlight
    bool check = true;              // a check mark on the current option
    bool frosted = true;            // glass themes blur what is behind the panel
    float backing = 0.8f;           // glass themes: page colour under the panel, this opaque
    float elevation = 1.0f;         // scales the shadow the panel floats on; 0 for none
    float scrim = 0.0f;             // darkens everything behind the list by this much (0..1)
    bool wrap = false;              // past the last option comes the first
    float entrance_step = 0.012f;   // seconds between rows arriving; 0 for none
};

// The dropdown for long lists: a field that shows the current value and, on
// confirm, opens a scrolling list anchored to it. The list flips above the
// field when there is no room below, starts on the current option and marks
// it with a check. Confirm picks, back closes without a change.
//
//   ui::Select region;
//   region.style.theme = theme;
//   region.set_label("Region");
//   region.set_options({{"Northern Isles"}, {"Amber Coast", "Warm, crowded servers"}});
//   region.set_index(0);
//   region.set_bounds({96, 300, 480, region.preferred_height()});
//   ...
//   region.set_active(focused);
//   if (focused || region.is_open())
//       if (region.handle(input, feedback) == ui::Event::changed) apply(region.index());
//   region.update(dt);
//   region.draw(canvas);            // the field, with the rest of the screen
//   region.draw_popover(overlay);   // the list, last (a modal layer)
//
// While is_open() the list is modal: forward every input to handle().
class Select
{
  public:
    // box is the room reserved by style.leading_width in a row; focus is 0..1.
    using Leading = std::function<void(Canvas &canvas, const gfx::Rect &box,
                                       const SelectOption &option, int index, float focus)>;

    SelectStyle style;
    Leading leading; // draws at the start of every row of the list

    void set_label(std::string label)
    {
        label_ = std::move(label);
    }
    // Shown while nothing is selected (index -1).
    void set_placeholder(std::string placeholder)
    {
        placeholder_ = std::move(placeholder);
    }
    void set_options(std::vector<SelectOption> options);
    const std::vector<SelectOption> &options() const
    {
        return options_;
    }
    // Silent; -1 selects nothing.
    void set_index(int index);
    int index() const
    {
        return index_;
    }
    // The selected option's label; empty when nothing is selected.
    const std::string &value() const;

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    // The part of the screen the list may cover (it flips and shortens to fit).
    void set_limits(const gfx::Rect &limits)
    {
        limits_ = limits;
    }
    void set_active(bool active)
    {
        active_ = active;
    }
    // Label line and field together.
    float preferred_height() const;
    gfx::Rect field_rect() const;
    // Where the list is (or would be), and whether it opens above the field.
    gfx::Rect popover_rect() const;
    bool opens_above() const;

    // Opens the list with the open cue, focused on the current option.
    void open(Feedback &feedback);
    // Closes it with the close cue and no change.
    void close(Feedback &feedback);
    // Closes it without a sound (the screen is leaving).
    void dismiss()
    {
        open_ = false;
    }
    bool is_open() const
    {
        return open_;
    }
    // Still drawn: the list fades out after it closed.
    bool visible() const
    {
        return open_ || amount_.value > 0.01f;
    }
    // The option under the list's highlight.
    int focus() const
    {
        return list_.focus();
    }

    // Closed: confirm opens (activated). Open: up / down move, confirm picks
    // (changed, or cancelled when it is the current option), back closes
    // (cancelled).
    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;
    void draw_popover(Canvas &canvas) const;

  private:
    struct Placement
    {
        gfx::Rect panel;
        bool above = false;
        int rows = 1;
    };
    Placement place() const;
    float row_size() const;
    void sync();
    Event step(int direction, const InputFrame &input, Feedback &feedback);

    std::vector<SelectOption> options_;
    std::string label_;
    std::string placeholder_ = "Choose";
    gfx::Rect bounds_{0.0f, 0.0f, 420.0f, 90.0f};
    gfx::Rect limits_{48.0f, 48.0f, gfx::kVirtualWidth - 96.0f, gfx::kVirtualHeight - 96.0f};
    int index_ = -1;
    bool described_ = false; // an option has a second line
    bool active_ = false;
    bool open_ = false;
    float laid_row_ = 0.0f; // the row size and gap the list was last laid out with
    float laid_gap_ = 0.0f;
    ListView list_;
    tween::Spring focus_;
    tween::Bounce amount_; // 0 closed .. 1 open
    Pulse refusal_;
};

} // namespace hui::ui
