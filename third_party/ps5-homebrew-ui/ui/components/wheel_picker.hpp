// ps5-homebrew-ui - Components: WheelPicker, and DatePicker and TimePicker built on it.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/card.hpp"
#include "ui/components/component.hpp"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace hui::ui
{

struct WheelColumn
{
    std::vector<std::string> values;
    float weight = 1.0f;   // its share of the width
    std::string separator; // drawn before the column on the centre line (":" in a time)
    bool wrap = true;      // goes round when style.wrap is on (a year column says false)

    // So a column can be written {{"XS", "S", "M"}} or {{"Slim", "Loose"}, 1.6f}.
    WheelColumn() = default;
    WheelColumn(std::vector<std::string> texts, float share = 1.0f)
        : values(std::move(texts)), weight(share)
    {
    }
};

struct WheelStyle : ComponentStyle
{
    // ---- geometry ----
    int visible = 5; // rows shown; the middle one is the value
    float row_height = 36.0f;
    float padding = 10.0f;         // inside the well, left and right
    float column_gap = 6.0f;       // between columns
    float separator_width = 16.0f; // the room a column's separator takes
    // ---- type ----
    float value_size = 25.0f;
    float title_size = 20.0f;
    float title_gap = 10.0f;
    // ---- look ----
    float fade = 0.8f;        // how far rows fade toward the top and bottom (0..1)
    float shrink = 0.22f;     // how far they shrink (0..1)
    bool boxed = true;        // a sunken well behind the wheels
    bool band = true;         // a quiet band across the middle row of every column
    HighlightStyle highlight; // the focus: glides between the columns' middle rows
    bool focus_ring = true;   // the theme's ring around the well while active but not engaged
    bool on_page = false;     // drawn straight on the page (with boxed off)
    // ---- behaviour ----
    bool wrap = false;              // past the last value comes the first (columns that allow it)
    int fast_after = 6;             // held repeats before the wheel takes longer strides; 0 never
    int fast_factor = 3;            // values passed at once from then on
    EdgeExits exits;                // left and right edges that hand the focus back
    bool pitch_by_direction = true; // the tick is higher going down the list than up
    audio::Cue tick = audio::Cue::tick; // one value passing the middle row
};

// Columns of values that spin: up and down turn the active column one value
// (longer strides when held), left and right change column. Values fade and
// shrink away from the middle row; the wheel overshoots a little and settles,
// and bulges against its end instead of stopping dead.
//
//   ui::WheelPicker size;
//   size.style.theme = theme;
//   size.set_columns({{{"XS", "S", "M", "L", "XL"}}, {{"Slim", "Regular", "Loose"}, 1.6f}});
//   size.set_index(0, 2);
//   size.set_bounds({96, 300, 360, size.preferred_height()});
//   ...
//   size.set_active(focused);
//   if (focused && size.handle(input, feedback) == ui::Event::changed) apply(size.index(0));
//   size.update(dt);
//   size.draw(canvas);
class WheelPicker
{
  public:
    // cell is where a value is drawn; emphasis is 1 on the middle row and
    // falls to 0 at the edges.
    using Item = std::function<void(Canvas &canvas, const gfx::Rect &cell, int column, int index,
                                    float emphasis)>;

    WheelStyle style;
    Item item; // draws a value instead of its text

    void set_title(std::string title)
    {
        title_ = std::move(title);
    }
    void set_columns(std::vector<WheelColumn> columns);
    int column_count() const
    {
        return static_cast<int>(columns_.size());
    }
    const WheelColumn &column_at(int column) const
    {
        return columns_[static_cast<std::size_t>(column)];
    }
    // Replaces a column's values. The index is kept (clamped), and the wheel
    // turns to it when it had to move: a month that got shorter.
    void set_values(int column, std::vector<std::string> values);
    // Silent and without animation: for loading a stored value.
    void set_index(int column, int index);
    int index(int column) const
    {
        return states_[static_cast<std::size_t>(column)].index;
    }
    // The selected value's text; empty for a column without values.
    const std::string &value(int column) const;

    // The column the D-pad turns.
    int column() const
    {
        return column_;
    }
    void set_column(int column);
    // The column the last `changed` event was about.
    int changed_column() const
    {
        return changed_;
    }

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    float preferred_height() const;
    void set_active(bool active)
    {
        active_ = active;
    }
    // A screen that makes the player "enter" the picker before the D-pad
    // turns it says so here: while not engaged the focus is a ring around the
    // whole picker instead of the highlight on one column.
    void set_engaged(bool engaged)
    {
        engaged_ = engaged;
    }
    // The edge the last handle() left through (style.exits), or none.
    Direction exit() const
    {
        return exit_;
    }

    // Up / down turn (changed / refused), left / right change column (moved,
    // refused, or none with exit() set), confirm is activated, back cancelled.
    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    struct State
    {
        int index = 0;
        // Where the wheel is, in values. It is not wrapped, so going round
        // from the last value to the first turns one step, not all the way back.
        tween::Bounce position;
    };
    bool wraps(int column) const;
    gfx::Rect well_rect() const;             // relative to the bounds' corner
    gfx::Rect column_rect(int column) const; // ... the full height of the well
    gfx::Rect middle_rect(int column) const; // ... its middle row

    std::vector<WheelColumn> columns_;
    std::vector<State> states_;
    std::string title_;
    gfx::Rect bounds_{0.0f, 0.0f, 360.0f, 220.0f};
    int column_ = 0;
    int changed_ = 0;
    int held_ = 0;
    bool active_ = false;
    bool engaged_ = true;
    bool placed_ = false;
    Direction exit_ = Direction::none;
    Highlight highlight_;
    tween::Spring active_amount_;
    tween::Spring engaged_amount_{1.0f, 0.0f, 1.0f};
    Pulse unused_; // refuse() wants a pulse; the wheel bulges instead of shaking
};

// ---- DatePicker --------------------------------------------------------------

enum class DateOrder : std::uint8_t
{
    day_month_year,
    month_day_year,
    year_month_day,
};

enum class MonthStyle : std::uint8_t
{
    number,     // 03
    short_name, // Mar
    full_name,  // March
};

struct DatePickerStyle : WheelStyle
{
    DateOrder order = DateOrder::day_month_year;
    MonthStyle months = MonthStyle::short_name;
};

// A date on three wheels. The day wheel always has as many days as the month
// has in that year: turning to February (or away from a leap year) turns the
// day back to the last one that exists.
//
//   ui::DatePicker born;
//   born.style.theme = theme;
//   born.set_years(1950, 2030);
//   born.set_date(1998, 3, 14);
//   born.set_bounds({96, 300, 360, born.preferred_height()});
//   if (born.handle(input, feedback) == ui::Event::changed) store(born.year(), ...);
class DatePicker
{
  public:
    DatePicker();

    DatePickerStyle style;

    static bool is_leap_year(int year);
    static int days_in_month(int year, int month); // month 1..12

    void set_title(std::string title)
    {
        wheel_.set_title(std::move(title));
    }
    // The first and last year of the year wheel.
    void set_years(int first, int last);
    // Silent; clamped to the years and to the month's length.
    void set_date(int year, int month, int day);
    int year() const
    {
        return year_;
    }
    int month() const
    {
        return month_;
    }
    int day() const
    {
        return day_;
    }
    // "14 Mar 1998", "Mar 14, 1998" or "1998-03-14", by style.order.
    std::string text() const;

    void set_bounds(const gfx::Rect &bounds)
    {
        wheel_.set_bounds(bounds);
    }
    const gfx::Rect &bounds() const
    {
        return wheel_.bounds();
    }
    float preferred_height() const;
    void set_active(bool active)
    {
        wheel_.set_active(active);
    }
    void set_engaged(bool engaged)
    {
        wheel_.set_engaged(engaged);
    }
    Direction exit() const
    {
        return wheel_.exit();
    }
    // The wheels themselves, for reading (which column is active, ...).
    const WheelPicker &wheel() const
    {
        return wheel_;
    }

    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    enum Part
    {
        kDay,
        kMonth,
        kYear,
    };
    int column_of(Part part) const;
    void sync();
    void rebuild();
    void fit_days();

    WheelPicker wheel_;
    int first_year_ = 1950;
    int last_year_ = 2040;
    int year_ = 2000;
    int month_ = 1;
    int day_ = 1;
    int day_count_ = 0; // the length the day wheel was built with
    DateOrder built_order_ = DateOrder::day_month_year;
    MonthStyle built_months_ = MonthStyle::short_name;
    bool built_ = false;
};

// ---- TimePicker --------------------------------------------------------------

struct TimePickerStyle : WheelStyle
{
    bool twelve_hour = false; // 1..12 and an AM / PM wheel instead of 00..23
    int minute_step = 1;      // 5 gives 00, 05, 10, ...
    std::string am = "AM";
    std::string pm = "PM";
};

// A time of day on two wheels, or three with AM / PM.
//
//   ui::TimePicker alarm;
//   alarm.style.twelve_hour = true;
//   alarm.style.minute_step = 5;
//   alarm.set_time(19, 30);
class TimePicker
{
  public:
    TimePicker();

    TimePickerStyle style;

    void set_title(std::string title)
    {
        wheel_.set_title(std::move(title));
    }
    // Silent. hour is 0..23 whatever the wheels show.
    void set_time(int hour, int minute);
    int hour() const
    {
        return hour_;
    }
    int minute() const
    {
        return minute_;
    }
    // "19:30" or "7:30 PM".
    std::string text() const;

    void set_bounds(const gfx::Rect &bounds)
    {
        wheel_.set_bounds(bounds);
    }
    const gfx::Rect &bounds() const
    {
        return wheel_.bounds();
    }
    float preferred_height() const;
    void set_active(bool active)
    {
        wheel_.set_active(active);
    }
    void set_engaged(bool engaged)
    {
        wheel_.set_engaged(engaged);
    }
    Direction exit() const
    {
        return wheel_.exit();
    }
    const WheelPicker &wheel() const
    {
        return wheel_;
    }

    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    void sync();
    void rebuild();

    WheelPicker wheel_;
    int hour_ = 12;
    int minute_ = 0;
    bool built_twelve_ = false;
    int built_step_ = 1;
    std::string built_am_;
    std::string built_pm_;
    bool built_ = false;
};

} // namespace hui::ui
