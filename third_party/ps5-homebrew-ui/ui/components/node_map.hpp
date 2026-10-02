// ps5-homebrew-ui - Component: NodeMap, free-direction focus over nodes placed anywhere.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/card.hpp"

#include <functional>
#include <string>
#include <vector>

namespace hui::ui
{

enum class NodeState : std::uint8_t
{
    locked,    // out of reach: dim, with a lock
    available, // can be taken now: it calls for attention
    done,      // taken: filled, with a tick
};

struct MapNode
{
    float x = 0.0f; // its centre, in the map's own pixels (any origin)
    float y = 0.0f;
    std::string label;
    NodeState state = NodeState::locked;
    float size = 0.0f;                        // 0: style.node_size
    gfx::Color color{0.0f, 0.0f, 0.0f, 0.0f}; // its accent; alpha 0: the theme's
    int tag = 0;                              // yours
};

// A line between two nodes. It lights up from `from` towards `to`, so write
// links in the direction the path is walked (parent first).
struct MapLink
{
    int from = 0;
    int to = 0;
};

struct NodeMapStyle : ComponentStyle
{
    // ---- geometry ----
    float node_size = 64.0f;
    float link_width = 5.0f;
    float zoom = 1.0f;     // scales the whole map about its corner
    float margin = 120.0f; // the camera keeps the focus this far inside the bounds
    float padding = 36.0f; // room kept around the outermost nodes
    // ---- labels ----
    bool labels = true;
    float label_size = 19.0f;
    float label_gap = 16.0f;    // between a node and its label
    float label_width = 170.0f; // labels are cut to this
    bool on_surface = false;    // the map sits on a panel: labels take the panel's ink
    // ---- look ----
    bool flow = true; // dots travel along links that lead to an available node
    // ---- navigation: which node a direction means ----
    float cone = 1.0f;             // tan of the half angle searched (1 = 45 degrees)
    float linked_cone = 2.2f;      // ... for nodes linked to the focus (about 65 degrees)
    float side_cost = 2.0f;        // how much being off the axis counts against a node
    float linked_discount = 0.45f; // linked nodes score at this share: lines win ties
    // ---- behaviour ----
    bool activate_locked = false; // confirm on a locked node reports `activated` too
    EdgeExits exits;              // directions with no node hand the focus back
    float entrance_step = 0.03f;  // seconds between nodes arriving; 0 for none
};

// A skill tree, a world map, a mission board: nodes anywhere, joined by
// lines. A direction moves the focus to the best node that way (the one most
// nearly straight ahead, preferring the drawn lines), a ring glides to it and
// the camera pans to keep it in view when the map is larger than its bounds.
//
//   ui::NodeMap tree;
//   tree.style.theme = theme;
//   tree.set_nodes({{0, 0, "Root", ui::NodeState::done}, {160, -80, "Dash"}}, {{0, 1}});
//   tree.set_bounds({96, 300, 1100, 560});
//   ...
//   if (tree.handle(input, feedback) == ui::Event::activated && can_afford(tree.focus()))
//       tree.set_state(tree.focus(), ui::NodeState::done);
//   tree.update(dt);
//   tree.draw(canvas);
class NodeMap
{
  public:
    // box is the node's square in the map's own pixels (the map's zoom and
    // camera are already applied to the canvas); focus is 0..1.
    using Slot = std::function<void(Canvas &canvas, const gfx::Rect &box, const MapNode &node,
                                    int index, float focus)>;

    NodeMapStyle style;
    Slot node_slot; // draws a node instead of the default (the ring and label stay)

    void set_nodes(std::vector<MapNode> nodes, std::vector<MapLink> links);
    const std::vector<MapNode> &nodes() const
    {
        return nodes_;
    }
    const std::vector<MapLink> &links() const
    {
        return links_;
    }
    MapNode &node(int index)
    {
        return nodes_[static_cast<std::size_t>(index)];
    }
    // Changes a node's state; a node that becomes done pops and its links light.
    void set_state(int index, NodeState state);
    void set_bounds(const gfx::Rect &bounds);
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }

    int focus() const
    {
        return focus_;
    }
    // Moves the focus without sound; snap skips the glide and the pan.
    void set_focus(int index, bool snap = true);
    void set_active(bool active)
    {
        active_ = active;
    }
    // Replays the entrance animation.
    void enter();

    // The node a direction leads to from `from`, or -1 when nothing lies
    // that way.
    int neighbour(int from, Direction direction) const;
    bool linked(int a, int b) const;

    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

    // The direction that had no node in the last handle() (see style.exits),
    // or Direction::none. handle() returned Event::none.
    Direction exit() const
    {
        return exit_;
    }
    // Where a node is on screen right now (camera and zoom applied).
    gfx::Rect node_rect(int index) const;

  private:
    float size_of(const MapNode &node) const;
    gfx::Rect box_of(int index) const; // in map pixels
    float node_radius(const gfx::Rect &box) const;
    void retarget(bool snap);
    void draw_node(Canvas &canvas, int index, float focus) const;

    std::vector<MapNode> nodes_;
    std::vector<MapLink> links_;
    std::vector<tween::Spring> lit_; // per link, 0..1 along it
    std::vector<Pulse> pops_;        // per node
    gfx::Rect bounds_{0.0f, 0.0f, 900.0f, 520.0f};
    int focus_ = 0;
    bool active_ = true;
    float age_ = 10.0f;
    Direction exit_ = Direction::none;
    Highlight highlight_;         // in map pixels
    tween::Spring cam_x_, cam_y_; // the map point shown at the bounds' corner
    Pulse press_;
};

} // namespace hui::ui
