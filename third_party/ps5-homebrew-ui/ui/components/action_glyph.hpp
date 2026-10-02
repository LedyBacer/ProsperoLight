// ps5-homebrew-ui - Components: the controller glyph that stands for a logical action.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ui/glyphs.hpp draws physical buttons; screens and components think in
// logical actions (core/input.hpp). The Keyboard shows the button a shortcut
// is bound to and the KeyBinder shows the button of every row, so both need
// the same translation. The D-pad glyph has no directions of its own: the arm
// that is meant stays bright and the other three are dimmed.

#pragma once

#include "core/input.hpp"
#include "ui/components/component.hpp"
#include "ui/glyphs.hpp"

namespace hui::ui
{

// The button an action is on. Confirm and back are Cross and Circle unless
// the player swapped them (pass the setting).
inline Button button_for(Action action, bool swap_confirm = false)
{
    switch (action)
    {
    case Action::up:
    case Action::down:
    case Action::left:
    case Action::right:
        return Button::dpad;
    case Action::confirm:
        return swap_confirm ? Button::circle : Button::cross;
    case Action::back:
        return swap_confirm ? Button::cross : Button::circle;
    case Action::north:
        return Button::triangle;
    case Action::west:
        return Button::square;
    case Action::page_prev:
        return Button::l1;
    case Action::page_next:
        return Button::r1;
    case Action::jump_prev:
        return Button::l2;
    case Action::jump_next:
        return Button::r2;
    case Action::menu:
        return Button::options;
    case Action::touch:
        return Button::touchpad;
    case Action::l3:
        return Button::left_stick;
    case Action::r3:
        return Button::right_stick;
    default:
        return Button::none;
    }
}

// The glyph colours that suit a theme's page.
inline GlyphStyle glyph_style_for(const Theme &theme)
{
    return theme.dark ? GlyphStyle::dark() : GlyphStyle::light();
}

inline float action_glyph_width(Action action, float size, bool swap_confirm = false)
{
    return button_width(button_for(action, swap_confirm), size);
}

// Draws the glyph of an action with its left edge at x, centred on cy.
inline void draw_action_glyph(Canvas &canvas, const Theme &theme, Action action, float x, float cy,
                              float size, bool swap_confirm = false)
{
    const Button button = button_for(action, swap_confirm);
    if (button == Button::none)
        return;
    const GlyphStyle glyphs = glyph_style_for(theme);
    draw_button(canvas.list, canvas.fonts, glyphs, button, x, cy, size);
    if (button != Button::dpad)
        return;
    // The same arms draw_button drew (ui/glyphs.cpp), veiled except for one.
    const float cx = x + size * 0.5f;
    const float arm = size * 0.3f;
    const float thick = size * 0.2f;
    const float half = thick * 0.5f;
    const gfx::Color veil = gfx::Color{glyphs.body.r, glyphs.body.g, glyphs.body.b, 0.72f};
    if (action != Action::up)
        canvas.list.rounded_rect({cx - half, cy - arm, thick, arm - half}, 0.0f, veil);
    if (action != Action::down)
        canvas.list.rounded_rect({cx - half, cy + half, thick, arm - half}, 0.0f, veil);
    if (action != Action::left)
        canvas.list.rounded_rect({cx - arm, cy - half, arm - half, thick}, 0.0f, veil);
    if (action != Action::right)
        canvas.list.rounded_rect({cx + half, cy - half, arm - half, thick}, 0.0f, veil);
}

} // namespace hui::ui
