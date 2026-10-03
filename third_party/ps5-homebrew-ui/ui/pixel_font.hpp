// ps5-homebrew-ui - A 5x7 bitmap typeface drawn from rectangles.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gfx/draw_list.hpp"

#include <string_view>

namespace hui::ui
{

// Text in hard-edged pixels for 8-bit designs: every lit dot is a square, so
// it stays crisp at any size (a distance-field font would round the corners).
// Capitals, digits and common punctuation; lower case draws as capitals.
// `size` is the cap height in virtual pixels. Returns the width drawn.
float pixel_text(gfx::DrawList &list, std::string_view text, float x, float baseline, float size,
                 gfx::Color color, gfx::Align align = gfx::Align::left);
float pixel_text_width(std::string_view text, float size);

} // namespace hui::ui
