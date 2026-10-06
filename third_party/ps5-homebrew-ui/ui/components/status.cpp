// ps5-homebrew-ui - Components: Countdown, StorageBar and StatusBar.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/status.hpp"

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

constexpr float kTau = 6.2831853f;

bool rounded(const Theme &theme)
{
    return theme.corner == Corner::round && theme.radius >= 2.0f;
}

// Themes whose wells have a thick frame: what fills a well sits inside it.
bool framed(const Theme &theme)
{
    return theme.style == SurfaceStyle::hard || theme.style == SurfaceStyle::bevel ||
           theme.style == SurfaceStyle::pixel;
}

Color base_text(const Theme &theme, bool on_panel)
{
    return !on_panel && theme.page_text.a > 0.0f ? theme.page_text : theme.text;
}

} // namespace

// ---- Countdown -------------------------------------------------------------

void Countdown::start(float seconds)
{
    total_ = std::max(seconds, 0.0f);
    remaining_ = total_;
    running_ = total_ > 0.0f;
    started_ = true;
}

void Countdown::set_remaining(float seconds)
{
    remaining_ = std::max(seconds, 0.0f);
    total_ = std::max(total_, remaining_);
    started_ = true;
}

Status Countdown::zone() const
{
    if (remaining_ <= style.danger_at)
        return Status::danger;
    if (remaining_ <= style.warning_at)
        return Status::warning;
    return Status::neutral;
}

std::string Countdown::text() const
{
    // Rounded up: "0:01" stays until the time is really over.
    const int seconds = static_cast<int>(std::ceil(remaining_ - 1e-4f));
    const int whole = std::max(seconds, 0);
    char out[32];
    if (style.hours || whole >= 3600)
        std::snprintf(out, sizeof(out), "%d:%02d:%02d", whole / 3600, (whole / 60) % 60,
                      whole % 60);
    else
        std::snprintf(out, sizeof(out), "%d:%02d", whole / 60, whole % 60);
    return out;
}

void Countdown::advance(float dt, Feedback *feedback)
{
    if (running_)
    {
        const float before = remaining_;
        remaining_ = std::max(remaining_ - dt, 0.0f);
        const bool new_second = std::ceil(before) != std::ceil(remaining_);
        if (remaining_ <= 0.0f)
        {
            running_ = false;
            finish_.trigger();
            if (feedback != nullptr)
                play_cue(*feedback, style, style.sounds.notify, bounds_.cx());
        }
        else if (new_second && remaining_ <= style.pulse_at)
        {
            if (!style.reduced_motion)
                beat_.trigger();
            // The ticks climb as the end comes closer.
            if (feedback != nullptr)
                play_cue(*feedback, style, style.sounds.step, bounds_.cx(),
                         tween::lerp(1.22f, 0.96f,
                                     tween::clamp01(remaining_ / std::max(style.pulse_at, 1.0f))),
                         0.8f);
        }
    }
    const Theme &theme = style.theme;
    const Status now = zone();
    const Color calm = style.color.a > 0.0f ? style.color : status_color(theme, style.status);
    const Color wanted =
        visible_on(theme, started_ && now != Status::neutral ? status_color(theme, now) : calm,
                   style.on_panel);
    ink_.target(wanted);
    if (!ink_set_)
    {
        ink_.snap(wanted);
        ink_set_ = true;
    }
    ink_.update(dt, 10.0f);
    beat_.update(dt, 5.0f);
    finish_.update(dt, 2.5f);
}

void Countdown::update(float dt)
{
    advance(dt, nullptr);
}

void Countdown::update(float dt, Feedback &feedback)
{
    advance(dt, &feedback);
}

void Countdown::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const Inks ink = inks(paint, style.on_panel);
    const Color tone = ink_set_
                           ? ink_.value()
                           : visible_on(theme, status_color(theme, style.status), style.on_panel);
    const bool alarmed = started_ && zone() != Status::neutral;
    const Color figure = alarmed || style.tint_text ? tone : base_text(theme, style.on_panel);
    const std::string time = text();
    const float bump = style.reduced_motion ? 0.0f : 0.14f * beat_.value + 0.1f * finish_.value;

    if (style.shape == CountdownShape::ring && bounds_.w > bounds_.h * 1.6f)
    {
        // A strip: the ring at the leading end, the figure beside it, at
        // its full size.
        const float ring = bounds_.h;
        const float size = style.text_size;
        const float group = ring + 14.0f + number_width(paint, time, size);
        float x = bounds_.x;
        if (style.align == gfx::Align::right)
            x = bounds_.x + bounds_.w - group;
        else if (style.align == gfx::Align::center)
            x = bounds_.cx() - group * 0.5f;
        const float cx = x + ring * 0.5f;
        const float cy = bounds_.cy();
        const float left = total_ > 0.0f ? tween::clamp01(remaining_ / total_) : 0.0f;
        list.ring(cx, cy, ring * 0.5f, style.thickness, ink.muted.with_alpha(0.22f));
        if (finish_.value > 0.01f)
            list.ring(cx, cy, ring * 0.5f + 6.0f * (1.0f - finish_.value), style.thickness,
                      tone.with_alpha(finish_.value));
        if (left > 0.002f)
            list.arc(cx, cy, ring * 0.5f, style.thickness, 0.0f, kTau * left, tone, rounded(theme));
        const float text_x = x + ring + 14.0f;
        list.push_transform(1.0f + bump, text_x + (group - ring - 14.0f) * 0.5f, cy, 0.0f, 0.0f);
        draw_number(paint, list, time, text_x, cy + size * 0.35f, size, figure);
        list.pop_transform();
        return;
    }
    if (style.shape == CountdownShape::ring)
    {
        const float caption = label.empty() ? 0.0f : style.label_size + 8.0f;
        const float size = std::max(std::min(bounds_.w, bounds_.h - caption), 16.0f);
        const float cx = bounds_.cx();
        const float cy = bounds_.y + size * 0.5f;
        const float radius = size * 0.5f;
        const float left = total_ > 0.0f ? tween::clamp01(remaining_ / total_) : 0.0f;
        list.ring(cx, cy, radius, style.thickness, ink.muted.with_alpha(0.22f));
        if (finish_.value > 0.01f)
            list.ring(cx, cy, radius + 6.0f * (1.0f - finish_.value), style.thickness,
                      tone.with_alpha(finish_.value));
        if (left > 0.002f)
            list.arc(cx, cy, radius, style.thickness, 0.0f, kTau * left, tone, rounded(theme));
        const float text_size = std::clamp(size * 0.26f, 13.0f, 40.0f);
        list.push_transform(1.0f + bump, cx, cy, 0.0f, 0.0f);
        // The figure shrinks to the hole rather than being cut.
        const float hole = size - 2.0f * style.thickness - 10.0f;
        const float width = number_width(paint, time, text_size);
        const float fitted = width > hole ? std::max(text_size * hole / width, 10.0f) : text_size;
        draw_number(paint, list, time, cx, cy + fitted * 0.35f, fitted, figure, gfx::Align::center);
        list.pop_transform();
        if (!label.empty())
            paint.bounded_label(label, cx, bounds_.y + size + style.label_size + 4.0f, style.label_size, ink.muted, bounds_.w, gfx::Align::center);
        return;
    }

    // Text: the caption, then the figure, as one group.
    const float size = style.text_size;
    const float time_width = number_width(paint, time, size);
    const float room = std::max(bounds_.w - time_width - 12.0f, 0.0f);
    const std::string caption = label.empty() || room < 30.0f
                                    ? std::string()
                                    : label;
    const float caption_width =
        caption.empty() ? 0.0f : std::min(paint.label_width(caption, style.label_size), room) + 12.0f;
    const float group = caption_width + time_width;
    float x = bounds_.x;
    if (style.align == gfx::Align::right)
        x = bounds_.x + bounds_.w - group;
    else if (style.align == gfx::Align::center)
        x = bounds_.cx() - group * 0.5f;
    const float cy = bounds_.cy();
    if (!caption.empty())
        paint.bounded_label(caption, x, cy + style.label_size * 0.35f, style.label_size, ink.muted, room);
    const float figure_cx = x + caption_width + time_width * 0.5f;
    list.push_transform(1.0f + bump, figure_cx, cy, 0.0f, 0.0f);
    draw_number(paint, list, time, x + caption_width, cy + size * 0.35f, size, figure);
    list.pop_transform();
}

// ---- StorageBar ------------------------------------------------------------

void StorageBar::set_capacity(float capacity)
{
    capacity_ = std::max(capacity, 0.0f);
    sync_legend();
}

void StorageBar::set_categories(std::vector<StorageCategory> categories, bool snap)
{
    const std::vector<tween::Bounce> old = std::move(values_);
    categories_ = std::move(categories);
    values_.assign(categories_.size(), tween::Bounce{});
    for (std::size_t i = 0; i < categories_.size(); ++i)
    {
        if (i < old.size())
            values_[i] = old[i];
        values_[i].target = std::max(categories_[i].value, 0.0f);
        if (snap)
            values_[i].snap(values_[i].target);
    }
    lifts_.resize(categories_.size());
    focus_ = std::clamp(focus_, 0, std::max(static_cast<int>(categories_.size()) - 1, 0));
    sync_legend();
}

float StorageBar::used() const
{
    float sum = 0.0f;
    for (const StorageCategory &category : categories_)
        sum += std::max(category.value, 0.0f);
    return sum;
}

void StorageBar::set_focus(int category)
{
    focus_ = std::clamp(category, 0, std::max(static_cast<int>(categories_.size()) - 1, 0));
}

void StorageBar::enter()
{
    age_ = 0.0f;
}

void StorageBar::sync_legend()
{
    const Theme &theme = style.theme;
    std::vector<LegendItem> items;
    for (std::size_t i = 0; i < categories_.size(); ++i)
    {
        const StorageCategory &category = categories_[i];
        const Color color = category.color.a > 0.0f
                                ? visible_on(theme, category.color, style.on_panel)
                                : series_color(theme, static_cast<int>(i), style.on_panel);
        items.push_back({category.label, color,
                         style.legend_values
                             ? format_value(category.value, style.decimals) + style.unit
                             : std::string()});
    }
    if (style.show_free)
    {
        const Color ground = ground_color(theme, style.on_panel);
        const Color quiet = !style.on_panel && theme.page_text_muted.a > 0.0f
                                ? theme.page_text_muted
                                : theme.text_muted;
        items.push_back({style.free_label,
                         gfx::mix(ground, Color{quiet.r, quiet.g, quiet.b, 1.0f}, 0.45f),
                         style.legend_values ? format_value(free(), style.decimals) + style.unit
                                             : std::string()});
    }
    legend_.set_items(std::move(items));
    legend_.style.theme = theme;
    legend_.style.text_size = style.legend_size;
    legend_.style.on_panel = style.on_panel;
    legend_.set_highlight(active_ ? focus_ : -1);
}

Event StorageBar::handle(const InputFrame &input, Feedback &feedback)
{
    exit_ = Direction::none;
    const int count = static_cast<int>(categories_.size());
    if (count == 0)
        return Event::none;
    const float x = bounds_.cx();
    if (input.nav != Direction::none)
    {
        int next = -1;
        if (input.nav == Direction::right)
            next = focus_ + 1;
        else if (input.nav == Direction::left)
            next = focus_ - 1;
        if (next < 0 || next >= count)
        {
            if (style.exits.allows(input.nav))
            {
                exit_ = input.nav;
                return Event::none;
            }
            return refuse(feedback, style, input, refusal_, x);
        }
        focus_ = next;
        const float along =
            count > 1 ? static_cast<float>(focus_) / static_cast<float>(count - 1) : 0.0f;
        play_cue(feedback, style, style.sounds.move, bounds_.x + bounds_.w * along,
                 tween::lerp(0.96f, 1.06f, along));
        return Event::moved;
    }
    if (input.is_pressed(Action::confirm))
    {
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

void StorageBar::update(float dt)
{
    age_ += dt;
    const float omega = std::min(std::max(style.omega() * 0.6f, 8.0f), 60.0f);
    for (tween::Bounce &value : values_)
        value.update(dt, omega, std::max(style.damping(), 0.85f));
    active_amount_.target = active_ ? 1.0f : 0.0f;
    active_amount_.update(dt, 18.0f);
    for (std::size_t i = 0; i < lifts_.size(); ++i)
    {
        lifts_[i].target = active_ && static_cast<int>(i) == focus_ ? 1.0f : 0.0f;
        lifts_[i].update(dt, std::max(style.omega(), 16.0f));
    }
    refusal_.update(dt, 9.0f);
    sync_legend();
    legend_.update(dt);
}

void StorageBar::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const Inks ink = inks(paint, style.on_panel);
    float y = bounds_.y;

    if (style.header)
    {
        const float size = style.text_size;
        const std::string figures = format_value(used(), style.decimals) + " / " +
                                    format_value(capacity_, style.decimals) + style.unit;
        const float width = draw_number(paint, list, figures, bounds_.x + bounds_.w,
                                        y + size * 0.86f, size, ink.muted, gfx::Align::right);
        paint.bounded_label(title, bounds_.x, y + size * 0.86f, size, ink.text, std::max(bounds_.w - width - 16.0f, 20.0f));
        y += size + style.spacing;
    }

    // ---- the bar ----
    const Rect bar{bounds_.x + shake(refusal_.value, canvas.time, 8.0f), y + style.focus_grow,
                   bounds_.w, style.height};
    const float radius =
        theme.style == SurfaceStyle::bevel ? 0.0f : std::min(theme.radius, style.height * 0.5f);
    paint.well(bar, radius, theme.surface_high);
    const float inset = framed(theme) ? std::min(theme.border, style.height * 0.25f) : 0.0f;
    const float track = bar.w - 2.0f * inset;
    const float grow = style.reduced_motion ? 1.0f : tween::cubic_out(age_ / 0.6f);
    const float fade = style.reduced_motion ? tween::cubic_out(age_ / 0.2f) : 1.0f;
    const int count = static_cast<int>(categories_.size());
    float x = bar.x + inset;
    const float end = bar.x + inset + track;
    for (int i = 0; i < count && capacity_ > 0.0f; ++i)
    {
        const std::size_t at = static_cast<std::size_t>(i);
        const float share = std::max(values_[at].value, 0.0f) / capacity_;
        const float width = std::min(track * share * grow, end - x);
        if (width <= 0.5f)
            continue;
        const float lift = at < lifts_.size() ? lifts_[at].value : 0.0f;
        const float tall = style.reduced_motion ? 0.0f : style.focus_grow * lift;
        const float dim = 1.0f - 0.25f * active_amount_.value * (1.0f - lift);
        const Color color = (categories_[at].color.a > 0.0f
                                 ? visible_on(theme, categories_[at].color, style.on_panel)
                                 : series_color(theme, i, style.on_panel))
                                .with_alpha(dim * fade);
        // The gap is cut from each segment's end, so the segments' starts
        // stay where the sizes put them.
        const float gap = width > style.segment_gap + 2.0f ? style.segment_gap : 0.0f;
        draw_block(canvas, theme,
                   {x, bar.y + inset - tall, width - gap, bar.h - 2.0f * inset + 2.0f * tall},
                   std::min(radius, 4.0f), color);
        x += width;
    }
    y += style.height + 2.0f * style.focus_grow + style.spacing;

    if (!style.legend)
        return;
    const Rect room{bounds_.x, y, bounds_.w, std::max(bounds_.y + bounds_.h - y, 0.0f)};
    if (legend_.height(paint, room.w) <= room.h + 4.0f || !style.legend_values)
    {
        legend_.draw_at(canvas, room);
        return;
    }
    // A wide face: the names alone fit where names and sizes do not.
    Legend names = legend_;
    std::vector<LegendItem> items = legend_.items();
    for (LegendItem &item : items)
        item.value.clear();
    names.set_items(std::move(items));
    names.draw_at(canvas, room);
}

// ---- StatusBar -------------------------------------------------------------

void StatusBar::set_battery(float level, bool charging)
{
    battery_.target = tween::clamp01(level);
    charging_ = charging;
}

void StatusBar::set_signal(int bars)
{
    signal_ = std::clamp(bars, 0, std::max(style.signal_bars, 0));
}

void StatusBar::set_player(int index, bool connected, Color color)
{
    if (index < 0 || index >= kPlayers)
        return;
    const std::size_t at = static_cast<std::size_t>(index);
    players_[at].connected = connected;
    players_[at].color = color;
}

void StatusBar::update(float dt)
{
    phase_ += dt;
    battery_.update(dt, 8.0f);
    charge_.target = charging_ ? 1.0f : 0.0f;
    charge_.update(dt, 14.0f);
    signal_shown_.target = static_cast<float>(signal_);
    signal_shown_.update(dt, 12.0f);
    for (std::size_t i = 0; i < players_.size(); ++i)
    {
        joined_[i].target = players_[i].connected ? 1.0f : 0.0f;
        joined_[i].update(dt, std::max(style.omega(), 14.0f), std::max(style.damping(), 0.6f));
    }
}

void StatusBar::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    if (style.panel)
        paint.panel(bounds_);
    const Inks ink = inks(paint, style.panel);
    const float cy = bounds_.cy();
    const float glyph = style.glyph_size;
    const bool round = rounded(theme);
    float right = bounds_.x + bounds_.w - style.padding;

    if (style.show_clock && !clock.empty())
    {
        right -= draw_number(paint, list, clock, right, cy + style.text_size * 0.35f,
                             style.text_size, ink.text, gfx::Align::right);
        right -= style.gap;
    }

    if (style.show_battery)
    {
        // A body with a contact on its end; the charge fills it from the left.
        const float h = std::round(glyph * 0.74f);
        const float w = std::round(h * 1.9f);
        const float nub = 3.0f;
        const Rect body{right - nub - w, cy - h * 0.5f, w, h};
        const float level = tween::clamp01(battery_.value);
        const bool low = battery_.target <= style.low_battery && !charging_;
        const Color fill = gfx::mix(low ? visible_on(theme, theme.danger, style.panel) : ink.text,
                                    visible_on(theme, theme.success, style.panel), charge_.value);
        const float corner = round ? 4.0f : 0.0f;
        list.bordered_rect(body, corner, Color{ink.text.r, ink.text.g, ink.text.b, 0.0f}, 2.0f,
                           ink.text);
        list.rounded_rect({body.x + body.w + 1.0f, cy - h * 0.22f, nub, h * 0.44f},
                          round ? 1.5f : 0.0f, ink.text);
        const Rect inside = body.inset(4.0f);
        if (level > 0.01f)
            list.rounded_rect({inside.x, inside.y, std::max(inside.w * level, 2.0f), inside.h},
                              round ? 1.5f : 0.0f, fill);
        right = body.x;
        if (charge_.value > 0.01f)
        {
            // A bolt beside the battery: two strokes meeting at a step.
            const float s = h * 0.5f * charge_.value;
            const float bx = right - 10.0f;
            const Color bolt =
                visible_on(theme, theme.success, style.panel).with_alpha(charge_.value);
            list.line(bx + s * 0.35f, cy - s, bx - s * 0.35f, cy + s * 0.1f, 2.5f, bolt);
            list.line(bx - s * 0.35f, cy + s * 0.1f, bx + s * 0.35f, cy - s * 0.1f, 2.5f, bolt);
            list.line(bx + s * 0.35f, cy - s * 0.1f, bx - s * 0.35f, cy + s, 2.5f, bolt);
            right -= 18.0f * charge_.value;
        }
        if (style.battery_percent)
        {
            char text[16];
            std::snprintf(text, sizeof(text), "%d%%",
                          static_cast<int>(std::lround(battery_.target * 100.0f)));
            right -= 8.0f;
            right -= draw_number(paint, list, text, right, cy + style.text_size * 0.3f,
                                 style.text_size * 0.82f, ink.muted, gfx::Align::right);
        }
        right -= style.gap;
    }

    if (style.show_signal && style.signal_bars > 0)
    {
        const int bars = style.signal_bars;
        const float w = std::max(std::round(glyph * 0.2f), 3.0f);
        const float step = w + 3.0f;
        const float total = step * static_cast<float>(bars) - 3.0f;
        const float base = cy + glyph * 0.4f;
        const float x0 = right - total;
        for (int i = 0; i < bars; ++i)
        {
            const float h =
                glyph * 0.8f * (0.3f + 0.7f * static_cast<float>(i + 1) / static_cast<float>(bars));
            const float lit = tween::clamp01(signal_shown_.value - static_cast<float>(i));
            list.rounded_rect({x0 + step * static_cast<float>(i), base - h, w, h},
                              round ? 1.5f : 0.0f,
                              gfx::mix(ink.muted.with_alpha(0.35f), ink.text, lit));
        }
        if (signal_ == 0)
        {
            // No connection: a stroke through the bars.
            const Color cut = visible_on(theme, theme.danger, style.panel);
            list.line(x0 - 2.0f, base - glyph * 0.8f, x0 + total + 2.0f, base + 1.0f, 2.5f, cut);
        }
        right = x0 - style.gap;
    }

    if (style.show_players)
    {
        const float size = glyph;
        const float step = size + 8.0f;
        const float x0 = right - step * static_cast<float>(kPlayers) + 8.0f;
        for (int i = 0; i < kPlayers; ++i)
        {
            const std::size_t at = static_cast<std::size_t>(i);
            const float cx = x0 + step * static_cast<float>(i) + size * 0.5f;
            const float on = tween::clamp01(joined_[at].value);
            const Color color = players_[at].color.a > 0.0f
                                    ? visible_on(theme, players_[at].color, style.panel)
                                    : series_color(theme, i, style.panel);
            // A slot is an outline until someone takes it; the fill pops in.
            const float radius = size * 0.5f;
            if (round)
                list.ring(cx, cy, radius, 2.0f, ink.muted.with_alpha(0.5f * (1.0f - on)));
            else
                list.bordered_rect({cx - radius, cy - radius, size, size}, 0.0f,
                                   Color{ink.muted.r, ink.muted.g, ink.muted.b, 0.0f}, 2.0f,
                                   ink.muted.with_alpha(0.5f * (1.0f - on)));
            const float scale = style.reduced_motion ? 1.0f : std::max(joined_[at].value, 0.0f);
            if (on > 0.01f)
                draw_marker(canvas, theme, cx, cy, radius * scale, color.with_alpha(on));
            char text[4];
            std::snprintf(text, sizeof(text), "%d", i + 1);
            const float number = size * 0.62f;
            draw_number(paint, list, text, cx, cy + number * 0.35f, number,
                        gfx::mix(ink.muted.with_alpha(0.7f), Painter::on(color), on),
                        gfx::Align::center);
        }
        right = x0 - style.gap;
    }

    const float left = bounds_.x + style.padding;
    if (!title.empty() && right - left > 40.0f)
        paint.bounded_label(title, left, cy + style.text_size * 0.35f, style.text_size, ink.text, right - left);
}

} // namespace hui::ui
