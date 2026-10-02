// ps5-homebrew-ui - Component: Skeleton.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/skeleton.hpp"

#include <algorithm>
#include <cmath>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

void Skeleton::set_loaded(bool loaded, bool snap)
{
    if (snap)
    {
        loaded_ = loaded;
        fade_.running = false;
        return;
    }
    if (loaded == loaded_)
        return;
    // Start from what is on screen, so a change of mind mid-fade turns back
    // instead of jumping.
    from_ = content_alpha();
    loaded_ = loaded;
    fade_.start(style.reduced_motion ? std::min(style.fade, 0.12f) : style.fade);
}

float Skeleton::content_alpha() const
{
    const float target = loaded_ ? 1.0f : 0.0f;
    if (!fade_.running)
        return target;
    return tween::lerp(from_, target, tween::smoothstep(fade_.progress()));
}

std::vector<Skeleton::Bone> Skeleton::bones() const
{
    std::vector<Bone> out;
    const Rect in = bounds_.inset(style.padding);
    const float line = style.line_height;
    const auto text_lines = [&](float x, float y, float width, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            const bool last = count > 1 && i == count - 1;
            const float top = y + static_cast<float>(i) * (line + style.line_gap);
            if (top + line > in.y + in.h + 0.5f)
                break;
            out.push_back({{x, top, last ? width * style.last_line : width, line}, false});
        }
    };

    switch (style.kind)
    {
    case SkeletonKind::line:
        text_lines(in.x, in.y, in.w, std::max(style.lines, 1));
        break;
    case SkeletonKind::block:
        out.push_back({in, false});
        break;
    case SkeletonKind::circle:
    {
        const float size = std::min(in.w, in.h);
        out.push_back({{in.x, in.y, size, size}, true});
        break;
    }
    case SkeletonKind::list_row:
        for (int i = 0; i < style.rows; ++i)
        {
            const float top = in.y + static_cast<float>(i) * (style.row_height + style.row_gap);
            if (top + style.row_height > in.y + in.h + 0.5f)
                break;
            const float size = style.row_height;
            const float x = in.x + size + 16.0f;
            const float width = std::max(in.w - size - 16.0f, 0.0f);
            // Two lines centred on the circle: a title and a shorter detail.
            const float block = 2.0f * line + style.line_gap * 0.8f;
            const float y = top + (size - block) * 0.5f;
            out.push_back({{in.x, top, size, size}, true});
            out.push_back({{x, y, width * 0.72f, line}, false});
            out.push_back({{x, y + line + style.line_gap * 0.8f, width * 0.46f, line}, false});
        }
        break;
    case SkeletonKind::card:
    {
        const float picture = in.h * tween::clamp01(style.picture);
        out.push_back({{in.x, in.y, in.w, picture}, false});
        const float title = line * 1.3f;
        const float y = in.y + picture + 18.0f;
        if (y + title <= in.y + in.h + 0.5f)
            out.push_back({{in.x, y, in.w * 0.7f, title}, false});
        text_lines(in.x, y + title + style.line_gap, in.w, std::max(style.lines, 0));
        break;
    }
    }
    return out;
}

void Skeleton::update(float dt)
{
    fade_.update(dt);
    phase_ = std::fmod(phase_ + dt, 3600.0f);
}

void Skeleton::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    if (style.panel)
        paint.panel(bounds_);

    const float alpha = content_alpha();
    if (alpha < 0.999f)
    {
        list.push_opacity(1.0f - alpha);
        Color bone = style.color.a > 0.0f ? style.color : theme.text_muted.with_alpha(0.24f);
        // Standing still, the placeholders breathe instead of shimmering.
        if (style.shimmer && style.reduced_motion)
            bone = bone.with_alpha(0.65f + 0.35f * breathe(phase_));
        const bool sweep = style.shimmer && !style.reduced_motion;
        // One band crosses the whole component, so the bones light up in
        // order and read as one surface. Light themes need a stronger band:
        // there it bleaches a grey bone instead of brightening a dark one.
        const float half = std::max(bounds_.w * style.shimmer_width * 0.5f, 8.0f);
        const float period = std::max(style.shimmer_period, 0.1f);
        const float t = phase_ / period - std::floor(phase_ / period);
        const float centre =
            tween::lerp(bounds_.x - half, bounds_.x + bounds_.w + half, tween::smoothstep(t));
        const Color light{1.0f, 1.0f, 1.0f, theme.dark ? 0.16f : 0.6f};
        const bool round_theme = theme.corner == Corner::round && theme.radius >= 2.0f;

        for (const Bone &entry : bones())
        {
            const Rect &r = entry.rect;
            if (r.w < 1.0f || r.h < 1.0f)
                continue;
            if (entry.round && round_theme)
            {
                list.circle(r.cx(), r.cy(), r.w * 0.5f, bone);
                // A band has square corners, so a disc brightens as a whole
                // while the band is over it.
                const float near = tween::clamp01(1.0f - std::fabs(centre - r.cx()) / half);
                if (sweep && near > 0.0f)
                    list.circle(r.cx(), r.cy(), r.w * 0.5f, light.with_alpha(near));
                continue;
            }
            const float limit = std::min(r.w, r.h) * 0.5f;
            const float radius = style.radius >= 0.0f
                                     ? std::min(style.radius, limit)
                                     : std::min(theme.radius, std::min(limit, 10.0f));
            paint.fill(r, radius, bone);
            if (sweep)
                draw_sweep(list, centre, half, r.x + radius * 0.5f, r.x + r.w - radius * 0.5f, r.y,
                           r.h, light);
        }
        list.pop_opacity();
    }

    if (content && alpha > 0.001f)
    {
        list.push_opacity(alpha);
        list.push_transform(1.0f, 0.0f, 0.0f, 0.0f,
                            style.reduced_motion ? 0.0f : 8.0f * (1.0f - alpha));
        content(canvas, bounds_.inset(style.padding), alpha);
        list.pop_transform();
        list.pop_opacity();
    }
}

} // namespace hui::ui
