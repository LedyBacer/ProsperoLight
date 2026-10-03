// ps5-homebrew-ui - Component: TagSelect, a wrapping cloud of chips to pick from.
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

struct TagOption
{
    std::string label;
    bool disabled = false; // focusable, but refuses confirm
    int tag = 0;           // yours

    // So a list of options can be written {{"One"}, {"Two"}}.
    TagOption() = default;
    TagOption(std::string text, bool is_disabled = false)
        : label(std::move(text)), disabled(is_disabled)
    {
    }
};

enum class TagShape : std::uint8_t
{
    theme,  // what the theme gives its chips
    pill,   // fully round ends, whatever the theme
    square, // square corners, whatever the theme
};

struct TagSelectStyle : ComponentStyle
{
    // ---- geometry ----
    float chip_height = 44.0f;
    float chip_padding = 18.0f;          // left and right of a chip's content
    float gap = 10.0f;                   // between chips of a row
    float row_gap = 10.0f;               // between rows
    float check_size = 16.0f;            // the mark of a selected chip
    gfx::Align align = gfx::Align::left; // how a row sits in the bounds
    // ---- type ----
    float text_size = 20.0f;
    float title_size = 20.0f;
    float title_gap = 10.0f; // between the title line and the first row
    // ---- look ----
    TagShape shape = TagShape::theme;
    bool check = true;    // a selected chip shows a check before its label
    bool counter = true;  // "3 of 5" (or "3 selected") at the end of the title line
    bool on_page = false; // title and counter sit on the page, not on a panel
    // ---- behaviour ----
    int max_selected = 0;       // more than this many are refused; 0 for no limit
    bool single = false;        // picking one clears the others
    bool flow = false;          // right at a row's end goes on to the next row (and back)
    EdgeExits exits;            // edges that hand the focus back instead of refusing
    bool pitch_by_state = true; // the change cue is higher for on than for off
};

// Chips that wrap into rows; confirm switches the focused one on or off. The
// focus moves by position: up and down go to the chip nearest to where the
// focus was horizontally, and keep that position while they cross rows of
// other widths.
//
//   ui::TagSelect genres;
//   genres.style.theme = theme;
//   genres.style.max_selected = 5;
//   genres.set_title("Favourite genres");
//   genres.set_options({{"Action"}, {"Puzzle"}, {"Racing"}, {"Strategy"}});
//   genres.set_selected(1, true);
//   genres.set_bounds({96, 300, 560, 200});
//   ...
//   genres.set_active(focused);
//   if (focused && genres.handle(input, feedback) == ui::Event::changed) store(genres.selection());
//   genres.update(dt);
//   genres.draw(canvas);
//
// The chips are measured with the fonts of the first draw and laid out again
// whenever they are needed, so the layout follows the style and the bounds.
// Call layout(fonts) yourself to have real rectangles before the first draw;
// until then widths are estimated. The fonts must outlive the component.
class TagSelect
{
  public:
    // The words of the counter, from how many are selected and the limit (0: none).
    using CounterText = std::function<std::string(int selected, int limit)>;

    TagSelectStyle style;
    CounterText counter_text;

    void set_title(std::string title)
    {
        title_ = std::move(title);
    }
    void set_options(std::vector<TagOption> options);
    const std::vector<TagOption> &options() const
    {
        return options_;
    }
    // Silent; the chip eases to the new state.
    void set_selected(int index, bool selected);
    bool selected(int index) const
    {
        return on_[static_cast<std::size_t>(index)] != 0;
    }
    int selected_count() const;
    // The indices of the selected chips, in order.
    std::vector<int> selection() const;

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    // Measures the chips and wraps them into the bounds' width.
    void layout(const Fonts &fonts) const;
    // Where a chip is on screen (after a layout).
    gfx::Rect chip_rect(int index) const;
    // The row a chip is in, and how many rows there are (after a layout).
    int row_of(int index) const;
    int rows() const;
    // The height of the title line and every row (after a layout).
    float preferred_height() const;

    int focus() const
    {
        return focus_;
    }
    void set_focus(int index, bool snap = true);
    void set_active(bool active)
    {
        active_ = active;
    }
    // The edge the last handle() left through (style.exits), or none.
    Direction exit() const
    {
        return exit_;
    }

    // Directions move (moved / refused, or none with exit() set); confirm
    // flips the focused chip (changed, or refused at the limit); back is
    // cancelled.
    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    void ensure_layout() const;
    void wrap() const;
    float chip_radius(const Painter &paint, const gfx::Rect &chip) const;
    int nearest_in_row(int row, float x) const;
    float title_height() const;

    std::vector<TagOption> options_;
    std::vector<char> on_;
    std::vector<tween::Spring> shown_; // how selected each chip looks, 0..1
    std::string title_;
    gfx::Rect bounds_{0.0f, 0.0f, 560.0f, 200.0f};
    int focus_ = 0;
    float keep_x_ = -1.0f; // the horizontal position up and down hold on to
    bool active_ = false;
    bool placed_ = false;
    Direction exit_ = Direction::none;
    Highlight highlight_;
    tween::Spring active_amount_;
    Pulse press_;
    Pulse limit_; // the counter flashes when the limit refuses
    // The layout: chip rectangles relative to the bounds' corner. It is a
    // cache of what the fonts, the style and the bounds give, so draw() may
    // refresh it.
    mutable std::vector<gfx::Rect> rects_;
    mutable std::vector<int> row_;
    mutable const Fonts *fonts_ = nullptr; // the fonts of the last draw or layout()
    mutable bool measured_ = false;
};

} // namespace hui::ui
