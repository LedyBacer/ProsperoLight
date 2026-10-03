// ps5-homebrew-ui - Polygon triangulation (ear clipping).
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <vector>

namespace hui::gfx
{

// Triangulates a simple polygon (convex or concave, either winding) given as
// interleaved x, y pairs. Appends vertex index triples to out; returns false
// for degenerate input (fewer than 3 distinct points or self-intersection
// that leaves no ear), after emitting a fan so something is still drawn.
bool triangulate(const float *xy, int count, std::vector<std::uint32_t> &out);

} // namespace hui::gfx
