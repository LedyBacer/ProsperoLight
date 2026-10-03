// ps5-homebrew-ui - Components: what the data components share.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Table, DetailList, Timeline, the charts, Calendar and the status widgets
// all show figures and all sit either on a themed panel or straight on the
// page. They share the face numbers are set in, the two text colours of each
// ground, the colours of chart series and the small bubble a readout uses.

#pragma once

#include "ui/components/component.hpp"

#include <string>
#include <string_view>

namespace hui::ui
{

// ---- numbers ---------------------------------------------------------------
// Figures that change or line up in a column are set in the monospaced face
// so they do not jitter. A theme whose label face is a bitmap or handwriting
// keeps its own face: a second face would break its look, and the bitmap one
// is monospaced already.
float number_width(const Painter &paint, std::string_view text, float size);
float draw_number(Painter &paint, gfx::DrawList &list, std::string_view text, float x,
                  float baseline, float size, gfx::Color color,
                  gfx::Align align = gfx::Align::left);
std::string fit_number(const Painter &paint, std::string_view text, float size, float width);

// A value as short text: 950, 1.2k, 3.4M. decimals < 0 picks none for whole
// numbers and one otherwise.
std::string format_value(double value, int decimals = -1);

// ---- grounds ---------------------------------------------------------------
// The two text colours of where a component sits: on a themed panel, or on
// the page (some themes use other text colours there).
struct Inks
{
    gfx::Color text;
    gfx::Color muted;
};
Inks inks(const Painter &paint, bool on_panel);
// The opaque colour under a component: the panel's surface or the page.
gfx::Color ground_color(const Theme &theme, bool on_panel);
// A hairline colour that shows on that ground in every theme.
gfx::Color rule_color(const Painter &paint, bool on_panel, float strength = 1.0f);

// ---- series ----------------------------------------------------------------
// The nth colour for a chart series, a storage category or a calendar mark:
// the theme's primary, accent, success, warning and danger colours and mixes
// of them, with duplicates removed (many themes use one colour for primary
// and accent) and anything too close to the ground pulled toward the text
// colour until it shows.
gfx::Color series_color(const Theme &theme, int index, bool on_panel = true);
// `color` made visible on the ground, by the same rule.
gfx::Color visible_on(const Theme &theme, gfx::Color color, bool on_panel = true);

// ---- focus -----------------------------------------------------------------
// Draws a component's gliding highlight. `active` is 0..1: how much the
// component has the screen's focus. Without it the highlight stays as a faint
// reminder of where the focus will return; a filled plate does not fade well
// (half a plate is mud), so there it gives way to a tint.
void draw_focus(Canvas &canvas, const ComponentStyle &style, const Highlight &highlight,
                HighlightStyle look, float active, float amount = 1.0f);

// ---- scrolling -------------------------------------------------------------
// How visible a row is in a scrolling view: 1 when all of it shows, 0 until
// `hidden` of it does (half a line of text is worse than none), fading in
// between. hidden <= 0 never fades.
float row_visibility(const gfx::Rect &row, const gfx::Rect &view, float hidden = 0.5f);

// ---- shapes ----------------------------------------------------------------
// A block in the theme's material: bars of a chart, segments of a capacity
// bar. Glossy themes shade it, lit themes make it glow, outlined languages
// (brutal, pixel, sketch) frame it.
void draw_block(Canvas &canvas, const Theme &theme, const gfx::Rect &r, float radius,
                gfx::Color color);
// A dot in the theme's shape: round, or square in square languages.
void draw_marker(Canvas &canvas, const Theme &theme, float cx, float cy, float radius,
                 gfx::Color color);

// A small bubble in the text colour with one line of text, centred on cx
// with its lower edge at `bottom`, kept inside `keep`. amount fades it and
// lets it rise a few pixels. It is what a focused bar, point or slice says.
void draw_readout(Canvas &canvas, const ComponentStyle &style, std::string_view text, float cx,
                  float bottom, float size, const gfx::Rect &keep, float amount);

} // namespace hui::ui
