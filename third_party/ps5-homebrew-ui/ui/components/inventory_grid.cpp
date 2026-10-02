// ps5-homebrew-ui - Component: InventoryGrid.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/inventory_grid.hpp"

#include "ui/components/progress.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

const Color kBlack = Color::rgb(0x000000);
constexpr float kFadeTime = 0.22f; // a stack that merged away dissolves over this

} // namespace

int InventoryGrid::default_merge(const InventoryItem &held, const InventoryItem &target)
{
    if (held.kind == 0 || held.kind != target.kind || target.max_stack <= 1)
        return 0;
    return std::clamp(target.max_stack - target.count, 0, held.count);
}

void InventoryGrid::clear()
{
    entries_.clear();
    held_ = 0;
    origin_ = -1;
    move_ = {};
}

float InventoryGrid::cell() const
{
    if (style.slot_size > 0.0f)
        return style.slot_size;
    const float across = static_cast<float>(std::max(style.columns, 1));
    const float down = static_cast<float>(std::max(style.rows, 1));
    const float by_width = (bounds_.w - style.gap * (across - 1.0f)) / across;
    const float by_height = (bounds_.h - style.gap * (down - 1.0f)) / down;
    return std::max(std::min(by_width, by_height), 8.0f);
}

// A slot relative to the bounds' corner; the grid is centred in the bounds.
Rect InventoryGrid::local_rect(int slot) const
{
    const int across = std::max(style.columns, 1);
    const int down = std::max(style.rows, 1);
    const float size = cell();
    const float width = static_cast<float>(across) * (size + style.gap) - style.gap;
    const float height = static_cast<float>(down) * (size + style.gap) - style.gap;
    const float x = (bounds_.w - width) * 0.5f;
    const float y = (bounds_.h - height) * 0.5f;
    return {x + static_cast<float>(slot % across) * (size + style.gap),
            y + static_cast<float>(slot / across) * (size + style.gap), size, size};
}

Rect InventoryGrid::slot_rect(int slot) const
{
    Rect r = local_rect(slot);
    r.x += bounds_.x;
    r.y += bounds_.y;
    return r;
}

InventoryGrid::Entry *InventoryGrid::entry_at(int slot)
{
    for (Entry &entry : entries_)
    {
        if (entry.slot == slot && entry.fade < 0.0f)
            return &entry;
    }
    return nullptr;
}

const InventoryGrid::Entry *InventoryGrid::entry_at(int slot) const
{
    for (const Entry &entry : entries_)
    {
        if (entry.slot == slot && entry.fade < 0.0f)
            return &entry;
    }
    return nullptr;
}

InventoryGrid::Entry *InventoryGrid::entry_of(int id)
{
    for (Entry &entry : entries_)
    {
        if (entry.item.id == id && entry.fade < 0.0f)
            return &entry;
    }
    return nullptr;
}

bool InventoryGrid::put(int slot, InventoryItem item)
{
    if (slot < 0 || slot >= slot_count() || item.id == 0 || entry_at(slot) != nullptr ||
        entry_of(item.id) != nullptr)
        return false;
    Entry entry;
    entry.item = std::move(item);
    entry.slot = slot;
    const Rect at = local_rect(slot);
    entry.x.snap(at.cx());
    entry.y.snap(at.cy());
    entries_.push_back(std::move(entry));
    return true;
}

bool InventoryGrid::remove(int id)
{
    const std::size_t before = entries_.size();
    std::erase_if(entries_, [id](const Entry &entry) { return entry.item.id == id; });
    if (held_ == id)
    {
        held_ = 0;
        origin_ = -1;
    }
    return entries_.size() != before;
}

const InventoryItem *InventoryGrid::at(int slot) const
{
    const Entry *entry = slot >= 0 ? entry_at(slot) : nullptr;
    return entry != nullptr ? &entry->item : nullptr;
}

const InventoryItem *InventoryGrid::find(int id) const
{
    for (const Entry &entry : entries_)
    {
        if (entry.item.id == id && entry.fade < 0.0f)
            return &entry.item;
    }
    return nullptr;
}

int InventoryGrid::slot_of(int id) const
{
    for (const Entry &entry : entries_)
    {
        if (entry.item.id == id && entry.fade < 0.0f)
            return entry.slot;
    }
    return -1;
}

int InventoryGrid::item_count() const
{
    int count = 0;
    for (const Entry &entry : entries_)
        count += entry.fade < 0.0f ? 1 : 0;
    return count;
}

bool InventoryGrid::shown(const InventoryItem &item) const
{
    return filter_ < 0 || item.category == filter_;
}

void InventoryGrid::set_bounds(const Rect &bounds)
{
    bounds_ = bounds;
    // Positions are relative to the corner, but the cell size may have
    // changed: settle everything where it now belongs.
    for (Entry &entry : entries_)
    {
        if (entry.slot < 0)
            continue;
        const Rect at = local_rect(entry.slot);
        entry.x.snap(at.cx());
        entry.y.snap(at.cy());
    }
    focus_ = std::clamp(focus_, 0, slot_count() - 1);
    highlight_.snap(local_rect(focus_));
}

void InventoryGrid::set_focus(int slot, bool snap)
{
    focus_ = std::clamp(slot, 0, slot_count() - 1);
    highlight_.target(local_rect(focus_));
    if (snap)
        highlight_.snap(local_rect(focus_));
}

void InventoryGrid::enter()
{
    age_ = 0.0f;
}

Event InventoryGrid::handle(const InputFrame &input, Feedback &feedback)
{
    exit_ = Direction::none;
    focus_ = std::clamp(focus_, 0, slot_count() - 1);
    const float x = slot_rect(focus_).cx();
    const int across = std::max(style.columns, 1);
    const int down = std::max(style.rows, 1);

    if (input.nav != Direction::none)
    {
        int column = focus_ % across;
        int row = focus_ / across;
        column += input.nav == Direction::right ? 1 : (input.nav == Direction::left ? -1 : 0);
        row += input.nav == Direction::down ? 1 : (input.nav == Direction::up ? -1 : 0);
        if (column < 0 || column >= across || row < 0 || row >= down)
        {
            // The hand never leaves the bag with something in it.
            if (held_ == 0 && style.exits.allows(input.nav))
            {
                exit_ = input.nav;
                return Event::none;
            }
            if (!style.wrap || input.nav_repeat)
                return refuse(feedback, style, input, highlight_.refusal(), x);
            column = (column + across) % across;
            row = (row + down) % down;
        }
        focus_ = row * across + column;
        highlight_.target(local_rect(focus_));
        // Carrying sounds a little lower: the hand is full.
        play_cue(feedback, style, style.sounds.move, slot_rect(focus_).cx(),
                 held_ != 0 ? 0.9f : 1.0f);
        return Event::moved;
    }

    if (input.is_pressed(Action::confirm))
    {
        Entry *here = entry_at(focus_);
        if (held_ == 0)
        {
            if (here == nullptr || !shown(here->item))
                return refuse(feedback, style, input, highlight_.refusal(), x);
            held_ = here->item.id;
            origin_ = focus_;
            here->slot = -1;
            move_ = {InventoryAction::picked, held_, origin_, focus_, 0, 0};
            play_cue(feedback, style, style.pickup, x);
            return Event::changed;
        }

        Entry *held = entry_of(held_);
        if (held == nullptr)
        {
            held_ = 0;
            return Event::none;
        }
        move_ = {InventoryAction::placed, held_, origin_, focus_, 0, 0};
        if (here == nullptr)
        {
            held->slot = focus_;
            held_ = 0;
            play_cue(feedback, style, style.drop, x);
            return Event::changed;
        }

        const int wanted =
            merge ? merge(held->item, here->item) : default_merge(held->item, here->item);
        const int amount = std::clamp(wanted, 0, held->item.count);
        move_.other = here->item.id;
        if (amount > 0)
        {
            here->item.count += amount;
            held->item.count -= amount;
            here->bump.trigger();
            move_.action = InventoryAction::merged;
            move_.amount = amount;
            const float full = here->item.max_stack > 0
                                   ? static_cast<float>(here->item.count) /
                                         static_cast<float>(here->item.max_stack)
                                   : 1.0f;
            play_cue(feedback, style, style.merge, x, 0.9f + 0.3f * tween::clamp01(full));
            if (held->item.count <= 0)
            {
                // Nothing left in the hand: the tile dissolves into the stack.
                held->fade = 0.0f;
                held->into = focus_;
                held_ = 0;
            }
            return Event::changed;
        }

        // They do not stack: trade places. The origin is free, since the
        // carried item left it.
        here->slot = origin_;
        held->slot = focus_;
        held_ = 0;
        move_.action = InventoryAction::swapped;
        play_cue(feedback, style, style.swap, x);
        return Event::changed;
    }

    if (input.is_pressed(Action::back))
    {
        if (held_ != 0)
        {
            Entry *held = entry_of(held_);
            if (held != nullptr)
                held->slot = origin_;
            move_ = {InventoryAction::returned, held_, origin_, origin_, 0, 0};
            held_ = 0;
            play_cue(feedback, style, style.sounds.cancel, x);
            return Event::changed;
        }
        play_cue(feedback, style, style.sounds.cancel, x);
        return Event::cancelled;
    }
    return Event::none;
}

void InventoryGrid::update(float dt)
{
    age_ += dt;
    focus_ = std::clamp(focus_, 0, slot_count() - 1);
    highlight_.target(local_rect(focus_));
    highlight_.update(dt, style);
    carrying_.target = held_ != 0 ? 1.0f : 0.0f;
    carrying_.update(dt, 12.0f);

    const float omega = std::max(style.omega(), 16.0f);
    const Rect under = local_rect(focus_);
    for (Entry &entry : entries_)
    {
        const bool carried = entry.slot < 0 && entry.fade < 0.0f;
        Rect home = under;
        if (entry.fade >= 0.0f)
        {
            entry.fade += dt;
            home = local_rect(std::max(entry.into, 0));
        }
        else if (!carried)
        {
            home = local_rect(std::min(entry.slot, slot_count() - 1));
        }
        entry.x.target = home.cx();
        entry.y.target = home.cy();
        // The carried item trails the focus a little, which is what makes it
        // lean; resting items go straight home.
        entry.x.update(dt, carried ? 15.0f : omega);
        entry.y.update(dt, carried ? 15.0f : omega);
        entry.lift.target = carried ? 1.0f : 0.0f;
        entry.lift.update(dt, 18.0f);
        entry.bump.update(dt, 9.0f);
    }
    std::erase_if(entries_, [](const Entry &entry) { return entry.fade >= kFadeTime; });
}

void InventoryGrid::draw_item(Canvas &canvas, const Entry &entry) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, 0);
    const InventoryItem &item = entry.item;
    const bool calm = style.reduced_motion;
    const float lift = tween::clamp01(entry.lift.value);
    const float size = cell() - 2.0f * style.tile_inset;
    const float cx = bounds_.x + entry.x.value;
    const float cy = bounds_.y + entry.y.value - style.carry_lift * lift;
    const Rect tile{cx - size * 0.5f, cy - size * 0.5f, size, size};
    const float gone = entry.fade >= 0.0f ? tween::clamp01(entry.fade / kFadeTime) : 0.0f;
    const float scale = (calm ? 1.0f : 1.0f + (style.carry_scale - 1.0f) * lift) *
                        (1.0f + 0.1f * entry.bump.value) * (1.0f - 0.4f * gone);
    // It leans into its movement: the faster it travels, the further.
    const float lean =
        calm ? 0.0f : std::clamp(entry.x.velocity * 0.0012f, -1.0f, 1.0f) * style.tilt * lift;

    list.push_opacity((shown(item) ? 1.0f : style.filtered_dim) * (1.0f - gone));
    if (lift > 0.01f)
    {
        // The shadow stays on the slot while the item floats above it.
        const float drop = style.carry_lift * lift;
        list.shadow({tile.x + 4.0f, tile.y + drop + 6.0f, tile.w - 8.0f, tile.h - 6.0f},
                    size * 0.2f, 18.0f + 10.0f * lift,
                    (theme.shadow.a > 0.05f ? theme.shadow : kBlack).with_alpha(0.42f * lift));
    }
    list.push_transform(scale, cx, cy, 0.0f, 0.0f);

    const float radius = std::min(theme.radius, size * 0.2f);
    // An item without a rarity wears a quiet frame: halfway between the
    // surface and the muted ink, so it reads in light and dark themes.
    const Color muted{theme.text_muted.r, theme.text_muted.g, theme.text_muted.b, 1.0f};
    const Color plain = gfx::mix(solid_surface(theme), muted, 0.5f);
    const Color rarity = item.rarity.a > 0.0f ? item.rarity : plain;
    const Color body = gfx::mix(solid_surface(theme), rarity, 0.14f);
    const float frame = style.rarity_frame ? std::max(3.0f, theme.border) : 0.0f;
    if (std::fabs(lean) > 0.002f)
    {
        // Only round-cornered boxes can turn; a carried item is in motion
        // anyway, and settles back into the theme's own corner.
        if (frame > 0.0f)
            list.rotated_rect(tile, radius, lean, rarity);
        list.rotated_rect(tile.inset(frame), std::max(radius - frame, 0.0f), lean, body);
    }
    else
    {
        if (frame > 0.0f)
            paint.fill(tile, radius, rarity);
        paint.fill(tile.inset(frame), std::max(radius - frame, 0.0f), body);
    }

    const Rect art = tile.inset(std::max(style.icon_inset, frame + 2.0f));
    if (icon)
    {
        icon(canvas, art, item, lift);
    }
    else if (item.texture != 0)
    {
        list.image(item.texture, art, item.uv, Color::rgb(0xffffff), std::min(radius, 6.0f));
    }
    else
    {
        // The placeholder: a plate in the item's tint, with its initial.
        const Color tint = item.tint.a > 0.0f ? item.tint : gfx::mix(rarity, body, 0.35f);
        const float corner = std::min(theme.radius, art.w * 0.3f);
        paint.fill(art, corner, tint);
        if (style.letters && !item.name.empty())
        {
            const float text = art.h * 0.5f;
            paint.label(item.name.substr(0, 1), art.cx(), art.cy() + text * 0.36f, text,
                        Painter::on(tint), gfx::Align::center);
        }
    }

    if (item.max_stack > 1 || item.count > 1)
    {
        char text[16];
        std::snprintf(text, sizeof(text), "%d", item.count);
        const float width = paint.label_width(text, style.count_size) + 12.0f;
        const float height = style.count_size + 6.0f;
        const Rect plate{tile.x + tile.w - width - frame, tile.y + tile.h - height - frame, width,
                         height};
        const bool full = item.count >= item.max_stack && item.max_stack > 1;
        const Color back = full ? rarity : solid_surface(theme);
        list.rounded_rect(plate, theme.corner == Corner::round ? std::min(radius, 6.0f) : 0.0f,
                          back);
        paint.label(text, plate.cx(), plate.cy() + style.count_size * 0.36f, style.count_size,
                    full ? Painter::on(rarity) : theme.text, gfx::Align::center);
    }
    list.pop_transform();
    list.pop_opacity();
}

void InventoryGrid::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const bool calm = style.reduced_motion;
    const int count = slot_count();
    const int across = std::max(style.columns, 1);
    const float size = cell();
    const float radius = std::min(theme.radius, size * 0.22f);
    const float carrying = tween::clamp01(carrying_.value);

    const auto entrance = [&](int slot)
    {
        if (style.entrance_step <= 0.0f || calm)
            return tween::cubic_out(age_ / 0.2f);
        // A diagonal wave from the top-left corner.
        return tween::stagger(age_, slot % across + slot / across, style.entrance_step * 3.0f,
                              0.3f);
    };

    for (int slot = 0; slot < count; ++slot)
    {
        const Rect r = slot_rect(slot);
        const float in = entrance(slot);
        if (in <= 0.0f)
            continue;
        list.push_opacity(in);
        paint.well(r, radius, theme.surface_high);
        // Where the carried item came from: the place back would return it to.
        if (slot == origin_ && carrying > 0.01f)
            paint.stroke(r.inset(4.0f), std::max(radius - 4.0f, 0.0f), 2.0f,
                         theme.text_muted.with_alpha(0.7f * carrying));
        list.pop_opacity();
    }

    for (const Entry &entry : entries_)
    {
        if (entry.slot < 0 && entry.fade < 0.0f)
            continue; // the carried one is drawn last
        const float in = entrance(std::max(entry.slot, 0));
        if (in <= 0.0f)
            continue;
        list.push_opacity(in);
        draw_item(canvas, entry);
        list.pop_opacity();
    }

    list.push_transform(1.0f, 0.0f, 0.0f, bounds_.x, bounds_.y);
    HighlightStyle ring;
    ring.kind = HighlightKind::ring;
    ring.radius = radius;
    highlight_.draw(canvas, style, ring, (active_ ? 1.0f : 0.35f) * entrance(focus_));
    list.pop_transform();

    for (const Entry &entry : entries_)
    {
        if (entry.slot < 0 && entry.fade < 0.0f)
            draw_item(canvas, entry);
    }
}

} // namespace hui::ui
