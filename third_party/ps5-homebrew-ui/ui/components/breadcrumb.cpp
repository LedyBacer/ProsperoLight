// ps5-homebrew-ui - Component: Breadcrumb.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/breadcrumb.hpp"

#include <algorithm>
#include <cmath>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

// A chip's body the way Painter::chip builds it, without its fixed label, so
// the text size stays a knob. Returns the colour of text on it.
Color draw_chip(Painter &paint, gfx::DrawList &list, const Theme &theme, const Rect &r,
                float selected)
{
    const bool strokes = theme.style == SurfaceStyle::outline || theme.style == SurfaceStyle::glow;
    const float radius = theme.pill_chips ? r.h * 0.5f : paint.control_radius(r);
    const Color body =
        strokes ? theme.surface_high : gfx::mix(theme.surface_high, theme.accent, selected);
    const Color edge = strokes ? gfx::mix(theme.outline, theme.accent, selected) : theme.outline;
    if (theme.pill_chips)
    {
        // Pills are always round, whatever corner type the language uses.
        list.rounded_rect(r, radius, body);
        if (theme.border > 0.0f)
            list.bordered_rect(r, radius, Color{0.0f, 0.0f, 0.0f, 0.0f}, theme.border,
                               gfx::mix(theme.outline, theme.accent, selected));
    }
    else
    {
        paint.fill(r, radius, body);
        paint.stroke(r, radius,
                     theme.style == SurfaceStyle::hard ? theme.border - 1.0f : theme.border, edge);
    }
    return strokes ? gfx::mix(theme.text_muted, theme.accent, selected)
                   : gfx::mix(theme.text, Painter::on(theme.accent), selected);
}

} // namespace

void Breadcrumb::set_path(const std::vector<std::string> &path, bool animate)
{
    // Keep what both paths start with; everything after the first difference
    // leaves, and the rest of the new path arrives.
    std::size_t kept = 0;
    bool same = true;
    for (Segment &segment : segments_)
    {
        if (segment.leaving)
            continue;
        if (same && kept < path.size() && segment.text == path[kept])
            ++kept;
        else
        {
            same = false;
            segment.leaving = true;
        }
    }
    for (std::size_t i = kept; i < path.size(); ++i)
        push(path[i]);
    if (animate)
        return;
    std::erase_if(segments_, [](const Segment &segment) { return segment.leaving; });
    for (Segment &segment : segments_)
        segment.shown.snap(1.0f);
    settle_ = true;
}

void Breadcrumb::push(std::string_view text)
{
    Segment segment;
    segment.text = std::string(text);
    segment.shown.target = 1.0f;
    segments_.push_back(std::move(segment));
}

bool Breadcrumb::pop()
{
    for (auto it = segments_.rbegin(); it != segments_.rend(); ++it)
    {
        if (!it->leaving)
        {
            it->leaving = true;
            return true;
        }
    }
    return false;
}

std::vector<std::string> Breadcrumb::path() const
{
    std::vector<std::string> out;
    for (const Segment &segment : segments_)
    {
        if (!segment.leaving)
            out.push_back(segment.text);
    }
    return out;
}

int Breadcrumb::depth() const
{
    return static_cast<int>(std::count_if(segments_.begin(), segments_.end(),
                                          [](const Segment &s) { return !s.leaving; }));
}

float Breadcrumb::separator_advance() const
{
    const float glyph = style.separator == CrumbSeparator::dot ? style.separator_size * 0.3f
                                                               : style.separator_size * 0.5f;
    return 2.0f * style.gap + glyph;
}

// The ellipsis is three drawn dots, so it exists in every face (the bitmap
// one has no such glyph) and its width needs no font.
float Breadcrumb::ellipsis_width() const
{
    const float dots = style.text_size * 0.75f;
    return style.chips ? dots + 2.0f * style.chip_padding : dots;
}

void Breadcrumb::update(float dt)
{
    const float omega = std::max(style.omega(), 14.0f);
    std::vector<Segment *> alive;
    for (Segment &segment : segments_)
    {
        if (!segment.leaving)
            alive.push_back(&segment);
    }
    const int count = static_cast<int>(alive.size());
    const bool measured =
        std::all_of(alive.begin(), alive.end(), [](const Segment *s) { return s->width >= 0.0f; });

    // Fold the segments after the first, one more at a time, until the path
    // fits. The first and the last always stay: where it starts and where
    // the player is.
    const float separator = separator_advance();
    const auto total = [&](int folded)
    {
        float width = 0.0f;
        int pieces = 0;
        for (int i = 0; i < count; ++i)
        {
            if (i >= 1 && i <= folded)
                continue;
            // A segment that has not been drawn yet has no measured width:
            // until it has one, an average glyph stands in for its letters, so
            // a path too long for the bounds folds on the frame it is set.
            const Segment &segment = *alive[static_cast<std::size_t>(i)];
            const float guess = static_cast<float>(segment.text.size()) *
                                    (style.text_size * 0.56f + style.theme.tracking) +
                                (style.chips ? 2.0f * style.chip_padding : 0.0f);
            width += segment.width >= 0.0f ? segment.width : guess;
            ++pieces;
        }
        if (folded > 0)
        {
            width += ellipsis_width();
            ++pieces;
        }
        return width + separator * static_cast<float>(std::max(pieces - 1, 0));
    };
    const int most = std::max(count - 2, 0);
    int folded = 0;
    if (style.max_segments >= 2 && count > style.max_segments)
        folded = count - style.max_segments;
    while (folded < most && total(folded) > bounds_.w)
        ++folded;
    folded_ = std::clamp(folded, 0, most);

    ellipsis_.target = folded_ > 0 ? 1.0f : 0.0f;
    for (int i = 0; i < count; ++i)
    {
        Segment &segment = *alive[static_cast<std::size_t>(i)];
        segment.fold.target = i >= 1 && i <= folded_ ? 1.0f : 0.0f;
        segment.last.target = i == count - 1 ? 1.0f : 0.0f;
    }
    // A path set without animation appears already folded.
    if (settle_ && measured)
    {
        ellipsis_.snap(ellipsis_.target);
        for (Segment *segment : alive)
        {
            segment->fold.snap(segment->fold.target);
            segment->last.snap(segment->last.target);
        }
        settle_ = false;
    }

    ellipsis_.update(dt, omega);
    for (Segment &segment : segments_)
    {
        if (segment.leaving)
            segment.shown.target = 0.0f;
        // Leaving is quicker than arriving.
        segment.shown.update(dt, segment.leaving ? omega * 1.4f : omega);
        segment.fold.update(dt, omega);
        segment.last.update(dt, omega);
    }
    std::erase_if(segments_, [](const Segment &s) { return s.leaving && s.shown.value < 0.01f; });
}

void Breadcrumb::draw_separator(Canvas &canvas, float x, float cy, Color ink) const
{
    gfx::DrawList &list = canvas.list;
    const float half = style.separator_size * 0.5f;
    const float reach = style.separator_size * 0.25f;
    switch (style.separator)
    {
    case CrumbSeparator::chevron:
        list.line(x - reach, cy - half, x + reach, cy, 2.2f, ink);
        list.line(x + reach, cy, x - reach, cy + half, 2.2f, ink);
        break;
    case CrumbSeparator::slash:
        list.line(x + reach, cy - half, x - reach, cy + half, 2.2f, ink);
        break;
    case CrumbSeparator::dot:
    {
        const float r = style.separator_size * 0.15f;
        if (style.theme.corner == Corner::round && style.theme.radius >= 2.0f)
            list.circle(x, cy, r, ink);
        else
            list.rounded_rect({x - r, cy - r, 2.0f * r, 2.0f * r}, 0.0f, ink);
        break;
    }
    }
}

void Breadcrumb::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const Color strong = style.on_page ? paint.page_text() : theme.text;
    const Color muted = style.on_page ? paint.page_text_muted() : theme.text_muted;
    const float cy = bounds_.cy();
    const float baseline = cy + style.text_size * 0.35f;
    const float separator = separator_advance();
    const float right = bounds_.x + bounds_.w;
    const float inset = style.chips ? style.chip_padding : 0.0f;

    // One segment's body: its chip (when it has one) and its label.
    const auto piece = [&](const Segment &segment, float x, float alpha, float room)
    {
        if (alpha <= 0.01f)
            return;
        list.push_opacity(alpha);
        const std::string label =
            fit_label(paint, segment.text, style.text_size, std::max(room - 2.0f * inset, 30.0f));
        Color ink = gfx::mix(muted, strong, segment.last.value);
        if (style.chips)
        {
            const float width = paint.label_width(label, style.text_size) + 2.0f * inset;
            ink = draw_chip(paint, list, theme,
                            {x, cy - style.chip_height * 0.5f, width, style.chip_height},
                            segment.last.value);
        }
        paint.label(label, x + inset, baseline, style.text_size, ink);
        list.pop_opacity();
    };

    float x = bounds_.x;
    float ghost = x;  // where a leaving segment is drawn: after what remains
    int alive = 0;    // segments of the path passed so far
    bool any = false; // something stands to the left: a separator is due
    for (const Segment &segment : segments_)
    {
        segment.width = paint.label_width(segment.text, style.text_size) + 2.0f * inset;
        const float shown = tween::clamp01(segment.shown.value);
        if (segment.leaving)
        {
            // It fades where it stood and drifts on, taking no room: what
            // replaces it can arrive in the same place.
            const float drift = style.reduced_motion ? 0.0f : style.travel * 0.35f * (1.0f - shown);
            if (any)
            {
                draw_separator(canvas, ghost + separator * 0.5f + drift, cy,
                               muted.with_alpha(0.7f * shown));
                ghost += separator;
            }
            piece(segment, ghost + drift, shown, right - ghost);
            ghost += segment.width;
            any = true;
            continue;
        }

        if (alive == 1)
        {
            // The ellipsis stands right after the first segment.
            const float amount = tween::clamp01(ellipsis_.value);
            if (amount > 0.01f)
            {
                draw_separator(canvas, x + separator * amount * 0.5f, cy,
                               muted.with_alpha(0.7f * amount));
                x += separator * amount;
                list.push_opacity(amount);
                float dots_x = x;
                if (style.chips)
                {
                    draw_chip(
                        paint, list, theme,
                        {x, cy - style.chip_height * 0.5f, ellipsis_width(), style.chip_height},
                        0.0f);
                    dots_x += inset;
                }
                const float r = style.text_size * 0.075f;
                const float pitch = style.text_size * 0.3f;
                const float dots_y = style.chips ? cy : baseline - r;
                for (int dot = 0; dot < 3; ++dot)
                {
                    const float cx = dots_x + r + pitch * static_cast<float>(dot);
                    if (theme.corner == Corner::pixel || theme.label == FontRole::pixel)
                        list.rounded_rect({cx - r, dots_y - r, 2.0f * r, 2.0f * r}, 0.0f,
                                          style.chips ? theme.text : muted);
                    else
                        list.circle(cx, dots_y, r, style.chips ? theme.text : muted);
                }
                list.pop_opacity();
                x += ellipsis_width() * amount;
            }
        }

        const float fold = tween::clamp01(segment.fold.value);
        const float presence = shown * (1.0f - fold);
        if (alive > 0)
        {
            draw_separator(canvas, x + separator * presence * 0.5f, cy,
                           muted.with_alpha(0.7f * presence));
            x += separator * presence;
        }
        // A folding label fades before its room is gone, so it never sits
        // on top of its neighbour.
        const float slide = style.reduced_motion ? 0.0f : -style.travel * (1.0f - shown);
        piece(segment, x + slide, shown * tween::clamp01(1.0f - fold * 1.6f), right - x);
        x += segment.width * presence;
        ghost = x;
        any = true;
        ++alive;
    }
}

} // namespace hui::ui
