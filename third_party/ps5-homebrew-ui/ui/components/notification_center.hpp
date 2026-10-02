// ps5-homebrew-ui - Component: NotificationCenter, the history of what the stack showed.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A notification that timed out while the player looked elsewhere must not be
// lost. The centre records every notification that left a NotificationStack,
// newest first, with how it ended, and lists them under "New" (not seen yet)
// and "Earlier". It draws inside any rectangle: put it in a ui::Sheet through
// the sheet's content slot, in a panel, or on a page of its own.

#pragma once

#include "ui/components/list.hpp"
#include "ui/components/notification.hpp"
#include "ui/components/stat.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hui::ui
{

struct NotificationRecord
{
    int id = 0; // the id it had in the stack; 0 when recorded by hand
    Notification notification;
    CloseReason reason = CloseReason::timed_out;
    int action = -1;    // the action that ended it (CloseReason::action), or -1
    bool unread = true; // listed under "New" until mark_all_read()
    float age = 0.0f;   // seconds since it was recorded
};

struct NotificationCenterStyle : ComponentStyle
{
    // ---- geometry ----
    float row_height = 84.0f;
    float header_height = 46.0f; // the "New" and "Earlier" titles
    float gap = 6.0f;            // between rows
    float padding = 16.0f;       // inside a row, left and right
    float icon_size = 36.0f;
    float button_height = 44.0f; // the "Clear all" button
    float button_padding = 18.0f;
    // ---- type ----
    float title_size = 23.0f;
    float body_size = 19.0f;
    float meta_size = 17.0f; // the time, the action hint and the summary
    float header_size = 17.0f;
    float button_text = 19.0f;
    // ---- look ----
    HighlightStyle highlight; // of the focused row
    bool on_panel = true;     // it sits on a sheet or a panel; false: straight on the page
    bool unread_dot = true;   // a dot in the primary colour before a row not seen yet
    bool action_hints = true; // a row that still has an action shows its label
    // ---- words ----
    std::string new_label = "New";
    std::string earlier_label = "Earlier";
    std::string clear_label = "Clear all";
    std::string unread_suffix = " unread"; // the summary: the count, then this
    std::string read_label = "All read";   // the summary when nothing is unread
    std::string empty_title = "No notifications";
    std::string empty_body = "What you miss while you play will wait for you here.";
    // ---- behaviour ----
    bool clear_button = true;    // a "Clear all" button above the list
    bool relative_time = true;   // "now", "5 min", "2 h" from the record's age; false: its own text
    bool missed_only = true;     // only what timed out or was dismissed counts as unread
    int max_records = 50;        // the oldest are dropped
    float entrance_step = 0.03f; // seconds between rows arriving; 0 for none
};

// The history of a NotificationStack.
//
//   ui::NotificationCenter center;
//   center.style.theme = theme;
//   center.attach(stack);                   // records what leaves the stack
//   sheet.content = [&](ui::Canvas &canvas, const gfx::Rect &, float) { center.draw(canvas); };
//   center.set_bounds(sheet.content_rect());
//   ...
//   if (sheet.is_open() && sheet.handle(input, feedback) == ui::Event::none)
//       if (center.handle(input, feedback) == ui::Event::activated)
//           run(*center.event_record());    // its main action, chosen again
//   center.update(dt);
class NotificationCenter
{
  public:
    // The record's main action was chosen from the list.
    using ActionSlot = std::function<void(const NotificationRecord &record, int action)>;

    NotificationCenterStyle style;
    ActionSlot on_action;

    NotificationCenter();
    // The list inside draws its rows through this object, and attach() hands
    // its address to a stack: a copy would still point at the original.
    NotificationCenter(const NotificationCenter &) = delete;
    NotificationCenter &operator=(const NotificationCenter &) = delete;

    // Records everything that leaves the stack from now on. It takes the
    // stack's on_leave slot and is captured by address: keep the centre where
    // it is while it is attached, and detach() before it goes away.
    void attach(NotificationStack &stack);
    void detach(NotificationStack &stack);
    // Records one by hand.
    void record(Notification notification, CloseReason reason, int id = 0, int action = -1);

    const std::vector<NotificationRecord> &records() const
    {
        return records_; // newest first
    }
    int count() const
    {
        return static_cast<int>(records_.size());
    }
    int unread() const;
    // Everything moves to "Earlier": call it when the player has seen the list.
    void mark_all_read();
    // Forgets everything.
    void clear();

    void set_bounds(const gfx::Rect &bounds);
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    // Whether it has the screen's focus (an inactive list keeps a faint highlight).
    void set_active(bool active)
    {
        active_ = active;
    }
    // The centre was opened: the focus goes to the newest record and the
    // rows arrive one after the other.
    void enter();
    // The focused record's index in records(), or -1 (the clear button, or
    // an empty history).
    int focus() const;

    // Event::moved and Event::refused from the list, Event::activated when a
    // record's main action was chosen again (event_record()), Event::changed
    // when "Clear all" emptied the history, Event::cancelled on back.
    Event handle(const InputFrame &input, Feedback &feedback);
    // The record the last Event::activated was about; nullptr otherwise.
    // Valid until the history changes.
    const NotificationRecord *event_record() const;

    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    void sync();
    void rebuild();
    gfx::Rect strip() const;
    gfx::Rect list_rect() const;
    int first_row() const;
    std::string time_text(const NotificationRecord &record) const;
    void draw_row(Canvas &canvas, const gfx::Rect &row, const NotificationRecord &record,
                  float focus) const;

    std::vector<NotificationRecord> records_;
    std::vector<std::uint32_t> serials_; // one per record: what the focus follows
    std::uint32_t next_serial_ = 1;
    gfx::Rect bounds_{0.0f, 0.0f, 520.0f, 720.0f};
    ListView list_;
    EmptyState empty_;
    bool active_ = true;
    bool on_clear_ = false; // the focus is on the "Clear all" button
    bool dirty_ = true;
    int event_ = -1;
    tween::Spring clear_focus_;
    Pulse clear_press_;
    Pulse refusal_;
};

} // namespace hui::ui
