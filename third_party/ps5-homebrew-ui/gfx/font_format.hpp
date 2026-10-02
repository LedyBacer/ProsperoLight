// ps5-homebrew-ui - Binary layout of baked SDF fonts (assets/fonts/*.huifont).
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

namespace hui::gfx::font_format
{

// Little-endian file layout, written by tools/font-baker and read by gfx::Font:
//
//   Header
//   Glyph[glyph_count]      sorted by codepoint
//   Kern[kern_count]        sorted by (first, second)
//   uint8 atlas[atlas_width * atlas_height]   single-channel SDF, row-major
//
// Metrics are in atlas pixels at `pixel_size`; scale by size / pixel_size.
// The SDF stores 128 on the glyph edge and falls by 128 / sdf_range per pixel
// outward.

constexpr std::uint32_t kMagic = 0x46505a50; // "PZPF"
constexpr std::uint32_t kVersion = 1;

struct Header
{
    std::uint32_t magic;
    std::uint32_t version;
    std::uint16_t atlas_width;
    std::uint16_t atlas_height;
    float pixel_size;
    float sdf_range;
    float ascent;
    float descent;
    float line_gap;
    std::uint32_t glyph_count;
    std::uint32_t kern_count;
};
static_assert(sizeof(Header) == 40);

struct Glyph
{
    std::uint32_t codepoint;
    std::uint16_t x; // atlas rectangle
    std::uint16_t y;
    std::uint16_t w;
    std::uint16_t h;
    float offset_x; // quad top-left relative to the pen on the baseline
    float offset_y;
    float advance;
};
static_assert(sizeof(Glyph) == 24);

struct Kern
{
    std::uint32_t first;
    std::uint32_t second;
    float amount;
};
static_assert(sizeof(Kern) == 12);

} // namespace hui::gfx::font_format
