// ps5-homebrew-ui - Component: Table.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/table.hpp"

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

constexpr float kPi = 3.14159265f;

char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

// Negative, zero or positive, ignoring ASCII case: "apple" sorts next to
// "Apple", as a person expects of a list of names.
int compare_text(const std::string &a, const std::string &b)
{
    const std::size_t count = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < count; ++i)
    {
        const char x = lower(a[i]);
        const char y = lower(b[i]);
        if (x != y)
            return x < y ? -1 : 1;
    }
    return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
}

// The order a column takes first: figures start with the largest (a
// leaderboard), text with A.
SortOrder first_order(const TableColumn &column)
{
    return column.numeric ? SortOrder::descending : SortOrder::ascending;
}

SortOrder other_order(SortOrder order)
{
    return order == SortOrder::ascending ? SortOrder::descending : SortOrder::ascending;
}

} // namespace

void Table::set_columns(std::vector<TableColumn> columns)
{
    columns_ = std::move(columns);
    if (sort_column_ >= static_cast<int>(columns_.size()))
    {
        sort_column_ = -1;
        sort_order_ = SortOrder::none;
    }
    cursor_ = std::max(next_sortable(-1, 1), 0);
    reorder(false);
    retarget(true);
}

void Table::set_rows(std::vector<TableRow> rows)
{
    const int focused = focus();
    const int focused_id = focused >= 0 ? rows_[static_cast<std::size_t>(focused)].id : -1;
    const bool had_rows = !rows_.empty();

    std::vector<tween::Bounce> tops(rows.size());
    std::vector<tween::Spring> marks(rows.size());
    std::vector<bool> selected(rows.size(), false);
    std::vector<bool> known(rows.size(), false);
    int focus_row = -1;
    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        for (std::size_t old = 0; old < rows_.size(); ++old)
        {
            if (rows_[old].id != rows[i].id)
                continue;
            tops[i] = tops_[old];
            marks[i] = marks_[old];
            selected[i] = selected_[old];
            known[i] = true;
            break;
        }
        if (rows[i].id == focused_id && focus_row < 0)
            focus_row = static_cast<int>(i);
    }
    rows_ = std::move(rows);
    tops_ = std::move(tops);
    marks_ = std::move(marks);
    selected_ = std::move(selected);

    place_ = std::clamp(place_, 0, std::max(static_cast<int>(rows_.size()) - 1, 0));
    order_.clear();
    reorder(had_rows);
    // A row that was not there before has no old place to come from.
    for (std::size_t i = 0; i < rows_.size(); ++i)
    {
        if (!known[i])
            tops_[i].snap(tops_[i].target);
    }
    if (focus_row >= 0)
        place_ = place_of(focus_row);
    retarget(!had_rows);
}

void Table::set_bounds(const Rect &bounds)
{
    bounds_ = bounds;
    retarget(true);
}

int Table::focus() const
{
    if (order_.empty() || place_ < 0 || place_ >= static_cast<int>(order_.size()))
        return -1;
    return order_[static_cast<std::size_t>(place_)];
}

int Table::place_of(int row) const
{
    for (std::size_t i = 0; i < order_.size(); ++i)
    {
        if (order_[i] == row)
            return static_cast<int>(i);
    }
    return -1;
}

void Table::set_focus(int row, bool snap)
{
    const int place = place_of(row);
    if (place < 0)
        return;
    place_ = place;
    in_header_ = false;
    retarget(snap);
}

bool Table::selected(int row) const
{
    return row >= 0 && row < static_cast<int>(selected_.size()) &&
           selected_[static_cast<std::size_t>(row)];
}

void Table::set_selected(int row, bool selected)
{
    if (row >= 0 && row < static_cast<int>(selected_.size()))
        selected_[static_cast<std::size_t>(row)] = selected;
}

int Table::selected_count() const
{
    return static_cast<int>(std::count(selected_.begin(), selected_.end(), true));
}

void Table::enter()
{
    age_ = 0.0f;
}

Rect Table::inner() const
{
    if (style.panel)
        return bounds_.inset(style.panel_padding);
    // Without a panel's padding the scroll thumb needs room of its own.
    return {bounds_.x, bounds_.y, bounds_.w - (style.scroll_thumb ? 10.0f : 0.0f), bounds_.h};
}

Rect Table::header_rect() const
{
    const Rect in = inner();
    return {in.x, in.y, in.w, style.header ? style.header_height : 0.0f};
}

int Table::pinned_row() const
{
    if (style.pinned_id < 0)
        return -1;
    for (std::size_t i = 0; i < rows_.size(); ++i)
    {
        if (rows_[i].id == style.pinned_id)
            return static_cast<int>(i);
    }
    return -1;
}

Rect Table::pinned_rect() const
{
    const Rect in = inner();
    return {in.x, in.y + in.h - style.row_height, in.w, style.row_height};
}

Rect Table::view() const
{
    const Rect in = inner();
    const float top = style.header ? style.header_height : 0.0f;
    const float bottom = pinned_row() >= 0 ? style.row_height + style.pinned_gap : 0.0f;
    return {in.x, in.y + top, in.w, std::max(in.h - top - bottom, 0.0f)};
}

std::vector<Table::Column> Table::layout() const
{
    const Rect in = inner();
    float left = in.x + style.padding;
    if (style.multi_select)
        left += style.mark_width;
    if (style.rank)
        left += style.rank_width;
    const float right = in.x + in.w - style.padding;
    const float count = static_cast<float>(columns_.size());
    float fixed = 0.0f;
    float flex = 0.0f;
    for (const TableColumn &column : columns_)
    {
        if (column.width > 0.0f)
            fixed += column.width;
        else
            flex += std::max(column.flex, 0.0f);
    }
    const float shared =
        std::max(right - left - fixed - style.column_gap * std::max(count - 1.0f, 0.0f), 0.0f);
    std::vector<Column> columns;
    float x = left;
    for (const TableColumn &column : columns_)
    {
        const float w = column.width > 0.0f
                            ? column.width
                            : (flex > 0.0f ? shared * std::max(column.flex, 0.0f) / flex : 0.0f);
        columns.push_back({x, w});
        x += w + style.column_gap;
    }
    return columns;
}

int Table::next_sortable(int from, int direction) const
{
    const int count = static_cast<int>(columns_.size());
    for (int i = from + direction; i >= 0 && i < count; i += direction)
    {
        if (columns_[static_cast<std::size_t>(i)].sortable)
            return i;
    }
    return -1;
}

// Rebuilds the shown order and sends every row to its new place. The focus
// stays on the row it was on.
void Table::reorder(bool animate)
{
    const int focused = focus();
    order_.resize(rows_.size());
    for (std::size_t i = 0; i < order_.size(); ++i)
        order_[i] = static_cast<int>(i);
    if (sort_order_ != SortOrder::none && sort_column_ >= 0 &&
        sort_column_ < static_cast<int>(columns_.size()))
    {
        const std::size_t c = static_cast<std::size_t>(sort_column_);
        const bool numeric = columns_[c].numeric;
        const bool descending = sort_order_ == SortOrder::descending;
        // Stable, so rows that tie keep the order they were given in.
        std::stable_sort(order_.begin(), order_.end(),
                         [&](int a, int b)
                         {
                             const TableRow &ra = rows_[static_cast<std::size_t>(a)];
                             const TableRow &rb = rows_[static_cast<std::size_t>(b)];
                             if (c >= ra.cells.size() || c >= rb.cells.size())
                                 return false;
                             if (numeric)
                                 return descending ? ra.cells[c].number > rb.cells[c].number
                                                   : ra.cells[c].number < rb.cells[c].number;
                             const int order = compare_text(ra.cells[c].text, rb.cells[c].text);
                             return descending ? order > 0 : order < 0;
                         });
    }
    for (std::size_t place = 0; place < order_.size(); ++place)
    {
        tween::Bounce &top = tops_[static_cast<std::size_t>(order_[place])];
        top.target = static_cast<float>(place) * style.row_height;
        if (!animate)
            top.snap(top.target);
    }
    if (focused >= 0 && focused < static_cast<int>(rows_.size()))
        place_ = std::max(place_of(focused), 0);
}

void Table::retarget(bool snap)
{
    const Rect in = inner();
    const Rect area = view();
    const float count = static_cast<float>(rows_.size());
    const float top = static_cast<float>(place_) * style.row_height;
    scroll_.reveal(top, top + style.row_height, area.h, style.row_height * 0.3f,
                   count * style.row_height);
    if (snap)
        scroll_.position.snap(scroll_.position.target);
    // The row highlight lives in content space: scrolling must not make it lag.
    const Rect target{0.0f, top, in.w, style.row_height};
    highlight_.target(target);
    if (snap)
        highlight_.snap(target);

    if (cursor_ >= 0 && cursor_ < static_cast<int>(columns_.size()))
    {
        const std::vector<Column> columns = layout();
        const Column &column = columns[static_cast<std::size_t>(cursor_)];
        const Rect header = header_rect();
        const float grow = std::min(style.column_gap * 0.5f, 10.0f);
        const Rect cell{column.x - grow, header.y + 4.0f, column.w + 2.0f * grow, header.h - 8.0f};
        column_.target(cell);
        if (snap || !in_header_)
            column_.snap(cell);
    }
}

void Table::sort_by(int column, SortOrder order, bool animate)
{
    if (column < 0 || column >= static_cast<int>(columns_.size()))
        order = SortOrder::none;
    sort_order_ = order;
    sort_column_ = order == SortOrder::none ? -1 : column;
    if (order != SortOrder::none)
        arrow_.target = order == SortOrder::descending ? 1.0f : 0.0f;
    if (!animate)
        arrow_.snap(arrow_.target);
    reorder(animate && !style.reduced_motion);
    retarget(!animate);
}

Event Table::turn(int column, Feedback &feedback)
{
    if (column < 0 || column >= static_cast<int>(columns_.size()) ||
        !columns_[static_cast<std::size_t>(column)].sortable || rows_.empty())
        return Event::refused;
    const SortOrder first = first_order(columns_[static_cast<std::size_t>(column)]);
    SortOrder next = first;
    if (sort_column_ == column && sort_order_ == first)
        next = other_order(first);
    else if (sort_column_ == column && style.unsorted_step)
        next = SortOrder::none;
    sort_by(column, next, true);
    sorted_.trigger();
    // The cue says which way the list now runs.
    const float pitch =
        next == SortOrder::ascending ? 1.06f : (next == SortOrder::descending ? 0.94f : 1.0f);
    play_cue(feedback, style, style.sounds.change, bounds_.cx(), pitch);
    return Event::changed;
}

Event Table::cycle_sort(Feedback &feedback)
{
    if (in_header_)
        return turn(cursor_, feedback);
    if (sort_column_ < 0)
        return turn(std::max(next_sortable(-1, 1), 0), feedback);
    const TableColumn &column = columns_[static_cast<std::size_t>(sort_column_)];
    if (sort_order_ == first_order(column))
        return turn(sort_column_, feedback);
    int next = next_sortable(sort_column_, 1);
    if (next < 0)
        next = next_sortable(-1, 1);
    if (next == sort_column_)
        return turn(next, feedback);
    // Another column always starts with its first order.
    sort_column_ = -1;
    return turn(next, feedback);
}

Event Table::handle(const InputFrame &input, Feedback &feedback)
{
    exit_ = Direction::none;
    if (rows_.empty() || columns_.empty())
        return Event::none;
    const float x = bounds_.cx();
    const auto leave = [&](Pulse &pulse)
    {
        if (style.exits.allows(input.nav))
        {
            exit_ = input.nav;
            return Event::none;
        }
        return refuse(feedback, style, input, pulse, x);
    };

    if (in_header_)
    {
        if (input.nav == Direction::left || input.nav == Direction::right)
        {
            const int next = next_sortable(cursor_, input.nav == Direction::right ? 1 : -1);
            if (next < 0)
                return leave(column_.refusal());
            cursor_ = next;
            retarget(false);
            const float along = static_cast<float>(cursor_) /
                                std::max(static_cast<float>(columns_.size() - 1), 1.0f);
            play_cue(feedback, style, style.sounds.move, column_.rect(0.0f).x,
                     tween::lerp(0.97f, 1.05f, along));
            return Event::moved;
        }
        if (input.nav == Direction::down)
        {
            in_header_ = false;
            play_cue(feedback, style, style.sounds.move, x);
            return Event::moved;
        }
        if (input.nav == Direction::up)
            return leave(column_.refusal());
        if (input.is_pressed(Action::confirm))
            return turn(cursor_, feedback);
    }
    else
    {
        if (input.nav == Direction::up || input.nav == Direction::down)
        {
            const int next = place_ + (input.nav == Direction::down ? 1 : -1);
            if (next < 0 && style.header && next_sortable(-1, 1) >= 0)
            {
                // The header is the row above the first: the cursor starts on
                // the column the table is sorted by.
                in_header_ = true;
                if (sort_column_ >= 0)
                    cursor_ = sort_column_;
                retarget(false);
                play_cue(feedback, style, style.sounds.move, x, 1.08f);
                return Event::moved;
            }
            if (next < 0 || next >= static_cast<int>(rows_.size()))
                return leave(highlight_.refusal());
            place_ = next;
            retarget(false);
            const float along =
                static_cast<float>(place_) / std::max(static_cast<float>(rows_.size() - 1), 1.0f);
            play_cue(feedback, style, style.sounds.move, x, tween::lerp(1.05f, 0.95f, along));
            return Event::moved;
        }
        if (input.nav == Direction::left || input.nav == Direction::right)
            return leave(highlight_.refusal());
        if (input.is_pressed(Action::confirm))
        {
            const int row = focus();
            if (row < 0 || rows_[static_cast<std::size_t>(row)].disabled)
                return refuse(feedback, style, input, highlight_.refusal(), x);
            press_.trigger();
            if (style.multi_select)
            {
                const bool now = !selected_[static_cast<std::size_t>(row)];
                selected_[static_cast<std::size_t>(row)] = now;
                play_cue(feedback, style, style.sounds.change, x, now ? 1.06f : 0.94f);
                return Event::changed;
            }
            play_cue(feedback, style, style.sounds.activate, x);
            if (style.sounds.rumble > 0.0f)
                feedback.rumble(0.3f * style.sounds.rumble, 0.04f);
            return Event::activated;
        }
    }
    if (input.is_pressed(Action::back))
    {
        play_cue(feedback, style, style.sounds.cancel, x);
        return Event::cancelled;
    }
    return Event::none;
}

void Table::update(float dt)
{
    age_ += dt;
    // A knob (the row height, the panel) may have changed since the rows
    // were placed: their targets and the highlight's follow the style.
    for (std::size_t place = 0; place < order_.size(); ++place)
        tops_[static_cast<std::size_t>(order_[place])].target =
            static_cast<float>(place) * style.row_height;
    if (!rows_.empty() && !columns_.empty())
        retarget(false);
    highlight_.update(dt, style);
    column_.update(dt, style);
    scroll_.update(dt, std::max(style.omega(), 14.0f));
    active_amount_.target = active_ ? 1.0f : 0.0f;
    active_amount_.update(dt, 18.0f);
    header_amount_.target = in_header_ ? 1.0f : 0.0f;
    header_amount_.update(dt, 20.0f);
    arrow_.update(dt, 16.0f);
    // Rows travel a little slower than the focus does, so the eye can follow
    // them, and never so bouncy that two rows swap twice.
    const float omega = std::min(std::max(style.omega() * 0.6f, 9.0f), 60.0f);
    const float damping = std::max(style.damping(), 0.8f);
    for (tween::Bounce &top : tops_)
        top.update(dt, omega, damping);
    for (std::size_t i = 0; i < marks_.size(); ++i)
    {
        marks_[i].target = selected_[i] ? 1.0f : 0.0f;
        marks_[i].update(dt, 20.0f);
    }
    press_.update(dt, 10.0f);
    sorted_.update(dt, 5.0f);
}

Rect Table::row_rect(int row) const
{
    const Rect area = view();
    if (row < 0 || row >= static_cast<int>(tops_.size()))
        return {area.x, area.y, area.w, style.row_height};
    return {area.x, area.y + tops_[static_cast<std::size_t>(row)].value - scroll_.offset(), area.w,
            style.row_height};
}

void Table::draw_cells(Canvas &canvas, Painter &paint, const std::vector<Column> &columns,
                       const Rect &area, int row, int place, float focus, const Inks &resting) const
{
    const TableRow &data = rows_[static_cast<std::size_t>(row)];
    gfx::DrawList &list = canvas.list;
    Color ink = Highlight::text_color(style, style.highlight, focus, resting.text);
    Color quiet = gfx::mix(resting.muted, ink, focus * 0.6f);
    if (data.disabled)
    {
        ink = ink.with_alpha(0.42f);
        quiet = quiet.with_alpha(0.42f);
    }
    const float baseline = area.cy() + style.text_size * 0.35f;
    float x = area.x + style.padding;
    if (style.multi_select)
    {
        const float size = std::min(26.0f, area.h - 12.0f);
        Look look;
        look.disabled = data.disabled;
        paint.checkbox({x + 2.0f, area.cy() - size * 0.5f, size, size},
                       marks_[static_cast<std::size_t>(row)].value, look);
        x += style.mark_width;
    }
    if (style.rank)
    {
        char text[16];
        std::snprintf(text, sizeof(text), "%d", place + 1);
        // The podium reads in the full text colour, the rest quieter.
        draw_number(paint, list, text, x + (style.rank_width - 14.0f) * 0.5f, baseline,
                    style.text_size, place < 3 ? ink : quiet, gfx::Align::center);
    }
    for (std::size_t c = 0; c < columns_.size() && c < columns.size(); ++c)
    {
        const TableColumn &column = columns_[c];
        const Rect cell{columns[c].x, area.y, columns[c].w, area.h};
        if (cell.w <= 1.0f)
            continue;
        if (column.cell)
        {
            column.cell(canvas, cell, data, focus);
            continue;
        }
        if (c >= data.cells.size())
            continue;
        const std::string &text = data.cells[c].text;
        const float at = column.align == gfx::Align::left
                             ? cell.x
                             : (column.align == gfx::Align::right ? cell.x + cell.w : cell.cx());
        if (column.numeric)
            draw_number(paint, list, fit_number(paint, text, style.text_size, cell.w), at, baseline,
                        style.text_size, ink, column.align);
        else if (column.strong)
            paint.label(fit_label(paint, text, style.text_size, cell.w), at, baseline,
                        style.text_size, ink, column.align);
        else
            paint.body(fit_body(paint, text, style.text_size, cell.w), at, baseline,
                       style.text_size, quiet, column.align);
    }
}

void Table::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    if (style.panel)
        paint.panel(bounds_);
    if (columns_.empty())
        return;

    const Rect in = inner();
    const Rect area = view();
    const std::vector<Column> columns = layout();
    const Inks ink = inks(paint, style.panel);
    const float rows = static_cast<float>(rows_.size());
    const float content = rows * style.row_height;
    const float scroll = scroll_.offset();
    const bool overflow = content > area.h + 0.5f;
    const int count = static_cast<int>(order_.size());
    const Color mark =
        visible_on(theme, theme.focus.a > 0.6f ? theme.focus : theme.primary, style.panel);

    const auto entrance = [&](int place)
    {
        if (style.entrance_step <= 0.0f || style.reduced_motion)
            return tween::cubic_out(age_ / 0.2f);
        return tween::stagger(age_, place, style.entrance_step, 0.32f);
    };

    // ---- header ----
    if (style.header)
    {
        const Rect header = header_rect();
        const float size = style.header_size;
        const float baseline = header.cy() + size * 0.35f;
        if (sort_column_ >= 0 && sorted_.value > 0.01f)
        {
            const Column &column = columns[static_cast<std::size_t>(sort_column_)];
            paint.fill({column.x - 8.0f, header.y + 4.0f, column.w + 16.0f, header.h - 8.0f},
                       std::min(theme.radius, 8.0f), mark.with_alpha(0.3f * sorted_.value));
        }
        HighlightStyle look = style.highlight;
        if (look.kind != HighlightKind::ring)
            look.kind = HighlightKind::tint;
        column_.draw(canvas, style, look, header_amount_.value * active_amount_.value);
        if (style.rank)
        {
            const float x = in.x + style.padding + (style.multi_select ? style.mark_width : 0.0f);
            paint.label("#", x + (style.rank_width - 14.0f) * 0.5f, baseline, size, ink.muted,
                        gfx::Align::center);
        }
        for (std::size_t c = 0; c < columns_.size(); ++c)
        {
            const TableColumn &column = columns_[c];
            const Column &at = columns[c];
            if (at.w <= 1.0f)
                continue;
            const bool sorted = static_cast<int>(c) == sort_column_;
            constexpr float kArrow = 20.0f; // the arrow and its gap
            const std::string title =
                fit_label(paint, column.title, size, at.w - (sorted ? kArrow : 0.0f));
            const float width = paint.label_width(title, size);
            const Color color = sorted ? ink.text : ink.muted;
            // The arrow follows the title, on the side the column is not
            // aligned to, so right-aligned figures keep their edge.
            float text_x = at.x;
            float arrow_x = at.x + width + 12.0f;
            if (column.align == gfx::Align::right)
            {
                text_x = at.x + at.w;
                arrow_x = at.x + at.w - width - 12.0f;
            }
            else if (column.align == gfx::Align::center)
            {
                text_x = at.x + at.w * 0.5f - (sorted ? kArrow * 0.5f : 0.0f);
                arrow_x = text_x + width * 0.5f + 12.0f;
            }
            paint.label(title, text_x, baseline, size, color, column.align);
            if (sorted)
                list.triangle({arrow_x - 6.0f, header.cy() - 5.0f, 12.0f, 10.0f}, mark, 0.0f,
                              arrow_.value * kPi);
        }
        list.rounded_rect({in.x + 4.0f, header.y + header.h - 1.5f, in.w - 8.0f, 1.5f}, 0.0f,
                          rule_color(paint, style.panel, 1.6f));
    }

    // ---- rows ----
    const auto visibility = [&](const Rect &row)
    { return row_visibility(row, area, style.edge_fade); };
    const float bleed = style.highlight.kind == HighlightKind::glow ? 30.0f : 10.0f;
    // Above, the header is in the way; below, a ring may need the room.
    list.push_clip({in.x - bleed, area.y - 1.0f, in.w + 2.0f * bleed,
                    area.h + 1.0f + (overflow ? 2.0f : bleed)});

    const int pinned = pinned_row();
    for (int place = 0; place < count; ++place)
    {
        const Rect slot{in.x, area.y + static_cast<float>(place) * style.row_height - scroll, in.w,
                        style.row_height};
        const float alpha = visibility(slot) * entrance(place);
        if (alpha <= 0.0f || slot.y > area.y + area.h || slot.y + slot.h < area.y)
            continue;
        if (style.lines == TableLines::zebra && place % 2 == 1)
            paint.fill(slot, std::min(theme.radius, 8.0f), ink.muted.with_alpha(0.1f * alpha));
        else if (style.lines == TableLines::dividers && place + 1 < count)
            list.rounded_rect({slot.x + style.padding, slot.y + slot.h - 0.75f,
                               slot.w - 2.0f * style.padding, 1.5f},
                              0.0f, rule_color(paint, style.panel, alpha));
    }
    if (pinned >= 0)
    {
        // The pinned row is also marked where it stands among the others.
        const Rect row = row_rect(pinned);
        const float alpha = visibility(row) * entrance(place_of(pinned));
        if (alpha > 0.0f && row.y < area.y + area.h && row.y + row.h > area.y)
            paint.fill(row, std::min(theme.radius, 8.0f), mark.with_alpha(0.16f * alpha));
    }

    // The highlight is kept in content space; bring it to the screen here.
    list.push_transform(1.0f, 0.0f, 0.0f, in.x, area.y - scroll);
    if (count > 0)
    {
        HighlightStyle look = style.highlight;
        look.grow -= 2.0f * press_.value;
        // In the header the rows keep a faint mark of where the focus was.
        draw_focus(canvas, style, highlight_, look,
                   active_amount_.value * (1.0f - header_amount_.value), entrance(place_));
    }
    list.pop_transform();

    for (int place = 0; place < count; ++place)
    {
        const int row = order_[static_cast<std::size_t>(place)];
        const Rect rect = row_rect(row);
        const float shown = entrance(place);
        const float alpha = visibility(rect) * shown;
        if (alpha <= 0.0f || rect.y > area.y + area.h || rect.y + rect.h < area.y)
            continue;
        list.push_opacity(alpha);
        const float slide = style.reduced_motion ? 0.0f : 14.0f * (1.0f - shown);
        list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, slide);
        const float focus = highlight_.coverage({0.0f, tops_[static_cast<std::size_t>(row)].value,
                                                 in.w, style.row_height}) *
                            active_amount_.value * (1.0f - header_amount_.value);
        draw_cells(canvas, paint, columns, rect, row, place, focus, ink);
        list.pop_transform();
        list.pop_opacity();
    }
    list.pop_clip();

    // ---- the pinned row ----
    if (pinned >= 0)
    {
        const Rect rect = pinned_rect();
        const float radius = std::min(paint.control_radius(rect), 12.0f);
        list.push_opacity(tween::cubic_out(age_ / 0.3f));
        paint.well(rect, radius, theme.surface_high);
        const float inset = std::max(theme.border, 0.0f) + 4.0f;
        paint.fill({rect.x + inset, rect.y + inset + 4.0f, 5.0f, rect.h - 2.0f * inset - 8.0f},
                   theme.corner == Corner::round && theme.radius >= 2.0f ? 2.5f : 0.0f,
                   visible_on(theme, theme.focus.a > 0.6f ? theme.focus : theme.primary, true));
        // A well may be lighter or darker than the panel: take the colour
        // that reads on it, whatever the theme's text colour is.
        const Color ground = ground_color(theme, true);
        const Color face = gfx::mix(
            ground, theme.surface_high,
            theme.style == SurfaceStyle::bevel ? 1.0f : tween::clamp01(theme.surface_high.a));
        const auto light = [](Color c) { return 0.299f * c.r + 0.587f * c.g + 0.114f * c.b; };
        const Color on_well = std::fabs(light(face) - light(theme.text)) < 0.3f
                                  ? Painter::on(Color{face.r, face.g, face.b, 1.0f})
                                  : theme.text;
        const Inks resting{on_well, gfx::mix(on_well, theme.text_muted, 0.5f)};
        draw_cells(canvas, paint, columns, rect, pinned, place_of(pinned), 0.0f, resting);
        list.pop_opacity();
    }

    if (style.scroll_thumb && overflow)
    {
        const float track = area.h - 12.0f;
        const float size = std::max(track * area.h / content, 30.0f);
        const float at = scroll / std::max(content - area.h, 1.0f);
        const float x = in.x + in.w + 3.0f;
        list.rounded_rect({x, area.y + 6.0f, 4.0f, track}, 2.0f, ink.muted.with_alpha(0.16f));
        list.rounded_rect({x, area.y + 6.0f + (track - size) * tween::clamp01(at), 4.0f, size},
                          2.0f, ink.muted.with_alpha(0.7f));
    }
}

} // namespace hui::ui
