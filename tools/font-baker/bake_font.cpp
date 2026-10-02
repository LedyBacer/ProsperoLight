// ps5-native-app-boilerplate - ProsperoLight: distance-field font baker (host tool).
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// usage: bake_font <font.ttf> <out.huifont> [pixel_size=56] [sdf_range=8] [atlas=1024]
// Bakes printable ASCII plus a few UI symbols into a single-channel signed
// distance field atlas with metrics and kerning (see src/gfx/font_format.hpp).

#define STB_TRUETYPE_IMPLEMENTATION
#include "../../third_party/stb/stb_truetype.h"

#include "../../third_party/ps5-homebrew-ui/gfx/font_format.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace
{

namespace ff = hui::gfx::font_format;

struct Baked
{
    int codepoint = 0;
    int w = 0;
    int h = 0;
    int xoff = 0;
    int yoff = 0;
    float advance = 0.0f;
    unsigned char *bitmap = nullptr;
    int x = 0;
    int y = 0;
};

std::vector<int> codepoints()
{
    std::vector<int> result;
    for (int c = 32; c < 127; ++c)
        result.push_back(c);
    // Western and central European letters and Cyrillic, for the names of
    // PCs and apps: Latin-1 Supplement, Latin Extended-A, basic Cyrillic.
    for (int c = 0x00A1; c <= 0x017F; ++c)
        result.push_back(c);
    for (int c = 0x0410; c <= 0x044F; ++c)
        result.push_back(c);
    // Io, quotation marks, en/em dash, bullet, ellipsis, euro, trade mark,
    // arrows, check mark, and (in fonts that have them) a full block, a black
    // circle and four pointing triangles. Glyphs a font lacks are skipped;
    // test with Font::has_glyph before relying on one.
    const int extra[] = {0x0401, 0x0451, 0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D,
                         0x201E, 0x2022, 0x2026, 0x20AC, 0x2122, 0x2190, 0x2191, 0x2192,
                         0x2193, 0x2713, 0x2588, 0x25CF, 0x25B2, 0x25B6, 0x25BC, 0x25C0};
    result.insert(result.end(), std::begin(extra), std::end(extra));
    return result;
}

template <typename T> void put(std::vector<unsigned char> &out, const T &value)
{
    const auto *bytes = reinterpret_cast<const unsigned char *>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: %s font.ttf out.huifont [pixel_size] [sdf_range] [atlas]\n", argv[0]);
        return 2;
    }
    const float pixel_size = argc > 3 ? std::strtof(argv[3], nullptr) : 56.0f;
    const int range = argc > 4 ? std::atoi(argv[4]) : 8;
    const int atlas_size = argc > 5 ? std::atoi(argv[5]) : 1024;

    std::ifstream input(argv[1], std::ios::binary);
    std::vector<unsigned char> ttf((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    stbtt_fontinfo font;
    if (ttf.empty() || !stbtt_InitFont(&font, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0)))
    {
        std::fprintf(stderr, "cannot read font %s\n", argv[1]);
        return 1;
    }
    const float scale = stbtt_ScaleForMappingEmToPixels(&font, pixel_size);

    std::vector<Baked> glyphs;
    for (int codepoint : codepoints())
    {
        if (codepoint != ' ' && stbtt_FindGlyphIndex(&font, codepoint) == 0)
        {
            std::fprintf(stderr, "skipping U+%04X (not in font)\n", codepoint);
            continue;
        }
        Baked glyph;
        glyph.codepoint = codepoint;
        int advance = 0;
        int bearing = 0;
        stbtt_GetCodepointHMetrics(&font, codepoint, &advance, &bearing);
        glyph.advance = static_cast<float>(advance) * scale;
        glyph.bitmap = stbtt_GetCodepointSDF(&font, scale, codepoint, range, 128,
                                             128.0f / static_cast<float>(range), &glyph.w, &glyph.h,
                                             &glyph.xoff, &glyph.yoff);
        glyphs.push_back(glyph);
    }

    // Shelf packing, tallest first, one pixel of spacing.
    std::vector<Baked *> order;
    for (Baked &glyph : glyphs)
        order.push_back(&glyph);
    std::sort(order.begin(), order.end(), [](const Baked *a, const Baked *b) { return a->h > b->h; });
    int pen_x = 1;
    int pen_y = 1;
    int shelf = 0;
    for (Baked *glyph : order)
    {
        if (glyph->bitmap == nullptr)
            continue;
        if (pen_x + glyph->w + 1 > atlas_size)
        {
            pen_x = 1;
            pen_y += shelf + 1;
            shelf = 0;
        }
        if (pen_y + glyph->h + 1 > atlas_size)
        {
            std::fprintf(stderr, "atlas %d is too small\n", atlas_size);
            return 1;
        }
        glyph->x = pen_x;
        glyph->y = pen_y;
        pen_x += glyph->w + 1;
        shelf = std::max(shelf, glyph->h);
    }
    const int atlas_height = std::min(atlas_size, pen_y + shelf + 1);
    std::vector<unsigned char> atlas(static_cast<std::size_t>(atlas_size * atlas_height), 0);
    for (const Baked &glyph : glyphs)
    {
        for (int row = 0; glyph.bitmap != nullptr && row < glyph.h; ++row)
            std::memcpy(&atlas[static_cast<std::size_t>((glyph.y + row) * atlas_size + glyph.x)],
                        glyph.bitmap + row * glyph.w, static_cast<std::size_t>(glyph.w));
    }

    std::vector<ff::Kern> kerns;
    for (const Baked &a : glyphs)
    {
        for (const Baked &b : glyphs)
        {
            const int kern = stbtt_GetCodepointKernAdvance(&font, a.codepoint, b.codepoint);
            if (kern != 0)
                kerns.push_back(ff::Kern{static_cast<std::uint32_t>(a.codepoint),
                                         static_cast<std::uint32_t>(b.codepoint),
                                         static_cast<float>(kern) * scale});
        }
    }

    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);
    ff::Header header{};
    header.magic = ff::kMagic;
    header.version = ff::kVersion;
    header.atlas_width = static_cast<std::uint16_t>(atlas_size);
    header.atlas_height = static_cast<std::uint16_t>(atlas_height);
    header.pixel_size = pixel_size;
    header.sdf_range = static_cast<float>(range);
    header.ascent = static_cast<float>(ascent) * scale;
    header.descent = static_cast<float>(descent) * scale;
    header.line_gap = static_cast<float>(line_gap) * scale;
    header.glyph_count = static_cast<std::uint32_t>(glyphs.size());
    header.kern_count = static_cast<std::uint32_t>(kerns.size());

    std::vector<unsigned char> out;
    put(out, header);
    std::sort(glyphs.begin(), glyphs.end(), [](const Baked &a, const Baked &b) { return a.codepoint < b.codepoint; });
    for (const Baked &glyph : glyphs)
    {
        ff::Glyph record{};
        record.codepoint = static_cast<std::uint32_t>(glyph.codepoint);
        record.x = static_cast<std::uint16_t>(glyph.x);
        record.y = static_cast<std::uint16_t>(glyph.y);
        record.w = static_cast<std::uint16_t>(glyph.w);
        record.h = static_cast<std::uint16_t>(glyph.h);
        record.offset_x = static_cast<float>(glyph.xoff);
        record.offset_y = static_cast<float>(glyph.yoff);
        record.advance = glyph.advance;
        put(out, record);
    }
    std::sort(kerns.begin(), kerns.end(), [](const ff::Kern &a, const ff::Kern &b) {
        return a.first != b.first ? a.first < b.first : a.second < b.second;
    });
    for (const ff::Kern &kern : kerns)
        put(out, kern);
    out.insert(out.end(), atlas.begin(), atlas.end());

    std::FILE *file = std::fopen(argv[2], "wb");
    if (file == nullptr || std::fwrite(out.data(), 1, out.size(), file) != out.size())
    {
        std::fprintf(stderr, "cannot write %s\n", argv[2]);
        return 1;
    }
    std::fclose(file);
    for (Baked &glyph : glyphs)
        stbtt_FreeSDF(glyph.bitmap, nullptr);
    std::printf("%s: %zu glyphs, %zu kerning pairs, atlas %dx%d, %zu bytes\n", argv[2], glyphs.size(),
                kerns.size(), atlas_size, atlas_height, out.size());
    return 0;
}
