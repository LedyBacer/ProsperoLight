// ps5-homebrew-ui - Components: ButtonGroup and SplitButton.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Buttons that belong together and are navigated as one thing: a toolbar, a
// "one of these" selector, a set of filters; and a button with a second part
// that opens a menu of alternatives.

#pragma once

#include "ui/components/button.hpp"
#include "ui/components/card.hpp"
#include "ui/components/menu.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hui::ui
{

// ---- ButtonGroup -----------------------------------------------------------

enum class GroupMode : std::uint8_t
{
    actions,   // confirm fires the focused button: Event::activated
    exclusive, // one stays selected, like radio buttons: Event::changed
    multiple,  // each toggles on its own, like check boxes: Event::changed
};

enum class GroupLayout : std::uint8_t
{
    row,    // left and right move the focus
    column, // up and down do
};

struct GroupItem
{
    std::string label;
    bool disabled = false; // focusable, but refuses confirm
    bool selected = false; // exclusive and multiple
    int tag = 0;           // yours
};

struct ButtonGroupStyle : ComponentStyle
{
    GroupMode mode = GroupMode::actions;
    GroupLayout layout = GroupLayout::row;
    // ---- geometry ----
    bool joined = false;     // one body with dividers, like a toolbar; false: separate buttons
    float gap = 12.0f;       // between separate buttons
    float item_width = 0.0f; // row: each button's width; 0 shares the bounds equally
    ButtonSize size = ButtonSize::medium;
    float height = 0.0f;     // 0: from the size
    float text_size = 0.0f;  // 0: from the size
    float padding = 0.0f;    // 0: from the size
    float icon_width = 0.0f; // room reserved before each label for the `icon` slot
    // ---- look ----
    ButtonRole role = ButtonRole::secondary;        // a button at rest
    ButtonRole selected_role = ButtonRole::primary; // a selected one
    bool on_page = true;                            // ghost buttons sit on the page
    // ---- behaviour ----
    bool wrap = false;       // past the last button comes the first
    bool allow_none = false; // exclusive: confirm on the selected button clears it
    EdgeExits exits;         // edges that hand the focus on instead of refusing
    bool pitch_by_position = true;
    float press_scale = 0.03f; // separate buttons shrink this much when pressed
};

// A row or a column of buttons with one focus.
//
//   ui::ButtonGroup view;
//   view.style.mode = ui::GroupMode::exclusive;
//   view.style.joined = true;
//   view.set_items({{"Grid"}, {"List"}, {"Shelf"}});
//   view.set_selected(0);
//   view.set_bounds({96, 300, 540, 64});
//   ...
//   if (view.handle(input, feedback) == ui::Event::changed) show(view.selected());
//   view.update(dt);
//   view.draw(canvas);
class ButtonGroup
{
  public:
    // box is the room reserved by style.icon_width; ink is the label's colour.
    using Icon = std::function<void(Canvas &canvas, const gfx::Rect &box, const GroupItem &item,
                                    int index, gfx::Color ink, float focus)>;

    ButtonGroupStyle style;
    Icon icon;

    void set_items(std::vector<GroupItem> items);
    const std::vector<GroupItem> &items() const
    {
        return items_;
    }
    GroupItem &item(int index)
    {
        return items_[static_cast<std::size_t>(index)];
    }
    int count() const
    {
        return static_cast<int>(items_.size());
    }
    void set_bounds(const gfx::Rect &bounds);
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }

    int focus() const
    {
        return focus_;
    }
    void set_focus(int index, bool snap = true);
    // Whether the group has the screen's focus.
    void set_active(bool active);
    // Exclusive: the selected index, or -1. Multiple: the first selected one.
    int selected() const;
    // Exclusive: selects this one and clears the others (-1 clears all).
    // Multiple: turns this one on.
    void set_selected(int index, bool snap = true);
    bool is_selected(int index) const;
    // The edge the focus left through on the last handle(), or none.
    Direction exit() const
    {
        return exit_;
    }

    // Where a button is, and the rectangle around all of them.
    gfx::Rect item_rect(int index) const;
    gfx::Rect rect() const;

    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    void draw_joined(Canvas &canvas, Painter &paint, const ButtonMetrics &m) const;
    void draw_separate(Canvas &canvas, const ButtonMetrics &m) const;
    void draw_content(Canvas &canvas, Painter &paint, const ButtonMetrics &m, int index,
                      const gfx::Rect &content, gfx::Color ink) const;
    void retarget(bool snap);

    std::vector<GroupItem> items_;
    std::vector<tween::Spring> chosen_; // how selected each button looks, 0..1
    gfx::Rect bounds_{0.0f, 0.0f, 480.0f, 64.0f};
    int focus_ = 0;
    int pressed_ = -1;
    bool active_ = false;
    Direction exit_ = Direction::none;
    tween::Spring active_amount_;
    Highlight highlight_;
    Pulse press_;
};

// ---- SplitButton -----------------------------------------------------------

struct SplitButtonStyle : ComponentStyle
{
    ButtonRole role = ButtonRole::primary;
    ButtonSize size = ButtonSize::medium;
    float height = 0.0f;         // 0: from the size
    float text_size = 0.0f;      // 0: from the size
    float padding = 0.0f;        // 0: from the size
    float chevron_width = 0.0f;  // the second part; 0: as wide as the button is tall
    float divider = 0.56f;       // the line between the parts, as a share of the height
    float menu_width = 0.0f;     // 0: the button's width, at least 300
    bool swap_on_choose = false; // a chosen alternative becomes the main action
    EdgeExits exits;             // left and right edges that hand the focus on
    float press_scale = 0.03f;
};

// A main action with a chevron that opens a menu of alternatives: "Save" and,
// behind the chevron, "Save as copy", "Save and quit". Left and right move
// between the two parts.
//
//   ui::SplitButton save;
//   save.label = "Save";
//   save.set_alternatives({{"Save as copy"}, {"Save and quit"}});
//   save.set_bounds({96, 400, 360, 64});
//   ...
//   if (save.handle(input, feedback) == ui::Event::activated)
//       run(save.choice());            // -1: the main action, else the alternative
//   save.update(dt);
//   save.draw(canvas);
//   save.draw_menu(canvas);            // last: the menu floats over the screen
class SplitButton
{
  public:
    SplitButtonStyle style;
    std::string label;

    // The menu is a ui::Menu: style it, or read its items, through this.
    Menu &menu()
    {
        return menu_;
    }
    const Menu &menu() const
    {
        return menu_;
    }
    void set_alternatives(std::vector<MenuItem> items);
    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    // The part of the screen the menu may use.
    void set_menu_bounds(const gfx::Rect &bounds)
    {
        menu_.set_bounds(bounds);
    }
    void set_active(bool active);
    // 0: the main action has the focus, 1: the chevron.
    int part() const
    {
        return part_;
    }
    void set_part(int part);
    // While the menu is open the button wants every input.
    bool menu_open() const
    {
        return menu_.is_open();
    }
    // Closes the menu without a sound (the screen is leaving).
    void dismiss()
    {
        menu_.dismiss();
    }
    // What the last Event::activated was: -1 the main action, else the index
    // of the alternative.
    int choice() const
    {
        return choice_;
    }
    Direction exit() const
    {
        return exit_;
    }
    gfx::Rect rect() const;
    gfx::Rect part_rect(int part) const;

    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;
    void draw_menu(Canvas &canvas) const;

  private:
    gfx::Rect bounds_{0.0f, 0.0f, 360.0f, 64.0f};
    Menu menu_;
    int part_ = 0;
    int choice_ = -1;
    bool active_ = false;
    Direction exit_ = Direction::none;
    tween::Spring focus_;
    tween::Spring part_amount_; // 0 on the main part .. 1 on the chevron
    tween::Spring open_amount_; // the chevron turns over while the menu is open
    Pulse press_;
    Pulse refusal_;
};

} // namespace hui::ui
