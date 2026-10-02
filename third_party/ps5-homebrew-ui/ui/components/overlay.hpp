// ps5-homebrew-ui - Components: what the overlay components share.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Dialog, Sheet, ToastStack and Tooltip all float above a screen. They share
// a panel that stays readable whatever is behind it, the status symbols and
// text wrapped in the theme's own faces.

#pragma once

#include "ui/components/component.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace hui::ui
{

// What an overlay reports. The colour comes from the theme: primary for info
// and question, and the theme's success, warning and danger colours.
enum class StatusKind : std::uint8_t
{
    none,
    info,
    success,
    warning,
    danger,
    question,
};

gfx::Color status_color(const Theme &theme, StatusKind kind);

// A status symbol drawn from shapes, `size` across, centred on (cx, cy).
// Stroke-only themes get an outlined symbol, square themes a square plate.
void draw_status_icon(Canvas &canvas, const Theme &theme, StatusKind kind, float cx, float cy,
                      float size);

// `color` as it looks on top of `base`, made opaque.
gfx::Color opaque_over(gfx::Color base, gfx::Color color);

// The panel of something that floats. Frosted, it shows the blurred screen
// behind it (canvas.glass) under a tint of the surface colour; that needs a
// glass texture and a theme with round corners, and falls back to solid
// otherwise. Solid, it is the theme's own panel made opaque, so a theme with
// translucent surfaces still hides what is under an overlay.
// radius < 0 uses the theme's card radius; tint is the opacity of the frost.
void draw_overlay_panel(Canvas &canvas, const Theme &theme, const gfx::Rect &r, bool frosted,
                        float tint = 0.55f, float radius = -1.0f);

// Width of a heading as Painter::heading draws it (the painter cannot say).
float heading_width(const Canvas &canvas, const Theme &theme, std::string_view text, float size);

// Text broken into at most max_lines lines no wider than width, in the face
// Painter::body / Painter::heading / Painter::label uses. A '\n' forces a
// break; the last line ends in "..." when text was left over.
std::vector<std::string> wrap_body(const Painter &paint, std::string_view text, float size,
                                   float width, int max_lines);
std::vector<std::string> wrap_heading(const Canvas &canvas, const Theme &theme,
                                      std::string_view text, float size, float width,
                                      int max_lines);

} // namespace hui::ui
