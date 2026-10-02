// ps5-homebrew-ui - Polygon triangulation (ear clipping).
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gfx/triangulate.hpp"

#include <cmath>

namespace hui::gfx
{

namespace
{

double cross(double ax, double ay, double bx, double by, double cx, double cy)
{
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

bool inside(double px, double py, double ax, double ay, double bx, double by, double cx, double cy)
{
    // Strictly inside or on an edge of the counter-clockwise triangle abc.
    return cross(ax, ay, bx, by, px, py) >= 0.0 && cross(bx, by, cx, cy, px, py) >= 0.0 &&
           cross(cx, cy, ax, ay, px, py) >= 0.0;
}

} // namespace

bool triangulate(const float *xy, int count, std::vector<std::uint32_t> &out)
{
    if (count < 3)
        return false;
    // Signed area decides the winding; work counter-clockwise.
    double area = 0.0;
    for (int i = 0, j = count - 1; i < count; j = i++)
        area += static_cast<double>(xy[2 * j]) * xy[2 * i + 1] -
                static_cast<double>(xy[2 * i]) * xy[2 * j + 1];
    std::vector<std::uint32_t> ring;
    ring.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
        ring.push_back(static_cast<std::uint32_t>(area >= 0.0 ? i : count - 1 - i));
    if (std::fabs(area) < 1e-9)
        return false;

    const auto x = [&](std::uint32_t k) { return static_cast<double>(xy[2 * k]); };
    const auto y = [&](std::uint32_t k) { return static_cast<double>(xy[2 * k + 1]); };
    std::size_t guard = 0;
    std::size_t i = 0;
    while (ring.size() > 3 && guard < ring.size() * 2)
    {
        const std::size_t n = ring.size();
        const std::uint32_t a = ring[(i + n - 1) % n];
        const std::uint32_t b = ring[i % n];
        const std::uint32_t c = ring[(i + 1) % n];
        bool ear = cross(x(a), y(a), x(b), y(b), x(c), y(c)) > 1e-12;
        for (std::size_t k = 0; ear && k < n; ++k)
        {
            const std::uint32_t p = ring[k];
            if (p == a || p == b || p == c)
                continue;
            // Duplicate points of the ear's vertices do not block it.
            if ((x(p) == x(a) && y(p) == y(a)) || (x(p) == x(b) && y(p) == y(b)) ||
                (x(p) == x(c) && y(p) == y(c)))
                continue;
            if (inside(x(p), y(p), x(a), y(a), x(b), y(b), x(c), y(c)))
                ear = false;
        }
        if (ear)
        {
            out.insert(out.end(), {a, b, c});
            ring.erase(ring.begin() + static_cast<std::ptrdiff_t>(i % n));
            guard = 0;
            if (i >= ring.size())
                i = 0;
        }
        else
        {
            i = (i + 1) % n;
            ++guard;
        }
    }
    if (ring.size() == 3)
    {
        out.insert(out.end(), {ring[0], ring[1], ring[2]});
        return true;
    }
    // No ear left (self-intersecting input): fan the remainder.
    for (std::size_t k = 1; k + 1 < ring.size(); ++k)
        out.insert(out.end(), {ring[0], ring[k], ring[k + 1]});
    return false;
}

} // namespace hui::gfx
