// ps5-homebrew-ui - Component: Menu.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/menu.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

constexpr float kCheckColumn = 34.0f; // the room a check mark takes before the label
constexpr float kGlyphGap = 12.0f;
constexpr float kPi = 3.14159265f;

// amount > 0 lightens toward white, < 0 darkens toward black.
Color shade(Color c, float amount)
{
    const Color target =
        amount > 0.0f ? Color{1.0f, 1.0f, 1.0f, c.a} : Color{0.0f, 0.0f, 0.0f, c.a};
    return gfx::mix(c, target, std::fabs(amount));
}

// Design languages whose surfaces carry their own depth cue (a solid offset
// shadow, bevelled edges, paired light and dark shadows): a soft drop shadow
// under them would belong to another language.
bool own_depth(SurfaceStyle style)
{
    return style == SurfaceStyle::hard || style == SurfaceStyle::bevel ||
           style == SurfaceStyle::pixel || style == SurfaceStyle::neumorphic ||
           style == SurfaceStyle::sketch;
}

} // namespace

void Menu::set_items(std::vector<MenuItem> items)
{
    items_ = std::move(items);
    checks_.assign(items_.size(), tween::Spring{});
    for (std::size_t i = 0; i < items_.size(); ++i)
        checks_[i].snap(items_[i].checked ? 1.0f : 0.0f);
    focus_ = std::clamp(focus_, 0, std::max(static_cast<int>(items_.size()) - 1, 0));
    if (!items_.empty() && items_[static_cast<std::size_t>(focus_)].separator)
        focus_ = std::max(step(-1, 1), 0);
    retarget(true);
}

void Menu::set_focus(int index, bool snap)
{
    if (items_.empty())
        return;
    focus_ = std::clamp(index, 0, static_cast<int>(items_.size()) - 1);
    retarget(snap);
}

void Menu::open(const Rect &anchor, Feedback &feedback)
{
    anchor_ = anchor;
    open_ = true;
    age_ = 0.0f;
    focus_ = std::max(step(-1, 1), 0);
    retarget(true);
    danger_.snap(!items_.empty() && items_[static_cast<std::size_t>(focus_)].danger ? 1.0f : 0.0f);
    play_cue(feedback, style, style.sounds.open, anchor.cx());
}

void Menu::close(Feedback &feedback)
{
    if (!open_)
        return;
    open_ = false;
    play_cue(feedback, style, style.sounds.close, anchor_.cx());
}

float Menu::item_height(int index) const
{
    return items_[static_cast<std::size_t>(index)].separator ? style.separator_height
                                                             : style.row_height;
}

// Heights are a few additions, so they are derived on demand: a knob changed
// after set_items() needs no call to take effect.
float Menu::item_top(int index) const
{
    float y = 0.0f;
    for (int i = 0; i < index; ++i)
        y += item_height(i);
    return y;
}

float Menu::content_height() const
{
    return item_top(static_cast<int>(items_.size()));
}

// The next item the focus can rest on in a direction, or -1 at the end.
int Menu::step(int from, int direction) const
{
    const int count = static_cast<int>(items_.size());
    for (int i = from + direction; i >= 0 && i < count; i += direction)
    {
        if (!items_[static_cast<std::size_t>(i)].separator)
            return i;
    }
    return -1;
}

void Menu::retarget(bool snap)
{
    if (items_.empty())
        return;
    const Rect target{0.0f, item_top(focus_), style.width - 2.0f * style.padding,
                      item_height(focus_)};
    highlight_.target(target);
    if (snap)
        highlight_.snap(target);
}

// Where the panel goes: on the wanted side of the anchor, on the opposite one
// when it does not fit there, then moved along the anchor until it is inside
// the bounds. The pointer stays aimed at the anchor's centre as far as the
// panel's corners allow.
Menu::Placement Menu::place() const
{
    Placement out;
    const float w = style.width;
    const float h = content_height() + 2.0f * style.padding;
    const float reach = style.pointer_size + style.offset;
    const float right = bounds_.x + bounds_.w;
    const float bottom = bounds_.y + bounds_.h;
    const float corner = std::min(style.theme.radius_card, std::min(w, h) * 0.5f);
    const float keep = corner + style.pointer_size + 4.0f;

    MenuSide side = style.side == MenuSide::automatic ? MenuSide::below : style.side;
    const float below = bottom - (anchor_.y + anchor_.h) - reach;
    const float above = anchor_.y - bounds_.y - reach;
    const float after = right - (anchor_.x + anchor_.w) - reach;
    const float before = anchor_.x - bounds_.x - reach;
    if (side == MenuSide::below && below < h && above > below)
        side = MenuSide::above;
    else if (side == MenuSide::above && above < h && below > above)
        side = MenuSide::below;
    else if (side == MenuSide::right && after < w && before > after)
        side = MenuSide::left;
    else if (side == MenuSide::left && before < w && after > before)
        side = MenuSide::right;
    out.side = side;

    const float max_x = std::max(right - w, bounds_.x);
    const float max_y = std::max(bottom - h, bounds_.y);
    if (side == MenuSide::below || side == MenuSide::above)
    {
        out.panel = {
            std::clamp(anchor_.cx() - w * 0.5f, bounds_.x, max_x),
            side == MenuSide::below ? anchor_.y + anchor_.h + reach : anchor_.y - reach - h, w, h};
        out.panel.y = std::clamp(out.panel.y, bounds_.y, max_y);
        out.tip_x = std::clamp(anchor_.cx(), out.panel.x + std::min(keep, w * 0.5f),
                               out.panel.x + w - std::min(keep, w * 0.5f));
        out.tip_y = side == MenuSide::below ? out.panel.y - style.pointer_size
                                            : out.panel.y + h + style.pointer_size;
    }
    else
    {
        out.panel = {side == MenuSide::right ? anchor_.x + anchor_.w + reach
                                             : anchor_.x - reach - w,
                     std::clamp(anchor_.cy() - h * 0.5f, bounds_.y, max_y), w, h};
        out.panel.x = std::clamp(out.panel.x, bounds_.x, max_x);
        out.tip_y = std::clamp(anchor_.cy(), out.panel.y + std::min(keep, h * 0.5f),
                               out.panel.y + h - std::min(keep, h * 0.5f));
        out.tip_x = side == MenuSide::right ? out.panel.x - style.pointer_size
                                            : out.panel.x + w + style.pointer_size;
    }
    return out;
}

Rect Menu::panel_rect() const
{
    return place().panel;
}

MenuSide Menu::side() const
{
    return place().side;
}

Event Menu::handle(const InputFrame &input, Feedback &feedback)
{
    if (!open_ || items_.empty())
        return Event::none;
    const float x = place().panel.cx();
    if (input.nav == Direction::up || input.nav == Direction::down)
    {
        const int direction = input.nav == Direction::down ? 1 : -1;
        int next = step(focus_, direction);
        if (next < 0 && style.wrap && !input.nav_repeat)
            next = step(direction > 0 ? -1 : static_cast<int>(items_.size()), direction);
        if (next < 0 || next == focus_)
            return refuse(feedback, style, input, highlight_.refusal(), x);
        focus_ = next;
        retarget(false);
        const float along = items_.size() > 1
                                ? static_cast<float>(focus_) / static_cast<float>(items_.size() - 1)
                                : 0.0f;
        play_cue(feedback, style, style.sounds.move, x,
                 style.pitch_by_position ? tween::lerp(1.05f, 0.95f, along) : 1.0f);
        return Event::moved;
    }
    if (input.is_pressed(Action::confirm))
    {
        MenuItem &item = items_[static_cast<std::size_t>(focus_)];
        if (item.disabled || item.separator)
            return refuse(feedback, style, input, highlight_.refusal(), x);
        press_.trigger();
        if (item.checkable)
        {
            item.checked = !item.checked;
            play_cue(feedback, style, style.sounds.change, x, item.checked ? 1.05f : 0.95f);
            if (style.close_on_check)
                open_ = false;
            return Event::changed;
        }
        play_cue(feedback, style, style.sounds.activate, x);
        if (style.sounds.rumble > 0.0f)
            feedback.rumble(0.3f * style.sounds.rumble, 0.04f);
        if (style.close_on_activate)
            open_ = false;
        return Event::activated;
    }
    if (input.is_pressed(Action::back))
    {
        close(feedback);
        return Event::cancelled;
    }
    return Event::none;
}

void Menu::update(float dt)
{
    age_ += dt;
    // Opening springs past its size and settles; closing is quicker and does
    // not bounce: a thing that leaves should not draw the eye.
    amount_.target = open_ ? 1.0f : 0.0f;
    const float omega = std::max(style.omega(), 14.0f);
    if (open_)
        amount_.update(dt, omega,
                       style.reduced_motion ? 1.0f : std::clamp(style.damping(), 0.55f, 0.8f));
    else
        amount_.update(dt, omega * 1.5f, 1.0f);

    highlight_.update(dt, style);
    press_.update(dt, 10.0f);
    const bool on_danger = !items_.empty() && items_[static_cast<std::size_t>(focus_)].danger;
    danger_.target = on_danger ? 1.0f : 0.0f;
    danger_.update(dt, 18.0f);
    for (std::size_t i = 0; i < items_.size() && i < checks_.size(); ++i)
    {
        checks_[i].target = items_[i].checked ? 1.0f : 0.0f;
        checks_[i].update(dt, 20.0f);
    }
}

// The pointer is the panel's surface continued into a triangle: filled in the
// colour the panel has along that edge, sunk a little into the panel so it
// covers the border where they join, then edged with two strokes.
void Menu::draw_pointer(Canvas &canvas, const Placement &at) const
{
    const float size = style.pointer_size;
    if (size <= 0.5f)
        return;
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;

    Color fill = theme.surface;
    Color edge = theme.outline;
    if (theme.style == SurfaceStyle::gloss)
    {
        // A glossy panel is a gradient: match the end the pointer is on.
        if (at.side == MenuSide::below)
            fill = shade(theme.surface, 0.2f);
        else if (at.side == MenuSide::above)
            fill = shade(theme.surface, -0.16f);
        edge = shade(theme.surface, -0.5f);
    }
    else if (theme.style == SurfaceStyle::glass)
    {
        edge = theme.light;
    }
    else if (theme.style == SurfaceStyle::bevel)
    {
        // The pointer leaves through a lit or a shaded edge.
        edge =
            at.side == MenuSide::below || at.side == MenuSide::right ? theme.light : theme.shadow;
    }

    const float line = theme.style == SurfaceStyle::neumorphic ? 0.0f : theme.border;
    const float sink = line + 1.0f;
    const float height = size + sink;
    const float half = height; // sides at 45 degrees: the base widens as it sinks
    // (dx, dy) points from the tip into the panel.
    float dx = 0.0f;
    float dy = 1.0f;
    float angle = 0.0f;
    if (at.side == MenuSide::above)
    {
        dy = -1.0f;
        angle = kPi;
    }
    else if (at.side == MenuSide::right)
    {
        dx = 1.0f;
        dy = 0.0f;
        angle = -kPi * 0.5f;
    }
    else if (at.side == MenuSide::left)
    {
        dx = -1.0f;
        dy = 0.0f;
        angle = kPi * 0.5f;
    }
    const float cx = at.tip_x + dx * height * 0.5f;
    const float cy = at.tip_y + dy * height * 0.5f;
    const Rect box{cx - half, cy - height * 0.5f, 2.0f * half, height};
    if (theme.surface.a < 0.99f && style.backing > 0.0f)
        list.triangle(box, theme.page.with_alpha(tween::clamp01(style.backing)), 0.0f, angle);
    list.triangle(box, fill, 0.0f, angle);
    if (line > 0.0f)
    {
        // The base corners, on the panel's edge, to either side of the tip.
        const float base_x = at.tip_x + dx * size;
        const float base_y = at.tip_y + dy * size;
        list.line(base_x - dy * size, base_y + dx * size, at.tip_x, at.tip_y, line, edge);
        list.line(base_x + dy * size, base_y - dx * size, at.tip_x, at.tip_y, line, edge);
    }
}

void Menu::draw(Canvas &canvas) const
{
    if (!visible() || items_.empty())
        return;
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const Placement at = place();
    const Rect panel = at.panel;
    const float amount = amount_.value;
    const float alpha = tween::clamp01(amount * 1.8f);

    if (style.scrim > 0.0f)
        list.rounded_rect(
            {0.0f, 0.0f, gfx::kVirtualWidth, gfx::kVirtualHeight}, 0.0f,
            Color::rgb(0x000000, tween::clamp01(style.scrim) * tween::clamp01(amount)));

    // It grows out of the pointer's tip, so it reads as coming from the anchor.
    const float scale = style.reduced_motion ? 1.0f : tween::lerp(0.8f, 1.0f, amount);
    list.push_opacity(alpha);
    list.push_transform(scale, at.tip_x, at.tip_y, 0.0f, 0.0f);

    const float radius = std::min(theme.radius_card, std::min(panel.w, panel.h) * 0.5f);
    if (style.elevation > 0.0f && !own_depth(theme.style))
        list.shadow({panel.x, panel.y + 12.0f * style.elevation, panel.w, panel.h}, radius,
                    34.0f * style.elevation, Color::rgb(0x000000, theme.dark ? 0.45f : 0.22f));
    if (theme.surface.a < 0.99f && style.backing > 0.0f)
        list.rounded_rect(panel, radius, theme.page.with_alpha(tween::clamp01(style.backing)));
    paint.panel(panel);
    draw_pointer(canvas, at);

    const Rect in = panel.inset(style.padding);
    const bool checks = std::any_of(items_.begin(), items_.end(),
                                    [](const MenuItem &item) { return item.checkable; });

    // The highlight takes the danger colour while it rests on a destructive
    // item; alpha 0 leaves it the theme's own.
    HighlightStyle look = style.highlight;
    if (danger_.value > 0.01f)
    {
        const Color base = look.color.a > 0.0f
                               ? look.color
                               : (look.kind == HighlightKind::fill ? theme.primary : theme.focus);
        look.color = gfx::mix(base, theme.danger, danger_.value);
    }
    look.grow -= 2.0f * press_.value;
    list.push_transform(1.0f, 0.0f, 0.0f, in.x, in.y);
    highlight_.draw(canvas, style, look, 1.0f);
    list.pop_transform();
    const Color on_plate = Highlight::text_color(style, look, 1.0f);

    const int count = static_cast<int>(items_.size());
    int row_index = 0;
    for (int i = 0; i < count; ++i)
    {
        const MenuItem &item = items_[static_cast<std::size_t>(i)];
        const Rect row{in.x, in.y + item_top(i), in.w, item_height(i)};
        if (item.separator)
        {
            list.rounded_rect({row.x + 10.0f, row.cy() - 0.75f, row.w - 20.0f, 1.5f}, 0.0f,
                              theme.text_muted.with_alpha(0.26f));
            continue;
        }
        const float arrive = style.entrance_step <= 0.0f || style.reduced_motion
                                 ? 1.0f
                                 : tween::stagger(age_, row_index, style.entrance_step, 0.22f);
        ++row_index;
        list.push_opacity(arrive * (item.disabled ? 0.42f : 1.0f));
        const float focus = highlight_.coverage({0.0f, item_top(i), in.w, row.h});
        const bool filled = look.kind == HighlightKind::fill;
        const Color base = item.danger ? theme.danger : theme.text;
        const Color ink = filled ? gfx::mix(base, on_plate, focus) : base;
        const Color quiet =
            filled ? gfx::mix(theme.text_muted, on_plate, focus * 0.9f) : theme.text_muted;

        float left = row.x + style.row_padding;
        float right = row.x + row.w - style.row_padding;
        const float cy = row.cy();
        if (checks)
        {
            // The mark draws itself: the short stroke, then the long one.
            const float value = item.checkable ? checks_[static_cast<std::size_t>(i)].value : 0.0f;
            if (value > 0.01f)
            {
                const float s = 22.0f;
                const float x = left - 2.0f;
                const float y = cy - s * 0.5f;
                const float first = tween::clamp01(value * 2.5f);
                const float second = tween::clamp01((value - 0.4f) / 0.6f);
                list.line(x + s * 0.16f, y + s * 0.54f, x + s * (0.16f + 0.24f * first),
                          y + s * (0.54f + 0.24f * first), 3.0f, ink);
                if (second > 0.0f)
                    list.line(x + s * 0.40f, y + s * 0.78f, x + s * (0.40f + 0.46f * second),
                              y + s * (0.78f - 0.56f * second), 3.0f, ink);
            }
            left += kCheckColumn;
        }
        if (style.glyph_width > 0.0f)
        {
            if (glyph)
                glyph(canvas, {left, row.y, style.glyph_width, row.h}, item, i, focus, ink);
            left += style.glyph_width + kGlyphGap;
        }
        if (!item.shortcut.empty())
        {
            right -= paint.label(item.shortcut, right, cy + style.shortcut_size * 0.35f,
                                 style.shortcut_size, quiet, gfx::Align::right);
            right -= 16.0f;
        }
        paint.bounded_label(item.label, left, cy + style.text_size * 0.35f, style.text_size, ink, std::max(right - left, 40.0f));
        list.pop_opacity();
    }

    list.pop_transform();
    list.pop_opacity();
}

} // namespace hui::ui
