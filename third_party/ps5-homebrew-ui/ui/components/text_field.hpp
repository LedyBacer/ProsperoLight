// ps5-homebrew-ui - Component: TextField, a labelled line of text with a caret.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/component.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace hui::ui
{

struct TextFieldStyle : ComponentStyle
{
    // ---- geometry ----
    float field_height = 60.0f;
    float label_gap = 12.0f;  // between the label line and the field
    float helper_gap = 16.0f; // between the field and the helper line (clears a wide ring)
    // ---- type ----
    float label_size = 20.0f;
    float helper_size = 20.0f;
    // ---- look ----
    bool counter = true;               // "5 / 12" at the end of the label line (max_length > 0)
    bool on_page = false;              // label and helper sit on the page, not on a panel
    bool password = false;             // show one mask per character instead of the text
    std::string mask = "\xE2\x80\xA2"; // the bullet; "*" where the face has no such glyph
    // ---- behaviour ----
    int max_length = 0;                   // characters; 0 for no limit
    float caret_period = 1.0f;            // seconds per blink; 0 keeps the caret lit
    audio::Cue type = audio::Cue::type;   // a character went in
    audio::Cue erase = audio::Cue::erase; // a character came out
};

// A text field without a keyboard: it shows a label, the value or a
// placeholder, a caret that blinks only while it is focused, and a helper or
// an error line underneath. Confirm returns Event::activated so the screen
// can open whatever enters text (an on-screen keyboard, the system dialog)
// and feed the result back through insert(), backspace() and clear().
//
//   ui::TextField name;
//   name.style.theme = theme;
//   name.style.max_length = 12;
//   name.set_label("Profile name");
//   name.set_placeholder("Not set");
//   name.set_helper("Shown to other players");
//   name.set_bounds({1200, 560, 420, name.preferred_height()});
//   ...
//   name.set_active(focused);
//   if (focused && name.handle(input, feedback) == ui::Event::activated)
//       open_keyboard();
//   // from the keyboard:  name.insert('a', feedback);  name.backspace(feedback);
//   name.update(dt);
//   name.draw(canvas);
class TextField
{
  public:
    TextFieldStyle style;

    void set_label(std::string label)
    {
        label_ = std::move(label);
    }
    void set_placeholder(std::string placeholder)
    {
        placeholder_ = std::move(placeholder);
    }
    void set_helper(std::string helper)
    {
        helper_ = std::move(helper);
    }
    // A non-empty error replaces the helper, in the danger colour, and shakes
    // the field once. An empty one brings the helper back.
    void set_error(std::string error);
    const std::string &error() const
    {
        return error_;
    }
    void set_disabled(bool disabled)
    {
        disabled_ = disabled;
    }

    // ---- the value ----
    // Replaces the text (cut to max_length). Silent.
    void set_text(std::string_view text);
    const std::string &text() const
    {
        return text_;
    }
    // Characters, not bytes.
    int length() const;
    bool full() const
    {
        return style.max_length > 0 && length() >= style.max_length;
    }

    // Editing without sound, for code. False when nothing changed (the field
    // is full, or already empty). insert() takes one printable ASCII character
    // or a UTF-8 string, which goes in as far as it fits.
    bool insert(char c);
    bool insert(std::string_view utf8);
    bool backspace();
    void clear();
    // Editing with the component's voice, for a keyboard: the type and erase
    // cues, and a soft refusal when the field is full or empty.
    Event insert(char c, Feedback &feedback);
    Event backspace(Feedback &feedback);

    // ---- the five rules ----
    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    // The height the label, the field and the helper line need together.
    float preferred_height() const;
    // Where the field itself is, inside the bounds.
    gfx::Rect field_rect() const;
    void set_active(bool active);

    // Confirm is activated (refused when disabled); back is cancelled.
    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    std::string shown(const Painter &paint, float room, std::string_view mask) const;

    std::string text_;
    std::string label_;
    std::string placeholder_;
    std::string helper_;
    std::string error_;
    gfx::Rect bounds_{0.0f, 0.0f, 420.0f, 124.0f};
    bool active_ = false;
    bool disabled_ = false;
    float blink_ = 0.0f; // seconds since the caret last had a reason to be lit
    tween::Spring focus_;
    tween::Spring error_amount_; // 0 helper .. 1 error
    Pulse shake_;
    Pulse press_;
};

} // namespace hui::ui
