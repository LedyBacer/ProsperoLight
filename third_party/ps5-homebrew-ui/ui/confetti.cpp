// ps5-homebrew-ui - Confetti celebration drawn over a finished game.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/confetti.hpp"

#include <algorithm>
#include <cmath>

namespace hui::ui
{

namespace
{

constexpr float kGravity = 980.0f; // virtual px/s^2
constexpr float kDrag = 1.6f;      // 1/s, pulls pieces to terminal velocity
constexpr float kPi = 3.14159265f;

const std::uint32_t kPalette[] = {0xffd166, 0xef476f, 0x06d6a0, 0x118ab2,
                                  0xf78c6b, 0xb388ff, 0xffffff};

} // namespace

float Confetti::random(float low, float high)
{
    // xorshift32: cheap, deterministic, good enough for scattering paper.
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return low + (high - low) * static_cast<float>(state_ & 0xffffff) / 16777215.0f;
}

void Confetti::burst(gfx::Color accent, std::uint32_t seed, bool reduced)
{
    state_ = seed == 0 ? 0x9e3779b9u : seed;
    const int per_side = reduced ? 30 : 110;
    for (int side = 0; side < 2; ++side)
    {
        const float origin_x = side == 0 ? -20.0f : 1940.0f;
        const float direction = side == 0 ? 1.0f : -1.0f;
        for (int i = 0; i < per_side; ++i)
        {
            Piece p{};
            p.x = origin_x + random(-10.0f, 10.0f);
            p.y = 1100.0f + random(-20.0f, 20.0f);
            // Aim up and inward, about 55-80 degrees above the horizon.
            const float angle = random(45.0f, 78.0f) * kPi / 180.0f;
            const float speed = reduced ? random(700.0f, 1100.0f) : random(1100.0f, 2000.0f);
            p.vx = direction * std::cos(angle) * speed;
            p.vy = -std::sin(angle) * speed;
            p.angle = random(0.0f, 2.0f * kPi);
            p.spin = reduced ? 0.0f : random(-9.0f, 9.0f);
            p.flip = random(0.0f, 2.0f * kPi);
            p.flip_speed = reduced ? 0.0f : random(5.0f, 13.0f);
            p.size = random(9.0f, 16.0f);
            p.life = random(2.6f, 3.6f);
            const float pick = random(0.0f, 1.0f);
            p.shape = pick < 0.72f ? Shape::strip : (pick < 0.9f ? Shape::dot : Shape::star);
            const int index = static_cast<int>(random(0.0f, 8.999f));
            p.color = index >= 7 ? accent : gfx::Color::rgb(kPalette[index]);
            pieces_.push_back(p);
        }
    }
}

void Confetti::update(float dt)
{
    const float damping = std::exp(-kDrag * dt);
    for (Piece &p : pieces_)
    {
        p.vy += kGravity * dt;
        p.vx *= damping;
        p.vy *= damping;
        // A little sideways sway once the piece is drifting down.
        p.x += (p.vx + std::sin(p.flip) * 40.0f) * dt;
        p.y += p.vy * dt;
        p.angle += p.spin * dt;
        p.flip += p.flip_speed * dt;
        p.age += dt;
    }
    pieces_.erase(std::remove_if(pieces_.begin(), pieces_.end(),
                                 [](const Piece &p) { return p.age >= p.life || p.y > 1200.0f; }),
                  pieces_.end());
}

void Confetti::draw(gfx::DrawList &list) const
{
    for (const Piece &p : pieces_)
    {
        const float remaining = p.life - p.age;
        const float alpha = std::clamp(remaining / 0.5f, 0.0f, 1.0f);
        // Paper turning over: the visible width swings and the face darkens.
        const float turn = std::cos(p.flip);
        const float shade = 0.72f + 0.28f * std::fabs(turn);
        gfx::Color c{p.color.r * shade, p.color.g * shade, p.color.b * shade, p.color.a * alpha};
        switch (p.shape)
        {
        case Shape::strip:
        {
            const float half = p.size * (0.35f + 0.65f * std::fabs(turn));
            const float dx = std::cos(p.angle) * half;
            const float dy = std::sin(p.angle) * half;
            list.line(p.x - dx, p.y - dy, p.x + dx, p.y + dy, p.size * 0.55f, c);
            break;
        }
        case Shape::dot:
            list.circle(p.x, p.y, p.size * 0.38f * (0.6f + 0.4f * std::fabs(turn)), c);
            break;
        case Shape::star:
            list.star(p.x, p.y, p.size * 0.75f, c);
            break;
        }
    }
}

} // namespace hui::ui
