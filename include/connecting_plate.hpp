/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// The connecting screen while the stream owns the display.
//
// The launcher draws the connecting screen, takes a picture of it without the
// progress bar's fill, and closes its display. The stream turns that picture
// into a video frame (a Plate) and keeps showing it, drawing the bar a little
// further each frame, until the first picture of the stream is ready. Nothing
// here touches the console: the same code runs in the PC tests.

namespace connecting
{

inline constexpr int kWidth = 1920;
inline constexpr int kHeight = 1080;
// The presenter reads surfaces whose height is a multiple of sixteen.
inline constexpr int kSurfaceHeight = 1088;

// The progress bar, in pixels of the picture, and the colour of its fill.
struct Bar
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    std::uint8_t fill[3] = {255, 255, 255};
};

// A part of a surface, in bytes from its start.
struct Range
{
    std::size_t offset = 0;
    std::size_t bytes = 0;
};

// A surface is 4:2:0 video: eight-bit samples, or sixteen-bit words holding
// ten bits for an HDR stream.
constexpr std::size_t surface_bytes(bool hdr)
{
    const std::size_t samples = static_cast<std::size_t>(kWidth) * kSurfaceHeight;
    return hdr ? samples * 3u : samples * 3u / 2u;
}

class Plate
{
  public:
    // rgba is kWidth x kHeight sRGB pixels, top row first. Without a picture
    // the plate is black and has no bar.
    void build(const std::uint8_t *rgba, const Bar &bar, bool hdr);
    void clear();

    bool hdr() const
    {
        return hdr_;
    }
    bool has_bar() const
    {
        return bar_.width > 0.0f && bar_.height > 0.0f;
    }

    // Writes the whole picture: the bar filled to progress (0 to 1), and the
    // light scaled by brightness (1 is the picture as drawn, 0 is black).
    void compose(void *surface, float progress, float brightness) const;
    // Rewrites only the rows the bar is in, and says which bytes it wrote
    // (the brightness part, then the colour part).
    void compose_bar(void *surface, float progress, Range written[2]) const;

  private:
    template <typename Sample>
    void write(const std::vector<Sample> &base, void *surface, float progress, float brightness,
               bool bar_rows_only, Range *written) const;

    std::vector<std::uint8_t> base8_;
    std::vector<std::uint16_t> base16_;
    Bar bar_;
    bool hdr_ = false;
    float fill_[3] = {};
};

} // namespace connecting
