/*
 * ps5-native-app-boilerplate - Portable launcher artwork decoder.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "launcher/launcher_artwork.hpp"
#include "launcher/launcher_model.hpp"
#include <png.h>
#include <turbojpeg.h>
#include <webp/decode.h>
#include <cstdint>
#include <cstring>
#include <vector>
namespace launcher
{
constexpr int kPosterHeight = 672;
constexpr std::uint64_t kLargestPicture = 64u * 1024u * 1024u;

namespace
{
bool Fits(std::uint64_t width, std::uint64_t height)
{
    return width && height && width <= kLargestPicture / 4 / height;
}

bool DecodePng(const unsigned char *data, std::size_t size, std::vector<unsigned char> *bgra,
               int *width, int *height)
{
    png_image info{};
    info.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&info, data, size) || !Fits(info.width, info.height))
    {
        png_image_free(&info);
        return false;
    }
    info.format = PNG_FORMAT_BGRA;
    bgra->resize(static_cast<std::size_t>(info.width) * info.height * 4);
    const bool success = png_image_finish_read(&info, nullptr, bgra->data(), 0, nullptr) != 0;
    png_image_free(&info);
    *width = static_cast<int>(info.width);
    *height = static_cast<int>(info.height);
    return success;
}

bool DecodeJpeg(const unsigned char *data, std::size_t size, std::vector<unsigned char> *bgra,
                int *width, int *height)
{
    // The TurboJPEG 2 API: PacBrew ships 3.0, the host tests' distributions still 2.1.
    tjhandle handle = tjInitDecompress();
    if (!handle)
        return false;
    int subsampling = 0;
    int colorspace = 0;
    bool success = tjDecompressHeader3(handle, data, static_cast<unsigned long>(size), width,
                                       height, &subsampling, &colorspace) == 0 &&
                   *width > 0 && *height > 0 && Fits(*width, *height);
    if (success)
    {
        bgra->resize(static_cast<std::size_t>(*width) * *height * 4);
        success = tjDecompress2(handle, data, static_cast<unsigned long>(size), bgra->data(),
                                *width, 0, *height, TJPF_BGRA, 0) == 0;
    }
    tjDestroy(handle);
    return success;
}

bool DecodeWebp(const unsigned char *data, std::size_t size, std::vector<unsigned char> *bgra,
                int *width, int *height)
{
    if (!WebPGetInfo(data, size, width, height) || !Fits(*width, *height))
        return false;
    bgra->resize(static_cast<std::size_t>(*width) * *height * 4);
    return WebPDecodeBGRAInto(data, size, bgra->data(), bgra->size(), *width * 4) != nullptr;
}
} // namespace

bool DecodePoster(const unsigned char *picture, std::size_t size, ArtworkImage *image)
{
    if (!picture || !image || size < 12 || size > kLargestPicture)
        return false;
    // Static decoders avoid firmware-dependent system imports for box art. Sunshine sends PNG;
    // other hosts send the library's own JPEG or WebP.
    std::vector<unsigned char> decoded;
    int source_width = 0;
    int source_height = 0;
    bool success = false;
    if (std::memcmp(picture, "\x89PNG\r\n\x1a\n", 8) == 0)
        success = DecodePng(picture, size, &decoded, &source_width, &source_height);
    else if (std::memcmp(picture, "\xff\xd8\xff", 3) == 0)
        success = DecodeJpeg(picture, size, &decoded, &source_width, &source_height);
    else if (std::memcmp(picture, "RIFF", 4) == 0 && std::memcmp(picture + 8, "WEBP", 4) == 0)
        success = DecodeWebp(picture, size, &decoded, &source_width, &source_height);
    if (!success)
        return false;
    // Each output pixel is the average of the block of source pixels it covers.
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
