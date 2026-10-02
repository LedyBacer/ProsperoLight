// ps5-homebrew-ui - Component: PauseMenu.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/pause_menu.hpp"

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

constexpr float kHeaderRow = 52.0f; // a section title inside the menu
constexpr float kRule = 22.0f;      // the room the hairline under the header takes

} // namespace

void PauseMenu::set_items(std::vector<ListItem> items)
{
    applied_row_ = style.row_height;
    applied_gap_ = style.row_gap;
    list_.style.row_height = style.row_height;
    list_.style.gap = style.row_gap;
    list_.style.header_height = kHeaderRow;
    list_.set_items(std::move(items));
    sync();
}

void PauseMenu::set_bounds(const Rect &bounds)
{
    bounds_ = bounds;
    sync();
}

float PauseMenu::head_height() const
{
    float height = style.padding;
    if (!kicker.empty())
        height += style.kicker_size + 14.0f;
    if (!title.empty())
        height += style.title_size + 8.0f;
    if (!subtitle.empty())
        height += style.subtitle_size + 10.0f;
    return height + kRule;
}

float PauseMenu::rows_height() const
{
    float height = 0.0f;
    for (const ListItem &item : list_.items())
        height += (item.header ? kHeaderRow : style.row_height) + style.row_gap;
    return std::max(height - style.row_gap, 0.0f);
}

Rect PauseMenu::panel_rect() const
{
    const float width = std::min(style.width, bounds_.w - 48.0f);
    const float wanted = head_height() + rows_height() + style.padding;
    const float height = std::min(wanted, bounds_.h - 80.0f);
    const float y = bounds_.cy() - height * 0.5f;
    float x = bounds_.x + style.margin;
    if (style.layout == PauseLayout::right)
    {
        x = bounds_.x + bounds_.w - style.margin - width;
    }
    else if (style.layout == PauseLayout::center)
    {
        const float pair = width + (side ? style.side_gap + style.side_width : 0.0f);
        x = bounds_.cx() - std::min(pair, bounds_.w - 48.0f) * 0.5f;
    }
    return {x, y, width, height};
}

Rect PauseMenu::side_rect() const
{
    if (!side)
        return {};
    const Rect menu = panel_rect();
    const float height =
        style.side_height > 0.0f ? std::min(style.side_height, bounds_.h - 80.0f) : menu.h;
    const float y = bounds_.cy() - height * 0.5f;
    if (style.layout == PauseLayout::right)
    {
        const float x = bounds_.x + style.margin;
        return {x, y, std::max(std::min(style.side_width, menu.x - style.side_gap - x), 0.0f),
                height};
    }
    if (style.layout == PauseLayout::center)
    {
        const float x = menu.x + menu.w + style.side_gap;
        return {x, y, std::max(std::min(style.side_width, bounds_.x + bounds_.w - 24.0f - x), 0.0f),
                height};
    }
    const float edge = bounds_.x + bounds_.w - style.margin;
    const float x = std::max(edge - style.side_width, menu.x + menu.w + style.side_gap);
    return {x, y, std::max(edge - x, 0.0f), height};
}

// Brings the inner list up to date with this style. The list keeps its own
// focus and scroll, so only what changed is touched: set_bounds() and
// set_items() would otherwise snap its highlight every frame.
void PauseMenu::sync()
{
    ListStyle &rows = list_.style;
    static_cast<ComponentStyle &>(rows) = style;
    // The rows sit on this menu's panel, not on the page: give the list the
    // panel's text colours (it has no "on a surface" switch of its own).
    rows.theme.page_text = style.theme.text;
    rows.theme.page_text_muted = style.theme.text_muted;
    rows.row_height = style.row_height;
    rows.gap = style.row_gap;
    rows.header_height = kHeaderRow;
    rows.title_size = style.item_size;
    rows.subtitle_size = style.item_size * 0.72f;
    rows.value_size = style.item_size * 0.82f;
    rows.padding = style.padding * 0.5f;
    rows.highlight = style.highlight;
    rows.dividers = style.dividers;
    rows.wrap = style.wrap;
    rows.entrance_step = style.entrance_step;
    rows.panel = false;
    rows.cards = false;
    if (applied_row_ != style.row_height || applied_gap_ != style.row_gap)
    {
        applied_row_ = style.row_height;
        applied_gap_ = style.row_gap;
        list_.set_items(std::vector<ListItem>(list_.items()));
    }

    const Rect panel = panel_rect();
    const float top = panel.y + head_height();
    const Rect area{panel.x + style.padding * 0.5f, top, panel.w - style.padding,
                    std::max(panel.y + panel.h - style.padding - top, style.row_height)};
    const Rect &now = list_.bounds();
    if (now.x != area.x || now.y != area.y || now.w != area.w || now.h != area.h)
        list_.set_bounds(area);
}

void PauseMenu::open(Feedback &feedback)
{
    sync();
    if (!visible())
    {
        fade_.snap(0.0f);
        pop_.snap(0.0f);
    }
    open_ = true;
    age_ = 0.0f;
    int first = 0;
    const std::vector<ListItem> &rows = list_.items();
    while (first + 1 < static_cast<int>(rows.size()) &&
           rows[static_cast<std::size_t>(first)].header)
        ++first;
    list_.set_focus(first);
    list_.enter();
    play_cue(feedback, style, style.sounds.open, panel_rect().cx());
}

void PauseMenu::close(Feedback &feedback)
{
    if (!open_)
        return;
    open_ = false;
    play_cue(feedback, style, style.sounds.close, panel_rect().cx());
}

void PauseMenu::dismiss()
{
    open_ = false;
    fade_.snap(0.0f);
    pop_.snap(0.0f);
}

bool PauseMenu::visible() const
{
    return open_ || fade_.value > 0.004f;
}

Event PauseMenu::handle(const InputFrame &input, Feedback &feedback)
{
    if (!open_)
        return Event::none;
    sync();
    if (input.is_pressed(Action::back))
    {
        // Back means "resume": one step up from a pause menu is the game.
        if (style.close_on_back)
            close(feedback);
        else
            play_cue(feedback, style, style.sounds.cancel, panel_rect().cx());
        return Event::cancelled;
    }
    return list_.handle(input, feedback);
}

void PauseMenu::update(float dt)
{
    age_ += dt;
    sync();
    list_.set_active(open_);
    list_.update(dt);
    const float omega = std::max(style.omega(), 10.0f);
    if (open_)
    {
        fade_.target = 1.0f;
        fade_.update(dt, omega * 1.2f);
        pop_.target = 1.0f;
        pop_.update(dt, omega, std::max(style.damping(), 0.7f));
    }
    else
    {
        const float out = omega * std::max(style.exit_speed, 0.1f);
        fade_.target = 0.0f;
        fade_.update(dt, out);
        pop_.target = 0.0f;
        pop_.update(dt, out, 1.0f);
    }
}

void PauseMenu::draw(Canvas &canvas) const
{
    if (!visible())
        return;
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const bool calm = style.reduced_motion;
    const float fade = tween::clamp01(fade_.value);

    // The game behind goes out of focus, then dark.
    if (style.backdrop_blur && canvas.glass != 0)
        list.glass(canvas.glass, bounds_, 0.0f, Color{1.0f, 1.0f, 1.0f, fade});
    list.rounded_rect(bounds_, 0.0f, style.scrim_color.with_alpha(style.scrim * fade));

    const Rect panel = panel_rect();
    const float away = calm ? 0.0f : style.travel * (1.0f - pop_.value);
    const float side_sign = style.layout == PauseLayout::right ? 1.0f : -1.0f;
    const bool centred = style.layout == PauseLayout::center;
    const float scale = centred && !calm ? tween::lerp(0.94f, 1.0f, pop_.value) : 1.0f;

    list.push_opacity(tween::clamp01(fade * 1.5f));
    list.push_transform(scale, panel.cx(), panel.cy(), centred ? 0.0f : side_sign * away,
                        centred ? away * 0.3f : 0.0f);
    draw_overlay_panel(canvas, theme, panel, style.frosted, style.frost);

    const float x = panel.x + style.padding;
    const float room = panel.w - 2.0f * style.padding;
    float y = panel.y + style.padding;
    int piece = 0;
    // The header assembles top to bottom, a beat ahead of the rows.
    const auto arrive = [&]()
    {
        const float in = calm ? tween::cubic_out(age_ / 0.2f)
                              : tween::stagger(age_, piece, style.entrance_step, 0.32f);
        ++piece;
        list.push_opacity(open_ ? in : 1.0f);
        list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, calm || !open_ ? 0.0f : 14.0f * (1.0f - in));
    };
    const auto leave = [&]()
    {
        list.pop_transform();
        list.pop_opacity();
    };
    if (!kicker.empty())
    {
        arrive();
        const float mark = style.kicker_size * 0.8f;
        paint.fill({x, y + style.kicker_size * 0.1f, 4.0f, mark},
                   theme.corner == Corner::round ? 2.0f : 0.0f, theme.accent);
        paint.label(fit_label(paint, upper(kicker), style.kicker_size, room - 14.0f), x + 14.0f,
                    y + style.kicker_size * 0.84f, style.kicker_size, theme.text_muted);
        leave();
        y += style.kicker_size + 14.0f;
    }
    if (!title.empty())
    {
        arrive();
        const std::vector<std::string> lines =
            wrap_heading(canvas, theme, title, style.title_size, room, 1);
        if (!lines.empty())
            paint.heading(lines[0], x - 1.0f, y + style.title_size * 0.84f, style.title_size,
                          theme.text);
        leave();
        y += style.title_size + 8.0f;
    }
    if (!subtitle.empty())
    {
        arrive();
        paint.body(fit_body(paint, subtitle, style.subtitle_size, room), x,
                   y + style.subtitle_size * 0.9f, style.subtitle_size, theme.text_muted);
        leave();
        y += style.subtitle_size + 10.0f;
    }
    list.rounded_rect({x, y + kRule * 0.4f, room, 1.5f}, 0.0f, theme.text_muted.with_alpha(0.25f));

    list_.draw(canvas);
    list.pop_transform();

    if (side)
    {
        const Rect beside = side_rect();
        if (beside.w > 80.0f && beside.h > 80.0f)
        {
            // The side panel comes from the other edge, a little later.
            const float late = calm ? 1.0f : tween::cubic_out((age_ - 0.08f) / 0.4f);
            const float shown = open_ ? late : 1.0f;
            list.push_opacity(shown);
            list.push_transform(scale, beside.cx(), beside.cy(),
                                centred ? 0.0f : -side_sign * (away + 40.0f * (1.0f - shown)),
                                centred ? away * 0.3f + 20.0f * (1.0f - shown) : 0.0f);
            draw_overlay_panel(canvas, theme, beside, style.frosted, style.frost);
            side(canvas, beside.inset(style.padding), fade * shown);
            list.pop_transform();
            list.pop_opacity();
        }
    }
    list.pop_opacity();
}

} // namespace hui::ui
