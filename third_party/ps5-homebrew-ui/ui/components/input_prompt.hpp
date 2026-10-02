// ps5-homebrew-ui - Component: InputPrompt, the "enter a name" overlay.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/keyboard.hpp"
#include "ui/components/overlay.hpp"
#include "ui/components/text_field.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace hui::ui
{

struct InputPromptStyle : ComponentStyle
{
    // ---- geometry ----
    float width = 900.0f;
    float padding = 36.0f; // between the panel's edge and its content
    float gap = 20.0f;     // between the title, the field, the keys and the buttons
    float key_height = 58.0f;
    float key_gap = 8.0f;
    float button_height = 56.0f; // Painter::button sets its label at 24
    float button_width = 190.0f;
    float button_gap = 16.0f;
    // ---- type ----
    float title_size = 34.0f;
    // ---- look ----
    bool buttons = true; // a Cancel / Done row under the keys
    std::string done_label = "Done";
    std::string cancel_label = "Cancel";
    bool frosted = true; // the blurred screen behind (needs canvas.glass), else solid
    float frost = 0.6f;  // how much surface colour covers the frost
    float scrim = 0.55f; // opacity of the veil over the screen
    gfx::Color scrim_color{0.0f, 0.0f, 0.0f, 1.0f};
    // ---- motion ----
    float enter_scale = 0.94f; // the panel grows from this
    float enter_rise = 26.0f;  // ... and rises this far
    float exit_speed = 1.8f;   // leaving is this much quicker than arriving
    // ---- behaviour ----
    int max_length = 0;       // characters; 0 for no limit
    bool password = false;    // the field shows masks
    bool allow_empty = false; // Done accepts an empty text
    bool trim = true;         // spaces at the end are removed on Done
    bool auto_capital = true; // shift is armed while the text is empty
    std::string empty_error = "Type something first";
};

// The overlay most screens want when they need a line of text: a title, a
// TextField, a Keyboard and Done / Cancel on a modal panel. It composes the
// other components; `field` and `keyboard` are public so everything about
// them stays adjustable (label, placeholder, helper, layouts, bindings, the
// highlight). Each frame the prompt gives both its theme, sounds and the
// knobs it owns (max_length, password, key size, auto_capital).
//
//   ui::InputPrompt prompt;
//   prompt.style.theme = theme;
//   prompt.style.max_length = 16;
//   prompt.set_title("Name this save");
//   prompt.field.set_placeholder("Untitled");
//   prompt.keyboard.style.bindings = ui::KeyboardBindings::standard();
//   prompt.open(feedback, save.name);
//   ...
//   if (prompt.is_open())
//   {
//       const ui::Event event = prompt.handle(input, feedback);
//       if (event == ui::Event::activated) save.name = prompt.text();
//   }
//   prompt.update(dt);
//   prompt.draw(overlay); // last, so it is on top
class InputPrompt
{
  public:
    InputPromptStyle style;
    TextField field;
    Keyboard keyboard;

    void set_title(std::string title)
    {
        title_ = std::move(title);
    }
    // The area the scrim covers and the panel is centred in: the whole
    // canvas unless you say otherwise.
    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }

    // Opens on the keys with `initial` in the field and plays sounds.open.
    void open(Feedback &feedback, std::string_view initial = {});
    // Closes and plays sounds.close. Does nothing when already closed.
    void close(Feedback &feedback);
    // Closes without a sound or an exit animation (a screen change).
    void dismiss();
    bool is_open() const
    {
        return open_;
    }
    // Still on screen: open, or animating out.
    bool visible() const;

    // The text as it stands; after Event::activated, the answer.
    const std::string &text() const
    {
        return field.text();
    }
    // True while the focus is on the Cancel / Done row.
    bool on_buttons() const
    {
        return on_buttons_;
    }

    // activated: Done with a text the prompt accepts (it closes). cancelled:
    // back or Cancel (it closes). changed: the text changed. refused: Done on
    // an empty text, a full field, an edge.
    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

    // Where the panel rests.
    gfx::Rect panel_rect() const;

  private:
    void sync();
    void place();
    gfx::Rect button_rect(int index) const;
    Event submit(Feedback &feedback);
    Event cancel(Feedback &feedback);

    std::string title_;
    gfx::Rect bounds_{0.0f, 0.0f, gfx::kVirtualWidth, gfx::kVirtualHeight};
    bool open_ = false;
    bool on_buttons_ = false;
    int button_ = 1; // 0 Cancel, 1 Done
    int pressed_ = -1;
    tween::Spring fade_;
    tween::Bounce pop_;
    tween::Spring buttons_focus_;
    Highlight highlight_; // the ring on the Cancel / Done row
    Pulse press_;
};

} // namespace hui::ui
