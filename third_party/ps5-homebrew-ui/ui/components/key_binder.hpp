// ps5-homebrew-ui - Component: KeyBinder, rows that remap controller buttons.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/card.hpp"
#include "ui/components/component.hpp"

#include <string>
#include <vector>

namespace hui::ui
{

struct KeyBinding
{
    int id = 0;        // yours; reported by changed_id()
    std::string label; // "Jump"
    // The input this is bound to; Action::count while it has none.
    Action action = Action::count;
    // What "Reset to defaults" restores. Leave it at Action::count to take
    // the action the binding had when it was given to set_bindings().
    Action fallback = Action::count;
    bool locked = false; // shown, but cannot be changed
};

enum class BindConflict : std::uint8_t
{
    swap,   // the row that had the button takes this row's old one; both flash
    refuse, // the press is refused and the row keeps listening
    allow,  // two rows may share a button
};

struct KeyBinderStyle : ComponentStyle
{
    // The inputs a row may be bound to: the face buttons, the D-pad and the
    // stick clicks. The shoulder buttons, the triggers, Options and the
    // touchpad are left to the application.
    static constexpr std::uint32_t kDefaultAllowed =
        action_bit(Action::confirm) | action_bit(Action::back) | action_bit(Action::north) |
        action_bit(Action::west) | action_bit(Action::up) | action_bit(Action::down) |
        action_bit(Action::left) | action_bit(Action::right) | action_bit(Action::l3) |
        action_bit(Action::r3);

    // ---- geometry ----
    float row_height = 60.0f;
    float gap = 4.0f;            // between rows
    float padding = 22.0f;       // inside a row, left and right
    float panel_padding = 12.0f; // between the panel and the rows (panel = true)
    float glyph_size = 38.0f;    // the controller glyph of a binding
    // ---- type ----
    float label_size = 25.0f;
    float hint_size = 20.0f; // "Press a button..." and "Not set"
    // ---- look ----
    HighlightStyle highlight;
    bool panel = false;    // a themed panel behind the rows
    bool dividers = true;  // hairlines between rows
    bool on_page = false;  // rows without a panel sit on the page
    bool reset_row = true; // a last row that restores every fallback
    std::string reset_label = "Reset to defaults";
    std::string listening_text = "Press a button...";
    std::string unbound_text = "Not set";
    // ---- behaviour ----
    std::uint32_t allowed = kDefaultAllowed; // action bits a row accepts
    BindConflict conflict = BindConflict::swap;
    float listen_seconds = 4.0f;  // a row stops listening after this long
    bool cancel_with_back = true; // back ends listening; false: back can be bound
    bool swap_confirm = false;    // mirror the player's setting: which glyph confirm shows
    bool wrap = false;            // past the last row comes the first
    EdgeExits exits;              // edges that hand the focus back
};

// Controller remapping: a list of actions, each with the button it is bound
// to. Confirm on a row makes it listen: it pulses, counts down and takes the
// next button the player presses.
//
//   ui::KeyBinder binder;
//   binder.style.theme = theme;
//   binder.set_bindings({{kJump, "Jump", Action::confirm},
//                        {kDodge, "Dodge", Action::back},
//                        {kMap, "Open the map", Action::north}});
//   binder.set_bounds({1200, 300, 520, 400});
//   ...
//   // While binder.listening() give it every input: it is waiting for one.
//   if (binder.handle(input, feedback) == ui::Event::changed)
//       save(binder.changed_id(), binder.action_of(binder.changed_id()));
//   binder.update(dt);
//   binder.draw(canvas);
class KeyBinder
{
  public:
    // changed_id() after "Reset to defaults".
    static constexpr int kResetId = -1;

    KeyBinderStyle style;

    void set_bindings(std::vector<KeyBinding> bindings);
    const std::vector<KeyBinding> &bindings() const
    {
        return rows_;
    }
    // The action a binding has now; Action::count for an unknown id.
    Action action_of(int id) const;
    // Silent. False for an unknown id.
    bool set_action(int id, Action action);
    // Puts every binding back to its fallback, silently. True if any changed.
    bool restore_defaults();
    bool is_default() const;

    // The row under the focus; the reset row is bindings().size().
    int focus() const
    {
        return focus_;
    }
    void set_focus(int index, bool snap = true);
    // True while a row waits for a button.
    bool listening() const
    {
        return listening_ >= 0;
    }
    // Seconds left before the listening row gives up.
    float listen_left() const;
    void stop_listening()
    {
        listening_ = -1;
    }
    // The binding the last Event::changed was about, or kResetId.
    int changed_id() const
    {
        return changed_id_;
    }
    Direction exit() const
    {
        return exit_;
    }

    // ---- the five rules ----
    void set_bounds(const gfx::Rect &bounds);
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    float preferred_height() const;
    void set_active(bool active)
    {
        active_ = active;
    }

    // activated: a row began to listen. changed: a binding changed (or the
    // defaults came back). cancelled: back, or the listening ended unanswered.
    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

    gfx::Rect row_rect(int index) const;

  private:
    int count() const; // rows, the reset row included
    gfx::Rect inner() const;
    float row_top(int index) const;
    void retarget(bool snap);
    void flash(int index);
    Event listen(const InputFrame &input, Feedback &feedback);

    std::vector<KeyBinding> rows_;
    std::vector<Pulse> flashes_; // one per row
    gfx::Rect bounds_{0.0f, 0.0f, 520.0f, 400.0f};
    int focus_ = 0;
    int listening_ = -1;
    float listen_time_ = 0.0f;
    int changed_id_ = 0;
    bool active_ = true;
    Direction exit_ = Direction::none;
    float clock_ = 0.0f;
    Highlight highlight_;
    Scroller scroll_;
    tween::Spring active_amount_{1.0f, 0.0f, 1.0f};
    tween::Spring listen_amount_;
    Pulse press_;
    Pulse shake_; // the listening row refused a button
};

} // namespace hui::ui
