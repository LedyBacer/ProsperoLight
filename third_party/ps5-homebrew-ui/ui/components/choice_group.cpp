// ps5-homebrew-ui - Components: CheckGroup and RadioGroup.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/choice_group.hpp"

#include <algorithm>
#include <cmath>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

constexpr int kEdge = -2; // "there is nothing in that direction"

bool stroke_only(SurfaceStyle style)
{
    return style == SurfaceStyle::outline || style == SurfaceStyle::glow;
}

} // namespace

void ChoiceGroup::set_items(std::vector<ChoiceItem> items)
{
    items_ = std::move(items);
    on_.assign(items_.size(), 0);
    shown_.assign(items_.size(), tween::Spring{});
    const int count = static_cast<int>(items_.size());
    focus_ = std::clamp(focus_, has_header() ? -1 : 0, std::max(count - 1, 0));
    placed_ = false;
}

int ChoiceGroup::columns() const
{
    const int count = static_cast<int>(items_.size());
    if (count == 0 || style.layout == ChoiceLayout::vertical)
        return 1;
    if (style.layout == ChoiceLayout::horizontal)
        return count;
    return std::clamp(style.columns, 1, count);
}

float ChoiceGroup::cell_height() const
{
    const bool described = std::any_of(items_.begin(), items_.end(), [](const ChoiceItem &item)
                                       { return !item.description.empty(); });
    return described ? style.described_height : style.row_height;
}

Rect ChoiceGroup::local_rect(int index) const
{
    float top = title_.empty() ? 0.0f : style.title_size + style.title_gap;
    if (index < 0)
        return {0.0f, top, bounds_.w, style.row_height};
    if (has_header())
        top += style.row_height + style.gap;
    const int cols = columns();
    const float cell =
        (bounds_.w - static_cast<float>(cols - 1) * style.column_gap) / static_cast<float>(cols);
    const float height = cell_height();
    return {static_cast<float>(index % cols) * (cell + style.column_gap),
            top + static_cast<float>(index / cols) * (height + style.gap), cell, height};
}

Rect ChoiceGroup::item_rect(int index) const
{
    const Rect local = local_rect(index);
    return {bounds_.x + local.x, bounds_.y + local.y, local.w, local.h};
}

float ChoiceGroup::item_x(int index) const
{
    return items_.empty() ? bounds_.cx() : item_rect(index).cx();
}

float ChoiceGroup::preferred_height() const
{
    float height = title_.empty() ? 0.0f : style.title_size + style.title_gap;
    if (has_header())
        height += style.row_height + style.gap;
    const int count = static_cast<int>(items_.size());
    if (count == 0)
        return height;
    const int rows = (count + columns() - 1) / columns();
    return height + static_cast<float>(rows) * cell_height() +
           static_cast<float>(rows - 1) * style.gap;
}

void ChoiceGroup::set_focus(int index, bool snap)
{
    const int count = static_cast<int>(items_.size());
    focus_ = std::clamp(index, has_header() ? -1 : 0, std::max(count - 1, 0));
    if (snap)
        placed_ = false;
}

// The items form a grid of `columns()` cells per row, with the "select all"
// row above it. A direction that leaves the grid wraps (along the layout's
// own axis), leaves through an exit, or is refused.
bool ChoiceGroup::navigate(const InputFrame &input, Feedback &feedback, bool quiet_move,
                           Event *event)
{
    exit_ = Direction::none;
    if (input.nav == Direction::none)
        return false;
    const Direction d = input.nav;
    const int count = static_cast<int>(items_.size());
    if (count == 0)
    {
        if (style.exits.allows(d))
            exit_ = d;
        *event = Event::none;
        return true;
    }
    const int cols = columns();
    const int last_row = (count - 1) / cols;
    int next = kEdge;
    if (focus_ < 0)
    {
        if (d == Direction::down)
            next = std::min(return_column_, count - 1);
    }
    else
    {
        const int col = focus_ % cols;
        const int row = focus_ / cols;
        if (d == Direction::left && col > 0)
            next = focus_ - 1;
        else if (d == Direction::right && col < cols - 1 && focus_ + 1 < count)
            next = focus_ + 1;
        else if (d == Direction::up && row > 0)
            next = focus_ - cols;
        else if (d == Direction::up && has_header())
            next = -1;
        else if (d == Direction::down && focus_ + cols < count)
            next = focus_ + cols;
        else if (d == Direction::down && row < last_row)
            next = count - 1; // a short last row: its last item is the nearest
    }

    if (next == kEdge && style.wrap && !input.nav_repeat)
    {
        if (style.layout == ChoiceLayout::horizontal)
        {
            if (d == Direction::right)
                next = 0;
            else if (d == Direction::left)
                next = count - 1;
        }
        else
        {
            const int col = focus_ < 0 ? std::min(return_column_, cols - 1) : focus_ % cols;
            if (d == Direction::down)
            {
                next = has_header() ? -1 : col;
            }
            else if (d == Direction::up)
            {
                next = last_row * cols + col;
                while (next >= count)
                    next -= cols;
            }
        }
        if (next == focus_)
            next = kEdge;
    }

    if (next == kEdge)
    {
        if (style.exits.allows(d))
        {
            exit_ = d;
            *event = Event::none;
        }
        else
        {
            *event = refuse(feedback, style, input, highlight_.refusal(), item_x(focus_));
        }
        return true;
    }
    if (next < 0 && focus_ >= 0)
        return_column_ = focus_ % cols;
    focus_ = next;
    if (!quiet_move)
    {
        const float along =
            count > 1 ? static_cast<float>(std::max(focus_, 0)) / static_cast<float>(count - 1)
                      : 0.0f;
        play_cue(feedback, style, style.sounds.move, item_x(focus_),
                 tween::lerp(1.05f, 0.95f, along));
    }
    *event = Event::moved;
    return true;
}

Event ChoiceGroup::press_back(const InputFrame &input, Feedback &feedback)
{
    if (!input.is_pressed(Action::back))
        return Event::none;
    play_cue(feedback, style, style.sounds.cancel, bounds_.cx());
    return Event::cancelled;
}

void ChoiceGroup::update(float dt)
{
    active_amount_.target = active_ ? 1.0f : 0.0f;
    active_amount_.update(dt, 18.0f);
    press_.update(dt, 10.0f);
    if (items_.empty())
        return;

    // The highlight lives in the group's own space, so moving the group (or
    // restyling it) never makes it lag behind.
    const Rect target = local_rect(focus_);
    highlight_.target(target);
    if (!placed_)
        highlight_.snap(target);
    highlight_.update(dt, style);

    const float omega = std::max(style.omega(), 14.0f) * 1.3f;
    int enabled = 0;
    int enabled_on = 0;
    for (std::size_t i = 0; i < items_.size(); ++i)
    {
        shown_[i].target = on_[i] != 0 ? 1.0f : 0.0f;
        if (!placed_)
            shown_[i].snap(shown_[i].target);
        shown_[i].update(dt, omega);
        if (!items_[i].disabled)
        {
            ++enabled;
            enabled_on += on_[i] != 0 ? 1 : 0;
        }
    }
    all_.target = enabled > 0 && enabled_on == enabled ? 1.0f : 0.0f;
    some_.target = enabled_on > 0 && enabled_on < enabled ? 1.0f : 0.0f;
    if (!placed_)
    {
        all_.snap(all_.target);
        some_.snap(some_.target);
    }
    all_.update(dt, omega);
    some_.update(dt, omega);
    placed_ = true;
}

void ChoiceGroup::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const Color resting = style.on_page ? paint.page_text() : theme.text;
    const Color quiet = style.on_page ? paint.page_text_muted() : theme.text_muted;
    if (!title_.empty())
        paint.label(fit_label(paint, title_, style.title_size, bounds_.w), bounds_.x,
                    bounds_.y + style.title_size * 0.82f, style.title_size, quiet);
    if (items_.empty())
        return;

    const float active = active_amount_.value;
    {
        HighlightStyle look = style.highlight;
        look.grow -= 2.0f * press_.value; // a press pushes it in for a moment
        list.push_transform(1.0f, 0.0f, 0.0f, bounds_.x, bounds_.y);
        highlight_.draw(canvas, style, look, tween::lerp(style.idle_highlight, 1.0f, active));
        list.pop_transform();
    }

    const auto row = [&](int index, const std::string &label, const std::string &description,
                         float value, float dash, bool disabled)
    {
        const Rect local = local_rect(index);
        const Rect cell{bounds_.x + local.x, bounds_.y + local.y, local.w, local.h};
        const float focus = highlight_.coverage(local) * active;
        if (disabled)
            list.push_opacity(0.42f);

        const float size = style.box_size;
        const Rect box{cell.x + style.padding, cell.cy() - size * 0.5f, size, size};
        const bool pressed = index == focus_ && press_.value > 0.01f && !style.reduced_motion;
        if (pressed)
            list.push_transform(1.0f - 0.12f * press_.value, box.cx(), box.cy(), 0.0f, 0.0f);
        if (radio_)
            paint.radio(box, value, {});
        else
            paint.checkbox(box, value, {});
        if (dash > 0.01f)
        {
            // "Some, not all": a dash that grows from the middle of the box.
            const float half = size * 0.24f * dash;
            list.line(box.cx() - half, box.cy(), box.cx() + half, box.cy(),
                      std::max(3.0f, size * 0.11f),
                      (stroke_only(theme.style) ? theme.accent : theme.text).with_alpha(dash));
        }
        if (pressed)
            list.pop_transform();

        const Color ink = Highlight::text_color(style, style.highlight, focus, resting);
        const float left = box.x + size + style.box_gap;
        const float room = std::max(cell.x + cell.w - style.padding - left, 30.0f);
        if (description.empty())
        {
            paint.label(fit_label(paint, label, style.label_size, room), left,
                        cell.cy() + style.label_size * 0.35f, style.label_size, ink);
        }
        else
        {
            const float block = style.label_size + style.description_size * 1.25f;
            const float first = cell.cy() - block * 0.5f + style.label_size * 0.84f;
            paint.label(fit_label(paint, label, style.label_size, room), left, first,
                        style.label_size, ink);
            paint.body(fit_body(paint, description, style.description_size, room), left,
                       first + style.description_size * 1.3f, style.description_size,
                       gfx::mix(quiet, ink, focus * 0.6f));
        }
        if (disabled)
            list.pop_opacity();
    };

    if (has_header())
        row(-1, style.select_all_label, std::string(), all_.value,
            some_.value * (1.0f - all_.value), false);
    for (std::size_t i = 0; i < items_.size(); ++i)
        row(static_cast<int>(i), items_[i].label, items_[i].description, shown_[i].value, 0.0f,
            items_[i].disabled);
}

// ---- CheckGroup -------------------------------------------------------------

void CheckGroup::set_checked(int index, bool checked)
{
    if (index < 0 || index >= static_cast<int>(items_.size()))
        return;
    on_[static_cast<std::size_t>(index)] = checked ? 1 : 0;
}

int CheckGroup::checked_count() const
{
    return static_cast<int>(std::count_if(on_.begin(), on_.end(), [](char v) { return v != 0; }));
}

void CheckGroup::set_all(bool checked)
{
    for (std::size_t i = 0; i < items_.size(); ++i)
    {
        if (!items_[i].disabled)
            on_[i] = checked ? 1 : 0;
    }
}

Event CheckGroup::handle(const InputFrame &input, Feedback &feedback)
{
    Event event = Event::none;
    if (navigate(input, feedback, false, &event))
        return event;
    if (input.is_pressed(Action::confirm) && !items_.empty())
    {
        const float x = item_x(focus_);
        bool now_on = false;
        if (focus_ < 0)
        {
            // Everything on becomes everything off; anything less becomes all.
            int enabled = 0;
            int enabled_on = 0;
            for (std::size_t i = 0; i < items_.size(); ++i)
            {
                if (items_[i].disabled)
                    continue;
                ++enabled;
                enabled_on += on_[i] != 0 ? 1 : 0;
            }
            if (enabled == 0)
                return refuse(feedback, style, input, highlight_.refusal(), x);
            now_on = enabled_on < enabled;
            set_all(now_on);
        }
        else
        {
            if (items_[static_cast<std::size_t>(focus_)].disabled)
                return refuse(feedback, style, input, highlight_.refusal(), x);
            char &value = on_[static_cast<std::size_t>(focus_)];
            value = value != 0 ? 0 : 1;
            now_on = value != 0;
        }
        changed_ = focus_;
        press_.trigger();
        play_cue(feedback, style, style.sounds.change, x,
                 style.pitch_by_state ? (now_on ? 1.06f : 0.94f) : 1.0f);
        return Event::changed;
    }
    return press_back(input, feedback);
}

// ---- RadioGroup -------------------------------------------------------------

void RadioGroup::set_selected(int index)
{
    std::fill(on_.begin(), on_.end(), 0);
    if (index >= 0 && index < static_cast<int>(on_.size()))
        on_[static_cast<std::size_t>(index)] = 1;
}

int RadioGroup::selected() const
{
    for (std::size_t i = 0; i < on_.size(); ++i)
    {
        if (on_[i] != 0)
            return static_cast<int>(i);
    }
    return -1;
}

Event RadioGroup::select(int index, const InputFrame &input, Feedback &feedback)
{
    const float x = item_x(focus_);
    if (items_[static_cast<std::size_t>(focus_)].disabled)
        return refuse(feedback, style, input, highlight_.refusal(), x);
    set_selected(index);
    changed_ = focus_;
    press_.trigger();
    play_cue(feedback, style, style.sounds.change, x,
             style.pitch_by_state ? (index >= 0 ? 1.06f : 0.94f) : 1.0f);
    return Event::changed;
}

Event RadioGroup::handle(const InputFrame &input, Feedback &feedback)
{
    Event event = Event::none;
    if (navigate(input, feedback, style.select_on_move, &event))
    {
        if (event != Event::moved || !style.select_on_move)
            return event;
        // The move was silent: either the selection follows it and speaks,
        // or (a disabled item, the selected one) the move cue plays after all.
        if (!items_[static_cast<std::size_t>(focus_)].disabled && selected() != focus_)
            return select(focus_, input, feedback);
        play_cue(feedback, style, style.sounds.move, item_x(focus_));
        return Event::moved;
    }
    if (input.is_pressed(Action::confirm) && !items_.empty())
    {
        if (selected() != focus_)
            return select(focus_, input, feedback);
        if (style.allow_none)
            return select(-1, input, feedback);
        if (!style.reduced_motion)
            press_.trigger(0.6f);
        return Event::none;
    }
    return press_back(input, feedback);
}

} // namespace hui::ui
