/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Foreground native AGC presentation of a Videodec2 AVC8 caller buffer. */

#include "native_agc_present.hpp"
#include "lan_http_report.hpp"
#include "moonlight_stream_keyboard.hpp"
#include "native_agc_output.hpp"

#include <stddef.h>
#include <atomic>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef PROSPEROLIGHT_LAN_TELEMETRY
#define PROSPEROLIGHT_LAN_TELEMETRY 0
#endif
#ifndef PROSPEROLIGHT_GPU_TIMESTAMPS
#define PROSPEROLIGHT_GPU_TIMESTAMPS 0
#endif
static_assert(PROSPEROLIGHT_GPU_TIMESTAMPS == 0 || PROSPEROLIGHT_GPU_TIMESTAMPS == 1,
              "GPU_TIMESTAMPS must be 0 or 1");
#ifndef PROSPEROLIGHT_VIDEO_OUTPUT_SELF_TEST_FPS
#define PROSPEROLIGHT_VIDEO_OUTPUT_SELF_TEST_FPS 0
#endif
#ifndef PROSPEROLIGHT_PRESENT_OVERLAP
#define PROSPEROLIGHT_PRESENT_OVERLAP 1
#endif
#ifndef PROSPEROLIGHT_FLIP_POLL_US
#define PROSPEROLIGHT_FLIP_POLL_US 500
#endif
static_assert(PROSPEROLIGHT_FLIP_POLL_US >= 100 && PROSPEROLIGHT_FLIP_POLL_US <= 2000,
              "Flip polling experiment must stay between 100 and 2000 microseconds");

#define BASE_OUTPUT_WIDTH 1920u
#define BASE_OUTPUT_HEIGHT 1080u
#define TV_SAFE_INSET_X 64u
#define TV_SAFE_INSET_Y 36u
#define FRAMEBUFFER_ALIGNMENT 0x200000u
#define SHADER_STATIC_BYTES 0x10000u
#define HUD_WIDTH 832u
#define HUD_HEIGHT 140u
#define HUD_X 16u
#define HUD_Y 16u
#define HUD_TEXT_X 16u
#define HUD_GLYPH_WIDTH 10u
#define HUD_GLYPH_HEIGHT 10u
#define HUD_LINE_HEIGHT 13u
#define HUD_SURFACE_OFFSET SHADER_STATIC_BYTES
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
#define SHADER_MEMORY_BYTES 0x0d0000u
#define HDR_HUD_SHADER_CODE_OFFSET 0xd000u
#define HDR_HUD_SHADER_HEADER_OFFSET 0xe000u
#define HDR_HUD_RESOURCES_OFFSET 0xf000u
#define HUD_REFRESH_FRAMES 60u
#define DIRECT_MEMORY_TYPE 12
#define MAP_PROTECTION 0x33
#define VIDEO_OUT_PIXEL_FORMAT_SDR UINT64_C(0x8000000000000000)
#define VIDEO_OUT_PIXEL_FORMAT_HDR UINT64_C(0x8100070422000000)
#define VIDEO_OUT_REFRESH_RATE_59_94 UINT64_C(3)
#define VIDEO_OUT_REFRESH_RATE_119_88 UINT64_C(13)
#define VIDEO_OUT_REFRESH_RATE_89_91 UINT64_C(35)
#define VIDEO_OUT_REQUEST_DEFAULT 1u
#define VIDEO_OUT_REQUEST_120_HZ 15u
#define LOADING_PITCH 1920u
#define LOADING_SURFACE_HEIGHT 1088u
#define LOADING_VISIBLE_HEIGHT 1080u
#define VIDEO_OUT_FLIP_MODE_VSYNC 1u
#define VIDEO_OUT_FLIP_MODE_HSYNC 2u
// Flip events only wake the waiter early; flip status stays authoritative.
#define FLIP_EVENT_WAIT_US 2000u
#define KERNEL_ERROR_ETIMEDOUT UINT32_C(0x8002003c)
// Two 64-bit GPU clock stamps (99.79 MHz) after the HUD/keyboard surfaces.
#define GPU_TIMESTAMP_OFFSET 0xcf000u
#define GPU_TICK_PICOSECONDS UINT64_C(10021)

static_assert(HUD_SURFACE_OFFSET + KEYBOARD_SURFACE_BYTES <= GPU_TIMESTAMP_OFFSET,
              "overlay surfaces must not overlap the GPU timestamp words");
static_assert(HUD_SURFACE_BYTES <= KEYBOARD_SURFACE_BYTES,
              "the HUD shares the keyboard overlay allocation");
static_assert(GPU_TIMESTAMP_OFFSET + 64u <= SHADER_MEMORY_BYTES,
              "GPU timestamp words must stay inside shader memory");

static_assert((BASE_OUTPUT_WIDTH - TV_SAFE_INSET_X * 2u) * 9u ==
                  (BASE_OUTPUT_HEIGHT - TV_SAFE_INSET_Y * 2u) * 16u,
              "TV-safe viewport must remain 16:9");

typedef struct agc_register
{
    uint16_t offset;
    uint16_t pad;
    uint32_t value;
} agc_register_t;

typedef struct agc_command_buffer
{
    uint32_t *bottom;
    uint32_t *top;
    uint32_t *up;
    uint32_t *down;
    uintptr_t callback;
    void *user_data;
    uint32_t reserved_dwords;
    uint32_t pad;
} agc_command_buffer_t;

typedef struct agc_submit_description
{
    void *words;
    uint32_t word_count;
    uint8_t flag;
    uint8_t pad[3];
} agc_submit_description_t;

typedef struct video_buffer
{
    void *data;
    void *metadata;
    void *reserved0;
    void *reserved1;
} video_buffer_t;

typedef struct video_attribute
{
    uint8_t reserved[80];
} video_attribute_t;

typedef struct video_resolution_status
{
    uint32_t full_width;
    uint32_t full_height;
    uint32_t pane_width;
    uint32_t pane_height;
    uint64_t refresh_rate;
    float screen_size_inches;
    uint32_t reserved[4];
} video_resolution_status_t;

static_assert(sizeof(video_resolution_status_t) == 48,
              "VideoOut resolution status ABI must remain 48 bytes");

typedef struct video_output_status
{
    uint32_t resolution_class;
    uint32_t output_class;
    uint64_t refresh_rate;
    uint64_t flags;
    uint32_t mode;
    uint32_t reserved[5];
} video_output_status_t;

static_assert(sizeof(video_output_status_t) == 48,
              "VideoOut output status ABI must remain 48 bytes");
static_assert(offsetof(video_output_status_t, refresh_rate) == 8,
              "VideoOut output refresh ABI offset must remain 8 bytes");

#if PROSPEROLIGHT_LAN_TELEMETRY
typedef struct notification_request
{
    uint8_t reserved[45];
    char message[3075];
} notification_request_t;
#endif

extern "C"
{
    int64_t sceKernelGetDirectMemorySize(void);
    int32_t sceKernelAllocateDirectMemory(int64_t search_start, int64_t search_end, size_t length,
                                          size_t alignment, int memory_type,
                                          int64_t *direct_memory_start);
    int32_t sceKernelMapDirectMemory(void **address, size_t length, int protection, int flags,
                                     int64_t direct_memory_start, size_t alignment);
    int32_t sceKernelMunmap(void *address, size_t length);
    int32_t sceKernelReleaseDirectMemory(int64_t direct_memory_start, size_t length);
    int sceKernelUsleep(uint32_t microseconds);
#if PROSPEROLIGHT_LAN_TELEMETRY
    int32_t sceKernelSendNotificationRequest(uint32_t device, void *request, size_t size,
                                             int32_t blocking);
#endif
    int sceVideoOutOpen(int32_t user_id, int32_t bus_type, int32_t index, const void *param);
    int sceVideoOutClose(int32_t handle);
    int sceVideoOutSetFlipRate(int32_t handle, int32_t rate);
    int sceVideoOutConfigureOutput(int32_t handle, uint32_t request_type, const void *param3,
                                   const void *param4, const void *param5);
    int sceVideoOutIsOutputSupported(int32_t handle, uint32_t request_type, const void *param3,
                                     const void *param4, const void *param5);
    int sceVideoOutVrrUnpegFromFixedRate(int32_t handle);
    int sceVideoOutGetResolutionStatus(int32_t handle, video_resolution_status_t *status);
    int sceVideoOutGetOutputStatus(int32_t handle, video_output_status_t *status);
    void sceVideoOutSetBufferAttribute2(video_attribute_t *attribute, uint64_t pixel_format,
                                        uint32_t tiling_mode, uint32_t width, uint32_t height,
                                        uint64_t option, uint32_t dcc_control,
                                        uint64_t dcc_clear_color);
    int sceVideoOutRegisterBuffers2(int32_t handle, int32_t set_index, int32_t buffer_index_start,
                                    video_buffer_t *buffers, int32_t buffer_count,
                                    video_attribute_t *attribute, int32_t category, void *option);
    int sceVideoOutUnregisterBuffers(int32_t handle, int32_t set_index);
    int sceVideoOutSubmitFlip(int32_t handle, int32_t buffer_index, uint32_t flip_mode,
                              int64_t flip_argument);
    int sceVideoOutIsFlipPending(int32_t handle);
    int sceVideoOutWaitVblank(int32_t handle);
    int sceVideoOutGetFlipStatus(int32_t handle, void *status);
    int sceVideoOutAddFlipEvent(void *queue, int32_t handle, void *user_data);
    int sceVideoOutDeleteFlipEvent(void *queue, int32_t handle);
    int sceKernelCreateEqueue(void **queue, const char *name);
    int sceKernelDeleteEqueue(void *queue);
    int sceKernelWaitEqueue(void *queue, void *event, int count, int *out,
                            unsigned int *timeout_us);

    int32_t sceAgcInit(void *state, uint32_t size);
    int32_t sceAgcCreateShader(void **shader, void *header, void *code);
    int32_t sceAgcLinkShaders(void *cx, void *uc, void *reserved, void *vertex_shader,
                              void *pixel_shader, uint32_t primitive_type);
    void *sceAgcGetRegisterDefaults(void);
    uint32_t *sceAgcDcbSetCxRegistersIndirect(void *command, const void *registers, uint32_t count);
    uint32_t *sceAgcDcbSetShRegistersIndirect(void *command, const void *registers, uint32_t count);
    uint32_t *sceAgcDcbSetUcRegistersIndirect(void *command, const void *registers, uint32_t count);
    uint32_t *sceAgcCbSetShRegisterRangeDirect(void *command, uint32_t offset,
                                               const uint32_t *values, uint32_t count);
    uint32_t *sceAgcDcbDrawIndexAuto(void *command, uint32_t count, uint64_t modifier);
    uint32_t *sceAgcDcbSetFlip(void *command, uint32_t handle, int buffer_index, uint32_t flip_mode,
                               int64_t flip_argument);
    int32_t sceAgcDriverSubmitDcb(void *description);
    int32_t sceAgcSuspendPoint(void);
    uint32_t sceAgcDriverGetWaitRenderingPacketSizeInDwords(void);
    uint32_t sceAgcDriverWaitUntilSafeForRendering(uint32_t **command, uint32_t packet_size,
                                                   uint32_t reserved, uint32_t handle,
                                                   int buffer_index);
#if PROSPEROLIGHT_GPU_TIMESTAMPS
    // Same call and arguments as the hardware-validated OpenGL runtime probe:
    // BOTTOM_OF_PIPE_TS (40) writing the GPU clock (data select 3) with write
    // confirmation (interrupt select 3).
    uint32_t *sceAgcCbReleaseMem(void *command, uint8_t event, int16_t cache_action,
                                 uint64_t reserved0, int8_t reserved1, void *address,
                                 uint32_t data_select, uint64_t data, uint16_t gds_offset,
                                 uint16_t gds_size, int8_t interrupt_select, int32_t reserved2);
#endif
}

static uint64_t present_now_us(void)
{
    timespec now{};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return static_cast<uint64_t>(now.tv_sec) * UINT64_C(1000000) +
           static_cast<uint64_t>(now.tv_nsec) / UINT64_C(1000);
}

static void report_agc_receipt(const char *receipt)
{
#if PROSPEROLIGHT_VIDEO_OUTPUT_SELF_TEST_FPS != 0
    if (FILE *log = fopen("/download0/prosperolight-agc-selftest.log", "a"))
    {
        fprintf(log, "%s\n", receipt);
        fclose(log);
    }
#endif
    (void)lan_http_report_text(receipt);
#if PROSPEROLIGHT_LAN_TELEMETRY
    notification_request_t notification = {};
    snprintf(notification.message, sizeof(notification.message), "%s", receipt);
    (void)sceKernelSendNotificationRequest(0, &notification, sizeof(notification), 0);
#endif
}

#ifdef NATIVE_AGC_EXTERNAL_SOURCE
#define NATIVE_AGC_ASSET(path) "../../assets/private/" path
#else
#define NATIVE_AGC_ASSET(path) "assets/private/" path
#endif

#define EMBED_ASSET(symbol, path)                                                                  \
    __asm__(".section .rodata\n"                                                                   \
            ".global " #symbol "_start\n" #symbol "_start:\n"                                      \
            ".incbin \"" NATIVE_AGC_ASSET(path) "\"\n"                                             \
                                                ".global " #symbol "_end\n" #symbol "_end:\n"      \
                                                ".text\n");                                        \
    extern const uint8_t symbol##_start[];                                                         \
    extern const uint8_t symbol##_end[]

EMBED_ASSET(native_agc_geometry_header, "geometry.header.bin");
EMBED_ASSET(native_agc_geometry_code, "geometry.text.bin");
EMBED_ASSET(native_agc_pixel_header, "pixel.header.bin");
EMBED_ASSET(native_agc_pixel_code, "pixel.text.linear-buffer.bin");
EMBED_ASSET(native_agc_pixel_hdr_code, "pixel.text.p010-passthrough.bin");
EMBED_ASSET(native_agc_resources, "netflix-video-resources.bin");
EMBED_ASSET(native_loading_prosperolight, "loading-prosperolight-alpha.bin");
EMBED_ASSET(native_loading_connecting, "loading-connecting-alpha.bin");

static const uint8_t hud_font[96 * 8] = {
#include "native_hud_font.inc"
};

static std::atomic<int> tv_safe_area = 1;

static uint8_t agc_out_of_space(agc_command_buffer_t *buffer, uint32_t words, void *user_data)
{
    (void)buffer;
    (void)words;
    (void)user_data;
    return 0;
}

static uint32_t float_bits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static void flush_gpu_data(const void *address, size_t bytes)
{
    const auto *at = static_cast<const uint8_t *>(address);
    const uint8_t *end = at + bytes;

    for (; at < end; at += 64)
        __asm__ volatile("clflush (%0)" : : "r"(at) : "memory");
    __asm__ volatile("mfence" ::: "memory");
}

static void overlay_draw_text_scaled(uint8_t *luma, uint32_t width, uint32_t height,
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

static void overlay_draw_text(uint8_t *luma, uint32_t width, uint32_t height, const char *text,
                              uint32_t x, uint32_t y, uint8_t value)
{
    overlay_draw_text_scaled(luma, width, height, text, x, y, value, HUD_GLYPH_WIDTH,
                             HUD_GLYPH_HEIGHT);
}

static void hud_line(uint8_t *luma, unsigned index, uint8_t value, const char *text)
{
    overlay_draw_text(luma, HUD_WIDTH, HUD_HEIGHT, text, HUD_TEXT_X, 4u + HUD_LINE_HEIGHT * index,
                      value);
}

static void refresh_hud_surface(uint8_t *surface, const native_agc_metrics_t *metrics,
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
             metrics->incoming_fps_x100 % 100u, metrics->video_codec ? "HEVC" : "H.264",
             hdr ? " / HDR" : "", metrics->slices_observed, metrics->slices_requested);
    hud_line(luma, 0, text_luma, line);
    snprintf(line, sizeof(line), "Output: %ux%u @ %u.%02u Hz / V-Sync %s", output_width,
             output_height, output_refresh_x100 / 100u, output_refresh_x100 % 100u,
             metrics->vsync_enabled ? "on" : "off (tearing)");
    hud_line(luma, 1, text_luma, line);
    if (metrics->pipeline_depth > 1u)
        snprintf(line, sizeof(line), "Decoder: Videodec2 adaptive x%u / %u cores (mask 0x%llx)",
                 metrics->pipeline_depth, metrics->decoder_cores,
                 (unsigned long long)metrics->decoder_cpu_mask);
    else
        snprintf(line, sizeof(line), "Decoder: Videodec2 classic / %u cores (mask 0x%llx)",
                 metrics->decoder_cores, (unsigned long long)metrics->decoder_cpu_mask);
    hud_line(luma, 2, text_luma, line);
    snprintf(line, sizeof(line),
             "Decode (last second): %llu.%02llu ms avg / %llu.%02llu ms p95 / load %u%%",
             (unsigned long long)(decode_us / 1000u),
             (unsigned long long)((decode_us % 1000u) / 10u), (unsigned long long)(p95_us / 1000u),
             (unsigned long long)((p95_us % 1000u) / 10u), metrics->decoder_load_permille / 10u);
    hud_line(luma, 3, text_luma, line);
    snprintf(line, sizeof(line),
             "Frame rate: network %u.%02u / decoded %u.%02u / displayed %u.%02u FPS",
             metrics->incoming_fps_x100 / 100u, metrics->incoming_fps_x100 % 100u,
             metrics->decoded_fps_x100 / 100u, metrics->decoded_fps_x100 % 100u,
             metrics->rendering_fps_x100 / 100u, metrics->rendering_fps_x100 % 100u);
    hud_line(luma, 4, text_luma, line);
    snprintf(line, sizeof(line), "Frames dropped by your network connection: %u.%02u%%",
             metrics->network_drop_percent_x100 / 100u, metrics->network_drop_percent_x100 % 100u);
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
    snprintf(line, sizeof(line), "Host processing latency min/max/average: %u.%u/%u.%u/%u.%u ms",
             metrics->host_min_tenths_ms / 10u, metrics->host_min_tenths_ms % 10u,
             metrics->host_max_tenths_ms / 10u, metrics->host_max_tenths_ms % 10u,
             metrics->host_average_tenths_ms / 10u, metrics->host_average_tenths_ms % 10u);
    hud_line(luma, 8, text_luma, line);
    snprintf(line, sizeof(line), "Queue: %u frames now / avg %llu.%02llu ms / max %llu.%02llu ms",
             metrics->pending_frames, (unsigned long long)(metrics->queue_delay_average_us / 1000u),
             (unsigned long long)((metrics->queue_delay_average_us % 1000u) / 10u),
             (unsigned long long)(metrics->queue_delay_max_us / 1000u),
             (unsigned long long)((metrics->queue_delay_max_us % 1000u) / 10u));
    hud_line(luma, 9, text_luma, line);
}

static void overlay_fill_rect(uint8_t *luma, uint32_t width, uint32_t height, uint32_t x,
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

static void refresh_keyboard_surface(uint8_t *surface, uint32_t selected, int shifted, int hdr)
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

static int shader_resource_offset(void *shader, unsigned kind, uint32_t *offset)
{
    uint8_t *layout = *(uint8_t **)((uint8_t *)shader + 8);
    uint16_t *counts;
    uint16_t *entries;

    if (!layout || kind >= 4)
        return -1;
    counts = (uint16_t *)(layout + 46);
    if (!counts[kind])
        return -1;
    entries = *(uint16_t **)(layout + 8 + kind * sizeof(void *));
    *offset = entries[0] & 0x7fffu;
    return 0;
}

static int copy_asset(void *destination, size_t capacity, const uint8_t *start, const uint8_t *end)
{
    uintptr_t start_address = (uintptr_t)start;
    uintptr_t end_address = (uintptr_t)end;
    size_t bytes;

    if (end_address < start_address)
        return -1;
    bytes = (size_t)(end_address - start_address);

    if (bytes > capacity)
        return -1;
    memcpy(destination, start, bytes);
    return 0;
}

static int prepare_resources(uint8_t *resources, int hdr)
{
    uint32_t *header = (uint32_t *)resources;
    uint32_t table_offsets[2] = {header[1], header[3]};
    static const uint32_t table_counts[2] = {2, 8};
    uint32_t *limited_offset = (uint32_t *)(resources + 0x500);
    uint32_t *limited_scale = (uint32_t *)(resources + 0x600);
    uint32_t *sample_scale = (uint32_t *)(resources + 0x700);

    if (limited_offset[0] != 0x3d802008u || limited_offset[1] != 0x3d802008u ||
        limited_offset[2] != 0x3d802008u || limited_scale[0] != 0x3f957abdu ||
        limited_scale[1] != 0x3f922492u || limited_scale[2] != 0x3f922492u ||
        sample_scale[0] != 0x42801f88u)
        return -1;

    if (!hdr)
    {
        limited_offset[0] = limited_offset[1] = limited_offset[2] = 0x3d808081u;
        limited_scale[0] = 0x3f950a85u;
        limited_scale[1] = limited_scale[2] = 0x3f91b6dbu;
        sample_scale[0] = 0x3f800000u;
    }

    for (uint32_t table = 0; table < 2; ++table)
    {
        uint32_t *entry = (uint32_t *)(resources + table_offsets[table]);
        for (uint32_t index = 0; index < table_counts[table]; ++index, entry += 4)
        {
            uintptr_t address = (uintptr_t)resources + entry[0];
            entry[0] = (uint32_t)address;
            entry[1] = (entry[1] & 0xffff0000u) | (uint32_t)(address >> 32);
        }
    }
    return 0;
}

static void bind_pixel_source(agc_command_buffer_t *command, uint8_t *resources, const void *source,
                              size_t y_bytes, size_t uv_bytes, const void *pixel_cb)
{
    uint32_t descriptor[30] = {};
    uintptr_t uv = (uintptr_t)source + y_bytes;
    uint32_t *header = (uint32_t *)resources;
    uintptr_t table = (uintptr_t)resources + header[3];

    descriptor[0] = (uint32_t)(uintptr_t)source;
    descriptor[1] = (uint32_t)((uintptr_t)source >> 32);
    descriptor[2] = (uint32_t)y_bytes;
    descriptor[3] = 0x31016facu;
    descriptor[5] = 0x00700000u;
    descriptor[8] = (uint32_t)uv;
    descriptor[9] = (uint32_t)(uv >> 32);
    descriptor[10] = (uint32_t)uv_bytes;
    descriptor[11] = 0x31016facu;
    descriptor[13] = 0x00700000u;
    descriptor[16] = descriptor[20] = 0x00000092u;
    descriptor[17] = descriptor[21] = 0x00fff000u;
    descriptor[18] = descriptor[22] = kNativeAgcBilinearSamplerWord;
    descriptor[24] = (uint32_t)(uintptr_t)pixel_cb;
    descriptor[25] = (uint32_t)((uintptr_t)pixel_cb >> 32) | (16u << 16);
    descriptor[26] = 4;
    descriptor[27] = 0x0004dfacu;
    descriptor[28] = (uint32_t)table;
    descriptor[29] = (uint32_t)(table >> 32);
    sceAgcCbSetShRegisterRangeDirect(command, 0x0c, descriptor, 30);
}

static void bind_main10_source(agc_command_buffer_t *command, uint8_t *resources,
                               const void *source, size_t y_bytes, uint32_t visible_width,
                               uint32_t visible_height, const void *pixel_cb)
{
    uint32_t descriptor[30] = {};
    uintptr_t uv = (uintptr_t)source + y_bytes;
    uint32_t *header = (uint32_t *)resources;
    uintptr_t table = (uintptr_t)resources + header[3];
    uint32_t y_width = visible_width - 1u;
    uint32_t y_height = visible_height - 1u;
    uint32_t uv_width = visible_width / 2u - 1u;
    uint32_t uv_height = (visible_height + 1u) / 2u - 1u;

    descriptor[0] = (uint32_t)((uintptr_t)source >> 8);
    descriptor[1] = 0x00700000u | ((y_width & 3u) << 30) | (uint32_t)((uintptr_t)source >> 40);
    descriptor[2] = (y_width >> 2) | (y_height << 14);
    descriptor[3] = 0x90000204u;
    descriptor[5] = 0x00700000u;
    descriptor[8] = (uint32_t)(uv >> 8);
    descriptor[9] = 0x01700000u | ((uv_width & 3u) << 30) | (uint32_t)(uv >> 40);
    descriptor[10] = (uv_width >> 2) | (uv_height << 14);
    descriptor[11] = 0x9000022cu;
    descriptor[13] = 0x00700000u;
    descriptor[16] = descriptor[20] = 0x00000092u;
    descriptor[17] = descriptor[21] = 0x00fff000u;
    descriptor[18] = descriptor[22] = kNativeAgcBilinearSamplerWord;
    descriptor[24] = (uint32_t)(uintptr_t)pixel_cb;
    descriptor[25] = (uint32_t)((uintptr_t)pixel_cb >> 32) | (16u << 16);
    descriptor[26] = 3;
    descriptor[27] = 0x0004dfacu;
    descriptor[28] = (uint32_t)table;
    descriptor[29] = (uint32_t)(table >> 32);
    sceAgcCbSetShRegisterRangeDirect(command, 0x0c, descriptor, 30);
}

static NativeAgcPerformance agc_performance;

void native_agc_reset_performance()
{
    agc_performance = {};
}

const NativeAgcPerformance &native_agc_performance()
{
    return agc_performance;
}

static int render_frame(int video, int buffer_index, void *target, uint8_t *memory,
                        void *vertex_shader, void *pixel_shader, void *hud_pixel_shader,
                        const void *source, size_t source_bytes, uint32_t pitch,
                        uint32_t surface_height, uint32_t visible_width, uint32_t visible_height,
                        uint32_t output_width, uint32_t output_height, int64_t render_marker,
                        uint32_t flip_mode, bool *submitted, uint32_t *word_count, int hdr,
                        int draw_overlay, uint32_t overlay_width, uint32_t overlay_height,
                        uint32_t overlay_x, uint32_t overlay_y, uint32_t overlay_scale,
                        float overlay_alpha)
{
    if (submitted)
        *submitted = false;
    const uint64_t prepare_started = moonlight::performance_now_us();
    static const uint16_t target_offsets[16] = {0x318, 0x31b, 0x31c, 0x31d, 0x31e, 0x31f,
                                                0x321, 0x323, 0x324, 0x325, 0x390, 0x398,
                                                0x3a0, 0x3a8, 0x3b0, 0x3b8};
    static const uint32_t geometry_constants[16] = {
        /* The recovered Netflix quad originally overscanned 32 source pixels
         * on the left and 16 on the bottom. Keep its native bounds but fit
         * the complete decoded frame to the selected AGC viewport. */
        0x3fa24ce6, 0, 0,          0x3e2f0fdd, 0, 0x3fa21449, 0, 0x3e111049,
        0,          0, 0xbf800000, 0x80000000, 0, 0,          0, 0x3f800000};
    static const uint32_t pixel_constants[16] = {
        0x3f800000, 0x3f800000, 0x3f800000, 0, 0x3fed844d, 0xbe3fd0d0, 0, 0,
        0,          0xbeefad6d, 0x3fc9930c, 0, 0,          0,          0, 0};
    static const uint32_t hdr_pixel_constants[12] = {0x3f800000, 0x3f800000, 0x3f800000, 0,
                                                     0,          0xbe28809d, 0x3ff0d1b7, 0,
                                                     0x3fbcbfb1, 0xbf124433, 0,          0};
    agc_register_t *cx = (agc_register_t *)(memory + 0x7000);
    uint8_t *geometry_cb = memory + 0x7800;
    uint8_t *pixel_cb = memory + 0x7900;
    uint8_t *hud_pixel_cb = memory + 0x7a00;
    agc_register_t *hud_state = (agc_register_t *)(memory + 0x7b00);
    uint8_t *resources = memory + 0xc000;
    uint8_t *hud_resources = hdr ? memory + HDR_HUD_RESOURCES_OFFSET : resources;
    uint32_t *words = (uint32_t *)(memory + 0x8000);
    agc_command_buffer_t command = {};
    agc_submit_description_t submit = {};
    uint32_t descriptor[30] = {};
    void *defaults = sceAgcGetRegisterDefaults();
    agc_register_t **blocks;
    uint32_t default_count;
    uint32_t cx_count = 16;
    uint32_t slot;
    size_t component_bytes = hdr ? sizeof(uint16_t) : 1u;
    size_t y_bytes = (size_t)pitch * surface_height * component_bytes;
    size_t uv_bytes = (size_t)pitch * ((surface_height + 1u) / 2u) * component_bytes;
    uint32_t inset_x = std::atomic_load_explicit(&tv_safe_area, std::memory_order_relaxed)
                           ? output_width * TV_SAFE_INSET_X / BASE_OUTPUT_WIDTH
                           : 0u;
    uint32_t inset_y = std::atomic_load_explicit(&tv_safe_area, std::memory_order_relaxed)
                           ? output_height * TV_SAFE_INSET_Y / BASE_OUTPUT_HEIGHT
                           : 0u;

    if (!defaults || visible_width == 0 || visible_height == 0 || visible_width > pitch ||
        visible_height > surface_height || (pitch & 1u) != 0 || y_bytes > UINT32_MAX ||
        uv_bytes > UINT32_MAX || y_bytes + uv_bytes > source_bytes || output_width == 0 ||
        output_height == 0 || overlay_scale == 0)
        return -1;

    blocks = *(agc_register_t ***)defaults;
    default_count = *(uint32_t *)((uint8_t *)defaults + 0x20);
    for (uint32_t index = 0; index < 16; ++index)
    {
        cx[index] = agc_register_t{target_offsets[index], 0, 0};
        for (uint32_t candidate = 0; blocks && blocks[0] && candidate < default_count; ++candidate)
        {
            if (blocks[0][candidate].offset == target_offsets[index])
            {
                cx[index].value = blocks[0][candidate].value;
                break;
            }
        }
    }

    cx[0].value = (uint32_t)((uintptr_t)target >> 8);
    cx[1].value &= 0xfc001fffu;
    cx[2].value = (cx[2].value & ~(0x7cu | 0x700u | 0x1800u | 0x10000000u | 0x10000u | 0x8000u |
                                   0x40000u | 0x4000u)) |
                  (hdr ? 0x24u : 0x28u) | 0x8000u;
    cx[3].value &= ~(0x7000u | 0x18000u);
    cx[4].value = (cx[4].value & ~(0x60u | 0x0cu | 0x00100200u | 0x80000u)) | 0x48u;
    cx[5].value = cx[6].value = cx[9].value = 0;
    cx[10].value = (cx[10].value & 0xffffff00u) | (uint32_t)((uintptr_t)target >> 40);
    cx[11].value &= 0xffffff00u;
    cx[12].value &= 0xffffff00u;
    cx[13].value &= 0xffffff00u;
    cx[14].value = (output_height - 1u) | ((output_width - 1u) << 14);
    cx[15].value = (cx[15].value & ~(0x1fffu | 0x7c000u | 0x03000000u | 0x44000000u)) | 0x6c000u |
                   0x01000000u | 0x44000000u;

#define ADD_REG(register_offset, register_value)                                                   \
    do                                                                                             \
    {                                                                                              \
        cx[cx_count++] = agc_register_t{(register_offset), 0, (register_value)};                   \
    } while (0)
    ADD_REG(0x10f, float_bits((output_width - inset_x * 2u) * .5f));
    ADD_REG(0x110, float_bits(output_width * .5f));
    ADD_REG(0x111, float_bits((output_height - inset_y * 2u) * -.5f));
    ADD_REG(0x112, float_bits(output_height * .5f));
    ADD_REG(0x113, float_bits(1));
    ADD_REG(0x114, 0);
    ADD_REG(0x0b4, 0);
    ADD_REG(0x0b5, float_bits(1));
    ADD_REG(0x2fa, float_bits(1));
    ADD_REG(0x2fb, float_bits(1));
    ADD_REG(0x2fc, float_bits(1));
    ADD_REG(0x2fd, float_bits(1));
    ADD_REG(0x090, 0x80000000u);
    ADD_REG(0x091, output_width | (output_height << 16));
    ADD_REG(0x08e, 0x0f);
#undef ADD_REG

    memcpy(geometry_cb, geometry_constants, sizeof(geometry_constants));
    if (hdr)
    {
        memcpy(pixel_cb, hdr_pixel_constants, sizeof(hdr_pixel_constants));
    }
    else
    {
        memcpy(pixel_cb, pixel_constants, sizeof(pixel_constants));
        ((uint32_t *)pixel_cb)[12] = float_bits((float)visible_width);
        ((uint32_t *)pixel_cb)[13] = float_bits((float)visible_height);
        ((uint32_t *)pixel_cb)[14] = pitch;
        ((uint32_t *)pixel_cb)[15] = pitch / 2u;
    }

    command.bottom = words;
    command.top = words + 0x4000u / sizeof(*words);
    command.up = words;
    command.down = command.top;
    command.callback = (uintptr_t)agc_out_of_space;

    sceAgcDriverWaitUntilSafeForRendering(&command.up,
                                          sceAgcDriverGetWaitRenderingPacketSizeInDwords(), 0,
                                          (uint32_t)video, buffer_index);
#if PROSPEROLIGHT_GPU_TIMESTAMPS
    {
        // Zero, then write back, before submission: a dirty CPU line evicted
        // later must not overwrite the GPU's stamps.
        auto *stamps = reinterpret_cast<volatile uint64_t *>(memory + GPU_TIMESTAMP_OFFSET);
        stamps[0] = stamps[1] = 0;
        flush_gpu_data(memory + GPU_TIMESTAMP_OFFSET, 64);
        if (!sceAgcCbReleaseMem(&command, 40, 0, 0, 0, memory + GPU_TIMESTAMP_OFFSET, 3, 0, 0, 0, 3,
                                0))
            return -9;
    }
#endif

    {
        agc_register_t *link_cx = (agc_register_t *)(memory + 0x5000);
        agc_register_t *combined_sh = (agc_register_t *)(memory + 0x6800);
        agc_register_t *vs_cx = *(agc_register_t **)((uint8_t *)vertex_shader + 24);
        agc_register_t *ps_cx = *(agc_register_t **)((uint8_t *)pixel_shader + 24);
        agc_register_t *vs_sh = *(agc_register_t **)((uint8_t *)vertex_shader + 32);
        agc_register_t *ps_sh = *(agc_register_t **)((uint8_t *)pixel_shader + 32);
        uint32_t vs_cx_count = *((uint8_t *)vertex_shader + 91);
        uint32_t ps_cx_count = *((uint8_t *)pixel_shader + 91);
        uint32_t vs_sh_count = *((uint8_t *)vertex_shader + 92);
        uint32_t ps_sh_count = *((uint8_t *)pixel_shader + 92);

        memcpy(cx + cx_count, link_cx, 34 * sizeof(*cx));
        cx_count += 34;
        memcpy(cx + cx_count, vs_cx, vs_cx_count * sizeof(*cx));
        cx_count += vs_cx_count;
        memcpy(cx + cx_count, ps_cx, ps_cx_count * sizeof(*cx));
        cx_count += ps_cx_count;
        memcpy(combined_sh, vs_sh, vs_sh_count * sizeof(*combined_sh));
        memcpy(combined_sh + vs_sh_count, ps_sh, ps_sh_count * sizeof(*combined_sh));
        sceAgcDcbSetCxRegistersIndirect(&command, cx, cx_count);
        sceAgcDcbSetUcRegistersIndirect(&command, memory + 0x6000, 3);
        sceAgcDcbSetShRegistersIndirect(&command, combined_sh, vs_sh_count + ps_sh_count);
    }

    if (shader_resource_offset(vertex_shader, 3, &slot) != 0)
        return -2;
    descriptor[0] = (uint32_t)(uintptr_t)geometry_cb;
    descriptor[1] = (uint32_t)((uintptr_t)geometry_cb >> 32) | (16u << 16);
    descriptor[2] = 4;
    descriptor[3] = 0x0004dfacu;
    {
        uint32_t *header = (uint32_t *)resources;
        uintptr_t table = (uintptr_t)resources + header[1];
        uintptr_t vertex = (uintptr_t)resources + header[2];
        descriptor[4] = (uint32_t)table;
        descriptor[5] = (uint32_t)(table >> 32);
        descriptor[6] = (uint32_t)vertex;
        descriptor[7] = (uint32_t)(vertex >> 32);
    }
    sceAgcCbSetShRegisterRangeDirect(&command, 0x8c + slot, descriptor, 8);

    if (hdr)
        bind_main10_source(&command, resources, source, y_bytes, visible_width, visible_height,
                           pixel_cb);
    else
        bind_pixel_source(&command, resources, source, y_bytes, uv_bytes, pixel_cb);
    sceAgcDcbDrawIndexAuto(&command, 4, 2);
    if (draw_overlay)
    {
        const agc_register_t state[11] = {
            {0x10f, 0, float_bits(overlay_width * overlay_scale * .5f)},
            {0x110, 0, float_bits(overlay_x + overlay_width * overlay_scale * .5f)},
            {0x111, 0, float_bits(overlay_height * overlay_scale * -.5f)},
            {0x112, 0, float_bits(overlay_y + overlay_height * overlay_scale * .5f)},
            {0x113, 0, float_bits(1)},
            {0x114, 0, 0},
            {0x105, 0, float_bits(overlay_alpha)},
            {0x106, 0, float_bits(overlay_alpha)},
            {0x107, 0, float_bits(overlay_alpha)},
            {0x108, 0, float_bits(overlay_alpha)},
            /* src * constant alpha + dst * (1 - constant alpha) */
            {0x1e0, 0, 0x40001413u},
        };

        memcpy(hud_pixel_cb, pixel_constants, sizeof(pixel_constants));
        ((uint32_t *)hud_pixel_cb)[12] = float_bits((float)overlay_width);
        ((uint32_t *)hud_pixel_cb)[13] = float_bits((float)overlay_height);
        ((uint32_t *)hud_pixel_cb)[14] = overlay_width;
        ((uint32_t *)hud_pixel_cb)[15] = overlay_width / 2u;
        memcpy(hud_state, state, sizeof(state));
        sceAgcDcbSetCxRegistersIndirect(&command, hud_state, 11);
        if (hdr)
        {
            agc_register_t *hud_ps_sh = *(agc_register_t **)((uint8_t *)hud_pixel_shader + 32);
            uint32_t hud_ps_sh_count = *((uint8_t *)hud_pixel_shader + 92);

            sceAgcDcbSetShRegistersIndirect(&command, hud_ps_sh, hud_ps_sh_count);
        }
        bind_pixel_source(&command, hud_resources, memory + HUD_SURFACE_OFFSET,
                          (size_t)overlay_width * overlay_height,
                          (size_t)overlay_width * overlay_height / 2u, hud_pixel_cb);
        sceAgcDcbDrawIndexAuto(&command, 4, 2);
    }
#if PROSPEROLIGHT_GPU_TIMESTAMPS
    if (!sceAgcCbReleaseMem(&command, 40, 0, 0, 0, memory + GPU_TIMESTAMP_OFFSET + 8u, 3, 0, 0, 0,
                            3, 0))
        return -9;
#endif
    sceAgcDcbSetFlip(&command, (uint32_t)video, buffer_index, flip_mode, render_marker);

    submit.words = words;
    submit.word_count = (uint32_t)(command.up - words);
    *word_count = submit.word_count;
    moonlight::record_performance_elapsed(agc_performance.prepare, prepare_started);
    const uint64_t flush_started = moonlight::performance_now_us();
    // Shader code, linked state and resource tables were published at setup.
    // Only the combined registers, constants and command bank change per frame.
    // Flush the whole command bank (including downward-growing indirect data).
    flush_gpu_data(memory + 0x6800, 0xc000 - 0x6800);
    moonlight::record_performance_elapsed(agc_performance.cache_flush, flush_started);
    {
        const uint64_t submit_started = moonlight::performance_now_us();
        int32_t result = sceAgcDriverSubmitDcb(&submit);
        if (submitted)
            *submitted = result == 0;
        if (result == 0)
            result = sceAgcSuspendPoint();
        moonlight::record_performance_elapsed(agc_performance.submit, submit_started);
        return result;
    }
}

typedef struct native_agc_presenter
{
    uint8_t *shader_memory;
    int64_t shader_start;
    void *framebuffer;
    int64_t framebuffer_start;
    void *vertex_shader;
    void *pixel_shader;
    void *hud_pixel_shader;
    int video;
    uint32_t frame_number;
    uint32_t keyboard_generation;
    uint32_t output_width;
    uint32_t output_height;
    uint32_t scanout_width;
    uint32_t scanout_height;
    uint32_t scanout_refresh_x100;
    uint32_t requested_fps;
    size_t framebuffer_bytes;
    size_t framebuffer_pool_bytes;
    int64_t overlay_marker;
    const void *pending_source;
    int64_t pending_marker;
    void *flip_queue;
    uint32_t pending_flip_mode;
    uint8_t overlay_kind;
    uint8_t hdr;
    uint8_t ready;
} native_agc_presenter_t;

static native_agc_presenter_t presenter = {
    .shader_memory = nullptr,
    .shader_start = -1,
    .framebuffer = nullptr,
    .framebuffer_start = -1,
    .vertex_shader = nullptr,
    .pixel_shader = nullptr,
    .hud_pixel_shader = nullptr,
    .video = -1,
    .frame_number = 0,
    .keyboard_generation = 0,
    .output_width = 0,
    .output_height = 0,
    .scanout_width = 0,
    .scanout_height = 0,
    .scanout_refresh_x100 = 0,
    .requested_fps = 60,
    .framebuffer_bytes = 0,
    .framebuffer_pool_bytes = 0,
    .overlay_marker = 0,
    .pending_source = nullptr,
    .pending_marker = 0,
    .flip_queue = nullptr,
    .pending_flip_mode = 0,
    .overlay_kind = 0,
    .hdr = 0,
    .ready = 0,
};
static uint64_t agc_state;
static uint8_t agc_initialized;
static std::atomic<int> hud_enabled = 1;
static std::atomic<int> keyboard_enabled = 0;
static std::atomic<uint32_t> keyboard_selected = 0;
static std::atomic<int> keyboard_shifted = 0;
static std::atomic<uint32_t> keyboard_generation = 0;
static std::atomic<uint32_t> requested_flip_mode = VIDEO_OUT_FLIP_MODE_VSYNC;
static std::atomic<int> hsync_rejected = 0;

static uint32_t effective_flip_mode(void)
{
    return std::atomic_load_explicit(&hsync_rejected, std::memory_order_relaxed)
               ? VIDEO_OUT_FLIP_MODE_VSYNC
               : std::atomic_load_explicit(&requested_flip_mode, std::memory_order_relaxed);
}

void native_agc_set_vsync(int enabled)
{
    std::atomic_store_explicit(&requested_flip_mode,
                               enabled ? VIDEO_OUT_FLIP_MODE_VSYNC : VIDEO_OUT_FLIP_MODE_HSYNC,
                               std::memory_order_relaxed);
}

int native_agc_vsync_active(void)
{
    return effective_flip_mode() == VIDEO_OUT_FLIP_MODE_VSYNC;
}

int native_agc_flip_events_active(void)
{
    return presenter.flip_queue != nullptr;
}

static void open_flip_events(void)
{
    void *queue = nullptr;
    const int create_result = sceKernelCreateEqueue(&queue, "prosperolight-flip");
    int add_result = -1;
    char receipt[160];

    if (create_result == 0 && queue)
    {
        add_result = sceVideoOutAddFlipEvent(queue, presenter.video, nullptr);
        if (add_result == 0)
            presenter.flip_queue = queue;
        else
            (void)sceKernelDeleteEqueue(queue);
    }
    snprintf(receipt, sizeof(receipt), "Native flip events: create=%08x add=%08x active=%u",
             (uint32_t)create_result, (uint32_t)add_result, presenter.flip_queue ? 1u : 0u);
    report_agc_receipt(receipt);
}

static void close_flip_events(void)
{
    if (!presenter.flip_queue)
        return;
    if (presenter.video >= 0)
        (void)sceVideoOutDeleteFlipEvent(presenter.flip_queue, presenter.video);
    (void)sceKernelDeleteEqueue(presenter.flip_queue);
    presenter.flip_queue = nullptr;
}

// Flip status is authoritative. A flip event only wakes the waiter early: an
// event wait that does not find the flip complete (a stale, missed, coalesced
// or failing event) is always followed by one short sleep before the queue is
// tried again, so no queue behaviour can turn this into a spin or a false
// timeout. No vblank waits: a vblank can wake before VideoOut publishes flip
// status, costing a refresh.
static int wait_for_marker(int64_t marker, unsigned *waits_out)
{
    const uint64_t budget_us = presenter.requested_fps > 60u ? 100000u : 500000u;
    const unsigned max_polls = static_cast<unsigned>(budget_us / PROSPEROLIGHT_FLIP_POLL_US);
    const unsigned max_waits = presenter.flip_queue ? max_polls * 2u : max_polls;
    uint64_t status[16] = {};
    unsigned waits = 0;
    bool timed_out = false;
    bool use_queue = presenter.flip_queue != nullptr;

    if (marker > 0 && presenter.video >= 0)
    {
        const uint64_t started = present_now_us();
        for (;;)
        {
            if (PROSPEROLIGHT_PERFORMANCE_DETAIL)
                ++agc_performance.flip_queries;
            if (sceVideoOutGetFlipStatus(presenter.video, status) == 0 &&
                (int64_t)status[3] >= marker)
                break;
            const uint64_t now = present_now_us();
            if (waits >= max_waits || (started && now >= started + budget_us))
            {
                timed_out = true;
                break;
            }
            ++waits;
            if (PROSPEROLIGHT_PERFORMANCE_DETAIL)
                ++agc_performance.flip_sleeps;
            if (use_queue)
            {
                uint64_t event[4] = {};
                int count = 0;
                unsigned int timeout = FLIP_EVENT_WAIT_US;
                const int result =
                    sceKernelWaitEqueue(presenter.flip_queue, event, 1, &count, &timeout);
                if (result == 0 && count > 0)
                    ++agc_performance.flip_event_wakeups;
                else if (result != 0 && (uint32_t)result != KERNEL_ERROR_ETIMEDOUT)
                    ++agc_performance.flip_event_errors;
                use_queue = false;
            }
            else
            {
                sceKernelUsleep(PROSPEROLIGHT_FLIP_POLL_US);
                use_queue = presenter.flip_queue != nullptr;
            }
        }
    }
    if (waits_out)
        *waits_out = waits;
    if (timed_out)
        ++agc_performance.flip_timeouts;
    return timed_out ? -5 : 0;
}

#if PROSPEROLIGHT_GPU_TIMESTAMPS
static void record_gpu_timestamps(void)
{
    if (!presenter.shader_memory)
        return;
    flush_gpu_data(presenter.shader_memory + GPU_TIMESTAMP_OFFSET, 64);
    const auto *stamps =
        reinterpret_cast<const volatile uint64_t *>(presenter.shader_memory + GPU_TIMESTAMP_OFFSET);
    const uint64_t start = stamps[0], end = stamps[1];
    if (!start || end < start)
    {
        ++agc_performance.gpu_samples_invalid;
        return;
    }
    agc_performance.gpu_render.add((end - start) * GPU_TICK_PICOSECONDS / UINT64_C(1000000));
}
#endif

int native_agc_wait_source_idle(const void *source)
{
    if (!source)
        return -1;
    return source == presenter.pending_source ? native_agc_finish_frame() : 0;
}

int native_agc_finish_frame(void)
{
    const bool pending = presenter.pending_marker > 0;
    int result = wait_for_marker(presenter.pending_marker, NULL);
    if (result != 0 && pending && presenter.pending_flip_mode == VIDEO_OUT_FLIP_MODE_HSYNC)
    {
        // An immediate flip that is never reported: its commands finished long
        // ago, so stop waiting for it and use V-Sync for the rest of the process.
        std::atomic_store_explicit(&hsync_rejected, 1, std::memory_order_relaxed);
        ++agc_performance.vsync_fallbacks;
        report_agc_receipt("Native AGC immediate flip never completed; using V-Sync");
        result = 0;
    }
    if (result == 0)
    {
#if PROSPEROLIGHT_GPU_TIMESTAMPS
        if (pending)
            record_gpu_timestamps();
#else
        (void)pending;
#endif
        presenter.pending_marker = 0;
        presenter.pending_source = nullptr;
    }
    return result;
}

void native_agc_output_status(uint32_t *width, uint32_t *height, uint32_t *refresh_x100)
{
    *width = presenter.output_width;
    *height = presenter.output_height;
    *refresh_x100 = presenter.scanout_refresh_x100;
}

static uint32_t video_output_refresh_x100(uint64_t refresh_rate)
{
    if (refresh_rate == VIDEO_OUT_REFRESH_RATE_89_91)
        return 8991u;
    if (refresh_rate == VIDEO_OUT_REFRESH_RATE_119_88)
        return 11988u;
    if (refresh_rate == VIDEO_OUT_REFRESH_RATE_59_94)
        return 5994u;
    return 0u;
}

static int configure_high_refresh_output(int32_t handle, uint32_t requested_fps,
                                         int32_t *support_result, int32_t *preset_result,
                                         int32_t *vrr_result)
{
    *support_result = 0;
    *preset_result = 0;
    *vrr_result = 0;
    *support_result =
        sceVideoOutIsOutputSupported(handle, VIDEO_OUT_REQUEST_120_HZ, NULL, NULL, NULL);
    if (*support_result <= 0)
        return *support_result ? *support_result : -1;
    *preset_result = sceVideoOutConfigureOutput(handle, VIDEO_OUT_REQUEST_120_HZ, NULL, NULL, NULL);
    if (*preset_result != 0)
        return *preset_result;
    if (requested_fps == 90u)
        *vrr_result = sceVideoOutVrrUnpegFromFixedRate(handle);
    return *vrr_result;
}

static int configure_launcher_output(int32_t handle)
{
    return sceVideoOutConfigureOutput(handle, VIDEO_OUT_REQUEST_DEFAULT, NULL, NULL, NULL);
}

static void update_presenter_output_status(const char *stage)
{
    video_resolution_status_t resolution = {};
    video_output_status_t output = {};
    const uint32_t previous_width = presenter.scanout_width;
    const uint32_t previous_height = presenter.scanout_height;
    const uint32_t previous_refresh = presenter.scanout_refresh_x100;
    const int resolution_result = sceVideoOutGetResolutionStatus(presenter.video, &resolution);
    const int output_result = sceVideoOutGetOutputStatus(presenter.video, &output);
    const uint32_t reported_width = presenter.output_width;
    const uint32_t reported_height = presenter.output_height;
    uint32_t reported_refresh = video_output_refresh_x100(resolution.refresh_rate);
    if (!reported_refresh && output_result == 0)
        reported_refresh = video_output_refresh_x100(output.refresh_rate);

    if (reported_width && reported_height)
    {
        presenter.scanout_width = reported_width;
        presenter.scanout_height = reported_height;
    }
    if (reported_refresh)
        presenter.scanout_refresh_x100 = reported_refresh;

    if (stage || presenter.scanout_width != previous_width ||
        presenter.scanout_height != previous_height ||
        presenter.scanout_refresh_x100 != previous_refresh)
    {
        char receipt[512];

        snprintf(receipt, sizeof(receipt),
                 "Native VideoOut %s: resolution_rc=%08x output_rc=%08x full=%ux%u "
                 "pane=%ux%u refresh_ids=%llu/%llu active=%ux%u@%u.%02u requested=%ux%u@%u",
                 stage ? stage : "changed", (uint32_t)resolution_result, (uint32_t)output_result,
                 resolution.full_width, resolution.full_height, resolution.pane_width,
                 resolution.pane_height, (unsigned long long)resolution.refresh_rate,
                 (unsigned long long)output.refresh_rate, presenter.scanout_width,
                 presenter.scanout_height, presenter.scanout_refresh_x100 / 100u,
                 presenter.scanout_refresh_x100 % 100u, presenter.output_width,
                 presenter.output_height, presenter.requested_fps);
        report_agc_receipt(receipt);
    }
}

void native_agc_set_hud_enabled(int enabled)
{
    std::atomic_store_explicit(&hud_enabled, enabled != 0, std::memory_order_relaxed);
}

int native_agc_hud_enabled(void)
{
    return std::atomic_load_explicit(&hud_enabled, std::memory_order_relaxed);
}

void native_agc_set_keyboard_state(int enabled, uint32_t selected, int shifted)
{
    std::atomic_store_explicit(&keyboard_selected, selected, std::memory_order_relaxed);
    std::atomic_store_explicit(&keyboard_shifted, shifted != 0, std::memory_order_relaxed);
    std::atomic_store_explicit(&keyboard_enabled, enabled != 0, std::memory_order_release);
    std::atomic_fetch_add_explicit(&keyboard_generation, 1u, std::memory_order_relaxed);
}

void native_agc_set_tv_safe_area(int enabled)
{
    std::atomic_store_explicit(&tv_safe_area, enabled != 0, std::memory_order_relaxed);
}

static int initialize_presenter(const void *source, size_t source_bytes, uint32_t pitch,
                                uint32_t surface_height, uint32_t visible_width,
                                uint32_t visible_height, uint32_t requested_fps, int hdr,
                                const NativeAgcOutputGeometry &output)
{
    const size_t framebuffer_pool_bytes = output.framebuffer_bytes * 2u;
    int64_t shader_start = -1;
    int64_t framebuffer_start = -1;
    int64_t direct_limit = sceKernelGetDirectMemorySize();
    int32_t result;
    int32_t link_result = -1;
    int32_t mode_result = 0;
    int32_t mode_support_result = 0;
    int32_t mode_preset_result = 0;
    int32_t mode_vrr_result = 0;
    uint8_t reused = agc_initialized;
    char receipt[512];

    if (!source || source_bytes == 0 || direct_limit <= 0)
        return -1;

    presenter.output_width = output.width;
    presenter.output_height = output.height;
    presenter.scanout_width = output.width;
    presenter.scanout_height = output.height;
    presenter.scanout_refresh_x100 = 0;
    presenter.requested_fps = requested_fps;
    presenter.framebuffer_bytes = output.framebuffer_bytes;
    presenter.framebuffer_pool_bytes = framebuffer_pool_bytes;

    result = 0;
    if (!agc_initialized)
    {
        result = sceAgcInit(&agc_state, 8);
        if (result == 0)
            agc_initialized = 1;
    }
    snprintf(receipt, sizeof(receipt),
             "Native AGC stage 1: init=%08x reused=%u source=%p bytes=%zx pitch=%u surface=%u "
             "visible=%ux%u",
             (uint32_t)result, reused ? 1u : 0u, source, source_bytes, pitch, surface_height,
             visible_width, visible_height);
    report_agc_receipt(receipt);
    if (result != 0)
        return result;

    result = sceKernelAllocateDirectMemory(0, direct_limit, SHADER_MEMORY_BYTES, 0x4000,
                                           DIRECT_MEMORY_TYPE, &shader_start);
    presenter.shader_start = shader_start;
    if (result == 0)
        result = sceKernelMapDirectMemory((void **)&presenter.shader_memory, SHADER_MEMORY_BYTES,
                                          MAP_PROTECTION, 0, shader_start, 0x4000);
    if (result != 0 || !presenter.shader_memory)
        return result ? result : -2;

    memset(presenter.shader_memory, 0, SHADER_MEMORY_BYTES);
    if (copy_asset(presenter.shader_memory, 0x1000, native_agc_geometry_header_start,
                   native_agc_geometry_header_end) != 0 ||
        copy_asset(presenter.shader_memory + 0x3700, 0x1000, native_agc_geometry_code_start,
                   native_agc_geometry_code_end) != 0 ||
        copy_asset(presenter.shader_memory + 0x1000, 0x1000, native_agc_pixel_header_start,
                   native_agc_pixel_header_end) != 0 ||
        copy_asset(presenter.shader_memory + 0x2000, 0x1000,
                   hdr ? native_agc_pixel_hdr_code_start : native_agc_pixel_code_start,
                   hdr ? native_agc_pixel_hdr_code_end : native_agc_pixel_code_end) != 0 ||
        copy_asset(presenter.shader_memory + 0xc000, 0x1000, native_agc_resources_start,
                   native_agc_resources_end) != 0 ||
        (hdr && copy_asset(presenter.shader_memory + HDR_HUD_SHADER_CODE_OFFSET, 0x1000,
                           native_agc_pixel_code_start, native_agc_pixel_code_end) != 0) ||
        (hdr && copy_asset(presenter.shader_memory + HDR_HUD_SHADER_HEADER_OFFSET, 0x1000,
                           native_agc_pixel_header_start, native_agc_pixel_header_end) != 0) ||
        (hdr && copy_asset(presenter.shader_memory + HDR_HUD_RESOURCES_OFFSET, 0x1000,
                           native_agc_resources_start, native_agc_resources_end) != 0) ||
        prepare_resources(presenter.shader_memory + 0xc000, hdr) != 0 ||
        (hdr && prepare_resources(presenter.shader_memory + HDR_HUD_RESOURCES_OFFSET, 0) != 0))
        return -3;

    result = sceAgcCreateShader(&presenter.vertex_shader, presenter.shader_memory,
                                presenter.shader_memory + 0x3700);
    if (result == 0)
        result = sceAgcCreateShader(&presenter.pixel_shader, presenter.shader_memory + 0x1000,
                                    presenter.shader_memory + 0x2000);
    if (result == 0 && hdr)
        result = sceAgcCreateShader(&presenter.hud_pixel_shader,
                                    presenter.shader_memory + HDR_HUD_SHADER_HEADER_OFFSET,
                                    presenter.shader_memory + HDR_HUD_SHADER_CODE_OFFSET);
    if (result == 0)
        link_result =
            sceAgcLinkShaders(presenter.shader_memory + 0x5000, presenter.shader_memory + 0x6000,
                              NULL, presenter.vertex_shader, presenter.pixel_shader, 6);
    snprintf(receipt, sizeof(receipt),
             "Native AGC stage 2: create=%08x link=%08x vs=%p ps=%p hud_ps=%p shader=%p",
             (uint32_t)result, (uint32_t)link_result, presenter.vertex_shader,
             presenter.pixel_shader, presenter.hud_pixel_shader,
             static_cast<void *>(presenter.shader_memory));
    report_agc_receipt(receipt);
    if (result != 0 || link_result != 0)
        return result ? result : link_result;

    flush_gpu_data(presenter.shader_memory, SHADER_STATIC_BYTES);

    presenter.video = sceVideoOutOpen(0xff, 0, 0, NULL);
    if (presenter.video >= 0)
        open_flip_events();
    if (presenter.video >= 0 && requested_fps > 60u)
        mode_result =
            configure_high_refresh_output(presenter.video, requested_fps, &mode_support_result,
                                          &mode_preset_result, &mode_vrr_result);
    if (presenter.video >= 0)
        result = sceVideoOutSetFlipRate(presenter.video, 0);
    else
        result = presenter.video;
    if (result == 0)
        result = sceKernelAllocateDirectMemory(0, direct_limit, framebuffer_pool_bytes,
                                               FRAMEBUFFER_ALIGNMENT, DIRECT_MEMORY_TYPE,
                                               &framebuffer_start);
    if (result == 0)
        presenter.framebuffer_start = framebuffer_start;
    if (result == 0)
        result =
            sceKernelMapDirectMemory(&presenter.framebuffer, framebuffer_pool_bytes, MAP_PROTECTION,
                                     0, framebuffer_start, FRAMEBUFFER_ALIGNMENT);
    snprintf(receipt, sizeof(receipt),
             "Native AGC stage 3: video=%08x mode_rc=%08x support_rc=%08x preset_rc=%08x "
             "vrr_rc=%08x "
             "framebuffer_rc=%08x "
             "framebuffer=%p/%zx output=%ux%u requested_fps=%u",
             (uint32_t)presenter.video, (uint32_t)mode_result, (uint32_t)mode_support_result,
             (uint32_t)mode_preset_result, (uint32_t)mode_vrr_result, (uint32_t)result,
             presenter.framebuffer, framebuffer_pool_bytes, output.width, output.height,
             requested_fps);
    report_agc_receipt(receipt);
    if (result != 0 || !presenter.framebuffer)
        return result ? result : -4;

    memset(presenter.framebuffer, 0, framebuffer_pool_bytes);
    flush_gpu_data(presenter.framebuffer, framebuffer_pool_bytes);
    {
        video_buffer_t buffers[2] = {
            {presenter.framebuffer, NULL, NULL, NULL},
            {(uint8_t *)presenter.framebuffer + output.framebuffer_bytes, NULL, NULL, NULL}};
        video_attribute_t attribute = {};

        sceVideoOutSetBufferAttribute2(
            &attribute, hdr ? VIDEO_OUT_PIXEL_FORMAT_HDR : VIDEO_OUT_PIXEL_FORMAT_SDR, 0,
            output.width, output.height, 0, 0, 0);
        result =
            sceVideoOutRegisterBuffers2(presenter.video, 0, 0, buffers, 2, &attribute, 0, NULL);
    }
    snprintf(receipt, sizeof(receipt),
             "Native AGC stage 4: register=%08x hdr=%u format=%016llx source=%p target=%p "
             "same_source=%u output=%ux%u",
             (uint32_t)result, hdr ? 1u : 0u,
             (unsigned long long)(hdr ? VIDEO_OUT_PIXEL_FORMAT_HDR : VIDEO_OUT_PIXEL_FORMAT_SDR),
             source, presenter.framebuffer, source == presenter.framebuffer, output.width,
             output.height);
    report_agc_receipt(receipt);
    if (result != 0)
        return result;

    update_presenter_output_status("registered");

    presenter.hdr = (uint8_t)(hdr != 0);
    presenter.ready = 1;
    return 0;
}

static int present_frame(const void *source, size_t source_bytes, uint32_t pitch,
                         uint32_t surface_height, uint32_t visible_width, uint32_t visible_height,
                         uint32_t requested_fps, const native_agc_metrics_t *metrics, int hdr,
                         uint32_t output_source_width, uint32_t output_source_height,
                         bool defer_flip)
{
    const NativeAgcOutputGeometry output =
        native_agc_output_geometry(output_source_width, output_source_height, requested_fps);
    uint64_t status[16] = {};
    uint32_t frame_number = presenter.frame_number;
    uint32_t buffer_index = frame_number & 1u;
    int64_t render_marker = INT64_C(0x4444) + ((int64_t)frame_number << 8);
    void *target;
    uint32_t words = 0;
    int draw_keyboard = std::atomic_load_explicit(&keyboard_enabled, std::memory_order_acquire);
    int draw_hud = !draw_keyboard && native_agc_hud_enabled() && metrics;
    int draw_overlay = draw_keyboard || draw_hud;
    uint32_t render_width;
    uint32_t render_height;
    uint32_t overlay_scale;
    uint32_t overlay_inset_x;
    uint32_t overlay_inset_y;
    uint32_t overlay_width = draw_keyboard ? KEYBOARD_WIDTH : HUD_WIDTH;
    uint32_t overlay_height = draw_keyboard ? KEYBOARD_HEIGHT : HUD_HEIGHT;
    uint32_t overlay_x;
    uint32_t overlay_y;
    float overlay_alpha = draw_keyboard ? .82f : .5f;
    unsigned render_waits;
    int32_t result;
    char receipt[512];

    // At most ONE native submission is outstanding. Retire it before rewriting
    // shared command/constant/overlay memory, even if a caller omitted finish.
    result = native_agc_finish_frame();
    if (result != 0)
        return result;

    if (presenter.ready && presenter.hdr != (uint8_t)(hdr != 0))
        return -6;
    if (presenter.ready &&
        (presenter.output_width != output.width || presenter.output_height != output.height))
        return -7;
    if (presenter.ready && presenter.requested_fps != requested_fps)
        return -8;
    if (!presenter.ready)
    {
        result = initialize_presenter(source, source_bytes, pitch, surface_height, visible_width,
                                      visible_height, requested_fps, hdr, output);
        if (result != 0)
            return result;
    }

    if (frame_number < 120u || frame_number % 120u == 0u)
        update_presenter_output_status(NULL);
    render_width = output.width;
    render_height = output.height;
    overlay_scale = render_width / BASE_OUTPUT_WIDTH;
    if (!overlay_scale)
        overlay_scale = 1u;
    overlay_inset_x = std::atomic_load_explicit(&tv_safe_area, std::memory_order_relaxed)
                          ? render_width * TV_SAFE_INSET_X / BASE_OUTPUT_WIDTH
                          : 0u;
    overlay_inset_y = std::atomic_load_explicit(&tv_safe_area, std::memory_order_relaxed)
                          ? render_height * TV_SAFE_INSET_Y / BASE_OUTPUT_HEIGHT
                          : 0u;
    overlay_x = draw_keyboard ? (render_width - KEYBOARD_WIDTH * overlay_scale) / 2u
                              : overlay_inset_x + HUD_X * overlay_scale;
    overlay_y =
        draw_keyboard ? KEYBOARD_Y * overlay_scale : overlay_inset_y + HUD_Y * overlay_scale;

    target = (uint8_t *)presenter.framebuffer + buffer_index * presenter.framebuffer_bytes;
    if (draw_keyboard)
    {
        uint32_t generation =
            std::atomic_load_explicit(&keyboard_generation, std::memory_order_relaxed);

        if (presenter.overlay_kind != 2u || presenter.keyboard_generation != generation)
        {
            result = wait_for_marker(presenter.overlay_marker, NULL);
            if (result != 0)
                return result;
            const uint64_t overlay_started = moonlight::performance_now_us();
            refresh_keyboard_surface(
                presenter.shader_memory + HUD_SURFACE_OFFSET,
                std::atomic_load_explicit(&keyboard_selected, std::memory_order_relaxed),
                std::atomic_load_explicit(&keyboard_shifted, std::memory_order_relaxed), hdr);
            flush_gpu_data(presenter.shader_memory + HUD_SURFACE_OFFSET, KEYBOARD_SURFACE_BYTES);
            moonlight::record_performance_elapsed(agc_performance.overlay, overlay_started);
            presenter.keyboard_generation = generation;
        }
    }
    else if (draw_hud)
    {
        if (presenter.overlay_kind != 1u || frame_number % HUD_REFRESH_FRAMES == 0)
        {
            result = wait_for_marker(presenter.overlay_marker, NULL);
            if (result != 0)
                return result;
            const uint64_t overlay_started = moonlight::performance_now_us();
            refresh_hud_surface(presenter.shader_memory + HUD_SURFACE_OFFSET, metrics,
                                visible_width, visible_height, render_width, render_height,
                                presenter.scanout_refresh_x100, hdr);
            flush_gpu_data(presenter.shader_memory + HUD_SURFACE_OFFSET, HUD_SURFACE_BYTES);
            moonlight::record_performance_elapsed(agc_performance.overlay, overlay_started);
        }
    }
    presenter.overlay_kind = draw_keyboard ? 2u : draw_hud ? 1u : 0u;
    // Loading frames always wait for vblank; only stream frames may tear.
    const uint32_t flip_mode = defer_flip ? effective_flip_mode() : VIDEO_OUT_FLIP_MODE_VSYNC;
    bool submitted = false;
    result = render_frame(presenter.video, (int)buffer_index, target, presenter.shader_memory,
                          presenter.vertex_shader, presenter.pixel_shader,
                          presenter.hud_pixel_shader, source, source_bytes, pitch, surface_height,
                          visible_width, visible_height, render_width, render_height, render_marker,
                          flip_mode, &submitted, &words, hdr, draw_overlay, overlay_width,
                          overlay_height, overlay_x, overlay_y, overlay_scale, overlay_alpha);
    if (result != 0 && !submitted && flip_mode == VIDEO_OUT_FLIP_MODE_HSYNC)
    {
        // Nothing reached the GPU: retire immediate flips for this process.
        std::atomic_store_explicit(&hsync_rejected, 1, std::memory_order_relaxed);
        ++agc_performance.vsync_fallbacks;
        snprintf(receipt, sizeof(receipt), "Native AGC immediate flip rejected: rc=%08x",
                 (uint32_t)result);
        report_agc_receipt(receipt);
        result = render_frame(presenter.video, (int)buffer_index, target, presenter.shader_memory,
                              presenter.vertex_shader, presenter.pixel_shader,
                              presenter.hud_pixel_shader, source, source_bytes, pitch,
                              surface_height, visible_width, visible_height, render_width,
                              render_height, render_marker, VIDEO_OUT_FLIP_MODE_VSYNC, &submitted,
                              &words, hdr, draw_overlay, overlay_width, overlay_height, overlay_x,
                              overlay_y, overlay_scale, overlay_alpha);
    }
    if (result == 0)
    {
        presenter.pending_source = source;
        presenter.pending_marker = render_marker;
        presenter.pending_flip_mode = effective_flip_mode() == VIDEO_OUT_FLIP_MODE_HSYNC
                                          ? flip_mode
                                          : VIDEO_OUT_FLIP_MODE_VSYNC;
        render_waits = 0;
        if (!defer_flip)
        {
            result = wait_for_marker(render_marker, &render_waits);
            if (result == 0)
            {
                presenter.pending_source = nullptr;
                presenter.pending_marker = 0;
            }
        }
        (void)sceVideoOutGetFlipStatus(presenter.video, status);
    }
    else
    {
        render_waits = 0;
    }

    if (result != 0 || frame_number == 0)
    {
        snprintf(receipt, sizeof(receipt),
                 "Native AGC frame: rc=%08x frame=%u buffer=%u words=%u hdr=%u hud=%u "
                 "flip_marker=%llx status=%llx waits=%u source=%p target=%p active=%ux%u@%u.%02u",
                 (uint32_t)result, frame_number, buffer_index, words, hdr ? 1u : 0u,
                 draw_overlay ? 1u : 0u, (unsigned long long)render_marker,
                 (unsigned long long)status[3], render_waits, source, target, render_width,
                 render_height, presenter.scanout_refresh_x100 / 100u,
                 presenter.scanout_refresh_x100 % 100u);
        report_agc_receipt(receipt);
    }

    if (result == 0)
        ++presenter.frame_number;
    return result;
}

int native_agc_present_nv12(const void *source, size_t source_bytes, uint32_t pitch,
                            uint32_t surface_height, uint32_t visible_width,
                            uint32_t visible_height, uint32_t requested_fps,
                            const native_agc_metrics_t *metrics)
{
    return present_frame(source, source_bytes, pitch, surface_height, visible_width, visible_height,
                         requested_fps, metrics, 0, visible_width, visible_height,
                         PROSPEROLIGHT_PRESENT_OVERLAP != 0);
}

int native_agc_present_main10(const void *source, size_t source_bytes, uint32_t pitch,
                              uint32_t surface_height, uint32_t visible_width,
                              uint32_t visible_height, uint32_t requested_fps,
                              const native_agc_metrics_t *metrics)
{
    return present_frame(source, source_bytes, pitch, surface_height, visible_width, visible_height,
                         requested_fps, metrics, 1, visible_width, visible_height,
                         PROSPEROLIGHT_PRESENT_OVERLAP != 0);
}

static void loading_set_luma(void *surface, uint32_t x, uint32_t y, uint16_t value, int hdr)
{
    size_t index;

    if (x >= LOADING_PITCH || y >= LOADING_VISIBLE_HEIGHT)
        return;
    index = (size_t)y * LOADING_PITCH + x;
    if (hdr)
        ((uint16_t *)surface)[index] = value;
    else
        ((uint8_t *)surface)[index] = (uint8_t)value;
}

static void loading_draw_disc(void *surface, int center_x, int center_y, int radius, uint16_t value,
                              int hdr)
{
    for (int y = -radius; y <= radius; ++y)
        for (int x = -radius; x <= radius; ++x)
            if (x * x + y * y <= radius * radius)
                loading_set_luma(surface, (uint32_t)(center_x + x), (uint32_t)(center_y + y), value,
                                 hdr);
}

static void loading_blend_luma(void *surface, uint32_t x, uint32_t y, uint16_t value, uint8_t alpha,
                               int hdr)
{
    size_t index;

    if (!alpha || x >= LOADING_PITCH || y >= LOADING_VISIBLE_HEIGHT)
        return;
    index = (size_t)y * LOADING_PITCH + x;
    if (hdr)
    {
        auto *samples = static_cast<uint16_t *>(surface);
        const int current = samples[index];
        samples[index] = (uint16_t)(current + ((int)value - current) * alpha / 255);
    }
    else
    {
        auto *samples = static_cast<uint8_t *>(surface);
        const int current = samples[index];
        samples[index] = (uint8_t)(current + ((int)value - current) * alpha / 255);
    }
}

static void loading_draw_label(void *surface, const uint8_t *mask, size_t mask_bytes,
                               uint32_t width, uint32_t height, uint32_t center_x, uint32_t y,
                               uint16_t value, int hdr)
{
    const uint32_t x = center_x - width / 2u;

    if (mask_bytes != (size_t)width * height)
        return;
    for (uint32_t row = 0; row < height; ++row)
        for (uint32_t column = 0; column < width; ++column)
            loading_blend_luma(surface, x + column, y + row, value,
                               mask[(size_t)row * width + column], hdr);
}

int native_agc_present_loading(void *surface, size_t surface_bytes, uint32_t phase, int hdr,
                               uint32_t output_source_width, uint32_t output_source_height,
                               uint32_t requested_fps)
{
    static const int dot_offsets[8][2] = {
        {0, -58}, {41, -41}, {58, 0}, {41, 41}, {0, 58}, {-41, 41}, {-58, 0}, {-41, -41},
    };
    const size_t sample_count = (size_t)LOADING_PITCH * LOADING_SURFACE_HEIGHT;
    const size_t required_bytes = hdr ? sample_count * 3u : sample_count * 3u / 2u;
    const uint16_t background = hdr ? 80u : 20u;
    const uint16_t neutral = hdr ? 512u : 128u;
    const uint16_t dim = hdr ? 180u : 64u;
    const uint16_t trail = hdr ? 440u : 142u;
    const uint16_t bright = hdr ? 760u : 235u;
    const uint16_t text = hdr ? 700u : 220u;
    const uint32_t active = phase & 7u;

    if (!surface || surface_bytes < required_bytes)
        return -1;

    {
        const int result = native_agc_wait_source_idle(surface);
        if (result != 0)
            return result;
    }

    if (hdr)
    {
        auto *samples = static_cast<uint16_t *>(surface);
        for (size_t index = 0; index < sample_count; ++index)
            samples[index] = background;
        for (size_t index = sample_count; index < sample_count + sample_count / 2u; ++index)
            samples[index] = neutral;
    }
    else
    {
        memset(surface, (int)background, sample_count);
        memset((uint8_t *)surface + sample_count, (int)neutral, sample_count / 2u);
    }

    for (uint32_t index = 0; index < 8u; ++index)
    {
        uint16_t value = dim;
        int radius = 8;

        if (index == active)
        {
            value = bright;
            radius = 13;
        }
        else if (index == ((active + 7u) & 7u))
        {
            value = trail;
            radius = 10;
        }
        loading_draw_disc(surface, 960 + dot_offsets[index][0], 432 + dot_offsets[index][1], radius,
                          value, hdr);
    }
    loading_draw_label(
        surface, native_loading_prosperolight_start,
        (size_t)(native_loading_prosperolight_end - native_loading_prosperolight_start), 232u, 35u,
        960u, 536u, text, hdr);
    loading_draw_label(surface, native_loading_connecting_start,
                       (size_t)(native_loading_connecting_end - native_loading_connecting_start),
                       287u, 52u, 960u, 586u, text, hdr);
    flush_gpu_data(surface, required_bytes);

    return present_frame(surface, surface_bytes, LOADING_PITCH, LOADING_SURFACE_HEIGHT,
                         LOADING_PITCH, LOADING_VISIBLE_HEIGHT, requested_fps, NULL, hdr,
                         output_source_width, output_source_height, false);
}

void native_agc_note_initialized(void)
{
    agc_initialized = 1;
}

int native_agc_present_shutdown(void)
{
    const int finish_result = native_agc_finish_frame();
    if (finish_result != 0)
        return finish_result; // Never unmap a still-owned native submission.
    unsigned drain_waits = 0;
    int32_t pending_result = 0;
    int32_t unregister_result = 0;
    int32_t restore_result = 0;
    int32_t close_result = 0;
    int32_t framebuffer_unmap_result = 0;
    int32_t framebuffer_release_result = 0;
    int32_t shader_unmap_result = 0;
    int32_t shader_release_result = 0;
    char receipt[512];

    if (presenter.video >= 0)
    {
        while (drain_waits < 120 &&
               (pending_result = sceVideoOutIsFlipPending(presenter.video)) > 0)
        {
            sceVideoOutWaitVblank(presenter.video);
            ++drain_waits;
        }
    }
    if (presenter.video >= 0 && presenter.ready)
        unregister_result = sceVideoOutUnregisterBuffers(presenter.video, 0);
    if (presenter.video >= 0 && presenter.requested_fps > 60u)
    {
        restore_result = configure_launcher_output(presenter.video);
        if (restore_result == 0)
        {
            (void)sceVideoOutWaitVblank(presenter.video);
            (void)sceVideoOutWaitVblank(presenter.video);
        }
    }
    close_flip_events();
    if (presenter.video >= 0)
        close_result = sceVideoOutClose(presenter.video);
    if (presenter.framebuffer)
        framebuffer_unmap_result =
            sceKernelMunmap(presenter.framebuffer, presenter.framebuffer_pool_bytes);
    if (presenter.framebuffer_start >= 0)
        framebuffer_release_result = sceKernelReleaseDirectMemory(presenter.framebuffer_start,
                                                                  presenter.framebuffer_pool_bytes);
    if (presenter.shader_memory)
        shader_unmap_result = sceKernelMunmap(presenter.shader_memory, SHADER_MEMORY_BYTES);
    if (presenter.shader_start >= 0)
        shader_release_result =
            sceKernelReleaseDirectMemory(presenter.shader_start, SHADER_MEMORY_BYTES);

    snprintf(receipt, sizeof(receipt),
             "Native AGC cleanup: pending=%08x waits=%u unregister=%08x restore=%08x close=%08x "
             "framebuffer=%08x/%08x shader=%08x/%08x",
             (uint32_t)pending_result, drain_waits, (uint32_t)unregister_result,
             (uint32_t)restore_result, (uint32_t)close_result, (uint32_t)framebuffer_unmap_result,
             (uint32_t)framebuffer_release_result, (uint32_t)shader_unmap_result,
             (uint32_t)shader_release_result);
    report_agc_receipt(receipt);

    memset(&presenter, 0, sizeof(presenter));
    presenter.shader_start = -1;
    presenter.framebuffer_start = -1;
    presenter.video = -1;

    if (pending_result != 0)
        return pending_result;
    if (unregister_result != 0 && (uint32_t)unregister_result != UINT32_C(0x80290009))
        return unregister_result;
    if (restore_result != 0)
        return restore_result;
    if (framebuffer_unmap_result != 0)
        return framebuffer_unmap_result;
    if (framebuffer_release_result != 0)
        return framebuffer_release_result;
    if (close_result != 0)
        return close_result;
    if (shader_unmap_result != 0)
        return shader_unmap_result;
    return shader_release_result;
}
