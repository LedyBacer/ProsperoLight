// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
struct PresentationStats
{
    uint64_t shown = 0, flip_count = 0, submitted = 0, failed = 0, vblanks = 0;
    bool available = false, vblank_available = false;
};
PresentationStats ps5_presentation_stats();
void ps5_drain_presents(uint64_t expected);

int ps5_hdr_output_active();
