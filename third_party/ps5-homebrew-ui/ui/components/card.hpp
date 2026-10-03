// ps5-homebrew-ui - Component: Card, one piece of content as artwork and a few words.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A card is the unit the collection components (GridView, Carousel) repeat:
// artwork, a title, a subtitle and up to three marks on the art (a badge, a
// progress bar, a "selected" check). It has no state of its own: the owner
// says how focused and how pressed it is and the card draws that, so one card
// can be drawn alone, forty times in a grid, or by your own layout.

#pragma once

#include "ui/components/component.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace hui::ui
{

// What one card shows. Everything but the title is optional.
struct CardItem
{
    std::string title;
    std::string subtitle;
    std::string badge;             // a short pill in a corner of the art ("NEW", "4K")
    std::uint32_t texture = 0;     // the artwork; 0 draws the placeholder gradient
    gfx::Rect uv = gfx::kFullUv;   // gfx::kCanvasUv for textures you rendered into
    float image_aspect = 1.0f;     // width / height of the picture inside `uv`
    gfx::Color top{0, 0, 0, 0};    // placeholder gradient; alpha 0 uses the theme's well colour
    gfx::Color bottom{0, 0, 0, 0}; // ... its lower end; alpha 0 repeats `top`
    gfx::Color accent{0, 0, 0, 0}; // the colour of its glow; alpha 0 uses the theme's focus
    float progress = -1.0f;        // 0..1 draws a bar along the bottom of the art
    bool selected = false;         // draws the check
    bool disabled = false;         // dimmed; containers refuse confirm on it
    int tag = 0;                   // yours
};

enum class CardText : std::uint8_t
{
    below, // title and subtitle under the art
    over,  // on the art's lower edge, on a scrim
    none,  // artwork only
};

enum class CardCorner : std::uint8_t
{
    top_left,
    top_right,
    bottom_left,
    bottom_right,
};

// The knobs of a card. GridStyle and CarouselStyle carry one as `card`.
struct CardLook
{
    // ---- geometry ----
    float art_aspect = 1.0f;     // width / height of the art; 0 fills what the text leaves
    float radius = -1.0f;        // corners of the art; negative: the theme's card radius
    bool plate = false;          // a themed surface behind art and text
    float plate_padding = 10.0f; // between that surface and the art
    // ---- text ----
    CardText text = CardText::below;
    gfx::Align align = gfx::Align::left;
    float title_size = 24.0f;
    float subtitle_size = 20.0f; // 0 leaves the subtitle out (and its room)
    float text_gap = 12.0f;      // between the art and the title (CardText::below)
    float text_inset = 14.0f;    // from the art's edges to the text (CardText::over)
    // What CardText::over darkens the art with; the text takes whichever of
    // black and white reads on it.
    gfx::Color scrim = gfx::Color::rgb(0x000000, 0.78f);
    bool on_panel = false; // the card sits on a themed panel, not on the page
    // ---- marks ----
    float badge_size = 16.0f;
    CardCorner badge_corner = CardCorner::top_left;
    float progress_height = 6.0f;
    float check_size = 30.0f;
    // ---- focus ----
    float focus_scale = 1.06f;         // size at full focus; 1 keeps it still
    float lift = 6.0f;                 // pixels it rises at full focus
    float press_scale = 0.04f;         // how far a press pushes it back in
    bool shadow = true;                // a soft shadow grows under it (soft, flat and glass themes)
    bool ring = true;                  // the theme's focus indicator around it
    bool glow = false;                 // light in the item's accent colour around it
    gfx::Color glow_color{0, 0, 0, 0}; // alpha 0: the item's accent, else the theme's focus
    float dim = 0.0f;                  // 0..1: how far cards without the focus fade
};

// The animated state of one card, eased by its owner.
struct CardState
{
    float focus = 0.0f;     // 0..1
    float press = 0.0f;     // 0..1
    float selected = -1.0f; // 0..1 for an animated check; negative reads item.selected
    float emphasis = -1.0f; // 0..1: how much of focus_scale applies; negative: same as focus
    float text = 1.0f;      // opacity of the title and subtitle: a shelf fades the words of
                            // an item its edge cuts, since half a word is worse than none
    bool marks = true;      // draw the ring and the glow; false when the owner draws one
                            // gliding indicator for all its cards
};

// Draws the artwork of a card instead of its texture: art is where, radius
// the corner it should have.
using CardArt = std::function<void(Canvas &canvas, const gfx::Rect &art, float radius,
                                   const CardItem &item, float focus)>;

// ---- measuring ----
// The room the text takes under the art (0 unless CardText::below).
float card_text_height(const CardLook &look);
// How tall a card of this width is. Needs art_aspect > 0.
float card_height(const CardLook &look, float width);
// Where the artwork sits inside a card.
gfx::Rect card_art(const CardLook &look, const gfx::Rect &card);
// What the focus surrounds: the plate when there is one, the art otherwise.
gfx::Rect card_frame(const CardLook &look, const gfx::Rect &card);
// The corner of that frame in this theme.
float card_radius(const ComponentStyle &style, const CardLook &look, const gfx::Rect &frame);
// The scale and the rise of a card in a state (1 and 0 under reduced motion).
float card_scale(const ComponentStyle &style, const CardLook &look, const CardState &state);
float card_lift(const ComponentStyle &style, const CardLook &look, const CardState &state);
// How far the focus indicator reaches past the frame: the room a container
// keeps around its cells so a ring is never clipped.
float card_ring_reach(const Theme &theme);

// ---- drawing ----
// A card into `card`, scaled about its centre and lifted as its state says.
void draw_card(Canvas &canvas, const ComponentStyle &style, const CardLook &look,
               const gfx::Rect &card, const CardItem &item, const CardState &state,
               const CardArt &art = {});
// The two halves of the focus indicator, for owners that draw one for all
// their cards: the halo goes under the focused card, the ring over it.
// `frame` is card_frame() after the card's scale and lift.
void draw_card_halo(Canvas &canvas, const ComponentStyle &style, const CardLook &look,
                    const gfx::Rect &frame, gfx::Color glow, float amount);
void draw_card_ring(Canvas &canvas, const ComponentStyle &style, const CardLook &look,
                    const gfx::Rect &frame, float amount);
// `frame` as a card of this scale and lift shows it (scaled about `card`).
gfx::Rect card_placed(const gfx::Rect &card, const gfx::Rect &frame, float scale, float lift);

// Which edges of a collection hand the focus back to the screen instead of
// refusing. The component then reports the edge through exit().
struct EdgeExits
{
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;

    bool allows(Direction direction) const
    {
        return (direction == Direction::up && up) || (direction == Direction::down && down) ||
               (direction == Direction::left && left) || (direction == Direction::right && right);
    }
};

struct CardStyle : ComponentStyle, CardLook
{
};

// One card on its own: a "continue" tile, a featured item, a detail header.
//
//   ui::Card card;
//   card.style.theme = theme;
//   card.style.text = ui::CardText::over;
//   card.draw(canvas, {96, 300, 420, 236}, item, focus.value, press.value);
class Card
{
  public:
    CardStyle style;
    CardArt art; // draws the artwork yourself

    void draw(Canvas &canvas, const gfx::Rect &card, const CardItem &item, float focus,
              float press = 0.0f) const;
    // The same with every part of the state given.
    void draw(Canvas &canvas, const gfx::Rect &card, const CardItem &item,
              const CardState &state) const;
    float height_for(float width) const
    {
        return card_height(style, width);
    }
};

} // namespace hui::ui
