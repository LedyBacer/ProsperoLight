// ps5-homebrew-ui - Component: GridView, a scrolling grid of cards with one gliding focus.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/card.hpp"

#include <functional>
#include <vector>

namespace hui::ui
{

enum class GridWrap : std::uint8_t
{
    none, // every edge refuses (or exits, see GridStyle::exits)
    rows, // a row and a column each wrap onto themselves
    flow, // reading order: the end of a row continues on the next one
};

struct GridStyle : ComponentStyle
{
    CardLook card; // how a cell looks: art shape, text, focus treatment
    // ---- geometry ----
    int columns = 5;
    float cell_width = 0.0f;  // 0: the columns share the width
    float cell_height = 0.0f; // 0: what a card of that width needs
    float gap_x = 22.0f;
    float gap_y = 22.0f;
    // Room kept inside the bounds so the focused cell can grow and wear its
    // ring without being clipped. Negative: worked out from the card and theme.
    float padding = -1.0f;
    // ---- look ----
    bool scroll_thumb = true;     // shown only when the grid overflows
    float edge_fade = 0.8f;       // a row cut by the clip fades over this share of its height
    float entrance_step = 0.035f; // seconds between cells arriving (a diagonal wave); 0 for none
    // ---- behaviour ----
    GridWrap wrap = GridWrap::none;
    EdgeExits exits;                // edges that hand the focus back; an exit beats the wrap
    bool select_on_confirm = false; // confirm toggles the item's check and reports `changed`
    bool pitch_by_row = true;       // lower rows sound lower
};

// A grid of cards: one focus that glides between cells, rows that scroll on a
// spring and fade at the edges, soft refusals on all four sides.
//
//   ui::GridView grid;
//   grid.style.theme = theme;
//   grid.style.columns = 6;
//   grid.style.card.art_aspect = 2.0f / 3.0f;       // posters
//   grid.set_items(items);                           // std::vector<ui::CardItem>
//   grid.set_bounds({96, 300, 1728, 600});
//   ...
//   if (grid.handle(input, feedback) == ui::Event::activated) open(grid.focus());
//   grid.update(dt);
//   grid.draw(canvas);
class GridView
{
  public:
    // cell is the cell's rectangle on screen, already scaled and lifted by
    // its focus; focus is 0..1.
    using Slot = std::function<void(Canvas &canvas, const gfx::Rect &cell, const CardItem &item,
                                    int index, float focus)>;

    GridStyle style;
    Slot content; // replaces the card: draw a cell yourself (the focus ring stays)
    CardArt art;  // replaces only the artwork of the default card

    void set_items(std::vector<CardItem> items);
    const std::vector<CardItem> &items() const
    {
        return items_;
    }
    CardItem &item(int index)
    {
        return items_[static_cast<std::size_t>(index)];
    }
    // Call again after changing sizes in `style` to settle without a glide.
    void set_bounds(const gfx::Rect &bounds);
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }

    int focus() const
    {
        return focus_;
    }
    // Moves the focus without sound; snap skips the glide (use it on open).
    void set_focus(int index, bool snap = true);
    // An inactive grid keeps a faint ring; do not call handle() meanwhile.
    void set_active(bool active)
    {
        active_ = active;
    }
    // Replays the entrance animation.
    void enter();

    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

    // The edge the focus asked to leave through in the last handle() (see
    // GridStyle::exits), or Direction::none. handle() returned Event::none.
    Direction exit() const
    {
        return exit_;
    }
    int columns() const;
    int rows() const;
    // Where a cell is on screen right now (scroll applied, before its focus
    // scale).
    gfx::Rect cell_rect(int index) const;

  private:
    struct Layout
    {
        int columns = 1;
        int rows = 0;
        float cell_w = 0.0f;
        float cell_h = 0.0f;
        float pad_y = 0.0f;
        float pitch = 0.0f; // a row plus the gap under it
        int full_rows = 1;  // rows that fit in the view entirely
        gfx::Rect view;     // where the rows scroll
    };
    Layout layout() const;
    gfx::Rect content_rect(const Layout &at, int index) const;
    int step(const Layout &at, Direction direction, bool round) const;
    void retarget(bool snap);

    std::vector<CardItem> items_;
    std::vector<float> checks_;
    gfx::Rect bounds_{0.0f, 0.0f, 800.0f, 500.0f};
    int focus_ = 0;
    int column_ = 0; // the column the player chose; short rows do not overwrite it
    bool active_ = true;
    float age_ = 10.0f;
    Direction exit_ = Direction::none;
    Direction refused_ = Direction::none;
    Highlight highlight_; // in content space: scrolling must not make it lag
    int top_row_ = 0;     // the first row in view: the grid scrolls by whole rows
    tween::Spring scroll_;
    SpringColor glow_;
    tween::Spring active_amount_{1.0f, 0.0f, 1.0f};
    Pulse press_;
    Pulse refusal_;
};

} // namespace hui::ui
