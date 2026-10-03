// ps5-homebrew-ui - Component: Sheet, a drawer that slides in from an edge.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/overlay.hpp"

#include <functional>
#include <string>
#include <utility>

namespace hui::ui
{

enum class SheetEdge : std::uint8_t
{
    left,
    right,
    bottom,
};

struct SheetStyle : ComponentStyle
{
    // ---- geometry ----
    SheetEdge edge = SheetEdge::right;
    float size = 560.0f;    // width of a side sheet, height of a bottom sheet
    float margin = 0.0f;    // above 0 the sheet floats this far from the edges, fully rounded
    float padding = 36.0f;  // between the panel's edge and the title and content
    float overhang = 96.0f; // margin 0: how far the panel continues off screen (see below)
    float footer = 0.0f;    // room left free at the bottom of the bounds, for a hint row:
                            // the panel stops above it, the scrim still covers it
    // ---- type ----
    float title_size = 34.0f;
    // ---- look ----
    bool handle = true;  // a short grab bar on the inner edge
    bool divider = true; // a hairline under the title
    bool frosted = true; // the blurred screen behind (needs canvas.glass), else solid
    float frost = 0.55f; // how much surface colour covers the frost
    float scrim = 0.5f;  // opacity of the veil over the screen
    gfx::Color scrim_color{0.0f, 0.0f, 0.0f, 1.0f};
    // ---- motion and behaviour ----
    float exit_speed = 1.6f; // leaving is this much quicker than arriving
    bool dismissable = true; // back closes it; false refuses softly
};

// A drawer over a scrim with a title and room for anything. The kit rounds
// all four corners of a shape or none, so an attached sheet (margin 0) is a
// whole themed panel pushed partly off screen: only its inner edge shows, and
// a spring that overshoots never opens a gap at the screen's edge.
//
//   ui::Sheet sheet;
//   sheet.style.theme = theme;
//   sheet.set_title("Queue");
//   sheet.content = [&](ui::Canvas &canvas, const gfx::Rect &, float) { list.draw(canvas); };
//   list.set_bounds(sheet.content_rect());
//   sheet.open(feedback);
//   ...
//   if (sheet.is_open() && sheet.handle(input, feedback) == ui::Event::none)
//       list.handle(input, feedback); // the sheet only wants back
//   sheet.update(dt);
//   sheet.draw(canvas);
class Sheet
{
  public:
    // area is where the content rests; the sheet has already applied the
    // slide and the fade to the draw list, and opacity is that fade (0..1).
    using Slot = std::function<void(Canvas &canvas, const gfx::Rect &area, float opacity)>;

    SheetStyle style;
    Slot content;

    void set_title(std::string title)
    {
        title_ = std::move(title);
    }
    const std::string &title() const
    {
        return title_;
    }
    // The screen the sheet is attached to: the whole canvas by default. The
    // panel never draws outside it.
    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }

    // Opens and plays sounds.open.
    void open(Feedback &feedback);
    // Closes and plays sounds.close. Does nothing when already closed.
    void close(Feedback &feedback);
    // Closes without a sound or an exit animation (a screen change).
    void dismiss();
    bool is_open() const
    {
        return open_;
    }
    // Still on screen: open, or animating out.
    bool visible() const;

    // Back closes the sheet (Event::cancelled). Everything else is yours:
    // forward the input to what lives inside when this returns Event::none.
    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

    // The part of the panel that is on screen once it has settled.
    gfx::Rect panel_rect() const;
    // Where the content slot draws: give it to the component inside.
    gfx::Rect content_rect() const;

  private:
    float header_height() const;
    // The bounds without the footer: where the panel may be.
    gfx::Rect room() const;

    std::string title_;
    gfx::Rect bounds_{0.0f, 0.0f, gfx::kVirtualWidth, gfx::kVirtualHeight};
    bool open_ = false;
    tween::Spring fade_;
    tween::Bounce slide_;
    Pulse refusal_;
};

} // namespace hui::ui
