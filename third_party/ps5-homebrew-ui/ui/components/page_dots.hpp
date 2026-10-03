// ps5-homebrew-ui - Component: PageDots, where the player is in paged content.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/component.hpp"

namespace hui::ui
{

enum class PageDotsKind : std::uint8_t
{
    dots,    // one dot per page; the active one is larger and coloured
    dashes,  // short dashes; the active one is a longer pill
    numbers, // "3 / 8", the current number rolling
};

struct PageDotsStyle : ComponentStyle
{
    // ---- geometry ----
    PageDotsKind kind = PageDotsKind::dots;
    float dot_size = 14.0f;      // a dot's diameter
    float idle_scale = 0.62f;    // dots that are not active are this share of it
    float dash_width = 20.0f;    // a resting dash
    float active_width = 44.0f;  // the active dash, and the active dot while it shows progress
    float dash_thickness = 6.0f; // of every dash
    float gap = 12.0f;           // between marks
    int max_visible = 9;         // with more pages the marks at the ends shrink away
    gfx::Align align = gfx::Align::center; // where the marks sit in the bounds
    // ---- type ----
    float text_size = 24.0f; // numbers
    // ---- look ----
    bool on_page = true;       // drawn straight on the page: use the page's text colours
    bool focus_ring = true;    // the theme's ring around the marks while focused
    bool show_progress = true; // the active mark fills as the page's time runs
    // ---- behaviour ----
    float auto_advance = 0.0f; // seconds a page stays before the next one; 0 for never
    bool wrap = true;          // past the last page comes the first
    bool pitch_by_position = true;
};

// The position in a set of pages, animated between positions. Give it the
// number of pages and the current one; slide the content with value(). It can
// turn the pages itself: on left and right through handle(), and after
// style.auto_advance seconds, with the time left shown inside the active mark.
//
//   ui::PageDots dots;
//   dots.style.theme = theme;
//   dots.style.kind = ui::PageDotsKind::dashes;
//   dots.style.auto_advance = 6.0f;
//   dots.set_count(5);
//   dots.set_bounds({400, 880, 900, 30});
//   ...
//   if (dots.handle(input, feedback) == ui::Event::changed) show(dots.page());
//   dots.update(dt);
//   if (dots.take_advanced()) show(dots.page());   // it turned the page itself
//   strip_x = -dots.value() * page_width;
//   dots.draw(canvas);
class PageDots
{
  public:
    PageDotsStyle style;

    void set_count(int count);
    int count() const
    {
        return count_;
    }
    int page() const
    {
        return page_;
    }
    // Goes to a page without sound and restarts its time. snap skips the glide.
    void set_page(int page, bool snap = false);
    // The page as an animated number: 1.4 is between pages 1 and 2.
    float value() const
    {
        return value_.value;
    }
    // How much of the page's time has run, 0..1.
    float progress() const
    {
        return progress_;
    }
    // For screens that keep the time themselves (a video, a download):
    // leave style.auto_advance at 0 and set the fill here.
    void set_progress(float progress)
    {
        progress_ = tween::clamp01(progress);
    }
    // Stops the auto-advance clock, for instance while the player is choosing.
    void set_paused(bool paused)
    {
        paused_ = paused;
    }
    // True once after update() turned the page by itself.
    bool take_advanced();
    void set_focused(bool focused)
    {
        focused_ = focused;
    }

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }

    // Left and right turn the page.
    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    void draw_marks(Canvas &canvas, Painter &paint, gfx::Color idle, gfx::Color active) const;
    void draw_numbers(Canvas &canvas, Painter &paint, gfx::Color strong, gfx::Color muted,
                      gfx::Color active) const;

    gfx::Rect bounds_{0.0f, 0.0f, 400.0f, 30.0f};
    int count_ = 0;
    int page_ = 0;
    bool focused_ = false;
    bool paused_ = false;
    bool advanced_ = false;
    float progress_ = 0.0f;
    tween::Bounce value_;
    tween::Spring focus_amount_;
    Pulse refusal_;
};

} // namespace hui::ui
