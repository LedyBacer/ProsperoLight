/*
 * ps5-native-app-boilerplate - ProsperoLight shared native/RADV stream overlays.
 * Copyright (C) 2026 BlackBearReloaded and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include "native_agc_present.hpp"
#include "moonlight_stream_keyboard.hpp"
#include "frame_pacing.hpp"
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstring>
#define HUD_WIDTH 832u
#define HUD_HEIGHT 220u
#define HUD_X 16u
#define HUD_Y 16u
#define HUD_TEXT_X 16u
#define HUD_GLYPH_WIDTH 10u
#define HUD_GLYPH_HEIGHT 10u
#define HUD_LINE_HEIGHT 13u
#define HUD_Y_BYTES (HUD_WIDTH * HUD_HEIGHT)
#define HUD_UV_BYTES (HUD_WIDTH * (HUD_HEIGHT / 2u))
#define HUD_SURFACE_BYTES (HUD_Y_BYTES + HUD_UV_BYTES)
#define KEYBOARD_WIDTH 1120u
#define KEYBOARD_HEIGHT 400u
#define KEYBOARD_Y 180u
#define KEYBOARD_GLYPH_WIDTH 18u
#define KEYBOARD_GLYPH_HEIGHT 18u
#define KEYBOARD_Y_BYTES (KEYBOARD_WIDTH * KEYBOARD_HEIGHT)
#define KEYBOARD_UV_BYTES (KEYBOARD_WIDTH * (KEYBOARD_HEIGHT / 2u))
#define KEYBOARD_SURFACE_BYTES (KEYBOARD_Y_BYTES + KEYBOARD_UV_BYTES)
namespace stream_overlay
{
struct FrameGraph
{
    std::array<uint32_t, 180> samples{};
    unsigned count{}, next{};
    uint64_t previous{};
    bool active{};
    void record(bool enabled, uint64_t now)
    {
        if (!enabled)
        {
            active = false;
            return;
        }
        if (!active)
        {
            count = next = 0;
            previous = 0;
            active = true;
        }
        if (previous && now >= previous)
        {
            samples[next] = unsigned(std::min<uint64_t>(now - previous, 1000000));
            next = (next + 1) % samples.size();
            count = std::min(count + 1u, unsigned(samples.size()));
        }
        previous = now;
    }
};
// Owned solely by the active presentation worker; launcher never accesses it.
inline FrameGraph graph;
static const uint8_t hud_font[96 * 8] = {
#include "../src/native_hud_font.inc"
};

inline void overlay_draw_text_scaled(uint8_t *luma, uint32_t width, uint32_t height,
                                     const char *text, uint32_t x, uint32_t y, uint8_t value,
                                     uint32_t glyph_width, uint32_t glyph_height)
{
    while (*text && x + glyph_width <= width && y + glyph_height <= height)
    {
        uint8_t character = (uint8_t)*text++;
        const uint8_t *glyph;

        if (character < 0x20u || character > 0x7fu)
            character = '?';
        glyph = hud_font + (character - 0x20u) * 8u;
        for (uint32_t row = 0; row < glyph_height; ++row)
        {
            uint32_t source_row = row * 8u / glyph_height;

            for (uint32_t bit = 0; bit < glyph_width; ++bit)
            {
                uint32_t source_bit = bit * 8u / glyph_width;

                if (glyph[source_row] & (uint8_t)(1u << source_bit))
                    luma[(y + row) * width + x + bit] = value;
            }
        }
        x += glyph_width;
    }
}

inline void overlay_draw_text(uint8_t *luma, uint32_t width, uint32_t height, const char *text,
                              uint32_t x, uint32_t y, uint8_t value)
{
    overlay_draw_text_scaled(luma, width, height, text, x, y, value, HUD_GLYPH_WIDTH,
                             HUD_GLYPH_HEIGHT);
}

inline void hud_line(uint8_t *luma, unsigned index, uint8_t value, const char *text)
{
    overlay_draw_text(luma, HUD_WIDTH, HUD_HEIGHT, text, HUD_TEXT_X, 4u + HUD_LINE_HEIGHT * index,
                      value);
}

inline void draw_graph(uint8_t *luma, uint8_t ink)
{
    overlay_draw_text(luma, HUD_WIDTH, HUD_HEIGHT, "Unique-frame submit interval (CPU), 0-50 ms",
                      16, 142, ink);
    const unsigned base = 208;
    for (unsigned i = 0; i < graph.count; ++i)
    {
        const unsigned us = graph.samples[(graph.next + graph.samples.size() - graph.count + i) %
                                          graph.samples.size()];
        const unsigned y = base - std::min(50u, us / 1000u);
        for (unsigned x = 0; x < 3; ++x)
            for (unsigned row = y; row <= base; ++row)
                luma[row * HUD_WIDTH + 16 + i * 4 + x] = ink;
    }
}

inline void refresh_hud_surface(uint8_t *surface, const native_agc_metrics_t *metrics,
                                uint32_t video_width, uint32_t video_height, uint32_t output_width,
                                uint32_t output_height, uint32_t output_refresh_x100, int hdr)
{
    uint8_t *luma = surface;
    char line[112];
    const uint8_t text_luma = hdr ? 143u : 255u;
    const uint64_t decode_us = metrics->decode_average_us;
    const uint64_t p95_us = metrics->decode_p95_us;

    memset(luma, 76, HUD_Y_BYTES);
    memset(surface + HUD_Y_BYTES, 128, HUD_UV_BYTES);

    snprintf(line, sizeof(line), "Video stream: %ux%u %u.%02u FPS (Codec: %s%s) / slices %u of %u",
             video_width, video_height, metrics->incoming_fps_x100 / 100u,
             metrics->incoming_fps_x100 % 100u,
             metrics->video_codec == 2 ? "PyroWave"
             : metrics->video_codec    ? "HEVC"
                                       : "H.264",
             hdr ? " / HDR" : "", metrics->slices_observed, metrics->slices_requested);
    hud_line(luma, 0, text_luma, line);
    snprintf(line, sizeof(line), "Output %ux%u %u.%02u Hz / %s / VSync %s / reserve %llu us%s",
             output_width, output_height, output_refresh_x100 / 100u, output_refresh_x100 % 100u,
             moonlight::effective_pacing_name(metrics->pacing_mode, metrics->vrr_api_active != 0),
             metrics->vsync_enabled ? "on" : "off", (unsigned long long)metrics->pacing_reserve_us,
             metrics->pacing_mode == 2 && !metrics->vrr_api_active ? " / fixed fallback" : "");
    hud_line(luma, 1, text_luma, line);
    if (metrics->video_codec == 2)
        snprintf(line, sizeof(line), "Decoder: PyroWave / RADV GPU");
    else if (metrics->pipeline_depth > 1u)
        snprintf(line, sizeof(line), "Decoder: Videodec2 adaptive x%u / %u cores (mask 0x%llx)",
                 metrics->pipeline_depth, metrics->decoder_cores,
                 (unsigned long long)metrics->decoder_cpu_mask);
    else
        snprintf(line, sizeof(line), "Decoder: Videodec2 classic / %u cores (mask 0x%llx)",
                 metrics->decoder_cores, (unsigned long long)metrics->decoder_cpu_mask);
    hud_line(luma, 2, text_luma, line);
    if (metrics->video_codec == 2)
        snprintf(
            line, sizeof(line), "GPU decode avg: %llu.%02llu ms / GPU render avg: %llu.%02llu ms",
            (unsigned long long)(decode_us / 1000), (unsigned long long)((decode_us % 1000) / 10),
            (unsigned long long)(metrics->gpu_render_average_us / 1000),
            (unsigned long long)((metrics->gpu_render_average_us % 1000) / 10));
    else
        snprintf(line, sizeof(line),
                 "Decode (last second): %llu.%02llu ms avg / %llu.%02llu ms p95 / "
                 "load %u%%",
                 (unsigned long long)(decode_us / 1000u),
                 (unsigned long long)((decode_us % 1000u) / 10u),
                 (unsigned long long)(p95_us / 1000u), (unsigned long long)((p95_us % 1000u) / 10u),
                 metrics->decoder_load_permille / 10u);
    hud_line(luma, 3, text_luma, line);
    snprintf(line, sizeof(line),
             "Frame rate: network %u.%02u / decoded %u.%02u / displayed %u.%02u FPS",
             metrics->incoming_fps_x100 / 100u, metrics->incoming_fps_x100 % 100u,
             metrics->decoded_fps_x100 / 100u, metrics->decoded_fps_x100 % 100u,
             metrics->rendering_fps_x100 / 100u, metrics->rendering_fps_x100 % 100u);
    hud_line(luma, 4, text_luma, line);
    if (metrics->video_codec == 2)
        snprintf(line, sizeof(line), "Network: rejected %u / partial %u / lost packets %llu",
                 metrics->rejected_frames, metrics->partial_frames,
                 (unsigned long long)metrics->lost_packets);
    else
        snprintf(line, sizeof(line), "Frames dropped by your network connection: %u.%02u%%",
                 metrics->network_drop_percent_x100 / 100u,
                 metrics->network_drop_percent_x100 % 100u);
    hud_line(luma, 5, text_luma, line);
    snprintf(line, sizeof(line), "Frames dropped by decoder backlog: %u.%02u%% / not displayed: %u",
             metrics->decoder_drop_percent_x100 / 100u, metrics->decoder_drop_percent_x100 % 100u,
             metrics->not_displayed);
    hud_line(luma, 6, text_luma, line);
    if (metrics->rtt_valid)
        snprintf(line, sizeof(line), "Average network latency: %u ms (variance: %u ms)",
                 metrics->rtt_ms, metrics->rtt_variance_ms);
    else
        snprintf(line, sizeof(line), "Average network latency: N/A");
    hud_line(luma, 7, text_luma, line);
    snprintf(line, sizeof(line),
             metrics->video_codec == 2
                 ? "Host processing latency: N/A"
                 : "Host processing latency min/max/average: %u.%u/%u.%u/%u.%u ms",
             metrics->host_min_tenths_ms / 10u, metrics->host_min_tenths_ms % 10u,
             metrics->host_max_tenths_ms / 10u, metrics->host_max_tenths_ms % 10u,
             metrics->host_average_tenths_ms / 10u, metrics->host_average_tenths_ms % 10u);
    hud_line(luma, 8, text_luma, line);
    snprintf(line, sizeof(line),
             metrics->video_codec == 2
                 ? "Queue: %u frames now / delay: N/A"
                 : "Queue: %u frames now / avg %llu.%02llu ms / max %llu.%02llu ms",
             metrics->pending_frames, (unsigned long long)(metrics->queue_delay_average_us / 1000u),
             (unsigned long long)((metrics->queue_delay_average_us % 1000u) / 10u),
             (unsigned long long)(metrics->queue_delay_max_us / 1000u),
             (unsigned long long)((metrics->queue_delay_max_us % 1000u) / 10u));
    hud_line(luma, 9, text_luma, line);
    draw_graph(luma, text_luma);
}

inline void overlay_fill_rect(uint8_t *luma, uint32_t width, uint32_t height, uint32_t x,
                              uint32_t y, uint32_t rect_width, uint32_t rect_height, uint8_t value)
{
    if (x >= width || y >= height)
        return;
    if (rect_width > width - x)
        rect_width = width - x;
    if (rect_height > height - y)
        rect_height = height - y;
    for (uint32_t row = 0; row < rect_height; ++row)
        memset(luma + (size_t)(y + row) * width + x, value, rect_width);
}

inline void refresh_keyboard_surface(uint8_t *surface, uint32_t selected, int shifted, int hdr)
{
    static const uint32_t margin = 18u;
    static const uint32_t gap = 8u;
    static const uint32_t row_height = 54u;
    static const uint32_t first_row_y = 70u;
    uint8_t *luma = surface;
    uint8_t text_luma = hdr ? 143u : 245u;

    memset(luma, 44, KEYBOARD_Y_BYTES);
    memset(surface + KEYBOARD_Y_BYTES, 128, KEYBOARD_UV_BYTES);
    overlay_draw_text(luma, KEYBOARD_WIDTH, KEYBOARD_HEIGHT, "ProsperoLight keyboard", margin, 10u,
                      text_luma);
    overlay_draw_text(luma, KEYBOARD_WIDTH, KEYBOARD_HEIGHT,
                      "Cross: type   Square: backspace   Triangle: shift   Options: enter   "
                      "Circle: close",
                      margin, 32u, text_luma);

    for (uint32_t row = 0; row < moonlight_keyboard_row_count; ++row)
    {
        uint32_t start = moonlight_keyboard_row_offsets[row];
        uint32_t count = moonlight_keyboard_row_offsets[row + 1] - start;
        uint32_t key_width = (KEYBOARD_WIDTH - margin * 2u - gap * (count - 1u)) / count;
        uint32_t row_width = key_width * count + gap * (count - 1u);
        uint32_t row_x = (KEYBOARD_WIDTH - row_width) / 2u;
        uint32_t y = first_row_y + row * (row_height + gap);

        for (uint32_t column = 0; column < count; ++column)
        {
            uint32_t index = start + column;
            uint32_t x = row_x + column * (key_width + gap);
            const char *label = moonlight_keyboard_label(index, shifted != 0);
            uint32_t label_width = (uint32_t)strlen(label) * KEYBOARD_GLYPH_WIDTH;
            uint8_t fill = index == selected ? (hdr ? 112u : 168u) : (hdr ? 60u : 72u);

            if (moonlight_keyboard_keys[index].action == moonlight_keyboard_action::shift &&
                shifted)
                fill = hdr ? 92u : 136u;
            overlay_fill_rect(luma, KEYBOARD_WIDTH, KEYBOARD_HEIGHT, x, y, key_width, row_height,
                              fill);
            overlay_draw_text_scaled(luma, KEYBOARD_WIDTH, KEYBOARD_HEIGHT, label,
                                     x + (key_width - label_width) / 2u,
                                     y + (row_height - KEYBOARD_GLYPH_HEIGHT) / 2u, text_luma,
                                     KEYBOARD_GLYPH_WIDTH, KEYBOARD_GLYPH_HEIGHT);
        }
    }
}

} // namespace stream_overlay
