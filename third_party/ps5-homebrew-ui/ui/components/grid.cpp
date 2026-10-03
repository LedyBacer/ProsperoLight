// ps5-homebrew-ui - Component: GridView.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/grid.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

void GridView::set_items(std::vector<CardItem> items)
{
    items_ = std::move(items);
    checks_.assign(items_.size(), 0.0f);
    for (std::size_t i = 0; i < items_.size(); ++i)
        checks_[i] = items_[i].selected ? 1.0f : 0.0f;
    focus_ = std::clamp(focus_, 0, std::max(static_cast<int>(items_.size()) - 1, 0));
    retarget(true);
}

void GridView::set_bounds(const Rect &bounds)
{
    bounds_ = bounds;
    retarget(true);
}

void GridView::set_focus(int index, bool snap)
{
    if (items_.empty())
        return;
    focus_ = std::clamp(index, 0, static_cast<int>(items_.size()) - 1);
    column_ = focus_ % columns();
    retarget(snap);
}

void GridView::enter()
{
    age_ = 0.0f;
}

int GridView::columns() const
{
    return std::max(style.columns, 1);
}

int GridView::rows() const
{
    return (static_cast<int>(items_.size()) + columns() - 1) / columns();
}

GridView::Layout GridView::layout() const
{
    Layout at;
    at.columns = columns();
    at.rows = rows();
    const float across = static_cast<float>(at.columns);
    const float gaps = (across - 1.0f) * style.gap_x;
    const float grow = std::max(style.card.focus_scale - 1.0f, 0.0f);
    float pad_x = style.padding;
    if (style.padding >= 0.0f)
    {
        at.cell_w = style.cell_width > 0.0f ? style.cell_width
                                            : (bounds_.w - 2.0f * style.padding - gaps) / across;
        at.pad_y = style.padding;
    }
    else
    {
        // The focused cell grows by `grow` of its own width and wears a ring:
        // both must fit beside the outer columns. With automatic cells the
        // width depends on the padding and the padding on the width; solved.
        const float reach = card_ring_reach(style.theme) + 3.0f;
        at.cell_w = style.cell_width > 0.0f ? style.cell_width
                                            : (bounds_.w - 2.0f * reach - gaps) / (across + grow);
        pad_x = reach + at.cell_w * grow * 0.5f;
    }
    at.cell_w = std::max(at.cell_w, 8.0f);
    at.cell_h = style.cell_height > 0.0f ? style.cell_height : card_height(style.card, at.cell_w);
    at.cell_h = std::max(at.cell_h, 8.0f);
    if (style.padding < 0.0f)
        at.pad_y = card_ring_reach(style.theme) + 3.0f + at.cell_h * grow * 0.5f + style.card.lift;
    at.view = {bounds_.x + pad_x, bounds_.y + at.pad_y, across * at.cell_w + gaps,
               std::max(bounds_.h - 2.0f * at.pad_y, 1.0f)};
    at.pitch = at.cell_h + style.gap_y;
    at.full_rows = std::max(1, static_cast<int>((at.view.h + style.gap_y + 0.5f) / at.pitch));
    return at;
}

Rect GridView::content_rect(const Layout &at, int index) const
{
    const int row = index / at.columns;
    const int column = index % at.columns;
    return {static_cast<float>(column) * (at.cell_w + style.gap_x),
            static_cast<float>(row) * (at.cell_h + style.gap_y), at.cell_w, at.cell_h};
}

Rect GridView::cell_rect(int index) const
{
    const Layout at = layout();
    const Rect r = content_rect(at, index);
    return {at.view.x + r.x, at.view.y + r.y - scroll_.value, r.w, r.h};
}

// Where a direction leads from the focus, or -1 at an edge. `round` allows
// the moves that go round the grid.
int GridView::step(const Layout &at, Direction direction, bool round) const
{
    const int count = static_cast<int>(items_.size());
    const int across = at.columns;
    const int row = focus_ / across;
    const int column = focus_ % across;
    const int last_row = at.rows - 1;
    const int in_row = std::min(across, count - row * across);
    const int wanted = std::min(column_, across - 1);
    round = round && style.wrap != GridWrap::none;
    switch (direction)
    {
    case Direction::left:
        if (column > 0)
            return focus_ - 1;
        if (style.wrap == GridWrap::flow && focus_ > 0)
            return focus_ - 1;
        if (!round)
            return -1;
        return style.wrap == GridWrap::rows ? row * across + in_row - 1 : count - 1;
    case Direction::right:
        if (column + 1 < in_row)
            return focus_ + 1;
        if (style.wrap == GridWrap::flow && focus_ + 1 < count)
            return focus_ + 1;
        if (!round)
            return -1;
        return style.wrap == GridWrap::rows ? row * across : 0;
    case Direction::up:
        if (row > 0)
            return (row - 1) * across + wanted;
        // A short last row takes its last cell; the column is remembered.
        return round ? std::min(last_row * across + wanted, count - 1) : -1;
    case Direction::down:
        if (row < last_row)
            return std::min((row + 1) * across + wanted, count - 1);
        return round ? wanted : -1;
    default:
        return -1;
    }
}

void GridView::retarget(bool snap)
{
    if (items_.empty())
        return;
    const Layout at = layout();
    focus_ = std::clamp(focus_, 0, static_cast<int>(items_.size()) - 1);
    const Rect target = content_rect(at, focus_);
    // The grid scrolls by whole rows, the least that brings the focused row
    // into view: no row is ever cut at the top, and whatever does not fit at
    // the bottom peeks in, faded, to say there is more.
    const int row = focus_ / at.columns;
    top_row_ = std::clamp(top_row_, row - at.full_rows + 1, row);
    top_row_ = std::clamp(top_row_, 0, std::max(at.rows - at.full_rows, 0));
    scroll_.target = static_cast<float>(top_row_) * at.pitch;
    highlight_.target(target);
    const CardItem &item = items_[static_cast<std::size_t>(focus_)];
    glow_.target(item.accent.a > 0.0f ? item.accent : style.theme.focus);
    if (snap)
    {
        scroll_.snap(scroll_.target);
        highlight_.snap(target);
        glow_.snap(item.accent.a > 0.0f ? item.accent : style.theme.focus);
    }
}

Event GridView::handle(const InputFrame &input, Feedback &feedback)
{
    exit_ = Direction::none;
    if (items_.empty())
        return Event::none;
    const Layout at = layout();
    if (input.nav != Direction::none)
    {
        // An edge that is an exit hands the focus over instead of wrapping,
        // and going round is a decision, not something a held direction does.
        const bool leaves = style.exits.allows(input.nav);
        const int next = step(at, input.nav, !leaves && !input.nav_repeat);
        if (next < 0 || next == focus_)
        {
            if (leaves)
            {
                exit_ = input.nav;
                return Event::none;
            }
            refused_ = input.nav;
            return refuse(feedback, style, input, refusal_, cell_rect(focus_).cx());
        }
        focus_ = next;
        if (input.nav == Direction::left || input.nav == Direction::right)
            column_ = focus_ % at.columns;
        retarget(false);
        const float along =
            at.rows > 1 ? static_cast<float>(focus_ / at.columns) / static_cast<float>(at.rows - 1)
                        : 0.0f;
        play_cue(feedback, style, style.sounds.move, at.view.x + content_rect(at, focus_).cx(),
                 style.pitch_by_row ? tween::lerp(1.05f, 0.95f, along) : 1.0f);
        return Event::moved;
    }
    const float x = cell_rect(focus_).cx();
    if (input.is_pressed(Action::confirm))
    {
        CardItem &item = items_[static_cast<std::size_t>(focus_)];
        if (item.disabled)
        {
            refused_ = Direction::none;
            return refuse(feedback, style, input, refusal_, x);
        }
        press_.trigger();
        if (style.select_on_confirm)
        {
            item.selected = !item.selected;
            play_cue(feedback, style, style.sounds.change, x, item.selected ? 1.06f : 0.94f);
            return Event::changed;
        }
        play_cue(feedback, style, style.sounds.activate, x);
        if (style.sounds.rumble > 0.0f)
            feedback.rumble(0.3f * style.sounds.rumble, 0.04f);
        return Event::activated;
    }
    if (input.is_pressed(Action::back))
    {
        play_cue(feedback, style, style.sounds.cancel, x);
        return Event::cancelled;
    }
    return Event::none;
}

void GridView::update(float dt)
{
    age_ += dt;
    // Every frame, so a change of style (columns, sizes, theme) simply takes
    // effect: the focus and the scroll glide to where they now belong.
    retarget(false);
    highlight_.update(dt, style);
    scroll_.update(dt, std::max(style.omega(), 14.0f));
    glow_.update(dt, 8.0f);
    active_amount_.target = active_ ? 1.0f : 0.0f;
    active_amount_.update(dt, 18.0f);
    press_.update(dt, 10.0f);
    refusal_.update(dt, 9.0f);
    const float ease = 1.0f - std::exp(-(style.reduced_motion ? 40.0f : 14.0f) * dt);
    for (std::size_t i = 0; i < items_.size() && i < checks_.size(); ++i)
    {
        const float target = items_[i].selected ? 1.0f : 0.0f;
        checks_[i] += (target - checks_[i]) * ease;
        if (std::fabs(target - checks_[i]) < 0.002f)
            checks_[i] = target;
    }
}

void GridView::draw(Canvas &canvas) const
{
    if (items_.empty())
        return;
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    const Layout at = layout();
    const float scroll = scroll_.value;
    const bool overflow = at.rows > at.full_rows;
    const float active = active_amount_.value;

    // Sideways the clip is wide enough for shadows and rings; up and down it
    // is the bounds themselves once rows scroll through them.
    constexpr float kBleed = 28.0f;
    list.push_clip({bounds_.x - kBleed, bounds_.y - (overflow ? 0.0f : kBleed),
                    bounds_.w + 2.0f * kBleed, bounds_.h + (overflow ? 0.0f : 2.0f * kBleed)});

    const auto on_screen = [&](const Rect &c) -> Rect
    { return {at.view.x + c.x, at.view.y + c.y - scroll, c.w, c.h}; };
    // How visible a cell is: 1 inside the view, fading as its row is cut.
    const auto visibility = [&](const Rect &r)
    {
        if (!overflow || style.edge_fade <= 0.0f)
            return 1.0f;
        // A row that has scrolled out above is gone entirely, not left as a
        // sliver in the padding.
        const float top = std::max(bounds_.y, at.view.y - style.gap_y);
        const float shown = std::min(r.y + r.h - top, bounds_.y + bounds_.h - r.y);
        // Eased both ways: a sliver is all but gone, half a row is plain to see.
        return tween::smoothstep(shown / (r.h * style.edge_fade));
    };
    const auto entrance = [&](int index)
    {
        if (style.entrance_step <= 0.0f || style.reduced_motion)
            return tween::cubic_out(age_ / 0.2f);
        return tween::stagger(age_, index / at.columns + index % at.columns, style.entrance_step,
                              0.34f);
    };

    // A refusal nudges the focused cell and its ring along the refused axis.
    const float nudge = shake(refusal_.value, canvas.time, 9.0f);
    const bool vertical = refused_ == Direction::up || refused_ == Direction::down;
    const float nudge_x = vertical ? 0.0f : nudge;
    const float nudge_y = vertical ? nudge : 0.0f;

    const auto draw_cell = [&](int index, float focus)
    {
        const CardItem &item = items_[static_cast<std::size_t>(index)];
        const Rect r = on_screen(content_rect(at, index));
        const float arrived = entrance(index);
        const float alpha = visibility(r) * arrived;
        if (alpha <= 0.0f)
            return;
        const bool held = focus > 0.5f;
        CardState state;
        state.focus = focus;
        state.press = held ? press_.value : 0.0f;
        state.selected = checks_[static_cast<std::size_t>(index)];
        state.marks = false; // one ring glides for the whole grid
        list.push_opacity(alpha);
        list.push_transform(1.0f, 0.0f, 0.0f, held ? nudge_x : 0.0f,
                            (style.reduced_motion ? 0.0f : 20.0f * (1.0f - arrived)) +
                                (held ? nudge_y : 0.0f));
        if (content)
        {
            list.push_transform(card_scale(style, style.card, state), r.cx(), r.cy(), 0.0f,
                                -card_lift(style, style.card, state));
            content(canvas, r, item, index, focus);
            list.pop_transform();
        }
        else
        {
            draw_card(canvas, style, style.card, r, item, state, art);
        }
        list.pop_transform();
        list.pop_opacity();
    };

    // Cells the highlight touches are drawn last, so the one that grows lies
    // over its neighbours. While it glides that is up to four cells.
    constexpr int kHot = 6;
    int hot[kHot];
    float hot_focus[kHot];
    int hot_count = 0;
    const int first_row = std::clamp(static_cast<int>(std::floor((scroll - at.pad_y) / at.pitch)),
                                     0, std::max(at.rows - 1, 0));
    const int last_row =
        std::clamp(static_cast<int>(std::floor((scroll + bounds_.h) / at.pitch)), 0, at.rows - 1);
    const int count = static_cast<int>(items_.size());
    for (int index = first_row * at.columns; index < std::min((last_row + 1) * at.columns, count);
         ++index)
    {
        const float focus = highlight_.coverage(content_rect(at, index)) * active;
        if (focus > 0.001f && hot_count < kHot)
        {
            // Insertion keeps them in rising order of focus.
            int slot = hot_count++;
            while (slot > 0 && hot_focus[slot - 1] > focus)
            {
                hot[slot] = hot[slot - 1];
                hot_focus[slot] = hot_focus[slot - 1];
                --slot;
            }
            hot[slot] = index;
            hot_focus[slot] = focus;
        }
        else
        {
            draw_cell(index, 0.0f);
        }
    }

    // The one focus indicator: where the highlight is, as large as a card
    // under it is drawn.
    Rect under = on_screen(highlight_.rect(0.0f));
    under.x += nudge_x;
    under.y += nudge_y;
    CardState lead;
    lead.focus = active;
    lead.press = press_.value;
    const Rect frame =
        card_placed(under, content ? under : card_frame(style.card, under),
                    card_scale(style, style.card, lead), card_lift(style, style.card, lead));
    const float amount = (0.35f + 0.65f * active) * entrance(focus_) * visibility(under);
    draw_card_halo(canvas, style, style.card, frame, glow_.value(), amount * active);
    for (int i = 0; i < hot_count; ++i)
        draw_cell(hot[i], hot_focus[i]);
    draw_card_ring(canvas, style, style.card, frame, amount);
    list.pop_clip();

    if (style.scroll_thumb && overflow)
    {
        const Painter paint(list, canvas.fonts, theme, canvas.glass);
        const Color ink = style.card.on_panel ? theme.text_muted : paint.page_text_muted();
        const float track = bounds_.h - 16.0f;
        const float size =
            std::max(track * static_cast<float>(at.full_rows) / static_cast<float>(at.rows), 36.0f);
        const float travel = track - size;
        const float along = scroll / (static_cast<float>(at.rows - at.full_rows) * at.pitch);
        const float x = bounds_.x + bounds_.w + 8.0f;
        const float round = theme.corner == Corner::round && theme.radius >= 2.0f ? 2.0f : 0.0f;
        list.rounded_rect({x, bounds_.y + 8.0f, 4.0f, track}, round, ink.with_alpha(0.16f));
        list.rounded_rect({x, bounds_.y + 8.0f + travel * tween::clamp01(along), 4.0f, size}, round,
                          ink.with_alpha(0.7f));
    }
}

} // namespace hui::ui
