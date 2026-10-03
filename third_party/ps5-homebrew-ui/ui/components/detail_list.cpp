// ps5-homebrew-ui - Component: DetailList.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/detail_list.hpp"

#include "ui/components/overlay.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

constexpr float kCopyMark = 26.0f; // room a focusable pair keeps for its mark

// Two offset squares: "this value can be taken".
void draw_copy_mark(gfx::DrawList &list, float cx, float cy, Color color)
{
    const Color clear{color.r, color.g, color.b, 0.0f};
    list.bordered_rect({cx - 7.0f, cy - 7.0f, 10.0f, 10.0f}, 2.0f, clear, 1.5f, color);
    list.bordered_rect({cx - 3.0f, cy - 3.0f, 10.0f, 10.0f}, 2.0f, clear, 1.5f, color);
}

} // namespace

void DetailList::set_items(std::vector<DetailItem> items)
{
    items_ = std::move(items);
    lines_.assign(items_.size(), 1);
    const int count = static_cast<int>(items_.size());
    if (focus_ >= count || (focus_ >= 0 && !items_[static_cast<std::size_t>(focus_)].focusable))
        focus_ = -1;
    if (focus_ < 0)
    {
        for (int i = 0; i < count; ++i)
        {
            if (items_[static_cast<std::size_t>(i)].focusable &&
                !items_[static_cast<std::size_t>(i)].header)
            {
                focus_ = i;
                break;
            }
        }
    }
    relayout();
    retarget(true);
    settle_ = true;
}

void DetailList::set_bounds(const Rect &bounds)
{
    bounds_ = bounds;
    relayout();
    retarget(true);
    settle_ = true;
}

void DetailList::set_focus(int index, bool snap)
{
    if (index < 0 || index >= static_cast<int>(items_.size()) ||
        !items_[static_cast<std::size_t>(index)].focusable)
        return;
    focus_ = index;
    retarget(snap);
}

void DetailList::enter()
{
    age_ = 0.0f;
}

Rect DetailList::inner() const
{
    if (style.panel)
        return bounds_.inset(style.panel_padding);
    // Without a panel's padding the scroll thumb needs room of its own.
    return {bounds_.x, bounds_.y, bounds_.w - (style.scroll_thumb ? 10.0f : 0.0f), bounds_.h};
}

float DetailList::line_height() const
{
    return style.value_size * 1.26f;
}

// The width a value may take, from the pair's own width.
float DetailList::value_room(int index) const
{
    const DetailItem &item = items_[static_cast<std::size_t>(index)];
    const float inside = rects_[static_cast<std::size_t>(index)].w - 2.0f * style.padding;
    if (style.layout != DetailLayout::rows)
        return std::max(inside, 20.0f);
    const float mark = item.focusable ? kCopyMark : 0.0f;
    return std::max(inside - mark - inside * style.label_share - 14.0f, 20.0f);
}

int DetailList::count_lines(const Painter &paint, int index) const
{
    const DetailItem &item = items_[static_cast<std::size_t>(index)];
    if (item.header || item.numeric || style.max_lines <= 1 || static_cast<bool>(value))
        return 1;
    const std::vector<std::string> lines =
        wrap_body(paint, item.value, style.value_size, value_room(index), style.max_lines);
    return std::max(static_cast<int>(lines.size()), 1);
}

void DetailList::measure(const Fonts &fonts)
{
    // A painter only measures here; the list it is given stays empty.
    gfx::DrawList scratch;
    const Painter paint(scratch, fonts, style.theme);
    relayout(); // the widths, which do not depend on the line counts
    for (std::size_t i = 0; i < items_.size(); ++i)
        lines_[i] = count_lines(paint, static_cast<int>(i));
    relayout();
    retarget(true);
    settle_ = true;
}

void DetailList::relayout()
{
    const Rect in = inner();
    const std::size_t count = items_.size();
    rects_.resize(count);
    if (lines_.size() != count)
        lines_.assign(count, 1);
    const auto height = [&](std::size_t i)
    {
        const float text = static_cast<float>(std::max(lines_[i], 1)) * line_height();
        if (style.layout == DetailLayout::rows)
            return std::max(style.row_height, text + 2.0f * style.padding_y);
        return style.label_size * 1.15f + style.stack_gap + text + 2.0f * style.padding_y;
    };
    float y = 0.0f;
    if (style.layout == DetailLayout::grid)
    {
        const int columns = std::max(style.columns, 1);
        const float width = (in.w - style.column_gap * static_cast<float>(columns - 1)) /
                            static_cast<float>(columns);
        std::size_t i = 0;
        while (i < count)
        {
            if (items_[i].header)
            {
                rects_[i] = {0.0f, y, in.w, style.header_height};
                y += style.header_height + style.gap;
                ++i;
                continue;
            }
            // One row of the grid is as tall as its tallest pair.
            std::size_t end = i;
            float tallest = 0.0f;
            while (end < count && !items_[end].header &&
                   end - i < static_cast<std::size_t>(columns))
            {
                tallest = std::max(tallest, height(end));
                ++end;
            }
            for (std::size_t k = i; k < end; ++k)
                rects_[k] = {static_cast<float>(k - i) * (width + style.column_gap), y, width,
                             tallest};
            y += tallest + style.gap;
            i = end;
        }
    }
    else
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            const float h = items_[i].header ? style.header_height : height(i);
            rects_[i] = {0.0f, y, in.w, h};
            y += h + style.gap;
        }
    }
    content_ = std::max(y - style.gap, 0.0f);
}

void DetailList::retarget(bool snap)
{
    const Rect in = inner();
    if (focus_ >= 0 && focus_ < static_cast<int>(rects_.size()))
    {
        const Rect &r = rects_[static_cast<std::size_t>(focus_)];
        // Reveal the section title above the first pair of a section too.
        const bool under_header = focus_ > 0 && items_[static_cast<std::size_t>(focus_ - 1)].header;
        const float top = under_header ? rects_[static_cast<std::size_t>(focus_ - 1)].y : r.y;
        scroll_.reveal(top, r.y + r.h, in.h, 6.0f, content_);
        highlight_.target(r);
        if (snap)
            highlight_.snap(r);
    }
    else
    {
        scroll_.position.target =
            std::clamp(scroll_.position.target, 0.0f, std::max(content_ - in.h, 0.0f));
    }
    if (snap)
        scroll_.position.snap(scroll_.position.target);
}

// The focusable pair nearest to the focused one in a direction, or -1.
int DetailList::neighbour(Direction direction) const
{
    if (focus_ < 0)
        return -1;
    const Rect &from = rects_[static_cast<std::size_t>(focus_)];
    int best = -1;
    float best_score = 0.0f;
    for (std::size_t i = 0; i < items_.size(); ++i)
    {
        if (static_cast<int>(i) == focus_ || !items_[i].focusable || items_[i].header)
            continue;
        const Rect &to = rects_[i];
        float along = 0.0f;
        float across = 0.0f;
        switch (direction)
        {
        case Direction::up:
            along = from.y - (to.y + to.h);
            across = std::fabs(to.cx() - from.cx());
            break;
        case Direction::down:
            along = to.y - (from.y + from.h);
            across = std::fabs(to.cx() - from.cx());
            break;
        case Direction::left:
            along = from.x - (to.x + to.w);
            across = std::fabs(to.cy() - from.cy());
            break;
        case Direction::right:
            along = to.x - (from.x + from.w);
            across = std::fabs(to.cy() - from.cy());
            break;
        case Direction::none:
            return -1;
        }
        // Sideways moves stay in their row; a pair that overlaps the focused
        // one along the axis of travel is not "in that direction".
        const bool sideways = direction == Direction::left || direction == Direction::right;
        if (along < -0.5f || (sideways && across > 1.0f))
            continue;
        const float score = along + 3.0f * across;
        if (best < 0 || score < best_score)
        {
            best = static_cast<int>(i);
            best_score = score;
        }
    }
    return best;
}

Event DetailList::handle(const InputFrame &input, Feedback &feedback)
{
    exit_ = Direction::none;
    if (items_.empty())
        return Event::none;
    const float x = bounds_.cx();
    if (input.nav != Direction::none)
    {
        const auto leave = [&]()
        {
            if (style.exits.allows(input.nav))
            {
                exit_ = input.nav;
                return Event::none;
            }
            return refuse(feedback, style, input, highlight_.refusal(), x);
        };
        if (focus_ < 0)
        {
            // Nothing to focus: up and down read the sheet by scrolling it.
            if (input.nav != Direction::up && input.nav != Direction::down)
                return leave();
            const float limit = std::max(content_ - inner().h, 0.0f);
            const float step = style.row_height * 1.5f;
            const float wanted =
                std::clamp(scroll_.position.target + (input.nav == Direction::down ? step : -step),
                           0.0f, limit);
            if (std::fabs(wanted - scroll_.position.target) < 1.0f)
                return leave();
            scroll_.position.target = wanted;
            play_cue(feedback, style, style.sounds.move, x, 1.0f, 0.7f);
            return Event::moved;
        }
        const int next = neighbour(input.nav);
        if (next < 0)
            return leave();
        focus_ = next;
        retarget(false);
        const float along =
            content_ > 0.0f ? rects_[static_cast<std::size_t>(focus_)].y / content_ : 0.0f;
        play_cue(feedback, style, style.sounds.move, x, tween::lerp(1.05f, 0.95f, along));
        return Event::moved;
    }
    if (input.is_pressed(Action::confirm) && focus_ >= 0)
    {
        press_.trigger();
        play_cue(feedback, style, style.sounds.activate, x);
        return Event::activated;
    }
    if (input.is_pressed(Action::back))
    {
        play_cue(feedback, style, style.sounds.cancel, x);
        return Event::cancelled;
    }
    return Event::none;
}

void DetailList::update(float dt)
{
    age_ += dt;
    // Line counts noted by the last draw may have changed the heights.
    relayout();
    retarget(settle_);
    settle_ = false;
    highlight_.update(dt, style);
    scroll_.update(dt, std::max(style.omega(), 14.0f));
    active_amount_.target = active_ ? 1.0f : 0.0f;
    active_amount_.update(dt, 18.0f);
    press_.update(dt, 5.0f);
}

Rect DetailList::item_rect(int index) const
{
    const Rect in = inner();
    if (index < 0 || index >= static_cast<int>(rects_.size()))
        return in;
    const Rect &r = rects_[static_cast<std::size_t>(index)];
    return {in.x + r.x, in.y + r.y - scroll_.offset(), r.w, r.h};
}

void DetailList::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    if (style.panel)
        paint.panel(bounds_);
    if (items_.empty() || rects_.size() != items_.size())
        return;

    const Rect in = inner();
    const Inks ink = inks(paint, style.panel);
    const bool overflow = content_ > in.h + 0.5f;
    const bool rows = style.layout == DetailLayout::rows;
    const float lh = line_height();
    const int count = static_cast<int>(items_.size());
    const Color mark =
        visible_on(theme, theme.focus.a > 0.6f ? theme.focus : theme.primary, style.panel);

    const auto entrance = [&](int index)
    {
        if (style.reduced_motion)
            return tween::cubic_out(age_ / 0.2f);
        return tween::stagger(age_, index, 0.025f, 0.32f);
    };
    const auto visibility = [&](const Rect &r) { return overflow ? row_visibility(r, in) : 1.0f; };

    const float bleed = style.highlight.kind == HighlightKind::glow ? 30.0f : 10.0f;
    const float above = overflow ? 2.0f : bleed;
    list.push_clip({in.x - bleed, in.y - above, in.w + 2.0f * bleed, in.h + 2.0f * above});

    if (focus_ >= 0)
    {
        list.push_transform(1.0f, 0.0f, 0.0f, in.x, in.y - scroll_.offset());
        HighlightStyle look = style.highlight;
        look.grow -= 2.0f * std::min(press_.value * 2.0f, 1.0f);
        draw_focus(canvas, style, highlight_, look, active_amount_.value, entrance(focus_));
        list.pop_transform();
    }

    for (int i = 0; i < count; ++i)
    {
        const DetailItem &item = items_[static_cast<std::size_t>(i)];
        const Rect r = item_rect(i);
        const float shown = entrance(i);
        const float alpha = visibility(r) * shown;
        if (alpha <= 0.0f || r.y > in.y + in.h || r.y + r.h < in.y)
            continue;
        list.push_opacity(alpha);
        list.push_transform(1.0f, 0.0f, 0.0f, 0.0f,
                            style.reduced_motion ? 0.0f : 12.0f * (1.0f - shown));

        if (item.header)
        {
            const float baseline = r.y + r.h - 14.0f;
            const float width = paint.label(
                fit_label(paint, item.label, style.header_size, r.w - 2.0f * style.padding),
                r.x + style.padding, baseline, style.header_size, ink.muted);
            const float from = r.x + style.padding + width + 14.0f;
            const float to = r.x + r.w - style.padding;
            if (to - from > 20.0f)
                list.rounded_rect({from, baseline - style.header_size * 0.35f, to - from, 1.5f},
                                  0.0f, rule_color(paint, style.panel));
            list.pop_transform();
            list.pop_opacity();
            continue;
        }

        const Rect &slot = rects_[static_cast<std::size_t>(i)];
        const float focus = focus_ == i ? highlight_.coverage(slot) * active_amount_.value : 0.0f;
        const Color strong = Highlight::text_color(style, style.highlight, focus, ink.text);
        const Color quiet = gfx::mix(ink.muted, strong, focus * 0.6f);
        const float flash = focus_ == i ? std::min(press_.value * 1.6f, 1.0f) : 0.0f;
        Color value_ink = item.color.a > 0.0f ? visible_on(theme, item.color, style.panel) : strong;
        if (style.highlight.kind == HighlightKind::fill)
            value_ink = gfx::mix(value_ink, strong, focus);
        else
            value_ink = gfx::mix(value_ink, mark, flash);

        const float left = r.x + style.padding;
        float right = r.x + r.w - style.padding;
        if (item.focusable)
        {
            const float cy = rows ? r.cy() : r.y + style.padding_y + style.label_size * 0.5f;
            draw_copy_mark(list, right - 9.0f, cy, gfx::mix(quiet.with_alpha(0.7f), mark, flash));
            if (rows)
                right -= kCopyMark;
        }

        // Where the value goes, and how wide it may be.
        const float label_room = rows ? (r.w - 2.0f * style.padding) * style.label_share
                                      : right - left - (item.focusable ? kCopyMark : 0.0f);
        const float room = value_room(i);
        const float value_left = rows ? right - room : left;
        const bool one_line = item.numeric || style.max_lines <= 1 || static_cast<bool>(value);
        std::vector<std::string> lines;
        if (!one_line)
            lines = wrap_body(paint, item.value, style.value_size, room, style.max_lines);
        const int line_count = one_line ? 1 : std::max(static_cast<int>(lines.size()), 1);
        lines_[static_cast<std::size_t>(i)] = line_count;

        const float text_h = static_cast<float>(line_count) * lh;
        float first = 0.0f; // baseline of the value's first line
        if (rows)
        {
            first = r.cy() - text_h * 0.5f + lh * 0.5f + style.value_size * 0.35f;
            paint.label(fit_label(paint, item.label, style.label_size, label_room), left,
                        r.cy() - text_h * 0.5f + lh * 0.5f + style.label_size * 0.35f,
                        style.label_size, quiet);
        }
        else
        {
            const float top = r.y + style.padding_y;
            paint.label(fit_label(paint, item.label, style.label_size, label_room), left,
                        top + style.label_size * 0.86f, style.label_size, quiet);
            first = top + style.label_size * 1.15f + style.stack_gap + lh * 0.5f +
                    style.value_size * 0.35f;
        }
        const gfx::Align align = rows ? gfx::Align::right : gfx::Align::left;
        const float at = rows ? right : value_left;
        if (value)
        {
            value(canvas, {value_left, first - style.value_size * 0.35f - lh * 0.5f, room, lh},
                  item, i, focus);
        }
        else if (item.numeric)
        {
            draw_number(paint, list, fit_number(paint, item.value, style.value_size, room), at,
                        first, style.value_size, value_ink, align);
        }
        else if (one_line)
        {
            paint.body(fit_body(paint, item.value, style.value_size, room), at, first,
                       style.value_size, value_ink, align);
        }
        else
        {
            for (std::size_t k = 0; k < lines.size(); ++k)
                paint.body(lines[k], at, first + static_cast<float>(k) * lh, style.value_size,
                           value_ink, align);
        }

        const bool last = i + 1 >= count || items_[static_cast<std::size_t>(i + 1)].header;
        if (style.dividers && style.layout != DetailLayout::grid && !last)
            list.rounded_rect({r.x + style.padding, r.y + r.h + style.gap * 0.5f - 0.75f,
                               r.w - 2.0f * style.padding, 1.5f},
                              0.0f, rule_color(paint, style.panel));
        list.pop_transform();
        list.pop_opacity();
    }
    list.pop_clip();

    if (style.scroll_thumb && overflow)
    {
        const float track = in.h - 12.0f;
        const float size = std::max(track * in.h / content_, 30.0f);
        const float at = scroll_.offset() / std::max(content_ - in.h, 1.0f);
        const float x = in.x + in.w + 3.0f;
        list.rounded_rect({x, in.y + 6.0f, 4.0f, track}, 2.0f, ink.muted.with_alpha(0.16f));
        list.rounded_rect({x, in.y + 6.0f + (track - size) * tween::clamp01(at), 4.0f, size}, 2.0f,
                          ink.muted.with_alpha(0.7f));
    }
}

} // namespace hui::ui
