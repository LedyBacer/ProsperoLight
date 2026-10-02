// ps5-homebrew-ui - Components: the focus ring of a control that has an inside of its own.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Painter::focus_ring adds a glow in the soft, gloss, glass and glow looks,
// and a glow lights the inside of its rectangle as well as the outside. Drawn
// over a control, as the painter's own widgets do, it washes out the text in
// it. The form controls (Stepper, ChoicePicker, TextField) keep their inside
// clean with the two helpers below: where the ring has a glow it goes under
// the control, and where nothing opaque would then cover the glow's inside
// only the ring's stroke is drawn.

#pragma once

#include "ui/components/component.hpp"

namespace hui::ui
{

// True when the ring must be drawn before the control instead of after it.
inline bool focus_frame_goes_under(const Theme &theme)
{
    return theme.style == SurfaceStyle::soft || theme.style == SurfaceStyle::gloss ||
           theme.style == SurfaceStyle::glass || theme.style == SurfaceStyle::glow;
}

// The theme's focus ring around r at an opacity. solid says the control fills
// r with something opaque after this call (a well); glass is never opaque.
inline void focus_frame(Canvas &canvas, const Theme &theme, const gfx::Rect &r, float radius,
                        float amount, bool solid)
{
    if (amount <= 0.01f)
        return;
    Painter paint(canvas.list, canvas.fonts, theme, canvas.glass);
    if (!focus_frame_goes_under(theme) || (solid && theme.style != SurfaceStyle::glass))
    {
        paint.focus_ring(r, radius, amount);
        return;
    }
    // The strokes of Painter::focus_ring, without the light.
    canvas.list.push_opacity(tween::clamp01(amount));
    if (theme.style == SurfaceStyle::glow)
    {
        paint.stroke(r.inset(-5.0f), radius + 4.0f, 2.0f, theme.focus);
    }
    else
    {
        const float reach = theme.focus_gap + theme.focus_width;
        paint.stroke(r.inset(-reach), radius > 0.0f ? radius + reach : 0.0f, theme.focus_width,
                     theme.focus);
    }
    canvas.list.pop_opacity();
}

} // namespace hui::ui
