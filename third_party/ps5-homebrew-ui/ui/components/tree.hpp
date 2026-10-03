// ps5-homebrew-ui - Component: TreeView, a hierarchy that opens and closes branch by branch.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/card.hpp"
#include "ui/components/component.hpp"

#include <functional>
#include <string>
#include <vector>

namespace hui::ui
{

struct TreeNode
{
    std::string label;
    std::string value{};   // quiet text at the end of the row ("48", "2.4 GB")
    std::string badge{};   // a small pill before the value ("NEW")
    bool expanded = false; // a branch that starts open
    bool disabled = false; // focusable, but refuses confirm
    int tag = 0;           // yours
    std::vector<TreeNode> children{};
};

struct TreeStyle : ComponentStyle
{
    // ---- geometry ----
    float row_height = 56.0f;
    float gap = 4.0f;            // between rows
    float padding = 16.0f;       // inside a row, left and right
    float indent = 30.0f;        // per level of depth
    float chevron_width = 26.0f; // room for the expand chevron (kept on leaves, so labels align)
    float icon_width = 0.0f;     // room reserved for the `icon` slot
    float panel_padding = 12.0f; // between the panel and the rows (panel = true)
    // ---- type ----
    float label_size = 24.0f;
    float value_size = 20.0f;
    float badge_size = 17.0f;
    // ---- look ----
    HighlightStyle highlight;
    bool guides = true;       // a vertical line per level of depth
    bool panel = false;       // a themed panel behind the tree
    bool scroll_thumb = true; // shown only when the rows overflow
    // ---- behaviour ----
    bool confirm_toggles = true; // confirm on a branch opens or closes it instead of activating
    bool wrap = false;           // past the last row comes the first
    EdgeExits exits;             // edges that hand the focus back instead of refusing
    float edge_fade = 0.8f;      // rows fade over this share of a row at the clip edges
    float entrance_step = 0.03f; // seconds between rows arriving; 0 for none
    bool pitch_by_depth = true;  // deeper rows sound lower
};

// A hierarchical list. Right opens a branch (or steps into its first child),
// left closes it (or steps out to its parent), up and down walk the rows that
// are showing, confirm activates. Rows appear and disappear with a height
// animation and one highlight glides between them.
//
// Nodes are numbered in reading order with every branch open (depth first):
// that index is what focus() returns and what node() takes.
//
//   ui::TreeView tree;
//   tree.style.theme = theme;
//   ui::TreeNode saves{"Saves"};
//   saves.children = {{"Slot 1", "63 h"}, {"Slot 2", "4 h"}};
//   tree.set_nodes({{"Library"}, saves, {"Settings"}});
//   tree.set_bounds({96, 240, 520, 600});
//   ...
//   if (tree.handle(input, feedback) == ui::Event::activated) open(tree.node(tree.focus()).tag);
//   tree.update(dt);
//   tree.draw(canvas);
class TreeView
{
  public:
    // box is the room reserved by style.icon_width; focus is 0..1; ink is the
    // colour the label is drawn in.
    using Icon = std::function<void(Canvas &canvas, const gfx::Rect &box, const TreeNode &node,
                                    int index, float focus, gfx::Color ink)>;

    TreeStyle style;
    Icon icon; // draws before the label, in style.icon_width pixels

    void set_nodes(std::vector<TreeNode> roots);
    int count() const
    {
        return static_cast<int>(rows_.size());
    }
    // A node by its index. Its `children` are empty here: the tree keeps the
    // structure itself (see parent(), depth(), child_count()).
    const TreeNode &node(int index) const
    {
        return rows_[static_cast<std::size_t>(index)].node;
    }
    TreeNode &node(int index)
    {
        return rows_[static_cast<std::size_t>(index)].node;
    }
    int parent(int index) const; // -1 for a root
    int depth(int index) const;  // 0 for a root
    int child_count(int index) const;
    bool is_expanded(int index) const;
    // Whether every branch above it is open.
    bool is_showing(int index) const;
    // Opens or closes a branch without sound; snap skips the animation.
    void set_expanded(int index, bool expanded, bool snap = false);
    void expand_all(bool snap = false);
    void collapse_all(bool snap = false);

    void set_bounds(const gfx::Rect &bounds);
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    int focus() const
    {
        return focus_;
    }
    // Moves the focus without sound, opening the branches above the node.
    void set_focus(int index, bool snap = true);
    // An inactive tree keeps a faint highlight.
    void set_active(bool active)
    {
        active_ = active;
    }
    // Replays the entrance animation.
    void enter();
    // The edge the last handle() left through, or Direction::none.
    Direction exit() const
    {
        return exit_;
    }

    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

    // Where a row is on screen right now (scroll and animation applied).
    gfx::Rect row_rect(int index) const;

  private:
    struct Row
    {
        TreeNode node;
        int parent = -1;
        int depth = 0;
        int end = 0;         // one past its last descendant
        int children = 0;    // direct children
        tween::Spring shown; // 0 hidden .. 1 a full row
        tween::Spring turn;  // the chevron: 0 closed .. 1 open
    };

    void flatten(std::vector<TreeNode> &nodes, int parent, int depth);
    gfx::Rect inner() const;
    int step(int from, int direction) const;
    float top(int index, bool settled) const;
    float total(bool settled) const;
    void move_to(int index, Feedback &feedback);
    void retarget(bool snap);
    void snap_rows();
    void place_highlight();

    std::vector<Row> rows_;
    gfx::Rect bounds_{0.0f, 0.0f, 520.0f, 400.0f};
    int focus_ = 0;
    bool active_ = true;
    Direction exit_ = Direction::none;
    float age_ = 10.0f;
    // See Accordion: the highlight sits on the focused row plus an offset
    // that springs to zero, so it glides yet never lags behind a moving row.
    tween::Bounce glide_;
    Highlight highlight_;
    Scroller scroll_;
    tween::Spring active_amount_{1.0f, 0.0f, 1.0f};
    Pulse press_;
};

} // namespace hui::ui
