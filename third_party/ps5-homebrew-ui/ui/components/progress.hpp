// ps5-homebrew-ui - Components: ProgressBar, ProgressRing, Spinner and Meter.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The indicators show state and take no input: they have a style, a value
// you set, update(dt) where the value eases with the theme's spring, and
// draw(). Nothing here jumps: a bar told "0.8" travels there.

#pragma once

#include "ui/components/component.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace hui::ui
{

// What a value means, for components that colour themselves by outcome.
enum class Status : std::uint8_t
{
    neutral, // the muted text colour
    primary, // the theme's main action colour
    accent,  // the theme's "on" colour: slider fills, selections
    success,
    warning,
    danger,
};

// The theme's colour for a status, and the text colour that reads on it.
gfx::Color status_color(const Theme &theme, Status status);
gfx::Color status_ink(const Theme &theme, Status status);
// The surface colour made opaque (a glass theme's is translucent): what a
// "cut out" rim or an unfilled glyph is painted with by default.
gfx::Color solid_surface(const Theme &theme);

// A band of light (clear, colour, clear) centred on cx and cut to [lo, hi]:
// a sheen crossing a bar, a shimmer crossing a placeholder. It is cut by
// arithmetic instead of a clip rectangle, so it costs no draw call.
void draw_sweep(gfx::DrawList &list, float cx, float half_width, float lo, float hi, float y,
                float height, gfx::Color color);

enum class ProgressMode : std::uint8_t
{
    determinate,   // one value, 0..1
    indeterminate, // "working": a segment that travels
    buffered,      // two values: played and loaded
    segmented,     // N steps that fill one after the other
};

enum class TrackStyle : std::uint8_t
{
    well, // the theme's sunken surface
    flat, // a faint plain fill
    none, // the fill alone
};

enum class RadiusSource : std::uint8_t
{
    theme,  // the theme's control radius
    pill,   // half the height
    square, // none
    custom, // style.radius
};

enum class LabelPlacement : std::uint8_t
{
    above,  // label left and percentage right, on a line over the bar
    right,  // after the bar, on its centre line
    inside, // centred in the bar, recoloured where the fill is under it
    none,
};

// ---- ProgressBar -----------------------------------------------------------

struct ProgressBarStyle : ComponentStyle
{
    ProgressMode mode = ProgressMode::determinate;
    // ---- geometry ----
    float height = 14.0f; // of the bar itself; the bounds also hold the text
    RadiusSource radius_source = RadiusSource::theme;
    float radius = 6.0f;      // used by RadiusSource::custom
    int segments = 5;         // ProgressMode::segmented
    float segment_gap = 6.0f; // between segments
    float text_size = 20.0f;  // label and percentage
    float text_gap = 10.0f;   // between the text and the bar
    // ---- look ----
    TrackStyle track = TrackStyle::well;
    Status status = Status::accent;           // the fill's colour
    gfx::Color color{0.0f, 0.0f, 0.0f, 0.0f}; // alpha > 0 overrides status
    float buffer_alpha = 0.35f;               // how strong the loaded part is
    bool sheen = false;                       // a band of light crossing the fill
    float sheen_period = 2.2f;                // seconds per crossing
    bool finish_flash = true;                 // a glow when the value reaches 1
    LabelPlacement placement = LabelPlacement::above;
    bool percent = true; // show the value as "64%"
    // ---- behaviour ----
    float travel_period = 1.6f; // indeterminate: seconds per crossing
    float travel_width = 0.32f; // ... and the segment's share of the track
};

// A horizontal bar.
//
//   ui::ProgressBar bar;
//   bar.style.theme = theme;
//   bar.label = "Downloading";
//   bar.set_bounds({96, 400, 480, 44});
//   bar.set_value(0.64f);     // eases there
//   bar.update(dt);
//   bar.draw(canvas);
class ProgressBar
{
  public:
    ProgressBarStyle style;
    std::string label;

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    // 0..1. snap skips the easing (use it when the bar appears).
    void set_value(float value, bool snap = false);
    void set_buffer(float value, bool snap = false);
    float value() const
    {
        return value_.target;
    }
    // The eased value: what is drawn right now.
    float shown() const;

    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    gfx::Rect bounds_{0.0f, 0.0f, 400.0f, 44.0f};
    tween::Bounce value_;
    tween::Bounce buffer_;
    Pulse flash_;
    float phase_ = 0.0f;
};

// ---- ProgressRing ----------------------------------------------------------

enum class RingCaps : std::uint8_t
{
    theme, // round where the theme is round, flat where it is square
    round,
    flat,
};

struct ProgressRingStyle : ComponentStyle
{
    ProgressMode mode = ProgressMode::determinate;
    // ---- geometry ----
    float thickness = 12.0f;
    float start_angle = 0.0f; // radians, clockwise from 12 o'clock
    float sweep = 6.2831853f; // the whole track; less than a turn gives a gauge
    int segments = 8;         // ProgressMode::segmented
    float segment_gap = 5.0f; // pixels between segments
    int ticks = 0;            // marks inside the ring; 0 for none
    RingCaps caps = RingCaps::theme;
    // ---- look ----
    TrackStyle track = TrackStyle::well;
    Status status = Status::accent;
    gfx::Color color{0.0f, 0.0f, 0.0f, 0.0f};    // alpha > 0 overrides status
    bool two_tone = false;                       // the leading half takes a second colour
    Status status_to = Status::primary;          // ... this one
    gfx::Color color_to{0.0f, 0.0f, 0.0f, 0.0f}; // alpha > 0 overrides status_to
    float buffer_alpha = 0.35f;
    // ---- centre ----
    bool percent = true;     // the value as a number in the middle
    float text_size = 0.0f;  // 0: sized from the ring
    float label_size = 0.0f; // 0: sized from the ring
    // ---- behaviour ----
    float spin_period = 1.4f; // indeterminate: seconds per turn
};

// A ring: the same modes as the bar, drawn with arcs.
class ProgressRing
{
  public:
    // inner is the square inside the ring; value is the eased value.
    using Slot = std::function<void(Canvas &canvas, const gfx::Rect &inner, float value)>;

    ProgressRingStyle style;
    std::string label; // a small line under the number
    Slot center;       // replaces the number and the label

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    void set_value(float value, bool snap = false);
    void set_buffer(float value, bool snap = false);
    float value() const
    {
        return value_.target;
    }
    float shown() const;

    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    gfx::Rect bounds_{0.0f, 0.0f, 120.0f, 120.0f};
    tween::Bounce value_;
    tween::Bounce buffer_;
    float phase_ = 0.0f;
};

// ---- Spinner ---------------------------------------------------------------

enum class SpinnerKind : std::uint8_t
{
    arc,   // a rotating arc whose length breathes
    dots,  // three dots bouncing in turn
    bars,  // equaliser bars
    orbit, // dots on a circle, fading in sequence
};

struct SpinnerStyle : ComponentStyle
{
    SpinnerKind kind = SpinnerKind::arc;
    float size = 0.0f;      // 0: the smaller side of the bounds
    float thickness = 0.0f; // arc only; 0: sized from the spinner
    float speed = 1.0f;     // multiplies every rate
    Status status = Status::accent;
    gfx::Color color{0.0f, 0.0f, 0.0f, 0.0f}; // alpha > 0 overrides status
    bool track = true;                        // arc only: a faint ring under it
};

// "Working, for an unknown time." With reduced motion it stands still and
// pulses slowly instead.
class Spinner
{
  public:
    SpinnerStyle style;

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    // A stopped spinner fades out and draws nothing.
    void set_spinning(bool spinning, bool snap = false);
    bool spinning() const
    {
        return spinning_;
    }

    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    gfx::Rect bounds_{0.0f, 0.0f, 48.0f, 48.0f};
    bool spinning_ = true;
    tween::Spring shown_{1.0f, 0.0f, 1.0f};
    float phase_ = 0.0f;
};

// ---- Meter -----------------------------------------------------------------

enum class MeterShape : std::uint8_t
{
    linear, // a bar
    radial, // a 270 degree arc with the value in the middle
};

struct MeterStyle : ComponentStyle
{
    MeterShape shape = MeterShape::linear;
    // ---- zones ----
    float warning_at = 0.6f; // from here the level is in the warning zone
    float danger_at = 0.85f; // ... and from here in the danger zone
    bool zone_strip = true;  // a thin scale beside the track in the zone colours
    // ---- geometry ----
    float height = 16.0f;    // linear: the bar
    float thickness = 14.0f; // radial: the arc
    int segments = 0;        // linear: more than 0 draws that many lamps
    float segment_gap = 4.0f;
    RadiusSource radius_source = RadiusSource::theme;
    float radius = 4.0f; // RadiusSource::custom
    TrackStyle track = TrackStyle::well;
    // ---- peak ----
    bool peak_hold = true;     // a marker that stays at the highest recent level
    float peak_seconds = 1.2f; // how long it stays
    float peak_fall = 0.6f;    // then how fast it falls, in levels per second
    // ---- text ----
    bool show_value = true;
    float value_scale = 100.0f; // the number shown is level * value_scale
    std::string unit = "%";
    float text_size = 20.0f; // linear: label and value; radial: the label
    float value_size = 0.0f; // radial: the number; 0: sized from the gauge
    float text_gap = 10.0f;
};

// A level with an opinion: green, amber, red.
class Meter
{
  public:
    MeterStyle style;
    std::string label;

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    void set_value(float value, bool snap = false);
    float value() const
    {
        return value_.target;
    }
    float shown() const;
    float peak() const
    {
        return peak_;
    }
    // Which zone a level is in: success, warning or danger.
    Status zone(float level) const;

    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    void draw_linear(Canvas &canvas, Painter &paint) const;
    void draw_radial(Canvas &canvas, Painter &paint) const;

    gfx::Rect bounds_{0.0f, 0.0f, 300.0f, 48.0f};
    tween::Bounce value_;
    SpringColor ink_;
    bool ink_set_ = false;
    float peak_ = 0.0f;
    float peak_age_ = 0.0f;
};

} // namespace hui::ui
