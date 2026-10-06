// ps5-homebrew-ui - The font set every screen draws with.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gfx/draw_list.hpp"
#include "gfx/font.hpp"
#include "ui/text_lane.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hui::ui
{

// A loaded font and the GL texture holding its atlas.
struct FontRef
{
    const gfx::Font *font = nullptr;
    std::uint32_t texture = 0;

    float measure(std::string_view text, float size, float tracking = 0.0f) const
    {
        return font->measure(text, size, tracking);
    }
};

struct Fonts
{
    FontRef regular;  // Inter Regular: body text
    FontRef semibold; // Inter SemiBold: titles, labels, buttons
    FontRef display;  // Montserrat Medium: wide geometric headlines
    FontRef mono;     // DejaVu Sans Mono: numbers that must not jump, terminals
    FontRef pixel;    // Press Start 2P: an 8x8 bitmap face (the Pixel theme)
    FontRef hand;     // Patrick Hand: handwriting (the Sketch theme)
};

// Draws one line with its baseline at y; x is the left edge, centre or right
// edge depending on align. Returns the width.
inline float text(gfx::DrawList &list, const FontRef &font, std::string_view value, float x,
                  float baseline, float size, gfx::Color color, gfx::Align align = gfx::Align::left,
                  float tracking = 0.0f)
{
    return list.text(*font.font, font.texture, value, x, baseline, size, color, align, tracking);
}

// ASCII upper case, for small tracked labels ("CONTINUE PLAYING").
inline std::string upper(std::string_view value)
{
    std::string result(value);
    for (char &c : result)
    {
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    }
    return result;
}

// Draws word-wrapped text from its first baseline at y, at most max_lines
// lines; overflow remains readable by slow clipped scrolling. Returns the
// baseline after the last line drawn.
inline float paragraph(gfx::DrawList &list, const FontRef &font, std::string_view value, float x,
                       float y, float size, float width, float line_height, gfx::Color color,
                       int max_lines = 99, gfx::Align align = gfx::Align::left)
{
    const std::vector<std::string> lines = font.font->wrap(value, size, width);
    max_lines = std::max(max_lines, 1);
    const int visible = std::min(max_lines, static_cast<int>(lines.size()));
    const float left = align == gfx::Align::right ? x-width : align == gfx::Align::center ? x-width*0.5f : x;
    const float distance = std::max(static_cast<int>(lines.size())-visible, 0)*line_height;
    const float offset = text_lane_offset(text_lane_seconds(), distance, 0.0f);
    list.push_clip({left, y-size*1.3f, width, std::max((visible-1)*line_height+size*1.8f, size*1.8f)});
    list.readable_text(true);
    for (std::size_t i=0; i<lines.size(); ++i) {
        const float baseline = y+static_cast<float>(i)*line_height-offset;
        const float measured = font.measure(lines[i], size);
        if (measured > width) {
            const float horizontal = text_lane_offset(text_lane_seconds(), measured, width);
            text(list, font, lines[i], left-horizontal, baseline, size, color);
        } else text(list, font, lines[i], x, baseline, size, color, align);
    }
    list.readable_text(false);
    list.pop_clip();
    y += visible*line_height;
    return y;
}

} // namespace hui::ui
