// ps5-homebrew-ui - Component: NotificationStack, notifications the player can answer.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A ToastStack says something and leaves. A notification can be answered: it
// carries up to two actions and a close control, it may stay until someone
// deals with it, and it can change in place (a download that fills a bar and
// then offers "Restart"). There is no pointer on a console, so the stack
// offers two ways to reach it, and a screen may use both:
//
//   - a shortcut button: the newest notification shows the button's glyph in
//     its main action, and handle_shortcut() fires it from anywhere;
//   - the focus: set_active(true) puts one ring on the newest notification
//     and handle() moves it; back hands the focus to the screen again.
//
// What leaves the stack is reported through on_close and on_leave; a
// NotificationCenter (notification_center.hpp) keeps those as a history.

#pragma once

#include "core/input.hpp"
#include "ui/components/overlay.hpp"
#include "ui/components/progress.hpp"
#include "ui/components/toast.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hui::ui
{

enum class NotificationLook : std::uint8_t
{
    card,    // a header row (icon, source, time, close), then title and body, then buttons
    compact, // one row: icon, title, the main action, close
    accent,  // a bar in the status colour on the leading edge and a tinted panel
};

// How a notification ended.
enum class CloseReason : std::uint8_t
{
    timed_out, // its time ran out
    closed,    // the player closed it
    action,    // the player chose one of its actions
    dismissed, // the application sent it away (dismiss(), clear())
};

struct Notification
{
    // box is the square the status icon would take.
    using Icon = std::function<void(Canvas &canvas, const gfx::Rect &box)>;

    StatusKind kind = StatusKind::info; // the icon and the colour; none for no icon
    std::string source;                 // who says it: "System update"
    std::string time;                   // when: "now", "2 min"
    std::string title;
    std::string body;                 // wraps to style.body_lines lines
    std::vector<std::string> actions; // up to two labels; the first is the main one
    bool closable = true;             // it has a close control
    float seconds = 0.0f;             // 0: style.duration; negative: stays until closed
    float progress = -1.0f;           // 0..1 draws a progress bar; negative: none
    Icon icon;                        // replaces the status icon
    int tag = 0;                      // yours
};

struct NotificationStyle : ComponentStyle
{
    NotificationLook look = NotificationLook::card;
    // ---- geometry ----
    ToastAnchor anchor = ToastAnchor::top_right;
    float width = 560.0f;
    float margin = 48.0f;      // from the edges of the bounds
    float gap = 14.0f;         // between notifications
    float padding = 20.0f;     // inside a notification
    float icon_size = 40.0f;   // the leading icon (accent, and cards without a header)
    float header_icon = 26.0f; // the icon in a card's header and in a compact row
    float close_size = 36.0f;  // the close control's box
    float bar_width = 6.0f;    // accent: the bar on the leading edge
    float button_height = 46.0f;
    float button_padding = 18.0f;   // left and right of a button's label
    float button_min_width = 96.0f; // a button that hugs its label is never narrower
    float button_gap = 10.0f;
    bool stretch_actions = true; // card and accent: the buttons share the row; false: they hug
    float glyph_size = 26.0f;    // the shortcut glyphs
    // ---- type ----
    float meta_size = 17.0f; // source, time and the percentage
    float title_size = 24.0f;
    float body_size = 20.0f;
    float button_text = 20.0f;
    int body_lines = 3; // the body wraps to this many lines; 0 hides bodies
    // ---- look ----
    bool frosted = true;           // the blurred screen behind when canvas.glass is set, else solid
    float frost = 0.6f;            // how much surface colour covers the frost
    float tint = 0.12f;            // accent: how much status colour washes the panel
    bool divider = true;           // card: a hairline under the header
    bool time_bar = true;          // a thin bar showing the time a timed one has left
    float time_bar_height = 4.0f;  // its thickness
    float progress_height = 8.0f;  // the progress bar of Notification::progress
    bool progress_percent = true;  // ... with the value as "64%" beside it (not in compact)
    bool more_marker = true;       // "+N more" after the stack while some are waiting
    std::string more_prefix = "+"; // the marker reads prefix, count, suffix
    std::string more_suffix = " more";
    // ---- behaviour ----
    int max_visible = 3;             // more than this wait their turn
    bool fit_bounds = true;          // ... and so does one the bounds have no room for yet
    float duration = 6.0f;           // seconds on screen when a notification names none
    bool newest_first = true;        // the newest sits at the anchored edge; false: at the far end
    bool pause_when_active = true;   // timers stand still while the stack has the focus
    bool close_on_action = true;     // choosing an action sends the notification away
    bool pitch_by_kind = true;       // success chimes a little higher, danger lower
    Action shortcut = Action::count; // fires the newest main action (count: none)
    Action dismiss_shortcut = Action::count; // closes the newest closable one (count: none)
    bool swap_confirm = false;               // the player swapped Cross and Circle (for the glyphs)
};

// A stack of notifications anchored to a corner or an edge.
//
//   ui::NotificationStack stack;
//   stack.style.theme = theme;
//   ui::Notification note;
//   note.source = "System update";
//   note.title = "Version 2.4 is ready";
//   note.actions = {"Update now", "Later"};
//   note.seconds = -1.0f;                         // stays until answered
//   const int id = stack.push(note);
//   ...
//   if (stack.active() && stack.handle(input, feedback) == ui::Event::activated)
//       run(stack.event_id(), stack.event_action());
//   stack.update(dt, feedback);
//   stack.draw(canvas);                           // after everything else
class NotificationStack
{
  public:
    using ActionSlot = std::function<void(int id, int action)>;
    using CloseSlot = std::function<void(int id, CloseReason reason)>;
    // action is the action that ended it, or -1.
    using LeaveSlot = std::function<void(int id, const Notification &notification,
                                         CloseReason reason, int action)>;

    NotificationStyle style;
    ActionSlot on_action; // an action was chosen (called from handle / handle_shortcut)
    CloseSlot on_close;   // one left the stack (called from update)
    LeaveSlot on_leave;   // the same with its content; NotificationCenter::attach() takes it

    // The screen the stack is anchored in: the whole canvas by default.
    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }

    // Queues a notification and returns its id (ids start at 1).
    int push(Notification notification);
    // Replaces the content of one in place, with a short flash and a
    // cross-fade. Called for the notification whose action was just chosen,
    // it keeps that notification on screen instead of letting it close.
    // False when the id is not in the stack any more.
    bool update_notification(int id, Notification notification, bool restart_timer = true);
    // Change one field. Progress and running text change quietly; pass pulse
    // to flash the notification as update_notification() does.
    bool set_title(int id, std::string title, bool pulse = false);
    bool set_body(int id, std::string body, bool pulse = false);
    bool set_progress(int id, float progress);
    bool set_actions(int id, std::vector<std::string> actions, bool pulse = true);
    bool set_kind(int id, StatusKind kind, bool pulse = true);
    // Sends one away (or drops it from the queue): CloseReason::dismissed.
    void dismiss(int id);
    // Sends every one away and empties the queue; `now` skips the exits.
    void clear(bool now = false);

    // Notifications on screen and not leaving, and notifications waiting.
    int visible_count() const;
    int queued_count() const
    {
        return static_cast<int>(queue_.size());
    }
    bool empty() const
    {
        return shown_.empty() && queue_.empty();
    }
    // The notification with that id while it is waiting or on screen (also
    // while it leaves, so the answer to an action can still read its tag).
    const Notification *find(int id) const;
    // The newest one on screen, or 0.
    int newest() const;
    // Time left of one on screen, 0..1 (1 for a sticky one); -1 when it is not on screen.
    float remaining(int id) const;

    // ---- the focus ----
    // Gives the stack the screen's focus: the ring starts on the newest
    // notification's main control. While it is active the timers stand still.
    void set_active(bool active);
    bool active() const
    {
        return active_;
    }
    // Something on screen has a control the focus could rest on.
    bool focusable() const;
    // The focused notification (0: none) and its control: the actions in
    // order, then the close control.
    int focus_id() const
    {
        return focus_id_;
    }
    int focus_control() const
    {
        return focus_control_;
    }
    bool set_focus(int id, int control = 0);
    // How many controls a notification on screen has.
    int controls(int id) const;

    // The focus path, while the stack is active. Up and down move between
    // notifications, left and right between one's controls. Returns
    // Event::activated for an action (event_id(), event_action()),
    // Event::changed when the player closed one (event_id()), Event::moved,
    // Event::refused, and Event::cancelled on back or when nothing is left to
    // focus: the stack is then inactive again and the screen has the focus.
    Event handle(const InputFrame &input, Feedback &feedback);
    // The shortcut path: call it every frame, whoever has the focus. Fires
    // style.shortcut (Event::activated) and style.dismiss_shortcut
    // (Event::changed) on the newest notification that can take them.
    Event handle_shortcut(const InputFrame &input, Feedback &feedback);
    // What the last handle() or handle_shortcut() was about.
    int event_id() const
    {
        return event_id_;
    }
    int event_action() const
    {
        return event_action_;
    }

    void update(float dt, Feedback &feedback);
    void draw(Canvas &canvas) const;

    // Where a notification rests on screen, and where one of its controls is.
    // An empty rectangle when the id is not on screen.
    gfx::Rect card_rect(const Fonts &fonts, int id) const;
    gfx::Rect control_rect(const Fonts &fonts, int id, int control) const;

  private:
    struct Entry
    {
        int id = 0;
        Notification note;
        Notification old;     // what it said before update_notification()
        float swap = 0.0f;    // 1 -> 0 while the old content gives way to the new
        float seconds = 0.0f; // negative: stays
        float left = 0.0f;
        bool leaving = false;
        bool reported = false; // on_close has been told
        CloseReason reason = CloseReason::dismissed;
        int action = -1;
        int pressed = -1;                     // the control the last press was on
        tween::Bounce enter;                  // 0 off screen .. 1 in place
        tween::Spring leave;                  // 0 in place .. 1 gone
        tween::Bounce slot{0.0f, 0.0f, 1.0f}; // share of its room it takes
        tween::Spring height;                 // the panel's height follows the content
        bool sized = false;
        Pulse changed;
        Pulse press;
        ProgressBar bar;
    };

    // Where everything of one notification is, relative to its top left corner.
    struct Layout
    {
        float height = 0.0f;
        gfx::Rect icon{0.0f, 0.0f, 0.0f, 0.0f}; // w 0: none
        std::string meta;                       // source (card) or source and time (accent)
        std::string time;                       // card: right-aligned in the header
        float meta_x = 0.0f;
        float meta_baseline = 0.0f;
        float time_right = 0.0f;
        float divider_y = -1.0f;
        float text_x = 0.0f;
        std::string title;
        float title_baseline = 0.0f;
        std::vector<std::string> body;
        float body_top = 0.0f;
        gfx::Rect progress{0.0f, 0.0f, 0.0f, 0.0f}; // h 0: none
        gfx::Rect controls[3];
        int count = 0;                              // actions, then the close control
        int actions = 0;                            // how many of them are actions
        gfx::Rect time_bar{0.0f, 0.0f, 0.0f, 0.0f}; // h 0: none
        float action_glyph = 0.0f;  // width of the shortcut glyph in the main action
        float dismiss_glyph = 0.0f; // ... and of the one beside the close control
    };

    struct Placed
    {
        std::size_t index = 0; // into shown_
        gfx::Rect rect;        // at rest, the eased height applied
        Layout layout;
    };

    struct Report
    {
        int id = 0;
        Notification note;
        CloseReason reason = CloseReason::dismissed;
        int action = -1;
    };

    Layout lay(const Fonts &fonts, const Notification &note, bool timed, bool action_glyph,
               bool dismiss_glyph) const;
    Layout lay(const Fonts &fonts, const Entry &entry) const;
    std::vector<Placed> place(const Fonts &fonts, float *end = nullptr) const;
    void draw_entry(Canvas &canvas, const Entry &entry, const Placed &at) const;
    void draw_content(Canvas &canvas, const Entry &entry, const Notification &note,
                      const Layout &at, float x, float y, bool live) const;
    void style_bar(ProgressBar &bar, const Notification &note) const;

    Entry *shown(int id);
    const Entry *shown(int id) const;
    int control_count(const Notification &note) const;
    bool fits(const Entry &next) const;
    int action_count(const Notification &note) const;
    int glyph_owner() const;   // the id whose main action shows style.shortcut
    int dismiss_owner() const; // the id whose close control shows style.dismiss_shortcut
    float anchor_x() const;
    float resolve(float seconds) const;
    void start_leave(Entry &entry, CloseReason reason, int action = -1);
    Event fire(Entry &entry, int action, Feedback &feedback);
    // The notification with that id while its content may still change.
    Notification *editable(int id);
    void flash(int id);
    // The ids the focus can rest on, top to bottom.
    std::vector<int> focus_order() const;
    void move_focus(int id, int control);
    void ensure_focus();

    gfx::Rect bounds_{0.0f, 0.0f, gfx::kVirtualWidth, gfx::kVirtualHeight};
    std::vector<Entry> shown_; // in order of appearance
    std::vector<Entry> queue_;
    std::vector<Report> reports_; // left the queue: told at the next update()
    int next_id_ = 1;
    bool active_ = false;
    int focus_id_ = 0;
    int focus_control_ = 0;
    int event_id_ = 0;
    int event_action_ = -1;
    // The ring glides in the focused notification's own space, so it rides
    // along while the stack closes a gap instead of chasing its control.
    SpringRect ring_;
    bool ring_set_ = false;
    Pulse refusal_;
    tween::Spring active_amount_;
    tween::Spring more_;                       // 0..1: the "+N more" marker
    tween::Bounce more_pop_{1.0f, 0.0f, 1.0f}; // it bounces when the count changes
    int more_count_ = 0;
    // The fonts of the last draw: update() needs them to measure text for the
    // heights and the ring, and only a canvas brings fonts.
    mutable const Fonts *fonts_ = nullptr;
};

} // namespace hui::ui
