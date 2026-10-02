// ps5-homebrew-ui - Component: FocusGroup, spatial navigation for anything a screen draws.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A screen made of its own drawing (panels, tiles, buttons from ui::Painter)
// still needs one focus that the D-pad moves "the way it looks". FocusGroup
// is that: the screen registers every focusable thing as a rectangle with an
// id, and the group decides where up, down, left and right lead, owns the
// highlight that glides between the items, plays the cues and refuses softly
// at the edges. The screen writes no navigation code.
//
//   ui::FocusGroup focus;
//   focus.style.theme = theme;
//   focus.add({kPlay, play_rect});                  // id, rectangle
//   focus.add({kOptions, options_rect});
//   focus.set_focus(kPlay);
//   ...
//   focus.set_rect(kPlay, play_rect);               // every frame, if it moves
//   if (focus.handle(input, feedback) == ui::Event::activated) run(focus.focus());
//   focus.update(dt);
//   ...
//   draw_my_things();  focus.draw(canvas);          // the highlight, on top
//
// How a direction picks its item. Take a move to the right (the other three
// are the same, turned). An item is a candidate when it lies further right:
// its left edge is right of the focused item's left edge and its right edge
// is right of the focused item's right edge. Each candidate gets a cost, and
// the lowest wins (ties go to the item added first):
//
//   along   = max(0, candidate.left - focused.right)        the gap to cross
//   shared  = length the two share when projected on the vertical axis
//   overlap = clamp(shared / min(candidate.h, focused.h), 0, 1)
//   side    = max(0, -shared)                               how far it is off to the side
//
//   cost = along
//        + style.misalign * (1 - overlap)                    aligned items first
//        + style.side_weight * side                          then the nearest sideways
//        + (overlap > 0 ? 0 : style.beam_bias)               in the beam beats out of it
//
// So an item straight ahead wins over a nearer one that is off to the side,
// unless that one is `beam_bias` pixels nearer; among items straight ahead
// the nearest wins; among items off to the side the least far off wins.
// set_neighbour() overrides the answer for one item and one direction.
//
// Scopes. Every item carries a scope tag (0 unless you say otherwise): a
// region of the screen, a pane, a dialog. The group remembers the last item
// focused in each scope, and a move that lands in another scope goes to that
// item, so re-entering a region returns where you were. push_scope() keeps
// the focus inside one scope (a modal layer) until pop_scope() restores the
// focus that was there before. A layer's items are items like any others:
// add them when the layer opens and remove them when it closes (or keep them
// disabled in between), or the D-pad will find them under the screen.

#pragma once

#include "ui/components/card.hpp"
#include "ui/components/component.hpp"

#include <vector>

namespace hui::ui
{

// What a neighbour override can say besides an item's id.
constexpr int kNeighbourAuto = -1; // the geometry decides (the default)
constexpr int kNeighbourNone = -2; // nothing lies that way: the edge of the group

// One focusable thing.
struct FocusItem
{
    int id = 0;           // yours; 0 or above, unique in the group
    gfx::Rect rect;       // where it is on screen
    int scope = 0;        // the region it belongs to
    bool enabled = true;  // false: the focus passes over it
    float radius = -1.0f; // the highlight's corner on this item; negative: the style's
    // The item is a picture, not a themed surface. A theme whose focus ring
    // is drawn inside its controls (the bevelled desktop's dotted rectangle)
    // would lose it on artwork: the ring keeps clear of a picture instead.
    bool picture = false;
    // The highlight is cut to this while it sits on the item (an item inside
    // a ui::ScrollArea gives the area's bounds). No width: not clipped.
    gfx::Rect clip{};
    // Explicit neighbours: an item's id, kNeighbourAuto or kNeighbourNone.
    int up = kNeighbourAuto;
    int down = kNeighbourAuto;
    int left = kNeighbourAuto;
    int right = kNeighbourAuto;
};

struct FocusGroupStyle : ComponentStyle
{
    FocusGroupStyle()
    {
        highlight.kind = HighlightKind::ring;
    }

    // ---- look ----
    HighlightStyle highlight; // kind, colour, radius, thickness, grow, breathe
    float press = 3.0f;       // pixels the highlight squeezes in on confirm
    float inactive = 0.0f;    // the highlight's opacity while set_active(false)
    float clip_bleed = 12.0f; // room past an item's clip, so a ring is not cut
    // ---- behaviour ----
    bool wrap = false;           // past the last item in a direction comes the first
    bool remember = true;        // entering another scope returns to its last item
    EdgeExits exits;             // edges that hand the focus on instead of refusing
    bool pitch_by_height = true; // the move cue falls in pitch down the screen
    // ---- scoring (see the formula at the top of this file) ----
    float misalign = 60.0f;   // cost of an item that only partly lines up
    float side_weight = 2.0f; // cost of each pixel an item is off to the side
    float beam_bias = 480.0f; // head start of items straight ahead; huge: always first
};

class FocusGroup
{
  public:
    FocusGroupStyle style;

    // ---- items ----
    // Adds an item, or replaces the one with the same id. The first enabled
    // item added takes the focus.
    void add(const FocusItem &item);
    void remove(int id);
    // Forgets every item but not the focus: a screen that rebuilds its items
    // (clear, then add) keeps its place as long as the focused id comes back.
    void clear();
    // Call these as often as you like: items may move, scroll and animate.
    void set_rect(int id, const gfx::Rect &rect);
    void set_clip(int id, const gfx::Rect &clip);
    void set_enabled(int id, bool enabled);
    void set_radius(int id, float radius);
    // Overrides where `direction` leads from `id`: another item's id,
    // kNeighbourNone for "nowhere", kNeighbourAuto to give it back to geometry.
    void set_neighbour(int id, Direction direction, int target);
    const FocusItem *item(int id) const;
    int count() const
    {
        return static_cast<int>(items_.size());
    }

    // ---- focus ----
    // The focused item's id, or -1 when the group has none.
    int focus() const;
    // The focused item's scope (0 when there is none).
    int focus_scope() const;
    // Moves the focus without sound; snap skips the glide (use it on open).
    void set_focus(int id, bool snap = true);
    // The item a move in this direction would reach, or -1: what handle()
    // does, without doing it.
    int neighbour(Direction direction) const;
    // The group has the screen's focus. While it has not, the highlight fades
    // to style.inactive; simply do not call handle() then.
    void set_active(bool active)
    {
        active_ = active;
    }
    // 0..1: how much of this item the highlight covers right now. An item's
    // own focus amount (for its text colour, a lift, a glow) without a spring
    // per item.
    float focus_amount(int id) const;
    // Where the highlight is now, shake included.
    gfx::Rect highlight_rect(float time) const;

    // ---- scopes ----
    // Keeps the focus among the items of `scope` and moves it there: to
    // `focus_id`, or the item remembered for that scope, or its first one.
    void push_scope(int scope, int focus_id = -1, bool snap = false);
    // Lifts the restriction and returns the focus to where it was.
    void pop_scope(bool snap = false);
    int scope_depth() const
    {
        return static_cast<int>(layers_.size());
    }
    // Forgets the item remembered for a scope (its content changed).
    void forget(int scope);

    // ---- the five rules ----
    // moved: the focus went to another item. refused: there is none that way.
    // activated / cancelled: confirm and back, passed through for the screen.
    // none with exit() set: the move left through an edge in style.exits.
    Event handle(const InputFrame &input, Feedback &feedback);
    Direction exit() const
    {
        return exit_;
    }
    void update(float dt);
    // Draws the highlight. Kinds that fill (fill, tint, bar, glow) go under
    // the items' text, so call it between their surfaces and their words;
    // a ring can be drawn last.
    void draw(Canvas &canvas) const;

    // The cost of moving from one rectangle to another in a direction, as the
    // formula above gives it; negative when `to` is not a candidate.
    static float cost(const gfx::Rect &from, const gfx::Rect &to, Direction direction,
                      const FocusGroupStyle &style);

  private:
    struct Memory
    {
        int scope;
        int id;
    };
    struct Layer
    {
        int scope;
        int previous; // the focus when the scope was pushed
    };

    int find(int id) const;
    bool navigable(const FocusItem &item) const;
    int first_navigable(int scope, bool any_scope) const;
    int best_from(const gfx::Rect &from, Direction direction, int skip) const;
    int resolve(Direction direction, bool wrap) const;
    int remembered(int scope) const;
    void remember(const FocusItem &item);
    void focus_on(int id, bool snap);

    std::vector<FocusItem> items_;
    std::vector<Memory> memory_;
    std::vector<Layer> layers_;
    int focus_ = -1;
    bool active_ = true;
    Direction exit_ = Direction::none;

    // The highlight lives in a space that travels with the focused item, so
    // an item that scrolls or animates carries its highlight along instead of
    // being chased by it. origin_ is that space's offset on screen.
    Highlight highlight_;
    bool placed_ = false;
    int tracked_ = -1;
    gfx::Rect tracked_rect_{};
    float origin_x_ = 0.0f;
    float origin_y_ = 0.0f;
    tween::Spring active_amount_{1.0f, 0.0f, 1.0f};
    Pulse press_;
};

} // namespace hui::ui
