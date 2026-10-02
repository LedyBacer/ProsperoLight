// ps5-homebrew-ui - Components: BarChart, LineChart, DonutChart and Legend.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Charts drawn with the kit's own shapes, so they take a theme like any
// other component: series colours come from its palette, bars are built in
// its material, grid lines and labels use its muted text colour and its
// faces. Values never jump: new data eases in with the theme's spring, and
// enter() lets bars grow, lines draw themselves and a donut sweep round.

#pragma once

#include "ui/components/card.hpp"
#include "ui/components/data_common.hpp"

#include <string>
#include <vector>

namespace hui::ui
{

// ---- shared ----------------------------------------------------------------

// One line of a line chart, or one bar per category of a bar chart.
struct ChartSeries
{
    std::string name;
    std::vector<float> values;
    gfx::Color color{0.0f, 0.0f, 0.0f, 0.0f}; // alpha 0: the theme's nth series colour
};

// The value axis of a chart: where it starts, where it ends and how far
// apart its ticks are.
struct ChartScale
{
    float low = 0.0f;
    float high = 1.0f;
    float step = 0.25f;
};
// The "nice" distance between ticks for a range: 1, 2, 2.5 or 5 times a
// power of ten, giving about `ticks` intervals.
float nice_step(float range, int ticks);
// A scale that holds [low, high] and ends on ticks.
ChartScale nice_scale(float low, float high, int ticks);

enum class LegendPlacement : std::uint8_t
{
    top,    // on the title's line, at its end
    bottom, // a row under the plot
    side,   // a column beside the plot (the donut's default)
    none,
};

struct AxisStyle
{
    bool grid = true;   // lines across the plot at each tick
    bool labels = true; // tick values and category names
    int ticks = 4;      // about this many intervals
    float label_size = 16.0f;
    bool from_zero = true; // the scale includes zero
    std::string unit;      // appended to tick values and readouts ("h", "%")
};

// What the three charts have in common.
struct ChartStyle : ComponentStyle
{
    bool panel = true;     // a themed panel behind the chart
    float padding = 16.0f; // between the panel and the chart (panel = true)
    float title_size = 20.0f;
    LegendPlacement legend = LegendPlacement::top;
    float legend_size = 17.0f;
    bool readout = true; // the bubble over the focused bar, point or slice
    float readout_size = 18.0f;
    float entrance = 0.6f; // seconds bars take to grow, a line to draw, a donut to sweep
    EdgeExits exits;       // edges that hand the focus back to the screen
};

// ---- Legend ----------------------------------------------------------------

struct LegendItem
{
    std::string label;
    gfx::Color color;
    std::string value; // optional, after the label
};

struct LegendStyle : ComponentStyle
{
    float text_size = 17.0f;
    float swatch = 12.0f;                // the colour mark's size
    float gap = 20.0f;                   // between items in a row
    float row_gap = 8.0f;                // between rows
    bool vertical = false;               // one item per row, values at the right edge
    bool on_panel = true;                // which text colours to use
    gfx::Align align = gfx::Align::left; // of a horizontal legend inside its bounds
    float dim = 0.45f;                   // opacity of the items that are not highlighted
};

// What the colours of a chart mean. The charts draw one themselves; use it
// alone to label anything else that is colour coded.
class Legend
{
  public:
    LegendStyle style;

    void set_items(std::vector<LegendItem> items);
    const std::vector<LegendItem> &items() const
    {
        return items_;
    }
    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    // Dims every item but this one; -1 shows them all alike.
    void set_highlight(int index)
    {
        highlight_ = index;
    }
    // The width one row of all the items takes, for placing it.
    float width(const Painter &paint) const;
    // The height it takes in a space this wide (a horizontal legend wraps).
    float height(const Painter &paint, float width) const;

    void update(float dt);
    void draw(Canvas &canvas) const;
    // The same, somewhere else than its bounds.
    void draw_at(Canvas &canvas, const gfx::Rect &bounds) const;

  private:
    float item_width(const Painter &paint, std::size_t index) const;
    float row_height() const;

    std::vector<LegendItem> items_;
    std::vector<tween::Spring> emphasis_;
    gfx::Rect bounds_{0.0f, 0.0f, 300.0f, 24.0f};
    int highlight_ = -1;
};

// ---- BarChart --------------------------------------------------------------

enum class BarLayout : std::uint8_t
{
    grouped, // a category's bars stand side by side; each bar takes the focus
    stacked, // ... on top of each other; the focus takes a whole category
};

struct BarChartStyle : ChartStyle
{
    bool horizontal = false; // bars run left to right, categories top to bottom
    BarLayout layout = BarLayout::grouped;
    float slot_fill = 0.68f;   // the share of a category's room its bars take
    float bar_gap = 3.0f;      // between the bars of a group
    float radius = 5.0f;       // corner of a bar, at most the theme's control radius
    bool value_labels = false; // each bar's value at its end
    float grow_step = 0.045f;  // seconds between categories starting to grow
    HighlightStyle highlight;  // the plate behind the focused bar or category
    AxisStyle axis;
};

// Bars per category, vertical or horizontal, grouped or stacked.
//
//   ui::BarChart chart;
//   chart.title = "Hours played";
//   chart.set_categories({"Mon", "Tue", "Wed"});
//   chart.set_series({{"Solo", {2, 4, 3}}, {"Co-op", {1, 0, 2}}});
//   chart.set_bounds({96, 300, 420, 280});
//   chart.enter();
//   ...
//   chart.set_series(next_week);      // the bars ease to the new values
class BarChart
{
  public:
    BarChartStyle style;
    std::string title;

    void set_categories(std::vector<std::string> categories);
    // snap places the bars at once instead of easing them.
    void set_series(std::vector<ChartSeries> series, bool snap = false);
    const std::vector<ChartSeries> &series() const
    {
        return series_;
    }
    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }

    // The focused category, and the focused series inside it (0 when stacked).
    int focus() const
    {
        return category_;
    }
    int focus_series() const
    {
        return style.layout == BarLayout::stacked ? 0 : member_;
    }
    void set_focus(int category, int series = 0);
    // The value under the focus: a bar's, or a category's total when stacked.
    float focused_value() const;
    void set_active(bool active)
    {
        active_ = active;
    }
    void enter();

    Event handle(const InputFrame &input, Feedback &feedback);
    Direction exit() const
    {
        return exit_;
    }
    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    int categories() const;
    float target(int series, int category) const;
    float shown(int series, int category) const;
    void retarget();

    std::vector<std::string> categories_;
    std::vector<ChartSeries> series_;
    std::vector<tween::Bounce> values_; // series * categories, eased
    Legend legend_;
    gfx::Rect bounds_{0.0f, 0.0f, 420.0f, 280.0f};
    ChartScale scale_;
    tween::Spring high_; // the eased end of the scale
    tween::Spring low_;
    bool scale_set_ = false;
    int category_ = 0;
    int member_ = 0;
    bool active_ = true;
    float age_ = 10.0f;
    Direction exit_ = Direction::none;
    tween::Spring cursor_; // the focus along the categories, eased
    tween::Spring active_amount_{1.0f, 0.0f, 1.0f};
    Pulse refusal_;
    Pulse press_;
};

// ---- LineChart -------------------------------------------------------------

enum class LineShape : std::uint8_t
{
    theme,    // straight, or stepped in a pixel-art theme
    straight, // point to point
    stepped,  // level until the next point, then a jump
};

struct LineChartStyle : ChartStyle
{
    LineShape shape = LineShape::theme;
    float line_width = 3.5f;
    bool area = true;         // a fill fading down from each line
    float area_alpha = 0.34f; // its opacity under the line
    float area_step = 4.0f;   // width of the strips the fill is built from
    bool points = false;      // a dot on every value
    float point_size = 5.0f;  // its radius; the cursor's dots are a little larger
    AxisStyle axis;
};

// One or more series over the same steps, with a cursor that left and right
// move from step to step and a readout of the values under it.
//
//   ui::LineChart chart;
//   chart.title = "Frame time";
//   chart.set_labels({"0", "10", "20", "30"});
//   chart.set_series({{"ms", {16.6f, 17.1f, 16.4f, 18.0f}}});
class LineChart
{
  public:
    LineChartStyle style;
    std::string title;

    void set_labels(std::vector<std::string> labels);
    void set_series(std::vector<ChartSeries> series, bool snap = false);
    const std::vector<ChartSeries> &series() const
    {
        return series_;
    }
    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }

    // The step the cursor is on.
    int focus() const
    {
        return cursor_;
    }
    void set_focus(int step);
    void set_active(bool active)
    {
        active_ = active;
    }
    void enter();

    Event handle(const InputFrame &input, Feedback &feedback);
    Direction exit() const
    {
        return exit_;
    }
    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    int steps() const;
    float shown(int series, int step) const;
    void rescale();
    void sync_legend();

    std::vector<std::string> labels_;
    std::vector<ChartSeries> series_;
    std::vector<tween::Bounce> values_; // series * steps, eased
    Legend legend_;
    gfx::Rect bounds_{0.0f, 0.0f, 420.0f, 280.0f};
    ChartScale scale_;
    tween::Spring high_;
    tween::Spring low_;
    bool scale_set_ = false;
    int cursor_ = 0;
    bool active_ = true;
    float age_ = 10.0f;
    Direction exit_ = Direction::none;
    tween::Spring cursor_x_; // the cursor along the steps, eased
    tween::Spring active_amount_{1.0f, 0.0f, 1.0f};
    Pulse refusal_;
};

// ---- DonutChart ------------------------------------------------------------

struct DonutSlice
{
    std::string label;
    float value = 0.0f;
    gfx::Color color{0.0f, 0.0f, 0.0f, 0.0f}; // alpha 0: the theme's nth series colour
};

struct DonutChartStyle : ChartStyle
{
    DonutChartStyle()
    {
        legend = LegendPlacement::side;
    }
    float thickness = 0.3f;             // the ring's width as a share of its radius
    float gap = 4.0f;                   // pixels between slices
    float pop = 8.0f;                   // how far the focused slice moves out
    float start_angle = 0.0f;           // radians, clockwise from 12 o'clock
    bool center = true;                 // a figure in the middle: the total, or the focused slice
    std::string center_label = "Total"; // under the total
    float center_size = 0.0f;           // of the figure; 0: sized from the ring
    bool percent = false;               // the legend and the readout show shares, not values
    bool wrap = true;                   // past the last slice comes the first (an exit beats it)
    std::string unit;                   // appended to values
};

// Shares of a whole as arcs with gaps, a figure in the middle and a legend.
// Left and right walk round the slices; the focused one moves out.
//
//   ui::DonutChart chart;
//   chart.title = "Storage";
//   chart.set_slices({{"Games", 412}, {"Media", 96}, {"Saves", 12}});
class DonutChart
{
  public:
    DonutChartStyle style;
    std::string title;

    void set_slices(std::vector<DonutSlice> slices, bool snap = false);
    const std::vector<DonutSlice> &slices() const
    {
        return slices_;
    }
    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }

    int focus() const
    {
        return focus_;
    }
    void set_focus(int slice);
    // The sum of the slices' values.
    float total() const;
    void set_active(bool active)
    {
        active_ = active;
    }
    void enter();

    Event handle(const InputFrame &input, Feedback &feedback);
    Direction exit() const
    {
        return exit_;
    }
    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    std::string value_text(float value, float total) const;
    void sync_legend();

    std::vector<DonutSlice> slices_;
    std::vector<tween::Bounce> values_; // eased
    std::vector<tween::Spring> pops_;   // 0..1 per slice: how far out it is
    Legend legend_;
    gfx::Rect bounds_{0.0f, 0.0f, 420.0f, 280.0f};
    int focus_ = 0;
    bool active_ = true;
    float age_ = 10.0f;
    Direction exit_ = Direction::none;
    tween::Spring active_amount_{1.0f, 0.0f, 1.0f};
    Pulse refusal_;
    Pulse press_;
};

} // namespace hui::ui
