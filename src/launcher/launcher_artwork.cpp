/*
 * ps5-native-app-boilerplate - Portable launcher artwork decoder.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "launcher/launcher_artwork.hpp"
#include "launcher/launcher_model.hpp"
#include <png.h>
#include <cstdint>
#include <vector>
namespace launcher
{
constexpr int kPosterHeight = 672;
constexpr std::uint64_t kLargestPicture = 64u * 1024u * 1024u;
bool DecodePoster(const unsigned char *png, std::size_t size, ArtworkImage *image)
{
    if (!png || !image || !size || size > kLargestPicture)
        return false;
    // Static libpng avoids firmware-dependent system imports for box art.
    png_image info{};
    info.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&info, png, size))
    {
        png_image_free(&info);
        return false;
    }
    if (!info.width || !info.height || info.width > kLargestPicture / 4 / info.height)
    {
        png_image_free(&info);
        return false;
    }
    const std::uint64_t bytes = static_cast<std::uint64_t>(info.width) * info.height * 4;
    info.format = PNG_FORMAT_BGRA;
    std::vector<unsigned char> decoded(static_cast<std::size_t>(bytes));
    const bool success = png_image_finish_read(&info, nullptr, decoded.data(), 0, nullptr) != 0;
    png_image_free(&info);
    if (!success)
        return false;
    // Each output pixel is the average of the block of source pixels it covers.
    const int source_width = static_cast<int>(info.width);
    const int source_height = static_cast<int>(info.height);
    int height = source_height > kPosterHeight ? kPosterHeight : source_height;
    int width = static_cast<int>(static_cast<std::int64_t>(source_width) * height / source_height);
    if (width < 1)
        width = 1;
    image->width = width;
    image->height = height;
    image->rgba.resize(static_cast<std::size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y)
    {
        const int y0 = static_cast<int>(static_cast<std::int64_t>(y) * source_height / height);
        int y1 = static_cast<int>(static_cast<std::int64_t>(y + 1) * source_height / height);
        if (y1 <= y0)
            y1 = y0 + 1;
        for (int x = 0; x < width; ++x)
        {
            const int x0 = static_cast<int>(static_cast<std::int64_t>(x) * source_width / width);
            int x1 = static_cast<int>(static_cast<std::int64_t>(x + 1) * source_width / width);
            if (x1 <= x0)
                x1 = x0 + 1;
            unsigned sum[4] = {0, 0, 0, 0};
            for (int sy = y0; sy < y1; ++sy)
            {
                const unsigned char *row =
                    decoded.data() + (static_cast<std::size_t>(sy) * source_width + x0) * 4;
                for (int sx = x0; sx < x1; ++sx, row += 4)
                {
                    sum[0] += row[0];
                    sum[1] += row[1];
                    sum[2] += row[2];
                    sum[3] += row[3];
                }
            }
            const unsigned count = static_cast<unsigned>((y1 - y0) * (x1 - x0));
            unsigned char *out = image->rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4;
            out[0] = static_cast<unsigned char>(sum[2] / count);
            out[1] = static_cast<unsigned char>(sum[1] / count);
            out[2] = static_cast<unsigned char>(sum[0] / count);
            out[3] = static_cast<unsigned char>(sum[3] / count);
        }
    }
    return true;
}

} // namespace launcher
