// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gfx/draw_list.hpp"
#include <algorithm>
#include <vector>

namespace hui::ui
{
// Natural-width controls wrap as complete items, retaining their reading order.
inline std::vector<gfx::Rect> flow_layout(const gfx::Rect &room, const std::vector<float> &widths,
                                          float height, float gap)
{
    std::vector<gfx::Rect> result;
    float x = room.x;
    float y = room.y;
    for (float natural : widths)
    {
        const float width = std::min(std::max(natural, 1.0f), room.w);
        if (x > room.x && x + width > room.x + room.w)
        {
            x = room.x;
            y += height + gap;
        }
        result.push_back({x, y, width, height});
        x += width + gap;
    }
    return result;
}
} // namespace hui::ui
