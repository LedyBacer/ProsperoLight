// ps5-homebrew-ui - Component: SideNav.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/sidenav.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

// Room between the scrolling entries and the pinned one, beyond the row gap.
constexpr float kFooterGap = 14.0f;

} // namespace

void SideNav::set_entries(std::vector<NavEntry> entries)
{
    entries_ = std::move(entries);
    const int last = std::max(static_cast<int>(entries_.size()) - 1, 0);
    focus_ = std::clamp(focus_, 0, last);
    current_ = std::clamp(current_, 0, last);
    retarget(true);
}

void SideNav::set_bounds(const Rect &bounds)
{
    bounds_ = bounds;
    retarget(true);
}

void SideNav::set_focus(int index, bool snap)
{
    if (entries_.empty())
        return;
    focus_ = std::clamp(index, 0, static_cast<int>(entries_.size()) - 1);
    retarget(snap);
}

void SideNav::set_current(int index, bool snap)
{
    if (entries_.empty())
        return;
    current_ = std::clamp(index, 0, static_cast<int>(entries_.size()) - 1);
    if (!focused_)
        focus_ = current_;
    retarget(snap);
}

void SideNav::set_focused(bool focused)
{
    if (focused_ == focused)
        return;
    focused_ = focused;
    // A rail nobody is steering shows where the player is, not where the
    // cursor was left.
    if (!focused_ && !entries_.empty())
    {
        focus_ = current_;
        retarget(false);
    }
}

Event SideNav::toggle(Feedback &feedback)
{
    style.expanded = !style.expanded;
    play_cue(feedback, style, style.sounds.change, bounds_.x + target_width() * 0.5f,
             style.expanded ? 1.04f : 0.96f);
    return Event::changed;
}

void SideNav::enter()
{
    age_ = 0.0f;
}

float SideNav::target_width() const
{
    return style.expanded ? style.expanded_width : style.collapsed_width;
}

// Before the first update() the spring holds nothing yet: the rail is then
// simply where it is going.
float SideNav::width() const
{
    return sized_ ? std::max(width_.value, 0.0f) : target_width();
}

Rect SideNav::rect() const
{
    return {bounds_.x, bounds_.y, width(), bounds_.h};
}

float SideNav::expansion() const
{
    return tween::inverse_lerp(style.collapsed_width, style.expanded_width, width());
}

bool SideNav::pinned(int index) const
{
    return style.footer && entries_.size() > 1 && index == static_cast<int>(entries_.size()) - 1;
}

// The room a section start takes above its first row.
float SideNav::lead(int index) const
{
    const NavEntry &entry = entries_[static_cast<std::size_t>(index)];
    if (pinned(index))
        return 0.0f;
    if (!entry.section.empty())
        return style.section_height;
    return entry.separator && index > 0 ? style.section_height * 0.5f : 0.0f;
}

// The layout is a few additions, so it is derived on demand: a knob changed
// after set_entries() needs no call to take effect.
float SideNav::row_top(int index) const
{
    float y = 0.0f;
    for (int i = 0; i <= index; ++i)
    {
        y += lead(i);
        if (i < index)
            y += style.row_height + style.gap;
    }
    return y;
}

float SideNav::content_height() const
{
    const int count = static_cast<int>(entries_.size());
    const int last = pinned(count - 1) ? count - 2 : count - 1;
    return last < 0 ? 0.0f : row_top(last) + style.row_height;
}

float SideNav::view_height() const
{
    float height = bounds_.h - 2.0f * style.padding;
    if (style.footer && entries_.size() > 1)
        height -= style.row_height + style.gap + kFooterGap;
    return std::max(height, 0.0f);
}

Rect SideNav::row_rect(int index) const
{
    const float inner = width() - 2.0f * style.padding;
    const float top = bounds_.y + style.padding;
    if (pinned(index))
        return {bounds_.x + style.padding, top + view_height() + style.gap + kFooterGap, inner,
                style.row_height};
    return {bounds_.x + style.padding, top + row_top(index) - scroll_.offset(), inner,
            style.row_height};
}

// The highlight lives in content space so scrolling does not make it lag; the
// pinned entry does not scroll, so its place there moves with the scroll.
Rect SideNav::highlight_target() const
{
    const float inner = width() - 2.0f * style.padding;
    const float top = pinned(focus_) ? view_height() + style.gap + kFooterGap + scroll_.offset()
                                     : row_top(focus_);
    return {0.0f, top, inner, style.row_height};
}

void SideNav::retarget(bool snap)
{
    if (entries_.empty())
        return;
    if (!pinned(focus_))
    {
        const float top = row_top(focus_);
        scroll_.reveal(top - lead(focus_), top + style.row_height, view_height(),
                       style.row_height * 0.4f, content_height());
        if (snap)
            scroll_.position.snap(scroll_.position.target);
    }
    const Rect target = highlight_target();
    highlight_.target(target);
    marker_.target = pinned(current_) ? view_height() + style.gap + kFooterGap + scroll_.offset()
                                      : row_top(current_);
    if (snap)
    {
        highlight_.snap(target);
        marker_.snap(marker_.target);
    }
}

Event SideNav::handle(const InputFrame &input, Feedback &feedback)
{
    if (entries_.empty())
        return Event::none;
    const int count = static_cast<int>(entries_.size());
    const float x = bounds_.x + width() * 0.5f;
    if (input.nav == Direction::up || input.nav == Direction::down)
    {
        int next = focus_ + (input.nav == Direction::down ? 1 : -1);
        if ((next < 0 || next >= count) && style.wrap && !input.nav_repeat)
            next = (next + count) % count;
        if (next < 0 || next >= count || next == focus_)
            return refuse(feedback, style, input, highlight_.refusal(), x);
        focus_ = next;
        retarget(false);
        const float along =
            count > 1 ? static_cast<float>(focus_) / static_cast<float>(count - 1) : 0.0f;
        play_cue(feedback, style, style.sounds.move, x,
                 style.pitch_by_position ? tween::lerp(1.05f, 0.95f, along) : 1.0f);
        return Event::moved;
    }
    if (input.is_pressed(Action::confirm))
    {
        const NavEntry &entry = entries_[static_cast<std::size_t>(focus_)];
        if (entry.disabled)
            return refuse(feedback, style, input, highlight_.refusal(), x);
        if (!entry.action)
            current_ = focus_;
        retarget(false);
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

void SideNav::update(float dt)
{
    age_ += dt;
    width_.target = target_width();
    if (!sized_)
    {
        width_.snap(width_.target);
        sized_ = true;
    }
    width_.update(dt, std::max(style.omega(), 12.0f));

    if (!entries_.empty())
    {
        // While the rail changes width the highlight must be exactly as wide
        // as a row: a spring chasing a spring would poke out of the panel. It
        // keeps its own motion only while it is travelling between entries.
        const Rect target = highlight_target();
        const Rect now = highlight_.rect(0.0f);
        if (!width_.settled() && std::fabs(now.y - target.y) < 0.5f)
            highlight_.snap(target);
        highlight_.target(target);
        marker_.target = pinned(current_)
                             ? view_height() + style.gap + kFooterGap + scroll_.offset()
                             : row_top(current_);
    }
    highlight_.update(dt, style);
    marker_.update(dt, std::max(style.omega(), 18.0f));
    scroll_.update(dt, std::max(style.omega(), 14.0f));
    focus_amount_.target = focused_ ? 1.0f : 0.0f;
    focus_amount_.update(dt, 18.0f);
    press_.update(dt, 10.0f);
}

void SideNav::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const Rect rail = rect();
    if (style.panel)
        paint.panel(rail);
    if (entries_.empty())
        return;

    const int count = static_cast<int>(entries_.size());
    const float expand = tween::smoothstep(expansion());
    const float inner_x = rail.x + style.padding;
    const float inner_w = rail.w - 2.0f * style.padding;
    const float top = rail.y + style.padding;
    const float scroll = scroll_.offset();
    const float view = view_height();
    const bool overflow = content_height() > view + 0.5f;
    const Rect window{rail.x, top, rail.w, view};
    const Color accent = theme.style == SurfaceStyle::sketch ? theme.on_primary : theme.primary;
    const Color hair = theme.text_muted.with_alpha(0.24f);
    // Icons keep their place when the rail opens: they are centred in the
    // collapsed width, and the labels start beside them.
    const float icon_cx = inner_x + (style.collapsed_width - 2.0f * style.padding) * 0.5f;
    const float label_x = icon_cx + style.icon_size * 0.5f + style.label_gap;

    const auto entrance = [&](int index)
    {
        if (style.entrance_step <= 0.0f || style.reduced_motion)
            return tween::cubic_out(age_ / 0.2f);
        return tween::stagger(age_, index, style.entrance_step, 0.32f);
    };
    // How visible a scrolling row is: 1 inside the window, fading as it is cut.
    const auto visibility = [&](int index, const Rect &row)
    {
        if (!overflow || pinned(index))
            return 1.0f;
        const float shown = std::min(row.y + row.h - window.y, window.y + window.h - row.y);
        return tween::clamp01(shown / (row.h * 0.8f));
    };

    // ---- section starts and the line above the pinned entry ----
    for (int i = 0; i < count; ++i)
    {
        const NavEntry &entry = entries_[static_cast<std::size_t>(i)];
        const float room = lead(i);
        if (room <= 0.0f)
            continue;
        const Rect row = row_rect(i);
        const float alpha = visibility(i, {row.x, row.y - room, row.w, row.h}) * entrance(i);
        if (alpha <= 0.0f)
            continue;
        list.push_opacity(alpha);
        const float title = entry.section.empty() ? 0.0f : expand;
        // Collapsed, a titled section is only its line; expanded, a title
        // at the start of the rail needs no line above it.
        const float line_y = row.y - room + (entry.section.empty() ? room * 0.5f : 8.0f);
        if (i > 0)
            list.rounded_rect({inner_x + 6.0f, line_y - 0.75f, inner_w - 12.0f, 1.5f}, 0.0f, hair);
        if (title > 0.01f)
            paint.bounded_label(upper(entry.section), inner_x + 12.0f, row.y - 12.0f, style.section_size, theme.text_muted.with_alpha(title), style.expanded_width - 2.0f * style.padding - 24.0f);
        list.pop_opacity();
    }
    if (pinned(count - 1))
    {
        const Rect row = row_rect(count - 1);
        list.rounded_rect({inner_x + 6.0f, row.y - (style.gap + kFooterGap) * 0.5f - 0.75f,
                           inner_w - 12.0f, 1.5f},
                          0.0f, hair.with_alpha(entrance(count - 1)));
    }

    // ---- where the player is: a quiet plate, and a bar on the rail's edge ----
    // Both follow the current entry on a spring of their own. They are not
    // the focus: that is the highlight, and it shows only while the rail is
    // being steered.
    if (!entries_[static_cast<std::size_t>(current_)].action)
    {
        const Rect plate{inner_x, top + marker_.value - scroll, inner_w, style.row_height};
        const bool inside = pinned(current_) || !overflow ||
                            (plate.y >= window.y && plate.y + plate.h <= window.y + window.h);
        const float alpha = inside ? entrance(current_) : 0.0f;
        if (style.current_plate)
            paint.fill(plate,
                       style.highlight.radius >= 0.0f
                           ? std::min(style.highlight.radius, plate.h * 0.5f)
                           : paint.control_radius(plate),
                       theme.text.with_alpha(0.09f * alpha));
        if (style.current_marker)
        {
            const float height = style.row_height * 0.5f;
            paint.fill({rail.x + std::max(theme.border, 1.0f) + 2.0f,
                        plate.y + (plate.h - height) * 0.5f, style.marker_thickness, height},
                       theme.corner == Corner::round ? style.marker_thickness * 0.5f : 0.0f,
                       accent.with_alpha(alpha));
        }
    }

    // ---- the gliding highlight ----
    {
        const bool clip = overflow && !pinned(focus_);
        if (clip)
            list.push_clip(window);
        list.push_transform(1.0f, 0.0f, 0.0f, inner_x, top - scroll);
        HighlightStyle look = style.highlight;
        look.grow -= 2.0f * press_.value; // a press pushes it in for a moment
        highlight_.draw(canvas, style, look, focus_amount_.value * entrance(focus_));
        list.pop_transform();
        if (clip)
            list.pop_clip();
    }

    // ---- icons, then labels: the labels are clipped to the rail as it opens ----
    struct Shown
    {
        Rect row;
        float alpha;
        float focus;
        Color ink;
    };
    const auto shown_of = [&](int index)
    {
        const NavEntry &entry = entries_[static_cast<std::size_t>(index)];
        Shown out{};
        out.row = row_rect(index);
        const float in_amount = entrance(index);
        out.alpha = visibility(index, out.row) * in_amount * (entry.disabled ? 0.42f : 1.0f);
        if (!style.reduced_motion)
            out.row.x -= 14.0f * (1.0f - in_amount);
        const float content_top =
            pinned(index) ? view + style.gap + kFooterGap + scroll : row_top(index);
        out.focus = highlight_.coverage({0.0f, content_top, inner_w, style.row_height});
        // A faded plate (the rail lost the focus) is too faint to carry the
        // colour of text on a full one.
        const Color strong =
            Highlight::text_color(style, style.highlight, out.focus * focus_amount_.value);
        const float weight = std::max(out.focus * focus_amount_.value,
                                      index == current_ && !entry.action ? 1.0f : 0.0f);
        out.ink = gfx::mix(theme.text_muted, strong, weight);
        return out;
    };

    if (overflow)
        list.push_clip(window);
    for (int i = 0; i < count; ++i)
    {
        if (i == count - 1 && pinned(i) && overflow)
            list.pop_clip();
        const NavEntry &entry = entries_[static_cast<std::size_t>(i)];
        const Shown at = shown_of(i);
        if (at.alpha <= 0.0f)
            continue;
        list.push_opacity(at.alpha);
        const float shift = at.row.x - inner_x;
        const Rect box{icon_cx + shift - style.icon_size * 0.5f,
                       at.row.cy() - style.icon_size * 0.5f, style.icon_size, style.icon_size};
        if (icon)
        {
            icon(canvas, box, entry, i, at.focus, at.ink);
        }
        else if (!entry.label.empty())
        {
            // No icon given: the entry's initial in a ring stands in for one.
            list.ring(box.cx(), box.cy(), box.w * 0.5f, 2.0f, at.ink.with_alpha(0.7f));
            const float size = box.w * 0.52f;
            paint.label(upper(entry.label.substr(0, 1)), box.cx(), box.cy() + size * 0.35f, size,
                        at.ink, gfx::Align::center);
        }
        if (!entry.badge.empty() && expand < 0.99f)
            list.circle(box.x + box.w - 1.0f, box.y + 2.0f, 5.5f,
                        theme.accent.with_alpha(1.0f - expand));
        list.pop_opacity();
    }
    if (overflow && !pinned(count - 1))
        list.pop_clip();

    if (expand <= 0.01f)
        return;
    const float text_right = rail.x + rail.w - style.padding;
    list.push_clip({rail.x, overflow ? window.y : rail.y, std::max(text_right - rail.x, 0.0f),
                    overflow ? window.h : rail.h});
    for (int i = 0; i < count; ++i)
    {
        if (i == count - 1 && pinned(i) && overflow)
        {
            // The pinned entry sits below the scrolling window.
            list.pop_clip();
            list.push_clip({rail.x, rail.y, std::max(text_right - rail.x, 0.0f), rail.h});
        }
        const NavEntry &entry = entries_[static_cast<std::size_t>(i)];
        const Shown at = shown_of(i);
        if (at.alpha <= 0.0f)
            continue;
        list.push_opacity(at.alpha * expand);
        const float shift = at.row.x - inner_x;
        // Measured against the expanded width, so a label is cut the same
        // way while the rail is still opening.
        const float full_right = rail.x + style.expanded_width - style.padding - 14.0f;
        float room = full_right - label_x;
        if (!entry.badge.empty())
        {
            const float height = style.badge_size + 10.0f;
            const float width =
                std::max(paint.label_width(entry.badge, style.badge_size) + 16.0f, height);
            const Rect pill{full_right - width + shift, at.row.cy() - height * 0.5f, width, height};
            // On a filled highlight the badge takes the text's colour, so it
            // does not vanish into a plate of its own colour.
            const Color body = style.highlight.kind == HighlightKind::fill
                                   ? gfx::mix(theme.accent, at.ink, at.focus)
                                   : theme.accent;
            paint.fill(pill,
                       theme.pill_chips ? height * 0.5f : std::min(theme.radius, height * 0.5f),
                       body);
            paint.label(entry.badge, pill.cx(), pill.cy() + style.badge_size * 0.35f,
                        style.badge_size, Painter::on(body), gfx::Align::center);
            room -= width + 12.0f;
        }
        paint.bounded_label(entry.label, label_x + shift, at.row.cy() + style.text_size * 0.35f, style.text_size, at.ink, std::max(room, 40.0f));
        list.pop_opacity();
    }
    list.pop_clip();
}

} // namespace hui::ui
