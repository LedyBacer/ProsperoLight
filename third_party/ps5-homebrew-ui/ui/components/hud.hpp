// ps5-homebrew-ui - Components: HUD pieces that sit over gameplay.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A HUD is read in a glance while the picture behind it changes every frame,
// so its pieces cannot count on the page colour of a theme. Each one carries
// its own backing (HudBacking): a soft dark under-glow with light ink, or a
// plate in the theme's surface with the theme's ink. Marks, fills and shapes
// still come from the theme, so a HUD belongs to the menus around it.
//
// HUD pieces take no input: there is no handle(). The game sets their values
// and they animate the change (a ghost of lost health, a tick that draws
// itself, a heading that turns the short way round).

#pragma once

#include "ui/components/progress.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace hui::ui
{

enum class HudBacking : std::uint8_t
{
    none,  // nothing behind the piece: text keeps a shadow
    glow,  // a soft dark light under the piece; ink is light whatever the theme
    plate, // a panel in the theme's surface; ink is the theme's text colour
};

// What every HUD piece shares.
struct HudStyle : ComponentStyle
{
    HudBacking backing = HudBacking::glow;
    float glow_spread = 34.0f; // how far the under-glow reaches past the piece
    // The under-glow, bare tracks and text shadows (glow and none).
    gfx::Color shade = gfx::Color::rgb(0x05080d, 0.62f);
    // Text and marks over the game; alpha 0: near white (plates use the theme's text).
    gfx::Color ink{0.0f, 0.0f, 0.0f, 0.0f};
    float plate_padding = 16.0f; // between a plate's edge and the piece
    bool frosted = true;         // plates show the blurred game when canvas.glass is set
    bool text_shadow = true;     // glow and none: a dark copy under every text
};

// ---- HealthBar --------------------------------------------------------------

enum class HealthKind : std::uint8_t
{
    continuous,
    segmented, // style.segments cells; the last lit one fills partly
};

struct HealthBarStyle : HudStyle
{
    HealthKind kind = HealthKind::continuous;
    int segments = 10;
    float segment_gap = 4.0f;
    float bar_height = 22.0f; // the bar sits at the bottom of the bounds
    float title_size = 18.0f;
    float value_size = 22.0f;
    bool show_value = true; // "72 / 100" at the right of the title line
    // ---- colours ----
    Status status = Status::success;        // the fill
    Status low_status = Status::danger;     // ... at or under `low`
    Status shield_status = Status::primary; // the shield strip
    // ---- behaviour ----
    float low = 0.25f;           // share of the maximum that counts as low health
    bool low_pulse = true;       // a low bar breathes and glows
    bool ghost = true;           // the part just lost stays for a moment, then drains
    float ghost_delay = 0.45f;   // seconds before it starts to drain
    float ghost_rate = 0.3f;     // how fast it drains, as a share of the theme's speed
    float shield_height = 0.38f; // the shield strip, as a share of the bar's height
    float shake = 8.0f;          // pixels the bar jolts on a hit; 0 for none
};

// Health that shows how hard a hit was: the fill drops at once and a pale
// ghost of the lost part drains after it.
//
//   ui::HealthBar health;
//   health.style.theme = theme;
//   health.title = "Warden";
//   health.set_bounds({96, 64, 420, 56});
//   health.set_value(72);          // from 100: the ghost shows the 28 lost
class HealthBar
{
  public:
    HealthBarStyle style;
    std::string title; // above the bar; empty for none

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    void set_max(float max);
    float max() const
    {
        return max_;
    }
    // snap shows the value at once: no ghost, no flash (use it at start-up).
    void set_value(float value, bool snap = false);
    float value() const
    {
        return value_;
    }
    void set_shield(float shield, bool snap = false);
    float shield() const
    {
        return shield_value_;
    }
    bool low() const;
    // What is drawn right now, as shares of the maximum.
    float shown() const
    {
        return fill_.value;
    }
    float ghost() const
    {
        return ghost_.value;
    }

    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    gfx::Rect bounds_{0.0f, 0.0f, 420.0f, 56.0f};
    float max_ = 100.0f;
    float value_ = 100.0f;
    float shield_value_ = 0.0f;
    float ghost_hold_ = 0.0f;
    tween::Spring fill_{1.0f, 0.0f, 1.0f};
    tween::Spring ghost_{1.0f, 0.0f, 1.0f};
    tween::Spring shield_;
    tween::Spring danger_; // 0 healthy .. 1 low: the fill's colour eases between the two
    Pulse hit_;
    Pulse heal_;
};

// ---- AmmoCounter ------------------------------------------------------------

struct AmmoStyle : HudStyle
{
    float digits_size = 60.0f;  // the rounds in the magazine
    float reserve_size = 26.0f; // "/ 120"
    float label_size = 16.0f;
    std::string label;                    // a small word above the reserve ("9 MM"); empty for none
    gfx::Align align = gfx::Align::right; // which side of the bounds the block hugs
    int min_digits = 2;                   // the count is padded to this many cells
    float low = 0.25f;                    // share of the capacity at or under which it warns
    bool ring = true;                     // a ring: magazine level, and the reload as it runs
    float ring_size = 64.0f;
    float ring_thickness = 7.0f;
    bool pips = false; // one small bar per round under the digits
    float pip_height = 10.0f;
};

// Rounds in the magazine over rounds in reserve, in fixed-width cells so the
// number never jitters while it counts down.
//
//   ui::AmmoCounter ammo;
//   ammo.set_capacity(30);
//   ammo.set_bounds({1500, 900, 320, 80});
//   ammo.set_ammo(29, 120);        // one fired: the digits kick
//   ammo.set_reload(0.4f);         // while reloading; negative when done
class AmmoCounter
{
  public:
    AmmoStyle style;

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    void set_capacity(int capacity);
    int capacity() const
    {
        return capacity_;
    }
    void set_ammo(int current, int reserve);
    int current() const
    {
        return current_;
    }
    int reserve() const
    {
        return reserve_;
    }
    // progress 0..1 while a reload runs; negative: not reloading.
    void set_reload(float progress);
    bool reloading() const
    {
        return reload_ >= 0.0f;
    }
    // The status the count is drawn in: neutral, warning (low) or danger (empty).
    Status level() const;

    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    gfx::Rect bounds_{0.0f, 0.0f, 320.0f, 80.0f};
    int capacity_ = 30;
    int current_ = 30;
    int reserve_ = 0;
    float reload_ = -1.0f;
    tween::Spring level_{1.0f, 0.0f, 1.0f};
    tween::Spring reloading_;
    Pulse kick_;
};

// ---- ObjectiveTracker -------------------------------------------------------

struct ObjectiveStyle : HudStyle
{
    float title_size = 17.0f;
    float text_size = 23.0f;
    float distance_size = 19.0f;
    float row_height = 38.0f;
    float check_size = 22.0f;
    int max_rows = 5;       // more than this wait their turn
    float linger = 1.6f;    // seconds a ticked objective stays; negative: it stays
    float slide = 36.0f;    // how far a new objective travels in
    float tick_time = 0.4f; // seconds the tick takes to draw itself
    Status done_status = Status::success;
    audio::Cue complete_cue = audio::Cue::mark;
    audio::Cue add_cue = audio::Cue::count; // silent unless you name one
};

// The current goals: a title and a checklist. A new objective slides in, a
// completed one ticks itself, is struck through and leaves; the rest close
// the gap.
//
//   ui::ObjectiveTracker goals;
//   goals.title = "The Lantern Pass";
//   const int id = goals.add("Reach the signal tower", "240 m");
//   ...
//   goals.complete(id, &feedback);
class ObjectiveTracker
{
  public:
    ObjectiveStyle style;
    std::string title;

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    // Returns the objective's id. With feedback, style.add_cue plays.
    int add(std::string text, std::string distance = {}, Feedback *feedback = nullptr);
    // Ticks it. False when the id is unknown or already done.
    bool complete(int id, Feedback *feedback = nullptr);
    void set_distance(int id, std::string distance);
    // `now` skips the exits.
    void clear(bool now = true);
    // Objectives listed (leaving ones excluded) and those still open.
    int count() const;
    int remaining() const;
    bool done(int id) const;

    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    struct Row
    {
        int id = 0;
        std::string text;
        std::string distance;
        bool done = false;
        bool leaving = false;
        bool shown = false;
        float age = 0.0f;   // since it became visible
        float tick = 0.0f;  // 0..1
        float left = 0.0f;  // of the linger
        tween::Spring slot; // share of a row it takes: 0 gone .. 1 in place
    };

    gfx::Rect bounds_{0.0f, 0.0f, 420.0f, 240.0f};
    std::vector<Row> rows_;
    int next_id_ = 1;
};

// ---- MinimapFrame -----------------------------------------------------------

enum class MinimapShape : std::uint8_t
{
    round,
    square,
};

// A point of interest, relative to the player.
struct MapPip
{
    float angle = 0.0f;    // radians clockwise from north
    float distance = 0.5f; // 0 centre .. 1 the frame's range; beyond sits on the rim
    gfx::Color color{0.0f, 0.0f, 0.0f, 0.0f}; // alpha 0: the theme's accent
    bool objective = false;                   // a diamond that pulses
    int tag = 0;                              // yours
};

struct MinimapStyle : HudStyle
{
    MinimapShape shape = MinimapShape::round;
    bool rotate = true; // the map turns under a fixed arrow; false: north stays up
    float rim = 4.0f;   // the frame's stroke
    float inset = 8.0f; // between the rim and the content
    float pip_size = 12.0f;
    float north_size = 26.0f;  // the "N" marker; 0 hides it
    float player_size = 22.0f; // the arrow in the middle; 0 hides it
    bool grid = true;          // range rings and a cross when there is no content slot
};

// The frame of a minimap: you draw the map (the `content` slot), it draws the
// rim, the north marker, the player and the pips, and turns with the heading.
//
//   ui::MinimapFrame map;
//   map.set_bounds({1600, 64, 220, 220});
//   map.set_pips({{0.6f, 0.4f}, {2.1f, 1.4f, {}, true}});
//   map.set_heading(player.yaw);
class MinimapFrame
{
  public:
    // inner is the square the map may fill; heading is the angle the map is
    // turned by (0 when style.rotate is off).
    using Slot = std::function<void(Canvas &canvas, const gfx::Rect &inner, float heading,
                                    MinimapShape shape)>;

    MinimapStyle style;
    Slot content;

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    // Radians clockwise from north. The frame turns the short way round.
    void set_heading(float radians, bool snap = false);
    float heading() const;
    void set_pips(std::vector<MapPip> pips)
    {
        pips_ = std::move(pips);
    }
    const std::vector<MapPip> &pips() const
    {
        return pips_;
    }
    // The square the frame occupies inside the bounds, and the map's inside it.
    gfx::Rect frame() const;
    gfx::Rect inner() const;
    // Where a pip is drawn right now.
    void pip_position(const MapPip &pip, float *x, float *y) const;

    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    gfx::Rect bounds_{0.0f, 0.0f, 220.0f, 220.0f};
    std::vector<MapPip> pips_;
    tween::Spring heading_; // unwrapped, so it never spins the long way
};

} // namespace hui::ui
