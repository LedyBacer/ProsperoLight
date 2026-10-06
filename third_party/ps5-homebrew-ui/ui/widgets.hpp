// ps5-homebrew-ui - Themeable widgets: one set of controls, many design languages.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gfx/draw_list.hpp"
#include "ui/fonts.hpp"
#include "ui/theme.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace hui::ui
{

// The animated state of one widget. The owner eases these (a Spring or a
// Pulse per widget) and the painter only draws them, so the same widget code
// serves every theme and every kind of motion.
struct Look
{
    float focus = 0.0f; // 0..1: how focused it is
    float press = 0.0f; // 0..1: how far it is pushed in
    bool disabled = false;
};

enum class ButtonKind : std::uint8_t
{
    primary,   // the main action: filled in the theme's primary colour
    secondary, // an ordinary action: a surface
    ghost,     // a quiet action: text only
};

// Draws widgets in a theme. Stateless: make one where you draw.
//
//   ui::Painter paint(list, fonts, theme, frame.glass_texture);
//   paint.panel(card);
//   paint.button(rect, "Continue", ui::ButtonKind::primary, look);
//
// Widgets do not handle input or keep values: a toggle is given how "on" it
// is (0..1) and draws that. See src/concepts/themes.cpp for a screen built
// from them, and docs/THEMES.md for how the styles are constructed.
class Painter
{
  public:
    Painter(gfx::DrawList &list, const Fonts &fonts, const Theme &theme,
            std::uint32_t glass_texture = 0);

    const Theme &theme() const
    {
        return theme_;
    }
    const FontRef &font(FontRole role) const;
    static float pixel_em(float size);

    // ---- text ----
    // Heading in the theme's heading face, in the text colour (or another).
    float heading(std::string_view text, float x, float baseline, float size,
                  gfx::Align align = gfx::Align::left);
    float heading(std::string_view text, float x, float baseline, float size, gfx::Color color,
                  gfx::Align align = gfx::Align::left);
    float heading_width(std::string_view text, float size) const;
    // Control label: the theme's label face, capitals and tracking.
    float label(std::string_view text, float x, float baseline, float size, gfx::Color color,
                gfx::Align align = gfx::Align::left);
    float label_width(std::string_view text, float size) const;
    // Running text: always the regular face, as written.
    float body(std::string_view text, float x, float baseline, float size, gfx::Color color,
               gfx::Align align = gfx::Align::left);
    float body_width(std::string_view text, float size) const;

    // Complete text, clipped to its allocated lane; long text scrolls slowly.
    float bounded_heading(std::string_view text, float x, float baseline, float size,
                          gfx::Color color, float width, gfx::Align align = gfx::Align::left);
    float bounded_label(std::string_view text, float x, float baseline, float size,
                        gfx::Color color, float width, gfx::Align align = gfx::Align::left);
    float bounded_body(std::string_view text, float x, float baseline, float size,
                       gfx::Color color, float width, gfx::Align align = gfx::Align::left);

    // ---- building blocks ----
    // The corner size for a control of this rectangle ("pill" resolved).
    float control_radius(const gfx::Rect &r) const;
    // A raised surface in the theme's style. raise 1 is at rest, 0 is fully
    // pressed. Returns where its content sits (some styles move when pressed).
    // border overrides the theme's stroke width (negative: use the theme's).
    gfx::Rect surface(const gfx::Rect &r, float radius, gfx::Color fill, gfx::Color edge,
                      float raise, float border = -1.0f);
    // A sunken surface: tracks, fields, check boxes.
    void well(const gfx::Rect &r, float radius, gfx::Color fill);
    // The theme's focus indicator around r, at an opacity.
    void focus_ring(const gfx::Rect &r, float radius, float amount);
    // Light spreading outward from a shape's edge; its inside stays clear.
    void halo(const gfx::Rect &r, float radius, float spread, gfx::Color color);
    // A plain filled or stroked shape with the theme's corner type.
    void fill(const gfx::Rect &r, float radius, gfx::Color color);
    void stroke(const gfx::Rect &r, float radius, float width, gfx::Color color);

    // ---- widgets ----
    void panel(const gfx::Rect &r);
    void button(const gfx::Rect &r, std::string_view text, ButtonKind kind, const Look &look);
    // value: 0 off .. 1 on, animated by the caller.
    void toggle(const gfx::Rect &r, float value, const Look &look);
    void checkbox(const gfx::Rect &r, float value, const Look &look);
    void radio(const gfx::Rect &r, float value, const Look &look);
    // value: 0..1 along the track.
    void slider(const gfx::Rect &r, float value, const Look &look);
    void progress(const gfx::Rect &r, float value);
    // active: the selected tab's index, animated (1.4 is between tabs 1 and 2).
    void tabs(const gfx::Rect &r, std::span<const char *const> labels, float active,
              const Look &look);
    void field(const gfx::Rect &r, std::string_view text, bool caret, const Look &look);
    void chip(const gfx::Rect &r, std::string_view text, float selected, const Look &look);
    void row(const gfx::Rect &r, std::string_view text, std::string_view value, float selected,
             const Look &look);

    // The colours of text drawn on the page rather than on a panel.
    gfx::Color page_text() const
    {
        return theme_.page_text.a > 0.0f ? theme_.page_text : theme_.text;
    }
    gfx::Color page_text_muted() const
    {
        return theme_.page_text_muted.a > 0.0f ? theme_.page_text_muted : theme_.text_muted;
    }
    // Black or white, whichever reads on the given colour.
    static gfx::Color on(gfx::Color background);

  private:
    gfx::DrawList &list_;
    const Fonts &fonts_;
    const Theme &theme_;
    std::uint32_t glass_;
};

} // namespace hui::ui
