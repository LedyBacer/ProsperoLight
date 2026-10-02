// ps5-homebrew-ui - Components: BarChart, LineChart, DonutChart and Legend.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/chart.hpp"

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

constexpr float kTau = 6.2831853f;

// A value travels a little slower than a focus ring does, and may keep a
// little of the theme's bounce: a bar that overshoots says "it grew".
void ease(tween::Bounce &value, float dt, const ComponentStyle &style)
{
    value.update(dt, std::min(std::max(style.omega() * 0.6f, 8.0f), 60.0f),
                 std::max(style.damping(), 0.72f));
}

Color series_ink(const ComponentStyle &style, const Color &given, int index, bool on_panel)
{
    if (given.a > 0.0f)
        return visible_on(style.theme, given, on_panel);
    return series_color(style.theme, index, on_panel);
}

// The first `count` characters of a name, whole UTF-8 characters only.
std::string first_letters(const std::string &name, std::size_t count)
{
    std::size_t end = 0;
    for (std::size_t seen = 0; end < name.size() && seen < count; ++seen)
    {
        ++end;
        while (end < name.size() && (static_cast<unsigned char>(name[end]) & 0xc0) == 0x80)
            ++end;
    }
    return name.substr(0, end);
}

std::string with_unit(float value, const std::string &unit)
{
    return format_value(value) + unit;
}

// Where the parts of a chart go once the panel and the title are drawn.
struct Frame
{
    Rect plot;
    Rect legend;
    bool legend_shown = false;
};

Frame begin_chart(Canvas &canvas, Painter &paint, const ChartStyle &style, const Rect &bounds,
                  const std::string &title, const Legend &legend, bool wants_legend)
{
    if (style.panel)
        paint.panel(bounds);
    const Rect in = style.panel ? bounds.inset(style.padding) : bounds;
    const Inks ink = inks(paint, style.panel);
    Frame frame;
    frame.plot = in;
    LegendPlacement placement = wants_legend ? style.legend : LegendPlacement::none;
    const float legend_width = placement == LegendPlacement::none ? 0.0f : legend.width(paint);
    const float line = style.title_size + 14.0f;

    // The legend shares the title's line when both fit; in a wide face it
    // gets a row of its own under the plot and the title keeps the line.
    const float title_width =
        title.empty() ? 0.0f : std::min(paint.label_width(title, style.title_size), in.w);
    if (placement == LegendPlacement::top && legend_width > in.w - title_width - 18.0f)
        placement = LegendPlacement::bottom;
    if (!title.empty())
        paint.label(fit_label(paint, title, style.title_size, in.w), in.x,
                    in.y + style.title_size * 0.9f, style.title_size, ink.text);
    if (!title.empty() || placement == LegendPlacement::top)
    {
        frame.plot.y += line;
        frame.plot.h -= line;
    }
    if (placement == LegendPlacement::top)
    {
        frame.legend = {in.x + in.w - legend_width, in.y, legend_width, style.title_size + 4.0f};
        frame.legend_shown = true;
    }
    else if (placement == LegendPlacement::bottom)
    {
        const float h = legend.height(paint, in.w);
        frame.legend = {in.x, in.y + in.h - h, in.w, h};
        frame.plot.h -= h + 8.0f;
        frame.legend_shown = true;
    }
    (void)canvas;
    return frame;
}

// The scale as it is drawn: the eased ends, and the ticks of the target.
struct Axis
{
    float low = 0.0f;
    float high = 1.0f;
    ChartScale ticks;

    float at(float value) const // 0 at low, 1 at high
    {
        return high > low ? (value - low) / (high - low) : 0.0f;
    }
};

int tick_count(const ChartScale &scale)
{
    if (scale.step <= 0.0f)
        return 0;
    return std::clamp(static_cast<int>(std::lround((scale.high - scale.low) / scale.step)), 0, 24);
}

float widest_tick(const Painter &paint, const AxisStyle &axis, const ChartScale &scale)
{
    float widest = 0.0f;
    for (int k = 0; k <= tick_count(scale); ++k)
        widest = std::max(
            widest, number_width(
                        paint, with_unit(scale.low + static_cast<float>(k) * scale.step, axis.unit),
                        axis.label_size));
    return widest;
}

// Grid lines and tick values for values running up the plot.
void draw_value_axis_y(Canvas &canvas, Painter &paint, const AxisStyle &axis, bool on_panel,
                       const Rect &plot, const Axis &scale)
{
    const Inks ink = inks(paint, on_panel);
    for (int k = 0; k <= tick_count(scale.ticks); ++k)
    {
        const float value = scale.ticks.low + static_cast<float>(k) * scale.ticks.step;
        const float t = scale.at(value);
        if (t < -0.01f || t > 1.01f)
            continue;
        const float y = plot.y + plot.h * (1.0f - t);
        // The line values grow from is the firm one.
        const bool base = std::fabs(value) < scale.ticks.step * 0.01f || k == 0;
        if (axis.grid || base)
            canvas.list.rounded_rect({plot.x, y - 0.75f, plot.w, 1.5f}, 0.0f,
                                     rule_color(paint, on_panel, base ? 2.2f : 1.0f));
        if (axis.labels)
            draw_number(paint, canvas.list, with_unit(value, axis.unit), plot.x - 8.0f,
                        y + axis.label_size * 0.35f, axis.label_size, ink.muted, gfx::Align::right);
    }
}

// The same for values running along the plot (horizontal bars).
void draw_value_axis_x(Canvas &canvas, Painter &paint, const AxisStyle &axis, bool on_panel,
                       const Rect &plot, const Axis &scale)
{
    const Inks ink = inks(paint, on_panel);
    for (int k = 0; k <= tick_count(scale.ticks); ++k)
    {
        const float value = scale.ticks.low + static_cast<float>(k) * scale.ticks.step;
        const float t = scale.at(value);
        if (t < -0.01f || t > 1.01f)
            continue;
        const float x = plot.x + plot.w * t;
        const bool base = std::fabs(value) < scale.ticks.step * 0.01f || k == 0;
        if (axis.grid || base)
            canvas.list.rounded_rect({x - 0.75f, plot.y, 1.5f, plot.h}, 0.0f,
                                     rule_color(paint, on_panel, base ? 2.2f : 1.0f));
        if (axis.labels)
            draw_number(paint, canvas.list, with_unit(value, axis.unit), x,
                        plot.y + plot.h + axis.label_size + 6.0f, axis.label_size, ink.muted,
                        gfx::Align::center);
    }
}

// The plate behind what has the focus, in the component's highlight style.
void draw_plate(Canvas &canvas, const ComponentStyle &style, const HighlightStyle &look,
                const Rect &r, float amount)
{
    if (amount <= 0.01f || r.w <= 0.0f || r.h <= 0.0f)
        return;
    // A Highlight draws every kind; it is placed where the chart's own
    // eased cursor says, so it needs no springs of its own.
    Highlight plate;
    plate.snap(r);
    plate.draw(canvas, style, look, amount);
}

} // namespace

float nice_step(float range, int ticks)
{
    if (!(range > 0.0f) || !std::isfinite(range))
        return 1.0f;
    const float raw = range / static_cast<float>(std::max(ticks, 1));
    const float magnitude = std::pow(10.0f, std::floor(std::log10(raw)));
    const float scaled = raw / magnitude;
    float nice = 10.0f;
    if (scaled <= 1.0f)
        nice = 1.0f;
    else if (scaled <= 2.0f)
        nice = 2.0f;
    else if (scaled <= 2.5f)
        nice = 2.5f;
    else if (scaled <= 5.0f)
        nice = 5.0f;
    return nice * magnitude;
}

ChartScale nice_scale(float low, float high, int ticks)
{
    if (!std::isfinite(low) || !std::isfinite(high))
        return {};
    if (high < low)
        std::swap(low, high);
    if (high - low < 1e-6f)
        high = low + 1.0f;
    ChartScale scale;
    scale.step = nice_step(high - low, ticks);
    // A hair of slack keeps 30 / 10 from rounding up to a fourth tick.
    scale.low = std::floor(low / scale.step + 1e-4f) * scale.step;
    scale.high = std::ceil(high / scale.step - 1e-4f) * scale.step;
    if (scale.high <= scale.low)
        scale.high = scale.low + scale.step;
    return scale;
}

// ---- Legend ----------------------------------------------------------------

void Legend::set_items(std::vector<LegendItem> items)
{
    items_ = std::move(items);
    if (emphasis_.size() != items_.size())
    {
        emphasis_.assign(items_.size(), tween::Spring{1.0f, 0.0f, 1.0f});
    }
}

float Legend::item_width(const Painter &paint, std::size_t index) const
{
    const LegendItem &item = items_[index];
    float width = style.swatch + 8.0f + paint.label_width(item.label, style.text_size);
    if (!item.value.empty())
        width += 8.0f + number_width(paint, item.value, style.text_size);
    return width;
}

float Legend::row_height() const
{
    return style.text_size + style.row_gap + 4.0f;
}

float Legend::width(const Painter &paint) const
{
    float total = 0.0f;
    for (std::size_t i = 0; i < items_.size(); ++i)
        total += item_width(paint, i) + style.gap;
    return std::max(total - style.gap, 0.0f);
}

float Legend::height(const Painter &paint, float width) const
{
    if (items_.empty())
        return 0.0f;
    if (style.vertical)
        return static_cast<float>(items_.size()) * row_height();
    int rows = 1;
    float used = 0.0f;
    for (std::size_t i = 0; i < items_.size(); ++i)
    {
        const float item = std::min(item_width(paint, i), width);
        if (used > 0.0f && used + style.gap + item > width + 0.5f)
        {
            ++rows;
            used = 0.0f;
        }
        used += (used > 0.0f ? style.gap : 0.0f) + item;
    }
    return static_cast<float>(rows) * row_height() - style.row_gap;
}

void Legend::update(float dt)
{
    for (std::size_t i = 0; i < emphasis_.size(); ++i)
    {
        const bool lit = highlight_ < 0 || static_cast<int>(i) == highlight_;
        emphasis_[i].target = lit ? 1.0f : 0.0f;
        emphasis_[i].update(dt, 16.0f);
    }
}

void Legend::draw(Canvas &canvas) const
{
    draw_at(canvas, bounds_);
}

void Legend::draw_at(Canvas &canvas, const Rect &bounds) const
{
    if (items_.empty())
        return;
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const Inks ink = inks(paint, style.on_panel);
    const float size = style.text_size;
    const float row = row_height();
    const auto alpha = [&](std::size_t i)
    {
        const float lit = i < emphasis_.size() ? emphasis_[i].value : 1.0f;
        return tween::lerp(style.dim, 1.0f, lit);
    };
    const auto swatch = [&](float x, float cy, const LegendItem &item, float a)
    {
        const float half = style.swatch * 0.5f;
        draw_marker(canvas, theme, x + half, cy, half, item.color.with_alpha(a));
    };

    if (style.vertical)
    {
        for (std::size_t i = 0; i < items_.size(); ++i)
        {
            const LegendItem &item = items_[i];
            const float cy = bounds.y + (static_cast<float>(i) + 0.5f) * row;
            if (cy + row * 0.5f > bounds.y + bounds.h + 1.0f)
                break;
            const float a = alpha(i);
            swatch(bounds.x, cy, item, a);
            float right = bounds.x + bounds.w;
            if (!item.value.empty())
                right -= draw_number(paint, list, item.value, right, cy + size * 0.35f, size,
                                     ink.text.with_alpha(a), gfx::Align::right) +
                         10.0f;
            const float left = bounds.x + style.swatch + 10.0f;
            paint.label(fit_label(paint, item.label, size, std::max(right - left, 20.0f)), left,
                        cy + size * 0.35f, size, ink.muted.with_alpha(a));
        }
        return;
    }

    // Rows of items; a row that would overflow wraps.
    std::vector<float> widths(items_.size());
    for (std::size_t i = 0; i < items_.size(); ++i)
        widths[i] = std::min(item_width(paint, i), bounds.w);
    std::size_t first = 0;
    float y = bounds.y;
    while (first < items_.size() && y + row <= bounds.y + bounds.h + row * 0.5f)
    {
        std::size_t end = first;
        float used = 0.0f;
        while (end < items_.size() &&
               (end == first || used + style.gap + widths[end] <= bounds.w + 0.5f))
        {
            used += (end == first ? 0.0f : style.gap) + widths[end];
            ++end;
        }
        float x = bounds.x;
        if (style.align == gfx::Align::right)
            x = bounds.x + bounds.w - used;
        else if (style.align == gfx::Align::center)
            x = bounds.x + (bounds.w - used) * 0.5f;
        const float cy = y + row * 0.5f - style.row_gap * 0.5f;
        for (std::size_t i = first; i < end; ++i)
        {
            const LegendItem &item = items_[i];
            const float a = alpha(i);
            swatch(x, cy, item, a);
            float text_x = x + style.swatch + 8.0f;
            // Half a pixel of slack: the room is a difference of sums, and a
            // label must not lose its end to rounding.
            float room = widths[i] - style.swatch - 8.0f + 0.5f;
            if (!item.value.empty())
                room -= 8.0f + number_width(paint, item.value, size);
            text_x += paint.label(fit_label(paint, item.label, size, std::max(room, 16.0f)), text_x,
                                  cy + size * 0.35f, size, ink.muted.with_alpha(a));
            if (!item.value.empty())
                draw_number(paint, list, item.value, text_x + 8.0f, cy + size * 0.35f, size,
                            ink.text.with_alpha(a));
            x += widths[i] + style.gap;
        }
        first = end;
        y += row;
    }
}

// ---- BarChart --------------------------------------------------------------

int BarChart::categories() const
{
    std::size_t count = categories_.size();
    for (const ChartSeries &series : series_)
        count = std::max(count, series.values.size());
    return static_cast<int>(count);
}

float BarChart::target(int series, int category) const
{
    const ChartSeries &s = series_[static_cast<std::size_t>(series)];
    return category < static_cast<int>(s.values.size())
               ? s.values[static_cast<std::size_t>(category)]
               : 0.0f;
}

float BarChart::shown(int series, int category) const
{
    const std::size_t at = static_cast<std::size_t>(series * categories() + category);
    return at < values_.size() ? values_[at].value : 0.0f;
}

void BarChart::set_categories(std::vector<std::string> categories)
{
    const int before = this->categories();
    categories_ = std::move(categories);
    const int count = this->categories();
    if (count != before)
    {
        // Another number of categories: the bars cannot be matched up, so
        // they start again from their values.
        const int rows = static_cast<int>(series_.size());
        values_.assign(static_cast<std::size_t>(rows * count), tween::Bounce{});
        for (int s = 0; s < rows; ++s)
        {
            for (int c = 0; c < count; ++c)
                values_[static_cast<std::size_t>(s * count + c)].snap(target(s, c));
        }
        category_ = std::clamp(category_, 0, std::max(count - 1, 0));
    }
    retarget();
}

void BarChart::set_series(std::vector<ChartSeries> series, bool snap)
{
    const int old_series = static_cast<int>(series_.size());
    const int old_categories = categories();
    const std::vector<tween::Bounce> old = std::move(values_);
    series_ = std::move(series);
    const int count = categories();
    const int rows = static_cast<int>(series_.size());
    values_.assign(static_cast<std::size_t>(rows * count), tween::Bounce{});
    for (int s = 0; s < rows; ++s)
    {
        for (int c = 0; c < count; ++c)
        {
            tween::Bounce &value = values_[static_cast<std::size_t>(s * count + c)];
            // A bar that was there keeps its height and eases to the new one;
            // a new bar grows from nothing.
            if (s < old_series && c < old_categories)
                value = old[static_cast<std::size_t>(s * old_categories + c)];
            value.target = target(s, c);
            if (snap)
                value.snap(value.target);
        }
    }
    category_ = std::clamp(category_, 0, std::max(count - 1, 0));
    member_ = std::clamp(member_, 0, std::max(rows - 1, 0));
    retarget();
    if (snap || !scale_set_)
    {
        high_.snap(scale_.high);
        low_.snap(scale_.low);
        scale_set_ = true;
        cursor_.snap(cursor_.target);
    }
}

// The scale and the legend follow the data; called whenever it changes.
void BarChart::retarget()
{
    const int count = categories();
    const int rows = static_cast<int>(series_.size());
    float low = 0.0f;
    float high = 0.0f;
    bool first = true;
    for (int c = 0; c < count; ++c)
    {
        float sum = 0.0f;
        for (int s = 0; s < rows; ++s)
        {
            const float value = target(s, c);
            sum += std::max(value, 0.0f);
            if (style.layout == BarLayout::grouped)
            {
                low = first ? value : std::min(low, value);
                high = first ? value : std::max(high, value);
                first = false;
            }
        }
        if (style.layout == BarLayout::stacked)
        {
            low = first ? sum : std::min(low, sum);
            high = first ? sum : std::max(high, sum);
            first = false;
        }
    }
    if (style.axis.from_zero || style.layout == BarLayout::stacked)
    {
        low = std::min(low, 0.0f);
        high = std::max(high, 0.0f);
    }
    scale_ = nice_scale(low, high, style.axis.ticks);
    high_.target = scale_.high;
    low_.target = scale_.low;
    cursor_.target = style.layout == BarLayout::stacked
                         ? static_cast<float>(category_)
                         : static_cast<float>(category_ * std::max(rows, 1) + member_);

    std::vector<LegendItem> items;
    for (int s = 0; s < rows; ++s)
        items.push_back(
            {series_[static_cast<std::size_t>(s)].name,
             series_ink(style, series_[static_cast<std::size_t>(s)].color, s, style.panel), ""});
    legend_.set_items(std::move(items));
    legend_.style.theme = style.theme;
    legend_.style.text_size = style.legend_size;
    legend_.style.on_panel = style.panel;
    legend_.style.align =
        style.legend == LegendPlacement::bottom ? gfx::Align::center : gfx::Align::right;
    legend_.set_highlight(style.layout == BarLayout::grouped && rows > 1 && active_ ? member_ : -1);
}

void BarChart::set_focus(int category, int series)
{
    category_ = std::clamp(category, 0, std::max(categories() - 1, 0));
    member_ = std::clamp(series, 0, std::max(static_cast<int>(series_.size()) - 1, 0));
    retarget();
    cursor_.snap(cursor_.target);
}

float BarChart::focused_value() const
{
    if (series_.empty() || categories() == 0)
        return 0.0f;
    if (style.layout == BarLayout::grouped)
        return target(member_, category_);
    float sum = 0.0f;
    for (int s = 0; s < static_cast<int>(series_.size()); ++s)
        sum += std::max(target(s, category_), 0.0f);
    return sum;
}

void BarChart::enter()
{
    age_ = 0.0f;
}

Event BarChart::handle(const InputFrame &input, Feedback &feedback)
{
    exit_ = Direction::none;
    const int count = categories();
    const int rows = static_cast<int>(series_.size());
    if (count == 0 || rows == 0)
        return Event::none;
    const float x = bounds_.cx();
    if (input.nav != Direction::none)
    {
        const Direction forward = style.horizontal ? Direction::down : Direction::right;
        const Direction backward = style.horizontal ? Direction::up : Direction::left;
        const bool stacked = style.layout == BarLayout::stacked;
        const int total = stacked ? count : count * rows;
        const int index = stacked ? category_ : category_ * rows + member_;
        int next = -1;
        if (input.nav == forward)
            next = index + 1;
        else if (input.nav == backward)
            next = index - 1;
        if (next < 0 || next >= total)
        {
            if (style.exits.allows(input.nav))
            {
                exit_ = input.nav;
                return Event::none;
            }
            return refuse(feedback, style, input, refusal_, x);
        }
        category_ = stacked ? next : next / rows;
        member_ = stacked ? member_ : next % rows;
        retarget();
        // A taller bar sounds higher.
        const float range = std::max(scale_.high - scale_.low, 1e-6f);
        const float level = tween::clamp01((focused_value() - scale_.low) / range);
        play_cue(feedback, style, style.sounds.move, x, tween::lerp(0.94f, 1.1f, level));
        return Event::moved;
    }
    if (input.is_pressed(Action::confirm))
    {
        press_.trigger();
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

void BarChart::update(float dt)
{
    age_ += dt;
    retarget(); // a knob (layout, ticks, theme) may have changed
    for (tween::Bounce &value : values_)
        ease(value, dt, style);
    const float omega = std::min(std::max(style.omega() * 0.6f, 8.0f), 60.0f);
    high_.update(dt, omega);
    low_.update(dt, omega);
    cursor_.update(dt, std::max(style.omega(), 18.0f));
    active_amount_.target = active_ ? 1.0f : 0.0f;
    active_amount_.update(dt, 18.0f);
    refusal_.update(dt, 9.0f);
    press_.update(dt, 10.0f);
    legend_.update(dt);
}

void BarChart::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const int count = categories();
    const int rows = static_cast<int>(series_.size());
    const Frame frame = begin_chart(canvas, paint, style, bounds_, title, legend_, rows > 1);
    if (frame.legend_shown)
        legend_.draw_at(canvas, frame.legend);
    if (count == 0 || rows == 0)
        return;

    const Inks ink = inks(paint, style.panel);
    const AxisStyle &axis = style.axis;
    Axis scale;
    scale.low = low_.value;
    scale.high = std::max(high_.value, low_.value + 1e-6f);
    scale.ticks = scale_;
    const bool stacked = style.layout == BarLayout::stacked;
    const bool across = style.horizontal;

    // ---- the plot, inside the room the axis text takes ----
    Rect plot = frame.plot;
    if (across)
    {
        float names = 0.0f;
        if (axis.labels)
        {
            for (const std::string &name : categories_)
                names = std::max(names, paint.label_width(name, axis.label_size));
            names = std::min(names, plot.w * 0.3f) + 10.0f;
        }
        const float below = axis.labels ? axis.label_size + 10.0f : 0.0f;
        // Half a tick label hangs past each end of the axis.
        const float half = axis.labels ? widest_tick(paint, axis, scale_) * 0.5f : 0.0f;
        plot = {plot.x + std::max(names, half), plot.y + 2.0f,
                plot.w - std::max(names, half) - half - 2.0f, plot.h - below - 2.0f};
        draw_value_axis_x(canvas, paint, axis, style.panel, plot, scale);
    }
    else
    {
        const float left = axis.labels ? widest_tick(paint, axis, scale_) + 10.0f : 0.0f;
        const float below = axis.labels ? axis.label_size + 10.0f : 0.0f;
        // A figure over the tallest bar needs room above the plot.
        const float above = style.value_labels ? axis.label_size + 8.0f : axis.label_size * 0.5f;
        plot = {plot.x + left, plot.y + above, plot.w - left, plot.h - below - above};
        draw_value_axis_y(canvas, paint, axis, style.panel, plot, scale);
    }
    if (plot.w < 8.0f || plot.h < 8.0f)
        return;

    const float along = across ? plot.h : plot.w;
    const float slot = along / static_cast<float>(count);
    const float fill = slot * std::clamp(style.slot_fill, 0.1f, 1.0f);
    const int members = stacked ? 1 : rows;
    const float bar = std::max((fill - style.bar_gap * static_cast<float>(members - 1)) /
                                   static_cast<float>(members),
                               2.0f);
    const float start = across ? plot.y : plot.x;
    // Where bar `member` of category `c` starts along the category axis.
    const auto bar_start = [&](int c, int member)
    {
        return start + static_cast<float>(c) * slot + (slot - fill) * 0.5f +
               static_cast<float>(member) * (bar + style.bar_gap);
    };
    // A bar from value v0 to v1 at `from` along the category axis.
    const auto bar_rect = [&](float from, float v0, float v1)
    {
        const float a = tween::clamp01(scale.at(std::min(v0, v1)));
        const float b = tween::clamp01(scale.at(std::max(v0, v1)));
        if (across)
            return Rect{plot.x + plot.w * a, from, plot.w * (b - a), bar};
        return Rect{from, plot.y + plot.h * (1.0f - b), bar, plot.h * (b - a)};
    };
    const auto grow = [&](int c)
    {
        if (style.reduced_motion || style.entrance <= 0.0f)
            return 1.0f;
        return tween::stagger(age_, c, style.grow_step, style.entrance);
    };
    const float fade = style.reduced_motion ? tween::cubic_out(age_ / 0.2f) : 1.0f;

    // ---- the plate behind the focus ----
    const float cursor =
        std::clamp(cursor_.value, 0.0f, static_cast<float>(stacked ? count - 1 : count * rows - 1));
    const auto cursor_start = [&](float at)
    {
        const int index = static_cast<int>(at);
        return stacked ? bar_start(index, 0) : bar_start(index / rows, index % rows);
    };
    const float lower = std::floor(cursor);
    const float focus_start =
        tween::lerp(cursor_start(lower), cursor_start(std::ceil(cursor)), cursor - lower) +
        shake(refusal_.value, canvas.time, 8.0f);
    {
        const float grow_by = 5.0f - 2.0f * press_.value;
        const Rect plate = across
                               ? Rect{plot.x, focus_start - grow_by, plot.w, bar + 2.0f * grow_by}
                               : Rect{focus_start - grow_by, plot.y, bar + 2.0f * grow_by, plot.h};
        draw_plate(canvas, style, style.highlight, plate,
                   (0.25f + 0.75f * active_amount_.value) * fade);
    }

    // ---- the bars ----
    const float radius = std::min(style.radius, theme.radius);
    const float base_value = std::clamp(0.0f, scale.low, scale.high);
    list.push_opacity(fade);
    for (int c = 0; c < count; ++c)
    {
        const float g = grow(c);
        float stack = 0.0f;
        for (int s = 0; s < rows; ++s)
        {
            const float value = shown(s, c) * g;
            const int index = stacked ? c : c * rows + s;
            const float lit = tween::clamp01(1.0f - std::fabs(cursor - static_cast<float>(index)));
            // The bars beside the focus step back a little while the chart
            // has it, so the focused one reads first.
            const float dim = 1.0f - 0.26f * active_amount_.value * (1.0f - lit);
            const Color color =
                series_ink(style, series_[static_cast<std::size_t>(s)].color, s, style.panel)
                    .with_alpha(dim);
            Rect r;
            if (stacked)
            {
                const float top = stack + std::max(value, 0.0f);
                r = bar_rect(bar_start(c, 0), stack, top);
                stack = top;
                // A hairline of the ground between the parts of a stack.
                if (s > 0 && (across ? r.w : r.h) > 3.0f)
                {
                    if (across)
                    {
                        r.x += 1.5f;
                        r.w -= 1.5f;
                    }
                    else
                    {
                        r.h -= 1.5f;
                    }
                }
            }
            else
            {
                r = bar_rect(bar_start(c, s), base_value, value);
            }
            draw_block(canvas, theme, r, radius, color);
            if (style.value_labels && (!stacked || s == rows - 1))
            {
                const float figure = stacked ? stack : value;
                const std::string text = with_unit(std::round(figure * 10.0f) / 10.0f, axis.unit);
                const float size = axis.label_size;
                const Color tone = gfx::mix(ink.muted, ink.text, lit * active_amount_.value);
                if (across)
                    draw_number(paint, list, text, r.x + r.w + 6.0f, r.cy() + size * 0.35f, size,
                                tone.with_alpha(g));
                else if (number_width(paint, text, size) <= (stacked ? fill : bar) + 10.0f)
                    draw_number(paint, list, text, r.cx(), r.y - 6.0f, size, tone.with_alpha(g),
                                gfx::Align::center);
            }
        }
    }
    list.pop_opacity();

    // ---- category names ----
    // Names that do not fit their slot are shortened together, to three
    // letters, two or one: "Mon Tue W..." would be worse than "M T W".
    std::size_t letters = 64;
    if (axis.labels && !across)
    {
        for (const std::size_t limit :
             {std::size_t{64}, std::size_t{3}, std::size_t{2}, std::size_t{1}})
        {
            letters = limit;
            bool fits = true;
            for (const std::string &name : categories_)
                fits = fits && paint.label_width(first_letters(name, limit), axis.label_size) <=
                                   slot - 5.0f;
            if (fits)
                break;
        }
    }
    if (axis.labels)
    {
        for (int c = 0; c < count && c < static_cast<int>(categories_.size()); ++c)
        {
            const float lit = c == category_ ? 1.0f : 0.0f;
            const Color tone = gfx::mix(ink.muted, ink.text, lit * active_amount_.value);
            const std::string name =
                first_letters(categories_[static_cast<std::size_t>(c)], letters);
            const float centre = start + (static_cast<float>(c) + 0.5f) * slot;
            if (across)
                paint.label(fit_label(paint, name, axis.label_size, plot.x - frame.plot.x - 8.0f),
                            plot.x - 10.0f, centre + axis.label_size * 0.35f, axis.label_size, tone,
                            gfx::Align::right);
            else
                paint.label(name, centre, plot.y + plot.h + axis.label_size + 6.0f, axis.label_size,
                            tone, gfx::Align::center);
        }
    }

    // ---- the readout ----
    if (style.readout && active_amount_.value > 0.01f)
    {
        const int c = category_;
        std::string text;
        if (!stacked && rows > 1)
            text = series_[static_cast<std::size_t>(member_)].name + " ";
        if (c < static_cast<int>(categories_.size()))
            text += categories_[static_cast<std::size_t>(c)] + ": ";
        text += with_unit(focused_value(), axis.unit);
        float value = 0.0f;
        if (stacked)
        {
            for (int s = 0; s < rows; ++s)
                value += std::max(shown(s, c), 0.0f);
        }
        else
        {
            value = shown(member_, c);
        }
        const Rect r = bar_rect(focus_start, base_value, value * grow(c));
        const Rect keep = style.panel ? bounds_.inset(4.0f) : bounds_;
        if (across)
            draw_readout(canvas, style, text, std::min(r.x + r.w + 40.0f, plot.x + plot.w), r.y,
                         style.readout_size, keep, active_amount_.value * fade);
        else
            draw_readout(canvas, style, text, r.cx(), r.y - 4.0f, style.readout_size, keep,
                         active_amount_.value * fade);
    }
}

// ---- LineChart -------------------------------------------------------------

int LineChart::steps() const
{
    std::size_t count = 0;
    for (const ChartSeries &series : series_)
        count = std::max(count, series.values.size());
    return static_cast<int>(count);
}

float LineChart::shown(int series, int step) const
{
    const std::size_t at = static_cast<std::size_t>(series * steps() + step);
    return at < values_.size() ? values_[at].value : 0.0f;
}

void LineChart::set_labels(std::vector<std::string> labels)
{
    labels_ = std::move(labels);
}

void LineChart::set_series(std::vector<ChartSeries> series, bool snap)
{
    const int old_series = static_cast<int>(series_.size());
    const int old_steps = steps();
    const std::vector<tween::Bounce> old = std::move(values_);
    series_ = std::move(series);
    const int count = steps();
    const int rows = static_cast<int>(series_.size());
    values_.assign(static_cast<std::size_t>(rows * count), tween::Bounce{});
    for (int s = 0; s < rows; ++s)
    {
        const std::vector<float> &given = series_[static_cast<std::size_t>(s)].values;
        for (int i = 0; i < count; ++i)
        {
            const float target = i < static_cast<int>(given.size())
                                     ? given[static_cast<std::size_t>(i)]
                                     : (given.empty() ? 0.0f : given.back());
            tween::Bounce &value = values_[static_cast<std::size_t>(s * count + i)];
            // A new line rises from where the old one was, or from its own
            // level when there was none: a line growing out of zero would
            // read as a spike.
            if (s < old_series && i < old_steps)
                value = old[static_cast<std::size_t>(s * old_steps + i)];
            else
                value.snap(target);
            value.target = target;
            if (snap)
                value.snap(target);
        }
    }
    rescale();
    cursor_ = std::clamp(cursor_, 0, std::max(count - 1, 0));
    cursor_x_.target = static_cast<float>(cursor_);
    if (snap || !scale_set_)
    {
        high_.snap(scale_.high);
        low_.snap(scale_.low);
        cursor_x_.snap(cursor_x_.target);
        scale_set_ = true;
    }

    sync_legend();
}

// The scale follows the data and the axis knobs; called whenever either may
// have changed.
void LineChart::rescale()
{
    float low = 0.0f;
    float high = 0.0f;
    bool first = true;
    for (const tween::Bounce &value : values_)
    {
        low = first ? value.target : std::min(low, value.target);
        high = first ? value.target : std::max(high, value.target);
        first = false;
    }
    if (style.axis.from_zero)
    {
        low = std::min(low, 0.0f);
        high = std::max(high, 0.0f);
    }
    scale_ = nice_scale(low, high, style.axis.ticks);
    high_.target = scale_.high;
    low_.target = scale_.low;
}

void LineChart::sync_legend()
{
    std::vector<LegendItem> items;
    for (std::size_t s = 0; s < series_.size(); ++s)
        items.push_back({series_[s].name,
                         series_ink(style, series_[s].color, static_cast<int>(s), style.panel),
                         ""});
    legend_.set_items(std::move(items));
    legend_.style.theme = style.theme;
    legend_.style.text_size = style.legend_size;
    legend_.style.on_panel = style.panel;
    legend_.style.align =
        style.legend == LegendPlacement::bottom ? gfx::Align::center : gfx::Align::right;
}

void LineChart::set_focus(int step)
{
    cursor_ = std::clamp(step, 0, std::max(steps() - 1, 0));
    cursor_x_.snap(static_cast<float>(cursor_));
}

void LineChart::enter()
{
    age_ = 0.0f;
}

Event LineChart::handle(const InputFrame &input, Feedback &feedback)
{
    exit_ = Direction::none;
    const int count = steps();
    if (count == 0)
        return Event::none;
    const float x = bounds_.cx();
    if (input.nav != Direction::none)
    {
        int next = -1;
        if (input.nav == Direction::right)
            next = cursor_ + 1;
        else if (input.nav == Direction::left)
            next = cursor_ - 1;
        if (next < 0 || next >= count)
        {
            if (style.exits.allows(input.nav))
            {
                exit_ = input.nav;
                return Event::none;
            }
            return refuse(feedback, style, input, refusal_, x);
        }
        cursor_ = next;
        cursor_x_.target = static_cast<float>(cursor_);
        // The tick follows the first line: up the chart, up the scale.
        const float range = std::max(scale_.high - scale_.low, 1e-6f);
        const float level = tween::clamp01(
            (values_[static_cast<std::size_t>(cursor_)].target - scale_.low) / range);
        const float along =
            count > 1 ? static_cast<float>(cursor_) / static_cast<float>(count - 1) : 0.5f;
        play_cue(feedback, style, style.sounds.move,
                 bounds_.x + bounds_.w * tween::lerp(0.2f, 0.9f, along),
                 tween::lerp(0.94f, 1.1f, level));
        return Event::moved;
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

void LineChart::update(float dt)
{
    age_ += dt;
    rescale(); // a knob (ticks, from_zero) may have changed
    for (tween::Bounce &value : values_)
        ease(value, dt, style);
    const float omega = std::min(std::max(style.omega() * 0.6f, 8.0f), 60.0f);
    high_.update(dt, omega);
    low_.update(dt, omega);
    cursor_x_.update(dt, std::max(style.omega(), 18.0f));
    active_amount_.target = active_ ? 1.0f : 0.0f;
    active_amount_.update(dt, 18.0f);
    refusal_.update(dt, 9.0f);

    sync_legend();
    legend_.update(dt);
}

void LineChart::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const int count = steps();
    const int rows = static_cast<int>(series_.size());
    const Frame frame = begin_chart(canvas, paint, style, bounds_, title, legend_, rows > 1);
    if (frame.legend_shown)
        legend_.draw_at(canvas, frame.legend);
    if (count == 0 || rows == 0)
        return;

    const Inks ink = inks(paint, style.panel);
    const AxisStyle &axis = style.axis;
    Axis scale;
    scale.low = low_.value;
    scale.high = std::max(high_.value, low_.value + 1e-6f);
    scale.ticks = scale_;
    const float left = axis.labels ? widest_tick(paint, axis, scale_) + 10.0f : 0.0f;
    const float below = axis.labels ? axis.label_size + 10.0f : 0.0f;
    const Rect plot{frame.plot.x + left, frame.plot.y + axis.label_size * 0.5f, frame.plot.w - left,
                    frame.plot.h - below - axis.label_size * 0.5f};
    if (plot.w < 8.0f || plot.h < 8.0f)
        return;
    draw_value_axis_y(canvas, paint, axis, style.panel, plot, scale);

    // The first and last points sit a little inside the plot, so their dots
    // and the line's round ends are not cut by it.
    constexpr float kInset = 8.0f;
    const float x0 = plot.x + kInset;
    const float span = std::max(plot.w - 2.0f * kInset, 1.0f);
    const auto x_at = [&](float step)
    { return count > 1 ? x0 + span * step / static_cast<float>(count - 1) : plot.cx(); };
    const auto y_at = [&](float value)
    { return plot.y + plot.h * (1.0f - tween::clamp01(scale.at(value))); };
    const bool stepped = style.shape == LineShape::stepped ||
                         (style.shape == LineShape::theme && theme.corner == Corner::pixel);
    // The value of a series at a fractional step.
    const auto value_at = [&](int s, float step)
    {
        const float clamped = std::clamp(step, 0.0f, static_cast<float>(count - 1));
        const int lower = static_cast<int>(clamped);
        const int upper = std::min(lower + 1, count - 1);
        if (stepped)
            return shown(s, lower);
        return tween::lerp(shown(s, lower), shown(s, upper), clamped - static_cast<float>(lower));
    };

    const bool plain = style.reduced_motion || style.entrance <= 0.0f;
    const float reveal = plain ? 1.0f : tween::cubic_out(age_ / (style.entrance * 1.3f));
    const float fade = style.reduced_motion ? tween::cubic_out(age_ / 0.2f) : 1.0f;
    const float limit = x0 + span * reveal; // nothing is drawn right of this yet
    list.push_opacity(fade);

    // ---- the fills, under every line ----
    if (style.area && count > 1)
    {
        const float base = plot.y + plot.h;
        const float width = std::max(style.area_step, 1.0f);
        const int strips = static_cast<int>(std::ceil((limit - x0) / width));
        for (int s = 0; s < rows; ++s)
        {
            const Color color =
                series_ink(style, series_[static_cast<std::size_t>(s)].color, s, style.panel);
            // More lines, fainter fills: they overlap.
            const float strength = style.area_alpha / (1.0f + 0.5f * static_cast<float>(rows - 1));
            for (int k = 0; k < strips; ++k)
            {
                const float x = x0 + static_cast<float>(k) * width;
                const float w = std::min(width, limit - x);
                const float step = (x + w * 0.5f - x0) / span * static_cast<float>(count - 1);
                const float y = y_at(value_at(s, step));
                if (base - y > 0.5f && w > 0.0f)
                    list.gradient_rect({x, y, w + 0.5f, base - y}, 0.0f, color.with_alpha(strength),
                                       color.with_alpha(0.0f));
            }
        }
    }

    // ---- the lines ----
    for (int s = 0; s < rows; ++s)
    {
        const Color color =
            series_ink(style, series_[static_cast<std::size_t>(s)].color, s, style.panel);
        const float width = style.line_width;
        if (count == 1)
        {
            draw_marker(canvas, theme, plot.cx(), y_at(shown(s, 0)), style.point_size, color);
            continue;
        }
        for (int i = 0; i + 1 < count; ++i)
        {
            const float xa = x_at(static_cast<float>(i));
            float xb = x_at(static_cast<float>(i + 1));
            if (xa >= limit)
                break;
            const float ya = y_at(shown(s, i));
            float yb = y_at(shown(s, i + 1));
            const bool cut = xb > limit;
            if (cut)
            {
                yb = tween::lerp(ya, yb, (limit - xa) / (xb - xa));
                xb = limit;
            }
            if (stepped)
            {
                // Level, then a jump: axis-aligned pieces stay crisp.
                list.rounded_rect({xa - width * 0.5f, ya - width * 0.5f, xb - xa + width, width},
                                  0.0f, color);
                if (!cut)
                {
                    const float jump = y_at(shown(s, i + 1));
                    list.rounded_rect({xb - width * 0.5f, std::min(ya, jump) - width * 0.5f, width,
                                       std::fabs(jump - ya) + width},
                                      0.0f, color);
                }
            }
            else
            {
                list.line(xa, ya, xb, yb, width, color);
            }
        }
        if (style.points)
        {
            for (int i = 0; i < count; ++i)
            {
                const float x = x_at(static_cast<float>(i));
                if (x <= limit + 0.5f)
                    draw_marker(canvas, theme, x, y_at(shown(s, i)), style.point_size, color);
            }
        }
    }
    list.pop_opacity();

    // ---- step names: as many as fit ----
    if (axis.labels && !labels_.empty())
    {
        float widest = 0.0f;
        for (const std::string &label : labels_)
            widest = std::max(widest, paint.label_width(label, axis.label_size));
        const int fit = std::max(static_cast<int>(plot.w / (widest + 18.0f)), 1);
        const int every = std::max((count + fit - 1) / fit, 1);
        for (int i = 0; i < count && i < static_cast<int>(labels_.size()); i += every)
        {
            const float lit =
                tween::clamp01(1.0f - std::fabs(cursor_x_.value - static_cast<float>(i)));
            const float half =
                paint.label_width(labels_[static_cast<std::size_t>(i)], axis.label_size) * 0.5f;
            const float x =
                std::clamp(x_at(static_cast<float>(i)), plot.x + half, plot.x + plot.w - half);
            paint.label(labels_[static_cast<std::size_t>(i)], x,
                        plot.y + plot.h + axis.label_size + 6.0f, axis.label_size,
                        gfx::mix(ink.muted, ink.text, lit * active_amount_.value),
                        gfx::Align::center);
        }
    }

    // ---- the cursor ----
    const float amount = active_amount_.value * fade;
    if (amount > 0.01f)
    {
        const float at = std::clamp(cursor_x_.value, 0.0f, static_cast<float>(count - 1));
        const float x = x_at(at) + shake(refusal_.value, canvas.time, 8.0f);
        list.rounded_rect({x - 1.0f, plot.y, 2.0f, plot.h}, 0.0f,
                          ink.muted.with_alpha(0.7f * amount));
        const Color ground = ground_color(theme, style.panel);
        float top = plot.y + plot.h;
        for (int s = 0; s < rows; ++s)
        {
            const Color color =
                series_ink(style, series_[static_cast<std::size_t>(s)].color, s, style.panel);
            const float y = y_at(value_at(s, at));
            top = std::min(top, y);
            // A rim in the ground colour parts the dot from the line.
            draw_marker(canvas, theme, x, y, style.point_size + 5.0f, ground.with_alpha(amount));
            draw_marker(canvas, theme, x, y, style.point_size + 2.0f, color.with_alpha(amount));
        }
        if (style.readout)
        {
            std::string text;
            if (cursor_ < static_cast<int>(labels_.size()))
                text = labels_[static_cast<std::size_t>(cursor_)] + ": ";
            for (int s = 0; s < rows; ++s)
            {
                if (s > 0)
                    text += " / ";
                const float value = values_[static_cast<std::size_t>(s * count + cursor_)].target;
                text += with_unit(std::round(value * 10.0f) / 10.0f, axis.unit);
            }
            draw_readout(canvas, style, text, x, top - style.point_size - 8.0f, style.readout_size,
                         style.panel ? bounds_.inset(4.0f) : bounds_, amount);
        }
    }
}

// ---- DonutChart ------------------------------------------------------------

void DonutChart::set_slices(std::vector<DonutSlice> slices, bool snap)
{
    const std::vector<tween::Bounce> old = std::move(values_);
    slices_ = std::move(slices);
    values_.assign(slices_.size(), tween::Bounce{});
    for (std::size_t i = 0; i < slices_.size(); ++i)
    {
        if (i < old.size())
            values_[i] = old[i];
        values_[i].target = std::max(slices_[i].value, 0.0f);
        if (snap)
            values_[i].snap(values_[i].target);
    }
    pops_.resize(slices_.size());
    focus_ = std::clamp(focus_, 0, std::max(static_cast<int>(slices_.size()) - 1, 0));
    sync_legend();
}

void DonutChart::sync_legend()
{
    const float sum = total();
    std::vector<LegendItem> items;
    for (std::size_t i = 0; i < slices_.size(); ++i)
        items.push_back({slices_[i].label,
                         series_ink(style, slices_[i].color, static_cast<int>(i), style.panel),
                         value_text(slices_[i].value, sum)});
    legend_.set_items(std::move(items));
    legend_.style.theme = style.theme;
    legend_.style.text_size = style.legend_size;
    legend_.style.on_panel = style.panel;
    legend_.style.vertical = style.legend == LegendPlacement::side;
    legend_.style.align =
        style.legend == LegendPlacement::bottom ? gfx::Align::center : gfx::Align::right;
    legend_.set_highlight(active_ ? focus_ : -1);
}

void DonutChart::set_focus(int slice)
{
    focus_ = std::clamp(slice, 0, std::max(static_cast<int>(slices_.size()) - 1, 0));
}

float DonutChart::total() const
{
    float sum = 0.0f;
    for (const DonutSlice &slice : slices_)
        sum += std::max(slice.value, 0.0f);
    return sum;
}

void DonutChart::enter()
{
    age_ = 0.0f;
}

std::string DonutChart::value_text(float value, float sum) const
{
    if (style.percent)
    {
        char text[24];
        std::snprintf(text, sizeof(text), "%.0f%%",
                      sum > 0.0f ? static_cast<double>(100.0f * value / sum) : 0.0);
        return text;
    }
    return format_value(std::round(value * 10.0f) / 10.0f) + style.unit;
}

Event DonutChart::handle(const InputFrame &input, Feedback &feedback)
{
    exit_ = Direction::none;
    const int count = static_cast<int>(slices_.size());
    if (count == 0)
        return Event::none;
    const float x = bounds_.cx();
    if (input.nav != Direction::none)
    {
        const bool sideways = input.nav == Direction::left || input.nav == Direction::right;
        int next = sideways ? focus_ + (input.nav == Direction::right ? 1 : -1) : -1;
        if (!sideways || next < 0 || next >= count)
        {
            if (style.exits.allows(input.nav))
            {
                exit_ = input.nav;
                return Event::none;
            }
            if (!sideways || !style.wrap || count < 2 || input.nav_repeat)
                return refuse(feedback, style, input, refusal_, x);
            next = (next + count) % count;
        }
        focus_ = next;
        // A larger share sounds lower: a bigger thing.
        const float sum = total();
        const float share =
            sum > 0.0f ? slices_[static_cast<std::size_t>(focus_)].value / sum : 0.0f;
        play_cue(feedback, style, style.sounds.move, x, tween::lerp(1.08f, 0.92f, share));
        return Event::moved;
    }
    if (input.is_pressed(Action::confirm))
    {
        press_.trigger();
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

void DonutChart::update(float dt)
{
    age_ += dt;
    for (tween::Bounce &value : values_)
        ease(value, dt, style);
    active_amount_.target = active_ ? 1.0f : 0.0f;
    active_amount_.update(dt, 18.0f);
    for (std::size_t i = 0; i < pops_.size(); ++i)
    {
        pops_[i].target = active_ && static_cast<int>(i) == focus_ ? 1.0f : 0.0f;
        pops_[i].update(dt, std::max(style.omega(), 16.0f));
    }
    refusal_.update(dt, 9.0f);
    press_.update(dt, 10.0f);

    sync_legend();
    legend_.update(dt);
}

void DonutChart::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const int count = static_cast<int>(slices_.size());
    const bool side = style.legend == LegendPlacement::side;
    const Frame frame =
        begin_chart(canvas, paint, style, bounds_, title, legend_, count > 0 && !side);
    const Inks ink = inks(paint, style.panel);
    Rect area = frame.plot;
    if (frame.legend_shown)
        legend_.draw_at(canvas, frame.legend);
    if (side && count > 0)
    {
        // The ring takes a square on the left, the legend the rest.
        const float size = std::min(area.h, area.w * 0.5f);
        const float row = style.legend_size + legend_.style.row_gap + 4.0f;
        const float height = std::min(static_cast<float>(count) * row, area.h);
        const Rect column{area.x + size + 16.0f, area.cy() - height * 0.5f, area.w - size - 16.0f,
                          height};
        if (column.w > 40.0f)
            legend_.draw_at(canvas, column);
        area = {area.x, area.y, size, area.h};
    }

    const float shown_sum = [&]()
    {
        float sum = 0.0f;
        for (const tween::Bounce &value : values_)
            sum += std::max(value.value, 0.0f);
        return sum;
    }();
    const float outer = std::max(std::min(area.w, area.h) * 0.5f - style.pop - 2.0f, 8.0f);
    const float thickness = std::clamp(outer * style.thickness, 4.0f, outer - 2.0f);
    const float cx = area.cx() + shake(refusal_.value, canvas.time, 8.0f);
    const float cy = area.cy();
    const float fade = style.reduced_motion ? tween::cubic_out(age_ / 0.2f) : 1.0f;
    const bool plain = style.reduced_motion || style.entrance <= 0.0f;
    const float turn = kTau * (plain ? 1.0f : tween::cubic_out(age_ / (style.entrance * 1.4f)));

    list.push_opacity(fade);
    if (count == 0 || shown_sum <= 1e-6f)
    {
        list.ring(cx, cy, outer, thickness, ink.muted.with_alpha(0.2f));
    }
    else
    {
        const float gap = count > 1 ? style.gap / std::max(outer - thickness * 0.5f, 1.0f) : 0.0f;
        float angle = style.start_angle;
        for (int i = 0; i < count; ++i)
        {
            const float share =
                std::max(values_[static_cast<std::size_t>(i)].value, 0.0f) / shown_sum;
            const float sweep = share * turn;
            const float pop = pops_[static_cast<std::size_t>(i)].value;
            const float lift =
                style.reduced_motion ? 0.0f : style.pop * pop - 2.0f * press_.value * pop;
            const float mid = angle + sweep * 0.5f;
            const float dim = 1.0f - 0.22f * active_amount_.value * (1.0f - pop);
            const Color color =
                series_ink(style, slices_[static_cast<std::size_t>(i)].color, i, style.panel)
                    .with_alpha(dim);
            if (sweep - gap > 0.004f)
                list.arc(cx + std::sin(mid) * lift, cy - std::cos(mid) * lift, outer + 1.5f * pop,
                         thickness + 3.0f * pop, angle + gap * 0.5f, sweep - gap, color, false);
            angle += sweep;
        }
    }
    list.pop_opacity();

    // ---- the figure in the middle ----
    if (style.center && count > 0)
    {
        const float hole = (outer - thickness) * 2.0f - 12.0f;
        const float size =
            style.center_size > 0.0f ? style.center_size : std::clamp(outer * 0.4f, 18.0f, 44.0f);
        const float small = std::clamp(size * 0.5f, 14.0f, 18.0f);
        const float sum = total();
        const auto figure = [&](const std::string &value, const std::string &label, float amount)
        {
            if (amount <= 0.01f || hole < 30.0f)
                return;
            list.push_opacity(amount * fade);
            // The figure shrinks to the hole rather than being cut: half a
            // number is a wrong number.
            float fitted = size;
            const float width = number_width(paint, value, size);
            if (width > hole)
                fitted = std::max(size * hole / width, 12.0f);
            // A caption that does not fit the hole is left out, not cut.
            const bool labelled = !label.empty() && paint.label_width(label, small) <= hole;
            draw_number(paint, list, value, cx,
                        cy + fitted * 0.35f - (labelled ? small * 0.6f : 0.0f), fitted, ink.text,
                        gfx::Align::center);
            if (labelled)
                paint.label(label, cx, cy + fitted * 0.5f + small * 0.7f, small, ink.muted,
                            gfx::Align::center);
            list.pop_opacity();
        };
        const float focused = active_amount_.value;
        figure(format_value(std::round(sum * 10.0f) / 10.0f) + style.unit, style.center_label,
               1.0f - focused);
        const DonutSlice &slice = slices_[static_cast<std::size_t>(focus_)];
        figure(value_text(slice.value, sum), slice.label, focused);
    }
}

} // namespace hui::ui
