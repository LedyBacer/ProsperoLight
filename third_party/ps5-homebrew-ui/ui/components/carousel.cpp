// ps5-homebrew-ui - Component: Carousel.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/carousel.hpp"

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

// The item a slot of a ring shows.
int wrapped(int slot, int count)
{
    return ((slot % count) + count) % count;
}

} // namespace

void Carousel::set_items(std::vector<CardItem> items)
{
    items_ = std::move(items);
    focus_ = std::clamp(focus_, 0, std::max(static_cast<int>(items_.size()) - 1, 0));
    retarget(true);
}

void Carousel::set_bounds(const Rect &bounds)
{
    bounds_ = bounds;
    retarget(true);
}

void Carousel::set_focus(int index, bool snap)
{
    if (items_.empty())
        return;
    focus_ = std::clamp(index, 0, static_cast<int>(items_.size()) - 1);
    slot_ = nearest_slot(focus_);
    retarget(snap);
}

// A ring needs enough items that none is in view twice.
bool Carousel::endless(const Layout &at) const
{
    return style.mode == CarouselMode::centered && style.wrap &&
           at.span >= bounds_.w + 3.0f * at.pitch;
}

// The slot showing an item that is closest to where the focus is now.
int Carousel::nearest_slot(int index) const
{
    const int count = static_cast<int>(items_.size());
    if (count == 0 || !endless(layout()))
        return index;
    int delta = wrapped(index - slot_, count);
    if (delta > count / 2)
        delta -= count;
    return slot_ + delta;
}

void Carousel::enter()
{
    age_ = 0.0f;
}

Carousel::Layout Carousel::layout() const
{
    Layout at;
    const int count = static_cast<int>(items_.size());
    const float grow = std::max(style.card.focus_scale - 1.0f, 0.0f);
    at.item_w = style.item_width;
    if (style.mode == CarouselMode::paged)
    {
        const float room = bounds_.w - 2.0f * style.peek;
        if (style.per_page > 0)
        {
            at.per_page = style.per_page;
            at.item_w = (room - static_cast<float>(at.per_page - 1) * style.gap) /
                        static_cast<float>(at.per_page);
        }
        else
        {
            at.per_page = std::max(
                1, static_cast<int>((room + style.gap) / (std::max(at.item_w, 8.0f) + style.gap)));
        }
        at.pages = std::max(1, (count + at.per_page - 1) / at.per_page);
    }
    at.item_w = std::max(at.item_w, 8.0f);
    at.item_h = style.item_height > 0.0f ? style.item_height : card_height(style.card, at.item_w);
    at.pitch = at.item_w + style.gap;
    // A page is a fixed window: its items grow over each other like the
    // cells of a grid. In the other modes the focused item pushes the row.
    at.push =
        style.mode == CarouselMode::paged || style.reduced_motion ? 0.0f : at.item_w * grow * 0.5f;
    const float title_h = title.empty() ? 0.0f : style.title_size + style.title_gap;
    at.row_y = bounds_.y + title_h + at.item_h * grow * 0.5f + style.card.lift;
    at.span = static_cast<float>(count) * at.pitch - style.gap;
    return at;
}

float Carousel::preferred_height() const
{
    const Layout at = layout();
    const float dots =
        style.mode == CarouselMode::paged && style.dots ? style.dots_gap + style.dot_size : 0.0f;
    return at.row_y - bounds_.y + at.item_h * (1.0f + 0.5f * (style.card.focus_scale - 1.0f)) +
           dots + 4.0f;
}

int Carousel::page() const
{
    return style.mode == CarouselMode::paged ? focus_ / layout().per_page : 0;
}

int Carousel::pages() const
{
    return layout().pages;
}

// Where the row should be scrolled to for the current focus.
float Carousel::scroll_target(const Layout &at) const
{
    const float at_focus = static_cast<float>(slot_) * at.pitch;
    switch (style.mode)
    {
    case CarouselMode::centered:
        return at_focus + at.push + at.item_w * 0.5f - bounds_.w * 0.5f;
    case CarouselMode::paged:
    {
        // Whole pages, except that the first and the last sit flush with the
        // ends of the shelf instead of leaving a hole there.
        const int first = (focus_ / at.per_page) * at.per_page;
        const float wanted = static_cast<float>(first) * at.pitch - style.peek;
        return std::clamp(wanted, 0.0f, std::max(at.span - bounds_.w, 0.0f));
    }
    default:
    {
        float wanted = std::max(at_focus - style.peek, 0.0f);
        if (style.stop_at_end)
            wanted = std::min(wanted, std::max(at.span + 2.0f * at.push - bounds_.w, 0.0f));
        return wanted;
    }
    }
}

// The left edge of an item (or of a position between two) in the row's own
// space. The focused item grows about its centre; everything after it moves
// over by that growth, so the gaps stay what they are while it travels.
float Carousel::content_x(const Layout &at, float index) const
{
    const float grown = style.mode == CarouselMode::centered ? 1.0f : active_amount_.value;
    return index * at.pitch +
           at.push * grown * (std::clamp(index - position_.value, -1.0f, 1.0f) + 1.0f);
}

Rect Carousel::item_rect(int index) const
{
    const Layout at = layout();
    return {bounds_.x + content_x(at, static_cast<float>(nearest_slot(index))) - scroll_.value,
            at.row_y, at.item_w, at.item_h};
}

void Carousel::retarget(bool snap)
{
    if (items_.empty())
        return;
    focus_ = std::clamp(focus_, 0, static_cast<int>(items_.size()) - 1);
    const Layout at = layout();
    const int count = static_cast<int>(items_.size());
    if (!endless(at) || wrapped(slot_, count) != focus_)
        slot_ = focus_;
    position_.target = static_cast<float>(slot_);
    scroll_.target = scroll_target(at);
    page_.target = static_cast<float>(focus_ / at.per_page);
    const CardItem &item = items_[static_cast<std::size_t>(focus_)];
    const Color glow = item.accent.a > 0.0f ? item.accent : style.theme.focus;
    glow_.target(glow);
    if (snap)
    {
        position_.snap(position_.target);
        scroll_.snap(scroll_.target);
        page_.snap(page_.target);
        glow_.snap(glow);
    }
}

Event Carousel::handle(const InputFrame &input, Feedback &feedback)
{
    exit_ = Direction::none;
    if (items_.empty())
        return Event::none;
    const int count = static_cast<int>(items_.size());
    const Layout at = layout();
    // Where the focused item will rest: sounds come from there.
    const auto resting_x = [&]()
    {
        const float x = bounds_.x + static_cast<float>(slot_) * at.pitch + at.push -
                        scroll_target(at) + at.item_w * 0.5f;
        return std::clamp(x, bounds_.x, bounds_.x + bounds_.w);
    };
    if (input.nav == Direction::left || input.nav == Direction::right)
    {
        const int direction = input.nav == Direction::right ? 1 : -1;
        const bool ring = endless(at);
        int next = focus_ + direction;
        // A ring simply keeps turning; a row runs back to its other end, and
        // that is a decision, not something a held direction does.
        if ((next < 0 || next >= count) && style.wrap && count > 1 && (ring || !input.nav_repeat))
            next = next < 0 ? count - 1 : 0;
        if (next < 0 || next >= count)
        {
            if (style.exits.allows(input.nav))
            {
                exit_ = input.nav;
                return Event::none;
            }
            return refuse(feedback, style, input, refusal_, resting_x());
        }
        const int page_before = focus_ / at.per_page;
        focus_ = next;
        slot_ = ring ? slot_ + direction : focus_;
        retarget(false);
        // Turning a page is a bigger step than moving inside one.
        const bool turned =
            style.mode == CarouselMode::paged && focus_ / at.per_page != page_before;
        play_cue(feedback, style, turned ? style.sounds.page : style.sounds.move, resting_x());
        return Event::moved;
    }
    if (input.is_pressed(Action::confirm))
    {
        if (items_[static_cast<std::size_t>(focus_)].disabled)
            return refuse(feedback, style, input, refusal_, resting_x());
        press_.trigger();
        play_cue(feedback, style, style.sounds.activate, resting_x());
        if (style.sounds.rumble > 0.0f)
            feedback.rumble(0.3f * style.sounds.rumble, 0.04f);
        return Event::activated;
    }
    if (input.is_pressed(Action::back))
    {
        play_cue(feedback, style, style.sounds.cancel, resting_x());
        return Event::cancelled;
    }
    return Event::none;
}

void Carousel::update(float dt)
{
    age_ += dt;
    // Every frame, so a change of style (mode, sizes, theme) simply takes
    // effect: the row glides to where it now belongs.
    retarget(false);
    // The focus must feel immediate whatever the theme's pace, and a hard
    // bounce on it reads as a glitch: both are clamped (as in ui::Highlight).
    position_.update(dt, std::max(style.omega(), 18.0f), std::max(style.damping(), 0.78f));
    scroll_.update(dt, std::max(style.omega(), 14.0f));
    page_.update(dt, std::max(style.omega(), 18.0f));
    glow_.update(dt, 8.0f);
    active_amount_.target = active_ ? 1.0f : 0.0f;
    active_amount_.update(dt, 18.0f);
    press_.update(dt, 10.0f);
    refusal_.update(dt, 9.0f);
    // A ring that has been turned comes back to small numbers while it
    // rests, where nobody can see the jump.
    if (slot_ != focus_ && position_.value == position_.target && scroll_.settled())
    {
        const float back = static_cast<float>(slot_ - focus_) * layout().pitch;
        slot_ = focus_;
        position_.snap(static_cast<float>(slot_));
        scroll_.snap(scroll_.target - back);
    }
}

void Carousel::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const Layout at = layout();
    const int count = static_cast<int>(items_.size());
    const Color ink = style.card.on_panel ? theme.text : paint.page_text();
    const Color quiet = style.card.on_panel ? theme.text_muted : paint.page_text_muted();
    const float active = active_amount_.value;

    if (!title.empty())
    {
        list.push_opacity(tween::cubic_out(age_ / 0.3f));
        const float baseline = bounds_.y + style.title_size * 0.82f;
        paint.label(title, bounds_.x, baseline, style.title_size, ink);
        if (style.counter && count > 0)
        {
            char text[32];
            std::snprintf(text, sizeof(text), "%02d / %02d", focus_ + 1, count);
            paint.body(text, bounds_.x + bounds_.w, baseline, 20.0f, quiet, gfx::Align::right);
        }
        list.pop_opacity();
    }
    if (items_.empty())
        return;

    const float left = bounds_.x;
    const float right = bounds_.x + bounds_.w;
    // Sideways the clip leaves room for the ring of an item that rests on
    // the edge; items cut by it are already fading (see `edge`).
    constexpr float kBleed = 16.0f;
    list.push_clip(
        {left - kBleed, bounds_.y - 60.0f, bounds_.w + 2.0f * kBleed, bounds_.h + 120.0f});

    // The share of an item that is inside the shelf, 0..1.
    const auto inside = [&](const Rect &r)
    { return tween::clamp01((std::min(r.x + r.w, right) - std::max(r.x, left)) / r.w); };
    const auto edge = [&](const Rect &r)
    {
        const float share = inside(r);
        if (share <= 0.0f || style.edge_fade <= 0.0f)
            return share > 0.0f ? 1.0f : 0.0f;
        // Eased, so the sliver of a peeking item is still there to see.
        return tween::cubic_out(share / style.edge_fade);
    };
    const auto entrance = [&](int index)
    {
        if (style.entrance_step <= 0.0f || style.reduced_motion)
            return tween::cubic_out(age_ / 0.2f);
        return tween::stagger(age_, std::abs(index - slot_), style.entrance_step, 0.34f);
    };
    const float nudge = shake(refusal_.value, canvas.time, 9.0f);
    const bool centered = style.mode == CarouselMode::centered;

    const bool ring = endless(at);
    // `slot` is a place on the row; on a ring it wraps onto the items.
    const auto draw_item = [&](int slot, float nearby)
    {
        const int index = ring ? wrapped(slot, count) : slot;
        const CardItem &item = items_[static_cast<std::size_t>(index)];
        const Rect r{left + content_x(at, static_cast<float>(slot)) - scroll_.value, at.row_y,
                     at.item_w, at.item_h};
        const float distance = std::fabs(static_cast<float>(slot) - position_.value);
        const float arrived = entrance(slot);
        const float alpha =
            edge(r) * arrived * std::max(1.0f - style.neighbour_fade * distance, 0.12f);
        if (alpha <= 0.0f)
            return;
        const bool held = nearby > 0.5f;
        CardState state;
        state.focus = nearby * active;
        // The wheel keeps its shape when the focus is elsewhere; a shelf lets go.
        state.emphasis = centered ? nearby : nearby * active;
        state.press = held ? press_.value : 0.0f;
        state.marks = false; // one ring glides for the whole shelf
        // A picture cut by the edge reads as "there is more"; a cut word
        // reads as a mistake. The words wait until the item is nearly whole.
        state.text = tween::smoothstep((inside(r) - 0.6f) / 0.35f);
        list.push_opacity(alpha);
        list.push_transform(
            1.0f, 0.0f, 0.0f,
            (style.reduced_motion ? 0.0f : 28.0f * (1.0f - arrived)) + (held ? nudge : 0.0f), 0.0f);
        if (content)
        {
            list.push_transform(card_scale(style, style.card, state), r.cx(), r.cy(), 0.0f,
                                -card_lift(style, style.card, state));
            content(canvas, r, item, index, state.focus);
            list.pop_transform();
        }
        else
        {
            draw_card(canvas, style, style.card, r, item, state, art);
        }
        list.pop_transform();
        list.pop_opacity();
    };

    // Only what can be seen, and the (at most two) items under the focus
    // last, the nearer one on top.
    const float reach = kBleed + 2.0f * at.push;
    const int low = ring ? -(1 << 20) : 0;
    const int high = ring ? (1 << 20) : count - 1;
    const int first =
        std::clamp(static_cast<int>(std::floor((scroll_.value - reach) / at.pitch)) - 1, low, high);
    const int last = std::clamp(
        static_cast<int>(std::ceil((scroll_.value + bounds_.w + reach) / at.pitch)), low, high);
    const int under = std::clamp(static_cast<int>(std::floor(position_.value)), low, high);
    const int beside = std::min(under + 1, high);
    const auto nearness = [&](int slot)
    { return tween::clamp01(1.0f - std::fabs(static_cast<float>(slot) - position_.value)); };
    for (int slot = first; slot <= last; ++slot)
    {
        if (slot != under && slot != beside)
            draw_item(slot, 0.0f);
    }

    // The one focus indicator, at the gliding position.
    const Rect spot{left + content_x(at, position_.value) - scroll_.value + nudge, at.row_y,
                    at.item_w, at.item_h};
    CardState lead;
    lead.focus = active;
    lead.emphasis = centered ? 1.0f : active;
    lead.press = press_.value;
    const Rect frame =
        card_placed(spot, content ? spot : card_frame(style.card, spot),
                    card_scale(style, style.card, lead), card_lift(style, style.card, lead));
    const float amount = (0.35f + 0.65f * active) * entrance(slot_) * edge(spot);
    draw_card_halo(canvas, style, style.card, frame, glow_.value(), amount * active);
    const bool beside_on_top = nearness(beside) > nearness(under);
    draw_item(beside_on_top ? under : beside, nearness(beside_on_top ? under : beside));
    if (beside != under)
        draw_item(beside_on_top ? beside : under, nearness(beside_on_top ? beside : under));
    draw_card_ring(canvas, style, style.card, frame, amount);
    list.pop_clip();

    if (style.mode == CarouselMode::paged && style.dots && at.pages > 1)
    {
        // The current page's dot is a short bar; it hands its length to the
        // next one as the page turns.
        const float dot = style.dot_size;
        const float bar = dot * 3.0f;
        const float gap = dot;
        const float width = static_cast<float>(at.pages) * (dot + gap) - gap + (bar - dot);
        if (width <= bounds_.w)
        {
            const float grow = std::max(style.card.focus_scale - 1.0f, 0.0f);
            const float y = at.row_y + at.item_h * (1.0f + grow * 0.5f) + style.dots_gap;
            const float round =
                theme.corner == Corner::round && theme.radius >= 2.0f ? dot * 0.5f : 0.0f;
            const Color lit = theme.focus.a > 0.6f ? theme.focus : theme.primary;
            float x = bounds_.cx() - width * 0.5f;
            list.push_opacity(tween::cubic_out(age_ / 0.3f) * (0.55f + 0.45f * active));
            for (int i = 0; i < at.pages; ++i)
            {
                const float weight =
                    tween::clamp01(1.0f - std::fabs(page_.value - static_cast<float>(i)));
                const float length = dot + (bar - dot) * weight;
                list.rounded_rect({x, y, length, dot}, round,
                                  gfx::mix(quiet.with_alpha(0.45f), lit, weight));
                x += length + gap;
            }
            list.pop_opacity();
        }
    }
}

} // namespace hui::ui
