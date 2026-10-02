// ps5-homebrew-ui - Component: FocusGroup.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/focus_group.hpp"

#include <algorithm>
#include <cmath>

namespace hui::ui
{

using gfx::Rect;

namespace
{

bool intersects(const Rect &a, const Rect &b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

int *override_of(FocusItem &item, Direction direction)
{
    switch (direction)
    {
    case Direction::up:
        return &item.up;
    case Direction::down:
        return &item.down;
    case Direction::left:
        return &item.left;
    case Direction::right:
        return &item.right;
    case Direction::none:
        break;
    }
    return nullptr;
}

int override_of(const FocusItem &item, Direction direction)
{
    switch (direction)
    {
    case Direction::up:
        return item.up;
    case Direction::down:
        return item.down;
    case Direction::left:
        return item.left;
    case Direction::right:
        return item.right;
    case Direction::none:
        break;
    }
    return kNeighbourAuto;
}

} // namespace

// ---- items -------------------------------------------------------------------

int FocusGroup::find(int id) const
{
    for (std::size_t i = 0; i < items_.size(); ++i)
    {
        if (items_[i].id == id)
            return static_cast<int>(i);
    }
    return -1;
}

bool FocusGroup::navigable(const FocusItem &item) const
{
    return item.enabled && (layers_.empty() || item.scope == layers_.back().scope);
}

int FocusGroup::first_navigable(int scope, bool any_scope) const
{
    for (const FocusItem &item : items_)
    {
        if (navigable(item) && (any_scope || item.scope == scope))
            return item.id;
    }
    return -1;
}

void FocusGroup::add(const FocusItem &item)
{
    const int at = find(item.id);
    if (at >= 0)
        items_[static_cast<std::size_t>(at)] = item;
    else
        items_.push_back(item);
    if (focus_ < 0 && navigable(item))
        focus_on(item.id, true);
}

void FocusGroup::remove(int id)
{
    const int at = find(id);
    if (at >= 0)
        items_.erase(items_.begin() + at);
}

void FocusGroup::clear()
{
    items_.clear();
}

void FocusGroup::set_rect(int id, const Rect &rect)
{
    const int at = find(id);
    if (at >= 0)
        items_[static_cast<std::size_t>(at)].rect = rect;
}

void FocusGroup::set_clip(int id, const Rect &clip)
{
    const int at = find(id);
    if (at >= 0)
        items_[static_cast<std::size_t>(at)].clip = clip;
}

void FocusGroup::set_enabled(int id, bool enabled)
{
    const int at = find(id);
    if (at >= 0)
        items_[static_cast<std::size_t>(at)].enabled = enabled;
}

void FocusGroup::set_radius(int id, float radius)
{
    const int at = find(id);
    if (at >= 0)
        items_[static_cast<std::size_t>(at)].radius = radius;
}

void FocusGroup::set_neighbour(int id, Direction direction, int target)
{
    const int at = find(id);
    if (at < 0)
        return;
    if (int *slot = override_of(items_[static_cast<std::size_t>(at)], direction))
        *slot = target;
}

const FocusItem *FocusGroup::item(int id) const
{
    const int at = find(id);
    return at >= 0 ? &items_[static_cast<std::size_t>(at)] : nullptr;
}

// ---- focus -------------------------------------------------------------------

int FocusGroup::focus() const
{
    return find(focus_) >= 0 ? focus_ : -1;
}

int FocusGroup::focus_scope() const
{
    const FocusItem *focused = item(focus_);
    return focused != nullptr ? focused->scope : 0;
}

int FocusGroup::remembered(int scope) const
{
    for (const Memory &entry : memory_)
    {
        if (entry.scope == scope)
            return entry.id;
    }
    return -1;
}

void FocusGroup::remember(const FocusItem &item)
{
    for (Memory &entry : memory_)
    {
        if (entry.scope == item.scope)
        {
            entry.id = item.id;
            return;
        }
    }
    memory_.push_back({item.scope, item.id});
}

void FocusGroup::forget(int scope)
{
    memory_.erase(std::remove_if(memory_.begin(), memory_.end(),
                                 [scope](const Memory &entry) { return entry.scope == scope; }),
                  memory_.end());
}

void FocusGroup::focus_on(int id, bool snap)
{
    focus_ = id;
    if (const FocusItem *focused = item(id))
        remember(*focused);
    if (snap)
        placed_ = false;
}

void FocusGroup::set_focus(int id, bool snap)
{
    if (find(id) < 0)
        return;
    focus_on(id, snap);
}

float FocusGroup::cost(const Rect &from, const Rect &to, Direction direction,
                       const FocusGroupStyle &style)
{
    // Every direction is turned into "ahead": near and far are the edges
    // along the way, low and high the extent across it.
    float from_near = 0.0f, from_far = 0.0f, to_near = 0.0f, to_far = 0.0f;
    float from_low = 0.0f, from_high = 0.0f, to_low = 0.0f, to_high = 0.0f;
    const bool across = direction == Direction::left || direction == Direction::right;
    if (direction == Direction::none)
        return -1.0f;
    if (across)
    {
        from_low = from.y;
        from_high = from.y + from.h;
        to_low = to.y;
        to_high = to.y + to.h;
    }
    else
    {
        from_low = from.x;
        from_high = from.x + from.w;
        to_low = to.x;
        to_high = to.x + to.w;
    }
    switch (direction)
    {
    case Direction::right:
        from_near = from.x;
        from_far = from.x + from.w;
        to_near = to.x;
        to_far = to.x + to.w;
        break;
    case Direction::left:
        from_near = -(from.x + from.w);
        from_far = -from.x;
        to_near = -(to.x + to.w);
        to_far = -to.x;
        break;
    case Direction::down:
        from_near = from.y;
        from_far = from.y + from.h;
        to_near = to.y;
        to_far = to.y + to.h;
        break;
    default:
        from_near = -(from.y + from.h);
        from_far = -from.y;
        to_near = -(to.y + to.h);
        to_far = -to.y;
        break;
    }
    // Further that way with both edges, or it is not "that way" at all (an
    // item wider than this one in the row below is not "to the right").
    constexpr float kSlack = 0.5f;
    if (to_near <= from_near + kSlack || to_far <= from_far + kSlack)
        return -1.0f;

    const float along = std::max(0.0f, to_near - from_far);
    const float shared = std::min(from_high, to_high) - std::max(from_low, to_low);
    const float smaller = std::min(from_high - from_low, to_high - to_low);
    const float overlap =
        smaller > 0.0f ? tween::clamp01(shared / smaller) : (shared >= 0.0f ? 1.0f : 0.0f);
    const float side = std::max(0.0f, -shared);
    return along + style.misalign * (1.0f - overlap) + style.side_weight * side +
           (overlap > 0.0f ? 0.0f : style.beam_bias);
}

int FocusGroup::best_from(const Rect &from, Direction direction, int skip) const
{
    int best = -1;
    float lowest = 0.0f;
    for (const FocusItem &candidate : items_)
    {
        if (candidate.id == skip || !navigable(candidate))
            continue;
        const float value = cost(from, candidate.rect, direction, style);
        if (value < 0.0f)
            continue;
        if (best < 0 || value < lowest)
        {
            best = candidate.id;
            lowest = value;
        }
    }
    return best;
}

int FocusGroup::neighbour(Direction direction) const
{
    return resolve(direction, style.wrap);
}

int FocusGroup::resolve(Direction direction, bool wrap) const
{
    const FocusItem *focused = item(focus_);
    if (focused == nullptr || direction == Direction::none)
        return -1;
    const int fixed = override_of(*focused, direction);
    if (fixed == kNeighbourNone)
        return -1;
    if (fixed >= 0)
    {
        const FocusItem *target = item(fixed);
        if (target != nullptr && navigable(*target))
            return fixed;
    }

    int best = best_from(focused->rect, direction, focused->id);
    if (best < 0 && wrap)
    {
        // Wrapping is the same search from a place just before every item:
        // the far end of the same row or column is then the nearest ahead.
        float left = focused->rect.x, top = focused->rect.y;
        float right = focused->rect.x + focused->rect.w;
        float bottom = focused->rect.y + focused->rect.h;
        for (const FocusItem &other : items_)
        {
            if (!navigable(other))
                continue;
            left = std::min(left, other.rect.x);
            top = std::min(top, other.rect.y);
            right = std::max(right, other.rect.x + other.rect.w);
            bottom = std::max(bottom, other.rect.y + other.rect.h);
        }
        Rect from = focused->rect;
        if (direction == Direction::right)
            from.x = left - from.w - 1.0f;
        else if (direction == Direction::left)
            from.x = right + 1.0f;
        else if (direction == Direction::down)
            from.y = top - from.h - 1.0f;
        else
            from.y = bottom + 1.0f;
        best = best_from(from, direction, focused->id);
    }
    if (best >= 0 && style.remember)
    {
        const FocusItem *target = item(best);
        if (target != nullptr && target->scope != focused->scope)
        {
            const FocusItem *last = item(remembered(target->scope));
            if (last != nullptr && navigable(*last))
                best = last->id;
        }
    }
    return best;
}

float FocusGroup::focus_amount(int id) const
{
    const FocusItem *target = item(id);
    if (target == nullptr)
        return 0.0f;
    const float active = tween::clamp01(active_amount_.value);
    if (!placed_)
        return id == focus_ ? active : 0.0f;
    const Rect &r = target->rect;
    return highlight_.coverage({r.x - origin_x_, r.y - origin_y_, r.w, r.h}) * active;
}

Rect FocusGroup::highlight_rect(float time) const
{
    if (!placed_)
    {
        const FocusItem *focused = item(focus_);
        return focused != nullptr ? focused->rect : Rect{};
    }
    const Rect r = highlight_.rect(time);
    return {r.x + origin_x_, r.y + origin_y_, r.w, r.h};
}

// ---- scopes ------------------------------------------------------------------

void FocusGroup::push_scope(int scope, int focus_id, bool snap)
{
    layers_.push_back({scope, focus_});
    const FocusItem *wanted = item(focus_id);
    int target = wanted != nullptr && navigable(*wanted) ? focus_id : -1;
    if (target < 0)
    {
        const FocusItem *last = item(remembered(scope));
        target = last != nullptr && navigable(*last) ? last->id : first_navigable(scope, false);
    }
    if (target >= 0)
        focus_on(target, snap);
}

void FocusGroup::pop_scope(bool snap)
{
    if (layers_.empty())
        return;
    const int previous = layers_.back().previous;
    layers_.pop_back();
    const FocusItem *before = item(previous);
    if (before != nullptr && navigable(*before))
        focus_on(previous, snap);
    else if (const int first = first_navigable(0, true); first >= 0)
        focus_on(first, snap);
}

// ---- the five rules ----------------------------------------------------------

Event FocusGroup::handle(const InputFrame &input, Feedback &feedback)
{
    exit_ = Direction::none;
    const FocusItem *focused = item(focus_);
    if (focused == nullptr || !navigable(*focused))
    {
        // Nothing to move from (an empty modal layer, say). Back still means
        // back, so such a layer can be left.
        if (input.is_pressed(Action::back))
        {
            play_cue(feedback, style, style.sounds.cancel);
            return Event::cancelled;
        }
        return Event::none;
    }
    const float x = focused->rect.cx();

    if (input.nav != Direction::none)
    {
        // A held direction stops at the end instead of running round and round.
        const int target = resolve(input.nav, style.wrap && !input.nav_repeat);
        if (target < 0)
        {
            if (style.exits.allows(input.nav))
            {
                exit_ = input.nav;
                return Event::none;
            }
            return refuse(feedback, style, input, highlight_.refusal(), x);
        }
        focus_on(target, false);
        const Rect &to = item(target)->rect;
        const float pitch =
            style.pitch_by_height
                ? tween::lerp(1.05f, 0.95f, tween::clamp01(to.cy() / gfx::kVirtualHeight))
                : 1.0f;
        play_cue(feedback, style, style.sounds.move, to.cx(), pitch);
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

void FocusGroup::update(float dt)
{
    // The focused item went away or was disabled: the nearest one that can
    // take the focus does, so the screen is never left without one.
    const FocusItem *focused = item(focus_);
    if (!items_.empty() && (focused == nullptr || !navigable(*focused)))
    {
        int nearest = -1;
        float least = 0.0f;
        for (const FocusItem &other : items_)
        {
            if (!navigable(other))
                continue;
            const float dx = other.rect.cx() - tracked_rect_.cx();
            const float dy = other.rect.cy() - tracked_rect_.cy();
            const float distance = dx * dx + dy * dy;
            if (nearest < 0 || distance < least)
            {
                nearest = other.id;
                least = distance;
            }
        }
        if (nearest >= 0)
            focus_on(nearest, tracked_ < 0);
        focused = item(focus_);
        if (focused != nullptr && !navigable(*focused))
            focused = nullptr;
    }

    active_amount_.target = active_ ? 1.0f : 0.0f;
    active_amount_.update(dt, 18.0f);
    press_.update(dt, 10.0f);

    if (focused != nullptr)
    {
        const Rect &r = focused->rect;
        if (!placed_)
        {
            origin_x_ = 0.0f;
            origin_y_ = 0.0f;
            highlight_.snap(r);
            placed_ = true;
        }
        else if (tracked_ == focus_)
        {
            // The same item as last frame, somewhere else: it scrolled or
            // animated, and the highlight's whole space moves with it.
            origin_x_ += r.x - tracked_rect_.x;
            origin_y_ += r.y - tracked_rect_.y;
        }
        tracked_ = focus_;
        tracked_rect_ = r;
        highlight_.target({r.x - origin_x_, r.y - origin_y_, r.w, r.h});
    }
    highlight_.update(dt, style);
}

void FocusGroup::draw(Canvas &canvas) const
{
    const FocusItem *focused = item(focus_);
    if (focused == nullptr)
        return;
    const float amount = tween::lerp(style.inactive, 1.0f, tween::clamp01(active_amount_.value));
    if (amount <= 0.001f)
        return;
    HighlightStyle look = style.highlight;
    if (focused->radius >= 0.0f)
        look.radius = focused->radius;
    look.grow -= style.press * press_.value; // a press pushes it in for a moment

    // The clip of the item applies once the highlight has reached it: on its
    // way in from another region it must not be cut by a clip it is not in.
    const bool clipped =
        focused->clip.w > 0.0f && intersects(highlight_rect(canvas.time), focused->clip);
    if (clipped)
        canvas.list.push_clip(focused->clip.inset(-style.clip_bleed));
    if (look.kind == HighlightKind::ring && style.theme.style == SurfaceStyle::bevel &&
        focused->picture)
    {
        // The dotted rectangle belongs inside a control; on a picture it is
        // lost. Old desktops put the selection colour around the chosen icon
        // and the dotted rectangle around that.
        const Rect r = highlight_rect(canvas.time).inset(-look.grow);
        Painter paint(canvas.list, canvas.fonts, style.theme, canvas.glass);
        const gfx::Color block = style.theme.primary.with_alpha(amount);
        canvas.list.bordered_rect(r.inset(-6.0f), 0.0f, block.with_alpha(0.0f), 6.0f, block);
        paint.focus_ring(r.inset(-17.0f), 0.0f, amount);
    }
    else if (placed_)
    {
        canvas.list.push_transform(1.0f, 0.0f, 0.0f, origin_x_, origin_y_);
        highlight_.draw(canvas, style, look, amount);
        canvas.list.pop_transform();
    }
    else
    {
        // Drawn before the first update(): there is no glide to show yet.
        Highlight still;
        still.snap(focused->rect);
        still.draw(canvas, style, look, amount);
    }
    if (clipped)
        canvas.list.pop_clip();
}

} // namespace hui::ui
