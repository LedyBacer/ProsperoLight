// ps5-homebrew-ui - Components: StatTile and EmptyState.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/stat.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string_view>
#include <utility>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

// Greedy word wrap in the theme's body face. The painter scales that face
// per theme, so the widths must come from the painter too.
std::vector<std::string> wrap_once(const Painter &paint, std::string_view text, float size,
                                   float width, int max_lines)
{
    std::vector<std::string> lines;
    std::string current;
    std::size_t at = 0;
    while (at < text.size() && max_lines > 0)
    {
        while (at < text.size() && text[at] == ' ')
            ++at;
        if (at >= text.size())
            break;
        std::size_t end = text.find(' ', at);
        if (end == std::string_view::npos)
            end = text.size();
        const std::string word(text.substr(at, end - at));
        const std::string joined = current.empty() ? word : current + " " + word;
        if (current.empty() || paint.body_width(joined, size) <= width)
        {
            current = joined;
            at = end;
            continue;
        }
        if (static_cast<int>(lines.size()) + 1 >= max_lines)
        {
            // The last line takes the rest and ends in an ellipsis.
            lines.push_back(
                fit_body(paint, current + " " + std::string(text.substr(at)), size, width));
            return lines;
        }
        lines.push_back(current);
        current.clear();
    }
    if (!current.empty())
        lines.push_back(fit_body(paint, current, size, width));
    return lines;
}

// The same, with lines of similar length: wrapping to the full width often
// leaves one word alone on the last line. The narrowest width that keeps the
// number of lines gives a block that looks set, not spilled.
std::vector<std::string> wrap_body(const Painter &paint, std::string_view text, float size,
                                   float width, int max_lines)
{
    std::vector<std::string> best = wrap_once(paint, text, size, width, max_lines);
    if (best.size() < 2)
        return best;
    for (int step = 1; step <= 10; ++step)
    {
        const float narrower = width * (1.0f - 0.05f * static_cast<float>(step));
        std::vector<std::string> lines = wrap_once(paint, text, size, narrower, max_lines + 1);
        if (lines.size() != best.size())
            break;
        best = std::move(lines);
    }
    return best;
}

} // namespace

// ---- StatTile --------------------------------------------------------------

void StatTile::sync()
{
    static_cast<ComponentStyle &>(counter_.style) = style;
    counter_.style.format = style.format;
    counter_.style.decimals = style.decimals;
    counter_.style.rolling = style.rolling;
}

void StatTile::set_value(double value, bool snap)
{
    sync();
    counter_.set_value(value, snap);
}

void StatTile::set_delta(float percent)
{
    const bool turned = (percent > 0.0f) != (delta_ > 0.0f) || (percent < 0.0f) != (delta_ < 0.0f);
    delta_ = percent;
    if (turned && !style.reduced_motion)
    {
        arrow_.value = 1.4f;
        arrow_.velocity = 0.0f;
    }
}

void StatTile::set_series(std::span<const float> values)
{
    if (!values.empty() && values.size() == series_.size())
    {
        // Same length: every point travels from where it is drawn now.
        const float t = tween::clamp01(morph_.value);
        for (std::size_t i = 0; i < series_.size(); ++i)
            previous_[i] = tween::lerp(previous_[i], series_[i], t);
        series_.assign(values.begin(), values.end());
        morph_.value = 0.0f;
        morph_.velocity = 0.0f;
        return;
    }
    series_.assign(values.begin(), values.end());
    previous_ = series_;
    morph_.snap(1.0f);
    reveal_.value = 0.0f;
    reveal_.velocity = 0.0f;
}

void StatTile::update(float dt)
{
    sync();
    counter_.update(dt);
    arrow_.update(dt, std::clamp(style.omega(), 14.0f, 40.0f),
                  style.reduced_motion ? 1.0f : std::min(style.theme.damping, 0.5f));
    morph_.update(dt, std::clamp(style.omega() * 0.6f, 4.0f, 60.0f));
    reveal_.update(dt, std::clamp(style.omega() * 0.3f, 3.0f, 60.0f));
}

void StatTile::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    if (style.panel)
        paint.panel(bounds_);
    const Rect in = bounds_.inset(style.padding);

    paint.label(fit_label(paint, label, style.label_size, in.w), in.x,
                in.y + style.label_size * 0.8f, style.label_size, theme.text_muted);

    // The figure is the tile's own Counter wearing the tile's current style:
    // a copy, because draw() may not change the tile.
    Counter figure = counter_;
    static_cast<ComponentStyle &>(figure.style) = style;
    figure.style.format = style.format;
    figure.style.decimals = style.decimals;
    figure.style.rolling = style.rolling;
    figure.style.prefix = style.prefix;
    figure.style.suffix = style.suffix;
    figure.style.face = CounterFace::heading;
    figure.style.size = style.value_size;
    const float wanted = figure.width(paint);
    if (wanted > in.w && wanted > 0.0f)
        figure.style.size = style.value_size * in.w / wanted;
    const float value_top = in.y + style.label_size * 1.45f;
    const float value_height = style.value_size * 1.12f;
    figure.set_bounds({in.x, value_top, in.w, value_height});
    figure.draw(canvas);

    float bottom = value_top + value_height;
    if (style.show_delta)
    {
        const float size = style.delta_size;
        const float baseline = bottom + size * 1.05f;
        const float cy = baseline - size * 0.35f;
        const bool up = delta_ > 0.0f;
        const bool flat = delta_ == 0.0f;
        const Color tone =
            flat ? theme.text_muted : (up == style.up_is_good ? theme.success : theme.danger);
        const float arrow = size * 0.62f * std::max(arrow_.value, 0.0f);
        const float ax = in.x + size * 0.36f;
        if (flat)
            list.line(ax - size * 0.25f, cy, ax + size * 0.25f, cy, 3.0f, tone);
        else
            list.triangle({ax - arrow * 0.58f, cy - arrow * 0.5f, arrow * 1.16f, arrow}, tone, 0.0f,
                          up ? 0.0f : 3.14159265f);
        char text[32];
        std::snprintf(text, sizeof(text), "%+.*f%%", std::clamp(style.delta_decimals, 0, 3),
                      static_cast<double>(delta_));
        float x = in.x + size * 0.92f;
        x += paint.label(text, x, baseline, size, tone);
        if (!style.delta_note.empty())
        {
            const float room = in.x + in.w - x - 10.0f;
            if (room > 40.0f)
                paint.body(fit_body(paint, style.delta_note, size, room), x + 10.0f, baseline, size,
                           theme.text_muted);
        }
        bottom = baseline + size * 0.3f;
    }

    if (!style.sparkline)
        return;
    const float spark_bottom = bounds_.y + bounds_.h - style.padding;
    const float spark_top = std::max(spark_bottom - style.spark_height, bottom + 8.0f);
    const Rect area{in.x, spark_top, in.w, spark_bottom - spark_top};
    if (area.h < 12.0f)
        return;
    if (sparkline)
    {
        sparkline(canvas, area);
        return;
    }
    const std::size_t count = series_.size();
    if (count < 2)
        return;

    const float t = tween::clamp01(morph_.value);
    float low = 0.0f;
    float high = 0.0f;
    std::vector<float> values(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        values[i] = tween::lerp(previous_[i], series_[i], t);
        low = i == 0 ? values[i] : std::min(low, values[i]);
        high = i == 0 ? values[i] : std::max(high, values[i]);
    }
    const float range = high - low;
    const float margin = style.spark_width * 2.0f + 2.0f; // room for the dot
    const auto point_x = [&](std::size_t i)
    {
        return area.x + margin +
               (area.w - 2.0f * margin) * static_cast<float>(i) / static_cast<float>(count - 1);
    };
    const auto point_y = [&](std::size_t i)
    {
        // A flat series draws through the middle.
        const float level = range > 1e-6f ? (values[i] - low) / range : 0.5f;
        return area.y + area.h - margin - (area.h - 2.0f * margin) * level;
    };

    const Color ink =
        style.spark_color.a > 0.0f ? style.spark_color : status_color(theme, style.spark_status);
    list.rounded_rect({area.x, area.y + area.h - 1.5f, area.w, 1.5f}, 0.0f,
                      theme.text_muted.with_alpha(0.22f));
    // The line draws itself from the oldest value to the newest.
    const float drawn = tween::clamp01(reveal_.value) * static_cast<float>(count - 1);
    float end_x = point_x(0);
    float end_y = point_y(0);
    const bool lit = theme.style == SurfaceStyle::glow;
    for (std::size_t i = 0; i + 1 < count; ++i)
    {
        const float part = tween::clamp01(drawn - static_cast<float>(i));
        if (part <= 0.0f)
            break;
        const float x0 = point_x(i);
        const float y0 = point_y(i);
        end_x = tween::lerp(x0, point_x(i + 1), part);
        end_y = tween::lerp(y0, point_y(i + 1), part);
        if (lit)
            list.line(x0, y0, end_x, end_y, style.spark_width * 3.0f, ink.with_alpha(0.22f));
        list.line(x0, y0, end_x, end_y, style.spark_width, ink);
    }
    if (style.spark_dot)
    {
        list.circle(end_x, end_y, style.spark_width * 2.0f + 1.0f, solid_surface(theme));
        list.circle(end_x, end_y, style.spark_width * 1.5f, ink);
    }
}

// ---- EmptyState ------------------------------------------------------------

void EmptyState::enter()
{
    age_ = 0.0f;
}

void EmptyState::update(float dt)
{
    age_ += dt;
    phase_ = std::fmod(phase_ + dt, 3600.0f);
}

void EmptyState::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    if (style.panel)
        paint.panel(bounds_);
    const Rect in = bounds_.inset(style.padding);
    const float cx = in.cx();
    const float width = std::min(style.max_text_width, in.w);
    const std::vector<std::string> lines =
        wrap_body(paint, body, style.body_size, width, style.max_lines);
    const float line_height = style.body_size * 1.36f;
    const float hint_height = 34.0f;

    float total = style.icon_size;
    if (!title.empty())
        total += style.gap + style.title_size;
    if (!lines.empty())
        total += style.gap * 0.6f + line_height * static_cast<float>(lines.size());
    if (!action.empty())
        total += style.gap + hint_height;
    float y = in.cy() - total * 0.5f;

    // The parts arrive one after the other, each rising a little.
    int part = 0;
    const auto begin = [&]()
    {
        const float shown = style.reduced_motion ? tween::cubic_out(age_ / 0.2f)
                                                 : tween::stagger(age_, part, 0.07f, 0.36f);
        ++part;
        list.push_opacity(shown);
        list.push_transform(1.0f, 0.0f, 0.0f, 0.0f,
                            style.reduced_motion ? 0.0f : 14.0f * (1.0f - shown));
    };
    const auto end = [&]()
    {
        list.pop_transform();
        list.pop_opacity();
    };

    begin();
    {
        const float size = style.icon_size;
        const float drift = style.float_icon && !style.reduced_motion
                                ? 4.0f * (breathe(phase_, 3.6f) - 0.5f)
                                : 0.0f;
        const Rect area{cx - size * 0.5f, y + drift, size, size};
        if (icon)
        {
            icon(canvas, area);
        }
        else
        {
            // An open, empty box on a quiet disc.
            const bool round = theme.corner == Corner::round && theme.radius >= 2.0f;
            const Color quiet = theme.text_muted;
            const float mid = area.cy();
            paint.fill(area, round ? size * 0.5f : 0.0f, quiet.with_alpha(0.12f));
            const Rect box{cx - size * 0.24f, mid - size * 0.02f, size * 0.48f, size * 0.24f};
            const float pen = std::max(size * 0.035f, 2.5f);
            paint.stroke(box, std::min(theme.radius, 5.0f), pen, quiet);
            list.line(box.x, box.y, box.x - size * 0.09f, box.y - size * 0.13f, pen, quiet);
            list.line(box.x + box.w, box.y, box.x + box.w + size * 0.09f, box.y - size * 0.13f, pen,
                      quiet);
            for (int i = -1; i <= 1; ++i)
                list.circle(cx + static_cast<float>(i) * size * 0.1f, mid - size * 0.2f,
                            pen * 0.75f, quiet.with_alpha(0.6f));
        }
        y += size;
    }
    end();

    if (!title.empty())
    {
        begin();
        y += style.gap + style.title_size;
        paint.label(fit_label(paint, title, style.title_size, width), cx,
                    y - style.title_size * 0.2f, style.title_size, theme.text, gfx::Align::center);
        end();
    }
    if (!lines.empty())
    {
        begin();
        y += style.gap * 0.6f;
        for (const std::string &line : lines)
        {
            paint.body(line, cx, y + style.body_size, style.body_size, theme.text_muted,
                       gfx::Align::center);
            y += line_height;
        }
        end();
    }
    if (!action.empty())
    {
        begin();
        y += style.gap;
        const float glyph = 32.0f;
        const float button =
            action_button == Button::none ? 0.0f : button_width(action_button, glyph) + 10.0f;
        const float text = paint.label_width(action, style.hint_size);
        const float x = cx - (button + text) * 0.5f;
        const float cy = y + hint_height * 0.5f;
        if (action_button != Button::none)
            draw_button(list, canvas.fonts, theme.dark ? GlyphStyle::dark() : GlyphStyle::light(),
                        action_button, x, cy, glyph);
        paint.label(action, x + button, cy + style.hint_size * 0.35f, style.hint_size, theme.text);
        end();
    }
}

} // namespace hui::ui
