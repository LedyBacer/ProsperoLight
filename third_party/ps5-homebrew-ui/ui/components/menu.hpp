// ps5-homebrew-ui - Component: Menu, a context menu that springs from what it is about.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/component.hpp"

#include <functional>
#include <string>
#include <vector>

namespace hui::ui
{

struct MenuItem
{
    std::string label;
    std::string shortcut;   // quiet text at the end of the row ("R3", "Hold")
    bool separator = false; // a line between groups; skipped by the focus
    bool disabled = false;  // focusable, but refuses confirm
    bool danger = false;    // destructive: drawn in the theme's danger colour
    bool checkable = false; // confirm flips `checked` instead of activating
    bool checked = false;
    int tag = 0; // yours
};

// Which side of the anchor the menu opens on.
enum class MenuSide : std::uint8_t
{
    automatic, // below, or above when there is no room below
    below,
    above,
    right,
    left,
};

struct MenuStyle : ComponentStyle
{
    // ---- geometry ----
    float width = 380.0f;
    float row_height = 56.0f;
    float separator_height = 18.0f; // the room a separator takes
    float padding = 8.0f;           // between the panel and its rows
    float row_padding = 18.0f;      // inside a row, left and right
    float glyph_width = 0.0f;       // room reserved for the `glyph` slot
    float pointer_size = 14.0f;     // the pointer's length; 0 for none
    float offset = 10.0f;           // between the anchor and the pointer's tip
    // ---- type ----
    float text_size = 24.0f;
    float shortcut_size = 19.0f;
    // ---- look ----
    HighlightStyle highlight;
    float elevation = 1.0f; // scales the shadow the menu floats on; 0 for none
    float scrim = 0.0f;     // darkens everything behind the menu by this much (0..1)
    // Glass themes only: a layer of the page colour under the translucent
    // panel, this opaque, so the rows stay readable over busy content.
    float backing = 0.8f;
    // ---- behaviour ----
    MenuSide side = MenuSide::automatic; // flips to the opposite side when it does not fit
    bool wrap = true;                    // past the last item comes the first
    bool close_on_activate = true;       // confirm on an ordinary item closes the menu
    bool close_on_check = false;         // ... and so does flipping a checkable one
    float entrance_step = 0.018f;        // seconds between rows arriving; 0 for none
    bool pitch_by_position = true;
};

// A popover menu anchored to a rectangle (a row, a card, a tab). It opens with
// a spring from the anchor, points at it, and moves or flips to stay inside
// its bounds, which are the part of the screen it may use.
//
//   ui::Menu menu;
//   menu.style.theme = theme;
//   ui::MenuItem pin{"Pin to home"};
//   pin.checkable = true;
//   ui::MenuItem line;
//   line.separator = true;
//   ui::MenuItem remove{"Remove", "Hold"};
//   remove.danger = true;
//   menu.set_items({{"Open"}, pin, line, remove});
//   ...
//   if (input.is_pressed(Action::north)) menu.open(card_rect, feedback);
//   if (menu.is_open())
//   {
//       const ui::Event event = menu.handle(input, feedback);
//       if (event == ui::Event::activated) run(menu.items()[menu.focus()].tag);
//   }
//   menu.update(dt);
//   menu.draw(canvas);     // last, so it is on top; it draws nothing when closed
class Menu
{
  public:
    // box is the room reserved by style.glyph_width; focus is 0..1; ink is
    // the colour the label is drawn in.
    using Glyph = std::function<void(Canvas &canvas, const gfx::Rect &box, const MenuItem &item,
                                     int index, float focus, gfx::Color ink)>;

    MenuStyle style;
    Glyph glyph; // draws before the label, in style.glyph_width pixels

    void set_items(std::vector<MenuItem> items);
    const std::vector<MenuItem> &items() const
    {
        return items_;
    }
    MenuItem &item(int index)
    {
        return items_[static_cast<std::size_t>(index)];
    }
    // The part of the screen the menu must stay inside.
    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }

    // Opens at an anchor with the open cue; the focus starts on the first item.
    void open(const gfx::Rect &anchor, Feedback &feedback);
    // Closes with the close cue. handle() calls it on back.
    void close(Feedback &feedback);
    // Closes without a sound (the screen is leaving, or something else spoke).
    void dismiss()
    {
        open_ = false;
    }
    // Open: it takes input. Visible: it is still drawn (it fades out closed).
    bool is_open() const
    {
        return open_;
    }
    bool visible() const
    {
        return open_ || amount_.value > 0.01f;
    }
    int focus() const
    {
        return focus_;
    }
    void set_focus(int index, bool snap = true);

    // Where the panel is, and the side of the anchor it ended up on.
    gfx::Rect panel_rect() const;
    MenuSide side() const;

    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    struct Placement
    {
        gfx::Rect panel;
        MenuSide side = MenuSide::below;
        float tip_x = 0.0f; // the pointer's tip
        float tip_y = 0.0f;
    };
    Placement place() const;
    float item_height(int index) const;
    float item_top(int index) const;
    float content_height() const;
    int step(int from, int direction) const;
    void retarget(bool snap);
    void draw_pointer(Canvas &canvas, const Placement &at) const;

    std::vector<MenuItem> items_;
    std::vector<tween::Spring> checks_; // how checked each item looks, 0..1
    gfx::Rect bounds_{48.0f, 48.0f, gfx::kVirtualWidth - 96.0f, gfx::kVirtualHeight - 96.0f};
    gfx::Rect anchor_{};
    int focus_ = 0;
    bool open_ = false;
    float age_ = 10.0f;
    tween::Bounce amount_; // 0 closed .. 1 open
    tween::Spring danger_; // 1 while the focus is on a destructive item
    Highlight highlight_;
    Pulse press_;
};

} // namespace hui::ui
