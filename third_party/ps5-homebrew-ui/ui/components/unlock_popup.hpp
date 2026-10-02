// ps5-homebrew-ui - Component: UnlockPopup, the achievement or reward pop-up.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/component.hpp"

#include <functional>
#include <string>
#include <vector>

namespace hui::ui
{

enum class UnlockTier : std::uint8_t
{
    bronze,
    silver,
    gold,
    special, // the rare one: the theme's primary colour unless you name another
};
constexpr int kUnlockTiers = 4;

enum class PopupAnchor : std::uint8_t
{
    top_left,
    top_center,
    top_right,
    bottom_left,
    bottom_center,
    bottom_right,
};

// One thing the player earned.
struct Unlock
{
    std::string title;
    std::string subtitle;
    UnlockTier tier = UnlockTier::bronze;
    int points = 0;       // 0 hides the points
    std::string kicker;   // the small line above the title; empty uses style.kicker
    bool icon = false;    // draw the `icon` slot instead of the medal
    float seconds = 0.0f; // time on screen; 0 uses style.hold
    int tag = 0;          // yours
};

struct UnlockPopupStyle : ComponentStyle
{
    // ---- geometry ----
    PopupAnchor anchor = PopupAnchor::top_right;
    float width = 560.0f;
    float height = 116.0f;
    float margin = 48.0f;     // from the edges of the bounds
    float padding = 20.0f;    // inside the plate
    float medal_size = 76.0f; // the medal, or the icon slot's square
    // ---- type ----
    float kicker_size = 16.0f;
    float title_size = 27.0f;
    float subtitle_size = 20.0f;
    float points_size = 26.0f;
    std::string kicker = "Achievement unlocked";
    // ---- look ----
    // The metals. They are content, not interface: a bronze medal is bronze
    // in every theme. Alpha 0 uses the theme's primary colour.
    gfx::Color tier_colors[kUnlockTiers] = {
        gfx::Color::rgb(0xc8733a),
        gfx::Color::rgb(0xb9c2cf),
        gfx::Color::rgb(0xf0bd3c),
        gfx::Color{0.0f, 0.0f, 0.0f, 0.0f},
    };
    bool frosted = true;      // the blurred screen behind (needs canvas.glass), else solid
    float frost = 0.6f;       // how much surface colour covers the frost
    bool tier_glow = true;    // light in the tier's colour around the plate
    bool shine = true;        // a band of light crosses the plate once
    float shine_delay = 0.4f; // seconds after it lands
    float shine_time = 0.75f; // seconds the band takes to cross
    bool sparks = true;       // a burst from the medal as it lands
    int spark_count = 14;
    // ---- behaviour ----
    float hold = 4.0f; // seconds on screen
    float gap = 0.3f;  // seconds between one pop-up leaving and the next arriving
    // Bronze and silver play sounds.notify, pitched by tier; gold and special
    // play these. audio::Cue::count falls back to sounds.notify.
    audio::Cue gold_cue = audio::Cue::complete;
    audio::Cue special_cue = audio::Cue::new_record;
};

// The pop-up that says "you earned this": it slides in from a corner, the
// medal lands with a burst of sparks, a shine crosses the plate, it holds and
// leaves. Several unlocks queue and show one after another. It never takes
// the focus: there is no handle(). update() needs the feedback because a
// queued pop-up sounds when it appears, which is later than push().
//
//   ui::UnlockPopup unlocks;
//   unlocks.style.theme = theme;
//   unlocks.push("First light", "Reach the summit before dawn", ui::UnlockTier::gold, 50);
//   ...
//   unlocks.update(dt, feedback);
//   unlocks.draw(canvas); // after everything else
class UnlockPopup
{
  public:
    // area is the square the medal would fill; shown is 0..1 as it lands.
    using Slot = std::function<void(Canvas &canvas, const gfx::Rect &area, const Unlock &unlock,
                                    float shown)>;

    UnlockPopupStyle style;
    Slot icon; // drawn for unlocks with `icon` set

    // The screen the pop-up is anchored in: the whole canvas by default.
    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }

    // Queues an unlock; it shows as soon as the ones before it are gone.
    void push(Unlock unlock);
    // The same for the common case.
    void push(std::string title, std::string subtitle, UnlockTier tier, int points = 0);
    // Sends the current one away and empties the queue; `now` skips the exit.
    void clear(bool now = false);
    // Something is on screen (arriving, holding or leaving).
    bool showing() const
    {
        return active_;
    }
    int queued() const
    {
        return static_cast<int>(queue_.size());
    }
    bool empty() const
    {
        return !active_ && queue_.empty();
    }
    // The unlock on screen; nullptr when there is none.
    const Unlock *current() const
    {
        return active_ ? &current_ : nullptr;
    }
    // The colour a tier is drawn in, in this style.
    gfx::Color tier_color(UnlockTier tier) const;
    // Where the plate rests.
    gfx::Rect rect() const;

    void update(float dt, Feedback &feedback);
    void draw(Canvas &canvas) const;

  private:
    void draw_medal(Canvas &canvas, float cx, float cy, float size, UnlockTier tier) const;

    gfx::Rect bounds_{0.0f, 0.0f, gfx::kVirtualWidth, gfx::kVirtualHeight};
    std::vector<Unlock> queue_;
    Unlock current_;
    bool active_ = false;
    bool leaving_ = false;
    float age_ = 0.0f;    // since the current one appeared
    float wait_ = 0.0f;   // before the next one may appear
    tween::Bounce enter_; // 0 off screen .. 1 in place
    tween::Spring leave_; // 0 in place .. 1 gone
    tween::Bounce medal_; // the medal's own landing
};

} // namespace hui::ui
