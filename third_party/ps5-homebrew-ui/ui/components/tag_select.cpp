// ps5-homebrew-ui - Component: TagSelect.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/tag_select.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

bool stroke_only(SurfaceStyle style)
{
    return style == SurfaceStyle::outline || style == SurfaceStyle::glow;
}

// The check mark of Painter::checkbox, on its own.
void draw_check(gfx::DrawList &list, const Rect &r, float value, Color ink)
{
    if (value <= 0.01f)
        return;
    const float s = r.w;
    const float x0 = r.x + s * 0.10f, y0 = r.y + s * 0.52f;
    const float x1 = r.x + s * 0.38f, y1 = r.y + s * 0.80f;
    const float x2 = r.x + s * 0.90f, y2 = r.y + s * 0.20f;
    const float first = tween::clamp01(value * 2.5f);
    const float second = tween::clamp01((value - 0.4f) / 0.6f);
    const float width = std::max(2.5f, s * 0.16f);
    list.line(x0, y0, x0 + (x1 - x0) * first, y0 + (y1 - y0) * first, width, ink);
    if (second > 0.0f)
        list.line(x1, y1, x1 + (x2 - x1) * second, y1 + (y2 - y1) * second, width, ink);
}

// The theme a chip of this shape is painted with.
Theme shaped(const Theme &theme, TagShape shape)
{
    Theme out = theme;
    if (shape == TagShape::pill)
    {
        out.pill_chips = true;
    }
    else if (shape == TagShape::square)
    {
        out.pill_chips = false;
        out.radius = 0.0f;
    }
    return out;
}

} // namespace

void TagSelect::set_options(std::vector<TagOption> options)
{
    options_ = std::move(options);
    on_.assign(options_.size(), 0);
    shown_.assign(options_.size(), tween::Spring{});
    focus_ = std::clamp(focus_, 0, std::max(static_cast<int>(options_.size()) - 1, 0));
    rects_.clear();
    row_.clear();
    measured_ = false;
    placed_ = false;
    keep_x_ = -1.0f;
}

void TagSelect::set_selected(int index, bool selected)
{
    if (index < 0 || index >= static_cast<int>(options_.size()))
        return;
    on_[static_cast<std::size_t>(index)] = selected ? 1 : 0;
}

int TagSelect::selected_count() const
{
    return static_cast<int>(std::count_if(on_.begin(), on_.end(), [](char v) { return v != 0; }));
}

std::vector<int> TagSelect::selection() const
{
    std::vector<int> out;
    for (std::size_t i = 0; i < on_.size(); ++i)
    {
        if (on_[i] != 0)
            out.push_back(static_cast<int>(i));
    }
    return out;
}

float TagSelect::title_height() const
{
    return title_.empty() && !style.counter ? 0.0f : style.title_size + style.title_gap;
}

// Places the chips, whose widths are already in rects_, left to right; a new
// row starts when the next one does not fit. Each row is then shifted as
// style.align says.
void TagSelect::wrap() const
{
    const std::size_t count = options_.size();
    const auto finish = [&](std::size_t from, std::size_t to, float used)
    {
        const float spare = std::max(bounds_.w - used, 0.0f);
        const float shift = style.align == gfx::Align::center  ? spare * 0.5f
                            : style.align == gfx::Align::right ? spare
                                                               : 0.0f;
        for (std::size_t i = from; i < to; ++i)
            rects_[i].x += shift;
    };
    float x = 0.0f;
    float y = title_height();
    int row = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i < count; ++i)
    {
        const float w = std::min(rects_[i].w, std::max(bounds_.w, 1.0f));
        if (x > 0.0f && x + w > bounds_.w + 0.5f)
        {
            finish(start, i, x - style.gap);
            x = 0.0f;
            y += style.chip_height + style.row_gap;
            ++row;
            start = i;
        }
        rects_[i] = {x, y, w, style.chip_height};
        row_[i] = row;
        x += w + style.gap;
    }
    finish(start, count, x - style.gap);
}

void TagSelect::layout(const Fonts &fonts) const
{
    fonts_ = &fonts;
    ensure_layout();
}

// The layout follows the style, the bounds and the theme's label face, any of
// which may change between two frames, so it is made again whenever it is
// needed: a dozen text measurements. Until the fonts are known (the first
// draw, or layout()) the widths are a guess from the number of letters, so
// that navigation works from the first frame.
void TagSelect::ensure_layout() const
{
    const std::size_t count = options_.size();
    rects_.resize(count);
    row_.resize(count);
    const float extra = 2.0f * style.chip_padding + (style.check ? style.check_size + 8.0f : 0.0f);
    if (fonts_ != nullptr)
    {
        gfx::DrawList scratch;
        const Painter paint(scratch, *fonts_, style.theme, 0);
        for (std::size_t i = 0; i < count; ++i)
            rects_[i].w = paint.label_width(options_[i].label, style.text_size) + extra;
        measured_ = true;
    }
    else
    {
        for (std::size_t i = 0; i < count; ++i)
            rects_[i].w =
                static_cast<float>(options_[i].label.size()) * style.text_size * 0.56f + extra;
    }
    wrap();
}

Rect TagSelect::chip_rect(int index) const
{
    ensure_layout();
    const Rect &r = rects_[static_cast<std::size_t>(index)];
    return {bounds_.x + r.x, bounds_.y + r.y, r.w, r.h};
}

int TagSelect::row_of(int index) const
{
    ensure_layout();
    return row_[static_cast<std::size_t>(index)];
}

int TagSelect::rows() const
{
    ensure_layout();
    return row_.empty() ? 0 : row_.back() + 1;
}

float TagSelect::preferred_height() const
{
    const int count = rows();
    return title_height() + static_cast<float>(count) * style.chip_height +
           static_cast<float>(std::max(count - 1, 0)) * style.row_gap;
}

void TagSelect::set_focus(int index, bool snap)
{
    focus_ = std::clamp(index, 0, std::max(static_cast<int>(options_.size()) - 1, 0));
    keep_x_ = -1.0f;
    if (snap)
        placed_ = false;
}

// The chip of a row that is under x, or else the one whose edge is nearest.
int TagSelect::nearest_in_row(int row, float x) const
{
    int best = -1;
    float best_distance = 0.0f;
    for (std::size_t i = 0; i < rects_.size(); ++i)
    {
        if (row_[i] != row)
            continue;
        const Rect &r = rects_[i];
        const float distance = x < r.x ? r.x - x : (x > r.x + r.w ? x - r.x - r.w : 0.0f);
        if (best < 0 || distance < best_distance)
        {
            best = static_cast<int>(i);
            best_distance = distance;
        }
    }
    return best;
}

float TagSelect::chip_radius(const Painter &paint, const Rect &chip) const
{
    return paint.theme().pill_chips ? chip.h * 0.5f : paint.control_radius(chip);
}

Event TagSelect::handle(const InputFrame &input, Feedback &feedback)
{
    exit_ = Direction::none;
    const int count = static_cast<int>(options_.size());
    if (input.nav != Direction::none)
    {
        const Direction d = input.nav;
        if (count == 0)
        {
            if (style.exits.allows(d))
                exit_ = d;
            return Event::none;
        }
        ensure_layout();
        const int row = row_[static_cast<std::size_t>(focus_)];
        int next = -1;
        if (d == Direction::left || d == Direction::right)
        {
            const int to = focus_ + (d == Direction::right ? 1 : -1);
            if (to >= 0 && to < count && (style.flow || row_[static_cast<std::size_t>(to)] == row))
            {
                next = to;
                keep_x_ = -1.0f;
            }
        }
        else
        {
            const int to = row + (d == Direction::down ? 1 : -1);
            if (to >= 0 && to <= row_.back())
            {
                // Hold on to where the column started, so crossing a short
                // row does not drag the focus sideways for good.
                if (keep_x_ < 0.0f)
                    keep_x_ = rects_[static_cast<std::size_t>(focus_)].cx();
                next = nearest_in_row(to, keep_x_);
            }
        }
        const float x = chip_rect(focus_).cx();
        if (next < 0)
        {
            if (style.exits.allows(d))
            {
                exit_ = d;
                return Event::none;
            }
            return refuse(feedback, style, input, highlight_.refusal(), x);
        }
        focus_ = next;
        const int last_row = std::max(row_.back(), 1);
        const float along = static_cast<float>(row_[static_cast<std::size_t>(focus_)]) /
                            static_cast<float>(last_row);
        play_cue(feedback, style, style.sounds.move, chip_rect(focus_).cx(),
                 tween::lerp(1.05f, 0.95f, along));
        return Event::moved;
    }
    if (input.is_pressed(Action::confirm) && count > 0)
    {
        const float x = chip_rect(focus_).cx();
        const std::size_t at = static_cast<std::size_t>(focus_);
        if (options_[at].disabled)
            return refuse(feedback, style, input, highlight_.refusal(), x);
        const bool turn_on = on_[at] == 0;
        if (turn_on && style.single)
        {
            std::fill(on_.begin(), on_.end(), 0);
        }
        else if (turn_on && style.max_selected > 0 && selected_count() >= style.max_selected)
        {
            if (!input.nav_repeat)
                limit_.trigger();
            return refuse(feedback, style, input, highlight_.refusal(), x);
        }
        on_[at] = turn_on ? 1 : 0;
        press_.trigger();
        play_cue(feedback, style, style.sounds.change, x,
                 style.pitch_by_state ? (turn_on ? 1.06f : 0.94f) : 1.0f);
        return Event::changed;
    }
    if (input.is_pressed(Action::back))
    {
        play_cue(feedback, style, style.sounds.cancel, bounds_.cx());
        return Event::cancelled;
    }
    return Event::none;
}

void TagSelect::update(float dt)
{
    active_amount_.target = active_ ? 1.0f : 0.0f;
    active_amount_.update(dt, 18.0f);
    press_.update(dt, 10.0f);
    limit_.update(dt, 4.0f);
    if (options_.empty())
        return;
    ensure_layout();
    const Rect target = rects_[static_cast<std::size_t>(focus_)];
    highlight_.target(target);
    if (!placed_)
        highlight_.snap(target);
    highlight_.update(dt, style);
    const float omega = std::max(style.omega(), 14.0f) * 1.2f;
    for (std::size_t i = 0; i < options_.size(); ++i)
    {
        shown_[i].target = on_[i] != 0 ? 1.0f : 0.0f;
        if (!placed_)
            shown_[i].snap(shown_[i].target);
        shown_[i].update(dt, omega);
    }
    // Until the chips were measured with real fonts the highlight keeps
    // snapping, so the first real layout does not make it glide.
    placed_ = measured_;
}

void TagSelect::draw(Canvas &canvas) const
{
    layout(canvas.fonts);
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const Theme chip_theme = shaped(theme, style.shape);
    Painter chips(list, canvas.fonts, chip_theme, canvas.glass);
    const Color quiet = style.on_page ? paint.page_text_muted() : theme.text_muted;

    // ---- the title line ----
    float title_room = bounds_.w;
    const float baseline = bounds_.y + style.title_size * 0.82f;
    if (style.counter)
    {
        const int selected = selected_count();
        std::string text;
        if (counter_text)
        {
            text = counter_text(selected, style.max_selected);
        }
        else
        {
            char buffer[32];
            if (style.max_selected > 0)
                std::snprintf(buffer, sizeof(buffer), "%d of %d", selected, style.max_selected);
            else
                std::snprintf(buffer, sizeof(buffer), "%d selected", selected);
            text = buffer;
        }
        const float used = paint.label(
            text, bounds_.x + bounds_.w, baseline, style.title_size,
            gfx::mix(quiet, theme.warning, tween::clamp01(limit_.value * 1.5f)), gfx::Align::right);
        title_room -= used + 20.0f;
    }
    if (!title_.empty())
        paint.bounded_label(title_, bounds_.x, baseline, style.title_size, quiet, std::max(title_room, 40.0f));
    if (options_.empty())
        return;

    const bool strokes = stroke_only(theme.style);
    const float pop = style.reduced_motion ? 0.0f : press_.value;
    const auto on_screen = [&](std::size_t i)
    {
        const Rect &r = rects_[i];
        return Rect{bounds_.x + r.x, bounds_.y + r.y, r.w, r.h};
    };
    // A chip that was just switched pushes in for a moment.
    const auto begin = [&](std::size_t i, const Rect &chip)
    {
        list.push_opacity(options_[i].disabled ? 0.4f : 1.0f);
        const bool pressed = static_cast<int>(i) == focus_;
        list.push_transform(pressed ? 1.0f - 0.06f * pop : 1.0f, chip.cx(), chip.cy(), 0.0f, 0.0f);
    };
    const auto end = [&]()
    {
        list.pop_transform();
        list.pop_opacity();
    };

    // ---- bodies, then the one gliding ring, then the words ----
    for (std::size_t i = 0; i < options_.size(); ++i)
    {
        const Rect chip = on_screen(i);
        begin(i, chip);
        chips.chip(chip, "", shown_[i].value, {});
        end();
    }

    const Rect glide = highlight_.rect(canvas.time);
    const Rect ring{bounds_.x + glide.x, bounds_.y + glide.y, glide.w, glide.h};
    chips.focus_ring(ring, chip_radius(chips, ring), active_amount_.value);

    for (std::size_t i = 0; i < options_.size(); ++i)
    {
        const Rect chip = on_screen(i);
        const float value = tween::clamp01(shown_[i].value);
        const Color ink = strokes ? gfx::mix(theme.text_muted, theme.accent, value)
                                  : gfx::mix(theme.text, Painter::on(theme.accent), value);
        begin(i, chip);
        // The label is centred while the chip is off; switched on, label and
        // check are centred together, so the label slides aside as the check
        // draws itself and the chip never changes width.
        const float mark = style.check ? style.check_size + 8.0f : 0.0f;
        // The chip was sized for exactly this label: half a pixel of slack keeps
        // rounding from deciding that it no longer fits.
        const float room = std::max(chip.w - 2.0f * style.chip_padding - mark, 10.0f) + 0.5f;
        const std::string label = fit_label(paint, options_[i].label, style.text_size, room);
        const float width = paint.label_width(label, style.text_size);
        const float cx = chip.cx() + mark * 0.5f * value;
        paint.label(label, cx, chip.cy() + style.text_size * 0.35f, style.text_size, ink,
                    gfx::Align::center);
        if (style.check)
            draw_check(list,
                       {cx - width * 0.5f - mark, chip.cy() - style.check_size * 0.5f,
                        style.check_size, style.check_size},
                       value, ink);
        end();
    }
}

} // namespace hui::ui
