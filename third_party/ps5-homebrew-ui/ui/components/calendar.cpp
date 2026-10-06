// ps5-homebrew-ui - Component: Calendar.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/calendar.hpp"

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

constexpr int kColumns = 7;
constexpr int kRows = 6; // always six, so the grid's height never jumps

// Days since 1970-01-01 and back: the standard civil-calendar arithmetic,
// exact for every Gregorian date (no tables, no loops over years).
long days_from_civil(int year, int month, int day)
{
    year -= month <= 2 ? 1 : 0;
    const long era = (year >= 0 ? year : year - 399) / 400;
    const long of_era = year - era * 400;
    const long of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const long day_of_era = of_era * 365 + of_era / 4 - of_era / 100 + of_year;
    return era * 146097 + day_of_era - 719468;
}

CalendarDate civil_from_days(long days)
{
    days += 719468;
    const long era = (days >= 0 ? days : days - 146096) / 146097;
    const long day_of_era = days - era * 146097;
    const long of_era =
        (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    const long of_year = day_of_era - (365 * of_era + of_era / 4 - of_era / 100);
    const long shifted = (5 * of_year + 2) / 153;
    const int day = static_cast<int>(of_year - (153 * shifted + 2) / 5 + 1);
    const int month = static_cast<int>(shifted < 10 ? shifted + 3 : shifted - 9);
    const int year = static_cast<int>(of_era + era * 400) + (month <= 2 ? 1 : 0);
    return {year, month, day};
}

bool same_month(const CalendarDate &a, const CalendarDate &b)
{
    return a.year == b.year && a.month == b.month;
}

} // namespace

bool is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int days_in_month(int year, int month)
{
    constexpr int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12)
        return 30;
    return month == 2 && is_leap_year(year) ? 29 : kDays[month - 1];
}

int weekday_of(const CalendarDate &date)
{
    // 1970-01-01 was a Thursday.
    const long days = days_from_civil(date.year, date.month, date.day);
    return static_cast<int>(((days % 7) + 11) % 7);
}

CalendarDate add_days(const CalendarDate &date, int days)
{
    return civil_from_days(days_from_civil(date.year, date.month, date.day) + days);
}

int days_between(const CalendarDate &a, const CalendarDate &b)
{
    return static_cast<int>(days_from_civil(b.year, b.month, b.day) -
                            days_from_civil(a.year, a.month, a.day));
}

void Calendar::set_bounds(const Rect &bounds)
{
    bounds_ = bounds;
    retarget(true);
}

void Calendar::set_today(const CalendarDate &date)
{
    today_ = date;
    focus_ = date;
    slide_.snap(0.0f);
    retarget(true);
}

void Calendar::set_marks(std::vector<CalendarMark> marks)
{
    marks_ = std::move(marks);
}

void Calendar::set_limits(const CalendarDate &first, const CalendarDate &last)
{
    const CalendarDate a = first;
    const CalendarDate b = last;
    first_limit_ = a < b ? a : b;
    last_limit_ = a < b ? b : a;
    limited_ = true;
    if (focus_ < first_limit_)
        set_focus(first_limit_);
    else if (last_limit_ < focus_)
        set_focus(last_limit_);
}

void Calendar::clear_limits()
{
    limited_ = false;
}

void Calendar::set_focus(const CalendarDate &date, bool snap)
{
    const CalendarDate before = focus_;
    focus_ = date;
    focus_.month = std::clamp(focus_.month, 1, 12);
    focus_.day = std::clamp(focus_.day, 1, days_in_month(focus_.year, focus_.month));
    if (!same_month(before, focus_) && !snap)
    {
        old_year_ = before.year;
        old_month_ = before.month;
        travel_ = before < focus_ ? 1.0f : -1.0f;
        slide_.snap(1.0f);
        slide_.target = 0.0f;
    }
    else if (snap)
    {
        slide_.snap(0.0f);
    }
    retarget(snap);
}

void Calendar::select(const CalendarDate &date)
{
    start_ = date;
    has_start_ = true;
    has_end_ = false;
}

void Calendar::select_range(const CalendarDate &first, const CalendarDate &last)
{
    // Copies: `first` may be the calendar's own start.
    const CalendarDate a = first;
    const CalendarDate b = last;
    start_ = a < b ? a : b;
    end_ = a < b ? b : a;
    has_start_ = true;
    has_end_ = true;
}

void Calendar::clear_selection()
{
    has_start_ = false;
    has_end_ = false;
}

void Calendar::enter()
{
    age_ = 0.0f;
}

Rect Calendar::inner() const
{
    return style.panel ? bounds_.inset(style.padding) : bounds_;
}

Rect Calendar::grid() const
{
    const Rect in = inner();
    const float top = style.title_height + style.weekday_height;
    return {in.x, in.y + top, in.w, std::max(in.h - top, 0.0f)};
}

int Calendar::start_column() const
{
    return style.week_start == WeekStart::monday ? 1 : 0;
}

// The date in the grid's first cell: the last days of the month before fill
// the row up to the first of this one.
CalendarDate Calendar::first_cell(int year, int month) const
{
    const CalendarDate first{year, month, 1};
    const int lead = (weekday_of(first) - start_column() + 7) % 7;
    return add_days(first, -lead);
}

Rect Calendar::cell_rect(int index) const
{
    const Rect area = grid();
    const float w = (area.w - style.cell_gap * (kColumns - 1)) / kColumns;
    const float h = (area.h - style.cell_gap * (kRows - 1)) / kRows;
    const int column = index % kColumns;
    const int row = index / kColumns;
    return {area.x + static_cast<float>(column) * (w + style.cell_gap),
            area.y + static_cast<float>(row) * (h + style.cell_gap), w, h};
}

Rect Calendar::day_rect(const CalendarDate &date) const
{
    const int index = days_between(first_cell(focus_.year, focus_.month), date);
    return cell_rect(std::clamp(index, 0, kColumns * kRows - 1));
}

bool Calendar::is_disabled(const CalendarDate &date) const
{
    if (limited_ && (date < first_limit_ || last_limit_ < date))
        return true;
    return disabled && disabled(date);
}

void Calendar::retarget(bool snap)
{
    const Rect cell = day_rect(focus_);
    highlight_.target(cell);
    if (snap)
        highlight_.snap(cell);
}

Event Calendar::handle(const InputFrame &input, Feedback &feedback)
{
    exit_ = Direction::none;
    const Rect cell = day_rect(focus_);
    const float x = cell.cx();
    if (input.nav != Direction::none)
    {
        const int index = days_between(first_cell(focus_.year, focus_.month), focus_);
        const int column = index % kColumns;
        const int length = days_in_month(focus_.year, focus_.month);
        // An edge of the month's own days: the first and last column, the
        // first week, and the last day of each weekday.
        bool at_edge = false;
        int delta = 0;
        switch (input.nav)
        {
        case Direction::left:
            at_edge = column == 0;
            delta = -1;
            break;
        case Direction::right:
            at_edge = column == kColumns - 1;
            delta = 1;
            break;
        case Direction::up:
            at_edge = focus_.day <= 7;
            delta = -7;
            break;
        case Direction::down:
            at_edge = focus_.day + 7 > length;
            delta = 7;
            break;
        case Direction::none:
            break;
        }
        if (at_edge && style.exits.allows(input.nav))
        {
            exit_ = input.nav;
            return Event::none;
        }
        const CalendarDate next = add_days(focus_, delta);
        if (limited_ && (next < first_limit_ || last_limit_ < next))
            return refuse(feedback, style, input, highlight_.refusal(), x);
        const bool turned = !same_month(next, focus_);
        set_focus(next, false);
        if (turned)
            play_cue(feedback, style, style.sounds.page, bounds_.cx(), delta > 0 ? 1.05f : 0.95f);
        else
            play_cue(feedback, style, style.sounds.move, day_rect(focus_).cx(),
                     tween::lerp(1.06f, 0.95f, static_cast<float>(focus_.day) / 31.0f));
        return Event::moved;
    }
    if (input.is_pressed(Action::confirm))
    {
        if (is_disabled(focus_))
            return refuse(feedback, style, input, highlight_.refusal(), x);
        press_.trigger();
        if (style.select == CalendarSelect::none)
        {
            play_cue(feedback, style, style.sounds.activate, x);
            return Event::activated;
        }
        if (style.select == CalendarSelect::single)
        {
            select(focus_);
            play_cue(feedback, style, style.sounds.change, x);
            return Event::changed;
        }
        // A range: the first confirm (or one after a finished range) sets the
        // start, the second the end; picked backwards, the two swap.
        if (!has_start_ || has_end_)
        {
            select(focus_);
            play_cue(feedback, style, style.sounds.change, x, 0.96f);
        }
        else
        {
            select_range(start_, focus_);
            play_cue(feedback, style, style.sounds.change, x, 1.08f);
        }
        return Event::changed;
    }
    if (input.is_pressed(Action::back))
    {
        play_cue(feedback, style, style.sounds.cancel, x);
        return Event::cancelled;
    }
    return Event::none;
}

void Calendar::update(float dt)
{
    age_ += dt;
    retarget(false);
    highlight_.update(dt, style);
    slide_.update(dt, std::max(style.omega() * 0.8f, 10.0f));
    active_amount_.target = active_ ? 1.0f : 0.0f;
    active_amount_.update(dt, 18.0f);
    press_.update(dt, 10.0f);
}

void Calendar::draw_month(Canvas &canvas, Painter &paint, int year, int month, float dx,
                          float alpha, bool current) const
{
    if (alpha <= 0.01f)
        return;
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    const Inks ink = inks(paint, style.panel);
    const Rect in = inner();
    const Color tone = visible_on(theme, theme.primary, style.panel);
    const Color today_tone =
        visible_on(theme, theme.focus.a > 0.6f ? theme.accent : theme.primary, style.panel);
    list.push_opacity(alpha);
    list.push_transform(1.0f, 0.0f, 0.0f, dx, 0.0f);

    char title[64];
    std::snprintf(title, sizeof(title), "%s %d",
                  style.months[static_cast<std::size_t>(std::clamp(month, 1, 12) - 1)].c_str(),
                  year);
    paint.heading(fit_label(paint, title, style.title_size, in.w), in.x + 4.0f,
                  in.y + style.title_height * 0.5f + style.title_size * 0.3f, style.title_size,
                  ink.text);

    const CalendarDate first = first_cell(year, month);
    // In range mode with only the start set, the range runs to the focus:
    // the player sees what the second confirm would select.
    const bool preview = style.select == CalendarSelect::range && has_start_ && !has_end_;
    CalendarDate from = start_;
    CalendarDate to = has_end_ ? end_ : (preview ? focus_ : start_);
    if (to < from)
        std::swap(from, to);
    const bool ranged = style.select == CalendarSelect::range && has_start_ && from != to;

    for (int i = 0; i < kColumns * kRows; ++i)
    {
        const CalendarDate date = add_days(first, i);
        const bool inside = date.month == month;
        if (!inside && !style.outside_days)
            continue;
        const Rect cell = cell_rect(i);
        const bool off = is_disabled(date);
        const bool chosen = style.select != CalendarSelect::none && has_start_ &&
                            (date == start_ || (has_end_ && date == end_));
        const bool between = ranged && from < date && date < to;
        const bool end_cap = ranged && (date == from || date == to);
        const float radius = paint.control_radius(cell);

        // The band of a range runs through the gaps, so it reads as one.
        if (between || end_cap)
        {
            Rect band = cell;
            const int column = i % kColumns;
            const float half = style.cell_gap * 0.5f + 0.5f;
            if (date != from && column > 0)
            {
                band.x -= half;
                band.w += half;
            }
            if (date != to && column < kColumns - 1)
                band.w += half;
            if (end_cap)
            {
                // Under an end's plate only the half toward the range shows.
                if (date == from)
                {
                    band.x += band.w * 0.5f;
                    band.w *= 0.5f;
                }
                else
                {
                    band.w *= 0.5f;
                }
            }
            list.rounded_rect(band, 0.0f, tone.with_alpha(preview ? 0.16f : 0.24f));
        }
        if (chosen)
            draw_block(canvas, theme, cell, radius,
                       theme.primary.with_alpha(inside ? 1.0f : 0.55f));
        if (style.mark_today && date == today_)
            paint.stroke(cell.inset(chosen ? 3.0f : 0.0f),
                         std::max(radius - (chosen ? 3.0f : 0.0f), 0.0f), 2.0f,
                         chosen ? theme.on_primary : today_tone);

        const float focus =
            current && date == focus_ ? highlight_.coverage(cell) * active_amount_.value : 0.0f;
        Color number = chosen ? theme.on_primary
                              : Highlight::text_color(style, style.highlight, focus, ink.text);
        float strength = 1.0f;
        if (off)
            strength = 0.3f;
        else if (!inside)
            strength = 0.42f;
        if (day)
        {
            list.push_opacity(strength);
            day(canvas, cell, date, focus, chosen);
            list.pop_opacity();
            continue;
        }
        // Dots under the number, for days with something on.
        int dots = 0;
        Color dot_colors[8];
        for (const CalendarMark &mark : marks_)
        {
            if (mark.date != date || dots >= std::min(style.max_marks, 8))
                continue;
            const Color base = mark.color.a > 0.0f ? mark.color : status_color(theme, mark.status);
            dot_colors[dots++] = chosen ? theme.on_primary : visible_on(theme, base, style.panel);
        }
        char text[8];
        std::snprintf(text, sizeof(text), "%d", date.day);
        const float lift = dots > 0 ? style.mark_size * 0.5f + 1.0f : 0.0f;
        draw_number(paint, list, text, cell.cx(), cell.cy() + style.day_size * 0.35f - lift,
                    style.day_size, number.with_alpha(strength), gfx::Align::center);
        if (off && inside)
            list.rounded_rect(
                {cell.cx() - style.day_size * 0.6f, cell.cy() - lift, style.day_size * 1.2f, 1.5f},
                0.0f, number.with_alpha(0.4f));
        const float step = style.mark_size + 3.0f;
        const float dots_x = cell.cx() - step * static_cast<float>(dots - 1) * 0.5f;
        for (int d = 0; d < dots; ++d)
            draw_marker(canvas, theme, dots_x + step * static_cast<float>(d),
                        cell.y + cell.h - style.mark_size * 0.5f - 3.0f, style.mark_size * 0.5f,
                        dot_colors[d].with_alpha(strength));
    }
    list.pop_transform();
    list.pop_opacity();
}

void Calendar::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    if (style.panel)
        paint.panel(bounds_);
    const Rect in = inner();
    const Inks ink = inks(paint, style.panel);
    const float shown = tween::cubic_out(age_ / 0.3f);
    list.push_opacity(shown);

    // The weekday names stay put while the months slide under them.
    const Rect area = grid();
    const float column = (area.w - style.cell_gap * (kColumns - 1)) / kColumns;
    for (int i = 0; i < kColumns; ++i)
    {
        const std::size_t weekday = static_cast<std::size_t>((start_column() + i) % 7);
        const float cx = area.x + static_cast<float>(i) * (column + style.cell_gap) + column * 0.5f;
        paint.bounded_label(style.weekdays[weekday], cx, in.y + style.title_height + style.weekday_height * 0.5f +
                        style.weekday_size * 0.35f, style.weekday_size, ink.muted, column, gfx::Align::center);
    }

    // The months move inside the calendar, never over its neighbours.
    const float moving = slide_.value;
    const bool clipped = moving > 0.01f && !style.reduced_motion;
    if (clipped)
        list.push_clip({in.x - 2.0f, in.y - 2.0f, in.w + 4.0f, in.h + 4.0f});
    const float travel = style.reduced_motion ? 0.0f : style.slide * travel_;
    if (moving > 0.01f)
        draw_month(canvas, paint, old_year_, old_month_, -travel * (1.0f - moving),
                   tween::clamp01(moving * 2.0f - 1.0f), false);
    draw_month(canvas, paint, focus_.year, focus_.month, travel * moving,
               moving > 0.01f ? tween::clamp01(2.0f - moving * 2.0f) : 1.0f, true);
    if (clipped)
        list.pop_clip();

    // The focus goes over the plates and bands; a ring leaves the number
    // under it readable.
    HighlightStyle look = style.highlight;
    look.grow -= 2.0f * press_.value;
    // A ring normally stands off the thing it surrounds; between days there
    // is no room for that, so it is drawn on the day's own edge.
    // (A hard-shadow theme's ring also wraps the shadow it has no room for:
    // there the focus is a tinted, outlined plate.)
    if (look.kind == HighlightKind::ring && theme.style == SurfaceStyle::hard)
        look.kind = HighlightKind::tint;
    else if (look.kind == HighlightKind::ring && theme.style == SurfaceStyle::bevel)
        look.grow += 5.0f; // the dotted box sits well inside a control: bring it to the edge
    else if (look.kind == HighlightKind::ring && theme.style != SurfaceStyle::bevel)
        look.grow -= std::min(
            theme.style == SurfaceStyle::glow ? 5.0f : theme.focus_gap + theme.focus_width, 10.0f);
    draw_focus(canvas, style, highlight_, look, active_amount_.value);
    if (look.kind == HighlightKind::fill && active_amount_.value > 0.5f)
    {
        // A filled highlight covers the number: draw it again on the plate.
        char text[8];
        std::snprintf(text, sizeof(text), "%d", focus_.day);
        const Rect cell = highlight_.rect(canvas.time);
        draw_number(
            paint, list, text, cell.cx(), cell.cy() + style.day_size * 0.35f, style.day_size,
            Highlight::text_color(style, look, active_amount_.value, ink.text), gfx::Align::center);
    }
    list.pop_opacity();
}

} // namespace hui::ui
