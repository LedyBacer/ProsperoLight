// ps5-homebrew-ui - Component: NotificationBell, the status-bar indicator of unread notifications.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A bell on a button with a count on its corner. The count pops when it
// changes and the bell swings for a moment when something arrives, so a
// notification the player did not look at still leaves a trace. Confirm
// reports Event::activated: the screen opens its NotificationCenter.

#pragma once

#include "ui/components/badge.hpp"
#include "ui/components/button.hpp"
#include "ui/components/component.hpp"

#include <cstdint>

namespace hui::ui
{

// A bell drawn from shapes inside `box` (a square), in one colour. swing
// turns it about its top (radians, clockwise); waves (0..1) is the opacity of
// the two sound lines beside it.
void draw_bell(Canvas &canvas, const gfx::Rect &box, gfx::Color ink, float swing = 0.0f,
               float waves = 0.0f);

struct NotificationBellStyle : ComponentStyle
{
    // ---- the button ----
    ButtonRole role = ButtonRole::secondary;
    IconButtonShape shape = IconButtonShape::round;
    float size = 0.0f;        // 0: the smaller side of the bounds
    float icon_scale = 0.52f; // the bell, as a share of the size
    bool on_page = true;      // a ghost bell sits on the page, not on a panel
    // ---- the count ----
    Status badge_kind = Status::danger;
    float badge_height = 26.0f;
    float badge_text = 16.0f;
    int badge_max = 99;        // above it the badge shows "99+"
    float badge_cutout = 2.0f; // a rim around the badge in the surface colour
    // ---- the ring ----
    bool ring_on_increase = true; // set_count() with a higher count rings by itself
    float ring_seconds = 0.9f;    // how long the bell swings
    float swing = 0.42f;          // the widest angle, in radians
    float swing_hz = 3.4f;        // swings per second
    bool waves = true;            // sound lines beside the bell while it rings
    // ---- feel ----
    float press_scale = 0.05f; // how far a press shrinks it (0 for none)
    float rumble = 0.3f;       // strength of the pulse on activation
};

// The indicator.
//
//   ui::NotificationBell bell;
//   bell.style.theme = theme;
//   bell.set_bounds({1760, 60, 64, 64});
//   bell.set_count(center.unread());        // pops, and rings when it grew
//   bell.set_active(focus == Focus::bell);
//   ...
//   if (bell.active() && bell.handle(input, feedback) == ui::Event::activated) open_center();
//   bell.update(dt);
//   bell.draw(canvas);
class NotificationBell
{
  public:
    NotificationBellStyle style;

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    // The square the button is drawn in.
    gfx::Rect rect() const;

    // The unread count; 0 hides the badge.
    void set_count(int count);
    int count() const
    {
        return badge_.count();
    }
    // Swings the bell: something arrived.
    void ring();
    bool ringing() const
    {
        return ring_ < style.ring_seconds;
    }

    // Whether it has the screen's focus (the ring eases in and out).
    void set_active(bool active)
    {
        active_ = active;
    }
    bool active() const
    {
        return active_;
    }

    // Confirm returns Event::activated; everything else is the screen's.
    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    void sync();
    float radius() const;

    gfx::Rect bounds_{0.0f, 0.0f, 64.0f, 64.0f};
    bool active_ = false;
    float ring_ = 1000.0f; // seconds since the last ring
    tween::Spring focus_;
    Pulse press_;
    Badge badge_;
};

} // namespace hui::ui
