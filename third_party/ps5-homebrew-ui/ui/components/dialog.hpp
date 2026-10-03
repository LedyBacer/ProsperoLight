// ps5-homebrew-ui - Component: Dialog, a modal question with one to three answers.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/overlay.hpp"

#include <string>
#include <vector>

namespace hui::ui
{

struct DialogButton
{
    std::string label;
    ButtonKind kind = ButtonKind::secondary;
    // Drawn filled in the theme's danger colour, and never where the focus
    // opens while the dialog has another button.
    bool destructive = false;
};

struct DialogContent
{
    StatusKind icon = StatusKind::none;
    std::string title;
    std::string body;                  // wrapped; '\n' starts a new line
    std::vector<DialogButton> buttons; // one to three; an empty list gets "OK"
    int default_button = 0;            // where the focus opens (see destructive)
};

enum class DialogButtons : std::uint8_t
{
    row,     // side by side
    stacked, // one under the other, each as wide as the dialog
};

enum class DialogAlign : std::uint8_t
{
    center,
    bottom, // near the bottom edge, like a sheet
};

struct DialogStyle : ComponentStyle
{
    // ---- geometry ----
    float width = 760.0f;
    float padding = 44.0f;       // between the panel's edge and its content
    float icon_size = 64.0f;     // 0 hides the icon whatever the content says
    float gap = 18.0f;           // between icon, title and body
    float button_height = 64.0f; // Painter::button sets its label at 24
    float button_gap = 16.0f;
    float button_width = 0.0f;   // row: 0 shares the width equally; stacked: 0 is full width
    float bottom_margin = 72.0f; // DialogAlign::bottom: distance from the bounds' lower edge
    // ---- type ----
    float title_size = 36.0f;
    float body_size = 25.0f;
    float body_line = 1.45f; // line height of the body, in body sizes
    int title_lines = 2;
    int body_lines = 6;
    // ---- look ----
    DialogButtons buttons = DialogButtons::row;
    DialogAlign align = DialogAlign::center;
    bool centered = true; // icon on top, text centred; false: icon leading, text left
    bool frosted = true;  // the blurred screen behind (needs canvas.glass), else solid
    float frost = 0.55f;  // how much surface colour covers the frost
    float scrim = 0.55f;  // opacity of the veil over the screen
    gfx::Color scrim_color{0.0f, 0.0f, 0.0f, 1.0f};
    // ---- motion ----
    float enter_scale = 0.9f; // the panel grows from this
    float enter_rise = 28.0f; // ... and rises this far
    float exit_speed = 1.8f;  // leaving is this much quicker than arriving
    // ---- behaviour ----
    bool close_on_activate = true; // confirm closes the dialog
    bool dismissable = true;       // back closes it; false refuses softly
};

// A modal: a scrim, a panel that springs in, a question and its answers.
// While it is open it should receive every input.
//
//   ui::Dialog dialog;
//   dialog.style.theme = theme;
//   dialog.open({ui::StatusKind::danger, "Delete the save?", "This cannot be undone.",
//                {{"Cancel"}, {"Delete", ui::ButtonKind::primary, true}}}, feedback);
//   ...
//   if (dialog.is_open())
//   {
//       const ui::Event event = dialog.handle(input, feedback);
//       if (event == ui::Event::activated && dialog.choice() == 1) erase();
//   }
//   dialog.update(dt);
//   dialog.draw(canvas); // last, so it is on top
class Dialog
{
  public:
    DialogStyle style;

    void set_content(DialogContent content);
    const DialogContent &content() const
    {
        return content_;
    }
    // The area the scrim covers and the panel is placed in: the whole canvas
    // unless you say otherwise.
    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }

    // Opens with the focus on the default button and plays sounds.open.
    void open(Feedback &feedback);
    void open(DialogContent content, Feedback &feedback);
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

    int focus() const
    {
        return focus_;
    }
    void set_focus(int index);
    // The button confirm was pressed on; -1 until then and after a cancel.
    int choice() const
    {
        return choice_;
    }

    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

    // Where the panel rests. Its height follows the text, so it needs fonts.
    gfx::Rect panel_rect(const Canvas &canvas) const;

  private:
    struct Layout
    {
        gfx::Rect panel;
        std::vector<std::string> title;
        std::vector<std::string> body;
        float icon_cx = 0.0f, icon_cy = 0.0f;
        float text_x = 0.0f;    // left edge, or the centre when centred
        float title_top = 0.0f; // relative to the panel
        float body_top = 0.0f;
        float buttons_top = 0.0f;
    };

    int count() const;
    bool has_icon() const;
    float inner_width() const;
    float panel_width() const;
    // A button's rectangle relative to the top-left of the button block.
    gfx::Rect button_rect(int index) const;
    float buttons_height() const;
    int first_focus() const;
    void retarget(bool snap);
    Layout layout(const Canvas &canvas) const;

    DialogContent content_;
    gfx::Rect bounds_{0.0f, 0.0f, gfx::kVirtualWidth, gfx::kVirtualHeight};
    bool open_ = false;
    int focus_ = 0;
    int choice_ = -1;
    tween::Spring fade_;
    tween::Bounce pop_;
    Highlight highlight_;
    Pulse press_;
};

} // namespace hui::ui
