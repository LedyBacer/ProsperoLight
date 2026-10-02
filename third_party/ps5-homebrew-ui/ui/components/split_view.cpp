// ps5-homebrew-ui - Component: SplitView.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/split_view.hpp"

#include <algorithm>
#include <cmath>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{
// True when the theme's page is one flat colour: the only thing a veil of
// that colour can be laid over without showing.
bool plain_page(const Theme &theme)
{
    const gfx::BackdropSpec &spec = theme.backdrop;
    const auto same = [](gfx::Color a, gfx::Color b)
    { return a.r == b.r && a.g == b.g && a.b == b.b; };
    return spec.mode == gfx::BackdropMode::gradient && spec.params[2] <= 0.0f &&
           same(spec.colors[0], spec.colors[1]) && same(spec.colors[0], theme.page);
}

gfx::Color opaque(gfx::Color over, gfx::Color base)
{
    const gfx::Color mixed = gfx::mix(base, over, tween::clamp01(over.a));
    return {mixed.r, mixed.g, mixed.b, 1.0f};
}

// A pane narrower than this is on its way out: its gap closes and it fades,
// so a collapsed pane leaves no empty strip behind.
constexpr float kFold = 90.0f;
// Room past a pane's edge inside its clip, for focus rings and shadows.
constexpr float kClipBleed = 14.0f;
} // namespace

// ---- ratio -------------------------------------------------------------------

void SplitView::set_ratio(float ratio, bool snap)
{
    style.ratio = tween::clamp01(ratio);
    if (snap)
        ratio_.snap(style.ratio);
}

void SplitView::set_preset(SplitPreset preset, bool snap)
{
    switch (preset)
    {
    case SplitPreset::collapsed:
        set_ratio(0.0f, snap);
        break;
    case SplitPreset::compact:
        set_ratio(style.compact, snap);
        break;
    case SplitPreset::regular:
        set_ratio(style.regular, snap);
        break;
    case SplitPreset::balanced:
        set_ratio(0.5f, snap);
        break;
    case SplitPreset::wide:
        set_ratio(style.wide, snap);
        break;
    }
}

float SplitView::ratio() const
{
    return started_ ? tween::clamp01(ratio_.value) : tween::clamp01(style.ratio);
}

bool SplitView::moving() const
{
    return started_ && (ratio_.value != ratio_.target || ratio_.velocity != 0.0f ||
                        third_.value != third_.target);
}

bool SplitView::collapsed() const
{
    const Shares now = shares(ratio(), started_ ? third_.value : style.third);
    return now.size[0] < kFold * 0.5f;
}

// ---- panes -------------------------------------------------------------------

SplitView::Shares SplitView::shares(float ratio, float third) const
{
    const float room = style.axis == SplitAxis::horizontal ? bounds_.w : bounds_.h;
    const float t = std::clamp(third, 0.0f, 0.8f);
    const float r = std::clamp(ratio, 0.0f, 1.0f - t);
    Shares out{};
    // A pane that is closing takes its gap with it.
    const float fold_first = tween::clamp01(r * room / kFold);
    const float fold_third = tween::clamp01(t * room / kFold);
    out.gap[0] = style.gap * fold_first;
    out.gap[1] = style.gap * fold_third;
    const float usable = std::max(room - out.gap[0] - out.gap[1], 0.0f);
    out.size[0] = r * usable;
    out.size[2] = t * usable;
    if (fold_first >= 1.0f)
        out.size[0] = std::max(out.size[0], std::min(style.min_pane, usable));
    out.size[1] = usable - out.size[0] - out.size[2];
    if (out.size[1] < style.min_pane)
    {
        // The middle pane keeps its minimum at the first pane's cost.
        out.size[1] = std::min(style.min_pane, std::max(usable - out.size[2], 0.0f));
        out.size[0] = std::max(usable - out.size[2] - out.size[1], 0.0f);
    }
    return out;
}

Rect SplitView::frame_for(const Shares &shares, int index) const
{
    index = std::clamp(index, 0, 2);
    float at = 0.0f;
    for (int i = 0; i < index; ++i)
        at += shares.size[i] + shares.gap[i];
    const float size = shares.size[index];
    if (style.axis == SplitAxis::horizontal)
        return {bounds_.x + at, bounds_.y, size, bounds_.h};
    return {bounds_.x, bounds_.y + at, bounds_.w, size};
}

Rect SplitView::inner(const Rect &frame) const
{
    if (!style.panels)
        return frame;
    const float pad = style.panel_padding;
    return {frame.x + pad, frame.y + pad, std::max(frame.w - 2.0f * pad, 0.0f),
            std::max(frame.h - 2.0f * pad, 0.0f)};
}

Rect SplitView::pane_frame(int index) const
{
    return frame_for(shares(ratio(), started_ ? third_.value : style.third), index);
}

Rect SplitView::pane_rect(int index) const
{
    return inner(pane_frame(index));
}

Rect SplitView::target_pane_rect(int index) const
{
    return inner(frame_for(shares(style.ratio, style.third), index));
}

Rect SplitView::divider_rect(int index) const
{
    index = std::clamp(index, 0, 1);
    const Shares now = shares(ratio(), started_ ? third_.value : style.third);
    float at = 0.0f;
    for (int i = 0; i <= index; ++i)
        at += now.size[i] + (i < index ? now.gap[i] : 0.0f);
    if (style.axis == SplitAxis::horizontal)
        return {bounds_.x + at, bounds_.y, now.gap[index], bounds_.h};
    return {bounds_.x, bounds_.y + at, bounds_.w, now.gap[index]};
}

// ---- focus -------------------------------------------------------------------

void SplitView::set_focus(int pane, bool snap)
{
    focus_ = std::clamp(pane, -1, pane_count() - 1);
    if (snap)
    {
        for (int i = 0; i < 3; ++i)
            emphasis_[i].snap(focus_ < 0 || i == focus_ ? 1.0f : 0.0f);
    }
}

float SplitView::emphasis(int index) const
{
    index = std::clamp(index, 0, 2);
    if (started_)
        return tween::clamp01(emphasis_[index].value);
    return focus_ < 0 || index == focus_ ? 1.0f : 0.0f;
}

// A pane that folds away fades with it.
float SplitView::presence(int index) const
{
    if (index == 1)
        return 1.0f;
    const Rect frame = pane_frame(index);
    return tween::clamp01((style.axis == SplitAxis::horizontal ? frame.w : frame.h) / kFold);
}

float SplitView::pane_opacity(int index) const
{
    return tween::lerp(1.0f - tween::clamp01(style.dim), 1.0f, emphasis(index)) * presence(index);
}

bool SplitView::veils() const
{
    if (style.dim_mode != SplitDim::automatic)
        return style.dim_mode == SplitDim::veil;
    return style.theme.style != SurfaceStyle::glass && plain_page(style.theme);
}

int SplitView::pane_toward(Direction direction) const
{
    int step = 0;
    if (style.axis == SplitAxis::horizontal)
        step = direction == Direction::right ? 1 : (direction == Direction::left ? -1 : 0);
    else
        step = direction == Direction::down ? 1 : (direction == Direction::up ? -1 : 0);
    const int target = focus_ + step;
    if (step == 0 || focus_ < 0 || target < 0 || target >= pane_count())
        return -1;
    if (target == 0 && collapsed())
        return -1;
    return target;
}

bool SplitView::cross(Direction direction, Feedback &feedback)
{
    const int target = pane_toward(direction);
    if (target < 0)
        return false;
    set_focus(target);
    play_cue(feedback, style, style.sounds.move, pane_frame(target).cx());
    return true;
}

Event SplitView::handle_exit(Direction direction, const InputFrame &input, Feedback &feedback)
{
    if (direction == Direction::none)
        return Event::none;
    if (cross(direction, feedback))
        return Event::moved;
    return refuse(feedback, style, input, refusal_, pane_frame(std::max(focus_, 0)).cx());
}

// ---- the five rules ----------------------------------------------------------

void SplitView::update(float dt)
{
    clock_ += dt;
    focus_ = std::clamp(focus_, -1, pane_count() - 1);
    if (!started_)
    {
        ratio_.snap(style.ratio);
        third_.snap(style.third);
        for (int i = 0; i < 3; ++i)
            emphasis_[i].snap(focus_ < 0 || i == focus_ ? 1.0f : 0.0f);
        started_ = true;
    }
    // A pane is a large thing: it moves a little slower than a highlight.
    const float omega =
        style.reduced_motion ? 60.0f : std::clamp(style.omega() * 0.8f, 9.0f, 26.0f);
    ratio_.target = tween::clamp01(style.ratio);
    ratio_.update(dt, omega, std::max(style.damping(), 0.7f));
    third_.target = std::clamp(style.third, 0.0f, 0.8f);
    third_.update(dt, omega);
    for (int i = 0; i < 3; ++i)
    {
        emphasis_[i].target = focus_ < 0 || i == focus_ ? 1.0f : 0.0f;
        emphasis_[i].update(dt, 16.0f);
    }
    refusal_.update(dt, 9.0f);
}

void SplitView::draw(Canvas &canvas) const
{
    const Shares now = shares(ratio(), started_ ? third_.value : style.third);
    const bool across = style.axis == SplitAxis::horizontal;
    const int panes = started_ && third_.value > 0.002f ? 3 : pane_count();

    if (style.panels)
    {
        Panel panel;
        panel.style.theme = style.theme;
        panel.style.reduced_motion = style.reduced_motion;
        panel.style.kind =
            style.panel_kind == PanelKind::titled ? PanelKind::plain : style.panel_kind;
        for (int i = 0; i < panes; ++i)
        {
            const Rect frame = frame_for(now, i);
            const float size = across ? frame.w : frame.h;
            const float present = i == 1 ? 1.0f : tween::clamp01(size / kFold);
            if (present <= 0.01f || size < 8.0f)
                continue;
            canvas.list.push_opacity(present);
            panel.draw(canvas, frame);
            canvas.list.pop_opacity();
        }
        return; // panels are parted by the gap itself
    }
    if (!style.divider)
        return;
    DividerStyle line;
    line.theme = style.theme;
    line.vertical = across;
    line.line = style.line;
    line.inset = style.divider_inset;
    line.on_panel = style.on_panel;
    for (int i = 0; i + 1 < panes; ++i)
    {
        const float present = tween::clamp01(now.gap[i] / std::max(style.gap, 1.0f));
        if (present <= 0.01f)
            continue;
        canvas.list.push_opacity(present);
        draw_divider(canvas, line, divider_rect(i));
        canvas.list.pop_opacity();
    }
}

void SplitView::begin_pane(Canvas &canvas, int index) const
{
    if (style.clip && moving())
        canvas.list.push_clip(pane_frame(index).inset(-kClipBleed));
    canvas.list.push_opacity(veils() ? presence(index) : pane_opacity(index));
    // The focused pane answers a refused crossing with a nudge.
    const float nudge = index == focus_ ? shake(refusal_.value, clock_, 10.0f) : 0.0f;
    const bool across = style.axis == SplitAxis::horizontal;
    canvas.list.push_transform(1.0f, 0.0f, 0.0f, across ? nudge : 0.0f, across ? 0.0f : nudge);
}

void SplitView::end_pane(Canvas &canvas, int index) const
{
    canvas.list.pop_transform();
    canvas.list.pop_opacity();
    const float wash = tween::clamp01(style.dim) * (1.0f - emphasis(index)) * presence(index);
    if (veils() && wash > 0.004f)
    {
        const Theme &theme = style.theme;
        const Color page{theme.page.r, theme.page.g, theme.page.b, 1.0f};
        const Rect frame = pane_frame(index);
        if (style.panels)
        {
            // Inside the pane's panel, in the panel's colour; its edge stays.
            const float edge = std::max(theme.border, 1.0f);
            const Rect in = frame.inset(edge);
            const float radius = std::max(
                std::min(theme.radius_card, std::min(frame.w, frame.h) * 0.5f) - edge, 0.0f);
            const Color fill = style.panel_kind == PanelKind::well
                                   ? opaque(theme.surface_high, opaque(theme.surface, page))
                                   : opaque(theme.surface, page);
            Painter paint(canvas.list, canvas.fonts, theme, canvas.glass);
            paint.fill(in, radius, fill.with_alpha(wash));
        }
        else
        {
            // On the page: past the pane's edges, where rings and shadows of
            // its content reach, but short of the divider.
            const bool across = style.axis == SplitAxis::horizontal;
            const float along = std::max(style.gap * 0.5f - 4.0f, 0.0f);
            const float beside = 14.0f;
            const Rect over{frame.x - (across ? along : beside),
                            frame.y - (across ? beside : along),
                            frame.w + 2.0f * (across ? along : beside),
                            frame.h + 2.0f * (across ? beside : along)};
            const Color fill = style.on_panel ? opaque(theme.surface, page) : page;
            canvas.list.rounded_rect(over, 0.0f, fill.with_alpha(wash));
        }
    }
    if (style.clip && moving())
        canvas.list.pop_clip();
}

} // namespace hui::ui
