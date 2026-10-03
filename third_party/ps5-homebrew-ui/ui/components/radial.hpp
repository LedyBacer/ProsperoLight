// ps5-homebrew-ui - Component: RadialMenu, a ring of wedges chosen by pointing the stick.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/component.hpp"

#include <functional>
#include <string>
#include <vector>

namespace hui::ui
{

struct RadialItem
{
    std::string label;
    std::string description{};     // one or two lines in the hub
    std::string value{};           // a short note under it ("3 left", "Hold")
    gfx::Color accent{0, 0, 0, 0}; // the pointed wedge's colour; alpha 0: the theme's primary
    bool disabled = false;         // can be pointed at, but refuses confirm
    int tag = 0;                   // yours
};

enum class RadialMode : std::uint8_t
{
    menu,  // opens and stays until confirm or back
    quick, // a "weapon wheel": up while a button is held, releasing it activates
};

enum class RadialLabels : std::uint8_t
{
    outside, // beyond the wedge, growing away from the wheel
    inside,  // in the wedge, under its icon
    none,    // icons only; the hub names the pointed wedge
};

struct RadialStyle : ComponentStyle
{
    // ---- geometry ----
    float radius = 300.0f;     // outer edge of a resting wedge
    float thickness = 110.0f;  // of the ring
    float gap = 8.0f;          // pixels between two wedges, at the middle of the ring
    float start_angle = 0.0f;  // centre of the first wedge: radians, clockwise from 12 o'clock
    float push = 16.0f;        // how far the pointed wedge springs out
    float hub_radius = 150.0f; // the centre plate; 0 for none
    float icon_size = 46.0f;   // side of the square the `icon` slot draws in
    // ---- type ----
    RadialLabels labels = RadialLabels::outside;
    float label_size = 23.0f;
    float label_gap = 50.0f;        // outside labels: distance beyond the ring
    float title_size = 30.0f;       // the hub's title
    float description_size = 20.0f; // ... and its lines
    // ---- look ----
    // The veil over everything inside the bounds, 0..1. Its colour is the
    // theme's page colour unless scrim_color has an alpha: a light page gets a
    // light veil, so labels and the screen's own hints keep the contrast the
    // theme was designed with. Over a game world, set a dark scrim_color.
    float scrim = 0.9f;
    gfx::Color scrim_color{0.0f, 0.0f, 0.0f, 0.0f};
    bool frosted = true; // glass themes: blur the screen behind the veil and the hub
    bool rim = true;     // a bracket in the focus colour that glides round the ring
    bool needle = true;  // an arrow in the channel that follows the stick exactly
    float light = -1.0f; // 0..1: coloured light around the pointed wedge; negative: by theme
    // ---- behaviour ----
    RadialMode mode = RadialMode::menu;
    bool close_on_activate = true; // menu mode: confirm closes the wheel
    float stick_engage = 0.35f;    // deflection at which the stick starts to aim ...
    float stick_release = 0.22f;   // ... and below which it lets go
    float hysteresis = 0.1f;       // radians past a boundary before the focus leaves a wedge
    float entrance_step = 0.028f;  // seconds between wedges fanning out; 0 for none
    bool pitch_by_position = true;
};

// A ring of 2 to 12 wedges around a hub. The left stick's angle picks the
// wedge (with hysteresis, so a thumb resting on a boundary never flickers);
// the D-pad steps round the same ring. The pointed wedge grows and lights and
// the hub describes it. It is an overlay: draw it last, in the overlay list.
//
//   ui::RadialMenu wheel;
//   wheel.style.theme = theme;
//   wheel.set_items({{"Map", "The charted valley"}, {"Camera"}, {"Rope"}, {"Flare"}});
//   wheel.set_bounds({0, 0, 1920, 1080});          // what the scrim covers
//   ...
//   if (input.is_pressed(Action::north)) wheel.open(feedback);
//   if (wheel.is_open() && wheel.handle(input, feedback) == ui::Event::activated)
//       use(wheel.items()[wheel.choice()].tag);
//   wheel.update(dt);
//   wheel.draw(canvas);                             // draws nothing when closed
//
// Quick mode: open it when the button goes down, then every frame
//   wheel.set_held(input.is_held(Action::north));
// before handle(); releasing the button activates the pointed wedge.
class RadialMenu
{
  public:
    // box is a square of style.icon_size centred in the wedge; focus is 0..1;
    // ink is the colour that reads on the wedge right now.
    using Icon = std::function<void(Canvas &canvas, const gfx::Rect &box, const RadialItem &item,
                                    int index, float focus, gfx::Color ink)>;
    // area is the square inside the hub; opacity fades while the focus changes.
    using Hub = std::function<void(Canvas &canvas, const gfx::Rect &area, const RadialItem &item,
                                   int index, float opacity)>;

    RadialStyle style;
    Icon icon; // draws a wedge's symbol
    Hub hub;   // replaces the title, description and value in the hub

    void set_items(std::vector<RadialItem> items);
    const std::vector<RadialItem> &items() const
    {
        return items_;
    }
    RadialItem &item(int index)
    {
        return items_[static_cast<std::size_t>(index)];
    }
    // The area the scrim covers; the wheel sits in its centre.
    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }

    // Opens with the open cue and replays the fan. The focus stays where it
    // was, so a wheel reopens on the last choice.
    void open(Feedback &feedback);
    // Closes with the close cue. handle() calls it on back.
    void close(Feedback &feedback);
    // Closes without a sound.
    void dismiss();
    // Open: it takes input. Visible: it is still drawn (it fades out closed).
    bool is_open() const
    {
        return open_;
    }
    bool visible() const
    {
        return open_ || fade_.value > 0.01f;
    }
    // Quick mode: whether the button that shows the wheel is still down.
    void set_held(bool held)
    {
        held_ = held;
    }

    int focus() const
    {
        return focus_;
    }
    // Points at a wedge without sound.
    void set_focus(int index);
    // The wedge of the last Event::activated, or -1.
    int choice() const
    {
        return choice_;
    }
    // Where a wedge's middle is on screen (for an effect that leaves from it).
    void wedge_center(int index, float *x, float *y) const;

    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    float step_angle() const;
    float wedge_angle(int index) const;
    int pick(float angle) const;
    int nearest(float angle) const;
    void point(int index, Feedback &feedback);
    Event activate(const InputFrame &input, Feedback &feedback, bool closes);
    void draw_wedge(Canvas &canvas, int index, gfx::Color on_scrim) const;
    void draw_hub(Canvas &canvas) const;
    void draw_hub_item(Canvas &canvas, const gfx::Rect &area, int index, float alpha,
                       float drop) const;

    std::vector<RadialItem> items_;
    std::vector<tween::Bounce> lifts_; // how far each wedge is pushed out, 0..1
    gfx::Rect bounds_{0.0f, 0.0f, gfx::kVirtualWidth, gfx::kVirtualHeight};
    int focus_ = 0;
    int choice_ = -1;
    bool open_ = false;
    bool held_ = false;
    bool engaged_ = false; // the stick is past the threshold and aims
    float stick_angle_ = 0.0f;
    float age_ = 10.0f;    // seconds since open(): drives the fan
    tween::Bounce pop_;    // 0 closed .. 1 open: the wheel's scale
    tween::Spring fade_;   // its opacity
    tween::Spring needle_; // angle: the stick's direction, or the pointed wedge
    tween::Spring rim_;    // angle of the gliding bracket
    int hub_shown_ = 0;    // the item the hub describes ...
    int hub_previous_ = 0; // ... and the one it is fading out
    tween::Timer hub_;
    Pulse flash_;
    Pulse refusal_;
};

} // namespace hui::ui
