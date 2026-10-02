// ps5-homebrew-ui - Component: NodeMap.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/node_map.hpp"

#include "ui/components/progress.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

// Where the camera should be on one axis. A map smaller than the view is
// centred; a larger one moves the least distance that keeps the focus
// `margin` inside the view, and never shows past its own ends.
float aim(float focus, float lo, float hi, float view, float margin, float current)
{
    if (hi - lo <= view)
        return (lo + hi) * 0.5f - view * 0.5f;
    margin = std::min(margin, view * 0.45f);
    float wanted = current;
    if (focus - margin < wanted)
        wanted = focus - margin;
    if (focus + margin > wanted + view)
        wanted = focus + margin - view;
    return std::clamp(wanted, lo, hi - view);
}

Color opaque(Color color)
{
    return {color.r, color.g, color.b, 1.0f};
}

// Themes built from hard edges have no soft light to spend on a glow.
bool lit_theme(const Theme &theme)
{
    return theme.style != SurfaceStyle::hard && theme.style != SurfaceStyle::pixel &&
           theme.style != SurfaceStyle::bevel && theme.style != SurfaceStyle::sketch &&
           theme.style != SurfaceStyle::neumorphic;
}

} // namespace

void NodeMap::set_nodes(std::vector<MapNode> nodes, std::vector<MapLink> links)
{
    nodes_ = std::move(nodes);
    links_.clear();
    const int count = static_cast<int>(nodes_.size());
    for (const MapLink &link : links)
    {
        if (link.from >= 0 && link.from < count && link.to >= 0 && link.to < count &&
            link.from != link.to)
            links_.push_back(link);
    }
    lit_.assign(links_.size(), tween::Spring{});
    pops_.assign(nodes_.size(), Pulse{});
    for (std::size_t i = 0; i < links_.size(); ++i)
    {
        const bool both =
            nodes_[static_cast<std::size_t>(links_[i].from)].state == NodeState::done &&
            nodes_[static_cast<std::size_t>(links_[i].to)].state == NodeState::done;
        lit_[i].snap(both ? 1.0f : 0.0f);
    }
    focus_ = std::clamp(focus_, 0, std::max(count - 1, 0));
    retarget(true);
}

void NodeMap::set_state(int index, NodeState state)
{
    if (index < 0 || index >= static_cast<int>(nodes_.size()))
        return;
    MapNode &target = nodes_[static_cast<std::size_t>(index)];
    if (target.state == state)
        return;
    target.state = state;
    if (state == NodeState::done && !style.reduced_motion)
        pops_[static_cast<std::size_t>(index)].trigger();
}

void NodeMap::set_bounds(const Rect &bounds)
{
    bounds_ = bounds;
    retarget(true);
}

void NodeMap::set_focus(int index, bool snap)
{
    if (nodes_.empty())
        return;
    focus_ = std::clamp(index, 0, static_cast<int>(nodes_.size()) - 1);
    retarget(snap);
}

void NodeMap::enter()
{
    age_ = 0.0f;
}

float NodeMap::size_of(const MapNode &node) const
{
    return node.size > 0.0f ? node.size : style.node_size;
}

Rect NodeMap::box_of(int index) const
{
    const MapNode &node = nodes_[static_cast<std::size_t>(index)];
    const float size = size_of(node);
    return {node.x - size * 0.5f, node.y - size * 0.5f, size, size};
}

// Round themes get discs; the others keep their own corner, so a node is the
// same kind of object as the theme's buttons.
float NodeMap::node_radius(const Rect &box) const
{
    const Theme &theme = style.theme;
    // Bevels, pixel boxes and pen strokes cannot be round.
    if (theme.style == SurfaceStyle::bevel || theme.style == SurfaceStyle::sketch ||
        theme.corner == Corner::pixel)
        return 0.0f;
    if (theme.corner == Corner::chamfer)
        return box.w * 0.3f;
    return theme.radius < 2.0f ? 0.0f : box.w * 0.5f;
}

Rect NodeMap::node_rect(int index) const
{
    const Rect box = box_of(index);
    const float z = style.zoom;
    return {bounds_.x + (box.x - cam_x_.value) * z, bounds_.y + (box.y - cam_y_.value) * z,
            box.w * z, box.h * z};
}

bool NodeMap::linked(int a, int b) const
{
    for (const MapLink &link : links_)
    {
        if ((link.from == a && link.to == b) || (link.from == b && link.to == a))
            return true;
    }
    return false;
}

// Which node does "left" mean, when nodes sit anywhere? Split the vector to
// each candidate into the distance travelled `along` the pressed direction
// and the `side` offset across it; drop what is behind or outside a cone;
// take the lowest along + side_cost * side. Counting the side offset double
// prefers "nearly straight ahead, a little further" over "closer, but well
// off to the side". Linked nodes get a wider cone and a discount, because
// following a drawn line is what the player means.
int NodeMap::neighbour(int from, Direction direction) const
{
    const int count = static_cast<int>(nodes_.size());
    if (direction == Direction::none || from < 0 || from >= count)
        return -1;
    const float dx =
        direction == Direction::right ? 1.0f : (direction == Direction::left ? -1.0f : 0.0f);
    const float dy =
        direction == Direction::down ? 1.0f : (direction == Direction::up ? -1.0f : 0.0f);
    const MapNode &origin = nodes_[static_cast<std::size_t>(from)];
    int best = -1;
    float best_score = 0.0f;
    for (int i = 0; i < count; ++i)
    {
        if (i == from)
            continue;
        const MapNode &other = nodes_[static_cast<std::size_t>(i)];
        const float vx = other.x - origin.x;
        const float vy = other.y - origin.y;
        const float along = vx * dx + vy * dy;
        const float side = std::fabs(vx * dy - vy * dx);
        if (along <= 0.0f)
            continue;
        const bool joined = linked(from, i);
        if (side > along * (joined ? style.linked_cone : style.cone))
            continue;
        const float score =
            (along + style.side_cost * side) * (joined ? style.linked_discount : 1.0f);
        if (best < 0 || score < best_score)
        {
            best = i;
            best_score = score;
        }
    }
    return best;
}

void NodeMap::retarget(bool snap)
{
    if (nodes_.empty())
        return;
    float lo_x = nodes_[0].x, hi_x = nodes_[0].x, lo_y = nodes_[0].y, hi_y = nodes_[0].y;
    for (const MapNode &node : nodes_)
    {
        const float half = size_of(node) * 0.5f;
        lo_x = std::min(lo_x, node.x - half);
        hi_x = std::max(hi_x, node.x + half);
        lo_y = std::min(lo_y, node.y - half);
        hi_y = std::max(hi_y, node.y + half);
    }
    lo_x -= style.padding;
    hi_x += style.padding;
    lo_y -= style.padding;
    hi_y += style.padding + (style.labels ? style.label_gap + style.label_size * 1.4f : 0.0f);

    const float z = std::max(style.zoom, 0.05f);
    const MapNode &at = nodes_[static_cast<std::size_t>(focus_)];
    cam_x_.target = aim(at.x, lo_x, hi_x, bounds_.w / z, style.margin / z, cam_x_.target);
    cam_y_.target = aim(at.y, lo_y, hi_y, bounds_.h / z, style.margin / z, cam_y_.target);
    highlight_.target(box_of(focus_));
    if (snap)
    {
        cam_x_.snap(cam_x_.target);
        cam_y_.snap(cam_y_.target);
        highlight_.snap(box_of(focus_));
    }
}

Event NodeMap::handle(const InputFrame &input, Feedback &feedback)
{
    exit_ = Direction::none;
    if (nodes_.empty())
        return Event::none;
    const float x = node_rect(focus_).cx();
    if (input.nav != Direction::none)
    {
        const int next = neighbour(focus_, input.nav);
        if (next < 0)
        {
            if (style.exits.allows(input.nav))
            {
                exit_ = input.nav;
                return Event::none;
            }
            return refuse(feedback, style, input, highlight_.refusal(), x);
        }
        focus_ = next;
        retarget(false);
        const MapNode &to = nodes_[static_cast<std::size_t>(focus_)];
        // Where the node will be once the camera settles: pan the tick there.
        play_cue(feedback, style, style.sounds.move,
                 bounds_.x + (to.x - cam_x_.target) * style.zoom);
        return Event::moved;
    }
    if (input.is_pressed(Action::confirm))
    {
        const MapNode &at = nodes_[static_cast<std::size_t>(focus_)];
        if (at.state == NodeState::locked && !style.activate_locked)
            return refuse(feedback, style, input, highlight_.refusal(), x);
        press_.trigger();
        play_cue(feedback, style, style.sounds.activate, x);
        if (style.sounds.rumble > 0.0f)
            feedback.rumble(0.3f * style.sounds.rumble, 0.04f);
        return Event::activated;
    }
    if (input.is_pressed(Action::back))
    {
        play_cue(feedback, style, style.sounds.cancel, x);
        return Event::cancelled;
    }
    return Event::none;
}

void NodeMap::update(float dt)
{
    age_ += dt;
    retarget(false);
    highlight_.update(dt, style);
    // The camera is slower than the ring: the eye follows the ring, and the
    // map catches up under it.
    const float pan = style.reduced_motion ? 60.0f : std::max(style.omega() * 0.55f, 7.0f);
    cam_x_.update(dt, pan);
    cam_y_.update(dt, pan);
    press_.update(dt, 10.0f);
    for (Pulse &pop : pops_)
        pop.update(dt, 5.0f);
    for (std::size_t i = 0; i < links_.size(); ++i)
    {
        const bool both =
            nodes_[static_cast<std::size_t>(links_[i].from)].state == NodeState::done &&
            nodes_[static_cast<std::size_t>(links_[i].to)].state == NodeState::done;
        lit_[i].target = both ? 1.0f : 0.0f;
        lit_[i].update(dt, style.reduced_motion ? 60.0f : 7.0f);
    }
}

void NodeMap::draw_node(Canvas &canvas, int index, float focus) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const MapNode &node = nodes_[static_cast<std::size_t>(index)];
    const Rect box = box_of(index);
    if (node_slot)
    {
        node_slot(canvas, box, node, index, focus);
        return;
    }
    const float radius = node_radius(box);
    const float s = box.w;
    const float cx = box.cx();
    const float cy = box.cy();
    const Color accent = node.color.a > 0.0f ? node.color : opaque(theme.accent);

    if (node.state == NodeState::done)
    {
        paint.surface(box, radius, accent, theme.outline, 1.0f);
        const Color ink = Painter::on(accent);
        const float pen = std::max(4.0f, s * 0.085f);
        list.line(cx - s * 0.2f, cy + s * 0.02f, cx - s * 0.05f, cy + s * 0.17f, pen, ink);
        list.line(cx - s * 0.05f, cy + s * 0.17f, cx + s * 0.22f, cy - s * 0.14f, pen, ink);
    }
    else if (node.state == NodeState::available)
    {
        // It asks to be taken: a slow light around it and a core in its colour.
        const float beat = style.reduced_motion ? 0.5f : breathe(canvas.time, 2.0f);
        if (lit_theme(theme))
            paint.halo(box, radius, 12.0f, accent.with_alpha(0.2f + 0.3f * beat));
        paint.surface(box, radius, solid_surface(theme), accent, 1.0f);
        paint.stroke(box, radius, std::max(theme.border, 3.0f), accent);
        const float core = s * (0.13f + 0.02f * beat);
        if (radius > 0.0f && theme.corner == Corner::round)
            list.circle(cx, cy, core, accent);
        else
            list.rounded_rect({cx - core, cy - core, 2.0f * core, 2.0f * core}, 0.0f, accent);
    }
    else
    {
        paint.well(box, radius, theme.surface_high);
        const Color ink = gfx::mix(theme.text_muted, theme.text, 0.5f * focus);
        const float w = s * 0.32f;
        const float h = s * 0.24f;
        list.arc(cx, cy - s * 0.03f, s * 0.12f, std::max(3.0f, s * 0.05f), -1.5707963f, 3.1415927f,
                 ink, false);
        list.rounded_rect({cx - w * 0.5f, cy - s * 0.03f, w, h},
                          theme.corner == Corner::round ? std::min(theme.radius, 4.0f) : 0.0f, ink);
    }
}

void NodeMap::draw(Canvas &canvas) const
{
    if (nodes_.empty())
        return;
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const bool calm = style.reduced_motion;
    const float z = std::max(style.zoom, 0.05f);
    const Color ink = style.on_surface ? theme.text : paint.page_text();
    const Color quiet = style.on_surface ? theme.text_muted : paint.page_text_muted();
    const int count = static_cast<int>(nodes_.size());

    const auto entrance = [&](int index)
    {
        if (style.entrance_step <= 0.0f || calm)
            return tween::cubic_out(age_ / 0.2f);
        return tween::stagger(age_, index, style.entrance_step, 0.34f);
    };

    list.push_clip(bounds_);
    // From here on everything is in the map's own pixels.
    list.push_transform(z, 0.0f, 0.0f, bounds_.x - cam_x_.value * z, bounds_.y - cam_y_.value * z);

    for (std::size_t i = 0; i < links_.size(); ++i)
    {
        const MapNode &a = nodes_[static_cast<std::size_t>(links_[i].from)];
        const MapNode &b = nodes_[static_cast<std::size_t>(links_[i].to)];
        const float length = std::hypot(b.x - a.x, b.y - a.y);
        const float start = size_of(a) * 0.5f;
        const float end = length - size_of(b) * 0.5f;
        if (end <= start + 1.0f)
            continue;
        // The line runs between the nodes' edges: a translucent node must
        // not show it passing underneath.
        const float ux = (b.x - a.x) / length;
        const float uy = (b.y - a.y) / length;
        const auto point = [&](float t, float *x, float *y)
        {
            const float d = tween::lerp(start, end, t);
            *x = a.x + ux * d;
            *y = a.y + uy * d;
        };
        float x0, y0, x1, y1;
        point(0.0f, &x0, &y0);
        point(1.0f, &x1, &y1);
        const float in = std::min(entrance(links_[i].from), entrance(links_[i].to));
        const Color accent = b.color.a > 0.0f ? b.color : opaque(theme.accent);
        list.push_opacity(in);
        list.line(x0, y0, x1, y1, style.link_width, quiet.with_alpha(0.36f));
        const float lit = tween::clamp01(lit_[i].value);
        if (lit > 0.01f)
        {
            point(lit, &x1, &y1);
            list.line(x0, y0, x1, y1, style.link_width, accent);
        }
        const bool leads = (a.state == NodeState::done && b.state == NodeState::available) ||
                           (b.state == NodeState::done && a.state == NodeState::available);
        if (style.flow && leads)
        {
            // Dots walk from the finished node to the one it opened.
            const bool forward = a.state == NodeState::done;
            const int dots = std::max(2, static_cast<int>((end - start) / 34.0f));
            const float phase = calm ? 0.5f : canvas.time * 0.7f - std::floor(canvas.time * 0.7f);
            for (int k = 0; k < dots; ++k)
            {
                const float t = (static_cast<float>(k) + phase) / static_cast<float>(dots);
                float x, y;
                point(forward ? t : 1.0f - t, &x, &y);
                list.circle(x, y, style.link_width * 0.85f,
                            accent.with_alpha(std::sin(t * 3.14159265f)));
            }
        }
        list.pop_opacity();
    }

    const float active = active_ ? 1.0f : 0.4f;
    for (int i = 0; i < count; ++i)
    {
        const float in = entrance(i);
        if (in <= 0.0f)
            continue;
        const Rect box = box_of(i);
        const float focus = highlight_.coverage(box) * active;
        const float pop = pops_[static_cast<std::size_t>(i)].value;
        const float press = i == focus_ ? press_.value : 0.0f;
        const float scale = calm ? 1.0f
                                 : tween::lerp(0.6f, 1.0f, tween::back_out(in)) *
                                       (1.0f + 0.07f * focus + 0.2f * pop - 0.05f * press);
        list.push_opacity(in);
        list.push_transform(scale, box.cx(), box.cy(), 0.0f, 0.0f);
        draw_node(canvas, i, focus);
        list.pop_transform();
        list.pop_opacity();
    }

    HighlightStyle ring;
    ring.kind = HighlightKind::ring;
    ring.radius = node_radius(box_of(focus_));
    // A bevel theme's indicator is a dotted box drawn inside what it marks;
    // on a filled node it would vanish, so it is pushed out around the node.
    ring.grow = theme.style == SurfaceStyle::bevel ? 11.0f : style.node_size * 0.035f;
    highlight_.draw(canvas, style, ring, active * entrance(focus_));

    if (style.labels)
    {
        // A hard theme's node stands on a solid shadow: the label clears it.
        const float below = theme.style == SurfaceStyle::hard ? theme.shadow_offset : 0.0f;
        for (int i = 0; i < count; ++i)
        {
            const MapNode &node = nodes_[static_cast<std::size_t>(i)];
            if (node.label.empty())
                continue;
            const Rect box = box_of(i);
            const float focus = highlight_.coverage(box) * active;
            const Color rest = node.state == NodeState::locked ? quiet : gfx::mix(quiet, ink, 0.7f);
            list.push_opacity(entrance(i));
            paint.label(fit_label(paint, node.label, style.label_size, style.label_width), box.cx(),
                        box.y + box.h + style.label_gap + below + style.label_size * 0.8f,
                        style.label_size, gfx::mix(rest, ink, focus), gfx::Align::center);
            list.pop_opacity();
        }
    }

    list.pop_transform();
    list.pop_clip();
}

} // namespace hui::ui
