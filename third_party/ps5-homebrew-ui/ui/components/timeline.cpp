// ps5-homebrew-ui - Component: Timeline.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/timeline.hpp"

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

constexpr float kCardGap = 8.0f;  // between an entry's text and its card
constexpr float kNodeGap = 5.0f;  // the rail stops this far from a node
constexpr float kTimeGap = 14.0f; // between the time column and the rail

bool rounded(const Theme &theme)
{
    return theme.corner == Corner::round && theme.radius >= 2.0f;
}

} // namespace

void Timeline::set_entries(std::vector<TimelineEntry> entries)
{
    entries_ = std::move(entries);
    lines_.assign(entries_.size(), 1);
    const int count = static_cast<int>(entries_.size());
    focus_ = std::clamp(focus_, 0, std::max(count - 1, 0));
    if (count > 0 && entries_[static_cast<std::size_t>(focus_)].group)
    {
        const int next = step(focus_, 1);
        focus_ = next >= 0 ? next : std::max(step(focus_, -1), 0);
    }
    relayout();
    retarget(true);
    settle_ = true;
}

void Timeline::set_bounds(const Rect &bounds)
{
    bounds_ = bounds;
    relayout();
    retarget(true);
    settle_ = true;
}

void Timeline::set_focus(int index, bool snap)
{
    if (index < 0 || index >= static_cast<int>(entries_.size()) ||
        entries_[static_cast<std::size_t>(index)].group)
        return;
    focus_ = index;
    retarget(snap);
}

void Timeline::enter()
{
    age_ = 0.0f;
}

Rect Timeline::inner() const
{
    if (style.panel)
        return bounds_.inset(style.panel_padding);
    // Without a panel's padding the scroll thumb needs room of its own.
    return {bounds_.x, bounds_.y, bounds_.w - (style.scroll_thumb ? 10.0f : 0.0f), bounds_.h};
}

float Timeline::rail_x() const
{
    const float column = style.time == TimelineTime::column ? style.time_width + kTimeGap : 0.0f;
    return style.padding + column + style.node_size * 0.5f;
}

float Timeline::title_line() const
{
    return style.title_size * 1.3f;
}

float Timeline::body_line() const
{
    return style.body_size * 1.3f;
}

int Timeline::step(int from, int direction) const
{
    const int count = static_cast<int>(entries_.size());
    for (int i = from + direction; i >= 0 && i < count; i += direction)
    {
        if (!entries_[static_cast<std::size_t>(i)].group)
            return i;
    }
    return -1;
}

// The width an entry's words may take.
float Timeline::body_room() const
{
    const float text_left = rail_x() + style.node_size * 0.5f + style.rail_gap;
    return std::max(inner().w - style.padding - text_left, 20.0f);
}

void Timeline::measure(const Fonts &fonts)
{
    // A painter only measures here; the list it is given stays empty.
    gfx::DrawList scratch;
    const Painter paint(scratch, fonts, style.theme);
    lines_.assign(entries_.size(), 1);
    for (std::size_t i = 0; i < entries_.size(); ++i)
    {
        if (entries_[i].group || entries_[i].body.empty() || style.body_lines <= 0)
            continue;
        const std::vector<std::string> lines =
            wrap_body(paint, entries_[i].body, style.body_size, body_room(), style.body_lines);
        lines_[i] = std::max(static_cast<int>(lines.size()), 1);
    }
    relayout();
    retarget(true);
    settle_ = true;
}

void Timeline::relayout()
{
    const std::size_t count = entries_.size();
    tops_.resize(count);
    heights_.resize(count);
    if (lines_.size() != count)
        lines_.assign(count, 1);
    float y = 0.0f;
    for (std::size_t i = 0; i < count; ++i)
    {
        const TimelineEntry &entry = entries_[i];
        float h = style.group_height;
        if (!entry.group)
        {
            h = 2.0f * style.padding_y + title_line();
            if (style.body_lines > 0 && !entry.body.empty())
                h += static_cast<float>(std::max(lines_[i], 1)) * body_line();
            if (entry.card_height > 0.0f)
                h += kCardGap + entry.card_height;
        }
        tops_[i] = y;
        heights_[i] = h;
        y += h + style.entry_gap;
    }
    content_ = std::max(y - style.entry_gap, 0.0f);
}

void Timeline::retarget(bool snap)
{
    if (entries_.empty())
        return;
    const Rect in = inner();
    const std::size_t at = static_cast<std::size_t>(focus_);
    // Reveal the group title above the first entry of a group too.
    const bool under_group = focus_ > 0 && entries_[at - 1].group;
    scroll_.reveal(under_group ? tops_[at - 1] : tops_[at], tops_[at] + heights_[at], in.h, 8.0f,
                   content_);
    if (snap)
        scroll_.position.snap(scroll_.position.target);
    const Rect target{0.0f, tops_[at], in.w, heights_[at]};
    highlight_.target(target);
    if (snap)
        highlight_.snap(target);
}

Event Timeline::handle(const InputFrame &input, Feedback &feedback)
{
    exit_ = Direction::none;
    if (entries_.empty())
        return Event::none;
    const float x = bounds_.cx();
    if (input.nav != Direction::none)
    {
        int next = -1;
        if (input.nav == Direction::up || input.nav == Direction::down)
            next = step(focus_, input.nav == Direction::down ? 1 : -1);
        if (next < 0)
        {
            if (style.exits.allows(input.nav))
            {
                exit_ = input.nav;
                return Event::none;
            }
            return refuse(feedback, style, input, highlight_.refusal(), x);
        }
        focus_ = next;
        retarget(false);
        const float along = entries_.size() > 1 ? static_cast<float>(focus_) /
                                                      static_cast<float>(entries_.size() - 1)
                                                : 0.0f;
        // Older entries sound lower: the feed runs down into the past.
        play_cue(feedback, style, style.sounds.move, x, tween::lerp(1.06f, 0.94f, along));
        return Event::moved;
    }
    if (input.is_pressed(Action::confirm) && !entries_[static_cast<std::size_t>(focus_)].group)
    {
        press_.trigger();
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

void Timeline::update(float dt)
{
    age_ += dt;
    relayout();
    retarget(settle_);
    settle_ = false;
    highlight_.update(dt, style);
    scroll_.update(dt, std::max(style.omega(), 14.0f));
    active_amount_.target = active_ ? 1.0f : 0.0f;
    active_amount_.update(dt, 18.0f);
    press_.update(dt, 10.0f);
}

Rect Timeline::entry_rect(int index) const
{
    const Rect in = inner();
    if (index < 0 || index >= static_cast<int>(tops_.size()))
        return in;
    const std::size_t at = static_cast<std::size_t>(index);
    return {in.x, in.y + tops_[at] - scroll_.offset(), in.w, heights_[at]};
}

void Timeline::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    if (style.panel)
        paint.panel(bounds_);
    if (entries_.empty() || tops_.size() != entries_.size())
        return;

    const Rect in = inner();
    const Inks ink = inks(paint, style.panel);
    const int count = static_cast<int>(entries_.size());
    const bool overflow = content_ > in.h + 0.5f;
    const float scroll = scroll_.offset();
    const float rail = in.x + rail_x();
    const float text_left = rail + style.node_size * 0.5f + style.rail_gap;
    const float right = in.x + in.w - style.padding;
    const float half = style.node_size * 0.5f;

    // How far down the rail has drawn itself, in content space.
    const bool plain = style.reduced_motion || style.draw_in <= 0.0f;
    const float tip =
        plain ? content_ + 100.0f : tween::cubic_out(age_ / style.draw_in) * (content_ + 40.0f);
    const float fade = plain ? tween::cubic_out(age_ / 0.2f) : 1.0f;
    const auto node_y = [&](int index)
    { return tops_[static_cast<std::size_t>(index)] + style.padding_y + title_line() * 0.5f; };
    // An entry appears as the rail's tip reaches its node.
    const auto appear = [&](int index)
    {
        const float at = entries_[static_cast<std::size_t>(index)].group
                             ? tops_[static_cast<std::size_t>(index)]
                             : node_y(index);
        return tween::clamp01((tip - at) / 36.0f) * fade;
    };
    const auto visibility = [&](const Rect &r) { return overflow ? row_visibility(r, in) : 1.0f; };

    const float bleed = style.highlight.kind == HighlightKind::glow ? 30.0f : 10.0f;
    const float above = overflow ? 2.0f : bleed;
    list.push_clip({in.x - bleed, in.y - above, in.w + 2.0f * bleed, in.h + 2.0f * above});

    list.push_transform(1.0f, 0.0f, 0.0f, in.x, in.y - scroll);
    {
        HighlightStyle look = style.highlight;
        look.grow -= 2.0f * press_.value;
        draw_focus(canvas, style, highlight_, look, active_amount_.value, appear(focus_));
    }
    list.pop_transform();

    // The rail: one piece between each pair of neighbouring nodes, so a
    // group title interrupts it and a node never has a line through it.
    const Color rail_color = ink.muted.with_alpha(0.42f * fade);
    for (int i = 0; i + 1 < count; ++i)
    {
        if (entries_[static_cast<std::size_t>(i)].group ||
            entries_[static_cast<std::size_t>(i + 1)].group)
            continue;
        const float from = node_y(i) + half + kNodeGap;
        const float to = std::min(node_y(i + 1) - half - kNodeGap, tip);
        if (to - from < 1.0f)
            continue;
        const float y = in.y + from - scroll;
        list.rounded_rect({rail - style.rail_width * 0.5f, y, style.rail_width, to - from},
                          rounded(theme) ? style.rail_width * 0.5f : 0.0f, rail_color);
    }

    for (int i = 0; i < count; ++i)
    {
        const TimelineEntry &entry = entries_[static_cast<std::size_t>(i)];
        const Rect r = entry_rect(i);
        const float shown = appear(i);
        const float alpha = visibility(r) * shown;
        if (alpha <= 0.0f || r.y > in.y + in.h || r.y + r.h < in.y)
            continue;
        list.push_opacity(alpha);

        if (entry.group)
        {
            const float baseline = r.y + r.h - 12.0f;
            const float x = in.x + style.padding;
            const float width =
                paint.label(fit_label(paint, entry.title, style.group_size, right - x), x, baseline,
                            style.group_size, ink.muted);
            if (right - (x + width + 14.0f) > 20.0f)
                list.rounded_rect({x + width + 14.0f, baseline - style.group_size * 0.35f,
                                   right - (x + width + 14.0f), 1.5f},
                                  0.0f, rule_color(paint, style.panel));
            list.pop_opacity();
            continue;
        }

        const float focus =
            i == focus_
                ? highlight_.coverage({0.0f, tops_[static_cast<std::size_t>(i)], in.w, r.h}) *
                      active_amount_.value
                : 0.0f;
        const Color strong = Highlight::text_color(style, style.highlight, focus, ink.text);
        const Color quiet = gfx::mix(ink.muted, strong, focus * 0.6f);
        const Color tone = visible_on(
            theme, entry.color.a > 0.0f ? entry.color : status_color(theme, entry.status),
            style.panel);

        // ---- the node ----
        const float cy = in.y + node_y(i) - scroll;
        const float pop = style.reduced_motion ? 1.0f : tween::back_out(shown);
        const float radius = half * (1.0f + 0.22f * focus) * pop;
        if (icon)
        {
            icon(canvas, {rail - radius, cy - radius, 2.0f * radius, 2.0f * radius}, entry, i,
                 focus);
        }
        else
        {
            if (focus > 0.01f)
                draw_marker(canvas, theme, rail, cy, radius + 5.0f, tone.with_alpha(0.28f * focus));
            if (style.node == TimelineNode::filled)
                draw_marker(canvas, theme, rail, cy, radius, tone);
            else if (rounded(theme))
                list.ring(rail, cy, radius, std::max(3.0f, radius * 0.34f), tone);
            else
                list.bordered_rect({rail - radius, cy - radius, 2.0f * radius, 2.0f * radius}, 0.0f,
                                   Color{tone.r, tone.g, tone.b, 0.0f},
                                   std::max(3.0f, radius * 0.34f), tone);
        }

        // ---- the words ----
        const float baseline =
            r.y + style.padding_y + title_line() * 0.5f + style.title_size * 0.35f;
        float title_room = right - text_left;
        if (!entry.time.empty() && style.time == TimelineTime::column)
        {
            const float edge = in.x + style.padding + style.time_width;
            paint.body(fit_body(paint, entry.time, style.time_size, style.time_width), edge,
                       baseline - (style.title_size - style.time_size) * 0.2f, style.time_size,
                       quiet, gfx::Align::right);
        }
        else if (!entry.time.empty() && style.time == TimelineTime::line)
        {
            const float width =
                paint.body(fit_body(paint, entry.time, style.time_size, title_room * 0.45f), right,
                           baseline, style.time_size, quiet, gfx::Align::right);
            title_room -= width + 14.0f;
        }
        paint.label(fit_label(paint, entry.title, style.title_size, std::max(title_room, 30.0f)),
                    text_left, baseline, style.title_size, strong);
        float y = r.y + style.padding_y + title_line();
        if (style.body_lines > 0 && !entry.body.empty())
        {
            const std::vector<std::string> lines =
                wrap_body(paint, entry.body, style.body_size, body_room(), style.body_lines);
            lines_[static_cast<std::size_t>(i)] = std::max(static_cast<int>(lines.size()), 1);
            for (const std::string &line : lines)
            {
                paint.body(line, text_left, y + body_line() * 0.5f + style.body_size * 0.35f,
                           style.body_size, quiet);
                y += body_line();
            }
        }
        if (entry.card_height > 0.0f && card)
            card(canvas,
                 {text_left, r.y + r.h - style.padding_y - entry.card_height, right - text_left,
                  entry.card_height},
                 entry, i, focus);
        list.pop_opacity();
    }
    list.pop_clip();

    if (style.scroll_thumb && overflow)
    {
        const float track = in.h - 12.0f;
        const float size = std::max(track * in.h / content_, 30.0f);
        const float at = scroll / std::max(content_ - in.h, 1.0f);
        const float x = in.x + in.w + 3.0f;
        list.rounded_rect({x, in.y + 6.0f, 4.0f, track}, 2.0f, ink.muted.with_alpha(0.16f));
        list.rounded_rect({x, in.y + 6.0f + (track - size) * tween::clamp01(at), 4.0f, size}, 2.0f,
                          ink.muted.with_alpha(0.7f));
    }
}

} // namespace hui::ui
