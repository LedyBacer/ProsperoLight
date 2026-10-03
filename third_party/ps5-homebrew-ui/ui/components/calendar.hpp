// ps5-homebrew-ui - Component: Calendar, a month grid with a gliding focus.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/card.hpp"
#include "ui/components/data_common.hpp"
#include "ui/components/progress.hpp"

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace hui::ui
{

// A day of the proleptic Gregorian calendar.
struct CalendarDate
{
    int year = 2026;
    int month = 1; // 1..12
    int day = 1;   // 1..31

    friend bool operator==(const CalendarDate &a, const CalendarDate &b)
    {
        return a.year == b.year && a.month == b.month && a.day == b.day;
    }
    friend bool operator!=(const CalendarDate &a, const CalendarDate &b)
    {
        return !(a == b);
    }
    friend bool operator<(const CalendarDate &a, const CalendarDate &b)
    {
        if (a.year != b.year)
            return a.year < b.year;
        return a.month != b.month ? a.month < b.month : a.day < b.day;
    }
};

bool is_leap_year(int year);
int days_in_month(int year, int month);
// 0 is Sunday, 6 is Saturday.
int weekday_of(const CalendarDate &date);
// The date `days` after (or, negative, before) a date.
CalendarDate add_days(const CalendarDate &date, int days);
// Days from a to b; negative when b is earlier.
int days_between(const CalendarDate &a, const CalendarDate &b);

// A dot under a day: something happens then.
struct CalendarMark
{
    CalendarDate date;
    Status status = Status::accent;
    gfx::Color color{0.0f, 0.0f, 0.0f, 0.0f}; // alpha > 0 overrides status
};

enum class CalendarSelect : std::uint8_t
{
    none,   // confirm returns Event::activated
    single, // confirm selects the focused day
    range,  // confirm sets the start, the next confirm the end
};

enum class WeekStart : std::uint8_t
{
    sunday,
    monday,
};

struct CalendarStyle : ComponentStyle
{
    // ---- behaviour ----
    WeekStart week_start = WeekStart::monday;
    CalendarSelect select = CalendarSelect::single;
    EdgeExits exits; // edges of the grid that hand the focus back instead of changing month
    // ---- geometry ----
    float padding = 14.0f;        // between the panel and the grid (panel = true)
    float title_height = 40.0f;   // the month and year
    float weekday_height = 26.0f; // the row of weekday names
    float cell_gap = 4.0f;        // between days
    float slide = 36.0f;          // how far a month travels when it changes
    // ---- type ----
    float title_size = 22.0f;
    float weekday_size = 15.0f;
    float day_size = 20.0f;
    // ---- look ----
    HighlightStyle highlight{HighlightKind::ring};
    bool panel = true;        // a themed panel behind the calendar
    bool outside_days = true; // show the neighbouring months' days in the grid's spare cells
    bool mark_today = true;   // a ring round today
    int max_marks = 3;        // dots shown under one day
    float mark_size = 5.0f;   // a dot's diameter
    // ---- words ----
    std::array<std::string, 12> months = {"January",   "February", "March",    "April",
                                          "May",       "June",     "July",     "August",
                                          "September", "October",  "November", "December"};
    // Sunday first, whatever week_start is.
    std::array<std::string, 7> weekdays = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
};

// A month: days moved through in two dimensions under one gliding focus,
// today marked, one day or a range selected, dots under days that have
// something, days that cannot be chosen. Moving past the first or last day
// shown slides to the next month.
//
//   ui::Calendar calendar;
//   calendar.set_today({2026, 10, 2});
//   calendar.style.select = ui::CalendarSelect::range;
//   calendar.set_marks({{{2026, 10, 9}}, {{2026, 10, 17}, ui::Status::danger}});
//   calendar.set_bounds({96, 300, 430, 330});
//   ...
//   if (calendar.handle(input, feedback) == ui::Event::changed) use(calendar.range_start());
class Calendar
{
  public:
    // True for a day that cannot be chosen.
    using Filter = std::function<bool(const CalendarDate &date)>;
    // cell is the day's square; focus is 0..1; selected says a plate is under it.
    using Slot = std::function<void(Canvas &canvas, const gfx::Rect &cell, const CalendarDate &date,
                                    float focus, bool selected)>;

    CalendarStyle style;
    Filter disabled; // days that refuse confirm (they stay focusable, dimmed)
    Slot day;        // draws a day instead of its number and dots

    void set_bounds(const gfx::Rect &bounds);
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    // Which day is "today"; it is also where the focus starts.
    void set_today(const CalendarDate &date);
    const CalendarDate &today() const
    {
        return today_;
    }
    void set_marks(std::vector<CalendarMark> marks);
    // The focus cannot leave [first, last]. Clear the limits with clear_limits().
    void set_limits(const CalendarDate &first, const CalendarDate &last);
    void clear_limits();

    const CalendarDate &focus() const
    {
        return focus_;
    }
    // Moves the focus (and the month shown) without sound.
    void set_focus(const CalendarDate &date, bool snap = true);
    // The month on show.
    int year() const
    {
        return focus_.year;
    }
    int month() const
    {
        return focus_.month;
    }

    // single: the selected day. range: the start and the end; while only the
    // start is set, has_range() is false and range_end() is the start.
    bool has_selection() const
    {
        return has_start_;
    }
    bool has_range() const
    {
        return has_start_ && has_end_;
    }
    const CalendarDate &selected() const
    {
        return start_;
    }
    const CalendarDate &range_start() const
    {
        return start_;
    }
    const CalendarDate &range_end() const
    {
        return has_end_ ? end_ : start_;
    }
    void select(const CalendarDate &date);
    void select_range(const CalendarDate &first, const CalendarDate &last);
    void clear_selection();

    void set_active(bool active)
    {
        active_ = active;
    }
    void enter();

    Event handle(const InputFrame &input, Feedback &feedback);
    // The edge the focus left through on the last handle(), or Direction::none.
    Direction exit() const
    {
        return exit_;
    }
    void update(float dt);
    void draw(Canvas &canvas) const;

    // Where a day of the month on show is on screen.
    gfx::Rect day_rect(const CalendarDate &date) const;

  private:
    gfx::Rect inner() const;
    gfx::Rect grid() const;
    int start_column() const; // the weekday of the first column, 0 = Sunday
    CalendarDate first_cell(int year, int month) const;
    gfx::Rect cell_rect(int index) const;
    bool is_disabled(const CalendarDate &date) const;
    void retarget(bool snap);
    void draw_month(Canvas &canvas, Painter &paint, int year, int month, float dx, float alpha,
                    bool current) const;

    gfx::Rect bounds_{0.0f, 0.0f, 430.0f, 330.0f};
    CalendarDate today_;
    CalendarDate focus_;
    CalendarDate start_;
    CalendarDate end_;
    bool has_start_ = false;
    bool has_end_ = false;
    CalendarDate first_limit_;
    CalendarDate last_limit_;
    bool limited_ = false;
    std::vector<CalendarMark> marks_;
    bool active_ = true;
    float age_ = 10.0f;
    Direction exit_ = Direction::none;
    // The month that is leaving, and which way: +1 when time moved forward.
    int old_year_ = 2026;
    int old_month_ = 1;
    float travel_ = 0.0f;
    tween::Spring slide_; // 1 just changed .. 0 settled
    Highlight highlight_;
    tween::Spring active_amount_{1.0f, 0.0f, 1.0f};
    Pulse press_;
};

} // namespace hui::ui
