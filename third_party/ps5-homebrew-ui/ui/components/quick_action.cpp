// ps5-homebrew-ui - Components: QuickAction and QuickActionBar.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/quick_action.hpp"

#include <algorithm>
#include <cmath>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

bool at_left(QuickAnchor anchor)
{
    return anchor == QuickAnchor::top_left || anchor == QuickAnchor::bottom_left;
}

bool at_right(QuickAnchor anchor)
{
    return anchor == QuickAnchor::top_right || anchor == QuickAnchor::bottom_right;
}

bool at_top(QuickAnchor anchor)
{
    return anchor == QuickAnchor::top_left || anchor == QuickAnchor::top_center ||
           anchor == QuickAnchor::top_right;
}

// A pill needs a language that can draw a circle; the others keep their own
// corners (a bevel, a notch, a pen stroke).
float pill_radius(const Theme &theme, bool pill, float height)
{
    if (!pill || theme.corner != Corner::round || theme.radius < 2.0f ||
        theme.style == SurfaceStyle::bevel || theme.style == SurfaceStyle::sketch)
        return -1.0f;
    return height * 0.5f;
}

} // namespace

// ---- QuickAction -----------------------------------------------------------

void QuickAction::set_visible(bool visible, bool snap)
{
    visible_ = visible;
    shown_.target = visible ? 1.0f : 0.0f;
    if (snap)
        shown_.snap(shown_.target);
}

void QuickAction::set_badge(int count)
{
    badge_.set_count(count);
}

void QuickAction::expand()
{
    age_ = 0.0f;
    collapse_.target = 0.0f;
}

float QuickAction::width(const Fonts &fonts) const
{
    gfx::DrawList scratch;
    const Painter paint(scratch, fonts, style.theme, 0);
    const float glyph_w = button_width(glyph, style.glyph_size);
    const float folded = std::max(style.height, glyph_w + 2.0f * style.padding);
    if (label.empty())
        return folded;
    const float open = style.padding + glyph_w + style.gap +
                       paint.label_width(label, style.text_size) + style.text_padding;
    return tween::lerp(std::max(open, folded), folded, tween::clamp01(collapse_.value));
}

Rect QuickAction::rect(const Fonts &fonts) const
{
    const float w = width(fonts);
    const float h = style.height;
    float x = bounds_.cx() - w * 0.5f;
    if (at_left(style.anchor))
        x = bounds_.x + style.margin;
    else if (at_right(style.anchor))
        x = bounds_.x + bounds_.w - style.margin - w;
    const float y =
        at_top(style.anchor) ? bounds_.y + style.margin : bounds_.y + bounds_.h - style.margin - h;
    return {x, y, w, h};
}

Event QuickAction::handle(const InputFrame &input, Feedback &feedback)
{
    if (!visible_ || !input.is_pressed(action))
        return Event::none;
    // Its place is only known with fonts; the corner is close enough to pan by.
    const float x = at_left(style.anchor)
                        ? bounds_.x
                        : (at_right(style.anchor) ? bounds_.x + bounds_.w : bounds_.cx());
    if (!enabled_)
        return refuse(feedback, style, input, refusal_, x);
    press_.trigger();
    if (style.expand_on_press)
        expand();
    play_cue(feedback, style, style.sounds.activate, x);
    if (style.sounds.rumble > 0.0f && style.rumble > 0.0f)
        feedback.rumble(style.rumble * style.sounds.rumble, 0.04f);
    return Event::activated;
}

void QuickAction::update(float dt)
{
    age_ += dt;
    const float omega = std::max(style.omega(), 14.0f);
    collapse_.target = style.collapse_after > 0.0f && age_ >= style.collapse_after ? 1.0f : 0.0f;
    collapse_.update(dt, omega);
    shown_.update(dt, omega);
    press_.update(dt, 7.0f);
    refusal_.update(dt, 9.0f);

    BadgeStyle &badge = badge_.style;
    badge.theme = style.theme;
    badge.reduced_motion = style.reduced_motion;
    badge.kind = style.badge_kind;
    badge.height = style.badge_height;
    badge.text_size = style.badge_text;
    badge.max_count = style.badge_max;
    badge.padding = 7.0f;
    badge_.update(dt);
}

void QuickAction::draw(Canvas &canvas) const
{
    const float shown = tween::clamp01(shown_.value);
    if (shown <= 0.01f)
        return;
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    Rect r = rect(canvas.fonts);
    r.x += shake(refusal_.value, canvas.time, 8.0f);
    const float press = tween::clamp01(press_.value);
    const float fold = tween::clamp01(collapse_.value);
    const bool still = style.reduced_motion;
    const float scale = still ? 1.0f : 1.0f - style.press_scale * press;
    // Hidden, it leaves through the edge it sits on.
    const float away = still ? 0.0f : (at_top(style.anchor) ? -14.0f : 14.0f) * (1.0f - shown);
    const float radius = pill_radius(theme, style.pill, r.h);

    list.push_opacity(shown);
    list.push_transform(scale, r.cx(), r.cy(), 0.0f, away);
    if (press > 0.01f && style.ripple > 0.0f && !still)
    {
        // A ring of light leaves the pill: the press was heard.
        const Color light{theme.focus.r, theme.focus.g, theme.focus.b, 1.0f};
        paint.halo(r, radius < 0.0f ? paint.control_radius(r) : radius,
                   4.0f + style.ripple * (1.0f - press), light.with_alpha(0.6f * press));
    }
    Look look;
    look.press = press;
    look.disabled = !enabled_;
    const ButtonFace face = draw_button_face(canvas, theme, r, style.role, look, radius);
    const Rect &content = face.content;

    list.push_opacity(enabled_ ? 1.0f : 0.4f);
    const float glyph_w = button_width(glyph, style.glyph_size);
    const float gx = content.x + tween::lerp(style.padding, (content.w - glyph_w) * 0.5f,
                                             label.empty() ? 1.0f : fold);
    draw_button(list, canvas.fonts, glyph_style_on(face, style.glyph_tinted), glyph, gx,
                content.cy(), style.glyph_size);
    const float words = (1.0f - fold) * (1.0f - fold);
    if (!label.empty() && words > 0.01f)
    {
        const float text_x = gx + glyph_w + style.gap;
        const float room =
            std::max(content.x + content.w - style.text_padding * 0.5f - text_x, 0.0f);
        paint.bounded_label(label, text_x, content.cy() + style.text_size * 0.35f, style.text_size, face.ink.with_alpha(words), room);
    }
    list.pop_opacity();

    // The member keeps the count and its bounce; the copy takes today's place.
    Badge count = badge_;
    count.set_bounds(
        {r.x + r.w - 48.0f, r.y - style.badge_height * 0.4f, 60.0f, style.badge_height});
    count.draw(canvas);
    list.pop_transform();
    list.pop_opacity();
}

// ---- QuickActionBar --------------------------------------------------------

void QuickActionBar::set_actions(const std::vector<QuickActionItem> &items)
{
    actions_.resize(items.size());
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        actions_[i].label = items[i].label;
        actions_[i].glyph = items[i].glyph;
        actions_[i].action = items[i].action;
    }
    apply_style();
}

void QuickActionBar::set_visible(bool visible, bool snap)
{
    for (QuickAction &action : actions_)
        action.set_visible(visible, snap);
}

// Every action is a QuickAction with the bar's style and bounds; the bar only
// moves each one from the corner to its place in the row or the stack.
void QuickActionBar::apply_style()
{
    for (QuickAction &action : actions_)
    {
        action.style = static_cast<const QuickActionStyle &>(style);
        action.set_bounds(bounds_);
    }
}

Rect QuickActionBar::rect(const Fonts &fonts, int index) const
{
    const int n = count();
    if (index < 0 || index >= n)
        return {};
    Rect r = at(index).rect(fonts);
    if (style.layout == GroupLayout::column)
    {
        const float step = style.height + style.spacing;
        r.y += at_top(style.anchor) ? step * static_cast<float>(index)
                                    : -step * static_cast<float>(n - 1 - index);
        return r;
    }
    float total = 0.0f;
    float before = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        if (i == index)
            before = total;
        total += at(i).width(fonts) + (i + 1 < n ? style.spacing : 0.0f);
    }
    float start = bounds_.cx() - total * 0.5f;
    if (at_left(style.anchor))
        start = bounds_.x + style.margin;
    else if (at_right(style.anchor))
        start = bounds_.x + bounds_.w - style.margin - total;
    r.x = start + before;
    return r;
}

Event QuickActionBar::handle(const InputFrame &input, Feedback &feedback)
{
    for (int i = 0; i < count(); ++i)
    {
        const Event event = at(i).handle(input, feedback);
        if (event != Event::none)
        {
            fired_ = i;
            return event;
        }
    }
    return Event::none;
}

void QuickActionBar::update(float dt)
{
    apply_style();
    for (QuickAction &action : actions_)
        action.update(dt);
}

void QuickActionBar::draw(Canvas &canvas) const
{
    for (int i = 0; i < count(); ++i)
    {
        const Rect alone = at(i).rect(canvas.fonts);
        const Rect placed = rect(canvas.fonts, i);
        canvas.list.push_transform(1.0f, 0.0f, 0.0f, placed.x - alone.x, placed.y - alone.y);
        at(i).draw(canvas);
        canvas.list.pop_transform();
    }
}

} // namespace hui::ui
