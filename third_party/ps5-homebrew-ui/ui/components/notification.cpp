// ps5-homebrew-ui - Component: NotificationStack.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/notification.hpp"

#include "ui/components/action_glyph.hpp"
#include "ui/components/button.hpp"

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

constexpr float kTitleLine = 1.22f;  // line height of the title, in title sizes
constexpr float kBodyLine = 1.36f;   // ... and of the body, in body sizes
constexpr float kBarInset = 10.0f;   // the accent bar floats this far inside the panel
constexpr float kGlyphGap = 10.0f;   // between a button's label and its shortcut glyph
constexpr float kSwapSeconds = 0.3f; // old content out, new content in
constexpr int kMaxActions = 2;

bool on_top(ToastAnchor anchor)
{
    return anchor == ToastAnchor::top_left || anchor == ToastAnchor::top_center ||
           anchor == ToastAnchor::top_right;
}

// -1 left, 0 centre, 1 right.
int side(ToastAnchor anchor)
{
    if (anchor == ToastAnchor::top_left || anchor == ToastAnchor::bottom_left)
        return -1;
    if (anchor == ToastAnchor::top_right || anchor == ToastAnchor::bottom_right)
        return 1;
    return 0;
}

float pitch_for(StatusKind kind)
{
    switch (kind)
    {
    case StatusKind::success:
        return 1.06f;
    case StatusKind::warning:
        return 0.96f;
    case StatusKind::danger:
        return 0.9f;
    default:
        return 1.0f;
    }
}

Status status_of(StatusKind kind)
{
    switch (kind)
    {
    case StatusKind::success:
        return Status::success;
    case StatusKind::warning:
        return Status::warning;
    case StatusKind::danger:
        return Status::danger;
    default:
        return Status::primary;
    }
}

bool framed(const Theme &theme)
{
    return theme.style == SurfaceStyle::hard || theme.style == SurfaceStyle::bevel ||
           theme.style == SurfaceStyle::pixel || theme.style == SurfaceStyle::sketch;
}

Rect moved(const Rect &r, float x, float y)
{
    return {r.x + x, r.y + y, r.w, r.h};
}

// How much of `item` the ring covers, 0..1: a control's own focus amount
// without a spring per control.
float coverage(const Rect &ring, const Rect &item)
{
    const float dx = std::fabs(ring.x - item.x) / std::max(item.w, 1.0f);
    const float dy = std::fabs(ring.y - item.y) / std::max(item.h, 1.0f);
    return tween::clamp01(1.0f - std::max(dx, dy));
}

} // namespace

// ---- content -----------------------------------------------------------------

int NotificationStack::action_count(const Notification &note) const
{
    const int limit = style.look == NotificationLook::compact ? 1 : kMaxActions;
    return std::min(static_cast<int>(note.actions.size()), limit);
}

int NotificationStack::control_count(const Notification &note) const
{
    return action_count(note) + (note.closable ? 1 : 0);
}

float NotificationStack::resolve(float seconds) const
{
    return seconds == 0.0f ? style.duration : seconds;
}

NotificationStack::Entry *NotificationStack::shown(int id)
{
    for (Entry &entry : shown_)
    {
        if (entry.id == id)
            return &entry;
    }
    return nullptr;
}

const NotificationStack::Entry *NotificationStack::shown(int id) const
{
    for (const Entry &entry : shown_)
    {
        if (entry.id == id)
            return &entry;
    }
    return nullptr;
}

int NotificationStack::push(Notification notification)
{
    if (notification.actions.size() > static_cast<std::size_t>(kMaxActions))
        notification.actions.resize(static_cast<std::size_t>(kMaxActions));
    Entry entry;
    entry.id = next_id_++;
    entry.note = std::move(notification);
    queue_.push_back(std::move(entry));
    return queue_.back().id;
}

Notification *NotificationStack::editable(int id)
{
    Entry *entry = shown(id);
    if (entry != nullptr)
        return entry->leaving ? nullptr : &entry->note;
    for (Entry &waiting : queue_)
    {
        if (waiting.id == id)
            return &waiting.note;
    }
    return nullptr;
}

void NotificationStack::flash(int id)
{
    Entry *entry = shown(id);
    if (entry != nullptr)
        entry->changed.trigger();
}

bool NotificationStack::update_notification(int id, Notification notification, bool restart_timer)
{
    if (notification.actions.size() > static_cast<std::size_t>(kMaxActions))
        notification.actions.resize(static_cast<std::size_t>(kMaxActions));
    for (Entry &waiting : queue_)
    {
        if (waiting.id == id)
        {
            waiting.note = std::move(notification);
            return true;
        }
    }
    Entry *entry = shown(id);
    if (entry == nullptr)
        return false;
    if (entry->leaving)
    {
        // The answer to its own action is new content: it stays after all.
        // Anything else that is already on its way out is gone.
        if (entry->reason != CloseReason::action || entry->reported)
            return false;
        entry->leaving = false;
        entry->action = -1;
        entry->leave.snap(0.0f);
    }
    const bool had_bar = entry->note.progress >= 0.0f;
    entry->old = std::move(entry->note);
    entry->note = std::move(notification);
    entry->swap = 1.0f;
    entry->changed.trigger();
    if (restart_timer)
    {
        entry->seconds = resolve(entry->note.seconds);
        entry->left = entry->seconds;
    }
    if (entry->note.progress >= 0.0f)
        entry->bar.set_value(tween::clamp01(entry->note.progress), !had_bar);
    return true;
}

bool NotificationStack::set_title(int id, std::string title, bool pulse)
{
    Notification *note = editable(id);
    if (note == nullptr)
        return false;
    note->title = std::move(title);
    if (pulse)
        flash(id);
    return true;
}

bool NotificationStack::set_body(int id, std::string body, bool pulse)
{
    Notification *note = editable(id);
    if (note == nullptr)
        return false;
    note->body = std::move(body);
    if (pulse)
        flash(id);
    return true;
}

bool NotificationStack::set_progress(int id, float progress)
{
    Notification *note = editable(id);
    if (note == nullptr)
        return false;
    const bool had_bar = note->progress >= 0.0f;
    note->progress = progress;
    Entry *entry = shown(id);
    if (entry != nullptr && progress >= 0.0f)
        entry->bar.set_value(tween::clamp01(progress), !had_bar);
    return true;
}

bool NotificationStack::set_actions(int id, std::vector<std::string> actions, bool pulse)
{
    Notification *note = editable(id);
    if (note == nullptr)
        return false;
    if (actions.size() > static_cast<std::size_t>(kMaxActions))
        actions.resize(static_cast<std::size_t>(kMaxActions));
    note->actions = std::move(actions);
    if (pulse)
        flash(id);
    return true;
}

bool NotificationStack::set_kind(int id, StatusKind kind, bool pulse)
{
    Notification *note = editable(id);
    if (note == nullptr)
        return false;
    note->kind = kind;
    if (pulse)
        flash(id);
    return true;
}

void NotificationStack::start_leave(Entry &entry, CloseReason reason, int action)
{
    if (entry.leaving)
        return;
    entry.leaving = true;
    entry.reported = false;
    entry.reason = reason;
    entry.action = action;
}

void NotificationStack::dismiss(int id)
{
    Entry *entry = shown(id);
    if (entry != nullptr)
    {
        start_leave(*entry, CloseReason::dismissed);
        return;
    }
    for (std::size_t i = 0; i < queue_.size(); ++i)
    {
        if (queue_[i].id != id)
            continue;
        reports_.push_back({id, std::move(queue_[i].note), CloseReason::dismissed, -1});
        queue_.erase(queue_.begin() + static_cast<std::ptrdiff_t>(i));
        return;
    }
}

void NotificationStack::clear(bool now)
{
    for (Entry &waiting : queue_)
        reports_.push_back({waiting.id, std::move(waiting.note), CloseReason::dismissed, -1});
    queue_.clear();
    for (Entry &entry : shown_)
    {
        start_leave(entry, CloseReason::dismissed);
        if (now && !entry.reported)
            reports_.push_back({entry.id, std::move(entry.note), entry.reason, entry.action});
    }
    if (now)
        shown_.clear();
}

int NotificationStack::visible_count() const
{
    int count = 0;
    for (const Entry &entry : shown_)
        count += entry.leaving ? 0 : 1;
    return count;
}

const Notification *NotificationStack::find(int id) const
{
    const Entry *entry = shown(id);
    if (entry != nullptr)
        return &entry->note;
    for (const Entry &waiting : queue_)
    {
        if (waiting.id == id)
            return &waiting.note;
    }
    return nullptr;
}

int NotificationStack::newest() const
{
    for (std::size_t i = shown_.size(); i > 0; --i)
    {
        if (!shown_[i - 1].leaving)
            return shown_[i - 1].id;
    }
    return 0;
}

float NotificationStack::remaining(int id) const
{
    const Entry *entry = shown(id);
    if (entry == nullptr || entry->leaving)
        return -1.0f;
    return entry->seconds > 0.0f ? tween::clamp01(entry->left / entry->seconds) : 1.0f;
}

int NotificationStack::controls(int id) const
{
    const Entry *entry = shown(id);
    return entry == nullptr || entry->leaving ? 0 : control_count(entry->note);
}

int NotificationStack::glyph_owner() const
{
    if (style.shortcut == Action::count)
        return 0;
    for (std::size_t i = shown_.size(); i > 0; --i)
    {
        const Entry &entry = shown_[i - 1];
        if (!entry.leaving && action_count(entry.note) > 0)
            return entry.id;
    }
    return 0;
}

int NotificationStack::dismiss_owner() const
{
    if (style.dismiss_shortcut == Action::count)
        return 0;
    for (std::size_t i = shown_.size(); i > 0; --i)
    {
        const Entry &entry = shown_[i - 1];
        if (!entry.leaving && entry.note.closable)
            return entry.id;
    }
    return 0;
}

float NotificationStack::anchor_x() const
{
    const int where = side(style.anchor);
    if (where < 0)
        return bounds_.x + style.margin + style.width * 0.5f;
    if (where > 0)
        return bounds_.x + bounds_.w - style.margin - style.width * 0.5f;
    return bounds_.cx();
}

// ---- the focus ---------------------------------------------------------------

bool NotificationStack::focusable() const
{
    for (const Entry &entry : shown_)
    {
        if (!entry.leaving && control_count(entry.note) > 0)
            return true;
    }
    return false;
}

void NotificationStack::set_active(bool active)
{
    if (active == active_)
        return;
    active_ = active;
    if (!active)
        return;
    focus_id_ = 0;
    focus_control_ = 0;
    ring_set_ = false;
    for (std::size_t i = shown_.size(); i > 0; --i)
    {
        const Entry &entry = shown_[i - 1];
        if (!entry.leaving && control_count(entry.note) > 0)
        {
            focus_id_ = entry.id;
            break;
        }
    }
}

bool NotificationStack::set_focus(int id, int control)
{
    const int count = controls(id);
    if (count <= 0)
        return false;
    move_focus(id, std::clamp(control, 0, count - 1));
    return true;
}

std::vector<int> NotificationStack::focus_order() const
{
    std::vector<int> order;
    for (const Entry &entry : shown_)
    {
        if (!entry.leaving && control_count(entry.note) > 0)
            order.push_back(entry.id);
    }
    // shown_ runs oldest to newest. From the anchored edge the stack runs
    // newest first (or oldest first), and that edge is the top or the bottom.
    const bool newest_at_top = style.newest_first == on_top(style.anchor);
    if (newest_at_top)
        std::reverse(order.begin(), order.end());
    return order;
}

void NotificationStack::move_focus(int id, int control)
{
    if (id != focus_id_)
    {
        // The ring lives in its notification's space: carry it over to the
        // new one's so it glides across instead of jumping.
        bool carried = false;
        if (fonts_ != nullptr && ring_set_)
        {
            const Rect from = card_rect(*fonts_, focus_id_);
            const Rect to = card_rect(*fonts_, id);
            if (from.w > 0.0f && to.w > 0.0f)
            {
                ring_.x.value += from.x - to.x;
                ring_.x.target += from.x - to.x;
                ring_.y.value += from.y - to.y;
                ring_.y.target += from.y - to.y;
                carried = true;
            }
        }
        if (!carried)
            ring_set_ = false;
    }
    focus_id_ = id;
    focus_control_ = control;
}

void NotificationStack::ensure_focus()
{
    if (!active_)
        return;
    const Entry *entry = shown(focus_id_);
    if (entry != nullptr && !entry->leaving)
    {
        const int count = control_count(entry->note);
        if (count > 0)
        {
            focus_control_ = std::clamp(focus_control_, 0, count - 1);
            return;
        }
    }
    // The focused one left, or lost its controls: a neighbour takes over,
    // first the one that will slide into its place.
    const auto takes = [this](std::ptrdiff_t index)
    {
        if (index < 0 || index >= static_cast<std::ptrdiff_t>(shown_.size()))
            return false;
        const Entry &other = shown_[static_cast<std::size_t>(index)];
        return !other.leaving && control_count(other.note) > 0;
    };
    const std::ptrdiff_t count = static_cast<std::ptrdiff_t>(shown_.size());
    std::ptrdiff_t found = -1;
    if (entry != nullptr)
    {
        const std::ptrdiff_t at = entry - shown_.data();
        const std::ptrdiff_t away = style.newest_first ? -1 : 1; // from the anchored edge
        for (std::ptrdiff_t reach = 1; reach < count && found < 0; ++reach)
        {
            if (takes(at + away * reach))
                found = at + away * reach;
            else if (takes(at - away * reach))
                found = at - away * reach;
        }
    }
    else
    {
        for (std::ptrdiff_t i = count - 1; i >= 0 && found < 0; --i)
        {
            if (takes(i))
                found = i;
        }
    }
    if (found < 0)
    {
        focus_id_ = 0;
        focus_control_ = 0;
        return;
    }
    move_focus(shown_[static_cast<std::size_t>(found)].id, 0);
}

Event NotificationStack::fire(Entry &entry, int action, Feedback &feedback)
{
    const int id = entry.id;
    event_id_ = id;
    event_action_ = action;
    entry.pressed = action;
    entry.press.trigger();
    play_cue(feedback, style, style.sounds.activate, anchor_x());
    if (style.sounds.rumble > 0.0f)
        feedback.rumble(0.3f * style.sounds.rumble, 0.04f);
    if (style.close_on_action)
        start_leave(entry, CloseReason::action, action);
    // The slot may push, update or clear: `entry` is not used after it.
    if (on_action)
        on_action(id, action);
    return Event::activated;
}

Event NotificationStack::handle(const InputFrame &input, Feedback &feedback)
{
    event_id_ = 0;
    event_action_ = -1;
    if (!active_)
        return Event::none;
    ensure_focus();
    Entry *entry = shown(focus_id_);
    if (entry == nullptr || entry->leaving)
    {
        // Nothing is left to focus: the screen has the focus again.
        active_ = false;
        return Event::cancelled;
    }
    const float x = anchor_x();
    const int count = control_count(entry->note);
    const int actions = action_count(entry->note);

    if (input.nav == Direction::left || input.nav == Direction::right)
    {
        const int next = focus_control_ + (input.nav == Direction::right ? 1 : -1);
        if (next < 0 || next >= count)
            return refuse(feedback, style, input, refusal_, x);
        focus_control_ = next;
        play_cue(feedback, style, style.sounds.move, x);
        return Event::moved;
    }
    if (input.nav == Direction::up || input.nav == Direction::down)
    {
        const std::vector<int> order = focus_order();
        const auto here = std::find(order.begin(), order.end(), focus_id_);
        const std::ptrdiff_t at = here - order.begin();
        const std::ptrdiff_t next = at + (input.nav == Direction::down ? 1 : -1);
        if (here == order.end() || next < 0 || next >= static_cast<std::ptrdiff_t>(order.size()))
            return refuse(feedback, style, input, refusal_, x);
        const int target = order[static_cast<std::size_t>(next)];
        // From a close control the focus goes to the next close control, so
        // several can be closed in a row; otherwise to the main control.
        const Entry *other = shown(target);
        const bool on_close_control = focus_control_ >= actions;
        const int control =
            on_close_control && other->note.closable ? control_count(other->note) - 1 : 0;
        move_focus(target, control);
        const float along = order.size() > 1
                                ? static_cast<float>(next) / static_cast<float>(order.size() - 1)
                                : 0.0f;
        play_cue(feedback, style, style.sounds.move, x, tween::lerp(1.05f, 0.95f, along));
        return Event::moved;
    }
    if (input.is_pressed(Action::confirm))
    {
        if (focus_control_ < actions)
            return fire(*entry, focus_control_, feedback);
        entry->pressed = focus_control_;
        entry->press.trigger();
        start_leave(*entry, CloseReason::closed);
        event_id_ = entry->id;
        play_cue(feedback, style, style.sounds.close, x);
        return Event::changed;
    }
    if (input.is_pressed(Action::back))
    {
        active_ = false;
        play_cue(feedback, style, style.sounds.cancel, x);
        return Event::cancelled;
    }
    return Event::none;
}

Event NotificationStack::handle_shortcut(const InputFrame &input, Feedback &feedback)
{
    event_id_ = 0;
    event_action_ = -1;
    if (style.shortcut != Action::count && input.is_pressed(style.shortcut))
    {
        Entry *entry = shown(glyph_owner());
        if (entry != nullptr)
            return fire(*entry, 0, feedback);
    }
    if (style.dismiss_shortcut != Action::count && input.is_pressed(style.dismiss_shortcut))
    {
        Entry *entry = shown(dismiss_owner());
        if (entry != nullptr)
        {
            entry->pressed = control_count(entry->note) - 1;
            entry->press.trigger();
            start_leave(*entry, CloseReason::closed);
            event_id_ = entry->id;
            play_cue(feedback, style, style.sounds.close, anchor_x());
            return Event::changed;
        }
    }
    return Event::none;
}

// ---- time --------------------------------------------------------------------

// Whether the stack still fits its bounds with one more. A stack that is
// empty always takes one, however tall, and before the first draw nothing
// can be measured.
bool NotificationStack::fits(const Entry &next) const
{
    if (!style.fit_bounds || fonts_ == nullptr || visible_count() == 0)
        return true;
    float total = lay(*fonts_, next.note, resolve(next.note.seconds) > 0.0f, false, false).height;
    for (const Entry &entry : shown_)
    {
        if (!entry.leaving)
            total += lay(*fonts_, entry).height + style.gap;
    }
    // The marker needs its line too while others keep waiting.
    if (queue_.size() > 1 && style.more_marker)
        total += style.gap + (style.meta_size + 2.0f) * 1.9f;
    return total <= bounds_.h - 2.0f * style.margin;
}

void NotificationStack::style_bar(ProgressBar &bar, const Notification &note) const
{
    ProgressBarStyle &look = bar.style;
    look.theme = style.theme;
    look.reduced_motion = style.reduced_motion;
    look.height = style.progress_height;
    look.text_size = style.meta_size;
    look.status = status_of(note.kind);
    look.percent = style.progress_percent && style.look != NotificationLook::compact;
    look.placement = look.percent ? LabelPlacement::right : LabelPlacement::none;
    bar.label.clear();
}

void NotificationStack::update(float dt, Feedback &feedback)
{
    const float omega = style.omega();
    // A notification may overshoot its place a little, never wobble for long.
    const float damping = std::max(style.damping(), 0.62f);
    const bool paused = active_ && style.pause_when_active;
    for (Entry &entry : shown_)
    {
        entry.enter.target = 1.0f;
        entry.enter.update(dt, omega, damping);
        if (!entry.leaving && entry.seconds > 0.0f && !paused)
        {
            entry.left -= dt;
            if (entry.left <= 0.0f)
                start_leave(entry, CloseReason::timed_out);
        }
        if (entry.leaving)
        {
            entry.leave.target = 1.0f;
            entry.leave.update(dt, omega * 1.5f);
            // Its room closes once it is mostly gone, so the others do not
            // slide under a notification that is still readable.
            if (entry.leave.value > 0.6f)
                entry.slot.target = 0.0f;
            if (!entry.reported)
            {
                entry.reported = true;
                reports_.push_back({entry.id, entry.note, entry.reason, entry.action});
            }
        }
        else
        {
            entry.slot.target = 1.0f;
        }
        entry.slot.update(dt, omega, damping);
        entry.changed.update(dt, 4.5f);
        entry.press.update(dt, 9.0f);
        entry.swap = std::max(entry.swap - dt / kSwapSeconds, 0.0f);
        style_bar(entry.bar, entry.note);
        entry.bar.update(dt);
    }
    std::erase_if(shown_,
                  [](const Entry &entry)
                  {
                      return entry.leaving && entry.leave.value > 0.99f &&
                             entry.slot.target == 0.0f && std::fabs(entry.slot.value) < 0.004f &&
                             std::fabs(entry.slot.velocity) < 0.05f;
                  });

    const int room = std::max(style.max_visible, 1);
    while (!queue_.empty() && visible_count() < room && fits(queue_.front()))
    {
        Entry entry = std::move(queue_.front());
        queue_.erase(queue_.begin());
        entry.seconds = resolve(entry.note.seconds);
        entry.left = entry.seconds;
        entry.enter.snap(0.0f);
        entry.leave.snap(0.0f);
        // It opens its own room, pushing the others along; alone, the room
        // is simply there.
        entry.slot.snap(shown_.empty() ? 1.0f : 0.0f);
        entry.slot.target = 1.0f;
        if (entry.note.progress >= 0.0f)
            entry.bar.set_value(tween::clamp01(entry.note.progress), true);
        play_cue(feedback, style, style.sounds.notify, anchor_x(),
                 style.pitch_by_kind ? pitch_for(entry.note.kind) : 1.0f);
        shown_.push_back(std::move(entry));
    }

    if (fonts_ != nullptr)
    {
        for (Entry &entry : shown_)
        {
            const float height = lay(*fonts_, entry).height;
            entry.height.target = height;
            if (!entry.sized)
                entry.height.snap(height);
            entry.sized = true;
            entry.height.update(dt, std::max(omega, 14.0f));
        }
    }

    const int waiting = queued_count();
    more_.target = waiting > 0 && style.more_marker ? 1.0f : 0.0f;
    more_.update(dt, std::max(omega, 14.0f));
    if (waiting > 0 && waiting != more_count_)
    {
        if (more_count_ != 0 && !style.reduced_motion)
            more_pop_.value = 1.16f;
        more_count_ = waiting;
    }
    more_pop_.update(dt, 20.0f, 0.5f);

    ensure_focus();
    const Entry *focused = active_ ? shown(focus_id_) : nullptr;
    active_amount_.target = focused != nullptr ? 1.0f : 0.0f;
    active_amount_.update(dt, std::max(omega, 18.0f));
    if (focused != nullptr && fonts_ != nullptr)
    {
        const Layout at = lay(*fonts_, *focused);
        if (focus_control_ < at.count)
        {
            const Rect target = at.controls[focus_control_];
            ring_.target(target);
            if (!ring_set_)
                ring_.snap(target);
            ring_set_ = true;
        }
    }
    ring_.update(dt, std::max(omega, 18.0f));
    refusal_.update(dt, 9.0f);

    // Told last: a slot may push, update or clear, and nothing above is
    // still walking the stack.
    if (!reports_.empty())
    {
        std::vector<Report> reports;
        reports.swap(reports_);
        for (const Report &report : reports)
        {
            if (on_leave)
                on_leave(report.id, report.note, report.reason, report.action);
            if (on_close)
                on_close(report.id, report.reason);
        }
    }
}

// ---- layout ------------------------------------------------------------------

NotificationStack::Layout NotificationStack::lay(const Fonts &fonts, const Entry &entry) const
{
    return lay(fonts, entry.note, entry.seconds > 0.0f, glyph_owner() == entry.id,
               dismiss_owner() == entry.id);
}

NotificationStack::Layout NotificationStack::lay(const Fonts &fonts, const Notification &note,
                                                 bool timed, bool action_glyph,
                                                 bool dismiss_glyph) const
{
    gfx::DrawList scratch;
    const Painter paint(scratch, fonts, style.theme, 0);
    Layout out;
    const float w = style.width;
    const float p = style.padding;
    const float close = style.close_size;
    const float button = style.button_height;
    const bool has_icon = static_cast<bool>(note.icon) || note.kind != StatusKind::none;
    out.actions = action_count(note);
    out.count = out.actions + (note.closable ? 1 : 0);
    if (action_glyph && out.actions > 0)
        out.action_glyph = action_glyph_width(style.shortcut, style.glyph_size, style.swap_confirm);
    if (dismiss_glyph && note.closable)
        out.dismiss_glyph =
            action_glyph_width(style.dismiss_shortcut, style.glyph_size, style.swap_confirm);
    // The cross has no body: part of its box may sit in the padding.
    const float tuck = std::min(p, close) * 0.35f;
    const auto hug = [&](const std::string &label, float glyph)
    {
        const float width = paint.label_width(label, style.button_text) +
                            2.0f * style.button_padding + (glyph > 0.0f ? glyph + kGlyphGap : 0.0f);
        return std::max(width, style.button_min_width);
    };
    // Where the time bar may start: the accent bar keeps its own column.
    float bar_left = p;
    float y = 0.0f;

    if (style.look == NotificationLook::compact)
    {
        const float pad = p * 0.6f;
        float row = std::max(style.header_icon, style.title_size * 1.3f);
        if (out.actions > 0)
            row = std::max(row, button);
        if (note.closable)
            row = std::max(row, close);
        const float cy = pad + row * 0.5f;
        float left = p;
        if (has_icon && style.header_icon > 0.0f)
        {
            out.icon = {left, cy - style.header_icon * 0.5f, style.header_icon, style.header_icon};
            left += style.header_icon + 12.0f;
        }
        float right = w - p;
        if (note.closable)
        {
            out.controls[out.count - 1] = {w - p + tuck - close, cy - close * 0.5f, close, close};
            right = out.controls[out.count - 1].x - 4.0f;
            if (out.dismiss_glyph > 0.0f)
                right -= out.dismiss_glyph + 6.0f;
        }
        if (out.actions > 0)
        {
            const float width =
                std::min(hug(note.actions[0], out.action_glyph), (right - left) * 0.6f);
            out.controls[0] = {right - width, cy - button * 0.5f, width, button};
            right -= width + 14.0f;
        }
        out.text_x = left;
        out.title = fit_label(paint, note.title, style.title_size, std::max(right - left, 40.0f));
        out.title_baseline = cy + style.title_size * 0.35f;
        y = pad + row;
        if (note.progress >= 0.0f)
        {
            y += 8.0f;
            out.progress = {p, y, w - 2.0f * p, style.progress_height};
            y += style.progress_height;
        }
        y += pad;
    }
    else
    {
        float column_x = p;
        float column_w = w - 2.0f * p;
        float text_w = column_w;
        const bool header =
            style.look == NotificationLook::card && (!note.source.empty() || !note.time.empty());
        bool leading_icon = false;
        if (header)
        {
            // The Bootstrap toast: who and when on a line of its own.
            const float icon = style.header_icon;
            const float row = std::max(icon, style.meta_size * 1.5f);
            y = p * 0.75f;
            const float cy = y + row * 0.5f;
            float left = p;
            if (has_icon && icon > 0.0f)
            {
                out.icon = {left, cy - icon * 0.5f, icon, icon};
                left += icon + 10.0f;
            }
            float right = w - p;
            if (note.closable)
            {
                out.controls[out.count - 1] = {w - p + tuck - close, cy - close * 0.5f, close,
                                               close};
                right = out.controls[out.count - 1].x - 4.0f;
                if (out.dismiss_glyph > 0.0f)
                    right -= out.dismiss_glyph + 6.0f;
            }
            if (!note.time.empty())
            {
                out.time = note.time;
                out.time_right = right;
                right -= paint.body_width(note.time, style.meta_size) + 14.0f;
            }
            out.meta =
                fit_label(paint, note.source, style.meta_size, std::max(right - left, 40.0f));
            out.meta_x = left;
            out.meta_baseline = cy + style.meta_size * 0.35f;
            y += row + p * 0.55f;
            if (style.divider)
            {
                out.divider_y = y;
                y += p * 0.6f;
            }
        }
        else
        {
            float left = p;
            if (style.look == NotificationLook::accent)
            {
                left = kBarInset + style.bar_width + std::max(p - kBarInset, 8.0f);
                bar_left = left;
            }
            y = p;
            if (has_icon && style.icon_size > 0.0f)
            {
                out.icon = {left, y, style.icon_size, style.icon_size};
                left += style.icon_size + 16.0f;
                leading_icon = true;
            }
            column_x = left;
            column_w = w - p - left;
            text_w = column_w;
            if (note.closable)
            {
                out.controls[out.count - 1] = {w - p + tuck - close, p * 0.5f, close, close};
                float right = out.controls[out.count - 1].x - 4.0f;
                if (out.dismiss_glyph > 0.0f)
                    right -= out.dismiss_glyph + 6.0f;
                text_w = right - left;
            }
            std::string meta = note.source;
            if (!note.time.empty())
                meta += (meta.empty() ? "" : "  \xC2\xB7  ") + note.time;
            if (!meta.empty())
            {
                out.meta = fit_label(paint, meta, style.meta_size, std::max(text_w, 40.0f));
                out.meta_x = left;
                out.meta_baseline = y + style.meta_size * 0.85f;
                y += style.meta_size * 1.5f;
            }
        }

        out.text_x = column_x;
        text_w = std::max(text_w, 40.0f);
        if (!note.title.empty())
        {
            out.title = fit_label(paint, note.title, style.title_size, text_w);
            out.title_baseline = y + style.title_size * 0.86f;
            y += style.title_size * kTitleLine;
        }
        if (!note.body.empty() && style.body_lines > 0)
        {
            out.body = wrap_body(paint, note.body, style.body_size, text_w, style.body_lines);
            out.body_top = y + 2.0f;
            y += 2.0f + static_cast<float>(out.body.size()) * style.body_size * kBodyLine;
        }
        if (leading_icon)
            y = std::max(y, p + style.icon_size);
        if (note.progress >= 0.0f)
        {
            const float height = style.progress_percent
                                     ? std::max(style.progress_height, style.meta_size * 1.25f)
                                     : style.progress_height;
            y += 10.0f;
            out.progress = {column_x, y, column_w, height};
            y += height;
        }
        if (out.actions > 0)
        {
            y += 14.0f;
            const float n = static_cast<float>(out.actions);
            // A hard shadow is part of a button: the next one starts after it.
            const float gap =
                style.button_gap +
                (style.theme.style == SurfaceStyle::hard ? style.theme.shadow_offset : 0.0f);
            if (style.stretch_actions)
            {
                const float share = (column_w - (n - 1.0f) * gap) / n;
                for (int i = 0; i < out.actions; ++i)
                    out.controls[i] = {column_x + static_cast<float>(i) * (share + gap), y, share,
                                       button};
            }
            else
            {
                float x = column_x;
                for (int i = 0; i < out.actions; ++i)
                {
                    const float room = column_x + column_w - x;
                    const float width = std::min(hug(note.actions[static_cast<std::size_t>(i)],
                                                     i == 0 ? out.action_glyph : 0.0f),
                                                 room);
                    out.controls[i] = {x, y, width, button};
                    x += width + gap;
                }
            }
            y += button;
        }
        y += p;
    }

    if (timed && style.time_bar && style.time_bar_height > 0.0f)
    {
        y += style.time_bar_height + p * 0.4f;
        out.time_bar = {bar_left, y - p * 0.6f - style.time_bar_height, w - p - bar_left,
                        style.time_bar_height};
    }
    out.height = y;
    return out;
}

std::vector<NotificationStack::Placed> NotificationStack::place(const Fonts &fonts,
                                                                float *end) const
{
    std::vector<Placed> out;
    const bool top = on_top(style.anchor);
    const float x = anchor_x() - style.width * 0.5f;
    // The stack is laid out from each notification's share of its room: a
    // new one opens its room with a spring, a leaving one gives it up, and
    // the rest follow.
    float cursor = 0.0f;
    const std::size_t count = shown_.size();
    out.reserve(count);
    for (std::size_t k = 0; k < count; ++k)
    {
        const std::size_t index = style.newest_first ? count - 1 - k : k;
        const Entry &entry = shown_[index];
        Placed at;
        at.index = index;
        at.layout = lay(fonts, entry);
        const float h = entry.sized ? entry.height.value : at.layout.height;
        const float y = top ? bounds_.y + style.margin + cursor
                            : bounds_.y + bounds_.h - style.margin - cursor - h;
        at.rect = {x, y, style.width, h};
        cursor += (h + style.gap) * std::max(entry.slot.value, 0.0f);
        out.push_back(std::move(at));
    }
    if (end != nullptr)
        *end = cursor;
    return out;
}

Rect NotificationStack::card_rect(const Fonts &fonts, int id) const
{
    for (const Placed &at : place(fonts))
    {
        if (shown_[at.index].id == id)
            return at.rect;
    }
    return {0.0f, 0.0f, 0.0f, 0.0f};
}

Rect NotificationStack::control_rect(const Fonts &fonts, int id, int control) const
{
    for (const Placed &at : place(fonts))
    {
        if (shown_[at.index].id != id)
            continue;
        if (control < 0 || control >= at.layout.count)
            return at.rect;
        return moved(at.layout.controls[control], at.rect.x, at.rect.y);
    }
    return {0.0f, 0.0f, 0.0f, 0.0f};
}

// ---- drawing -----------------------------------------------------------------

void NotificationStack::draw_content(Canvas &canvas, const Entry &entry, const Notification &note,
                                     const Layout &at, float x, float y, bool live) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    // Everything inside the panel is drawn without glass: it would show the
    // screen behind the notification instead of the notification.
    Canvas inside{list, canvas.fonts, 0, canvas.time};
    Painter flat(list, canvas.fonts, theme, 0);
    const Color tone = status_color(theme, note.kind);
    const bool round = theme.corner == Corner::round && theme.radius >= 2.0f;

    if (at.icon.w > 0.0f)
    {
        const Rect box = moved(at.icon, x, y);
        if (note.icon)
            note.icon(inside, box);
        else
            draw_status_icon(inside, theme, note.kind, box.cx(), box.cy(), box.w);
    }
    if (!at.meta.empty())
        flat.label(at.meta, x + at.meta_x, y + at.meta_baseline, style.meta_size, theme.text_muted);
    if (!at.time.empty())
        flat.body(at.time, x + at.time_right, y + at.meta_baseline, style.meta_size,
                  theme.text_muted, gfx::Align::right);
    if (at.divider_y >= 0.0f)
        list.rounded_rect(
            {x + style.padding, y + at.divider_y, style.width - 2.0f * style.padding, 1.5f}, 0.0f,
            theme.text_muted.with_alpha(0.22f));
    if (!at.title.empty())
        flat.label(at.title, x + at.text_x, y + at.title_baseline, style.title_size, theme.text);
    for (std::size_t i = 0; i < at.body.size(); ++i)
    {
        const float top = y + at.body_top + static_cast<float>(i) * style.body_size * kBodyLine;
        flat.body(at.body[i], x + at.text_x, top + style.body_size * 0.9f, style.body_size,
                  theme.text_muted);
    }
    if (at.progress.h > 0.0f)
    {
        // The bar's value is eased by the entry; its place is known only here.
        ProgressBar bar = entry.bar;
        style_bar(bar, note);
        bar.set_bounds(moved(at.progress, x, y));
        bar.draw(inside);
    }

    // ---- controls ----
    const bool focused = live && active_ && ring_set_ && entry.id == focus_id_;
    const float active = tween::clamp01(active_amount_.value);
    const Rect ring = ring_.value();
    const auto focus_of = [&](int index)
    { return focused ? coverage(ring, at.controls[index]) * active : 0.0f; };
    for (int i = 0; i < at.actions; ++i)
    {
        const Rect r = moved(at.controls[i], x, y);
        const float press = live && entry.pressed == i ? tween::clamp01(entry.press.value) : 0.0f;
        const float glyph = i == 0 ? at.action_glyph : 0.0f;
        const float scale = style.reduced_motion ? 1.0f : 1.0f - 0.04f * press;
        list.push_transform(scale, r.cx(), r.cy(), 0.0f, 0.0f);
        Look look;
        look.press = press;
        const ButtonFace face =
            draw_button_face(inside, theme, r, i == 0 ? ButtonRole::primary : ButtonRole::secondary,
                             look, -1.0f, false);
        const float room = r.w - 16.0f - (glyph > 0.0f ? glyph + kGlyphGap : 0.0f);
        const std::string label = fit_label(flat, note.actions[static_cast<std::size_t>(i)],
                                            style.button_text, std::max(room, 20.0f));
        const float label_w = flat.label_width(label, style.button_text);
        const float total = label_w + (glyph > 0.0f ? glyph + kGlyphGap : 0.0f);
        const float left = face.content.cx() - total * 0.5f;
        flat.label(label, left, face.content.cy() + style.button_text * 0.35f, style.button_text,
                   face.ink);
        if (glyph > 0.0f)
            draw_button(list, canvas.fonts, glyph_style_on(face, true),
                        button_for(style.shortcut, style.swap_confirm), left + label_w + kGlyphGap,
                        face.content.cy(), style.glyph_size);
        list.pop_transform();
    }
    if (note.closable && at.count > 0)
    {
        const int index = at.count - 1;
        const Rect r = moved(at.controls[index], x, y);
        const float focus = focus_of(index);
        const float press =
            live && entry.pressed == index ? tween::clamp01(entry.press.value) : 0.0f;
        flat.fill(r, flat.control_radius(r), theme.text.with_alpha(0.1f * focus + 0.12f * press));
        const Color cross = gfx::mix(theme.text_muted, theme.text, focus);
        const float reach = r.w * 0.16f;
        list.line(r.cx() - reach, r.cy() - reach, r.cx() + reach, r.cy() + reach, 2.5f, cross);
        list.line(r.cx() + reach, r.cy() - reach, r.cx() - reach, r.cy() + reach, 2.5f, cross);
        if (at.dismiss_glyph > 0.0f)
            draw_action_glyph(inside, theme, style.dismiss_shortcut, r.x - at.dismiss_glyph - 2.0f,
                              r.cy(), style.glyph_size, style.swap_confirm);
    }

    if (at.time_bar.h > 0.0f && entry.seconds > 0.0f)
    {
        const Rect track = moved(at.time_bar, x, y);
        const float fraction = tween::clamp01(entry.left / entry.seconds);
        const float radius = round ? track.h * 0.5f : 0.0f;
        // Held still by the focus, the bar dims: "the clock is not running".
        const bool paused = active_ && style.pause_when_active;
        list.rounded_rect(track, radius, theme.text_muted.with_alpha(0.2f));
        if (fraction > 0.0f)
            list.rounded_rect({track.x, track.y, std::max(track.w * fraction, track.h), track.h},
                              radius, tone.with_alpha(paused ? 0.55f : 1.0f));
    }
}

void NotificationStack::draw_entry(Canvas &canvas, const Entry &entry, const Placed &at) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    const Rect &r = at.rect;
    const bool top = on_top(style.anchor);
    const int where = side(style.anchor);
    const bool still = style.reduced_motion;

    const float in = entry.enter.value;
    const float out = tween::clamp01(entry.leave.value);
    const float opacity = tween::clamp01(in * 1.6f) * (1.0f - out);
    if (opacity <= 0.003f)
        return;
    float dx = 0.0f;
    float dy = 0.0f;
    if (!still)
    {
        // Corners slide in from their side; the centre drops in (or rises).
        if (where != 0)
            dx = static_cast<float>(where) * ((1.0f - in) * (style.width + style.margin) +
                                              tween::cubic_in(out) * style.width * 0.35f);
        else
            dy = (top ? -1.0f : 1.0f) *
                 ((1.0f - in) * (r.h + style.margin) + tween::cubic_in(out) * 36.0f);
    }
    const float changed = tween::clamp01(entry.changed.value);
    const float scale = still ? 1.0f : 1.0f + 0.018f * changed;
    list.push_opacity(opacity);
    list.push_transform(scale, r.cx(), r.cy(), dx, dy);

    Painter flat(list, canvas.fonts, theme, 0);
    const Color tone = status_color(theme, entry.note.kind);
    const float radius = std::min(theme.radius_card, std::min(r.w, r.h) * 0.5f);
    // "This one changed": light in its status colour, outside the panel only.
    if (changed > 0.01f)
        flat.halo(r, theme.corner == Corner::round ? radius : 0.0f, 18.0f,
                  tone.with_alpha(0.6f * changed));
    draw_overlay_panel(canvas, theme, r, style.frosted, style.frost);
    if (style.look == NotificationLook::accent)
    {
        // The wash stays inside the theme's own outline.
        const float edge = framed(theme) ? theme.border : 0.0f;
        flat.fill(r.inset(edge), std::max(radius - edge, 0.0f), tone.with_alpha(style.tint));
        const bool round = theme.corner == Corner::round && theme.radius >= 2.0f;
        // A cut corner would slice the bar's ends: it stops short of the cut.
        const float inset =
            theme.corner == Corner::chamfer ? std::max(kBarInset, radius * 0.8f) : kBarInset;
        flat.fill({r.x + kBarInset, r.y + inset, style.bar_width, r.h - 2.0f * inset},
                  round ? style.bar_width * 0.5f : 0.0f, tone);
    }

    if (entry.swap > 0.0f)
    {
        // The old content leaves in the first half, the new arrives in the
        // second, while the panel's height eases between the two.
        const float t = 1.0f - entry.swap;
        const Layout before = lay(canvas.fonts, entry.old, entry.seconds > 0.0f, false, false);
        list.push_opacity(tween::clamp01(1.0f - 2.0f * t));
        draw_content(canvas, entry, entry.old, before, r.x, r.y, false);
        list.pop_opacity();
        list.push_opacity(tween::clamp01(2.0f * t - 1.0f));
        draw_content(canvas, entry, entry.note, at.layout, r.x, r.y, true);
        list.pop_opacity();
    }
    else
    {
        draw_content(canvas, entry, entry.note, at.layout, r.x, r.y, true);
    }

    const float active = tween::clamp01(active_amount_.value);
    if (active_ && ring_set_ && entry.id == focus_id_ && active > 0.01f)
    {
        Canvas inside{list, canvas.fonts, 0, canvas.time};
        Rect ring = moved(ring_.value(), r.x, r.y);
        ring.x += shake(refusal_.value, canvas.time, 8.0f);
        // The ring of the control it rests on: the cross has no body.
        const int actions = action_count(entry.note);
        ButtonRole role = ButtonRole::secondary;
        if (focus_control_ >= actions)
            role = ButtonRole::ghost;
        else if (focus_control_ == 0)
            role = ButtonRole::primary;
        draw_button_ring(inside, theme, ring, role, active, -1.0f, false);
    }

    list.pop_transform();
    list.pop_opacity();
}

void NotificationStack::draw(Canvas &canvas) const
{
    fonts_ = &canvas.fonts;
    const float more = tween::clamp01(more_.value);
    if (shown_.empty() && more <= 0.01f)
        return;
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    float end = 0.0f;
    const std::vector<Placed> placed = place(canvas.fonts, &end);

    if (more > 0.01f && more_count_ > 0)
    {
        Painter flat(list, canvas.fonts, theme, 0);
        char number[16];
        std::snprintf(number, sizeof(number), "%d", more_count_);
        const std::string text = style.more_prefix + number + style.more_suffix;
        const float size = style.meta_size + 2.0f;
        const float w = flat.label_width(text, size) + 30.0f;
        const float h = size * 1.9f;
        const int where = side(style.anchor);
        const float centre = anchor_x();
        float x = centre - w * 0.5f;
        if (where < 0)
            x = centre - style.width * 0.5f;
        else if (where > 0)
            x = centre + style.width * 0.5f - w;
        const float y = on_top(style.anchor) ? bounds_.y + style.margin + end
                                             : bounds_.y + bounds_.h - style.margin - end - h;
        const Rect pill{x, y, w, h};
        const float scale =
            std::max(more_pop_.value, 0.0f) * (style.reduced_motion ? 1.0f : 0.85f + 0.15f * more);
        list.push_opacity(more);
        list.push_transform(scale, pill.cx(), pill.cy(), 0.0f, 0.0f);
        const float radius = theme.pill_chips ? h * 0.5f : std::min(theme.radius, h * 0.5f);
        const Color base = opaque_over(opaque_over(theme.page, theme.surface), theme.surface_high);
        flat.fill(pill, radius, base);
        flat.stroke(pill, radius, std::clamp(theme.border, 1.5f, 3.0f), theme.outline);
        flat.label(text, pill.cx(), pill.cy() + size * 0.35f, size, theme.text, gfx::Align::center);
        list.pop_transform();
        list.pop_opacity();
    }

    // Oldest first, so a notification sliding in passes over the others.
    for (std::size_t k = 0; k < placed.size(); ++k)
    {
        const Placed &at = style.newest_first ? placed[placed.size() - 1 - k] : placed[k];
        draw_entry(canvas, shown_[at.index], at);
    }
}

} // namespace hui::ui
