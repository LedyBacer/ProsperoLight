// ps5-homebrew-ui - Component: Carousel, a horizontal shelf of cards in three modes.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/card.hpp"

#include <functional>
#include <string>
#include <vector>

namespace hui::ui
{

enum class CarouselMode : std::uint8_t
{
    leading,  // the focused item stays near the left edge and the row scrolls under it
    centered, // the focused item sits in the middle, larger than its neighbours
    paged,    // the row moves a page at a time; dots show where you are
};

struct CarouselStyle : ComponentStyle
{
    CardLook card; // how an item looks; card.focus_scale is how much the focused one grows
    CarouselMode mode = CarouselMode::leading;
    // ---- geometry ----
    float item_width = 280.0f;
    float item_height = 0.0f; // 0: what a card of that width needs
    float gap = 22.0f;
    // paged: items on a page. The item width is then worked out so a page
    // fills the shelf. 0: as many items of item_width as fit.
    int per_page = 0;
    // How much of what comes before stays in view. leading: the focused item
    // rests this far from the left edge once the row has scrolled. paged: the
    // neighbouring pages show this much at both ends.
    float peek = 56.0f;
    bool stop_at_end = true; // leading: the row stops when its last item reaches the right edge
    // ---- look ----
    float neighbour_fade = 0.0f; // 0..1: opacity lost per item of distance from the focus
    float edge_fade = 0.9f;      // items fade over this share of their width at the edges
    float title_size = 26.0f;    // the shelf's title, when it has one
    float title_gap = 16.0f;     // between the title and the row
    bool counter = true;         // "07 / 24" at the right end of the title line
    bool dots = true;            // paged: one dot per page under the row
    float dot_size = 8.0f;
    float dots_gap = 18.0f;      // between the row and the dots
    float entrance_step = 0.04f; // seconds between items arriving, outward from the focus
    // ---- behaviour ----
    // Past the last item comes the first. A centered shelf with enough items
    // to fill its width becomes a ring that turns on for ever; otherwise the
    // row runs back to its other end.
    bool wrap = false;
    EdgeExits exits; // left / right: hand the focus back instead of refusing
};

// A horizontal shelf of cards. The focus is a position that glides along the
// row; the row scrolls so the focus stays where the mode wants it.
//
//   ui::Carousel shelf;
//   shelf.style.theme = theme;
//   shelf.style.mode = ui::CarouselMode::centered;
//   shelf.title = "Continue playing";
//   shelf.set_items(items);                          // std::vector<ui::CardItem>
//   shelf.set_bounds({96, 280, 1728, shelf.preferred_height()});
//   ...
//   if (shelf.handle(input, feedback) == ui::Event::activated) open(shelf.focus());
//   shelf.update(dt);
//   shelf.draw(canvas);
class Carousel
{
  public:
    // item is the item's rectangle on screen, already scaled and lifted by
    // its focus; focus is 0..1.
    using Slot = std::function<void(Canvas &canvas, const gfx::Rect &item_rect,
                                    const CardItem &item, int index, float focus)>;

    CarouselStyle style;
    std::string title; // drawn above the row; empty for none
    Slot content;      // replaces the card: draw an item yourself (the focus ring stays)
    CardArt art;       // replaces only the artwork of the default card

    void set_items(std::vector<CardItem> items);
    const std::vector<CardItem> &items() const
    {
        return items_;
    }
    CardItem &item(int index)
    {
        return items_[static_cast<std::size_t>(index)];
    }
    // Call again after changing sizes or the mode in `style` to settle
    // without a glide.
    void set_bounds(const gfx::Rect &bounds);
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    // The height the shelf needs: title, the grown focused item, dots.
    float preferred_height() const;

    int focus() const
    {
        return focus_;
    }
    // Moves the focus without sound; snap skips the glide (use it on open).
    void set_focus(int index, bool snap = true);
    // An inactive shelf keeps a faint ring; do not call handle() meanwhile.
    void set_active(bool active)
    {
        active_ = active;
    }
    // Replays the entrance animation.
    void enter();

    // Up and down are never the shelf's: they return Event::none untouched.
    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

    // The end the focus asked to leave through in the last handle() (see
    // CarouselStyle::exits), or Direction::none. handle() returned Event::none.
    Direction exit() const
    {
        return exit_;
    }
    // paged: the page the focus is on, and how many there are (1 otherwise).
    int page() const;
    int pages() const;
    // Where an item is on screen right now (scroll applied, before its focus
    // scale).
    gfx::Rect item_rect(int index) const;

  private:
    struct Layout
    {
        float item_w = 0.0f;
        float item_h = 0.0f;
        float pitch = 0.0f; // item plus gap
        float push = 0.0f;  // how far the focused item's growth moves its neighbours
        float row_y = 0.0f;
        float span = 0.0f; // all items, at rest
        int per_page = 1;
        int pages = 1;
    };
    Layout layout() const;
    float scroll_target(const Layout &at) const;
    float content_x(const Layout &at, float index) const;
    bool endless(const Layout &at) const;
    int nearest_slot(int index) const;
    void retarget(bool snap);

    std::vector<CardItem> items_;
    gfx::Rect bounds_{0.0f, 0.0f, 1200.0f, 320.0f};
    int focus_ = 0;
    // The focus as a place on the row. It is focus_ except on a ring, where
    // it keeps counting past the ends: slot 25 of 24 items shows item 1.
    int slot_ = 0;
    bool active_ = true;
    float age_ = 10.0f;
    Direction exit_ = Direction::none;
    tween::Bounce position_; // the focus, in items: 2.4 is between the third and fourth
    tween::Spring scroll_;
    tween::Spring page_;
    SpringColor glow_;
    tween::Spring active_amount_{1.0f, 0.0f, 1.0f};
    Pulse press_;
    Pulse refusal_;
};

} // namespace hui::ui
