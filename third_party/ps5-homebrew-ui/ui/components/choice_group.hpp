// ps5-homebrew-ui - Components: CheckGroup and RadioGroup, a labelled set of choices.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A set of check boxes (any number on) or radio buttons (one on) as one
// component: one title, one focus that glides between the items, values that
// ease, and edges that refuse or hand the focus back to the screen. Both
// share their layout, navigation and drawing (ChoiceGroup); they differ in
// what confirm does.

#pragma once

#include "ui/components/card.hpp"
#include "ui/components/component.hpp"

#include <string>
#include <utility>
#include <vector>

namespace hui::ui
{

struct ChoiceItem
{
    std::string label;
    std::string description; // a second, quieter line
    bool disabled = false;   // focusable, but refuses confirm
    int tag = 0;             // yours

    // So a list of items can be written {{"One"}, {"Two", "Its second line"}}.
    ChoiceItem() = default;
    ChoiceItem(std::string text, std::string second_line = {}, bool is_disabled = false)
        : label(std::move(text)), description(std::move(second_line)), disabled(is_disabled)
    {
    }
};

enum class ChoiceLayout : std::uint8_t
{
    vertical,   // one item per row
    horizontal, // all items in one row, in equal cells
    grid,       // style.columns equal cells per row
};

struct ChoiceGroupStyle : ComponentStyle
{
    // ---- geometry ----
    ChoiceLayout layout = ChoiceLayout::vertical;
    int columns = 2;                // ChoiceLayout::grid
    float row_height = 48.0f;       // an item without a description
    float described_height = 70.0f; // items when any of them has a description
    float gap = 4.0f;               // between rows
    float column_gap = 10.0f;       // between cells of a row
    float padding = 12.0f;          // inside an item, left and right
    float box_size = 28.0f;         // the check box or radio button
    float box_gap = 14.0f;          // between it and the label
    // ---- type ----
    float title_size = 20.0f;
    float title_gap = 10.0f; // between the title and the first row
    float label_size = 24.0f;
    float description_size = 19.0f;
    // ---- look ----
    HighlightStyle highlight;    // the gliding focus
    float idle_highlight = 0.0f; // how much of it stays when the group lost the focus
    bool on_page = false;        // drawn straight on the page, not on a panel
    // ---- behaviour ----
    bool wrap = false;          // past the last item comes the first (along the layout)
    EdgeExits exits;            // edges that hand the focus back instead of refusing
    bool pitch_by_state = true; // the change cue is higher for on than for off
    // CheckGroup: a first row that checks or clears everything, with a third
    // state (a dash) while only some items are on.
    bool select_all = false;
    std::string select_all_label = "Select all";
    // RadioGroup: moving the focus selects, as desktop radio buttons do.
    bool select_on_move = false;
    // RadioGroup: confirm on the selected item clears the selection.
    bool allow_none = false;
};

// What CheckGroup and RadioGroup share. Use one of those two.
class ChoiceGroup
{
  public:
    ChoiceGroupStyle style;

    void set_title(std::string title)
    {
        title_ = std::move(title);
    }
    void set_items(std::vector<ChoiceItem> items);
    const std::vector<ChoiceItem> &items() const
    {
        return items_;
    }
    ChoiceItem &item(int index)
    {
        return items_[static_cast<std::size_t>(index)];
    }
    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    // The height the title and the rows need at the bounds' width.
    float preferred_height() const;
    // Where an item is on screen. -1 is the "select all" row.
    gfx::Rect item_rect(int index) const;

    // The focused item; -1 is the "select all" row.
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
    // The item the last `changed` event was about; -1 for "select all".
    int changed_index() const
    {
        return changed_;
    }

    void update(float dt);
    void draw(Canvas &canvas) const;

  protected:
    explicit ChoiceGroup(bool radio) : radio_(radio)
    {
    }

    bool has_header() const
    {
        return !radio_ && style.select_all && !items_.empty();
    }
    int columns() const;
    float cell_height() const;
    gfx::Rect local_rect(int index) const; // relative to the bounds' corner
    // Handles a direction. Returns false when the input is not a direction.
    bool navigate(const InputFrame &input, Feedback &feedback, bool quiet_move, Event *event);
    Event press_back(const InputFrame &input, Feedback &feedback);
    float item_x(int index) const;

    std::vector<ChoiceItem> items_;
    std::vector<char> on_;             // the values
    std::vector<tween::Spring> shown_; // ... as drawn, 0..1
    std::string title_;
    gfx::Rect bounds_{0.0f, 0.0f, 420.0f, 200.0f};
    int focus_ = 0;
    int changed_ = 0;
    int return_column_ = 0; // the column the focus came to the header from
    bool radio_ = false;
    bool active_ = false;
    bool placed_ = false; // the highlight has been snapped to the focus once
    Direction exit_ = Direction::none;
    Highlight highlight_;
    tween::Spring active_amount_;
    tween::Spring all_;  // "select all": every item is on
    tween::Spring some_; // ... or only some are
    Pulse press_;
};

// Check boxes: any number of items on.
//
//   ui::CheckGroup notify;
//   notify.style.theme = theme;
//   notify.style.select_all = true;
//   notify.set_title("Notify me about");
//   notify.set_items({{"Friends online"}, {"Invitations"}, {"Updates", "About once a month"}});
//   notify.set_checked(0, true);
//   notify.set_bounds({96, 300, 480, notify.preferred_height()});
//   ...
//   notify.set_active(focused);
//   if (focused && notify.handle(input, feedback) == ui::Event::changed)
//       store(notify.changed_index(), notify.checked(notify.changed_index()));
class CheckGroup : public ChoiceGroup
{
  public:
    CheckGroup() : ChoiceGroup(false)
    {
    }
    // Silent; the box eases to the new value.
    void set_checked(int index, bool checked);
    bool checked(int index) const
    {
        return on_[static_cast<std::size_t>(index)] != 0;
    }
    int checked_count() const;
    // Every item that is not disabled, silently.
    void set_all(bool checked);

    // Directions move (moved / refused, or none with exit() set); confirm
    // flips the focused item (changed); back is cancelled.
    Event handle(const InputFrame &input, Feedback &feedback);
};

// Radio buttons: one item on.
//
//   ui::RadioGroup mode;
//   mode.style.layout = ui::ChoiceLayout::horizontal;
//   mode.set_items({{"Solo"}, {"Co-op"}, {"Versus"}});
//   mode.set_selected(1);
class RadioGroup : public ChoiceGroup
{
  public:
    RadioGroup() : ChoiceGroup(true)
    {
    }
    // Silent; -1 selects nothing.
    void set_selected(int index);
    int selected() const;

    // Directions move; confirm selects the focused item (changed, or none
    // when it already is); back is cancelled.
    Event handle(const InputFrame &input, Feedback &feedback);

  private:
    Event select(int index, const InputFrame &input, Feedback &feedback);
};

} // namespace hui::ui
