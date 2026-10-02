// ps5-homebrew-ui - Component: ToastStack, timed notifications that never take the focus.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/overlay.hpp"

#include <string>
#include <vector>

namespace hui::ui
{

enum class ToastAnchor : std::uint8_t
{
    top_left,
    top_center,
    top_right,
    bottom_left,
    bottom_center,
    bottom_right,
};

struct ToastStyle : ComponentStyle
{
    // ---- geometry ----
    ToastAnchor anchor = ToastAnchor::top_right;
    float width = 520.0f;
    float margin = 48.0f;    // from the edges of the bounds
    float gap = 14.0f;       // between toasts
    float padding = 20.0f;   // inside a toast
    float icon_size = 40.0f; // 0 for no icon
    // ---- type ----
    float title_size = 24.0f;
    float body_size = 20.0f;
    int body_lines = 2; // the body wraps to this many lines; 0 hides bodies
    // ---- look ----
    bool progress = true;         // a thin bar showing the time left
    float progress_height = 4.0f; // its thickness
    bool frosted = false;         // the blurred screen behind (needs canvas.glass)
    float frost = 0.6f;           // how much surface colour covers the frost
    // ---- behaviour ----
    int max_visible = 3;       // more than this wait their turn
    float duration = 4.0f;     // seconds on screen when push() names none
    bool pitch_by_kind = true; // success chimes a little higher, danger lower
};

// A stack of notifications in a corner. They slide in, wait, slide out, and
// the ones behind close the gap with a spring. They take no input: there is
// no handle(). update() needs the feedback because a toast sounds when it
// appears, which for a queued one is later than push().
//
//   ui::ToastStack toasts;
//   toasts.style.theme = theme;
//   toasts.push(ui::StatusKind::success, "Saved", "Slot 2, Lantern Pass");
//   ...
//   toasts.update(dt, feedback);
//   toasts.draw(canvas); // after everything else
class ToastStack
{
  public:
    ToastStyle style;

    // The screen the stack is anchored in: the whole canvas by default.
    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }

    // Queues a toast and returns its id. seconds 0 uses style.duration; a
    // negative value keeps it until dismiss().
    int push(StatusKind kind, std::string title, std::string body = {}, float seconds = 0.0f);
    // Sends one toast away now (or drops it from the queue).
    void dismiss(int id);
    // Sends every toast away and empties the queue; `now` skips the exits.
    void clear(bool now = false);

    // Toasts on screen and not leaving, and toasts still waiting.
    int visible_count() const;
    int queued_count() const
    {
        return static_cast<int>(queue_.size());
    }
    bool empty() const
    {
        return shown_.empty() && queue_.empty();
    }
    // Time left of a toast on screen, 0..1; -1 when it is not on screen.
    float remaining(int id) const;

    void update(float dt, Feedback &feedback);
    void draw(Canvas &canvas) const;

  private:
    struct Toast
    {
        int id = 0;
        StatusKind kind = StatusKind::info;
        std::string title;
        std::string body;
        float seconds = 0.0f; // negative: stays
        float left = 0.0f;
        bool leaving = false;
        tween::Bounce enter;                  // 0 off screen .. 1 in place
        tween::Spring leave;                  // 0 in place .. 1 gone
        tween::Bounce slot{1.0f, 0.0f, 1.0f}; // share of its room it still takes
    };

    float height(const Canvas &canvas, const Toast &toast) const;
    void draw_toast(Canvas &canvas, const Toast &toast, const gfx::Rect &r) const;
    float anchor_x() const;

    gfx::Rect bounds_{0.0f, 0.0f, gfx::kVirtualWidth, gfx::kVirtualHeight};
    std::vector<Toast> shown_;
    std::vector<Toast> queue_;
    int next_id_ = 1;
};

} // namespace hui::ui
