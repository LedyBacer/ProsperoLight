// ps5-homebrew-ui - Components: PushButton and IconButton, the things a player presses.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Painter::button draws a button; these own one: its focus, its press, its
// loading and disabled states, its sound. `ui::Button` already names the
// controller glyphs (ui/glyphs.hpp), so the component is PushButton.
//
// The face helpers at the top are what every component of the actions group
// builds its buttons from: the theme's own button body (a hard shadow it
// presses into, a bevel that swaps, a pen outline) with the content placed by
// the caller, so one button can hold an icon, a spinner, a label and a glyph.

#pragma once

#include "ui/components/badge.hpp"
#include "ui/components/component.hpp"
#include "ui/components/progress.hpp"
#include "ui/components/tooltip.hpp"
#include "ui/glyphs.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace hui::ui
{

// What a button is for. The first three are the painter's kinds; danger is
// the primary construction in the theme's danger colour.
enum class ButtonRole : std::uint8_t
{
    primary,
    secondary,
    ghost,
    danger,
};

enum class ButtonSize : std::uint8_t
{
    small,
    medium,
    large,
};

// The numbers behind a size. A style's own height, text_size and padding
// override them when they are above zero.
struct ButtonMetrics
{
    float height = 64.0f;
    float text_size = 24.0f;
    float padding = 26.0f; // left and right of the content
};
ButtonMetrics button_metrics(ButtonSize size);
ButtonMetrics button_metrics(ButtonSize size, float height, float text_size, float padding);

// What draw_button_face leaves for the caller to fill in.
struct ButtonFace
{
    gfx::Rect content; // where the content sits (some themes move it when pressed)
    gfx::Color ink;    // the colour of text and icons on it
    gfx::Color fill;   // what is under them, made opaque (for picking a glyph style)
    float radius = 0.0f;
};

// The body of a button as the theme builds it, without a label, and the focus
// ring at look.focus. radius < 0 uses the theme's control radius. on_page
// says a ghost button sits on the page and not on a panel.
ButtonFace draw_button_face(Canvas &canvas, const Theme &theme, const gfx::Rect &r, ButtonRole role,
                            const Look &look, float radius = -1.0f, bool on_page = true);
// Only the focus ring of a button of that role, for components that draw one
// ring for several buttons. It differs from Painter::focus_ring in two
// places: a ghost button in a hard-shadow theme has no shadow to wrap, and
// the dotted ring of a bevel theme takes the label's colour on a filled button.
void draw_button_ring(Canvas &canvas, const Theme &theme, const gfx::Rect &r, ButtonRole role,
                      float amount, float radius = -1.0f, bool on_page = true);
// Only the colours and the content rectangle, nothing drawn.
ButtonFace button_face(const Theme &theme, const gfx::Rect &r, ButtonRole role, const Look &look,
                       float radius = -1.0f, bool on_page = true);
// The colours Painter::button gives a role, for components that build a body
// of their own shape (a joined toolbar): the raw fill and edge, the text
// colour and the stroke width.
struct ButtonPaint
{
    gfx::Color fill;
    gfx::Color edge;
    gfx::Color ink;
    float border = 0.0f;
};
ButtonPaint button_paint(const Theme &theme, ButtonRole role, bool on_page = true);
// A controller glyph style that reads on a button face: the DualSense colours
// on a dark cap, or one colour (the ink) when tinted is false.
GlyphStyle glyph_style_on(const ButtonFace &face, bool tinted);

// ---- PushButton ------------------------------------------------------------

enum class ButtonJustify : std::uint8_t
{
    center,  // icon, label and glyph together in the middle
    start,   // ... at the leading edge
    between, // icon and label at the leading edge, the glyph at the far one
};

struct PushButtonStyle : ComponentStyle
{
    ButtonRole role = ButtonRole::primary;
    ButtonSize size = ButtonSize::medium;
    // ---- geometry (0: from the size) ----
    float height = 0.0f;
    float text_size = 0.0f;
    float padding = 0.0f;
    float gap = 12.0f;                   // between icon, label and glyph
    float icon_size = 0.0f;              // the icon slot and the spinner; 0: from the text size
    float glyph_size = 0.0f;             // the trailing controller glyph; 0: from the text size
    float min_width = 0.0f;              // a hugging button is never narrower than this
    bool full_width = true;              // fills its bounds; false: as wide as its content
    gfx::Align align = gfx::Align::left; // where a hugging button sits in its bounds
    ButtonJustify justify = ButtonJustify::center;
    // ---- look ----
    bool on_page = true;      // a ghost button's text is drawn on the page, not a panel
    bool glyph_tinted = true; // the glyph keeps its DualSense colours; false: the ink
    SpinnerKind spinner = SpinnerKind::arc; // what turns while it is loading
    bool loading_hides_label = false;       // loading shows the spinner alone
    // ---- feel ----
    float press_scale = 0.03f; // how far a press shrinks it (0 for none)
    float focus_scale = 0.0f;  // how far the focus grows it (0 for none)
    float rumble = 0.3f;       // strength of the pulse on activation
};

// A button that owns its state.
//
//   ui::PushButton save;
//   save.style.theme = theme;
//   save.label = "Save";
//   save.glyph = ui::Button::cross;          // optional trailing controller glyph
//   save.set_bounds({96, 400, 320, 64});
//   save.set_active(true);                   // it has the screen's focus
//   ...
//   if (save.handle(input, feedback) == ui::Event::activated) { save.set_loading(true); write(); }
//   save.update(dt);
//   save.draw(canvas);
class PushButton
{
  public:
    PushButton();

    // box is the square reserved before the label; ink is the label's colour.
    using Icon =
        std::function<void(Canvas &canvas, const gfx::Rect &box, gfx::Color ink, float focus)>;

    PushButtonStyle style;
    std::string label;
    Button glyph = Button::none; // drawn after the label
    Icon icon;                   // drawn before the label

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    // Whether it has the screen's focus (the ring eases in and out).
    void set_active(bool active);
    bool active() const
    {
        return active_;
    }
    // Loading: a spinner takes the icon's place and confirm is refused.
    void set_loading(bool loading);
    bool loading() const
    {
        return loading_;
    }
    // Disabled: dimmed, focusable, refuses confirm.
    void set_disabled(bool disabled)
    {
        disabled_ = disabled;
    }
    bool disabled() const
    {
        return disabled_;
    }
    // Shows the press animation without input (a shortcut fired it).
    void press();

    // The width its content needs, and the rectangle it is drawn in.
    float preferred_width(const Fonts &fonts) const;
    gfx::Rect rect(const Fonts &fonts) const;

    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    gfx::Rect bounds_{0.0f, 0.0f, 320.0f, 64.0f};
    bool active_ = false;
    bool loading_ = false;
    bool disabled_ = false;
    bool armed_ = false; // confirm went down on this button and is still held
    bool held_ = false;
    tween::Spring focus_;
    tween::Spring down_; // follows a held confirm
    tween::Spring busy_; // 1 while loading
    Pulse press_;
    Pulse refusal_;
    Spinner spinner_;
};

// ---- IconButton ------------------------------------------------------------

enum class IconButtonShape : std::uint8_t
{
    square, // the theme's own corners
    round,  // a circle where the theme can draw one, its own corners elsewhere
};

struct IconButtonStyle : ComponentStyle
{
    ButtonRole role = ButtonRole::secondary;
    IconButtonShape shape = IconButtonShape::square;
    float size = 0.0f;        // 0: the smaller side of the bounds
    float icon_scale = 0.46f; // the icon box, as a share of the size
    bool on_page = true;      // see PushButtonStyle
    // ---- toggle ----
    bool toggle = false;                      // confirm flips it and returns Event::changed
    ButtonRole on_role = ButtonRole::primary; // its role while it is on
    bool on_pressed = true; // languages with real depth hold an "on" button pushed in
    // ---- badge ----
    Status badge_kind = Status::danger;
    float badge_height = 26.0f;
    float badge_text = 16.0f;
    int badge_max = 99;
    float badge_cutout = 0.0f; // a rim around the badge in the surface colour
    // ---- tooltip ----
    TooltipPlacement tip_placement = TooltipPlacement::above;
    float tip_delay = 0.35f; // seconds of focus before the label shows
    float tip_size = 20.0f;
    bool tip_inverted = true;
    // ---- feel ----
    float press_scale = 0.05f;
    float rumble = 0.3f;
};

// A button that is only an icon: a toolbar action, a favourite star, a mute
// switch. Its name lives in a tooltip that appears while it has the focus.
//
//   ui::IconButton mute;
//   mute.style.toggle = true;
//   mute.tip = "Mute";
//   mute.icon = [](ui::Canvas &c, const gfx::Rect &box, gfx::Color ink, float, float on) { ... };
//   mute.set_bounds({96, 400, 64, 64});
//   ...
//   if (mute.handle(input, feedback) == ui::Event::changed) set_muted(mute.on());
//   mute.update(dt);
//   mute.draw(canvas);
//   mute.draw_tooltip(canvas);   // after the things the label may cover
class IconButton
{
  public:
    // box is the icon's square; on is 0..1 for a toggle.
    using Icon = std::function<void(Canvas &canvas, const gfx::Rect &box, gfx::Color ink,
                                    float focus, float on)>;

    IconButtonStyle style;
    Icon icon;
    std::string tip; // the tooltip; empty for none

    void set_bounds(const gfx::Rect &bounds);
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    // The square the button is drawn in.
    gfx::Rect rect() const;
    // The area the tooltip must stay inside (the whole canvas by default).
    void set_tip_bounds(const gfx::Rect &bounds)
    {
        tooltip_.set_bounds(bounds);
    }
    void set_active(bool active);
    bool active() const
    {
        return active_;
    }
    void set_disabled(bool disabled)
    {
        disabled_ = disabled;
    }
    bool disabled() const
    {
        return disabled_;
    }
    // A count on its corner; 0 hides it.
    void set_badge(int count);
    int badge() const
    {
        return badge_.count();
    }
    // Toggle state. snap skips the animation.
    void set_on(bool on, bool snap = false);
    bool on() const
    {
        return on_;
    }

    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;
    void draw_tooltip(Canvas &canvas) const;

  private:
    void sync();

    gfx::Rect bounds_{0.0f, 0.0f, 64.0f, 64.0f};
    bool active_ = false;
    bool disabled_ = false;
    bool on_ = false;
    bool armed_ = false;
    bool held_ = false;
    tween::Spring focus_;
    tween::Spring down_;
    tween::Bounce on_amount_;
    Pulse press_;
    Pulse flip_; // the icon pops when a toggle flips
    Pulse refusal_;
    Badge badge_;
    Tooltip tooltip_;
};

} // namespace hui::ui
