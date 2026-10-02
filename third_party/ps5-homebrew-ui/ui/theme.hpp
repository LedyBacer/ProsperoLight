// ps5-homebrew-ui - Design tokens: one struct that restyles every widget.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "audio/cues.hpp"
#include "gfx/backdrop_spec.hpp"
#include "gfx/draw_list.hpp"

#include <cstdint>
#include <span>

namespace hui::ui
{

// How a surface (a button, a card, a field) is built. This one choice is
// most of what separates one design language from another.
enum class SurfaceStyle : std::uint8_t
{
    flat,       // a fill, and a hairline when the theme has a border
    soft,       // a fill floating on a soft drop shadow
    hard,       // thick outline and a solid offset shadow it presses into
    neumorphic, // the page's own colour, raised by a light and a dark shadow
    bevel,      // light top-left and dark bottom-right edges; swapped when pressed
    gloss,      // vertical gradient with a glassy highlight and a dark edge
    glass,      // the blurred screen behind, tinted, with a hairline of light
    outline,    // a stroke only
    glow,       // dark fill, bright stroke, coloured light around it
    pixel,      // notched outline with a darker band inside the bottom-right edge
    sketch,     // slightly crooked, as if drawn by hand with a pen
};

enum class Corner : std::uint8_t
{
    round,   // radius 0 is square
    chamfer, // cut at 45 degrees
    pixel,   // one square notch per corner, like low-resolution pixel art
};

enum class FontRole : std::uint8_t
{
    regular,
    semibold,
    display,
    mono,
    pixel, // Press Start 2P on the pixel grid (ui/pixel_font.hpp draws a 5x7 face without it)
    hand,  // Patrick Hand: handwriting
};

// Everything a widget needs to know to draw itself. A theme is data: adding
// one is filling in this struct (see src/ui/theme.cpp and docs/THEMES.md).
struct Theme
{
    const char *id;
    const char *name;
    const char *family;  // the design language, and what it is modelled on
    const char *summary; // its recipe in one line

    // ---- page ----
    gfx::BackdropSpec backdrop; // the caller sets .time
    gfx::Color page;            // the colour behind the widgets
    // Text drawn straight on the page (headings, hints). Alpha 0 means
    // "the same as text"; set them when panels and page differ in tone.
    gfx::Color page_text{0.0f, 0.0f, 0.0f, 0.0f};
    gfx::Color page_text_muted{0.0f, 0.0f, 0.0f, 0.0f};

    // ---- palette ----
    gfx::Color surface;      // cards, panels
    gfx::Color surface_high; // tracks, chips, wells, fields
    gfx::Color text;         // primary text
    gfx::Color text_muted;   // secondary text
    gfx::Color primary;      // the main action
    gfx::Color on_primary;   // text on the main action
    gfx::Color secondary;    // an ordinary button's body
    gfx::Color on_secondary; // ... and its text
    gfx::Color accent;       // switches that are on, slider fills, selections
    gfx::Color outline;      // strokes
    gfx::Color focus;        // the focus indicator (may be translucent)
    gfx::Color shadow;       // drop, offset and dark-edge colour
    gfx::Color light;        // highlight edge (bevel, neumorphic, gloss, glass)
    // Status colours, for components that report an outcome (toasts, badges,
    // destructive buttons). The defaults read on light and dark pages.
    gfx::Color danger = gfx::Color::rgb(0xe5484d);
    gfx::Color success = gfx::Color::rgb(0x30a46c);
    gfx::Color warning = gfx::Color::rgb(0xf5a524);

    // ---- shape ----
    SurfaceStyle style = SurfaceStyle::soft;
    Corner corner = Corner::round;
    float radius = 12.0f;        // controls; 100 or more means "pill"
    float radius_card = 20.0f;   // panels and dialogs
    float border = 0.0f;         // stroke width; 0 for none
    float button_border = -1.0f; // buttons' stroke; negative means "same as border"
    bool pill_chips = false;     // chips and badges are pills whatever the radius
    bool pill_switches = true;   // switches are pills with a round thumb
    float shadow_offset = 6.0f;
    float shadow_blur = 16.0f;
    float focus_width = 3.0f;      // the ring's stroke
    float focus_gap = 4.0f;        // its distance from the control
    bool underline_fields = false; // text fields are a line, not a box

    // ---- type ----
    FontRole heading = FontRole::display;
    FontRole label = FontRole::semibold;
    bool caps = false;     // labels in capitals
    float tracking = 0.0f; // extra space between label glyphs

    // ---- motion and sound ----
    float omega = 16.0f;  // how fast things move (rad/s); 60 or more snaps
    float damping = 1.0f; // 1 settles without overshoot, 0.5 bounces
    audio::SoundSet sounds = audio::SoundSet::glass;
    bool dark = true; // which controller glyph style suits the page
};

// The built-in themes, in gallery order.
std::span<const Theme> themes();

} // namespace hui::ui
