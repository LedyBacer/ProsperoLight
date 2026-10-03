// ps5-homebrew-ui - Components: HUD pieces that sit over gameplay.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/hud.hpp"

#include "ui/components/overlay.hpp"

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
const Color kLightInk = Color::rgb(0xf5f7fb);
const Color kWhite = Color::rgb(0xffffff);

bool plated(const HudStyle &style)
{
    return style.backing == HudBacking::plate;
}

Color ink_of(const HudStyle &style)
{
    if (style.ink.a > 0.0f)
        return style.ink;
    return plated(style) ? style.theme.text : kLightInk;
}

Color quiet_of(const HudStyle &style)
{
    if (style.ink.a <= 0.0f && plated(style))
        return style.theme.text_muted;
    return ink_of(style).with_alpha(0.74f);
}

// The shade made nearly opaque: what tracks and outlines of marks use.
Color deep_shade(const HudStyle &style)
{
    return {style.shade.r, style.shade.g, style.shade.b, std::min(1.0f, style.shade.a + 0.24f)};
}

float luminance(Color color)
{
    return 0.299f * color.r + 0.587f * color.g + 0.114f * color.b;
}

// What a mark is drawn on: the plate, or the dark of the glow.
Color ground_of(const HudStyle &style)
{
    return plated(style) ? solid_surface(style.theme) : deep_shade(style);
}

// A theme colour that must be seen on the HUD ground. Some themes use a main
// colour as dark as the glow (navy) or as light as their surface (white): it
// is then pulled toward the ink until it stands out.
Color legible(const HudStyle &style, Color color)
{
    color.a = 1.0f;
    if (std::fabs(luminance(color) - luminance(ground_of(style))) >= 0.24f)
        return color;
    return gfx::mix(color, ink_of(style), 0.62f);
}

bool square_theme(const Theme &theme)
{
    return theme.corner != Corner::round || theme.radius < 2.0f;
}

float corner_for(const Theme &theme, const Rect &r, float limit)
{
    return std::min(std::min(theme.radius, limit), std::min(r.w, r.h) * 0.5f);
}

void draw_backing(Canvas &canvas, const HudStyle &style, const Rect &r)
{
    if (r.w <= 0.0f || r.h <= 0.0f)
        return;
    if (style.backing == HudBacking::glow)
    {
        // A soft pool of dark, wider than the piece: it lowers the contrast
        // of whatever the game shows there without drawing a box. The
        // falloff straddles the edge, so most of it is gradient, not plate.
        const float reach = style.glow_spread;
        canvas.list.shadow(r.inset(-reach * 0.5f), 22.0f, reach * 1.5f, style.shade);
    }
    else if (style.backing == HudBacking::plate)
    {
        draw_overlay_panel(canvas, style.theme, r.inset(-style.plate_padding),
                           style.frosted && canvas.glass != 0);
    }
}

enum class Face : std::uint8_t
{
    label,
    body,
    heading,
};

float measure(const Painter &paint, Face face, std::string_view text, float size)
{
    if (face == Face::heading)
        return paint.heading_width(text, size);
    return face == Face::body ? paint.body_width(text, size) : paint.label_width(text, size);
}

float put(Painter &paint, Face face, std::string_view text, float x, float baseline, float size,
          Color color, gfx::Align align)
{
    if (face == Face::heading)
        return paint.heading(text, x, baseline, size, color, align);
    if (face == Face::body)
        return paint.body(text, x, baseline, size, color, align);
    return paint.label(text, x, baseline, size, color, align);
}

Color shadow_ink(const HudStyle &style, Color over)
{
    return {style.shade.r, style.shade.g, style.shade.b,
            std::min(1.0f, style.shade.a + 0.2f) * over.a};
}

// Text with a dark copy under it when there is no plate: the cheapest way to
// keep a word readable over a bright sky.
float hud_text(Painter &paint, const HudStyle &style, Face face, std::string_view text, float x,
               float baseline, float size, Color color, gfx::Align align = gfx::Align::left)
{
    if (!plated(style) && style.text_shadow)
        put(paint, face, text, x, baseline + std::max(1.5f, size * 0.06f), size,
            shadow_ink(style, color), align);
    return put(paint, face, text, x, baseline, size, color, align);
}

// The width of one digit cell: as wide as the widest digit in this face.
float digit_cell(const Painter &paint, Face face, float size)
{
    float width = 0.0f;
    for (char c = '0'; c <= '9'; ++c)
        width = std::max(width, measure(paint, face, std::string_view(&c, 1), size));
    return width;
}

float tabular_width(const Painter &paint, Face face, std::string_view text, float size)
{
    const float cell = digit_cell(paint, face, size);
    float width = 0.0f;
    for (const char c : text)
        width += c >= '0' && c <= '9' ? cell : measure(paint, face, std::string_view(&c, 1), size);
    return width;
}

// Digits in fixed cells, from x to the right. `dim` leading characters are
// drawn faint (the zeros that pad a count).
float hud_tabular(Painter &paint, const HudStyle &style, Face face, std::string_view text, float x,
                  float baseline, float size, Color color, int dim = 0)
{
    const float cell = digit_cell(paint, face, size);
    float at = x;
    int index = 0;
    for (const char c : text)
    {
        const std::string_view one(&c, 1);
        const bool digit = c >= '0' && c <= '9';
        const float width = digit ? cell : measure(paint, face, one, size);
        const Color tone = index < dim ? color.with_alpha(0.32f) : color;
        hud_text(paint, style, face, one, at + width * 0.5f, baseline, size, tone,
                 gfx::Align::center);
        at += width;
        ++index;
    }
    return at - x;
}

// The channel a bar or a cell runs in.
void draw_track(Painter &paint, const HudStyle &style, const Rect &r, float radius)
{
    if (plated(style))
    {
        paint.well(r, radius, style.theme.surface_high);
        return;
    }
    paint.fill(r, radius, deep_shade(style));
    paint.stroke(r, radius, 1.5f, ink_of(style).with_alpha(0.3f));
}

// How far a fill stays inside its track.
float track_inset(const HudStyle &style)
{
    return plated(style) ? std::max(style.theme.border, 0.0f) : 2.5f;
}

// The left `share` of r, with a corner that shrinks with it so a sliver of a
// fill does not turn into a lozenge wider than itself.
void fill_share(Painter &paint, const Rect &r, float radius, float share, Color color)
{
    const float width = r.w * tween::clamp01(share);
    if (width < 1.5f)
        return;
    paint.fill({r.x, r.y, width, r.h}, std::min(radius, width * 0.5f), color);
}

Color tone(const Theme &theme, const HudStyle &style, Status status)
{
    return status == Status::neutral ? ink_of(style) : status_color(theme, status);
}

} // namespace

// ---- HealthBar --------------------------------------------------------------

void HealthBar::set_max(float max)
{
    max_ = std::max(max, 1.0f);
    value_ = std::clamp(value_, 0.0f, max_);
    shield_value_ = std::clamp(shield_value_, 0.0f, max_);
}

bool HealthBar::low() const
{
    return value_ <= max_ * style.low;
}

void HealthBar::set_value(float value, bool snap)
{
    value = std::clamp(value, 0.0f, max_);
    const float share = value / max_;
    if (snap)
    {
        value_ = value;
        fill_.snap(share);
        ghost_.snap(share);
        danger_.snap(low() ? 1.0f : 0.0f);
        ghost_hold_ = 0.0f;
        hit_.value = 0.0f;
        heal_.value = 0.0f;
        return;
    }
    if (value < value_)
    {
        // A harder hit jolts harder; the ghost waits again from the new blow.
        hit_.trigger(std::min(1.0f, 0.35f + 3.0f * (value_ - value) / max_));
        ghost_hold_ = style.ghost_delay;
    }
    else if (value > value_)
    {
        heal_.trigger();
    }
    value_ = value;
}

void HealthBar::set_shield(float shield, bool snap)
{
    shield_value_ = std::clamp(shield, 0.0f, max_);
    if (snap)
        shield_.snap(shield_value_ / max_);
}

void HealthBar::update(float dt)
{
    const float share = value_ / max_;
    fill_.target = share;
    // Losing health is immediate (the ghost tells the story); gaining it grows.
    fill_.update(dt, fill_.value > share ? 46.0f : std::max(style.omega(), 9.0f));
    if (ghost_hold_ > 0.0f)
    {
        ghost_hold_ -= dt;
    }
    else
    {
        ghost_.target = share;
        ghost_.update(dt, std::max(style.omega() * style.ghost_rate, 3.0f));
    }
    if (!style.ghost || ghost_.value < fill_.value)
        ghost_.snap(fill_.value);
    shield_.target = shield_value_ / max_;
    shield_.update(dt, std::max(style.omega(), 9.0f));
    danger_.target = low() ? 1.0f : 0.0f;
    danger_.update(dt, 10.0f);
    hit_.update(dt, 7.0f);
    heal_.update(dt, 3.5f);
}

void HealthBar::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const bool calm = style.reduced_motion;
    draw_backing(canvas, style, bounds_);

    const float jolt = calm ? 0.0f : shake(hit_.value, canvas.time, style.shake, 11.0f);
    list.push_transform(1.0f, 0.0f, 0.0f, jolt, 0.0f);

    const Color ink = ink_of(style);
    const Color quiet = quiet_of(style);
    const float height = std::min(style.bar_height, bounds_.h);
    const Rect bar{bounds_.x, bounds_.y + bounds_.h - height, bounds_.w, height};
    const float danger = tween::clamp01(danger_.value);
    const Color low_color = status_color(theme, style.low_status);
    const Color base = gfx::mix(tone(theme, style, style.status), low_color, danger);

    // ---- the line above the bar ----
    const float baseline = bar.y - 11.0f;
    float value_room = 0.0f;
    if (style.show_value && baseline - style.value_size * 0.7f >= bounds_.y - 1.0f)
    {
        char text[24];
        std::snprintf(text, sizeof(text), " / %d", static_cast<int>(max_ + 0.5f));
        const float small = style.value_size * 0.8f;
        const float of = tabular_width(paint, Face::label, text, small);
        hud_tabular(paint, style, Face::label, text, bar.x + bar.w - of, baseline, small, quiet);
        std::snprintf(text, sizeof(text), "%d", static_cast<int>(value_ + 0.5f));
        const float now = tabular_width(paint, Face::label, text, style.value_size);
        hud_tabular(paint, style, Face::label, text, bar.x + bar.w - of - now, baseline,
                    style.value_size, gfx::mix(ink, plated(style) ? low_color : ink, danger));
        value_room = of + now + 16.0f;
    }
    if (!title.empty() && baseline - style.title_size * 0.7f >= bounds_.y - 1.0f)
        hud_text(paint, style, Face::label,
                 fit_label(paint, title, style.title_size, std::max(bar.w - value_room, 40.0f)),
                 bar.x, baseline, style.title_size, quiet);

    // ---- the bar ----
    const float fill = tween::clamp01(fill_.value);
    const float ghost = tween::clamp01(ghost_.value);
    const float shield = tween::clamp01(shield_.value);
    const float breath =
        danger * (style.low_pulse ? (calm ? 0.5f : breathe(canvas.time, 0.7f)) : 0.0f);
    const Color body = gfx::mix(base, kWhite, 0.3f * breath + 0.45f * heal_.value);
    const Color pale = gfx::mix(base, kWhite, 0.66f);
    const Color guard = legible(style, status_color(theme, style.shield_status));
    const float inset = track_inset(style);

    if (style.kind == HealthKind::segmented)
    {
        const int count = std::max(style.segments, 1);
        const float n = static_cast<float>(count);
        const float width = (bar.w - style.segment_gap * (n - 1.0f)) / n;
        for (int i = 0; i < count; ++i)
        {
            const float at = static_cast<float>(i);
            const Rect cell{bar.x + at * (width + style.segment_gap), bar.y, width, bar.h};
            const float radius = corner_for(theme, cell, 6.0f);
            const float lit = tween::clamp01(fill * n - at);
            if (breath > 0.01f && lit > 0.0f)
                paint.halo(cell, radius, 12.0f, low_color.with_alpha(0.42f * breath));
            draw_track(paint, style, cell, radius);
            const Rect in = cell.inset(inset);
            const float inner = std::max(radius - inset, 0.0f);
            const float lost = tween::clamp01(ghost * n - at);
            if (lost > lit + 0.01f)
                fill_share(paint, in, inner, lost, pale);
            fill_share(paint, in, inner, lit, body);
            const float held = tween::clamp01(shield * n - at);
            fill_share(paint, {in.x, in.y, in.w, std::max(3.0f, in.h * style.shield_height)}, inner,
                       held, guard);
        }
    }
    else
    {
        const float radius = corner_for(theme, bar, bar.h * 0.5f);
        if (breath > 0.01f)
            paint.halo(bar, radius, 16.0f, low_color.with_alpha(0.45f * breath));
        draw_track(paint, style, bar, radius);
        const Rect in = bar.inset(inset);
        const float inner = std::max(radius - inset, 0.0f);
        if (ghost > fill + 0.002f)
            fill_share(paint, in, inner, ghost, pale);
        fill_share(paint, in, inner, fill, body);
        fill_share(paint, {in.x, in.y, in.w, std::max(3.0f, in.h * style.shield_height)}, inner,
                   shield, guard);
        // Quarter marks: a hit is judged against them.
        for (int i = 1; i < 4; ++i)
            list.rounded_rect(
                {in.x + in.w * 0.25f * static_cast<float>(i) - 1.0f, in.y, 2.0f, in.h}, 0.0f,
                deep_shade(style).with_alpha(0.4f));
    }
    if (hit_.value > 0.02f)
        paint.stroke(bar.inset(-3.0f), corner_for(theme, bar, bar.h * 0.5f) + 3.0f, 2.0f,
                     gfx::mix(low_color, kWhite, 0.5f).with_alpha(hit_.value));
    list.pop_transform();
}

// ---- AmmoCounter ------------------------------------------------------------

void AmmoCounter::set_capacity(int capacity)
{
    capacity_ = std::max(capacity, 1);
}

void AmmoCounter::set_ammo(int current, int reserve)
{
    current = std::max(current, 0);
    if (current < current_)
        kick_.trigger();
    current_ = current;
    reserve_ = std::max(reserve, 0);
}

void AmmoCounter::set_reload(float progress)
{
    reload_ = progress < 0.0f ? -1.0f : tween::clamp01(progress);
}

Status AmmoCounter::level() const
{
    if (current_ <= 0)
        return Status::danger;
    const float share = static_cast<float>(current_) / static_cast<float>(capacity_);
    return share <= style.low ? Status::warning : Status::neutral;
}

void AmmoCounter::update(float dt)
{
    level_.target = tween::clamp01(static_cast<float>(current_) / static_cast<float>(capacity_));
    level_.update(dt, 24.0f);
    reloading_.target = reloading() ? 1.0f : 0.0f;
    reloading_.update(dt, 16.0f);
    kick_.update(dt, 14.0f);
}

void AmmoCounter::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const Color ink = ink_of(style);
    const Color quiet = quiet_of(style);

    char digits[16];
    std::snprintf(digits, sizeof(digits), "%0*d", std::clamp(style.min_digits, 1, 6), current_);
    int zeros = 0;
    while (digits[zeros] == '0' && digits[zeros + 1] != '\0')
        ++zeros;
    char of[24];
    std::snprintf(of, sizeof(of), "/ %d", reserve_);

    const float ring = style.ring ? style.ring_size : 0.0f;
    const float digits_w = tabular_width(paint, Face::heading, digits, style.digits_size);
    const float of_w = std::max(tabular_width(paint, Face::label, of, style.reserve_size),
                                paint.label_width(style.label, style.label_size));
    const float total = ring + (ring > 0.0f ? 18.0f : 0.0f) + digits_w + 12.0f + of_w;
    float x = bounds_.x;
    if (style.align == gfx::Align::right)
        x = bounds_.x + bounds_.w - total;
    else if (style.align == gfx::Align::center)
        x = bounds_.cx() - total * 0.5f;
    draw_backing(canvas, style, {x, bounds_.y, total, bounds_.h});

    const bool with_pips = style.pips && capacity_ <= 60;
    const float block = bounds_.h - (with_pips ? style.pip_height + 8.0f : 0.0f);
    const float cy = bounds_.y + block * 0.5f;
    const float baseline = cy + style.digits_size * 0.35f;
    const float busy = tween::clamp01(reloading_.value);
    const Color count = gfx::mix(tone(theme, style, level()), quiet, 0.5f * busy);
    const bool flat_caps = square_theme(theme);

    if (style.ring)
    {
        const float rx = x + ring * 0.5f;
        const float radius = ring * 0.5f;
        if (!plated(style))
            list.circle(rx, cy, radius, deep_shade(style).with_alpha(0.7f));
        list.arc(rx, cy, radius, style.ring_thickness, 0.0f, kTau,
                 plated(style) ? theme.surface_high : ink.with_alpha(0.18f), false);
        // The magazine drains round the ring; a reload sweeps over it.
        const float level = tween::clamp01(level_.value);
        if (level > 0.004f)
            list.arc(rx, cy, radius, style.ring_thickness, 0.0f, kTau * level,
                     count.with_alpha(1.0f - busy), !flat_caps);
        if (reload_ > 0.004f)
            list.arc(rx, cy, radius, style.ring_thickness, 0.0f, kTau * reload_,
                     legible(style, theme.accent).with_alpha(busy), !flat_caps);
        // A round, standing: the ring says what it counts.
        const float tall = ring * 0.3f;
        const Rect round{rx - tall * 0.2f, cy - tall * 0.5f, tall * 0.4f, tall};
        list.rounded_rect(round, flat_caps ? 0.0f : tall * 0.2f, quiet);
        x += ring + 18.0f;
    }

    const float pop = style.reduced_motion ? 0.0f : 0.09f * kick_.value;
    list.push_transform(1.0f + pop, x + digits_w * 0.5f, baseline - style.digits_size * 0.35f, 0.0f,
                        0.0f);
    hud_tabular(paint, style, Face::heading, digits, x, baseline, style.digits_size, count, zeros);
    list.pop_transform();
    const float digits_left = x;
    x += digits_w + 12.0f;
    hud_tabular(paint, style, Face::label, of, x, baseline, style.reserve_size, quiet);
    if (!style.label.empty())
        hud_text(paint, style, Face::label, style.label, x, baseline - style.reserve_size - 8.0f,
                 style.label_size, quiet);

    if (with_pips)
    {
        const float span = digits_w + 12.0f + of_w;
        const float n = static_cast<float>(capacity_);
        const float gap = capacity_ > 30 ? 2.0f : 3.0f;
        const float width = std::max((span - gap * (n - 1.0f)) / n, 1.5f);
        const float y = bounds_.y + bounds_.h - style.pip_height;
        for (int i = 0; i < capacity_; ++i)
        {
            const Rect pip{digits_left + static_cast<float>(i) * (width + gap), y, width,
                           style.pip_height};
            list.rounded_rect(pip, flat_caps ? 0.0f : std::min(width * 0.5f, 2.0f),
                              i < current_ ? count : quiet.with_alpha(0.3f));
        }
    }
}

// ---- ObjectiveTracker -------------------------------------------------------

int ObjectiveTracker::add(std::string text, std::string distance, Feedback *feedback)
{
    Row row;
    row.id = next_id_++;
    row.text = std::move(text);
    row.distance = std::move(distance);
    rows_.push_back(std::move(row));
    if (feedback != nullptr)
        play_cue(*feedback, style, style.add_cue, bounds_.cx());
    return rows_.back().id;
}

bool ObjectiveTracker::complete(int id, Feedback *feedback)
{
    for (Row &row : rows_)
    {
        if (row.id != id || row.done)
            continue;
        row.done = true;
        row.left = style.linger;
        if (feedback != nullptr)
            play_cue(*feedback, style, style.complete_cue, bounds_.cx());
        return true;
    }
    return false;
}

void ObjectiveTracker::set_distance(int id, std::string distance)
{
    for (Row &row : rows_)
    {
        if (row.id == id)
            row.distance = std::move(distance);
    }
}

void ObjectiveTracker::clear(bool now)
{
    if (now)
    {
        rows_.clear();
        return;
    }
    for (Row &row : rows_)
        row.leaving = true;
}

int ObjectiveTracker::count() const
{
    int count = 0;
    for (const Row &row : rows_)
        count += row.leaving ? 0 : 1;
    return count;
}

int ObjectiveTracker::remaining() const
{
    int count = 0;
    for (const Row &row : rows_)
        count += row.leaving || row.done ? 0 : 1;
    return count;
}

bool ObjectiveTracker::done(int id) const
{
    for (const Row &row : rows_)
    {
        if (row.id == id)
            return row.done;
    }
    return false;
}

void ObjectiveTracker::update(float dt)
{
    int listed = 0;
    for (Row &row : rows_)
    {
        // Only the first max_rows have room; the others wait, unseen.
        row.shown = row.shown || listed < style.max_rows;
        if (!row.leaving)
            ++listed;
        if (!row.shown)
            continue;
        row.age += dt;
        if (row.done)
        {
            row.tick = std::min(1.0f, row.tick + dt / std::max(style.tick_time, 0.01f));
            if (row.tick >= 1.0f && style.linger >= 0.0f)
            {
                row.left -= dt;
                if (row.left <= 0.0f)
                    row.leaving = true;
            }
        }
        row.slot.target = row.leaving ? 0.0f : 1.0f;
        row.slot.update(dt, std::max(style.omega(), 12.0f));
    }
    std::erase_if(rows_, [](const Row &row) { return row.leaving && row.slot.value < 0.01f; });
}

void ObjectiveTracker::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const bool calm = style.reduced_motion;
    const Color ink = ink_of(style);
    const Color quiet = quiet_of(style);
    const Color done_color = status_color(theme, style.done_status);

    const float head = title.empty() ? 0.0f : style.title_size + 16.0f;
    float used = head;
    for (const Row &row : rows_)
        used += style.row_height * tween::clamp01(row.slot.value);
    if (used <= 0.5f)
        return;
    draw_backing(canvas, style, {bounds_.x, bounds_.y, bounds_.w, std::min(used, bounds_.h)});

    float y = bounds_.y;
    if (!title.empty())
    {
        // A short bar in the accent colour marks the block as "the mission".
        const float mark = style.title_size * 0.8f;
        paint.fill({bounds_.x, y + 1.0f, 4.0f, mark}, square_theme(theme) ? 0.0f : 2.0f,
                   legible(style, theme.accent));
        hud_text(paint, style, Face::label,
                 fit_label(paint, title, style.title_size, bounds_.w - 14.0f), bounds_.x + 14.0f,
                 y + style.title_size * 0.8f, style.title_size, quiet);
        y += head;
    }

    list.push_clip({bounds_.x - 40.0f, bounds_.y - 8.0f, bounds_.w + 80.0f, bounds_.h + 16.0f});
    bool first_open = true;
    for (const Row &row : rows_)
    {
        const float slot = tween::clamp01(row.slot.value);
        if (!row.shown || slot <= 0.004f)
            continue;
        const float appear = tween::cubic_out(row.age / (calm ? 0.2f : 0.36f));
        const Rect r{bounds_.x, y, bounds_.w, style.row_height};
        y += style.row_height * slot;
        if (r.y + r.h > bounds_.y + bounds_.h + 1.0f)
            break;
        list.push_opacity(appear * (row.leaving ? slot : 1.0f));
        list.push_transform(1.0f, 0.0f, 0.0f, calm ? 0.0f : style.slide * (1.0f - appear), 0.0f);

        const float p = row.tick;
        const float size = style.check_size;
        const Rect box{r.x, r.cy() - size * 0.5f, size, size};
        const float radius = corner_for(theme, box, size * 0.28f);
        if (!plated(style))
            paint.fill(box, radius, deep_shade(style).with_alpha(0.6f));
        paint.fill(box, radius, done_color.with_alpha(p));
        paint.stroke(box, radius, 2.0f, gfx::mix(quiet, done_color, p));
        // The tick is two strokes that grow one after the other: the short
        // down-stroke first, then the long one up.
        const float first = tween::clamp01(p / 0.35f);
        const float second = tween::cubic_out((p - 0.35f) / 0.65f);
        const Color mark = Painter::on(done_color);
        const float pen = std::max(2.5f, size * 0.14f);
        const float ax = box.x + size * 0.25f, ay = box.y + size * 0.52f;
        const float bx = box.x + size * 0.43f, by = box.y + size * 0.7f;
        const float cx = box.x + size * 0.77f, cy = box.y + size * 0.3f;
        if (first > 0.0f)
            list.line(ax, ay, tween::lerp(ax, bx, first), tween::lerp(ay, by, first), pen, mark);
        if (second > 0.0f)
            list.line(bx, by, tween::lerp(bx, cx, second), tween::lerp(by, cy, second), pen, mark);

        const float left = box.x + size + 14.0f;
        float right = r.x + r.w;
        const float baseline = r.cy() + style.text_size * 0.35f;
        if (!row.distance.empty() && p < 0.99f)
        {
            list.push_opacity(1.0f - p);
            right -= hud_text(paint, style, Face::label, row.distance, right, baseline,
                              style.distance_size, quiet, gfx::Align::right) +
                     14.0f;
            list.pop_opacity();
        }
        // The next thing to do is the brightest line of the list.
        const bool current = !row.done && first_open;
        if (!row.done)
            first_open = false;
        const Color rest = current ? ink : gfx::mix(quiet, ink, 0.5f);
        const std::string text =
            fit_body(paint, row.text, style.text_size, std::max(right - left, 40.0f));
        const float width = hud_text(paint, style, Face::body, text, left, baseline,
                                     style.text_size, gfx::mix(rest, quiet, p));
        if (p > 0.0f)
            list.rounded_rect(
                {left - 2.0f, r.cy() - 1.0f, (width + 4.0f) * tween::cubic_out(p), 2.0f}, 0.0f,
                quiet);
        list.pop_transform();
        list.pop_opacity();
    }
    list.pop_clip();
}

// ---- MinimapFrame -----------------------------------------------------------

void MinimapFrame::set_heading(float radians, bool snap)
{
    // Go to the nearest turn of the circle that shows this heading.
    heading_.target += std::remainder(radians - heading_.target, kTau);
    if (snap)
        heading_.snap(heading_.target);
}

float MinimapFrame::heading() const
{
    const float wrapped = std::fmod(heading_.target, kTau);
    return wrapped < 0.0f ? wrapped + kTau : wrapped;
}

Rect MinimapFrame::frame() const
{
    const float side = std::max(std::min(bounds_.w, bounds_.h), 0.0f);
    return {bounds_.cx() - side * 0.5f, bounds_.cy() - side * 0.5f, side, side};
}

Rect MinimapFrame::inner() const
{
    return frame().inset(style.rim + style.inset);
}

void MinimapFrame::pip_position(const MapPip &pip, float *x, float *y) const
{
    const Rect in = inner();
    const float reach = std::max(in.w * 0.5f - style.pip_size * 0.5f, 1.0f);
    const float angle = pip.angle - (style.rotate ? heading_.value : 0.0f);
    float dx = std::sin(angle);
    float dy = -std::cos(angle);
    if (style.shape == MinimapShape::round)
    {
        const float d = std::clamp(pip.distance, 0.0f, 1.0f) * reach;
        dx *= d;
        dy *= d;
    }
    else
    {
        // A square has more room toward its corners: clamp per axis.
        const float d = std::max(pip.distance, 0.0f) * reach;
        dx = std::clamp(dx * d, -reach, reach);
        dy = std::clamp(dy * d, -reach, reach);
    }
    *x = in.cx() + dx;
    *y = in.cy() + dy;
}

void MinimapFrame::update(float dt)
{
    heading_.update(dt, std::max(style.omega(), 10.0f));
}

void MinimapFrame::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const Rect f = frame();
    if (f.w < 24.0f)
        return;
    const bool round = style.shape == MinimapShape::round;
    const bool plate = plated(style);
    const bool calm = style.reduced_motion;
    const float cx = f.cx();
    const float cy = f.cy();
    const float radius = f.w * 0.5f;
    const float corner = round ? radius : std::min(theme.radius_card, radius);
    const Color ink = ink_of(style);
    const Color deep = deep_shade(style);
    const float turned = style.rotate ? heading_.value : 0.0f;

    // ---- the ground the map lies on ----
    if (style.backing == HudBacking::glow)
        list.shadow(f, corner, style.glow_spread, style.shade);
    if (plate)
    {
        const Color solid = solid_surface(theme);
        if (round)
        {
            if (theme.shadow.a > 0.0f)
                list.shadow({f.x, f.y + theme.shadow_offset * 0.5f, f.w, f.h}, radius,
                            std::max(theme.shadow_blur, 12.0f), theme.shadow);
            const bool frost = style.frosted && canvas.glass != 0;
            if (frost)
                list.glass(canvas.glass, f, radius, kWhite);
            list.circle(cx, cy, radius, frost ? solid.with_alpha(0.55f) : solid);
        }
        else
        {
            draw_overlay_panel(canvas, theme, f, style.frosted && canvas.glass != 0, 0.55f, corner);
        }
    }
    else if (round)
    {
        list.circle(cx, cy, radius, deep.with_alpha(0.86f));
    }
    else
    {
        paint.fill(f, corner, deep.with_alpha(0.86f));
    }

    // ---- the map ----
    const Rect in = inner();
    if (content)
    {
        content(canvas, in, turned, style.shape);
    }
    else if (style.grid)
    {
        const Color line = (plate ? theme.text_muted : ink).with_alpha(plate ? 0.3f : 0.16f);
        const float reach = in.w * 0.5f;
        if (round)
        {
            list.ring(cx, cy, reach, 1.5f, line);
            list.ring(cx, cy, reach * 0.55f, 1.5f, line);
        }
        else
        {
            paint.stroke(in.inset(reach * 0.45f), 0.0f, 1.5f, line);
        }
        // A cross that turns with the map shows the turn even without pips.
        list.push_clip(in);
        const float span = round ? reach : reach * 1.5f;
        for (int i = 0; i < 2; ++i)
        {
            const float angle = -turned + static_cast<float>(i) * kTau * 0.25f;
            const float dx = std::sin(angle) * span;
            const float dy = -std::cos(angle) * span;
            list.line(cx - dx, cy - dy, cx + dx, cy + dy, 1.5f, line);
        }
        list.pop_clip();
    }

    // ---- points of interest ----
    const bool squares = square_theme(theme);
    for (const MapPip &pip : pips_)
    {
        float x = 0.0f;
        float y = 0.0f;
        pip_position(pip, &x, &y);
        const Color color = legible(style, pip.color.a > 0.0f ? pip.color : theme.accent);
        // Out of range: it sits on the rim, smaller, and only says "that way".
        const bool far = pip.distance > 1.0f;
        const float size = style.pip_size * (far ? 0.72f : 1.0f);
        const float half = size * 0.5f;
        if (pip.objective)
        {
            if (!calm)
            {
                const float beat = breathe(canvas.time, 1.6f);
                list.ring(x, y, half + 4.0f + 6.0f * beat, 2.0f,
                          color.with_alpha(0.7f * (1 - beat)));
            }
            list.rotated_rect({x - half - 2.0f, y - half - 2.0f, size + 4.0f, size + 4.0f}, 1.0f,
                              kTau * 0.125f, deep);
            list.rotated_rect({x - half, y - half, size, size}, 1.0f, kTau * 0.125f, color);
        }
        else if (squares)
        {
            list.rounded_rect({x - half - 2.0f, y - half - 2.0f, size + 4.0f, size + 4.0f}, 0.0f,
                              deep);
            list.rounded_rect({x - half, y - half, size, size}, 0.0f, color);
        }
        else
        {
            list.circle(x, y, half + 2.0f, deep);
            list.circle(x, y, half, color);
        }
    }

    // ---- the player ----
    if (style.player_size > 0.0f)
    {
        const float s = style.player_size;
        const float angle = style.rotate ? 0.0f : heading_.value;
        list.triangle({cx - s * 0.5f - 2.5f, cy - s * 0.5f - 3.0f, s + 5.0f, s + 5.0f}, deep, 0.0f,
                      angle);
        list.triangle({cx - s * 0.5f, cy - s * 0.5f, s, s},
                      plate ? legible(style, theme.primary) : ink, 0.0f, angle);
    }

    // ---- the rim and north ----
    const Color rim = plate ? gfx::mix(theme.text_muted, theme.text, 0.25f) : ink.with_alpha(0.85f);
    if (round)
        list.ring(cx, cy, radius, style.rim, rim);
    else
        paint.stroke(f, corner, style.rim, rim);

    if (style.north_size > 0.0f)
    {
        const float north = -turned;
        float dx = std::sin(north);
        float dy = -std::cos(north);
        const float reach = radius - style.rim * 0.5f;
        if (!round)
        {
            // Slide along the square's edge instead of cutting its corners.
            const float longest = std::max(std::fabs(dx), std::fabs(dy));
            dx /= std::max(longest, 0.001f);
            dy /= std::max(longest, 0.001f);
        }
        const float nx = cx + dx * reach;
        const float ny = cy + dy * reach;
        const float size = style.north_size;
        const Rect tag{nx - size * 0.5f, ny - size * 0.5f, size, size};
        const Color marker = legible(style, theme.primary);
        if (squares)
        {
            paint.fill(tag.inset(-2.0f), 0.0f, deep);
            paint.fill(tag, 0.0f, marker);
        }
        else
        {
            list.circle(nx, ny, size * 0.5f + 2.0f, deep);
            list.circle(nx, ny, size * 0.5f, marker);
        }
        const float text = size * 0.62f;
        paint.label("N", nx, ny + text * 0.36f, text, Painter::on(marker), gfx::Align::center);
    }
}

} // namespace hui::ui
