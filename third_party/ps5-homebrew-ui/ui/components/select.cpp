// ps5-homebrew-ui - Component: Select.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/select.hpp"

#include "ui/components/focus_frame.hpp"
#include "ui/components/overlay.hpp"

#include <algorithm>
#include <cmath>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

// Design languages whose surfaces carry their own depth cue: a soft drop
// shadow under them would belong to another language (the Menu does the same).
bool own_depth(SurfaceStyle style)
{
    return style == SurfaceStyle::hard || style == SurfaceStyle::bevel ||
           style == SurfaceStyle::pixel || style == SurfaceStyle::neumorphic ||
           style == SurfaceStyle::sketch;
}

// The check mark of Painter::checkbox, on its own: the short stroke, then the
// long one, as `value` goes from 0 to 1.
void draw_check(gfx::DrawList &list, const Rect &r, float value, Color ink)
{
    if (value <= 0.01f)
        return;
    const float s = r.w;
    const float x0 = r.x + s * 0.14f, y0 = r.y + s * 0.52f;
    const float x1 = r.x + s * 0.40f, y1 = r.y + s * 0.78f;
    const float x2 = r.x + s * 0.88f, y2 = r.y + s * 0.22f;
    const float first = tween::clamp01(value * 2.5f);
    const float second = tween::clamp01((value - 0.4f) / 0.6f);
    const float width = std::max(3.0f, s * 0.14f);
    list.line(x0, y0, x0 + (x1 - x0) * first, y0 + (y1 - y0) * first, width, ink);
    if (second > 0.0f)
        list.line(x1, y1, x1 + (x2 - x1) * second, y1 + (y2 - y1) * second, width, ink);
}

} // namespace

void Select::set_options(std::vector<SelectOption> options)
{
    options_ = std::move(options);
    described_ = std::any_of(options_.begin(), options_.end(), [](const SelectOption &option)
                             { return !option.description.empty(); });
    const int count = static_cast<int>(options_.size());
    index_ = std::clamp(index_, -1, count - 1);

    std::vector<ListItem> items;
    items.reserve(options_.size());
    for (const SelectOption &option : options_)
    {
        ListItem item;
        item.title = option.label;
        item.subtitle = option.description;
        item.swatch = option.swatch;
        item.disabled = option.disabled;
        item.tag = option.tag;
        items.push_back(std::move(item));
    }
    sync();
    list_.set_items(std::move(items));
    laid_row_ = row_size();
    laid_gap_ = style.row_gap;
}

void Select::set_index(int index)
{
    index_ = std::clamp(index, -1, static_cast<int>(options_.size()) - 1);
}

const std::string &Select::value() const
{
    static const std::string kNone;
    return index_ < 0 ? kNone : options_[static_cast<std::size_t>(index_)].label;
}

float Select::preferred_height() const
{
    const bool above = style.label == SelectLabel::above && !label_.empty();
    return style.field_height + (above ? style.label_size + style.label_gap : 0.0f);
}

Rect Select::field_rect() const
{
    const bool above = style.label == SelectLabel::above && !label_.empty();
    const float top = above ? style.label_size + style.label_gap : 0.0f;
    return {bounds_.x, bounds_.y + top, bounds_.w, style.field_height};
}

float Select::row_size() const
{
    return described_ ? style.described_height : style.row_height;
}

// Below the field, or above it when the list does not fit below and there is
// more room above; then as many rows as that side has room for.
Select::Placement Select::place() const
{
    Placement out;
    const Rect field = field_rect();
    const float row = row_size();
    const auto height_for = [&](int rows)
    {
        return static_cast<float>(rows) * row + static_cast<float>(rows - 1) * style.row_gap +
               2.0f * style.popover_padding;
    };
    const int count = static_cast<int>(options_.size());
    int rows = std::max(std::min(count, std::max(style.max_rows, 1)), 1);
    const float below = limits_.y + limits_.h - (field.y + field.h) - style.popover_gap;
    const float above = field.y - limits_.y - style.popover_gap;
    out.above = height_for(rows) > below && above > below;
    const float room = out.above ? above : below;
    while (rows > 2 && height_for(rows) > room)
        --rows;
    out.rows = rows;
    const float w = style.popover_width > 0.0f ? style.popover_width : field.w;
    const float h = height_for(rows);
    const float x = std::clamp(field.x, limits_.x, std::max(limits_.x, limits_.x + limits_.w - w));
    out.panel = {
        x, out.above ? field.y - style.popover_gap - h : field.y + field.h + style.popover_gap, w,
        h};
    return out;
}

Rect Select::popover_rect() const
{
    return place().panel;
}

bool Select::opens_above() const
{
    return place().above;
}

// The list is an ordinary ListView that takes its look from this style. It
// is refreshed every frame, so a theme or a knob changed while the list is
// open shows at once. The slots are bound here too, never in a constructor:
// a Select that was copied or moved must not call into the one it came from.
void Select::sync()
{
    ListStyle &to = list_.style;
    to.theme = style.theme;
    // The rows sit on the popover's panel: a list without a panel of its own
    // would take the page's text colours, which differ in some themes.
    to.theme.page_text = style.theme.text;
    to.theme.page_text_muted = style.theme.text_muted;
    to.sounds = style.sounds;
    // Picking and closing are this component's to announce.
    to.sounds.activate = audio::Cue::count;
    to.sounds.cancel = audio::Cue::count;
    to.reduced_motion = style.reduced_motion;
    to.row_height = row_size();
    to.gap = style.row_gap;
    to.padding = style.row_padding;
    to.title_size = style.option_size;
    to.subtitle_size = style.description_size;
    to.leading_width = style.leading_width;
    to.highlight = style.highlight;
    to.wrap = style.wrap;
    to.entrance_step = style.entrance_step;
    to.focus_shift = 0.0f;
    to.panel = false;
    to.cards = false;

    if (style.leading_width > 0.0f && leading)
        list_.leading =
            [this](Canvas &canvas, const Rect &box, const ListItem &, int index, float focus)
        { leading(canvas, box, options_[static_cast<std::size_t>(index)], index, focus); };
    else
        list_.leading = nullptr;
    if (style.check)
        list_.trailing =
            [this](Canvas &canvas, const Rect &row, const ListItem &, int index, float focus)
        {
            if (index != index_)
                return;
            const float size = 22.0f;
            draw_check(
                canvas.list,
                {row.x + row.w - style.row_padding - size, row.cy() - size * 0.5f, size, size},
                1.0f, Highlight::text_color(style, style.highlight, focus));
        };
    else
        list_.trailing = nullptr;

    // Row sizes are baked into the list when its items are set.
    if (!list_.items().empty() && (laid_row_ != to.row_height || laid_gap_ != to.gap))
    {
        const int focus = list_.focus();
        list_.set_items(list_.items());
        list_.set_focus(focus);
        laid_row_ = to.row_height;
        laid_gap_ = to.gap;
    }
    const Placement at = place();
    const bool overflow = static_cast<int>(options_.size()) > at.rows;
    Rect inner = at.panel.inset(style.popover_padding);
    // The list draws its scroll thumb just past its right edge.
    if (overflow)
        inner.w -= 10.0f;
    const Rect &now = list_.bounds();
    // set_bounds snaps the list's highlight and scroll: only when it moved.
    if (now.x != inner.x || now.y != inner.y || now.w != inner.w || now.h != inner.h)
        list_.set_bounds(inner);
}

void Select::open(Feedback &feedback)
{
    if (options_.empty())
        return;
    open_ = true;
    sync();
    list_.set_focus(std::max(index_, 0));
    list_.set_active(true);
    list_.enter();
    play_cue(feedback, style, style.sounds.open, field_rect().cx());
}

void Select::close(Feedback &feedback)
{
    if (!open_)
        return;
    open_ = false;
    play_cue(feedback, style, style.sounds.close, field_rect().cx());
}

// Left and right on the closed field: the next option that can be chosen.
Event Select::step(int direction, const InputFrame &input, Feedback &feedback)
{
    const float x = field_rect().cx();
    const int count = static_cast<int>(options_.size());
    int next = index_ + direction;
    while (next >= 0 && next < count && options_[static_cast<std::size_t>(next)].disabled)
        next += direction;
    if (next < 0 || next >= count)
        return refuse(feedback, style, input, refusal_, x);
    index_ = next;
    play_cue(feedback, style, style.sounds.change, x, direction > 0 ? 1.05f : 0.95f);
    return Event::changed;
}

Event Select::handle(const InputFrame &input, Feedback &feedback)
{
    const float x = field_rect().cx();
    if (!open_)
    {
        if (input.is_pressed(Action::confirm))
        {
            if (options_.empty())
                return refuse(feedback, style, input, refusal_, x);
            open(feedback);
            return Event::activated;
        }
        if (style.step_closed && (input.nav == Direction::left || input.nav == Direction::right))
            return step(input.nav == Direction::right ? 1 : -1, input, feedback);
        return Event::none;
    }

    const Event event = list_.handle(input, feedback);
    if (event == Event::activated)
    {
        const int picked = list_.focus();
        if (picked == index_)
        {
            close(feedback);
            return Event::cancelled;
        }
        index_ = picked;
        open_ = false;
        play_cue(feedback, style, style.sounds.activate, x);
        if (style.sounds.rumble > 0.0f)
            feedback.rumble(0.3f * style.sounds.rumble, 0.04f);
        return Event::changed;
    }
    if (event == Event::cancelled)
    {
        close(feedback);
        return Event::cancelled;
    }
    return event;
}

void Select::update(float dt)
{
    sync();
    focus_.target = active_ || open_ ? 1.0f : 0.0f;
    focus_.update(dt, 18.0f);
    // Opening springs a little past its size; closing is quicker and plain.
    amount_.target = open_ ? 1.0f : 0.0f;
    const float omega = std::max(style.omega(), 14.0f);
    if (open_)
        amount_.update(dt, omega,
                       style.reduced_motion ? 1.0f : std::clamp(style.damping(), 0.7f, 0.9f));
    else
        amount_.update(dt, omega * 1.5f, 1.0f);
    refusal_.update(dt, 9.0f);
    list_.update(dt);
}

void Select::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const float focus = focus_.value;
    const bool above = style.label == SelectLabel::above && !label_.empty();
    if (above)
        paint.label(fit_label(paint, label_, style.label_size, bounds_.w), bounds_.x,
                    bounds_.y + style.label_size * 0.82f, style.label_size,
                    style.on_page ? paint.page_text_muted() : theme.text_muted);

    const float shift = style.reduced_motion ? 0.0f : shake(refusal_.value, canvas.time, 8.0f);
    Rect box = field_rect();
    box.x += shift;
    const float radius = paint.control_radius(box);
    const bool ring_first = focus_frame_goes_under(theme);
    if (style.focus_ring && ring_first)
        focus_frame(canvas, theme, box, radius, focus, true);
    paint.well(box, radius, theme.surface_high);

    // ---- the chevron: it turns over as the list opens ----
    const float open = tween::clamp01(amount_.value);
    const float s = style.chevron_size;
    const float cx = box.x + box.w - style.padding - s;
    const float tip = s * 0.55f * (1.0f - 2.0f * open);
    const Color quiet = gfx::mix(theme.text_muted, theme.text, std::max(focus, open));
    list.line(cx - s, box.cy() - tip, cx, box.cy() + tip, style.chevron_width, quiet);
    list.line(cx, box.cy() + tip, cx + s, box.cy() - tip, style.chevron_width, quiet);

    // ---- label and value ----
    float left = box.x + style.padding;
    const float right = cx - s - 14.0f;
    const float baseline = box.cy() + style.value_size * 0.35f;
    const bool inside = style.label == SelectLabel::inside && !label_.empty();
    const bool chosen = index_ >= 0;
    const std::string &text = chosen ? value() : placeholder_;
    const Color ink = chosen ? theme.text : theme.text_muted;
    if (inside)
    {
        // The label keeps at most half the field; the value gets the rest.
        const float room = std::max(right - left, 40.0f);
        const std::string name = fit_label(paint, label_, style.value_size, room * 0.5f);
        const float used = paint.label(name, left, baseline, style.value_size, theme.text_muted);
        const float value_room = std::max(room - used - 24.0f, 30.0f);
        float end = right;
        end -= paint.label(fit_label(paint, text, style.value_size, value_room), end, baseline,
                           style.value_size, ink, gfx::Align::right);
        if (chosen && options_[static_cast<std::size_t>(index_)].swatch.a > 0.0f)
            list.circle(end - 20.0f, box.cy(), 8.0f,
                        options_[static_cast<std::size_t>(index_)].swatch);
    }
    else
    {
        if (chosen && options_[static_cast<std::size_t>(index_)].swatch.a > 0.0f)
        {
            list.circle(left + 8.0f, box.cy(), 8.0f,
                        options_[static_cast<std::size_t>(index_)].swatch);
            left += 30.0f;
        }
        paint.label(fit_label(paint, text, style.value_size, std::max(right - left, 40.0f)), left,
                    baseline, style.value_size, ink);
    }

    if (style.focus_ring && !ring_first)
        focus_frame(canvas, theme, box, radius, focus, true);
}

void Select::draw_popover(Canvas &canvas) const
{
    if (!visible() || options_.empty())
        return;
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    const Placement at = place();
    const Rect panel = at.panel;
    const float amount = amount_.value;

    if (style.scrim > 0.0f)
        list.rounded_rect(
            {0.0f, 0.0f, gfx::kVirtualWidth, gfx::kVirtualHeight}, 0.0f,
            Color{0.0f, 0.0f, 0.0f, tween::clamp01(style.scrim) * tween::clamp01(amount)});

    // It grows out of the edge of the field it hangs from.
    const float scale = style.reduced_motion ? 1.0f : tween::lerp(0.92f, 1.0f, amount);
    list.push_opacity(tween::clamp01(amount * 1.8f));
    list.push_transform(scale, panel.cx(), at.above ? panel.y + panel.h : panel.y, 0.0f, 0.0f);

    const float radius = std::min(theme.radius_card, std::min(panel.w, panel.h) * 0.5f);
    if (style.elevation > 0.0f && !own_depth(theme.style))
        list.shadow({panel.x, panel.y + 12.0f * style.elevation, panel.w, panel.h}, radius,
                    34.0f * style.elevation, Color{0.0f, 0.0f, 0.0f, theme.dark ? 0.45f : 0.22f});
    const bool frost = style.frosted && theme.style == SurfaceStyle::glass && canvas.glass != 0;
    if (frost && style.backing > 0.0f)
        list.rounded_rect(panel, radius, theme.page.with_alpha(tween::clamp01(style.backing)));
    draw_overlay_panel(canvas, theme, panel, frost);
    list_.draw(canvas);

    list.pop_transform();
    list.pop_opacity();
}

} // namespace hui::ui
