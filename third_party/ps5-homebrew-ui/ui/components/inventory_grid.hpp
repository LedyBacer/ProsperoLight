// ps5-homebrew-ui - Component: InventoryGrid, slots of items you lift and set down.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/card.hpp"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace hui::ui
{

struct InventoryItem
{
    int id = 0; // unique and not 0: every animation is keyed by it
    std::string name;
    int count = 1;
    int max_stack = 1; // above 1 the count is shown and like kinds may merge
    int kind = 0;      // what it is; the default merge rule stacks equal, non-zero kinds
    int category = 0;  // for set_filter()
    gfx::Color rarity{0.0f, 0.0f, 0.0f, 0.0f}; // the frame; alpha 0: the theme's muted ink
    gfx::Color tint{0.0f, 0.0f, 0.0f, 0.0f};   // the placeholder icon; alpha 0: from the rarity
    std::uint32_t texture = 0;                 // the icon; 0 draws the placeholder
    gfx::Rect uv = gfx::kFullUv;
    int tag = 0; // yours
};

enum class InventoryAction : std::uint8_t
{
    none,
    picked,   // lifted out of `from`
    placed,   // set down in an empty slot `to`
    swapped,  // set down in `to`; the item that was there (`other`) went to `from`
    merged,   // `amount` units went into `other`; the hand may still hold the rest
    returned, // back was pressed: the item went back to `from`
};

// What the last `changed` event did.
struct InventoryMove
{
    InventoryAction action = InventoryAction::none;
    int item = 0;   // the id of the item that was in the hand
    int from = -1;  // the slot it was lifted from
    int to = -1;    // the slot the action happened on
    int other = 0;  // the id of the item it met there, or 0
    int amount = 0; // units merged
};

struct InventoryStyle : ComponentStyle
{
    // ---- geometry ----
    int columns = 6;
    int rows = 4;
    float slot_size = 0.0f; // 0: the largest square slots that fit the bounds
    float gap = 12.0f;
    float tile_inset = 5.0f;  // between a slot's well and the item in it
    float icon_inset = 12.0f; // between the item's frame and its icon
    // ---- look ----
    float count_size = 19.0f;
    bool rarity_frame = true;   // items wear a frame in their rarity colour
    bool letters = true;        // the placeholder icon shows the name's first letter
    float filtered_dim = 0.26f; // opacity of items the filter leaves out
    // ---- carrying ----
    float carry_scale = 1.16f; // the lifted item's size
    float carry_lift = 22.0f;  // how far it floats above the focused slot
    float tilt = 0.16f;        // radians it leans at full speed; 0 keeps it upright
    // ---- behaviour ----
    bool wrap = false;            // past an edge comes the opposite one
    EdgeExits exits;              // edges that hand the focus back (never while carrying)
    float entrance_step = 0.012f; // seconds between slots arriving; 0 for none
    audio::Cue pickup = audio::Cue::pickup;
    audio::Cue drop = audio::Cue::drop;
    audio::Cue swap = audio::Cue::rotate;
    audio::Cue merge = audio::Cue::merge; // pitched up as the stack fills
};

// A bag: fixed slots, items that live in them, and one gesture to rearrange
// them. Confirm lifts the focused item; it floats above the focus, leaning
// into its movement; confirm sets it down, swaps it with what is there or
// merges two stacks; back puts it where it came from. Every item springs to
// wherever it now lives, so nothing teleports.
//
//   ui::InventoryGrid bag;
//   bag.style.theme = theme;
//   bag.style.columns = 6;
//   bag.put(0, {1, "Tonic", 3, 9, kTonic});
//   bag.put(7, {2, "Tonic", 4, 9, kTonic});
//   bag.set_bounds({96, 300, 660, 440});
//   ...
//   if (bag.handle(input, feedback) == ui::Event::changed)
//       save(bag.last_move());
//   bag.update(dt);
//   bag.draw(canvas);
class InventoryGrid
{
  public:
    // tile is the inside of the item's frame; lift is 0..1 while it is carried.
    using Slot = std::function<void(Canvas &canvas, const gfx::Rect &tile,
                                    const InventoryItem &item, float lift)>;
    // How many units of `held` go into `target`: 0 means "they do not stack",
    // and the two swap places instead.
    using MergeRule = std::function<int(const InventoryItem &held, const InventoryItem &target)>;

    InventoryStyle style;
    Slot icon;       // draws an item's icon instead of its texture or placeholder
    MergeRule merge; // unset: default_merge()

    // Equal non-zero kinds stack, up to the target's max_stack.
    static int default_merge(const InventoryItem &held, const InventoryItem &target);

    // Empties the bag (and the hand).
    void clear();
    // Puts an item into an empty slot without animation. False when the slot
    // is taken or out of range, or the id is 0 or already in the bag.
    bool put(int slot, InventoryItem item);
    bool remove(int id);
    // The item resting in a slot (not the carried one); nullptr when empty.
    const InventoryItem *at(int slot) const;
    const InventoryItem *find(int id) const;
    // The slot an item rests in; -1 while it is carried or unknown.
    int slot_of(int id) const;
    int item_count() const;
    int slot_count() const
    {
        return std::max(style.columns, 1) * std::max(style.rows, 1);
    }

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
    void set_focus(int slot, bool snap = true);
    // The id of the carried item, or 0.
    int holding() const
    {
        return held_;
    }
    const InventoryMove &last_move() const
    {
        return move_;
    }
    // Items of other categories fade and cannot be lifted; -1 shows all.
    void set_filter(int category)
    {
        filter_ = category;
    }
    int filter() const
    {
        return filter_;
    }
    void set_active(bool active)
    {
        active_ = active;
    }
    // Replays the entrance animation.
    void enter();

    // moved: the focus went to another slot. changed: an item was lifted,
    // set down, swapped, merged or returned (see last_move()). cancelled:
    // back with an empty hand. refused: an edge, an empty or filtered slot.
    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

    // The edge the focus asked to leave through in the last handle() (see
    // style.exits), or Direction::none. handle() returned Event::none.
    Direction exit() const
    {
        return exit_;
    }
    gfx::Rect slot_rect(int slot) const;

  private:
    struct Entry
    {
        InventoryItem item;
        int slot = -1;      // -1: in the hand
        float fade = -1.0f; // >= 0: merged away, seconds since
        int into = -1;      // ... the slot it dissolves into
        tween::Spring x, y; // its centre, relative to the bounds' corner
        tween::Spring lift; // 0 resting .. 1 carried
        Pulse bump;         // its stack just grew
    };

    float cell() const;
    gfx::Rect local_rect(int slot) const;
    Entry *entry_at(int slot);
    const Entry *entry_at(int slot) const;
    Entry *entry_of(int id);
    bool shown(const InventoryItem &item) const;
    void draw_item(Canvas &canvas, const Entry &entry) const;

    std::vector<Entry> entries_;
    gfx::Rect bounds_{0.0f, 0.0f, 660.0f, 440.0f};
    int focus_ = 0;
    int held_ = 0;
    int origin_ = -1;
    int filter_ = -1;
    bool active_ = true;
    float age_ = 10.0f;
    InventoryMove move_;
    Direction exit_ = Direction::none;
    Highlight highlight_; // relative to the bounds' corner
    tween::Spring carrying_;
};

} // namespace hui::ui
