// ps5-homebrew-ui - Component: ColorPicker, preset swatches or a hue / saturation / value field.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/card.hpp"
#include "ui/components/component.hpp"

#include <string>
#include <utility>
#include <vector>

namespace hui::ui
{

enum class ColorPickerKind : std::uint8_t
{
    swatches, // a grid of preset colours
    hsv,      // a saturation / value field over a hue strip
};

struct ColorPickerStyle : ComponentStyle
{
    ColorPickerKind kind = ColorPickerKind::swatches;
    // ---- swatches ----
    int columns = 6;
    float swatch_size = 44.0f; // capped by what the width allows; 0 fills the width
    float swatch_gap = 12.0f;
    float swatch_radius = -1.0f; // negative: the theme's control radius
    bool select_on_move = false; // moving the focus picks the colour at once
    // ---- hsv ----
    float sv_height = 150.0f;           // only for preferred_height(): the field fills the bounds
    float hue_height = 24.0f;           // the hue strip
    float hue_gap = 14.0f;              // between the field and the strip
    float cursor_size = 20.0f;          // the marker on the field
    float sv_step = 0.05f;              // one D-pad step of saturation or value (0..1)
    float hue_step = 10.0f;             // one D-pad step of hue, in degrees
    float stick_speed = 0.9f;           // share of the range a full stick tilt crosses per second
    int fast_after = 8;                 // held repeats before D-pad steps grow; 0 never
    int fast_factor = 3;                // steps taken at once from then on
    audio::Cue tick = audio::Cue::tick; // the stick moving the marker
    // ---- preview ----
    bool preview = true; // a swatch of the current colour beside the picker
    float preview_width = 150.0f;
    float preview_gap = 20.0f;
    bool hex = true; // "#3E9EFF" under the preview
    float hex_size = 22.0f;
    // ---- shared ----
    float title_size = 20.0f;
    float title_gap = 10.0f;
    bool focus_ring = true;
    bool on_page = false;       // title and hex text sit on the page, not on a panel
    EdgeExits exits;            // edges that hand the focus back instead of refusing
    bool pitch_by_value = true; // cues rise along the palette, the hue and the value
};

// Picks a colour, in one of two ways (style.kind):
//
//   swatches  a grid of presets. Directions move a gliding ring, confirm picks
//             the colour under it; the picked one carries a check.
//   hsv       a saturation / value field and a hue strip. The D-pad steps the
//             one that is active and confirm switches between them; the left
//             stick moves the marker smoothly, the right stick turns the hue.
//
// The colour is kept across both, so the kind can change at any time.
//
//   ui::ColorPicker accent;
//   accent.style.theme = theme;
//   accent.set_title("Accent colour");
//   accent.set_color(gfx::Color::rgb(0x3e9eff));
//   accent.set_bounds({96, 300, 560, 220});
//   ...
//   accent.set_active(focused);
//   if (focused && accent.handle(input, feedback) == ui::Event::changed) apply(accent.color());
//   accent.update(dt);
//   accent.draw(canvas);
class ColorPicker
{
  public:
    ColorPicker();

    ColorPickerStyle style;

    // h in degrees (0..360), s and v in 0..1.
    static gfx::Color from_hsv(float h, float s, float v);
    static void to_hsv(gfx::Color color, float *h, float *s, float *v);
    static std::string hex_of(gfx::Color color);

    void set_title(std::string title)
    {
        title_ = std::move(title);
    }
    // The presets of the swatch grid (there is a default set of eighteen).
    void set_palette(std::vector<gfx::Color> colors);
    const std::vector<gfx::Color> &palette() const
    {
        return palette_;
    }
    // Silent. A colour that is in the palette also becomes the picked swatch.
    void set_color(gfx::Color color);
    gfx::Color color() const
    {
        return color_;
    }
    std::string hex() const
    {
        return hex_of(color_);
    }
    // The picked swatch, or -1 when the colour is not one of the presets.
    int index() const
    {
        return index_;
    }
    void set_index(int index);
    float hue() const
    {
        return h_;
    }
    float saturation() const
    {
        return s_;
    }
    float value() const
    {
        return v_;
    }
    // hsv: 0 while the field is active, 1 for the hue strip.
    int zone() const
    {
        return zone_;
    }

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    float preferred_height() const;
    // The swatch under the focus ring.
    int focus() const
    {
        return focus_;
    }
    void set_focus(int index);
    void set_active(bool active)
    {
        active_ = active;
    }
    // A screen that makes the player "enter" the picker before the D-pad
    // edits it says so here: while not engaged the ring surrounds the whole
    // picker instead of one swatch or zone.
    void set_engaged(bool engaged)
    {
        engaged_ = engaged;
    }
    // The edge the last handle() left through (style.exits), or none.
    Direction exit() const
    {
        return exit_;
    }

    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    struct Layout
    {
        gfx::Rect area;    // the picker itself, relative to the bounds' corner
        gfx::Rect preview; // w 0 when there is none
        gfx::Rect sv;
        gfx::Rect hue;
        float cell = 0.0f;
        int cols = 1;
    };
    Layout layout() const;
    gfx::Rect swatch_rect(const Layout &at, int index) const;
    Event handle_swatches(const InputFrame &input, Feedback &feedback);
    Event handle_hsv(const InputFrame &input, Feedback &feedback);
    Event pick(int index, Feedback &feedback);
    void apply_hsv();
    float center_x() const;

    std::vector<gfx::Color> palette_;
    std::vector<tween::Spring> checks_; // how picked each swatch looks
    std::string title_;
    gfx::Rect bounds_{0.0f, 0.0f, 560.0f, 220.0f};
    gfx::Color color_;
    float h_ = 0.0f;
    float s_ = 0.0f;
    float v_ = 1.0f;
    int index_ = -1;
    int focus_ = 0;
    int zone_ = 0;
    int held_ = 0;
    bool active_ = false;
    bool engaged_ = true;
    bool placed_ = false;
    Direction exit_ = Direction::none;
    // The sticks, as the last handle() saw them; update() moves the colour.
    float stick_x_ = 0.0f;
    float stick_y_ = 0.0f;
    float stick_hue_ = 0.0f;
    bool stick_live_ = false;
    bool stick_changed_ = false;
    float travelled_ = 0.0f;
    Highlight highlight_; // the ring: a swatch, a zone or the whole picker
    tween::Spring active_amount_;
    tween::Spring shown_s_, shown_v_, shown_h_;
    SpringColor shown_color_;
    Pulse press_;
};

} // namespace hui::ui
