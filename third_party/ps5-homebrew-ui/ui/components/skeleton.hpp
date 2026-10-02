// ps5-homebrew-ui - Component: Skeleton, the shape of content that is still loading.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/progress.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace hui::ui
{

enum class SkeletonKind : std::uint8_t
{
    line,     // style.lines lines of text; the last one is shorter
    block,    // one rectangle: an image, a button
    circle,   // an avatar
    list_row, // rows of a circle and two lines
    card,     // a picture over a title and two lines
};

struct SkeletonStyle : ComponentStyle
{
    SkeletonKind kind = SkeletonKind::line;
    // ---- geometry ----
    int lines = 3; // line: how many; card: lines under the picture
    float line_height = 16.0f;
    float line_gap = 14.0f;
    float last_line = 0.6f; // the last line's share of the width
    int rows = 3;           // list_row: how many rows
    float row_height = 64.0f;
    float row_gap = 14.0f;
    float picture = 0.5f; // card: the picture's share of the height
    float padding = 0.0f; // between the bounds and the bones
    float radius = -1.0f; // of blocks; negative: the theme's control radius
    // ---- look ----
    gfx::Color color{0.0f, 0.0f, 0.0f, 0.0f}; // bones; alpha 0: a faint text tone
    bool panel = false;                       // a themed panel behind everything
    bool shimmer = true;                      // a band of light sweeping across
    float shimmer_period = 1.7f;              // seconds per sweep
    float shimmer_width = 0.3f;               // the band, as a share of the width
    // ---- behaviour ----
    float fade = 0.28f; // seconds of cross-fade when the content arrives
};

// Placeholders that keep the layout still while content loads, then
// cross-fade to it:
//
//   ui::Skeleton rows;
//   rows.style.kind = ui::SkeletonKind::list_row;
//   rows.content = [&](ui::Canvas &canvas, const gfx::Rect &area, float alpha) { ... };
//   rows.set_bounds(area);
//   ...
//   rows.set_loaded(true);    // when the data is there
class Skeleton
{
  public:
    // Draws the real content inside `area`. It is already faded by the
    // component; alpha (0..1) is passed for slots that want to do more.
    using Slot = std::function<void(Canvas &canvas, const gfx::Rect &area, float alpha)>;

    SkeletonStyle style;
    Slot content;

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    void set_loaded(bool loaded, bool snap = false);
    bool loaded() const
    {
        return loaded_;
    }
    // How much of the content is visible, 0..1.
    float content_alpha() const;
    // The placeholder shapes for the current kind and bounds: a rectangle
    // each, round when `round` is set.
    struct Bone
    {
        gfx::Rect rect;
        bool round = false;
    };
    std::vector<Bone> bones() const;

    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    gfx::Rect bounds_{0.0f, 0.0f, 400.0f, 200.0f};
    bool loaded_ = false;
    tween::Timer fade_;
    float from_ = 0.0f; // content alpha when the current fade started
    float phase_ = 0.0f;
};

} // namespace hui::ui
