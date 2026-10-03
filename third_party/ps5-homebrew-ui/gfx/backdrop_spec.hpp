// ps5-homebrew-ui - What a backdrop or post overlay should paint (no GL here).
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gfx/draw_list.hpp"

namespace hui::gfx
{

// What the fragment shader paints behind (or over) the draw list. Each mode
// reads the four colours and the four parameters in its own way; see
// docs/BACKDROPS.md for the table.
enum class BackdropMode : int
{
    none = 0,     // nothing is drawn
    gradient = 1, // c0 top, c1 bottom, c2 highlight at (p0, p1) of strength p2
    aurora = 2,   // slow domain-warped colour clouds: c0/c1 base, c2/c3 clouds
    grid = 3,     // synthwave floor and sun: c0 sky top, c1 horizon, c2 lines, c3 sun
    stars = 4,    // star field with a nebula: c0/c1 space, c2/c3 nebula; p0,p1 camera
    waves = 5,    // layered sine ribbons: c0/c1 base, c2/c3 ribbons
    bokeh = 6,    // drifting soft discs: c0/c1 base, c2/c3 discs
    phosphor = 7, // CRT glass: c0 dark, c1 centre glow
    paper = 8,    // warm paper with grain: c0/c1 paper, c2 light from the top-left
    dots = 9,     // dot matrix with a travelling pulse: c0/c1 base, c2 dots
    vista = 10,   // parallax ridges under a sky, a stand-in game world: c0 sky,
                  // c1 horizon haze, c2 far ridge, c3 near ridge; p0 scroll speed
    // Post overlays, blended over the finished frame:
    scanlines = 20, // CRT lines and vignette: p0 line strength, p1 vignette
    vignette = 21,  // darkened corners: c0 tint, p0 strength
};

struct BackdropSpec
{
    BackdropMode mode = BackdropMode::none;
    Color colors[4] = {};
    float params[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float time = 0.0f; // seconds; freeze it to stop the motion
};

} // namespace hui::gfx
