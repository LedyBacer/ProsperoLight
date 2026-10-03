// ps5-homebrew-ui - Component: Table, a sortable data grid.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/card.hpp"
#include "ui/components/data_common.hpp"

#include <functional>
#include <string>
#include <vector>

namespace hui::ui
{

struct TableRow;

// One column, as data.
struct TableColumn
{
    // cell is the column's part of a row; focus is 0..1.
    using Slot = std::function<void(Canvas &canvas, const gfx::Rect &cell, const TableRow &row,
                                    float focus)>;

    std::string title;
    float width = 0.0f; // fixed width in pixels; 0 shares what is left by `flex`
    float flex = 1.0f;  // this column's share of the width the fixed ones leave
    gfx::Align align = gfx::Align::left;
    bool numeric = false; // set in the number face; sorts by TableCell::number
    bool strong = false;  // the label face instead of the body face (a name column)
    bool sortable = true;
    Slot cell; // draws the cell instead of its text
};

struct TableCell
{
    std::string text;
    double number = 0.0; // what a numeric column sorts by
};

struct TableRow
{
    int id = 0; // stable across set_rows(): keys the row's position, focus and mark
    std::vector<TableCell> cells;
    bool disabled = false; // focusable, but refuses confirm
    int tag = 0;           // yours
};

enum class SortOrder : std::uint8_t
{
    none,
    ascending,
    descending,
};

enum class TableLines : std::uint8_t
{
    plain,    // nothing between rows
    zebra,    // every second row on a faint band
    dividers, // hairlines between rows
};

struct TableStyle : ComponentStyle
{
    // ---- geometry ----
    float row_height = 52.0f;
    float header_height = 46.0f;
    float padding = 18.0f;       // inside the table, left and right
    float column_gap = 18.0f;    // between columns
    float panel_padding = 12.0f; // between the panel and the rows (panel = true)
    float rank_width = 52.0f;    // the leading rank column (rank = true)
    float mark_width = 44.0f;    // the selection mark column (multi_select = true)
    float pinned_gap = 8.0f;     // between the rows and the pinned row
    // ---- type ----
    float text_size = 22.0f;
    float header_size = 17.0f;
    // ---- look ----
    HighlightStyle highlight;
    TableLines lines = TableLines::zebra;
    bool panel = true;        // a themed panel behind the table
    bool header = true;       // the sticky header row
    bool rank = false;        // a leading column with each row's place in the order
    bool scroll_thumb = true; // shown only when the rows overflow
    float edge_fade = 0.5f;   // a row cut by the view is hidden until this share of it shows
    // ---- behaviour ----
    bool multi_select = false;   // confirm marks a row (Event::changed) instead of activating it
    int pinned_id = -1;          // the id of a row repeated under the others ("you"); -1: none
    bool unsorted_step = false;  // cycling a column's order passes through "unsorted"
    EdgeExits exits;             // edges that hand the focus back to the screen
    float entrance_step = 0.03f; // seconds between rows arriving; 0 for none
};

// A data grid for leaderboards and file lists: columns as data, a header
// that stays put, rows that scroll under one gliding highlight, and sorting
// that moves every row to its new place instead of redrawing the list.
//
//   ui::Table table;
//   table.style.theme = theme;
//   table.style.rank = true;
//   table.set_columns({{"Player", 0, 2}, {"Score", 120, 1, gfx::Align::right, true}});
//   table.set_rows(rows);                    // ids key the rows
//   table.set_bounds({96, 300, 620, 400});
//   table.sort_by(1, ui::SortOrder::descending, false);
//   ...
//   if (input.is_pressed(Action::north)) table.cycle_sort(feedback);
//   else if (table.handle(input, feedback) == ui::Event::activated) open(table.focus());
//
// Up from the first row enters the header: there left and right move a column
// cursor and confirm sorts by that column.
class Table
{
  public:
    TableStyle style;

    void set_columns(std::vector<TableColumn> columns);
    const std::vector<TableColumn> &columns() const
    {
        return columns_;
    }
    // Rows whose id was there before keep their position on screen, their
    // mark and the focus, so new data slides into place.
    void set_rows(std::vector<TableRow> rows);
    const std::vector<TableRow> &rows() const
    {
        return rows_;
    }
    void set_bounds(const gfx::Rect &bounds);
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }

    // The focused row, as an index into rows(); -1 when there are none.
    int focus() const;
    // Its place in the shown order, from 0.
    int focus_place() const
    {
        return place_;
    }
    // Where a row is in the shown order, from 0.
    int place_of(int row) const;
    // Moves the focus to a row without sound; snap skips the glide.
    void set_focus(int row, bool snap = true);
    bool in_header() const
    {
        return in_header_;
    }
    int column_cursor() const
    {
        return cursor_;
    }

    int sort_column() const
    {
        return sort_column_;
    }
    SortOrder sort_order() const
    {
        return sort_order_;
    }
    // Sorts without sound. animate = false places the rows at once.
    void sort_by(int column, SortOrder order, bool animate = true);
    // The dedicated sort action: in the header it turns the cursor's column
    // over; among the rows it steps through every sortable column and order.
    Event cycle_sort(Feedback &feedback);

    bool selected(int row) const;
    void set_selected(int row, bool selected);
    int selected_count() const;

    void set_active(bool active)
    {
        active_ = active;
    }
    // Replays the entrance animation.
    void enter();

    Event handle(const InputFrame &input, Feedback &feedback);
    // The edge the focus left through on the last handle() (see
    // TableStyle::exits), or Direction::none. handle() returned Event::none.
    Direction exit() const
    {
        return exit_;
    }
    void update(float dt);
    void draw(Canvas &canvas) const;

    // Where a row is on screen right now (scroll and sort motion applied).
    gfx::Rect row_rect(int row) const;

  private:
    struct Column
    {
        float x = 0.0f;
        float w = 0.0f;
    };

    gfx::Rect inner() const;
    gfx::Rect header_rect() const;
    gfx::Rect view() const;
    gfx::Rect pinned_rect() const;
    int pinned_row() const;
    std::vector<Column> layout() const;
    int next_sortable(int from, int direction) const;
    void reorder(bool animate);
    void retarget(bool snap);
    Event turn(int column, Feedback &feedback);
    void draw_cells(Canvas &canvas, Painter &paint, const std::vector<Column> &columns,
                    const gfx::Rect &row_area, int row, int place, float focus,
                    const Inks &resting) const;

    std::vector<TableColumn> columns_;
    std::vector<TableRow> rows_;
    std::vector<int> order_;           // place -> row
    std::vector<tween::Bounce> tops_;  // per row: its top in content space
    std::vector<tween::Spring> marks_; // per row: 0..1 selected
    std::vector<bool> selected_;
    gfx::Rect bounds_{0.0f, 0.0f, 640.0f, 400.0f};
    int place_ = 0;
    int cursor_ = 0;
    bool in_header_ = false;
    int sort_column_ = -1;
    SortOrder sort_order_ = SortOrder::none;
    bool active_ = true;
    float age_ = 10.0f;
    Direction exit_ = Direction::none;
    Highlight highlight_; // over the rows, in content space
    Highlight column_;    // over the header cells
    Scroller scroll_;
    tween::Spring active_amount_{1.0f, 0.0f, 1.0f};
    tween::Spring header_amount_;
    tween::Spring arrow_; // 0 points up (ascending), 1 down
    Pulse press_;
    Pulse sorted_; // a flash on the header when the order changes
};

} // namespace hui::ui
