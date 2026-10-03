// ps5-homebrew-ui - Components: Countdown, StorageBar and StatusBar.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The small status widgets a console screen keeps in a corner: how long is
// left, how full the drive is, what time it is, how the controller and the
// connection are doing and who is playing. Every glyph is drawn from shapes.

#pragma once

#include "ui/components/card.hpp"
#include "ui/components/chart.hpp"
#include "ui/components/data_common.hpp"
#include "ui/components/progress.hpp"

#include <array>
#include <string>
#include <vector>

namespace hui::ui
{

// ---- Countdown -------------------------------------------------------------

enum class CountdownShape : std::uint8_t
{
    text, // the time as m:ss
    ring, // a ring that drains, with the time inside
};

struct CountdownStyle : ComponentStyle
{
    CountdownShape shape = CountdownShape::text;
    // ---- geometry ----
    float text_size = 36.0f;             // the figure (text shape); the ring sizes its own
    float label_size = 17.0f;            // the caption
    float thickness = 7.0f;              // the ring's stroke
    gfx::Align align = gfx::Align::left; // of the text shape inside its bounds
    // ---- look ----
    Status status = Status::accent;           // the ring's and the figure's colour while calm
    gfx::Color color{0.0f, 0.0f, 0.0f, 0.0f}; // alpha > 0 overrides status
    bool tint_text = false;                   // the calm figure takes the status colour too
    bool on_panel = false;                    // which text colours to use
    // ---- behaviour ----
    float warning_at = 30.0f; // seconds left from which it shows the warning colour
    float danger_at = 10.0f;  // ... and from which the danger colour
    float pulse_at = 5.0f;    // the figure pulses once a second from here
    bool hours = false;       // always show hours (h:mm:ss); otherwise only when needed
};

// Time remaining. It counts down by itself between start() and zero; the
// colour warns, the last seconds pulse and tick, and finished() says when it
// is over.
//
//   ui::Countdown timer;
//   timer.label = "Next match";
//   timer.set_bounds({96, 96, 200, 56});
//   timer.start(90.0f);
//   ...
//   timer.update(dt, feedback);      // ticks the last seconds
//   if (timer.finished()) begin();
class Countdown
{
  public:
    CountdownStyle style;
    std::string label; // a caption: beside the figure (text) or under it (ring)

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    // Starts counting down from `seconds`.
    void start(float seconds);
    // Changes the time left without restarting (a server correction).
    void set_remaining(float seconds);
    void pause()
    {
        running_ = false;
    }
    void resume()
    {
        running_ = remaining_ > 0.0f;
    }
    bool running() const
    {
        return running_;
    }
    float remaining() const
    {
        return remaining_;
    }
    float total() const
    {
        return total_;
    }
    // It was started and has reached zero.
    bool finished() const
    {
        return started_ && remaining_ <= 0.0f;
    }
    // What a time left counts as: neutral (calm), warning or danger.
    Status zone() const;
    // The figure as it is shown: "1:05", "0:09", "1:02:00".
    std::string text() const;

    // Without feedback it is silent; with it, each of the last seconds
    // plays the step cue (rising) and zero the notify cue.
    void update(float dt);
    void update(float dt, Feedback &feedback);
    void draw(Canvas &canvas) const;

  private:
    void advance(float dt, Feedback *feedback);

    gfx::Rect bounds_{0.0f, 0.0f, 200.0f, 56.0f};
    float total_ = 0.0f;
    float remaining_ = 0.0f;
    bool running_ = false;
    bool started_ = false;
    SpringColor ink_;
    bool ink_set_ = false;
    Pulse beat_;   // once a second near the end
    Pulse finish_; // when it reaches zero
};

// ---- StorageBar ------------------------------------------------------------

struct StorageCategory
{
    std::string label;
    float value = 0.0f;                       // in the unit of the capacity
    gfx::Color color{0.0f, 0.0f, 0.0f, 0.0f}; // alpha 0: the theme's nth series colour
};

struct StorageBarStyle : ComponentStyle
{
    // ---- geometry ----
    float height = 16.0f;     // the bar
    float segment_gap = 3.0f; // between categories
    float focus_grow = 5.0f;  // the focused segment is this much taller
    float text_size = 20.0f;  // title and figures
    float legend_size = 17.0f;
    float spacing = 10.0f; // between the title line, the bar and the legend
    // ---- look ----
    bool header = true;        // the title and "used of capacity" on a line above the bar
    bool legend = true;        // the categories under the bar
    bool legend_values = true; // ... with their sizes
    bool show_free = true;     // "Free" as the legend's last entry
    bool on_panel = false;     // which text colours to use
    std::string unit = " GB";  // appended to sizes
    std::string free_label = "Free";
    int decimals = -1; // of sizes; negative: none for whole numbers, one otherwise
    // ---- behaviour ----
    EdgeExits exits; // edges that hand the focus back to the screen
};

// A drive's capacity, split by what fills it. Segments ease when the sizes
// change; left and right move a focus that lifts one category's segment and
// its legend entry.
//
//   ui::StorageBar storage;
//   storage.title = "Console storage";
//   storage.set_capacity(825.0f);
//   storage.set_categories({{"Games", 412}, {"Media", 96}, {"Saves", 12}});
//   storage.set_bounds({96, 300, 760, 84});
class StorageBar
{
  public:
    StorageBarStyle style;
    std::string title;

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    void set_capacity(float capacity);
    float capacity() const
    {
        return capacity_;
    }
    void set_categories(std::vector<StorageCategory> categories, bool snap = false);
    const std::vector<StorageCategory> &categories() const
    {
        return categories_;
    }
    float used() const;
    float free() const
    {
        return std::max(capacity_ - used(), 0.0f);
    }

    int focus() const
    {
        return focus_;
    }
    void set_focus(int category);
    void set_active(bool active)
    {
        active_ = active;
    }
    void enter();

    Event handle(const InputFrame &input, Feedback &feedback);
    Direction exit() const
    {
        return exit_;
    }
    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    void sync_legend();

    std::vector<StorageCategory> categories_;
    std::vector<tween::Bounce> values_;
    std::vector<tween::Spring> lifts_; // 0..1 per category: how focused
    Legend legend_;
    gfx::Rect bounds_{0.0f, 0.0f, 600.0f, 84.0f};
    float capacity_ = 100.0f;
    int focus_ = 0;
    bool active_ = false;
    float age_ = 10.0f;
    Direction exit_ = Direction::none;
    tween::Spring active_amount_;
    Pulse refusal_;
};

// ---- StatusBar -------------------------------------------------------------

struct PlayerSlot
{
    bool connected = false;
    gfx::Color color{0.0f, 0.0f, 0.0f, 0.0f}; // alpha 0: the theme's nth series colour
};

struct StatusBarStyle : ComponentStyle
{
    // ---- what it shows ----
    bool show_players = true;
    bool show_signal = true;
    bool show_battery = true;
    bool show_clock = true;
    bool battery_percent = false; // the level as a number beside the glyph
    // ---- geometry ----
    float padding = 20.0f;    // inside the strip, left and right
    float gap = 24.0f;        // between the groups
    float text_size = 22.0f;  // the title and the clock
    float glyph_size = 22.0f; // the height of the battery, bars and player marks
    int signal_bars = 4;
    // ---- look ----
    bool panel = true;        // a themed strip behind it
    float low_battery = 0.2f; // from here down the battery shows the danger colour
};

// The strip along the top of a screen: a title at one end and, at the other,
// who is playing, the connection, the controller's battery and the clock.
// It takes no input. The screen supplies the clock text: the kit does not
// know the time zone or the format the player chose.
//
//   ui::StatusBar bar;
//   bar.title = "Lantern Pass";
//   bar.clock = "21:47";
//   bar.set_battery(0.64f, false);
//   bar.set_signal(3);
//   bar.set_player(0, true);
//   bar.set_bounds({96, 60, 1728, 52});
class StatusBar
{
  public:
    static constexpr int kPlayers = 4;

    StatusBarStyle style;
    std::string title; // at the leading end
    std::string clock; // "21:47"

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    // level is 0..1.
    void set_battery(float level, bool charging);
    float battery() const
    {
        return battery_.target;
    }
    bool charging() const
    {
        return charging_;
    }
    // How many bars are lit, 0..style.signal_bars; 0 reads as "no connection".
    void set_signal(int bars);
    int signal() const
    {
        return signal_;
    }
    void set_player(int index, bool connected, gfx::Color color = {0.0f, 0.0f, 0.0f, 0.0f});
    const PlayerSlot &player(int index) const
    {
        return players_[static_cast<std::size_t>(std::clamp(index, 0, kPlayers - 1))];
    }

    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    gfx::Rect bounds_{0.0f, 0.0f, 900.0f, 52.0f};
    tween::Spring battery_{1.0f, 0.0f, 1.0f};
    bool charging_ = false;
    tween::Spring charge_; // 0..1: the bolt
    int signal_ = 0;
    tween::Spring signal_shown_;
    std::array<PlayerSlot, kPlayers> players_{};
    std::array<tween::Bounce, kPlayers> joined_{}; // 0..1 per slot
    float phase_ = 0.0f;
};

} // namespace hui::ui
