/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "launcher/launcher.hpp"
#include "moonlight_stream.hpp"
#include "native_agc_present.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <pthread.h>

extern "C" int sceKernelUsleep(std::uint32_t microseconds);
extern "C" int sceKernelDebugOutText(int channel, const char *text);
extern "C" std::int64_t sceKernelGetDirectMemorySize(void);
extern "C" int sceKernelAllocateDirectMemory(std::int64_t search_start, std::int64_t search_end,
                                             std::size_t length, std::size_t alignment,
                                             int memory_type, std::int64_t *direct_memory_start);
extern "C" int sceKernelMapDirectMemory(void **address, std::size_t length, int protection,
                                        int flags, std::int64_t direct_memory_start,
                                        std::size_t alignment);
extern "C" int sceKernelReleaseDirectMemory(std::int64_t direct_memory_start, std::size_t length);
extern "C" int sceSysmoduleLoadModule(std::uint16_t module_id);
extern "C" int munmap(void *address, std::size_t length);
extern "C" void *__dso_handle = nullptr;

#ifndef PROSPEROLIGHT_VIDEO_OUTPUT_SELF_TEST_FPS
#define PROSPEROLIGHT_VIDEO_OUTPUT_SELF_TEST_FPS 0
#endif
// How long the display is left alone after a stream above 60 Hz or in HDR,
// before the launcher opens it again: the television is switching back.
#ifndef PROSPEROLIGHT_HFR_SETTLE_MS
#define PROSPEROLIGHT_HFR_SETTLE_MS 5000
#endif

static_assert(PROSPEROLIGHT_VIDEO_OUTPUT_SELF_TEST_FPS == 0 ||
                  PROSPEROLIGHT_VIDEO_OUTPUT_SELF_TEST_FPS == 90 ||
                  PROSPEROLIGHT_VIDEO_OUTPUT_SELF_TEST_FPS == 120,
              "VIDEO_OUTPUT_SELF_TEST_FPS must be 0, 90, or 120");

extern "C" int pthread_once(pthread_once_t *once_control, void (*init_routine)(void))
{
    constexpr int running = 2;
    int state = __atomic_load_n(&once_control->state, __ATOMIC_ACQUIRE);
    if (state == PTHREAD_DONE_INIT)
        return 0;

    int expected = PTHREAD_NEEDS_INIT;
    if (__atomic_compare_exchange_n(&once_control->state, &expected, running, false,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    {
        init_routine();
        __atomic_store_n(&once_control->state, PTHREAD_DONE_INIT, __ATOMIC_RELEASE);
        return 0;
    }

    while (__atomic_load_n(&once_control->state, __ATOMIC_ACQUIRE) != PTHREAD_DONE_INIT)
    {
        sceKernelUsleep(100);
    }
    return 0;
}

extern "C" float strtof(const char *value, char **end)
{
    return static_cast<float>(strtod(value, end));
}

extern "C" int fseek(std::FILE *file, long offset, int origin)
{
    return fseeko(file, offset, origin);
}

extern "C" long ftell(std::FILE *file)
{
    return static_cast<long>(ftello(file));
}

extern "C" char *strcasestr(const char *haystack, const char *needle)
{
    if (!*needle)
        return const_cast<char *>(haystack);
    for (; *haystack; ++haystack)
    {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n)
        {
            const char hc = *h >= 'A' && *h <= 'Z' ? static_cast<char>(*h + ('a' - 'A')) : *h;
            const char nc = *n >= 'A' && *n <= 'Z' ? static_cast<char>(*n + ('a' - 'A')) : *n;
            if (hc != nc)
                break;
            ++h;
            ++n;
        }
        if (!*n)
            return const_cast<char *>(haystack);
    }
    return nullptr;
}

namespace
{

constexpr std::uint16_t kPngDecModule = 0x008c;

[[noreturn]] void KeepProcessAlive()
{
    for (;;)
        sceKernelUsleep(1000000);
}

[[maybe_unused]] std::uint64_t MonotonicMicroseconds()
{
    timespec now{};
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<std::uint64_t>(now.tv_sec) * 1000000u +
           static_cast<std::uint64_t>(now.tv_nsec) / 1000u;
}

void Log(const char *text)
{
    std::printf("%s\n", text);
    char line[200];
    std::snprintf(line, sizeof(line), "%s\n", text);
    (void)sceKernelDebugOutText(0, line);
}

#if PROSPEROLIGHT_VIDEO_OUTPUT_SELF_TEST_FPS != 0
void RunVideoOutputSelfTest()
{
    constexpr std::uint32_t pitch =
        PROSPEROLIGHT_STREAM_SELF_TEST_RESOLUTION == MOONLIGHT_STREAM_RESOLUTION_2160P   ? 3840
        : PROSPEROLIGHT_STREAM_SELF_TEST_RESOLUTION == MOONLIGHT_STREAM_RESOLUTION_1440P ? 2560
                                                                                         : 1920;
    constexpr std::uint32_t visible_height =
        PROSPEROLIGHT_STREAM_SELF_TEST_RESOLUTION == MOONLIGHT_STREAM_RESOLUTION_2160P   ? 2160
        : PROSPEROLIGHT_STREAM_SELF_TEST_RESOLUTION == MOONLIGHT_STREAM_RESOLUTION_1440P ? 1440
                                                                                         : 1080;
    constexpr std::uint32_t surface_height =
        PROSPEROLIGHT_STREAM_SELF_TEST_RESOLUTION == MOONLIGHT_STREAM_RESOLUTION_2160P ? 2176
        : visible_height == 1080                                                       ? 1088
                                 : visible_height;
    constexpr std::size_t visible_surface_bytes =
        static_cast<std::size_t>(pitch) * surface_height * 3 / 2;
    constexpr std::size_t surface_bytes = (visible_surface_bytes + 0x3fff) & ~std::size_t(0x3fff);
    constexpr std::uint32_t surface_count = 3;
    constexpr std::size_t surface_pool_bytes = surface_bytes * surface_count;
    constexpr std::uint32_t frame_count = PROSPEROLIGHT_VIDEO_OUTPUT_SELF_TEST_FPS * 5;
    std::int64_t surface_start = -1;
    void *surface = nullptr;
    (void)std::remove("/download0/prosperolight-agc-selftest.log");
    FILE *receipt = std::fopen("/download0/prosperolight-videoout-selftest.txt", "w");

    int result = sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(),
                                               surface_pool_bytes, 0x4000, 12, &surface_start);
    if (result == 0)
        result =
            sceKernelMapDirectMemory(&surface, surface_pool_bytes, 0x32, 0, surface_start, 0x4000);
    if (receipt)
    {
        std::fprintf(receipt, "requested=%u width=%u height=%u allocation_rc=%08x surface=%p\n",
                     PROSPEROLIGHT_VIDEO_OUTPUT_SELF_TEST_FPS, pitch, visible_height,
                     static_cast<unsigned>(result), surface);
        std::fflush(receipt);
    }
    if (result != 0 || !surface)
    {
        if (surface_start >= 0)
            (void)sceKernelReleaseDirectMemory(surface_start, surface_pool_bytes);
        KeepProcessAlive();
    }

    int present_result = 0;
    for (std::uint32_t index = 0; index < surface_count; ++index)
    {
        void *frame_surface = static_cast<std::uint8_t *>(surface) + index * surface_bytes;
        present_result =
            native_agc_present_loading(frame_surface, visible_surface_bytes, index, 0, pitch,
                                       visible_height, PROSPEROLIGHT_VIDEO_OUTPUT_SELF_TEST_FPS);
        if (present_result != 0)
            break;
    }

    const std::uint64_t frequency = 1000000;
    const std::uint64_t started = MonotonicMicroseconds();
    std::uint32_t presented = 0;
    for (std::uint32_t frame = 0; frame < frame_count && present_result == 0; ++frame)
    {
        void *frame_surface =
            static_cast<std::uint8_t *>(surface) + (frame % surface_count) * surface_bytes;
        present_result = native_agc_wait_source_idle(frame_surface);
        if (present_result == 0)
            present_result = native_agc_present_nv12(
                frame_surface, visible_surface_bytes, pitch, surface_height, pitch, visible_height,
                PROSPEROLIGHT_VIDEO_OUTPUT_SELF_TEST_FPS, nullptr);
        if (present_result != 0)
            break;
        ++presented;
        const std::uint64_t deadline = started + static_cast<std::uint64_t>(presented) * frequency /
                                                     PROSPEROLIGHT_VIDEO_OUTPUT_SELF_TEST_FPS;
        const std::uint64_t now = MonotonicMicroseconds();
        if (deadline > now)
        {
            const std::uint64_t remaining_us = (deadline - now) * 1000000 / frequency;
            if (remaining_us != 0)
                sceKernelUsleep(static_cast<std::uint32_t>(remaining_us));
        }
    }
    const std::uint64_t elapsed = MonotonicMicroseconds() - started;
    const std::uint64_t measured_fps_x100 =
        elapsed != 0 ? static_cast<std::uint64_t>(presented) * frequency * 100 / elapsed : 0;
    const int shutdown_result = native_agc_present_shutdown();
    const int unmap_result = munmap(surface, surface_pool_bytes);
    const int release_result = sceKernelReleaseDirectMemory(surface_start, surface_pool_bytes);
    if (receipt)
    {
        std::fprintf(receipt,
                     "presented=%u present_rc=%08x measured=%llu.%02llu shutdown_rc=%08x "
                     "unmap_rc=%08x release_rc=%08x\n",
                     presented, static_cast<unsigned>(present_result),
                     static_cast<unsigned long long>(measured_fps_x100 / 100),
                     static_cast<unsigned long long>(measured_fps_x100 % 100),
                     static_cast<unsigned>(shutdown_result), static_cast<unsigned>(unmap_result),
                     static_cast<unsigned>(release_result));
        std::fclose(receipt);
    }
    KeepProcessAlive();
}
#endif

} // namespace

int main()
{
#if PROSPEROLIGHT_VIDEO_OUTPUT_SELF_TEST_FPS != 0
    RunVideoOutputSelfTest();
#endif

    // Box art is decoded with the console's PNG decoder.
    (void)sceSysmoduleLoadModule(kPngDecModule);
    char stream_error[192]{};
    bool first_start = true;
    unsigned streams = 0;
    unsigned launcher_failures = 0;
    for (;;)
    {
        launcher::Selection selection;
        const launcher::Result result = launcher::Run(&selection, stream_error, first_start);
        first_start = false;
        stream_error[0] = '\0';
        if (result != launcher::Result::start_stream)
        {
            // The display can refuse while the television changes mode.
            if (++launcher_failures < 3)
            {
                Log("[PL] main: the launcher could not run; trying again");
                sceKernelUsleep(2000000);
                continue;
            }
            Log("[PL] main: the launcher could not run; waiting to be closed");
            KeepProcessAlive();
        }
        launcher_failures = 0;

        // Give the launcher's display teardown one final display interval
        // before the stream opens the display for itself.
        sceKernelUsleep(100000);
        moonlight_stream_options_t options{};
        moonlight_stream_metrics_t metrics{};
        options.host = selection.host;
        options.host_port = selection.host_port;
        options.app_name = selection.app_name;
        options.app_id = selection.app_id;
        options.bitrate_kbps = selection.bitrate_kbps;
        options.display_area = selection.display_area;
        options.video_codec = selection.video_codec;
        options.stream_resolution = selection.stream_resolution;
        options.stream_fps = selection.stream_fps;
        options.hdr_enabled = selection.hdr_enabled;
        options.audio_configuration = selection.audio_configuration;
        options.vsync_enabled = selection.vsync_enabled;
        options.decoder_pipeline = selection.decoder_pipeline;
        options.decoder_cores = selection.decoder_cores;
        ++streams;
        char line[160];
        std::snprintf(line, sizeof(line), "[PL] main: stream %u starts (%u FPS, hdr=%u)", streams,
                      selection.stream_fps, selection.hdr_enabled);
        Log(line);
        const int stream_result = moonlight_stream_run(&options, &metrics);
        std::snprintf(stream_error, sizeof(stream_error), "%s", metrics.error);
        const bool mode_changed =
            selection.stream_fps > MOONLIGHT_STREAM_FPS_60 || selection.hdr_enabled != 0;
        const unsigned settle_ms = mode_changed ? PROSPEROLIGHT_HFR_SETTLE_MS : 100;
        std::snprintf(line, sizeof(line),
                      "[PL] main: stream %u ended result=%d frames=%u; display settles %u ms",
                      streams, stream_result, metrics.presented_frames, settle_ms);
        Log(line);
        sceKernelUsleep(settle_ms * 1000u);
    }
}
