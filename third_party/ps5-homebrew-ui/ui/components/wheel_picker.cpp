// ps5-homebrew-ui - Components: WheelPicker, DatePicker and TimePicker.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/wheel_picker.hpp"

#include "ui/components/focus_frame.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

constexpr float kWellPad = 6.0f; // between the well's edge and the first and last row

constexpr const char *kMonthsShort[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
constexpr const char *kMonthsFull[12] = {"January",   "February", "March",    "April",
                                         "May",       "June",     "July",     "August",
                                         "September", "October",  "November", "December"};

int wrapped(int value, int count)
{
    return ((value % count) + count) % count;
}

std::string two_digits(int value)
{
    char text[16];
    std::snprintf(text, sizeof(text), "%02d", value);
    return text;
}

std::string number(int value)
{
    char text[16];
    std::snprintf(text, sizeof(text), "%d", value);
    return text;
}

float wheel_height(const WheelStyle &look, bool titled)
{
    return (titled ? look.title_size + look.title_gap : 0.0f) +
           static_cast<float>(std::max(look.visible, 1)) * look.row_height + 2.0f * kWellPad;
}

} // namespace

// ---- WheelPicker --------------------------------------------------------------

void WheelPicker::set_columns(std::vector<WheelColumn> columns)
{
    columns_ = std::move(columns);
    states_.resize(columns_.size());
    for (std::size_t i = 0; i < columns_.size(); ++i)
    {
        const int count = static_cast<int>(columns_[i].values.size());
        states_[i].index = std::clamp(states_[i].index, 0, std::max(count - 1, 0));
        states_[i].position.snap(static_cast<float>(states_[i].index));
    }
    column_ = std::clamp(column_, 0, std::max(column_count() - 1, 0));
    placed_ = false;
}

void WheelPicker::set_values(int column, std::vector<std::string> values)
{
    if (column < 0 || column >= column_count())
        return;
    State &state = states_[static_cast<std::size_t>(column)];
    const int before = static_cast<int>(columns_[static_cast<std::size_t>(column)].values.size());
    columns_[static_cast<std::size_t>(column)].values = std::move(values);
    const int count = static_cast<int>(columns_[static_cast<std::size_t>(column)].values.size());
    const int index = std::clamp(state.index, 0, std::max(count - 1, 0));
    // Where the wheel shows itself now, as a position in the old values.
    float shown = state.position.value - state.position.target + static_cast<float>(state.index);
    if (before > 0 && wraps(column))
        shown = std::fmod(std::fmod(shown, static_cast<float>(before)) + static_cast<float>(before),
                          static_cast<float>(before));
    state.index = index;
    state.position.target = static_cast<float>(index);
    state.position.value = std::min(shown, static_cast<float>(std::max(count - 1, 0)) + 0.5f);
}

void WheelPicker::set_index(int column, int index)
{
    if (column < 0 || column >= column_count())
        return;
    State &state = states_[static_cast<std::size_t>(column)];
    const int count = static_cast<int>(columns_[static_cast<std::size_t>(column)].values.size());
    state.index = std::clamp(index, 0, std::max(count - 1, 0));
    state.position.snap(static_cast<float>(state.index));
}

const std::string &WheelPicker::value(int column) const
{
    static const std::string kNone;
    const WheelColumn &entry = columns_[static_cast<std::size_t>(column)];
    return entry.values.empty() ? kNone
                                : entry.values[static_cast<std::size_t>(
                                      states_[static_cast<std::size_t>(column)].index)];
}

void WheelPicker::set_column(int column)
{
    column_ = std::clamp(column, 0, std::max(column_count() - 1, 0));
}

float WheelPicker::preferred_height() const
{
    return wheel_height(style, !title_.empty());
}

// A wheel shorter than the window would show the same value twice.
bool WheelPicker::wraps(int column) const
{
    const WheelColumn &entry = columns_[static_cast<std::size_t>(column)];
    return style.wrap && entry.wrap && static_cast<int>(entry.values.size()) >= style.visible;
}

Rect WheelPicker::well_rect() const
{
    const float top = title_.empty() ? 0.0f : style.title_size + style.title_gap;
    return {0.0f, top, bounds_.w,
            static_cast<float>(std::max(style.visible, 1)) * style.row_height + 2.0f * kWellPad};
}

Rect WheelPicker::column_rect(int column) const
{
    const Rect well = well_rect();
    float fixed = 0.0f;
    float weights = 0.0f;
    for (std::size_t i = 0; i < columns_.size(); ++i)
    {
        if (!columns_[i].separator.empty())
            fixed += style.separator_width;
        else if (i > 0)
            fixed += style.column_gap;
        weights += std::max(columns_[i].weight, 0.01f);
    }
    const float room = std::max(well.w - 2.0f * style.padding - fixed, 1.0f);
    float x = well.x + style.padding;
    for (int i = 0; i <= column; ++i)
    {
        const WheelColumn &entry = columns_[static_cast<std::size_t>(i)];
        if (!entry.separator.empty())
            x += style.separator_width;
        else if (i > 0)
            x += style.column_gap;
        const float w = room * std::max(entry.weight, 0.01f) / weights;
        if (i == column)
            return {x, well.y, w, well.h};
        x += w;
    }
    return well;
}

Rect WheelPicker::middle_rect(int column) const
{
    const Rect full = column_rect(column);
    return {full.x, full.cy() - style.row_height * 0.5f, full.w, style.row_height};
}

Event WheelPicker::handle(const InputFrame &input, Feedback &feedback)
{
    exit_ = Direction::none;
    const float x = columns_.empty() ? bounds_.cx() : bounds_.x + column_rect(column_).cx();
    if (input.nav == Direction::left || input.nav == Direction::right)
    {
        const int next = column_ + (input.nav == Direction::right ? 1 : -1);
        if (next < 0 || next >= column_count())
        {
            if (style.exits.allows(input.nav))
            {
                exit_ = input.nav;
                return Event::none;
            }
            return refuse(feedback, style, input, highlight_.refusal(), x);
        }
        column_ = next;
        play_cue(feedback, style, style.sounds.move, bounds_.x + column_rect(column_).cx());
        return Event::moved;
    }
    if ((input.nav == Direction::up || input.nav == Direction::down) && !columns_.empty())
    {
        State &state = states_[static_cast<std::size_t>(column_)];
        const int count =
            static_cast<int>(columns_[static_cast<std::size_t>(column_)].values.size());
        const int direction = input.nav == Direction::down ? 1 : -1;
        held_ = input.nav_repeat ? held_ + 1 : 0;
        int stride = 1;
        if (style.fast_after > 0 && held_ >= style.fast_after)
            stride = std::max(style.fast_factor, 1);
        int moved = direction * stride;
        if (count < 2)
        {
            moved = 0;
        }
        else if (!wraps(column_))
        {
            // A long stride lands on the end instead of stopping short of it.
            moved = std::clamp(state.index + moved, 0, count - 1) - state.index;
        }
        if (moved == 0)
        {
            // The wheel pushes against its end and comes back.
            if (!style.reduced_motion && !input.nav_repeat)
                state.position.kick(static_cast<float>(direction) * 5.0f);
            return refuse(feedback, style, input, unused_, x);
        }
        state.index = wrapped(state.index + moved, count);
        state.position.target += static_cast<float>(moved);
        changed_ = column_;
        play_cue(feedback, style, style.tick, x,
                 style.pitch_by_direction ? (direction > 0 ? 1.05f : 0.95f) : 1.0f);
        return Event::changed;
    }
    if (input.is_pressed(Action::confirm))
    {
        play_cue(feedback, style, style.sounds.activate, x);
        return Event::activated;
    }
    if (input.is_pressed(Action::back))
    {
        play_cue(feedback, style, style.sounds.cancel, x);
        return Event::cancelled;
    }
    return Event::none;
}

void WheelPicker::update(float dt)
{
    active_amount_.target = active_ ? 1.0f : 0.0f;
    active_amount_.update(dt, 18.0f);
    engaged_amount_.target = engaged_ ? 1.0f : 0.0f;
    engaged_amount_.update(dt, 18.0f);
    unused_.update(dt, 9.0f);
    if (columns_.empty())
        return;

    const Rect target = middle_rect(column_);
    highlight_.target(target);
    if (!placed_)
        highlight_.snap(target);
    placed_ = true;
    highlight_.update(dt, style);

    const float omega = std::max(style.omega(), 14.0f);
    const float damping = std::min(style.damping(), 0.8f);
    for (State &state : states_)
    {
        if (style.reduced_motion)
            state.position.snap(state.position.target);
        else
            state.position.update(dt, omega, damping);
        // At rest the position returns to the index, so it never grows large.
        if (state.position.value == state.position.target && state.position.velocity == 0.0f)
            state.position.snap(static_cast<float>(state.index));
    }
}

void WheelPicker::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const bool on_page = style.on_page && !style.boxed;
    const Color resting = on_page ? paint.page_text() : theme.text;
    const Color quiet = on_page ? paint.page_text_muted() : theme.text_muted;
    if (!title_.empty())
        paint.label(fit_label(paint, title_, style.title_size, bounds_.w), bounds_.x,
                    bounds_.y + style.title_size * 0.82f, style.title_size,
                    style.on_page ? paint.page_text_muted() : theme.text_muted);

    const auto placed = [&](const Rect &r)
    { return Rect{bounds_.x + r.x, bounds_.y + r.y, r.w, r.h}; };
    const Rect well = placed(well_rect());
    const float radius = std::min(theme.radius, 16.0f);
    const float active = active_amount_.value;
    const float engaged = engaged_amount_.value;
    const float ring = active * (1.0f - engaged);
    const bool ring_first = focus_frame_goes_under(theme);
    if (style.focus_ring && ring_first)
        focus_frame(canvas, theme, well, radius, ring, style.boxed);
    if (style.boxed)
        paint.well(well, radius, theme.surface_high);
    if (columns_.empty())
        return;

    const int count = column_count();
    if (style.band)
    {
        for (int c = 0; c < count; ++c)
        {
            const Rect mid = placed(middle_rect(c));
            paint.fill(mid, paint.control_radius(mid), quiet.with_alpha(0.13f * quiet.a));
        }
    }
    list.push_transform(1.0f, 0.0f, 0.0f, bounds_.x, bounds_.y);
    highlight_.draw(canvas, style, style.highlight, active * engaged);
    list.pop_transform();

    // Rows leaving through the top and bottom are cut by the well's edge.
    const float rim = std::max(theme.border, 1.0f) + 1.0f;
    list.push_clip({well.x, well.y + rim, well.w, well.h - 2.0f * rim});
    const int half = std::max(style.visible, 1) / 2;
    const float reach = static_cast<float>(half) + 0.5f;
    for (int c = 0; c < count; ++c)
    {
        const WheelColumn &entry = columns_[static_cast<std::size_t>(c)];
        const State &state = states_[static_cast<std::size_t>(c)];
        const int values = static_cast<int>(entry.values.size());
        if (values == 0)
            continue;
        const Rect full = placed(column_rect(c));
        const float focus = highlight_.coverage(middle_rect(c)) * active * engaged;
        const Color strong = Highlight::text_color(style, style.highlight, focus, resting);
        const float position = state.position.value;
        const int base = static_cast<int>(std::floor(position));
        const bool round = wraps(c);
        for (int k = base - half - 1; k <= base + half + 2; ++k)
        {
            const float d = static_cast<float>(k) - position;
            const float away = std::fabs(d);
            if (away > reach + 0.5f || (!round && (k < 0 || k >= values)))
                continue;
            const int index = round ? wrapped(k, values) : k;
            const float t = std::min(away / reach, 1.0f);
            const float emphasis = 1.0f - t;
            const float size = style.value_size * (1.0f - style.shrink * t);
            const float cy = full.cy() + d * style.row_height;
            list.push_opacity(1.0f - tween::clamp01(style.fade) * tween::smoothstep(t));
            if (item)
            {
                item(canvas, {full.x, cy - style.row_height * 0.5f, full.w, style.row_height}, c,
                     index, emphasis);
            }
            else
            {
                const Color ink = gfx::mix(strong, quiet, tween::clamp01(away));
                paint.label(
                    fit_label(paint, entry.values[static_cast<std::size_t>(index)], size, full.w),
                    full.cx(), cy + size * 0.35f, size, ink, gfx::Align::center);
            }
            list.pop_opacity();
        }
        if (!entry.separator.empty())
            paint.label(entry.separator, full.x - style.separator_width * 0.5f,
                        full.cy() + style.value_size * 0.33f, style.value_size, quiet,
                        gfx::Align::center);
    }
    list.pop_clip();

    if (style.focus_ring && !ring_first)
        focus_frame(canvas, theme, well, radius, ring, style.boxed);
}

// ---- DatePicker --------------------------------------------------------------

DatePicker::DatePicker()
{
    rebuild();
}

bool DatePicker::is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int DatePicker::days_in_month(int year, int month)
{
    constexpr int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    month = std::clamp(month, 1, 12);
    return month == 2 && is_leap_year(year) ? 29 : kDays[month - 1];
}

int DatePicker::column_of(Part part) const
{
    switch (built_order_)
    {
    case DateOrder::month_day_year:
        return part == kMonth ? 0 : (part == kDay ? 1 : 2);
    case DateOrder::year_month_day:
        return part == kYear ? 0 : (part == kMonth ? 1 : 2);
    default:
        return part == kDay ? 0 : (part == kMonth ? 1 : 2);
    }
}

// The wheels are made again when the order or the month names change; the
// date and the active part survive.
void DatePicker::rebuild()
{
    const bool had = built_;
    Part active = kDay;
    if (had)
    {
        for (Part part : {kDay, kMonth, kYear})
        {
            if (column_of(part) == wheel_.column())
                active = part;
        }
    }
    built_order_ = style.order;
    built_months_ = style.months;
    built_ = true;

    WheelColumn days;
    day_count_ = days_in_month(year_, month_);
    for (int d = 1; d <= day_count_; ++d)
        days.values.push_back(number(d));
    days.weight = 0.8f;

    WheelColumn months;
    for (int m = 0; m < 12; ++m)
        months.values.push_back(style.months == MonthStyle::number       ? two_digits(m + 1)
                                : style.months == MonthStyle::short_name ? kMonthsShort[m]
                                                                         : kMonthsFull[m]);
    months.weight = style.months == MonthStyle::full_name ? 2.0f : 1.0f;

    WheelColumn years;
    for (int y = first_year_; y <= last_year_; ++y)
        years.values.push_back(number(y));
    years.weight = 1.2f;
    years.wrap = false; // after the last year does not come the first

    std::vector<WheelColumn> columns(3);
    columns[static_cast<std::size_t>(column_of(kDay))] = std::move(days);
    columns[static_cast<std::size_t>(column_of(kMonth))] = std::move(months);
    columns[static_cast<std::size_t>(column_of(kYear))] = std::move(years);
    wheel_.set_columns(std::move(columns));
    wheel_.set_index(column_of(kDay), day_ - 1);
    wheel_.set_index(column_of(kMonth), month_ - 1);
    wheel_.set_index(column_of(kYear), year_ - first_year_);
    if (had)
        wheel_.set_column(column_of(active));
}

void DatePicker::sync()
{
    static_cast<WheelStyle &>(wheel_.style) = style;
    if (!built_ || built_order_ != style.order || built_months_ != style.months)
        rebuild();
}

void DatePicker::set_years(int first, int last)
{
    first_year_ = std::min(first, last);
    last_year_ = std::max(first, last);
    year_ = std::clamp(year_, first_year_, last_year_);
    day_ = std::min(day_, days_in_month(year_, month_));
    rebuild();
}

void DatePicker::set_date(int year, int month, int day)
{
    year_ = std::clamp(year, first_year_, last_year_);
    month_ = std::clamp(month, 1, 12);
    day_ = std::clamp(day, 1, days_in_month(year_, month_));
    rebuild();
}

// The month or the year changed: the day wheel gets that month's length, and
// a day that no longer exists turns back to the last one that does.
void DatePicker::fit_days()
{
    const int days = days_in_month(year_, month_);
    day_ = std::min(day_, days);
    if (days == day_count_)
        return;
    day_count_ = days;
    std::vector<std::string> values;
    for (int d = 1; d <= days; ++d)
        values.push_back(number(d));
    wheel_.set_values(column_of(kDay), std::move(values));
}

std::string DatePicker::text() const
{
    char buffer[48];
    const char *name = kMonthsShort[month_ - 1];
    if (style.order == DateOrder::month_day_year)
        std::snprintf(buffer, sizeof(buffer), "%s %d, %d", name, day_, year_);
    else if (style.order == DateOrder::year_month_day)
        std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", year_, month_, day_);
    else
        std::snprintf(buffer, sizeof(buffer), "%d %s %d", day_, name, year_);
    return buffer;
}

float DatePicker::preferred_height() const
{
    // The wheel's own style may not have been synchronised yet.
    return wheel_.preferred_height() - wheel_height(wheel_.style, false) +
           wheel_height(style, false);
}

Event DatePicker::handle(const InputFrame &input, Feedback &feedback)
{
    sync();
    const Event event = wheel_.handle(input, feedback);
    if (event == Event::changed)
    {
        day_ = wheel_.index(column_of(kDay)) + 1;
        month_ = wheel_.index(column_of(kMonth)) + 1;
        year_ = first_year_ + wheel_.index(column_of(kYear));
        fit_days();
    }
    return event;
}

void DatePicker::update(float dt)
{
    sync();
    wheel_.update(dt);
}

void DatePicker::draw(Canvas &canvas) const
{
    wheel_.draw(canvas);
}

// ---- TimePicker --------------------------------------------------------------

TimePicker::TimePicker()
{
    rebuild();
}

void TimePicker::rebuild()
{
    const int active = built_ ? wheel_.column() : 0;
    built_twelve_ = style.twelve_hour;
    built_step_ = std::clamp(style.minute_step, 1, 30);
    built_am_ = style.am;
    built_pm_ = style.pm;
    built_ = true;

    std::vector<WheelColumn> columns(style.twelve_hour ? 3 : 2);
    if (style.twelve_hour)
    {
        // A clock face starts at twelve.
        for (int h = 0; h < 12; ++h)
            columns[0].values.push_back(number(h == 0 ? 12 : h));
        columns[2].values = {style.am, style.pm};
        columns[2].weight = 1.1f;
    }
    else
    {
        for (int h = 0; h < 24; ++h)
            columns[0].values.push_back(two_digits(h));
    }
    for (int m = 0; m < 60; m += built_step_)
        columns[1].values.push_back(two_digits(m));
    columns[1].separator = ":";
    wheel_.set_columns(std::move(columns));
    wheel_.set_index(0, style.twelve_hour ? hour_ % 12 : hour_);
    wheel_.set_index(1, minute_ / built_step_);
    if (style.twelve_hour)
        wheel_.set_index(2, hour_ >= 12 ? 1 : 0);
    wheel_.set_column(active);
}

void TimePicker::sync()
{
    static_cast<WheelStyle &>(wheel_.style) = style;
    if (!built_ || built_twelve_ != style.twelve_hour ||
        built_step_ != std::clamp(style.minute_step, 1, 30) || built_am_ != style.am ||
        built_pm_ != style.pm)
        rebuild();
}

void TimePicker::set_time(int hour, int minute)
{
    hour_ = std::clamp(hour, 0, 23);
    minute_ = std::clamp(minute, 0, 59);
    rebuild();
}

std::string TimePicker::text() const
{
    char buffer[48];
    if (style.twelve_hour)
        std::snprintf(buffer, sizeof(buffer), "%d:%02d %s", hour_ % 12 == 0 ? 12 : hour_ % 12,
                      minute_, (hour_ >= 12 ? style.pm : style.am).c_str());
    else
        std::snprintf(buffer, sizeof(buffer), "%02d:%02d", hour_, minute_);
    return buffer;
}

float TimePicker::preferred_height() const
{
    return wheel_.preferred_height() - wheel_height(wheel_.style, false) +
           wheel_height(style, false);
}

Event TimePicker::handle(const InputFrame &input, Feedback &feedback)
{
    sync();
    const Event event = wheel_.handle(input, feedback);
    if (event == Event::changed)
    {
        if (built_twelve_)
            hour_ = wheel_.index(0) + (wheel_.index(2) == 1 ? 12 : 0);
        else
            hour_ = wheel_.index(0);
        minute_ = wheel_.index(1) * built_step_;
    }
    return event;
}

void TimePicker::update(float dt)
{
    sync();
    wheel_.update(dt);
}

void TimePicker::draw(Canvas &canvas) const
{
    wheel_.draw(canvas);
}

} // namespace hui::ui
