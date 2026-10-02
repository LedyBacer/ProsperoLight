// ps5-homebrew-ui - Component: Breadcrumb, the path to where the player is.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/component.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace hui::ui
{

enum class CrumbSeparator : std::uint8_t
{
    chevron, // >
    slash,   // /
    dot,     // a small dot
};

struct BreadcrumbStyle : ComponentStyle
{
    // ---- geometry ----
    float gap = 12.0f;            // between a segment and the separator beside it
    float separator_size = 16.0f; // the separator's height
    float chip_height = 40.0f;    // chips = true
    float chip_padding = 14.0f;   // inside a chip, left and right
    // ---- type ----
    float text_size = 24.0f;
    // ---- look ----
    CrumbSeparator separator = CrumbSeparator::chevron;
    bool chips = false;  // every segment sits on a chip; the last one is selected
    bool on_page = true; // drawn straight on the page: use the page's text colours
    // ---- behaviour ----
    int max_segments = 0; // more segments than this fold into "..."; 0: only when too long
    float travel = 26.0f; // how far a new segment slides in
};

// A path of segments: "Home > Library > Saves". It takes no input: the screen
// sets the path, and the component animates the difference. A new segment
// slides in, a removed one fades where it stood, and when the path is longer
// than the bounds the middle folds into an ellipsis.
//
//   ui::Breadcrumb crumbs;
//   crumbs.style.theme = theme;
//   crumbs.set_bounds({400, 240, 700, 40});
//   crumbs.set_path({"Home", "Library"}, false);
//   ...
//   crumbs.push("Saves");      // the player went deeper
//   crumbs.pop();              // ... and came back
//   crumbs.update(dt);
//   crumbs.draw(canvas);
class Breadcrumb
{
  public:
    BreadcrumbStyle style;

    // Replaces the path. Segments both paths start with stay where they are;
    // with animate the rest fades out and the new ones slide in.
    void set_path(const std::vector<std::string> &path, bool animate = true);
    void push(std::string_view segment);
    // Removes the last segment. Returns false when the path was empty.
    bool pop();
    std::vector<std::string> path() const;
    int depth() const;
    // How many segments are folded into the ellipsis. Labels are measured when
    // the component is drawn (a segment not drawn yet counts with an estimate),
    // so this is exact from the frame after a segment first appears.
    int folded() const
    {
        return folded_;
    }

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }

    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    struct Segment
    {
        std::string text;
        tween::Spring shown; // 0 absent .. 1 present
        tween::Spring fold;  // 1: inside the ellipsis
        tween::Spring last;  // 1: the end of the path, where the player is
        bool leaving = false;
        // The label's width in the theme's face. Only drawing has the fonts,
        // so draw() notes it here and the next update() lays the path out
        // with it. It is a measurement, not state: drawing twice notes the
        // same number.
        mutable float width = -1.0f;
    };
    float separator_advance() const;
    float ellipsis_width() const;
    void draw_separator(Canvas &canvas, float x, float cy, gfx::Color ink) const;

    std::vector<Segment> segments_;
    gfx::Rect bounds_{0.0f, 0.0f, 600.0f, 40.0f};
    tween::Spring ellipsis_;
    int folded_ = 0;
    bool settle_ = true; // the next measured layout appears without animating
};

} // namespace hui::ui
