// ps5-homebrew-ui - Confetti celebration drawn over a finished game.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gfx/draw_list.hpp"

#include <cstdint>
#include <vector>

namespace hui::ui
{

// Paper strips, dots and stars thrown from both bottom corners, tumbling
// under gravity and drag. Deterministic for a given seed (snapshots, tests).
class Confetti
{
  public:
    // Fires both cannons; accent tints part of the palette. reduced keeps a
    // gentle, sparse fall with no spin.
    void burst(gfx::Color accent, std::uint32_t seed, bool reduced = false);
    void update(float dt);
    void draw(gfx::DrawList &list) const;

    bool active() const
    {
        return !pieces_.empty();
    }
    std::size_t size() const
    {
        return pieces_.size();
    }

  private:
    enum class Shape : std::uint8_t
    {
        strip,
        dot,
        star,
    };
    struct Piece
    {
        float x, y, vx, vy;
        float angle, spin;      // radians, radians/s
        float flip, flip_speed; // tumble phase (fakes the paper turning over)
        float size;
        float age, life;
        gfx::Color color;
        Shape shape;
    };

    float random(float low, float high);

    std::vector<Piece> pieces_;
    std::uint32_t state_ = 1;
};

} // namespace hui::ui
