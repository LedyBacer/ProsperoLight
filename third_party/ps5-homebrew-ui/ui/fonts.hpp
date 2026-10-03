// ps5-homebrew-ui - The font set every screen draws with.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gfx/draw_list.hpp"
#include "gfx/font.hpp"

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
// lines (the last one ends in an ellipsis if text remains). Returns the
// baseline after the last line drawn.
inline float paragraph(gfx::DrawList &list, const FontRef &font, std::string_view value, float x,
                       float y, float size, float width, float line_height, gfx::Color color,
                       int max_lines = 99, gfx::Align align = gfx::Align::left)
{
    const std::vector<std::string> lines = font.font->wrap(value, size, width);
    int drawn = 0;
    for (const std::string &line : lines)
    {
        const bool last =
            drawn + 1 == max_lines && lines.size() > static_cast<std::size_t>(max_lines);
        if (last)
            text(list, font, font.font->fit(line + " \xE2\x80\xA6", size, width), x, y, size, color,
                 align);
        else
            text(list, font, line, x, y, size, color, align);
        y += line_height;
        if (++drawn >= max_lines)
            break;
    }
    return y;
}

} // namespace hui::ui
