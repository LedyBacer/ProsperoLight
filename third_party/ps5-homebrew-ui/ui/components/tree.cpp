// ps5-homebrew-ui - Component: TreeView.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/tree.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

// A chevron pointing right at angle 0, turned clockwise about its centre.
void draw_chevron(gfx::DrawList &list, float cx, float cy, float half, float angle, float width,
                  Color color)
{
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const auto px = [&](float x, float y) { return cx + x * c - y * s; };
    const auto py = [&](float x, float y) { return cy + x * s + y * c; };
    const float reach = half * 0.55f;
    list.line(px(-reach, -half), py(-reach, -half), px(reach, 0.0f), py(reach, 0.0f), width, color);
    list.line(px(reach, 0.0f), py(reach, 0.0f), px(-reach, half), py(-reach, half), width, color);
}

} // namespace

// Depth first, so a node's descendants are the rows right after it: the tree
// is then one flat list in which a closed branch hides a contiguous range.
void TreeView::flatten(std::vector<TreeNode> &nodes, int parent, int depth)
{
    for (TreeNode &node : nodes)
    {
        const std::size_t index = rows_.size();
        std::vector<TreeNode> children = std::move(node.children);
        node.children.clear();
        Row row;
        row.node = std::move(node);
        row.parent = parent;
        row.depth = depth;
        row.children = static_cast<int>(children.size());
        rows_.push_back(std::move(row));
        flatten(children, static_cast<int>(index), depth + 1);
        rows_[index].end = static_cast<int>(rows_.size());
    }
}

void TreeView::set_nodes(std::vector<TreeNode> roots)
{
    rows_.clear();
    flatten(roots, -1, 0);
    focus_ = std::clamp(focus_, 0, std::max(count() - 1, 0));
    while (count() > 0 && !is_showing(focus_))
        focus_ = parent(focus_);
    snap_rows();
    retarget(true);
    place_highlight();
}

int TreeView::parent(int index) const
{
    return rows_[static_cast<std::size_t>(index)].parent;
}

int TreeView::depth(int index) const
{
    return rows_[static_cast<std::size_t>(index)].depth;
}

int TreeView::child_count(int index) const
{
    return rows_[static_cast<std::size_t>(index)].children;
}

bool TreeView::is_expanded(int index) const
{
    const Row &row = rows_[static_cast<std::size_t>(index)];
    return row.children > 0 && row.node.expanded;
}

bool TreeView::is_showing(int index) const
{
    for (int up = parent(index); up >= 0; up = parent(up))
    {
        if (!is_expanded(up))
            return false;
    }
    return true;
}

void TreeView::set_expanded(int index, bool expanded, bool snap)
{
    if (index < 0 || index >= count() || child_count(index) == 0)
        return;
    Row &row = rows_[static_cast<std::size_t>(index)];
    row.node.expanded = expanded;
    // The focus cannot stay on a row that is going away.
    if (!expanded && focus_ > index && focus_ < row.end)
        focus_ = index;
    if (snap)
        snap_rows();
    retarget(snap);
    place_highlight();
}

void TreeView::expand_all(bool snap)
{
    for (Row &row : rows_)
        row.node.expanded = row.children > 0;
    if (snap)
        snap_rows();
    retarget(snap);
    place_highlight();
}

void TreeView::collapse_all(bool snap)
{
    for (Row &row : rows_)
        row.node.expanded = false;
    while (count() > 0 && parent(focus_) >= 0)
        focus_ = parent(focus_);
    if (snap)
        snap_rows();
    retarget(snap);
    place_highlight();
}

void TreeView::set_bounds(const Rect &bounds)
{
    bounds_ = bounds;
    retarget(true);
    place_highlight();
}

void TreeView::set_focus(int index, bool snap)
{
    if (rows_.empty())
        return;
    const int next = std::clamp(index, 0, count() - 1);
    for (int up = parent(next); up >= 0; up = parent(up))
        rows_[static_cast<std::size_t>(up)].node.expanded = true;
    if (snap)
        snap_rows();
    else
        glide_.value += top(focus_, false) - top(next, false);
    focus_ = next;
    retarget(snap);
    place_highlight();
}

void TreeView::enter()
{
    age_ = 0.0f;
}

Rect TreeView::inner() const
{
    return style.panel ? bounds_.inset(style.panel_padding) : bounds_;
}

// The next row that is showing in a direction, or -1 at the end.
int TreeView::step(int from, int direction) const
{
    for (int i = from + direction; i >= 0 && i < count(); i += direction)
    {
        if (is_showing(i))
            return i;
    }
    return -1;
}

// Where a row starts in content space: as it is now, or as it will be once
// every row has finished appearing or disappearing (the scroll aims there).
float TreeView::top(int index, bool settled) const
{
    const float pitch = style.row_height + style.gap;
    float y = 0.0f;
    if (!settled)
    {
        for (int i = 0; i < index; ++i)
            y += pitch * rows_[static_cast<std::size_t>(i)].shown.value;
        return y;
    }
    int i = 0;
    while (i < index)
    {
        y += pitch;
        // A closed branch hides everything up to its end.
        i = is_expanded(i) ? i + 1 : rows_[static_cast<std::size_t>(i)].end;
    }
    return y;
}

float TreeView::total(bool settled) const
{
    return rows_.empty() ? 0.0f : std::max(top(count(), settled) - style.gap, 0.0f);
}

Rect TreeView::row_rect(int index) const
{
    const Rect in = inner();
    return {in.x, in.y + top(index, false) - scroll_.offset(), in.w, style.row_height};
}

void TreeView::snap_rows()
{
    int hidden_until = 0;
    for (int i = 0; i < count(); ++i)
    {
        Row &row = rows_[static_cast<std::size_t>(i)];
        row.shown.snap(i >= hidden_until ? 1.0f : 0.0f);
        row.turn.snap(is_expanded(i) ? 1.0f : 0.0f);
        if (!is_expanded(i))
            hidden_until = std::max(hidden_until, row.end);
    }
}

void TreeView::retarget(bool snap)
{
    if (rows_.empty())
        return;
    const Rect in = inner();
    const float start = top(focus_, true);
    float span = style.row_height;
    if (is_expanded(focus_))
    {
        // An open branch brings its children into view, as far as they fit.
        const float children = top(rows_[static_cast<std::size_t>(focus_)].end, true) - start -
                               style.row_height - style.gap;
        span += std::clamp(children, 0.0f, std::max(in.h - 2.0f * style.row_height, 0.0f));
    }
    scroll_.reveal(start, start + span, in.h, style.row_height * 0.5f, total(true));
    if (snap)
    {
        scroll_.position.snap(scroll_.position.target);
        glide_.snap(0.0f);
    }
}

void TreeView::place_highlight()
{
    if (rows_.empty())
        return;
    highlight_.snap({0.0f, top(focus_, false) + glide_.value, inner().w, style.row_height});
}

void TreeView::move_to(int index, Feedback &feedback)
{
    glide_.value += top(focus_, false) - top(index, false);
    focus_ = index;
    retarget(false);
    play_cue(feedback, style, style.sounds.move, bounds_.cx(),
             style.pitch_by_depth
                 ? std::max(0.86f, 1.06f - 0.04f * static_cast<float>(depth(index)))
                 : 1.0f);
}

Event TreeView::handle(const InputFrame &input, Feedback &feedback)
{
    exit_ = Direction::none;
    if (rows_.empty())
        return Event::none;
    const float x = bounds_.cx();
    Row &row = rows_[static_cast<std::size_t>(focus_)];
    const bool branch = row.children > 0;
    const auto leave_or_refuse = [&]()
    {
        if (style.exits.allows(input.nav))
        {
            exit_ = input.nav;
            return Event::none;
        }
        return refuse(feedback, style, input, highlight_.refusal(), x);
    };
    const auto toggle = [&]()
    {
        row.node.expanded = !row.node.expanded;
        press_.trigger();
        retarget(false);
        play_cue(feedback, style, style.sounds.change, x, row.node.expanded ? 1.04f : 0.96f);
        return Event::changed;
    };

    if (input.nav == Direction::up || input.nav == Direction::down)
    {
        const int direction = input.nav == Direction::down ? 1 : -1;
        int next = step(focus_, direction);
        if (next < 0)
        {
            if (style.exits.allows(input.nav))
            {
                exit_ = input.nav;
                return Event::none;
            }
            if (style.wrap && !input.nav_repeat)
                next = step(direction > 0 ? -1 : count(), direction);
        }
        if (next < 0 || next == focus_)
            return refuse(feedback, style, input, highlight_.refusal(), x);
        move_to(next, feedback);
        return Event::moved;
    }
    if (input.nav == Direction::right)
    {
        if (!branch)
            return leave_or_refuse();
        if (!row.node.expanded)
            return toggle();
        move_to(focus_ + 1, feedback); // the first child is the next row
        return Event::moved;
    }
    if (input.nav == Direction::left)
    {
        if (branch && row.node.expanded)
            return toggle();
        if (row.parent < 0)
            return leave_or_refuse();
        move_to(row.parent, feedback);
        return Event::moved;
    }
    if (input.is_pressed(Action::confirm))
    {
        if (row.node.disabled)
            return refuse(feedback, style, input, highlight_.refusal(), x);
        if (branch && style.confirm_toggles)
            return toggle();
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

void TreeView::update(float dt)
{
    age_ += dt;
    const float omega = std::max(style.omega(), 14.0f);
    int hidden_until = 0;
    for (int i = 0; i < count(); ++i)
    {
        Row &row = rows_[static_cast<std::size_t>(i)];
        row.shown.target = i >= hidden_until ? 1.0f : 0.0f;
        row.shown.update(dt, omega);
        row.turn.target = is_expanded(i) ? 1.0f : 0.0f;
        row.turn.update(dt, std::max(omega, 18.0f));
        if (!is_expanded(i))
            hidden_until = std::max(hidden_until, row.end);
    }
    glide_.target = 0.0f;
    glide_.update(dt, std::max(style.omega(), 18.0f), std::max(style.damping(), 0.78f));
    highlight_.update(dt, style);
    place_highlight();
    // A branch that closed may have left the scroll past the new end.
    const float limit = std::max(total(true) - inner().h, 0.0f);
    scroll_.position.target = std::min(scroll_.position.target, limit);
    scroll_.update(dt, omega);
    active_amount_.target = active_ ? 1.0f : 0.0f;
    active_amount_.update(dt, 18.0f);
    press_.update(dt, 10.0f);
}

void TreeView::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    if (style.panel)
        paint.panel(bounds_);
    if (rows_.empty())
        return;

    const Rect in = inner();
    const float scroll = scroll_.offset();
    const float height = total(false);
    const bool overflow = std::max(height, total(true)) > in.h + 0.5f;
    const float bleed = style.highlight.kind == HighlightKind::glow ? 30.0f : 10.0f;
    const float above = overflow ? std::min(bleed, 9.0f) : bleed;
    const Rect clip{in.x - bleed, in.y - above, in.w + 2.0f * bleed, in.h + 2.0f * above};
    list.push_clip(clip);

    const Color strong = style.panel ? theme.text : paint.page_text();
    const Color muted = style.panel ? theme.text_muted : paint.page_text_muted();
    const float pitch = style.row_height + style.gap;

    // The highlight is kept in content space; bring it to the screen here.
    list.push_transform(1.0f, 0.0f, 0.0f, in.x, in.y - scroll);
    {
        HighlightStyle look = style.highlight;
        look.grow -= 2.0f * press_.value; // a press pushes it in for a moment
        highlight_.draw(canvas, style, look,
                        (0.35f + 0.65f * active_amount_.value) * tween::cubic_out(age_ / 0.3f));
    }
    list.pop_transform();

    float y = in.y - scroll;
    int order = 0; // position among the rows on screen: staggers the entrance
    for (int i = 0; i < count(); ++i)
    {
        const Row &row = rows_[static_cast<std::size_t>(i)];
        const float shown = row.shown.value;
        if (shown <= 0.01f)
            continue;
        const Rect r{in.x, y, in.w, style.row_height};
        y += pitch * shown;
        const int place = order++;
        if (r.y > in.y + in.h || r.y + r.h < in.y)
            continue;
        const float arrive = style.entrance_step <= 0.0f || style.reduced_motion
                                 ? tween::cubic_out(age_ / 0.2f)
                                 : tween::stagger(age_, place, style.entrance_step, 0.32f);
        float alpha = tween::smoothstep((shown - 0.25f) / 0.75f) * arrive;
        if (overflow && style.edge_fade > 0.0f)
        {
            const float visible = std::min(r.y + r.h - in.y, in.y + in.h - r.y);
            alpha *= tween::clamp01(visible / (r.h * style.edge_fade));
        }
        if (alpha <= 0.0f)
            continue;

        // A row on its way in or out shows only the part it has room for.
        const bool partial = shown < 0.995f;
        if (partial)
            list.push_clip({clip.x, r.y - style.gap, clip.w, pitch * shown});
        list.push_opacity(alpha);
        list.push_transform(1.0f, 0.0f, 0.0f, 0.0f,
                            style.reduced_motion ? 0.0f : 14.0f * (1.0f - arrive));

        const float base = in.x + style.padding;
        if (style.guides && row.depth > 0)
        {
            // One line per level, joined from row to row through the gap.
            const float width = std::clamp(theme.border, 1.5f, 2.5f);
            for (int level = 0; level < row.depth; ++level)
            {
                const float gx =
                    base + static_cast<float>(level) * style.indent + style.chevron_width * 0.5f;
                list.rounded_rect({gx - width * 0.5f, r.y - style.gap, width, pitch}, 0.0f,
                                  muted.with_alpha(0.3f));
            }
        }

        // The row's place in content space, where the highlight lives.
        const float under = highlight_.coverage({0.0f, r.y - in.y + scroll, in.w, r.h});
        const float focus = under * active_amount_.value;
        Color ink = Highlight::text_color(style, style.highlight, focus, strong);
        // On a plate in the primary colour the quiet text follows the ink
        // all the way, also while the tree is inactive and the plate is
        // faint: the muted colour would not read on either.
        Color quiet = style.highlight.kind == HighlightKind::fill
                          ? gfx::mix(muted, ink.with_alpha(0.8f), under)
                          : gfx::mix(muted, ink, focus * 0.6f);
        if (row.node.disabled)
        {
            ink = ink.with_alpha(0.42f);
            quiet = quiet.with_alpha(0.42f);
        }
        float left = base + static_cast<float>(row.depth) * style.indent;
        float right = in.x + in.w - style.padding;
        if (row.children > 0)
            draw_chevron(list, left + style.chevron_width * 0.5f, r.cy(), 7.0f,
                         row.turn.value * 1.5707963f, 2.5f, quiet);
        left += style.chevron_width;
        if (style.icon_width > 0.0f)
        {
            if (icon)
                icon(canvas, {left, r.y, style.icon_width, r.h}, row.node, i, focus, ink);
            left += style.icon_width + 12.0f;
        }
        else
        {
            left += 6.0f;
        }
        if (!row.node.value.empty())
        {
            right -= paint.label(row.node.value, right, r.cy() + style.value_size * 0.34f,
                                 style.value_size, quiet, gfx::Align::right);
            right -= 14.0f;
        }
        if (!row.node.badge.empty())
        {
            const float width = paint.label_width(row.node.badge, style.badge_size) + 22.0f;
            const Rect pill{right - width, r.cy() - 14.0f, width, 28.0f};
            paint.fill(pill, theme.pill_chips ? 14.0f : std::min(theme.radius, 14.0f),
                       theme.accent);
            paint.label(row.node.badge, pill.cx(), pill.cy() + style.badge_size * 0.34f,
                        style.badge_size, Painter::on(theme.accent), gfx::Align::center);
            right -= width + 14.0f;
        }
        paint.label(
            fit_label(paint, row.node.label, style.label_size, std::max(right - left, 40.0f)), left,
            r.cy() + style.label_size * 0.34f, style.label_size, ink);

        list.pop_transform();
        list.pop_opacity();
        if (partial)
            list.pop_clip();
    }
    list.pop_clip();

    if (style.scroll_thumb && overflow)
    {
        const float span = std::max(height, in.h + 1.0f);
        const float track = in.h - 16.0f;
        const float size = std::max(track * in.h / span, 36.0f);
        const float at = scroll / std::max(span - in.h, 1.0f);
        const float x = in.x + in.w + 8.0f;
        list.rounded_rect({x, in.y + 8.0f, 4.0f, track}, 2.0f, muted.with_alpha(0.16f));
        list.rounded_rect({x, in.y + 8.0f + (track - size) * tween::clamp01(at), 4.0f, size}, 2.0f,
                          muted.with_alpha(0.7f));
    }
}

} // namespace hui::ui
