// ps5-homebrew-ui - Component: SplitView, two or three panes with dividers between them.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Master and detail: a list on one side, what it selects on the other. The
// split view owns the geometry and the emphasis, not the content: it tells
// the screen where each pane is (pane_rect), dims the panes that do not have
// the focus, draws the dividers and the optional panels, and animates the
// ratio when it changes. What is in a pane is the screen's.
//
//   ui::SplitView split;
//   split.style.ratio = 0.36f;                       // the first pane's share
//   split.set_bounds(area);
//   list.set_bounds(split.pane_rect(0));             // place your components
//   grid.set_bounds(split.pane_rect(1));
//   ...
//   // An edge exit of the focused pane crosses the split:
//   if (grid.handle(input, feedback) == ui::Event::none && grid.exit() != Direction::none)
//       split.handle_exit(grid.exit(), input, feedback);   // moved, or a soft refusal
//   split.update(dt);
//   ...
//   split.draw(canvas);                              // panels and dividers
//   split.begin_pane(canvas, 0);  list.draw(canvas);  split.end_pane(canvas, 0);
//   split.begin_pane(canvas, 1);  grid.draw(canvas);  split.end_pane(canvas, 1);
//
// With a ui::FocusGroup spanning both panes the group does the crossing; the
// screen then only says which pane the focus is in: split.set_focus(pane).

#pragma once

#include "ui/components/component.hpp"
#include "ui/components/surface.hpp"

namespace hui::ui
{

enum class SplitAxis : std::uint8_t
{
    horizontal, // panes side by side
    vertical,   // panes stacked
};

// Ratios with a name, for set_preset().
enum class SplitPreset : std::uint8_t
{
    collapsed, // the first pane is gone; the second has everything
    compact,   // a narrow first pane
    regular,   // style.regular
    balanced,  // half and half
    wide,      // the first pane has the larger part
};

// How the panes without the focus are dimmed.
enum class SplitDim : std::uint8_t
{
    // A veil where what is behind the pane is one plain colour, a fade over a
    // backdrop that is not (and in glass themes).
    automatic,
    // The pane's content is drawn at a lower opacity. Right over anything, but
    // shapes are faded one by one: a surface drawn over its own shadow lets
    // the shadow show through and turns grey.
    fade,
    // A wash of the colour behind the pane is drawn over it. Exact where that
    // colour is plain (it is what a group fade would give), wrong over a
    // backdrop with something in it.
    veil,
};

struct SplitStyle : ComponentStyle
{
    SplitAxis axis = SplitAxis::horizontal;
    // ---- geometry ----
    float ratio = 0.36f;   // the first pane's share of the room, 0..1; changes glide
    float third = 0.0f;    // a third pane's share; 0 for two panes
    float gap = 32.0f;     // between two panes; the divider runs down its middle
    float min_pane = 0.0f; // no pane gets smaller than this, unless it is collapsed
    float regular = 0.36f; // what SplitPreset::regular means
    float compact = 0.24f; // ... SplitPreset::compact
    float wide = 0.6f;     // ... SplitPreset::wide
    // ---- look ----
    bool panels = false; // a themed panel behind every pane
    PanelKind panel_kind = PanelKind::plain;
    float panel_padding = 20.0f; // between such a panel and pane_rect()
    bool divider = true;         // a line in the gap (not drawn between panels)
    DividerLine line = DividerLine::solid;
    float divider_inset = 0.0f; // the line stops this far short of the panes' ends
    float dim = 0.4f;           // 0..1: how far the panes without the focus fade
    SplitDim dim_mode = SplitDim::automatic;
    bool clip = true;      // begin_pane() clips to the pane while the ratio is moving
    bool on_panel = false; // the whole view lies on a themed surface
};

class SplitView
{
  public:
    SplitStyle style;

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }

    // ---- ratio ----
    // The same as writing style.ratio; snap skips the glide.
    void set_ratio(float ratio, bool snap = false);
    void set_preset(SplitPreset preset, bool snap = false);
    // The first pane's share right now, while it glides.
    float ratio() const;
    bool moving() const;
    // The first pane has (nearly) no room: do not send the focus there.
    bool collapsed() const;

    // ---- panes ----
    int pane_count() const
    {
        return style.third > 0.0f ? 3 : 2;
    }
    // Where a pane's content goes, now (inside its panel's padding).
    gfx::Rect pane_rect(int index) const;
    // The pane itself: what its panel covers.
    gfx::Rect pane_frame(int index) const;
    // Where the pane will be when the ratio has settled: lay out against this
    // to let a ui::SpringLayout do all the moving.
    gfx::Rect target_pane_rect(int index) const;
    // The gap after pane `index` (0, or 1 with three panes).
    gfx::Rect divider_rect(int index = 0) const;

    // ---- focus ----
    // The pane the screen's focus is in; the others dim. -1 says the focus is
    // somewhere outside the view (a toolbar above it): no pane dims then.
    int focus() const
    {
        return focus_;
    }
    void set_focus(int pane, bool snap = false);
    // 0..1: how present a pane is (the focused one is 1, a collapsing one
    // fades), whichever way it is dimmed.
    float pane_opacity(int index) const;
    // The pane a direction leads to from the focused one, or -1: the edge of
    // the view, a direction along the divider, or a collapsed pane.
    int pane_toward(Direction direction) const;
    // Moves the focus across the split if `direction` leads to a pane: true
    // and a move cue, else false and nothing.
    bool cross(Direction direction, Feedback &feedback);
    // The same for an edge exit forwarded by the focused pane's component:
    // moved when it crossed, a soft refusal when there is nothing that way.
    Event handle_exit(Direction direction, const InputFrame &input, Feedback &feedback);

    // ---- the five rules ----
    void update(float dt);
    // The panels and the dividers. Draw it before the panes' content.
    void draw(Canvas &canvas) const;
    // Wrap a pane's content: begin_pane() dims it if it is faded (and clips
    // it while the ratio moves), end_pane() lays the veil over it if it is
    // veiled.
    void begin_pane(Canvas &canvas, int index) const;
    void end_pane(Canvas &canvas, int index) const;

  private:
    struct Shares
    {
        float size[3];
        float gap[2];
    };
    Shares shares(float ratio, float third) const;
    gfx::Rect frame_for(const Shares &shares, int index) const;
    bool veils() const;
    float presence(int index) const;
    float emphasis(int index) const;
    gfx::Rect inner(const gfx::Rect &frame) const;

    gfx::Rect bounds_{0.0f, 0.0f, 1200.0f, 600.0f};
    int focus_ = 0;
    bool started_ = false;
    tween::Bounce ratio_{0.36f, 0.0f, 0.36f};
    tween::Spring third_;
    tween::Spring emphasis_[3] = {{1.0f, 0.0f, 1.0f}, {}, {}};
    Pulse refusal_;
    float clock_ = 0.0f;
};

} // namespace hui::ui
