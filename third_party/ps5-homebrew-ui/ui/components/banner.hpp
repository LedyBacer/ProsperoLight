// ps5-homebrew-ui - Component: Banner, a message that stays until it is answered.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A toast passes; a banner stays. It lies across the top of an area, says
// what is the matter and offers up to two actions and a way to dismiss it.
// It slides in and pushes the content under it down: ask it how far.

#pragma once

#include "ui/components/button.hpp"
#include "ui/components/card.hpp"
#include "ui/components/overlay.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace hui::ui
{

enum class BannerLook : std::uint8_t
{
    filled,   // the status colour, text in the colour that reads on it
    tinted,   // the theme's surface washed with the status colour
    outlined, // the theme's surface inside a stroke in the status colour
    accent,   // the theme's surface with a bar in the status colour at its leading edge
};

struct BannerStyle : ComponentStyle
{
    StatusKind kind = StatusKind::info; // which colour and icon
    BannerLook look = BannerLook::tinted;
    // ---- geometry ----
    float padding = 22.0f;   // between the panel's edge and its content
    float gap = 16.0f;       // between the icon, the text and the controls
    float icon_size = 40.0f; // 0 hides the icon
    float min_height = 84.0f;
    float radius = -1.0f;   // negative: the theme's card radius
    float bar_width = 6.0f; // BannerLook::accent
    float tint = 0.16f;     // BannerLook::tinted: how much status colour is in the surface
    // ---- type ----
    float title_size = 24.0f;
    float body_size = 21.0f;
    float body_line = 1.4f; // body line height, in body sizes
    int body_lines = 2;     // the body wraps to this many lines, then ends in "..."
    // ---- controls ----
    float button_height = 48.0f;
    float button_text = 20.0f;
    float button_padding = 20.0f; // left and right of a label
    float button_width = 0.0f;    // 0: as wide as the label needs
    float button_gap = 12.0f;
    ButtonRole action_role = ButtonRole::secondary; // the action buttons
    bool emphasize_first = true;                    // ... except the first, which is primary
    float close_size = 44.0f;                       // the dismiss control
    bool dismissable = true;                        // shows the dismiss control
    bool back_dismisses = true;                     // back hides a dismissable banner
    EdgeExits exits;                                // edges that hand the focus on
    // ---- motion ----
    float slide = 1.0f; // it arrives from this many of its own heights above
};

// A persistent inline message.
//
//   ui::Banner banner;
//   banner.style.kind = ui::StatusKind::warning;
//   banner.title = "Storage almost full";
//   banner.body = "2.1 GB left. Captures will stop saving when it runs out.";
//   banner.set_actions({"Manage", "Later"});
//   banner.set_bounds({96, 240, 1728, 0});      // its height is its own
//   banner.show(feedback);
//   ...
//   if (banner_has_focus)
//   {
//       const ui::Event event = banner.handle(input, feedback);
//       if (event == ui::Event::activated) run(banner.choice());
//   }
//   banner.update(dt);
//   const float top = area.y + banner.pushed(fonts);  // content starts under it
//   banner.draw(canvas);
class Banner
{
  public:
    BannerStyle style;
    std::string title;
    std::string body;

    // Up to two action buttons; more are dropped.
    void set_actions(std::vector<std::string> labels);
    const std::vector<std::string> &actions() const
    {
        return actions_;
    }
    // The top of the area it lies across: x, y and width. The height is ignored.
    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }

    // Slides in with the notify cue / out with the close cue.
    void show(Feedback &feedback);
    void hide(Feedback &feedback);
    // The same without sound; snap skips the slide.
    void set_shown(bool shown, bool snap = false);
    bool is_shown() const
    {
        return shown_;
    }
    // Something of it is still on screen.
    bool visible() const
    {
        return shown_ || amount_.value > 0.004f;
    }

    // Whether it has the screen's focus.
    void set_active(bool active)
    {
        active_ = active;
    }
    // The things the focus can rest on: the actions, then the dismiss control.
    int controls() const;
    int focus() const
    {
        return focus_;
    }
    void set_focus(int index);
    // The action the last Event::activated came from.
    int choice() const
    {
        return choice_;
    }
    Direction exit() const
    {
        return exit_;
    }

    // Its height at rest, and how far it pushes the content under it right
    // now (0 hidden .. its height, plus a hard shadow where the theme has one).
    float height(const Fonts &fonts) const;
    float pushed(const Fonts &fonts) const;
    // Where it rests, and where one of its controls is.
    gfx::Rect rect(const Fonts &fonts) const;
    gfx::Rect control_rect(const Fonts &fonts, int index) const;

    // Event::moved between controls, Event::activated on an action,
    // Event::cancelled when it was dismissed, Event::refused at an edge.
    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    struct Layout
    {
        gfx::Rect panel;      // on screen, at rest
        float icon_cx = 0.0f; // the rest is relative to the panel
        float text_x = 0.0f;
        float text_top = 0.0f;
        std::string title;
        std::vector<std::string> body;
        gfx::Rect controls[3];
        int count = 0;
        int actions = 0;
    };
    Layout layout(const Fonts &fonts) const;

    std::vector<std::string> actions_;
    gfx::Rect bounds_{96.0f, 96.0f, 1728.0f, 0.0f};
    bool shown_ = false;
    bool active_ = false;
    int focus_ = 0;
    int choice_ = -1;
    Direction exit_ = Direction::none;
    tween::Spring amount_; // 0 hidden .. 1 shown
    tween::Spring active_amount_;
    tween::Spring glide_; // the focus ring's place, as a control index
    Pulse press_;
    Pulse refusal_;
};

} // namespace hui::ui
