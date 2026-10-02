// ps5-homebrew-ui - Component: ScrollArea, scrolling for content of any kind.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ListView and GridView scroll their own rows. ScrollArea scrolls whatever a
// screen draws: the screen says how large the content is, draws it between
// begin() and end() in the content's own coordinates (0, 0 is its top-left
// corner), and asks the area to keep the focused thing in view.
//
//   ui::ScrollArea scroll;
//   scroll.set_bounds(view);                          // where it is on screen
//   scroll.set_content_size(view.w, 2400.0f);         // how large the content is
//   ...
//   scroll.handle(input, feedback);                   // the right stick scrolls
//   scroll.reveal(card_rect);                         // when the focus moves
//   scroll.update(dt);
//   ...
//   scroll.begin(canvas);                             // clip + transform
//   for (const gfx::Rect &card : cards)               // content coordinates
//   {
//       canvas.list.push_opacity(scroll.visibility(card));
//       draw_card(card);
//       canvas.list.pop_opacity();
//   }
//   scroll.end(canvas);
//   scroll.draw(canvas);                              // the thumbs
//
// to_screen() turns a content rectangle into where it is on screen now, which
// is what a ui::FocusGroup wants for the items inside the area.

#pragma once

#include "ui/components/component.hpp"

namespace hui::ui
{

enum class ScrollAxes : std::uint8_t
{
    vertical,
    horizontal,
    both,
};

struct ScrollStyle : ComponentStyle
{
    ScrollAxes axes = ScrollAxes::vertical;
    // ---- behaviour ----
    float margin = 28.0f;        // room reveal() keeps between its target and the view's edge
    float stick_speed = 1500.0f; // pixels a second at full deflection of the right stick
    bool stick = true;           // the right stick scrolls
    // ---- look ----
    float edge_fade = 0.8f;   // content fades over this share of its own size where the view
                              // cuts it (see visibility()); 0 turns it off
    float clip_bleed = 10.0f; // the clip is this much larger than the bounds (rings, shadows)
    bool thumb = true;        // a position marker, only on an axis that overflows
    float thumb_width = 5.0f;
    float thumb_gap = 9.0f;  // between the bounds and the thumb (it lies outside them)
    float thumb_min = 36.0f; // its smallest length
    float thumb_idle = 0.5f; // its opacity at rest; it brightens while the area moves
    bool on_panel = false;   // it lies on a themed surface, not on the page
};

class ScrollArea
{
  public:
    ScrollStyle style;

    void set_bounds(const gfx::Rect &bounds);
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    // The size of what is drawn between begin() and end(). An axis that does
    // not scroll (see style.axes) never moves, whatever its size.
    void set_content_size(float width, float height);
    float content_width() const
    {
        return content_w_;
    }
    float content_height() const
    {
        return content_h_;
    }

    // ---- position ----
    // Brings a rectangle of the content into view, with style.margin to
    // spare, moving the least distance needed. snap skips the glide.
    void reveal(const gfx::Rect &content_rect, bool snap = false);
    void scroll_to(float x, float y, bool snap = false);
    void scroll_by(float dx, float dy);
    float offset_x() const
    {
        return x_.value;
    }
    float offset_y() const
    {
        return y_.value;
    }
    // How far each axis can scroll (0 when the content fits).
    float max_x() const;
    float max_y() const;
    // 0..1 along each axis, for a position read-out.
    float progress_x() const;
    float progress_y() const;

    // ---- coordinates ----
    gfx::Rect to_screen(const gfx::Rect &content_rect) const;
    gfx::Rect to_content(const gfx::Rect &screen_rect) const;
    // The part of the content that is in view, in content coordinates.
    gfx::Rect visible() const;
    // 0..1: how visible a content rectangle is. 1 inside the view; it falls
    // to 0 as the rectangle leaves through an edge that has more content
    // behind it. Multiply your content's opacity by it for soft edges.
    float visibility(const gfx::Rect &content_rect) const;

    // ---- the five rules ----
    // The right stick scrolls: changed while it moves the content, refused
    // (once, softly) when it is pushed against an end. Everything else is
    // none: the area has no focus of its own.
    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    // Clips to the bounds and moves the origin to the content's top-left
    // corner; end() undoes both. Draw the content between them.
    void begin(Canvas &canvas) const;
    void end(Canvas &canvas) const;
    // The thumbs. Call it after end().
    void draw(Canvas &canvas) const;

  private:
    bool scrolls_x() const
    {
        return style.axes != ScrollAxes::vertical;
    }
    bool scrolls_y() const
    {
        return style.axes != ScrollAxes::horizontal;
    }
    void clamp_targets();

    gfx::Rect bounds_{0.0f, 0.0f, 600.0f, 400.0f};
    float content_w_ = 0.0f;
    float content_h_ = 0.0f;
    tween::Spring x_, y_;
    Pulse activity_;       // brightens the thumb while the area moves
    Pulse bump_;           // the thumb's answer to a push against an end
    bool against_ = false; // the stick is being held against an end
    float dt_ = 1.0f / 60.0f;
};

} // namespace hui::ui
