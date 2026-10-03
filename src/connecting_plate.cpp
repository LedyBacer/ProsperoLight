/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "connecting_plate.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace connecting
{

namespace
{

constexpr std::size_t kLumaSamples = static_cast<std::size_t>(kWidth) * kSurfaceHeight;
constexpr std::size_t kChromaSamples = kLumaSamples / 2u;
// What white in the launcher becomes on an HDR television, in nits.
constexpr float kReferenceWhite = 203.0f;

// Video levels: where black and neutral colour sit, and how far they reach.
struct Levels
{
    int black;
    float luma_span;
    int neutral;
    float chroma_span;
};
constexpr Levels kSdrLevels{16, 219.0f, 128, 224.0f};
constexpr Levels kHdrLevels{64, 876.0f, 512, 896.0f};

struct Yuv
{
    float y;
    float u;
    float v;
};

// sRGB to linear light, and linear light (as a share of reference white) to
// the PQ signal an HDR10 television expects.
struct Tables
{
    float linear[256];
    float pq[4098];
};

Tables make_tables()
{
    Tables tables{};
    for (int i = 0; i < 256; ++i)
    {
        const float c = static_cast<float>(i) / 255.0f;
        tables.linear[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }
    for (int i = 0; i < 4098; ++i)
    {
        // Indexed by the square root, so the dark end keeps its precision.
        const float root = std::min(static_cast<float>(i) / 4096.0f, 1.0f);
        const float nits = root * root * kReferenceWhite;
        const float p = std::pow(nits / 10000.0f, 0.1593017578125f);
        tables.pq[i] = std::pow((0.8359375f + 18.8515625f * p) / (1.0f + 18.6875f * p), 78.84375f);
    }
    return tables;
}

const Tables &tables()
{
    static const Tables kTables = make_tables();
    return kTables;
}

float pq_of(const Tables &t, float linear)
{
    const float at = std::sqrt(std::clamp(linear, 0.0f, 1.0f)) * 4096.0f;
    const int index = static_cast<int>(at);
    const float share = at - static_cast<float>(index);
    return t.pq[index] + (t.pq[index + 1] - t.pq[index]) * share;
}

// One sRGB pixel as video samples: BT.709 for an SDR stream, BT.2020 with the
// PQ curve for an HDR one. Both use the limited range the stream uses.
Yuv convert(const Tables &t, std::uint8_t red, std::uint8_t green, std::uint8_t blue, bool hdr)
{
    if (!hdr)
    {
        const float r = static_cast<float>(red) / 255.0f;
        const float g = static_cast<float>(green) / 255.0f;
        const float b = static_cast<float>(blue) / 255.0f;
        const float y = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        return {16.0f + 219.0f * y, 128.0f + 224.0f * (b - y) / 1.8556f,
                128.0f + 224.0f * (r - y) / 1.5748f};
    }
    const float lr = t.linear[red];
    const float lg = t.linear[green];
    const float lb = t.linear[blue];
    const float r = pq_of(t, 0.6274f * lr + 0.3293f * lg + 0.0433f * lb);
    const float g = pq_of(t, 0.0691f * lr + 0.9195f * lg + 0.0114f * lb);
    const float b = pq_of(t, 0.0164f * lr + 0.0880f * lg + 0.8956f * lb);
    const float y = 0.2627f * r + 0.6780f * g + 0.0593f * b;
    return {64.0f + 876.0f * y, 512.0f + 896.0f * (b - y) / 1.8814f,
            512.0f + 896.0f * (r - y) / 1.4746f};
}

template <typename Sample> Sample sample_of(float value)
{
    return static_cast<Sample>(value + 0.5f);
}

template <typename Sample>
void build_base(const std::uint8_t *rgba, bool hdr, std::vector<Sample> &base)
{
    const Levels &levels = hdr ? kHdrLevels : kSdrLevels;
    base.assign(kLumaSamples + kChromaSamples, static_cast<Sample>(levels.neutral));
    std::fill(base.begin(), base.begin() + static_cast<std::ptrdiff_t>(kLumaSamples),
              static_cast<Sample>(levels.black));
    if (rgba == nullptr)
        return;
    const Tables &t = tables();
    Sample *luma = base.data();
    Sample *chroma = base.data() + kLumaSamples;
    for (int y = 0; y < kHeight; y += 2)
    {
        for (int x = 0; x < kWidth; x += 2)
        {
            float u = 0.0f;
            float v = 0.0f;
            for (int dy = 0; dy < 2; ++dy)
            {
                for (int dx = 0; dx < 2; ++dx)
                {
                    const std::size_t at = static_cast<std::size_t>(y + dy) * kWidth +
                                           static_cast<std::size_t>(x + dx);
                    const std::uint8_t *pixel = rgba + at * 4u;
                    const Yuv c = convert(t, pixel[0], pixel[1], pixel[2], hdr);
                    luma[at] = sample_of<Sample>(c.y);
                    u += c.u;
                    v += c.v;
                }
            }
            // One colour sample for each two by two pixels, U then V.
            Sample *pair = chroma + static_cast<std::size_t>(y / 2) * kWidth + x;
            pair[0] = sample_of<Sample>(u * 0.25f);
            pair[1] = sample_of<Sample>(v * 0.25f);
        }
    }
}

// The filled part of the bar: a capsule from the bar's left end.
struct Fill
{
    bool present = false;
    float left = 0.0f;  // centre of the left cap
    float right = 0.0f; // centre of the right cap
    float middle = 0.0f;
    float radius = 0.0f;
    int row0 = 0; // rows and columns it can touch; even, so colour samples line up
    int row1 = 0;
    int column0 = 0;
    int column1 = 0;
};

Fill fill_of(const Bar &bar, float progress)
{
    Fill fill;
    if (bar.width <= 0.0f || bar.height <= 0.0f)
        return fill;
    fill.row0 = std::clamp(static_cast<int>(std::floor(bar.y)) - 1, 0, kHeight) & ~1;
    fill.row1 = std::clamp((static_cast<int>(std::ceil(bar.y + bar.height)) + 2) & ~1, 0, kHeight);
    fill.column0 = std::clamp(static_cast<int>(std::floor(bar.x)) - 1, 0, kWidth) & ~1;
    fill.column1 = std::clamp((static_cast<int>(std::ceil(bar.x + bar.width)) + 2) & ~1, 0, kWidth);
    progress = std::clamp(progress, 0.0f, 1.0f);
    if (progress <= 0.0f)
        return fill;
    const float width = std::max(bar.width * progress, bar.height);
    fill.present = true;
    fill.radius = bar.height * 0.5f;
    fill.left = bar.x + fill.radius;
    fill.right = bar.x + width - fill.radius;
    fill.middle = bar.y + fill.radius;
    return fill;
}

// How much of a pixel the fill covers, 0 to 1: one soft pixel at its edge.
float coverage(const Fill &fill, int x, int y)
{
    const float px = static_cast<float>(x) + 0.5f;
    const float py = static_cast<float>(y) + 0.5f;
    const float dx = px - std::clamp(px, fill.left, fill.right);
    const float dy = py - fill.middle;
    return std::clamp(fill.radius - std::sqrt(dx * dx + dy * dy) + 0.5f, 0.0f, 1.0f);
}

template <typename Sample> void dim(Sample *row, int count, int rest, int scale)
{
    for (int i = 0; i < count; ++i)
        row[i] = static_cast<Sample>(rest + ((static_cast<int>(row[i]) - rest) * scale) / 256);
}

} // namespace

void Plate::build(const std::uint8_t *rgba, const Bar &bar, bool hdr)
{
    clear();
    hdr_ = hdr;
    if (hdr)
        build_base(rgba, true, base16_);
    else
        build_base(rgba, false, base8_);
    if (rgba == nullptr)
        return;
    bar_ = bar;
    const Yuv fill = convert(tables(), bar.fill[0], bar.fill[1], bar.fill[2], hdr);
    fill_[0] = fill.y;
    fill_[1] = fill.u;
    fill_[2] = fill.v;
}

void Plate::clear()
{
    base8_ = {};
    base16_ = {};
    bar_ = {};
    hdr_ = false;
}

// The surface is never read: it may be memory that is slow to read back.
template <typename Sample>
void Plate::write(const std::vector<Sample> &base, void *surface, float progress, float brightness,
                  bool bar_rows_only, Range *written) const
{
    if (base.size() != kLumaSamples + kChromaSamples || surface == nullptr)
        return;
    const Levels &levels = hdr_ ? kHdrLevels : kSdrLevels;
    const Fill fill = fill_of(bar_, progress);
    const int scale = static_cast<int>(std::clamp(brightness, 0.0f, 1.0f) * 256.0f + 0.5f);
    const int row0 = bar_rows_only ? fill.row0 : 0;
    const int row1 = bar_rows_only ? fill.row1 : kSurfaceHeight;
    auto *out = static_cast<Sample *>(surface);
    Sample row[kWidth];

    for (int y = row0; y < row1; ++y)
    {
        const Sample *source = base.data() + static_cast<std::size_t>(y) * kWidth;
        std::memcpy(row, source, sizeof(row));
        if (fill.present && y >= fill.row0 && y < fill.row1)
        {
            for (int x = fill.column0; x < fill.column1; ++x)
            {
                const float cover = coverage(fill, x, y);
                if (cover > 0.0f)
                    row[x] = sample_of<Sample>(static_cast<float>(row[x]) +
                                               (fill_[0] - static_cast<float>(row[x])) * cover);
            }
        }
        if (scale < 256)
            dim(row, kWidth, levels.black, scale);
        std::memcpy(out + static_cast<std::size_t>(y) * kWidth, row, sizeof(row));
    }
    for (int y = row0 / 2; y < row1 / 2; ++y)
    {
        const std::size_t at = kLumaSamples + static_cast<std::size_t>(y) * kWidth;
        std::memcpy(row, base.data() + at, sizeof(row));
        if (fill.present && y * 2 >= fill.row0 && y * 2 < fill.row1)
        {
            for (int x = fill.column0; x < fill.column1; x += 2)
            {
                const float cover =
                    (coverage(fill, x, y * 2) + coverage(fill, x + 1, y * 2) +
                     coverage(fill, x, y * 2 + 1) + coverage(fill, x + 1, y * 2 + 1)) *
                    0.25f;
                if (cover <= 0.0f)
                    continue;
                for (int part = 0; part < 2; ++part)
                    row[x + part] = sample_of<Sample>(
                        static_cast<float>(row[x + part]) +
                        (fill_[1 + part] - static_cast<float>(row[x + part])) * cover);
            }
        }
        if (scale < 256)
            dim(row, kWidth, levels.neutral, scale);
        std::memcpy(out + at, row, sizeof(row));
    }
    if (written != nullptr)
    {
        const std::size_t rows = static_cast<std::size_t>(row1 - row0);
        written[0] = {static_cast<std::size_t>(row0) * kWidth * sizeof(Sample),
                      rows * kWidth * sizeof(Sample)};
        written[1] = {(kLumaSamples + static_cast<std::size_t>(row0 / 2) * kWidth) * sizeof(Sample),
                      rows / 2u * kWidth * sizeof(Sample)};
    }
}

void Plate::compose(void *surface, float progress, float brightness) const
{
    if (hdr_)
        write(base16_, surface, progress, brightness, false, nullptr);
    else
        write(base8_, surface, progress, brightness, false, nullptr);
}

void Plate::compose_bar(void *surface, float progress, Range written[2]) const
{
    written[0] = {};
    written[1] = {};
    if (!has_bar())
        return;
    if (hdr_)
        write(base16_, surface, progress, 1.0f, true, written);
    else
        write(base8_, surface, progress, 1.0f, true, written);
}

} // namespace connecting
