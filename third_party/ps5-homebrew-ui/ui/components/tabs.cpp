// ps5-homebrew-ui - Component: TabBar.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/tabs.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

constexpr float kBadgeGap = 10.0f;

bool framed(SurfaceStyle style)
{
    return style == SurfaceStyle::hard || style == SurfaceStyle::pixel ||
           style == SurfaceStyle::bevel;
}

std::string badge_text(int count)
{
    char text[8];
    if (count > 99)
        std::snprintf(text, sizeof(text), "99+");
    else
        std::snprintf(text, sizeof(text), "%d", count);
    return text;
}

float badge_width(const Painter &paint, const TabBarStyle &style, int count)
{
    const float height = style.badge_size + 10.0f;
    return std::max(paint.label_width(badge_text(count), style.badge_size) + 14.0f, height);
}

Rect lerp_rect(const Rect &a, const Rect &b, float t)
{
    return {tween::lerp(a.x, b.x, t), tween::lerp(a.y, b.y, t), tween::lerp(a.w, b.w, t),
            tween::lerp(a.h, b.h, t)};
}

// The plate that marks the active tab, drawn the way the theme draws its
// main action. Returns the colour text on it takes. A plate inside a well
// (in_well) casts no shadow of its own: it would fall on the well's floor.
Color draw_plate(Painter &paint, const Theme &theme, const Rect &r, float radius, bool in_well)
{
    switch (theme.style)
    {
    case SurfaceStyle::glow:
        // On lit consoles the edge carries the colour, not the fill.
        paint.surface(r, radius, theme.surface, theme.primary, 1.0f);
        return theme.primary;
    case SurfaceStyle::neumorphic:
        paint.surface(r, radius, theme.surface, theme.outline, 1.0f);
        return theme.primary;
    case SurfaceStyle::bevel:
        // A raised button face in a sunken track, like a scroll bar's thumb.
        paint.surface(r, 0.0f, theme.secondary, theme.outline, 1.0f);
        return theme.on_secondary;
    case SurfaceStyle::hard:
    case SurfaceStyle::pixel:
    case SurfaceStyle::sketch:
        if (in_well)
        {
            paint.fill(r, radius, theme.primary);
            paint.stroke(r, radius, theme.border,
                         theme.style == SurfaceStyle::sketch ? theme.on_primary : theme.outline);
        }
        else
        {
            paint.surface(r, radius, theme.primary,
                          theme.style == SurfaceStyle::sketch ? theme.on_primary : theme.outline,
                          1.0f, theme.border);
        }
        return theme.on_primary;
    default:
        paint.surface(r, radius, theme.primary, theme.primary, 1.0f, in_well ? 0.0f : -1.0f);
        return theme.on_primary;
    }
}

} // namespace

void TabBar::set_tabs(std::vector<TabItem> tabs)
{
    tabs_ = std::move(tabs);
    const int count = static_cast<int>(tabs_.size());
    active_ = std::clamp(active_, 0, std::max(count - 1, 0));
    if (count > 0 && tabs_[static_cast<std::size_t>(active_)].disabled)
    {
        const int forward = next(active_, 1);
        active_ = forward >= 0 ? forward : std::max(next(active_, -1), 0);
    }
    value_.snap(static_cast<float>(active_));
}

void TabBar::set_active(int index, bool snap)
{
    if (tabs_.empty())
        return;
    active_ = std::clamp(index, 0, static_cast<int>(tabs_.size()) - 1);
    value_.target = static_cast<float>(active_);
    if (snap)
        value_.snap(value_.target);
}

// The next tab that can be chosen in a direction, or -1 at the end.
int TabBar::next(int from, int direction) const
{
    const int count = static_cast<int>(tabs_.size());
    for (int i = from + direction; i >= 0 && i < count; i += direction)
    {
        if (!tabs_[static_cast<std::size_t>(i)].disabled)
            return i;
    }
    return -1;
}

// Roughly where a tab is, for panning its cue: exact positions need fonts.
float TabBar::x_of(int index) const
{
    const float count = static_cast<float>(std::max<std::size_t>(tabs_.size(), 1));
    return bounds_.x + bounds_.w * (static_cast<float>(index) + 0.5f) / count;
}

Event TabBar::step(int direction, const InputFrame &input, Feedback &feedback)
{
    if (tabs_.empty() || direction == 0)
        return Event::none;
    direction = direction > 0 ? 1 : -1;
    int target = next(active_, direction);
    if (target < 0 && style.wrap && !input.nav_repeat)
        target = next(direction > 0 ? -1 : static_cast<int>(tabs_.size()), direction);
    if (target < 0 || target == active_)
        return refuse(feedback, style, input, refusal_, x_of(active_));
    active_ = target;
    value_.target = static_cast<float>(active_);
    if (!style.reduced_motion)
        change_.trigger();
    const float along = tabs_.size() > 1
                            ? static_cast<float>(active_) / static_cast<float>(tabs_.size() - 1)
                            : 0.0f;
    play_cue(feedback, style, style.sounds.page, x_of(active_),
             style.pitch_by_position ? tween::lerp(0.96f, 1.06f, along) : 1.0f);
    return Event::changed;
}

Event TabBar::handle(const InputFrame &input, Feedback &feedback)
{
    if (input.nav == Direction::left)
        return step(-1, input, feedback);
    if (input.nav == Direction::right)
        return step(1, input, feedback);
    return Event::none;
}

void TabBar::update(float dt)
{
    // The indicator must feel immediate whatever the theme's pace, and a hard
    // bounce between two tabs reads as a glitch: both are clamped.
    value_.update(dt, std::max(style.omega(), 16.0f), std::max(style.damping(), 0.72f));
    focus_amount_.target = focused_ ? 1.0f : 0.0f;
    focus_amount_.update(dt, 18.0f);
    refusal_.update(dt, 9.0f);
    change_.update(dt, 10.0f);
}

float TabBar::content_width(const Painter &paint, const TabItem &item) const
{
    float width = paint.label_width(item.label, style.text_size);
    if (style.glyph_width > 0.0f)
        width += style.glyph_width + (item.label.empty() ? 0.0f : style.glyph_gap);
    if (item.badge > 0)
        width += kBadgeGap + badge_width(paint, style, item.badge);
    return width;
}

// The whole geometry, from the style and the animated tab alone. It is
// computed where it is needed (labels are measured with the theme's face, and
// only drawing has the fonts), so there is no layout to keep in step when a
// knob or the theme changes.
TabBar::Layout TabBar::layout(const Painter &paint) const
{
    Layout out;
    const int count = static_cast<int>(tabs_.size());
    if (count == 0)
        return out;
    const float gap = style.kind == TabKind::segmented ? 0.0f : style.gap;
    const float y = bounds_.cy() - style.height * 0.5f;

    std::vector<float> widths(static_cast<std::size_t>(count));
    float widest = 0.0f;
    for (int i = 0; i < count; ++i)
    {
        const float width = std::max(content_width(paint, tabs_[static_cast<std::size_t>(i)]) +
                                         2.0f * style.padding,
                                     style.min_width);
        widths[static_cast<std::size_t>(i)] = width;
        widest = std::max(widest, width);
    }
    if (style.width == TabWidth::equal)
        std::fill(widths.begin(), widths.end(), widest);
    else if (style.width == TabWidth::fill)
        std::fill(
            widths.begin(), widths.end(),
            std::max((bounds_.w - gap * static_cast<float>(count - 1)) / static_cast<float>(count),
                     style.min_width));

    float x = 0.0f;
    for (int i = 0; i < count; ++i)
    {
        out.tabs.push_back({x, y, widths[static_cast<std::size_t>(i)], style.height});
        x += widths[static_cast<std::size_t>(i)] + gap;
    }
    const float total = x - gap;
    out.overflow = total > bounds_.w + 0.5f;

    // The indicator sits between the two tabs the animated value is between.
    // Past either end (a bounce) it leans out a little instead of leaving.
    const float value = value_.value;
    const int low = std::clamp(static_cast<int>(std::floor(value)), 0, count - 1);
    const int high = std::min(low + 1, count - 1);
    const float t = value - static_cast<float>(low);
    const Rect &from = out.tabs[static_cast<std::size_t>(low)];
    if (high == low || t < 0.0f)
        out.indicator = {from.x + t * (from.w + gap) * 0.35f, from.y, from.w, from.h};
    else
        out.indicator = lerp_rect(from, out.tabs[static_cast<std::size_t>(high)], t);

    // The row scrolls to keep the indicator in the middle. Because the scroll
    // is derived from the indicator, it glides with it and needs no state.
    float shift = bounds_.x;
    if (out.overflow)
        shift -= std::clamp(out.indicator.cx() - bounds_.w * 0.5f, 0.0f, total - bounds_.w);
    for (Rect &tab : out.tabs)
        tab.x += shift;
    out.indicator.x += shift;
    out.row = {shift, y, total, style.height};
    return out;
}

Rect TabBar::tab_rect(const Fonts &fonts, int index) const
{
    if (index < 0 || index >= static_cast<int>(tabs_.size()))
        return bounds_;
    // A painter only measures here; the list it is given stays empty.
    gfx::DrawList scratch;
    const Painter paint(scratch, fonts, style.theme);
    return layout(paint).tabs[static_cast<std::size_t>(index)];
}

void TabBar::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const int count = static_cast<int>(tabs_.size());
    if (count == 0)
        return;

    const Layout at = layout(paint);
    const bool welled =
        style.kind == TabKind::segmented || (style.kind == TabKind::pill && style.track);
    // Only labels that sit on nothing take the page's colours: a track and a
    // boxed tab are surfaces, and text on them is the theme's.
    const bool bare = style.on_page && !welled && style.kind != TabKind::boxed;
    const Color text = bare ? paint.page_text() : theme.text;
    const Color muted = bare ? paint.page_text_muted() : theme.text_muted;
    const float nudge = shake(refusal_.value, canvas.time, 8.0f);
    Rect indicator = at.indicator;
    indicator.x += nudge;

    if (at.overflow)
        list.push_clip({bounds_.x, at.row.y - 16.0f, bounds_.w, at.row.h + 32.0f});
    // What of the row can be seen: the track and the ring are drawn around it.
    const float seen_left = std::max(at.row.x, bounds_.x);
    const float seen_right = std::min(at.row.x + at.row.w, bounds_.x + bounds_.w);
    const Rect seen{seen_left, at.row.y, seen_right - seen_left, at.row.h};

    const float row_radius = paint.control_radius(at.row);
    Color active_ink = text;
    Rect ring = indicator;
    float ring_radius = paint.control_radius(indicator);

    switch (style.kind)
    {
    case TabKind::pill:
    case TabKind::segmented:
    {
        if (welled)
        {
            paint.well(at.row, row_radius, theme.surface_high);
            ring = seen;
            ring_radius = row_radius;
        }
        if (style.kind == TabKind::segmented && style.dividers)
        {
            const Color hair =
                theme.border > 0.0f ? theme.outline : theme.text_muted.with_alpha(0.3f);
            for (int i = 0; i + 1 < count; ++i)
            {
                // A divider next to the raised piece would double its edge.
                const float near = std::max(
                    tween::clamp01(1.0f - std::fabs(value_.value - static_cast<float>(i))),
                    tween::clamp01(1.0f - std::fabs(value_.value - static_cast<float>(i + 1))));
                const Rect &tab = at.tabs[static_cast<std::size_t>(i)];
                list.rounded_rect(
                    {tab.x + tab.w - 0.75f, tab.y + tab.h * 0.28f, 1.5f, tab.h * 0.44f}, 0.0f,
                    hair.with_alpha(1.0f - near));
            }
        }
        // A change squeezes the plate for a moment, like a press.
        const float inset = (welled ? style.track_inset : 0.0f) + 2.0f * change_.value;
        const Rect plate = indicator.inset(inset);
        const float radius =
            welled ? std::max(0.0f, std::min(row_radius, plate.h * 0.5f) - style.track_inset)
                   : paint.control_radius(plate);
        active_ink = draw_plate(paint, theme, plate, radius, welled);
        if (!welled)
        {
            ring = plate;
            ring_radius = radius;
        }
        break;
    }
    case TabKind::underline:
    {
        const Color bar_ink =
            theme.style == SurfaceStyle::sketch ? theme.on_primary : theme.primary;
        if (style.track)
            list.rounded_rect({bounds_.x, at.row.y + at.row.h - 2.0f, bounds_.w, 2.0f}, 0.0f,
                              muted.with_alpha(0.3f));
        const float reach = std::min(style.padding * 0.5f, indicator.w * 0.25f);
        const Rect bar{indicator.x + reach, at.row.y + at.row.h - style.thickness,
                       indicator.w - 2.0f * reach, style.thickness};
        if (theme.style == SurfaceStyle::glow)
            list.glow(bar, 0.0f, 10.0f, bar_ink.with_alpha(0.6f));
        paint.fill(bar, theme.radius > 4.0f ? style.thickness * 0.5f : 0.0f, bar_ink);
        ring = {indicator.x, indicator.y, indicator.w, indicator.h - style.thickness - 4.0f};
        ring_radius = paint.control_radius(ring);
        break;
    }
    case TabKind::boxed:
        // Drawn tab by tab below: every tab is a surface of its own.
        break;
    }

    // Boxed tabs are surfaces of their own. They are drawn before the ring and
    // the labels, like the track and the plate of the other kinds: a ring may
    // light what is under it (a glow fills its interior), and text must stay
    // on top of that light.
    std::vector<Rect> cells = at.tabs;
    std::vector<Color> inks(static_cast<std::size_t>(count));
    std::vector<float> alphas(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        const TabItem &item = tabs_[static_cast<std::size_t>(i)];
        Rect &tab = cells[static_cast<std::size_t>(i)];
        const float weight = tween::clamp01(1.0f - std::fabs(value_.value - static_cast<float>(i)));
        float alpha = item.disabled ? 0.4f : 1.0f;
        if (at.overflow && style.edge_fade > 0.0f)
        {
            const float shown = std::min(tab.x + tab.w - bounds_.x, bounds_.x + bounds_.w - tab.x);
            alpha *= tween::clamp01(shown / (tab.w * style.edge_fade));
        }
        alphas[static_cast<std::size_t>(i)] = alpha;
        inks[static_cast<std::size_t>(i)] = gfx::mix(muted, active_ink, weight);
        if (style.kind != TabKind::boxed || alpha <= 0.0f)
            continue;
        list.push_opacity(alpha);
        tab.x += nudge * weight;
        const bool hollow = theme.secondary.a <= 0.01f;
        Color fill = gfx::mix(theme.secondary, theme.primary, weight);
        Color edge = hollow ? theme.on_secondary : theme.outline;
        Color on = theme.on_primary;
        if (theme.style == SurfaceStyle::glow)
        {
            fill = theme.surface;
            edge = gfx::mix(theme.outline, theme.primary, weight);
            on = theme.primary;
        }
        else if (theme.style == SurfaceStyle::bevel)
        {
            // Bevelled tabs say "selected" by staying pushed in.
            fill = theme.secondary;
            on = theme.on_secondary;
        }
        const float raise =
            framed(theme.style) ? 1.0f - weight : 1.0f - 0.5f * change_.value * weight;
        tab = paint.surface(tab, paint.control_radius(tab), fill, edge, raise,
                            hollow ? std::max(theme.border, 1.5f) : -1.0f);
        inks[static_cast<std::size_t>(i)] = gfx::mix(theme.on_secondary, on, weight);
        list.pop_opacity();
    }
    if (style.kind == TabKind::boxed)
    {
        ring = indicator;
        ring_radius = paint.control_radius(indicator);
    }
    else if (welled && at.overflow)
    {
        // A ring around a cut row would be cut with it: keep it inside.
        const float reach = theme.focus_gap + theme.focus_width + 1.0f;
        ring = {seen.x + reach, seen.y, std::max(seen.w - 2.0f * reach, 0.0f), seen.h};
    }
    if (style.focus_ring)
        paint.focus_ring(ring, ring_radius, focus_amount_.value);

    for (int i = 0; i < count; ++i)
    {
        const TabItem &item = tabs_[static_cast<std::size_t>(i)];
        const Rect &tab = cells[static_cast<std::size_t>(i)];
        const float weight = tween::clamp01(1.0f - std::fabs(value_.value - static_cast<float>(i)));
        const float alpha = alphas[static_cast<std::size_t>(i)];
        if (alpha <= 0.0f)
            continue;
        list.push_opacity(alpha);
        const Color ink = inks[static_cast<std::size_t>(i)];

        const float room = tab.w - 2.0f * style.padding;
        float extras = 0.0f;
        if (style.glyph_width > 0.0f)
            extras += style.glyph_width + (item.label.empty() ? 0.0f : style.glyph_gap);
        const float badge = item.badge > 0 ? badge_width(paint, style, item.badge) : 0.0f;
        if (item.badge > 0)
            extras += kBadgeGap + badge;
        // A pixel to spare: a tab sized from its label must not cut that label
        // because two sums of the same numbers rounded differently.
        const std::string label =
            fit_label(paint, item.label, style.text_size, std::max(room - extras, 24.0f) + 1.0f);
        const float label_w = paint.label_width(label, style.text_size);
        float x = tab.cx() - (label_w + extras) * 0.5f;
        // Text on a rule sits a little higher: the bar takes the bottom.
        const float cy = tab.cy() - (style.kind == TabKind::underline ? style.thickness : 0.0f);

        if (style.glyph_width > 0.0f)
        {
            if (glyph)
                glyph(canvas, {x, cy - tab.h * 0.5f, style.glyph_width, tab.h}, item, i, weight,
                      ink);
            x += style.glyph_width + (item.label.empty() ? 0.0f : style.glyph_gap);
        }
        paint.label(label, x, cy + style.text_size * 0.35f, style.text_size, ink);
        x += label_w;
        if (item.badge > 0)
        {
            // On the plate the badge takes the label's ink, so it never
            // disappears into a plate of its own colour.
            const float height = style.badge_size + 10.0f;
            const Rect pill{x + kBadgeGap, cy - height * 0.5f, badge, height};
            const Color body = gfx::mix(theme.accent, ink, weight);
            paint.fill(pill,
                       theme.pill_chips ? height * 0.5f : std::min(theme.radius, height * 0.5f),
                       body);
            paint.label(badge_text(item.badge), pill.cx(), pill.cy() + style.badge_size * 0.35f,
                        style.badge_size, Painter::on(body), gfx::Align::center);
        }
        list.pop_opacity();
    }

    if (at.overflow)
        list.pop_clip();
}

} // namespace hui::ui
