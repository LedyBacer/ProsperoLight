// ps5-homebrew-ui - Component: NotificationCenter.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/notification_center.hpp"

#include "ui/components/button.hpp"
#include "ui/components/notification_bell.hpp"

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

constexpr float kStripGap = 10.0f; // between the summary line and the list
constexpr float kIconGap = 14.0f;  // between a row's icon and its text

} // namespace

NotificationCenter::NotificationCenter()
{
    list_.content = [this](Canvas &canvas, const Rect &row, const ListItem &item, int, float focus)
    {
        if (item.tag >= 0 && item.tag < count())
            draw_row(canvas, row, records_[static_cast<std::size_t>(item.tag)], focus);
    };
    sync();
}

void NotificationCenter::attach(NotificationStack &stack)
{
    stack.on_leave = [this](int id, const Notification &notification, CloseReason reason,
                            int action) { record(notification, reason, id, action); };
}

void NotificationCenter::detach(NotificationStack &stack)
{
    stack.on_leave = nullptr;
}

void NotificationCenter::record(Notification notification, CloseReason reason, int id, int action)
{
    NotificationRecord entry;
    entry.id = id;
    entry.notification = std::move(notification);
    entry.reason = reason;
    entry.action = action;
    // What the player closed or answered has been seen.
    entry.unread =
        !style.missed_only || reason == CloseReason::timed_out || reason == CloseReason::dismissed;
    records_.insert(records_.begin(), std::move(entry));
    serials_.insert(serials_.begin(), next_serial_++);
    const std::size_t limit = static_cast<std::size_t>(std::max(style.max_records, 1));
    if (records_.size() > limit)
    {
        records_.resize(limit);
        serials_.resize(limit);
    }
    event_ = -1;
    dirty_ = true;
}

int NotificationCenter::unread() const
{
    int total = 0;
    for (const NotificationRecord &entry : records_)
        total += entry.unread ? 1 : 0;
    return total;
}

void NotificationCenter::mark_all_read()
{
    for (NotificationRecord &entry : records_)
    {
        dirty_ = dirty_ || entry.unread;
        entry.unread = false;
    }
}

void NotificationCenter::clear()
{
    records_.clear();
    serials_.clear();
    on_clear_ = false;
    event_ = -1;
    dirty_ = true;
}

void NotificationCenter::set_bounds(const Rect &bounds)
{
    bounds_ = bounds;
    sync();
}

Rect NotificationCenter::strip() const
{
    return {bounds_.x, bounds_.y, bounds_.w, style.clear_button ? style.button_height : 0.0f};
}

Rect NotificationCenter::list_rect() const
{
    const float top = style.clear_button ? style.button_height + kStripGap : 0.0f;
    return {bounds_.x, bounds_.y + top, bounds_.w, std::max(bounds_.h - top, 0.0f)};
}

int NotificationCenter::first_row() const
{
    const std::vector<ListItem> &items = list_.items();
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        if (!items[i].header)
            return static_cast<int>(i);
    }
    return 0;
}

int NotificationCenter::focus() const
{
    const std::vector<ListItem> &items = list_.items();
    if (records_.empty() || on_clear_ || items.empty())
        return -1;
    return items[static_cast<std::size_t>(list_.focus())].tag;
}

void NotificationCenter::enter()
{
    on_clear_ = false;
    event_ = -1;
    sync();
    if (!list_.items().empty())
        list_.set_focus(first_row());
    list_.enter();
    empty_.enter();
}

const NotificationRecord *NotificationCenter::event_record() const
{
    if (event_ < 0 || event_ >= count())
        return nullptr;
    return &records_[static_cast<std::size_t>(event_)];
}

// The list shows the records in two groups; the focus stays on the record it
// was on, wherever the regrouping puts it.
void NotificationCenter::rebuild()
{
    dirty_ = false;
    std::uint32_t kept = 0;
    const int before = focus();
    if (before >= 0 && before < static_cast<int>(serials_.size()))
        kept = serials_[static_cast<std::size_t>(before)];

    std::vector<ListItem> items;
    int target = -1;
    for (int group = 0; group < 2; ++group)
    {
        const bool unread = group == 0;
        bool titled = false;
        for (std::size_t i = 0; i < records_.size(); ++i)
        {
            const NotificationRecord &entry = records_[i];
            if (entry.unread != unread)
                continue;
            if (!titled)
            {
                ListItem header;
                header.title = unread ? style.new_label : style.earlier_label;
                header.header = true;
                header.tag = -1;
                items.push_back(std::move(header));
                titled = true;
            }
            ListItem item;
            item.title = entry.notification.title;
            item.tag = static_cast<int>(i);
            // Nothing to choose again: confirm is refused softly.
            item.disabled = entry.notification.actions.empty();
            if (kept != 0 && serials_[i] == kept)
                target = static_cast<int>(items.size());
            items.push_back(std::move(item));
        }
    }
    list_.set_items(std::move(items));
    if (target >= 0)
        list_.set_focus(target);
    else if (!list_.items().empty() &&
             list_.items()[static_cast<std::size_t>(list_.focus())].header)
        list_.set_focus(first_row());
}

void NotificationCenter::sync()
{
    Theme theme = style.theme;
    if (style.on_panel)
    {
        // The list writes its section titles in the page's text colours; on
        // a sheet those are the panel's.
        theme.page_text = theme.text;
        theme.page_text_muted = theme.text_muted;
    }
    ListStyle &rows = list_.style;
    rows.theme = theme;
    rows.sounds = style.sounds;
    rows.reduced_motion = style.reduced_motion;
    rows.highlight = style.highlight;
    rows.padding = style.padding;
    rows.header_size = style.header_size;
    rows.entrance_step = style.entrance_step;
    if (rows.row_height != style.row_height || rows.header_height != style.header_height ||
        rows.gap != style.gap)
    {
        rows.row_height = style.row_height;
        rows.header_height = style.header_height;
        rows.gap = style.gap;
        dirty_ = true;
    }
    // set_bounds() snaps the highlight and the scroll: only when it moved.
    const Rect area = list_rect();
    const Rect &now = list_.bounds();
    if (now.x != area.x || now.y != area.y || now.w != area.w || now.h != area.h)
        list_.set_bounds(area);
    if (dirty_)
        rebuild();
    list_.set_active(active_ && !on_clear_);

    EmptyStateStyle &nothing = empty_.style;
    nothing.theme = style.theme;
    if (!style.on_panel)
    {
        // The empty state writes in the panel's colours; on the page those
        // are the page's.
        const Theme &from = style.theme;
        nothing.theme.text = from.page_text.a > 0.0f ? from.page_text : from.text;
        nothing.theme.text_muted =
            from.page_text_muted.a > 0.0f ? from.page_text_muted : from.text_muted;
    }
    nothing.sounds = style.sounds;
    nothing.reduced_motion = style.reduced_motion;
    nothing.max_text_width = std::max(bounds_.w - 2.0f * nothing.padding, 80.0f);
    empty_.title = style.empty_title;
    empty_.body = style.empty_body;
    empty_.action.clear();
    empty_.set_bounds(bounds_);
}

Event NotificationCenter::handle(const InputFrame &input, Feedback &feedback)
{
    event_ = -1;
    sync();
    const float x = bounds_.cx();
    if (records_.empty())
    {
        if (!input.is_pressed(Action::back))
            return Event::none;
        play_cue(feedback, style, style.sounds.cancel, x);
        return Event::cancelled;
    }
    if (on_clear_)
    {
        const float button_x = bounds_.x + bounds_.w * 0.8f;
        if (input.nav == Direction::down)
        {
            on_clear_ = false;
            play_cue(feedback, style, style.sounds.move, x);
            return Event::moved;
        }
        if (input.nav != Direction::none)
            return refuse(feedback, style, input, refusal_, button_x);
        if (input.is_pressed(Action::confirm))
        {
            clear_press_.trigger();
            play_cue(feedback, style, style.sounds.activate, button_x);
            if (style.sounds.rumble > 0.0f)
                feedback.rumble(0.3f * style.sounds.rumble, 0.04f);
            clear();
            return Event::changed;
        }
        if (input.is_pressed(Action::back))
        {
            play_cue(feedback, style, style.sounds.cancel, x);
            return Event::cancelled;
        }
        return Event::none;
    }
    // Above the first row is the button, not the edge.
    if (style.clear_button && input.nav == Direction::up && list_.focus() == first_row())
    {
        on_clear_ = true;
        play_cue(feedback, style, style.sounds.move, bounds_.x + bounds_.w * 0.8f, 1.05f);
        return Event::moved;
    }
    const Event event = list_.handle(input, feedback);
    if (event == Event::activated)
    {
        event_ = focus();
        const NotificationRecord *entry = event_record();
        if (entry == nullptr)
            return Event::none;
        if (on_action)
            on_action(*entry, 0);
    }
    return event;
}

void NotificationCenter::update(float dt)
{
    for (NotificationRecord &entry : records_)
        entry.age += dt;
    sync();
    list_.update(dt);
    empty_.update(dt);
    clear_focus_.target = active_ && on_clear_ ? 1.0f : 0.0f;
    clear_focus_.update(dt, std::max(style.omega(), 18.0f));
    clear_press_.update(dt, 9.0f);
    refusal_.update(dt, 9.0f);
}

std::string NotificationCenter::time_text(const NotificationRecord &entry) const
{
    if (!style.relative_time)
        return entry.notification.time;
    char text[24];
    const int seconds = static_cast<int>(entry.age);
    if (seconds < 60)
        return "now";
    if (seconds < 3600)
        std::snprintf(text, sizeof(text), "%d min", seconds / 60);
    else if (seconds < 86400)
        std::snprintf(text, sizeof(text), "%d h", seconds / 3600);
    else
        std::snprintf(text, sizeof(text), "%d d", seconds / 86400);
    return text;
}

void NotificationCenter::draw_row(Canvas &canvas, const Rect &row, const NotificationRecord &entry,
                                  float focus) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Canvas inside{list, canvas.fonts, 0, canvas.time};
    Painter paint(list, canvas.fonts, theme, 0);
    const Notification &note = entry.notification;
    const Color resting = style.on_panel ? theme.text : paint.page_text();
    const Color muted = style.on_panel ? theme.text_muted : paint.page_text_muted();
    const Color ink = Highlight::text_color(style, style.highlight, focus, resting);
    const Color quiet = gfx::mix(muted, ink, focus * 0.5f);

    float left = row.x + style.padding;
    const float right = row.x + row.w - style.padding;
    if (style.unread_dot && entry.unread)
        list.circle(row.x + style.padding * 0.45f, row.cy(), 4.0f,
                    Color{theme.primary.r, theme.primary.g, theme.primary.b, 1.0f});
    const bool has_icon = static_cast<bool>(note.icon) || note.kind != StatusKind::none;
    if (has_icon && style.icon_size > 0.0f)
    {
        const Rect box{left, row.cy() - style.icon_size * 0.5f, style.icon_size, style.icon_size};
        if (note.icon)
            note.icon(inside, box);
        else
            draw_status_icon(inside, theme, note.kind, box.cx(), box.cy(), box.w);
        left += style.icon_size + kIconGap;
    }

    const std::string &second = note.body.empty() ? note.source : note.body;
    // Two lines: the far column keeps them even when the row has one.
    const float block = style.title_size + style.body_size * 1.3f;
    const float upper = row.cy() - block * 0.5f + style.title_size * 0.84f;
    const float under = upper + style.body_size * 1.36f;
    const float first = second.empty() ? row.cy() + style.title_size * 0.35f : upper;

    // The far column: when, and what can still be done about it.
    const std::string when = time_text(entry);
    float title_right = right;
    if (!when.empty())
        title_right -=
            paint.body(when, right, upper, style.meta_size, quiet, gfx::Align::right) + 14.0f;
    float body_right = right;
    if (style.action_hints && !note.actions.empty())
    {
        const float size = style.meta_size;
        const float room = std::max((right - left) * 0.45f, 40.0f);
        const std::string label = fit_label(paint, note.actions[0], size, room - 22.0f);
        const float w = paint.label_width(label, size) + 22.0f;
        const float h = size * 1.7f;
        const Rect pill{right - w, under - size * 0.36f - h * 0.5f, w, h};
        const float radius = theme.pill_chips ? h * 0.5f : std::min(theme.radius, h * 0.5f);
        const Color solid{theme.primary.r, theme.primary.g, theme.primary.b, 1.0f};
        // An outline at rest; under the focus it fills: "confirm does this".
        paint.fill(pill, radius, solid.with_alpha(focus));
        paint.stroke(pill, radius, std::clamp(theme.border, 1.5f, 3.0f),
                     gfx::mix(quiet, solid, focus));
        paint.label(label, pill.cx(), pill.cy() + size * 0.35f, size,
                    gfx::mix(quiet, Painter::on(solid), focus), gfx::Align::center);
        body_right = pill.x - 14.0f;
        if (second.empty())
            title_right = std::min(title_right, body_right);
    }

    paint.bounded_label(note.title, left, first, style.title_size, ink, std::max(title_right - left, 40.0f));
    if (!second.empty())
        paint.bounded_body(second, left, under, style.body_size, quiet, std::max(body_right - left, 40.0f));
}

void NotificationCenter::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    if (records_.empty())
    {
        // The slot is set on a copy: it must not outlive this call's theme.
        EmptyState nothing = empty_;
        const Color quiet = nothing.style.theme.text_muted;
        const bool round = theme.corner == Corner::round && theme.radius >= 2.0f;
        nothing.icon = [&theme, quiet, round](Canvas &target, const Rect &area)
        {
            Painter paint(target.list, target.fonts, theme, 0);
            paint.fill(area, round ? area.w * 0.5f : 0.0f, quiet.with_alpha(0.12f));
            draw_bell(target, area.inset(area.w * 0.24f), quiet);
        };
        nothing.draw(canvas);
        return;
    }

    if (style.clear_button)
    {
        Canvas inside{list, canvas.fonts, 0, canvas.time};
        Painter paint(list, canvas.fonts, theme, 0);
        const Rect bar = strip();
        const Color muted = style.on_panel ? theme.text_muted : paint.page_text_muted();
        const float width = std::min(paint.label_width(style.clear_label, style.button_text) +
                                         2.0f * style.button_padding,
                                     bar.w * 0.6f);
        Rect button{bar.x + bar.w - width, bar.y, width, bar.h};
        button.x += shake(refusal_.value, canvas.time, 8.0f);

        const int fresh = unread();
        std::string summary = style.read_label;
        if (fresh > 0)
        {
            char number[16];
            std::snprintf(number, sizeof(number), "%d", fresh);
            summary = number + style.unread_suffix;
        }
        paint.bounded_body(summary, bar.x + 2.0f, bar.cy() + (style.meta_size + 2.0f) * 0.35f, style.meta_size + 2.0f, muted, std::max(button.x - bar.x - 16.0f, 40.0f));

        const float press = tween::clamp01(clear_press_.value);
        Look look;
        look.focus = tween::clamp01(clear_focus_.value);
        look.press = press;
        const float scale = style.reduced_motion ? 1.0f : 1.0f - 0.04f * press;
        list.push_transform(scale, button.cx(), button.cy(), 0.0f, 0.0f);
        const ButtonFace face = draw_button_face(inside, theme, button, ButtonRole::secondary, look,
                                                 -1.0f, !style.on_panel);
        paint.bounded_label(style.clear_label, face.content.cx(), face.content.cy() + style.button_text * 0.35f, style.button_text, face.ink, button.w - 16.0f, gfx::Align::center);
        list.pop_transform();
    }
    list_.draw(canvas);
}

} // namespace hui::ui
