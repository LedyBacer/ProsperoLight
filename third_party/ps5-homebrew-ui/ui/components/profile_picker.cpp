// ps5-homebrew-ui - Component: ProfilePicker.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/profile_picker.hpp"

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

constexpr float kAvatarTop = 24.0f; // from the card's top edge to the avatar
constexpr float kTitleGap = 34.0f;  // under the title

bool lit_theme(const Theme &theme)
{
    return theme.style != SurfaceStyle::hard && theme.style != SurfaceStyle::pixel &&
           theme.style != SurfaceStyle::bevel && theme.style != SurfaceStyle::sketch &&
           theme.style != SurfaceStyle::neumorphic;
}

bool square_theme(const Theme &theme)
{
    return theme.corner != Corner::round || theme.radius < 2.0f;
}

} // namespace

void ProfilePicker::set_profiles(std::vector<Profile> profiles)
{
    profiles_ = std::move(profiles);
    avatars_.assign(profiles_.size(), Avatar{});
    for (std::size_t i = 0; i < profiles_.size(); ++i)
    {
        avatars_[i].set_name(profiles_[i].name);
        avatars_[i].set_image(profiles_[i].image, profiles_[i].uv);
    }
    amounts_.assign(static_cast<std::size_t>(count()), tween::Spring{});
    focus_ = std::clamp(focus_, 0, std::max(count() - 1, 0));
    layout(true);
}

void ProfilePicker::set_bounds(const Rect &bounds)
{
    bounds_ = bounds;
    layout(true);
}

int ProfilePicker::count() const
{
    return static_cast<int>(profiles_.size()) + (style.add_card ? 1 : 0);
}

bool ProfilePicker::add_focused() const
{
    return style.add_card && focus_ == static_cast<int>(profiles_.size());
}

void ProfilePicker::set_focus(int index, bool snap)
{
    if (count() == 0)
        return;
    focus_ = std::clamp(index, 0, count() - 1);
    layout(snap);
}

void ProfilePicker::enter()
{
    age_ = 0.0f;
}

int ProfilePicker::columns() const
{
    const int n = std::max(count(), 1);
    return style.columns > 0 ? std::min(style.columns, n) : n;
}

int ProfilePicker::row_count() const
{
    const int across = columns();
    return (std::max(count(), 1) + across - 1) / across;
}

int ProfilePicker::row_length(int row) const
{
    return std::clamp(count() - row * columns(), 0, columns());
}

float ProfilePicker::head() const
{
    return style.title.empty() ? 0.0f : style.title_size + kTitleGap;
}

float ProfilePicker::content_width() const
{
    const float across = static_cast<float>(columns());
    return across * style.card_width + (across - 1.0f) * style.gap;
}

float ProfilePicker::height() const
{
    const float rows = static_cast<float>(row_count());
    const float grown = style.card_height * (style.focus_scale - 1.0f);
    return head() + style.lift + grown + 12.0f + rows * style.card_height +
           (rows - 1.0f) * (style.gap + style.expand + grown) + style.expand + 12.0f;
}

Rect ProfilePicker::rest_rect(int index) const
{
    const int across = columns();
    const int row = index / across;
    const int column = index % across;
    const float length = static_cast<float>(std::max(row_length(row), 1));
    const float row_width = length * style.card_width + (length - 1.0f) * style.gap;
    // Room at the sides for the focused card's growth and its ring.
    const float pad = style.card_width * (style.focus_scale - 1.0f) * 0.5f + 12.0f;
    const bool overflow = content_width() > bounds_.w - 2.0f * pad;
    float x = bounds_.x + pad;
    if (!overflow && style.align == gfx::Align::center)
        x = bounds_.cx() - row_width * 0.5f;
    else if (!overflow && style.align == gfx::Align::right)
        x = bounds_.x + bounds_.w - pad - row_width;
    const float top = bounds_.y + head() + style.lift +
                      style.card_height * (style.focus_scale - 1.0f) * 0.5f + 12.0f;
    return {x + static_cast<float>(column) * (style.card_width + style.gap),
            // Rows stand far enough apart for a card to grow and open downward.
            top + static_cast<float>(row) *
                      (style.card_height * style.focus_scale + style.gap + style.expand),
            style.card_width, style.card_height};
}

Rect ProfilePicker::card_rect(int index) const
{
    Rect r = rest_rect(index);
    r.x -= scroll_.offset();
    return r;
}

Rect ProfilePicker::focused_rect(int index) const
{
    Rect r = rest_rect(index);
    const float cy = r.cy();
    const float rest_height = r.h;
    r.h += style.expand;
    if (style.reduced_motion)
        return r;
    // draw_card() scales about the resting centre, so the top edge moves up by
    // half the growth and the extra height opens downward.
    const float s = style.focus_scale;
    return {r.cx() - r.w * s * 0.5f, cy - rest_height * s * 0.5f - style.lift, r.w * s, r.h * s};
}

Color ProfilePicker::accent_of(int index) const
{
    const Theme &theme = style.theme;
    const Color fallback{theme.focus.r, theme.focus.g, theme.focus.b, 1.0f};
    if (!style.accent_by_profile || index >= static_cast<int>(profiles_.size()))
        return fallback;
    const Profile &who = profiles_[static_cast<std::size_t>(index)];
    if (who.accent.a > 0.0f)
        return who.accent;
    return Avatar::color_of(who.name, 0.6f, 0.8f);
}

int ProfilePicker::nearest_in_row(int row, float x) const
{
    int best = -1;
    float best_distance = 0.0f;
    const int across = columns();
    for (int i = row * across; i < row * across + row_length(row); ++i)
    {
        const float distance = std::fabs(rest_rect(i).cx() - x);
        if (best < 0 || distance < best_distance)
        {
            best = i;
            best_distance = distance;
        }
    }
    return best;
}

// Everything that depends on the style and the focus but not on time.
void ProfilePicker::layout(bool snap)
{
    if (amounts_.size() != static_cast<std::size_t>(count()))
        amounts_.assign(static_cast<std::size_t>(count()), tween::Spring{});
    for (std::size_t i = 0; i < avatars_.size(); ++i)
    {
        Avatar &face = avatars_[i];
        static_cast<ComponentStyle &>(face.style) = style;
        face.style.shape = style.avatar_shape;
        const Rect card = rest_rect(static_cast<int>(i));
        face.set_bounds({card.cx() - style.avatar_size * 0.5f, card.y + kAvatarTop,
                         style.avatar_size, style.avatar_size});
    }
    if (count() == 0)
        return;
    focus_ = std::clamp(focus_, 0, count() - 1);
    const Rect at = rest_rect(focus_);
    const float pad = style.card_width * (style.focus_scale - 1.0f) * 0.5f + 12.0f;
    if (row_count() == 1 && content_width() > bounds_.w - 2.0f * pad)
        scroll_.reveal(at.x - bounds_.x - pad, at.x + at.w - bounds_.x - pad,
                       bounds_.w - 2.0f * pad, style.gap, content_width());
    else
        scroll_.position.target = 0.0f;
    highlight_.target(focused_rect(focus_));
    if (snap)
    {
        scroll_.position.snap(scroll_.position.target);
        highlight_.snap(focused_rect(focus_));
        for (std::size_t i = 0; i < amounts_.size(); ++i)
            amounts_[i].snap(static_cast<int>(i) == focus_ ? 1.0f : 0.0f);
    }
}

Event ProfilePicker::handle(const InputFrame &input, Feedback &feedback)
{
    exit_ = Direction::none;
    if (count() == 0)
        return Event::none;
    const float x = card_rect(focus_).cx();
    if (input.nav != Direction::none)
    {
        const int across = columns();
        const int row = focus_ / across;
        int next = -1;
        if (input.nav == Direction::left || input.nav == Direction::right)
        {
            const int first = row * across;
            const int last = first + row_length(row) - 1;
            next = focus_ + (input.nav == Direction::right ? 1 : -1);
            if (next < first || next > last)
            {
                // An exit beats the wrap: the screen asked for that edge.
                if (style.wrap && !input.nav_repeat && !style.exits.allows(input.nav))
                    next = next < first ? last : first;
                else
                    next = -1;
            }
        }
        else
        {
            const int target = row + (input.nav == Direction::down ? 1 : -1);
            if (target >= 0 && target < row_count())
                next = nearest_in_row(target, rest_rect(focus_).cx());
        }
        if (next < 0 || next == focus_)
        {
            if (style.exits.allows(input.nav))
            {
                exit_ = input.nav;
                return Event::none;
            }
            return refuse(feedback, style, input, highlight_.refusal(), x);
        }
        focus_ = next;
        layout(false);
        play_cue(feedback, style, style.sounds.move, card_rect(focus_).cx());
        return Event::moved;
    }
    if (input.is_pressed(Action::confirm))
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

void ProfilePicker::update(float dt)
{
    age_ += dt;
    layout(false);
    const float omega = std::max(style.omega(), 14.0f);
    for (std::size_t i = 0; i < amounts_.size(); ++i)
    {
        amounts_[i].target = static_cast<int>(i) == focus_ ? (active_ ? 1.0f : 0.3f) : 0.0f;
        amounts_[i].update(dt, omega);
    }
    for (Avatar &face : avatars_)
        face.update(dt);
    highlight_.update(dt, style);
    scroll_.update(dt, std::max(style.omega(), 14.0f));
    press_.update(dt, 10.0f);
}

void ProfilePicker::draw_card(Canvas &canvas, int index, float entrance) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const bool calm = style.reduced_motion;
    const std::size_t slot = static_cast<std::size_t>(index);
    const float f = slot < amounts_.size() ? tween::clamp01(amounts_[slot].value) : 0.0f;
    const bool is_add = index >= static_cast<int>(profiles_.size());

    Rect card = rest_rect(index);
    const float cx = card.cx();
    const float avatar_cy = card.y + kAvatarTop + style.avatar_size * 0.5f;
    card.h += style.expand * f;
    const float press = index == focus_ ? press_.value : 0.0f;
    const float scale = calm ? 1.0f : 1.0f + (style.focus_scale - 1.0f) * f - 0.03f * press;
    const float rise = calm ? 0.0f : 24.0f * (1.0f - entrance) - style.lift * f;
    list.push_opacity(entrance * (1.0f - style.dim * (1.0f - f)));
    // Grow about the resting centre, so the extra height opens downward.
    list.push_transform(scale, cx, rest_rect(index).cy(), 0.0f, rise);

    const float radius = std::min(theme.radius_card, std::min(card.w, card.h) * 0.5f);
    const Color accent = accent_of(index);
    if (f > 0.02f && lit_theme(theme))
        paint.halo(card, theme.corner == Corner::round ? radius : 0.0f, 26.0f,
                   accent.with_alpha(0.38f * f));

    const float name_y = card.y + kAvatarTop + style.avatar_size + 20.0f + style.name_size * 0.8f;
    const float room = card.w - 28.0f;
    if (is_add)
    {
        // An empty seat: a well instead of a card, and a plus where a face
        // would be.
        paint.well(card, radius, theme.surface_high);
        const Color ink = gfx::mix(theme.text_muted, theme.text, f);
        const float reach = style.avatar_size * 0.36f;
        const Rect seat{cx - reach, avatar_cy - reach, 2.0f * reach, 2.0f * reach};
        if (square_theme(theme))
            paint.stroke(seat, std::min(theme.radius, reach), 3.0f, ink);
        else
            list.ring(cx, avatar_cy, reach, 3.0f, ink);
        const float arm = reach * 0.42f;
        list.line(cx - arm, avatar_cy, cx + arm, avatar_cy, 4.0f, ink);
        list.line(cx, avatar_cy - arm, cx, avatar_cy + arm, 4.0f, ink);
        paint.bounded_label(style.add_label, cx, name_y, style.name_size, ink, room, gfx::Align::center);
    }
    else
    {
        const Profile &who = profiles_[slot];
        // On a panel a flat, borderless theme would lose the card: it takes a
        // step toward the well colour there.
        const Color body =
            style.on_surface ? gfx::mix(theme.surface, theme.surface_high, 0.5f) : theme.surface;
        paint.surface(card, radius, body, theme.outline, 1.0f);
        const Rect face{cx - style.avatar_size * 0.5f, card.y + kAvatarTop, style.avatar_size,
                        style.avatar_size};
        if (avatar)
            avatar(canvas, face, who, index, f);
        else
            avatars_[slot].draw(canvas);
        // The player's own colour rings the face while the card is chosen.
        if (f > 0.02f)
        {
            if (style.avatar_shape == AvatarShape::circle)
                list.ring(cx, avatar_cy, style.avatar_size * 0.5f + 7.0f, 3.0f,
                          accent.with_alpha(f));
            else
                paint.stroke(face.inset(-6.0f), std::min(theme.radius, face.w * 0.5f) + 6.0f, 3.0f,
                             accent.with_alpha(f));
        }
        paint.bounded_label(who.name, cx, name_y, style.name_size, theme.text, room, gfx::Align::center);
        const float detail_y = name_y + style.detail_size + 10.0f;
        if (!who.detail.empty())
            paint.bounded_body(who.detail, cx, detail_y, style.detail_size, theme.text_muted, room, gfx::Align::center);
        if (!who.extra.empty() && f > 0.02f && style.expand > 0.0f)
        {
            list.push_opacity(tween::clamp01(f * 1.6f - 0.6f));
            paint.bounded_body(who.extra, cx, detail_y + style.detail_size + 12.0f, style.detail_size, gfx::mix(theme.text_muted, theme.text, 0.6f), room, gfx::Align::center);
            list.pop_opacity();
        }
        if (style.slots && who.controller >= 1 && who.controller <= 4)
        {
            // Signed in: the controller that owns this profile.
            char text[16];
            std::snprintf(text, sizeof(text), "%s%d", style.slot_prefix.c_str(), who.controller);
            const float size = style.slot_size * 0.56f;
            const float width = paint.label_width(text, size) + 18.0f;
            const Rect tag{card.x + card.w - width - 12.0f, card.y + 12.0f, width, style.slot_size};
            paint.fill(tag, theme.pill_chips ? tag.h * 0.5f : std::min(theme.radius, tag.h * 0.5f),
                       Color{theme.primary.r, theme.primary.g, theme.primary.b, 1.0f});
            paint.label(text, tag.cx(), tag.cy() + size * 0.36f, size, theme.on_primary,
                        gfx::Align::center);
        }
    }
    list.pop_transform();
    list.pop_opacity();
}

void ProfilePicker::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const bool calm = style.reduced_motion;

    if (!style.title.empty())
    {
        const float in = tween::cubic_out(age_ / 0.3f);
        float x = bounds_.cx();
        if (style.align == gfx::Align::left)
            x = bounds_.x;
        else if (style.align == gfx::Align::right)
            x = bounds_.x + bounds_.w;
        list.push_opacity(in);
        paint.heading(style.title, x, bounds_.y + style.title_size * 0.84f, style.title_size,
                      style.on_surface ? theme.text : paint.page_text(), style.align);
        list.pop_opacity();
    }
    const int n = count();
    if (n == 0)
        return;

    const float pad = style.card_width * (style.focus_scale - 1.0f) * 0.5f + 12.0f;
    const bool overflow = row_count() == 1 && content_width() > bounds_.w - 2.0f * pad;
    if (overflow)
        list.push_clip({bounds_.x, bounds_.y, bounds_.w, bounds_.h + 40.0f});
    list.push_transform(1.0f, 0.0f, 0.0f, -scroll_.offset(), 0.0f);

    const auto entrance = [&](int index)
    {
        if (style.entrance_step <= 0.0f || calm)
            return tween::cubic_out(age_ / 0.2f);
        return tween::stagger(age_, index, style.entrance_step, 0.36f);
    };
    // The chosen card is drawn last: grown, it overlaps its neighbours.
    for (int i = 0; i < n; ++i)
    {
        if (i != focus_)
            draw_card(canvas, i, entrance(i));
    }
    draw_card(canvas, focus_, entrance(focus_));

    HighlightStyle ring;
    ring.kind = HighlightKind::ring;
    ring.radius = theme.radius_card;
    ring.grow = -1.5f * press_.value;
    highlight_.draw(canvas, style, ring, entrance(focus_) * (active_ ? 1.0f : 0.4f));

    list.pop_transform();
    if (overflow)
        list.pop_clip();
}

} // namespace hui::ui
