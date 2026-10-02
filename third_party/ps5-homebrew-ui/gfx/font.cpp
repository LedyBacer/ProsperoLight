// ps5-homebrew-ui - Baked SDF font: loading, measuring and glyph layout.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gfx/font.hpp"

#include <algorithm>
#include <cstring>

namespace hui::gfx
{

namespace ff = font_format;

std::uint32_t next_codepoint(std::string_view text, std::size_t *index)
{
    const auto byte = [&](std::size_t at) { return static_cast<unsigned char>(text[at]); };
    const std::size_t i = *index;
    const unsigned char lead = byte(i);
    int length = 1;
    std::uint32_t value = lead;
    if (lead >= 0xf0 && lead < 0xf8)
    {
        length = 4;
        value = lead & 0x07u;
    }
    else if (lead >= 0xe0)
    {
        length = 3;
        value = lead & 0x0fu;
    }
    else if (lead >= 0xc0)
    {
        length = 2;
        value = lead & 0x1fu;
    }
    else if (lead >= 0x80)
    {
        *index = i + 1;
        return 0xfffd;
    }
    if (i + static_cast<std::size_t>(length) > text.size())
    {
        *index = text.size();
        return 0xfffd;
    }
    for (int k = 1; k < length; ++k)
    {
        const unsigned char continuation = byte(i + static_cast<std::size_t>(k));
        if ((continuation & 0xc0u) != 0x80u)
        {
            *index = i + 1;
            return 0xfffd;
        }
        value = (value << 6) | (continuation & 0x3fu);
    }
    *index = i + static_cast<std::size_t>(length);
    return value;
}

bool Font::load(std::string_view data)
{
    error_.clear();
    if (data.size() < sizeof(ff::Header))
    {
        error_ = "font too small";
        return false;
    }
    std::memcpy(&header_, data.data(), sizeof(header_));
    if (header_.magic != ff::kMagic || header_.version != ff::kVersion ||
        header_.pixel_size <= 0.0f)
    {
        error_ = "not a huifont v1";
        return false;
    }
    const std::size_t glyph_bytes =
        static_cast<std::size_t>(header_.glyph_count) * sizeof(ff::Glyph);
    const std::size_t kern_bytes = static_cast<std::size_t>(header_.kern_count) * sizeof(ff::Kern);
    const std::size_t atlas_bytes = static_cast<std::size_t>(header_.atlas_width) *
                                    static_cast<std::size_t>(header_.atlas_height);
    if (data.size() != sizeof(ff::Header) + glyph_bytes + kern_bytes + atlas_bytes)
    {
        error_ = "font size mismatch";
        return false;
    }
    const char *cursor = data.data() + sizeof(ff::Header);
    // A font may have no glyphs or (monospaced faces) no kerning pairs.
    glyphs_.resize(header_.glyph_count);
    if (glyph_bytes != 0)
        std::memcpy(glyphs_.data(), cursor, glyph_bytes);
    cursor += glyph_bytes;
    kerns_.resize(header_.kern_count);
    if (kern_bytes != 0)
        std::memcpy(kerns_.data(), cursor, kern_bytes);
    cursor += kern_bytes;
    atlas_.assign(reinterpret_cast<const std::uint8_t *>(cursor),
                  reinterpret_cast<const std::uint8_t *>(cursor) + atlas_bytes);
    for (const ff::Glyph &glyph : glyphs_)
    {
        if (glyph.x + glyph.w > header_.atlas_width || glyph.y + glyph.h > header_.atlas_height)
        {
            error_ = "glyph outside atlas";
            return false;
        }
    }
    return true;
}

const ff::Glyph *Font::find(std::uint32_t codepoint) const
{
    const auto it =
        std::lower_bound(glyphs_.begin(), glyphs_.end(), codepoint,
                         [](const ff::Glyph &g, std::uint32_t c) { return g.codepoint < c; });
    return it != glyphs_.end() && it->codepoint == codepoint ? &*it : nullptr;
}

float Font::kern(std::uint32_t first, std::uint32_t second) const
{
    const auto it = std::lower_bound(
        kerns_.begin(), kerns_.end(), std::make_pair(first, second),
        [](const ff::Kern &k, const std::pair<std::uint32_t, std::uint32_t> &key)
        { return k.first != key.first ? k.first < key.first : k.second < key.second; });
    return it != kerns_.end() && it->first == first && it->second == second ? it->amount : 0.0f;
}

std::string Font::fit(std::string_view text, float size, float max_width, float tracking) const
{
    if (measure(text, size, tracking) <= max_width)
        return std::string(text);
    constexpr std::string_view kEllipsis = "\xE2\x80\xA6";
    const std::string_view mark = has_glyph(0x2026) ? kEllipsis : std::string_view("...");
    std::string best;
    for (std::size_t index = 0; index < text.size();)
    {
        const std::size_t start = index;
        next_codepoint(text, &index);
        std::string candidate(text.substr(0, start));
        while (!candidate.empty() && candidate.back() == ' ')
            candidate.pop_back();
        candidate.append(mark);
        if (measure(candidate, size, tracking) > max_width)
            break;
        best = std::move(candidate);
    }
    return best;
}

float Font::measure(std::string_view text, float size, float tracking) const
{
    const float scale = size / header_.pixel_size;
    float width = 0.0f;
    std::uint32_t previous = 0;
    int glyphs = 0;
    for (std::size_t index = 0; index < text.size();)
    {
        std::uint32_t codepoint = next_codepoint(text, &index);
        const ff::Glyph *glyph = find(codepoint);
        if (glyph == nullptr)
        {
            codepoint = '?';
            glyph = find(codepoint);
            if (glyph == nullptr)
                continue;
        }
        if (previous != 0)
            width += kern(previous, codepoint) * scale;
        width += glyph->advance * scale;
        previous = codepoint;
        ++glyphs;
    }
    return glyphs > 1 ? width + tracking * static_cast<float>(glyphs - 1) : width;
}

float Font::layout(std::string_view text, float x, float y, float size, Align align,
                   std::vector<GlyphQuad> &quads, float tracking) const
{
    const float scale = size / header_.pixel_size;
    const float width = measure(text, size, tracking);
    float pen = x;
    if (align == Align::center)
        pen -= width * 0.5f;
    else if (align == Align::right)
        pen -= width;
    const float inverse_w = 1.0f / static_cast<float>(header_.atlas_width);
    const float inverse_h = 1.0f / static_cast<float>(header_.atlas_height);
    std::uint32_t previous = 0;
    for (std::size_t index = 0; index < text.size();)
    {
        std::uint32_t codepoint = next_codepoint(text, &index);
        const ff::Glyph *glyph = find(codepoint);
        if (glyph == nullptr)
        {
            codepoint = '?';
            glyph = find(codepoint);
            if (glyph == nullptr)
                continue;
        }
        if (previous != 0)
            pen += kern(previous, codepoint) * scale;
        if (glyph->w > 0 && glyph->h > 0)
        {
            GlyphQuad quad;
            quad.x0 = pen + glyph->offset_x * scale;
            quad.y0 = y + glyph->offset_y * scale;
            quad.x1 = quad.x0 + static_cast<float>(glyph->w) * scale;
            quad.y1 = quad.y0 + static_cast<float>(glyph->h) * scale;
            quad.u0 = static_cast<float>(glyph->x) * inverse_w;
            quad.v0 = static_cast<float>(glyph->y) * inverse_h;
            quad.u1 = static_cast<float>(glyph->x + glyph->w) * inverse_w;
            quad.v1 = static_cast<float>(glyph->y + glyph->h) * inverse_h;
            quads.push_back(quad);
        }
        pen += glyph->advance * scale + tracking;
        previous = codepoint;
    }
    return width;
}

std::vector<std::string> Font::wrap(std::string_view text, float size, float max_width) const
{
    std::vector<std::string> lines;
    std::string line;
    std::size_t index = 0;
    while (index <= text.size())
    {
        const std::size_t newline = text.find('\n', index);
        const std::string_view paragraph = text.substr(
            index, newline == std::string_view::npos ? std::string_view::npos : newline - index);
        std::size_t word_start = 0;
        line.clear();
        while (word_start <= paragraph.size())
        {
            std::size_t word_end = paragraph.find(' ', word_start);
            if (word_end == std::string_view::npos)
                word_end = paragraph.size();
            const std::string_view word = paragraph.substr(word_start, word_end - word_start);
            const std::string candidate =
                line.empty() ? std::string(word) : line + " " + std::string(word);
            if (!line.empty() && measure(candidate, size) > max_width)
            {
                lines.push_back(line);
                line.assign(word);
            }
            else
            {
                line = candidate;
            }
            word_start = word_end + 1;
        }
        lines.push_back(line);
        if (newline == std::string_view::npos)
            break;
        index = newline + 1;
    }
    return lines;
}

} // namespace hui::gfx
