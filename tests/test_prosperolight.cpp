/*
 * ps5-native-app-boilerplate / ProsperoLight - Stream shortcut tests.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "lan_http_report.hpp"
#include "native_agc_output.hpp"
#include "moonlight_stream_input.hpp"
#include "moonlight_stream_keyboard.hpp"
#include "moonlight_config.hpp"
#include "moonlight_health.hpp"
#include "moonlight_physical_input.hpp"
#include "moonlight_performance.hpp"
#include "moonlight_tuning.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

TEST(Performance, SliceHeadersAreCountedWithoutReadingTruncatedNals)
{
    const uint8_t h264[] = {0, 0, 0, 1, 0x67, 0x42, 0, 0, 1, 0x65, 0x80, 0, 0, 1, 0x41, 0x80};
    EXPECT_EQ(moonlight::count_video_slices(h264, sizeof(h264), false), 2u);
    const uint8_t hevc[] = {0, 0, 1, 0x40, 1, 0, 0, 0, 1, 0x26, 1, 0x80, 0, 0, 1, 0x02, 1, 0};
    EXPECT_EQ(moonlight::count_video_slices(hevc, sizeof(hevc), true), 2u);
    EXPECT_EQ(moonlight::count_video_slices(nullptr, 99, true), 0u);
    EXPECT_EQ(moonlight::count_video_slices(hevc + 13, 3, true), 0u);
}

TEST(Performance, PacerBoundsWaitAndAbandonsBacklog)
{
    EXPECT_EQ(moonlight::input_poll_delay(100, 600, 2000), 1500u);
    EXPECT_EQ(moonlight::input_poll_delay(100, 5000, 2000), 100u);
    EXPECT_EQ(moonlight::input_poll_delay(100, 50, 2000), 2000u);
    moonlight::FramePacer p;
    EXPECT_EQ(p.delay(100000, 120, false), 0u);
    EXPECT_EQ(p.delay(104000, 120, false), 4333u);
    EXPECT_EQ(p.delay(105000, 120, true), 0u);
    EXPECT_EQ(p.delay(200000, 120, false), 0u);
    EXPECT_EQ(p.delay(201000, 0, false), 0u);
    // Clock discontinuity must not produce an unbounded sleep.
    EXPECT_EQ(p.delay(1, 120, false), 0u);
    p = {};
    EXPECT_EQ(p.delay(1000000, 120, false, 11988), 0u);
    EXPECT_EQ(p.delay(1004000, 120, false, 11988), 4341u);
    // Fractional intervals do not lose a microsecond on each frame.
    p = {};
    uint64_t now = 1000000;
    for (unsigned i = 0; i < 11988; ++i)
    {
        EXPECT_EQ(p.delay(now, 120, false, 11988), 0u);
        now = p.next_us;
    }
    EXPECT_EQ(now, 101000000u);
}

TEST(Performance, BoundedTimingPercentilesIncludeOverflow)
{
    moonlight::TimingHistogram timing;
    EXPECT_EQ(timing.percentile(95), 0u);
    for (unsigned i = 1; i <= 100; ++i)
        timing.add(i * 1000u);
    EXPECT_EQ(timing.count, 100u);
    EXPECT_EQ(timing.total_us, 5050000u);
    EXPECT_EQ(timing.percentile(95), 95499u);
    EXPECT_EQ(timing.percentile(99), 99499u);
    timing.add(3000000u);
    EXPECT_EQ(timing.percentile(100), 3000000u);
}

TEST(Performance, ExtraTimingIsDisabledInOrdinaryBuilds)
{
    moonlight::TimingHistogram timing;
    EXPECT_EQ(moonlight::performance_now_us(), 0u);
    moonlight::record_performance_elapsed(timing, 1u);
    EXPECT_EQ(timing.count, 0u);
}

TEST(Performance, RateWindowReflectsRecentSlowdownAndCounterReset)
{
    moonlight::RateWindow rate;
    EXPECT_FALSE(rate.update(0, 0));
    EXPECT_FALSE(rate.update(500000, 60));
    EXPECT_TRUE(rate.update(1000000, 120));
    EXPECT_EQ(rate.fps_x100, 12000u);
    EXPECT_TRUE(rate.update(2000000, 180));
    EXPECT_EQ(rate.fps_x100, 6000u);
    EXPECT_TRUE(rate.update(4000000, 180));
    EXPECT_EQ(rate.fps_x100, 0u);
    EXPECT_FALSE(rate.update(4000001, 0));
}

TEST(Performance, ReassemblyUsesOnlyOrderedLocalTimestamps)
{
    moonlight::TimingHistogram timing;
    EXPECT_TRUE(moonlight::record_reassembly(timing, 1000, 3500, 9000));
    EXPECT_TRUE(moonlight::record_reassembly(timing, 3500, 3500, 3500));
    EXPECT_FALSE(moonlight::record_reassembly(timing, 0, 3500, 9000));
    EXPECT_FALSE(moonlight::record_reassembly(timing, 1000, 0, 9000));
    EXPECT_FALSE(moonlight::record_reassembly(timing, 3501, 3500, 9000));
    EXPECT_FALSE(moonlight::record_reassembly(timing, 1000, 9001, 9000));
    EXPECT_EQ(timing.count, 2u);
    EXPECT_EQ(timing.total_us, 2500u);
    EXPECT_EQ(timing.max_us, 2500u);
}

TEST(Performance, RefreshHintPreservesSelectedRateAndFractionalCadence)
{
    // Rows are requested FPS; columns cover unknown, integral and fractional outputs.
    constexpr uint32_t outputs[] = {0, 5994, 6000, 8991, 9000, 11988, 12000, UINT32_MAX};
    constexpr uint32_t rates[] = {60, 90, 120};
    constexpr uint32_t expected[][8] = {
        {6000, 5994, 6000, 6000, 6000, 5994, 6000, 6000},
        {9000, 9000, 9000, 8991, 9000, 9000, 9000, 9000},
        {12000, 12000, 12000, 12000, 12000, 11988, 12000, 12000},
    };
    for (unsigned row = 0; row < 3; ++row)
        for (unsigned column = 0; column < 8; ++column)
            EXPECT_EQ(moonlight::client_refresh_x100(rates[row], outputs[column]),
                      expected[row][column])
                << rates[row] << " FPS on " << outputs[column];
    EXPECT_EQ(moonlight::client_refresh_x100(0, 0), 6000u);
    EXPECT_EQ(moonlight::client_refresh_x100(UINT32_MAX, UINT32_MAX), 6000u);
}

TEST(Performance, CatchUpPreservesVideoProgressAndAudioThreshold)
{
    EXPECT_TRUE(moonlight::drop_stale_presentation(30000, 120, 2, 8333));
    EXPECT_FALSE(moonlight::drop_stale_presentation(30000, 120, 0, 8333));
    EXPECT_FALSE(moonlight::drop_stale_presentation(30000, 120, 2, 100000));
    EXPECT_FALSE(moonlight::drop_stale_presentation(16000, 120, 2, 8333));
    EXPECT_FALSE(moonlight::discard_audio_backlog(31, 0));
    EXPECT_FALSE(moonlight::discard_audio_backlog(30, 30));
    EXPECT_FALSE(moonlight::discard_audio_backlog(-1, 30));
    EXPECT_TRUE(moonlight::discard_audio_backlog(31, 30));
}

namespace
{
const std::uint8_t *kernel_read_data;
std::size_t kernel_read_size;
std::size_t kernel_read_offset;
} // namespace

extern "C"
{
    int sceKernelOpen(const char *, int flags, std::uint16_t)
    {
        kernel_read_offset = 0;
        return flags == 0 && kernel_read_data ? 1 : -1;
    }

    int sceKernelClose(int)
    {
        return 0;
    }

    std::int64_t sceKernelRead(int, void *buffer, std::size_t length)
    {
        const std::size_t remaining = kernel_read_size - kernel_read_offset;
        const std::size_t count = length < remaining ? length : remaining;

        if (count)
            std::memcpy(buffer, kernel_read_data + kernel_read_offset, count);
        kernel_read_offset += count;
        return static_cast<std::int64_t>(count);
    }

    std::int64_t sceKernelWrite(int, const void *, std::size_t)
    {
        return -1;
    }

    int sceKernelRename(const char *, const char *)
    {
        return -1;
    }

    int sceKernelUnlink(const char *)
    {
        return -1;
    }
}

namespace
{
TEST(LanTelemetry, NormalBuildIsAnImmediateNoOp)
{
    lan_http_report_set_host("203.0.113.1");
    EXPECT_EQ(lan_http_report_text("must not open a socket"), 0);
}

TEST(VideoOutput, Uses1080pFor1080pSources)
{
    const auto output = native_agc_output_geometry(1920u, 1080u);

    EXPECT_EQ(output.width, 1920u);
    EXPECT_EQ(output.height, 1080u);
    EXPECT_EQ(output.framebuffer_bytes, 0x0a00000u);
}

TEST(VideoOutput, Uses4kFor1440pAnd2160pSources)
{
    const auto output_1440p = native_agc_output_geometry(2560u, 1440u);
    const auto output_2160p = native_agc_output_geometry(3840u, 2160u);

    EXPECT_EQ(output_1440p.width, 3840u);
    EXPECT_EQ(output_1440p.height, 2160u);
    EXPECT_EQ(output_1440p.framebuffer_bytes, 0x2000000u);
    EXPECT_EQ(output_2160p.width, 3840u);
    EXPECT_EQ(output_2160p.height, 2160u);
    EXPECT_EQ(output_2160p.framebuffer_bytes, 0x2000000u);
}

TEST(VideoOutput, Preserves4kOutputForHighResolutionHfrStreams)
{
    const auto output_1440p = native_agc_output_geometry(2560u, 1440u, 90u);
    const auto output_2160p = native_agc_output_geometry(3840u, 2160u, 120u);

    EXPECT_EQ(output_1440p.width, 3840u);
    EXPECT_EQ(output_1440p.height, 2160u);
    EXPECT_EQ(output_1440p.framebuffer_bytes, 0x2000000u);
    EXPECT_EQ(output_2160p.width, 3840u);
    EXPECT_EQ(output_2160p.height, 2160u);
    EXPECT_EQ(output_2160p.framebuffer_bytes, 0x2000000u);
}

TEST(VideoOutput, UsesHardwareValidatedBilinearSampler)
{
    EXPECT_EQ(kNativeAgcBilinearSamplerWord, 0x09500000u);
    EXPECT_NE(kNativeAgcBilinearSamplerWord, 0x08000000u);
}

TEST(StreamShortcuts, SelectL1ReturnsToLauncher)
{
    EXPECT_TRUE(
        moonlight_stream_disconnect_requested(MOONLIGHT_PS5_PAD_SELECT | MOONLIGHT_PS5_PAD_L1));
    EXPECT_FALSE(moonlight_stream_disconnect_requested(MOONLIGHT_PS5_PAD_SELECT));
}

TEST(StreamShortcuts, SelectR1TogglesMetrics)
{
    EXPECT_TRUE(
        moonlight_stream_hud_toggle_requested(MOONLIGHT_PS5_PAD_SELECT | MOONLIGHT_PS5_PAD_R1));
    EXPECT_FALSE(moonlight_stream_hud_toggle_requested(MOONLIGHT_PS5_PAD_R1));
}

TEST(StreamShortcuts, SelectTriangleTogglesStreamKeyboard)
{
    EXPECT_TRUE(
        moonlight_stream_keyboard_requested(MOONLIGHT_PS5_PAD_SELECT | MOONLIGHT_PS5_PAD_TRIANGLE));
    EXPECT_FALSE(moonlight_stream_keyboard_requested(MOONLIGHT_PS5_PAD_TRIANGLE));
}

TEST(StreamShortcuts, SelectSquareTogglesMouseMode)
{
    EXPECT_TRUE(moonlight_stream_mouse_toggle_requested(MOONLIGHT_PS5_PAD_SELECT |
                                                        MOONLIGHT_PS5_PAD_SQUARE));
    EXPECT_FALSE(moonlight_stream_mouse_toggle_requested(MOONLIGHT_PS5_PAD_SQUARE));
    EXPECT_FALSE(moonlight_stream_mouse_toggle_requested(MOONLIGHT_PS5_PAD_SELECT));
}

TEST(StreamShortcuts, MouseAxisHasDeadzoneAndDirection)
{
    EXPECT_EQ(moonlight_stream_mouse_axis_delta(0), 0);
    EXPECT_GT(moonlight_stream_mouse_axis_delta(32766), 0);
    EXPECT_LT(moonlight_stream_mouse_axis_delta(-32766), 0);
}

TEST(PhysicalInput, MapsUsbKeyboardLikeMoonlightQt)
{
    using prosperolight::physical_input::MapKey;

    EXPECT_EQ(MapKey(4).virtual_key, 0x41u);
    EXPECT_EQ(MapKey(29).virtual_key, 0x5au);
    EXPECT_EQ(MapKey(30).virtual_key, 0x31u);
    EXPECT_EQ(MapKey(39).virtual_key, 0x30u);
    EXPECT_EQ(MapKey(40).virtual_key, 0x0du);
    EXPECT_EQ(MapKey(80).virtual_key, 0x25u);
    EXPECT_EQ(MapKey(58).virtual_key, 0x70u);
    EXPECT_EQ(MapKey(69).virtual_key, 0x7bu);
    EXPECT_EQ(MapKey(100).virtual_key, 0xe2u);
    EXPECT_EQ(MapKey(100).flags, prosperolight::physical_input::kNonNormalized);
    EXPECT_FALSE(static_cast<bool>(MapKey(0)));
}

TEST(PhysicalInput, ConvertsBothSidesOfEveryModifier)
{
    using namespace prosperolight::physical_input;

    EXPECT_EQ(MoonlightModifiers(kLeftShift | kRightShift), kModifierShift);
    EXPECT_EQ(MoonlightModifiers(kRightControl | kLeftAlt | kRightMeta),
              kModifierControl | kModifierAlt | kModifierMeta);
    EXPECT_EQ(MapKey(ModifierUsage(kLeftControl)).virtual_key, 0xa2u);
    EXPECT_EQ(MapKey(ModifierUsage(kRightMeta)).virtual_key, 0x5cu);
}

TEST(PhysicalInput, ClampsNativeMouseRangesForMoonlightPackets)
{
    using namespace prosperolight::physical_input;

    EXPECT_EQ(ClampMotion(40000), INT16_MAX);
    EXPECT_EQ(ClampMotion(-40000), INT16_MIN);
    EXPECT_EQ(ClampMotion(12), 12);
    EXPECT_EQ(ClampScroll(200), INT8_MAX);
    EXPECT_EQ(ClampScroll(-200), -INT8_MAX);
}

TEST(StreamKeyboard, NavigationWrapsAndPreservesAValidColumn)
{
    EXPECT_EQ(moonlight_keyboard_move(0, -1, 0), 12u);
    EXPECT_EQ(moonlight_keyboard_move(12, 1, 0), 0u);
    EXPECT_EQ(moonlight_keyboard_move(12, 0, 1), 25u);
    EXPECT_EQ(moonlight_keyboard_move(51, 0, 1), 4u);
}

TEST(StreamKeyboard, ShiftedLabelsMatchTheSentVirtualKeys)
{
    EXPECT_STREQ(moonlight_keyboard_label(0, false), "1");
    EXPECT_STREQ(moonlight_keyboard_label(0, true), "!");
    EXPECT_STREQ(moonlight_keyboard_label(12, false), "`");
    EXPECT_STREQ(moonlight_keyboard_label(12, true), "~");
    EXPECT_STREQ(moonlight_keyboard_label(13, false), "q");
    EXPECT_STREQ(moonlight_keyboard_label(13, true), "Q");
    EXPECT_EQ(moonlight_keyboard_keys[13].virtual_key, 0x51u);
    EXPECT_EQ(moonlight_keyboard_keys[47].action, moonlight_keyboard_action::shift);
}

TEST(StreamKeyboard, CoversEveryPrintableUsAsciiPasswordCharacter)
{
    for (int character = 0x20; character <= 0x7e; ++character)
    {
        bool found = character == ' ';

        for (uint32_t index = 0; !found && index < moonlight_keyboard_key_count; ++index)
        {
            const char *normal = moonlight_keyboard_keys[index].normal;
            const char *shifted = moonlight_keyboard_keys[index].shifted;

            found = (normal[0] == character && normal[1] == '\0') ||
                    (shifted[0] == character && shifted[1] == '\0');
        }
        EXPECT_TRUE(found) << "Missing printable ASCII character " << character;
    }
}

TEST(Configuration, DefaultsMatchLauncherDefaults)
{
    moonlight_config_t config{};
    moonlight_config_defaults(&config);

    EXPECT_EQ(config.host_count, 0U);
    EXPECT_EQ(config.bitrate_mbps, 20U);
    EXPECT_EQ(config.display_area, MOONLIGHT_DISPLAY_AREA_FULL);
    EXPECT_EQ(config.video_codec, MOONLIGHT_VIDEO_CODEC_H264);
    EXPECT_EQ(config.stream_resolution, MOONLIGHT_STREAM_RESOLUTION_1080P);
    EXPECT_EQ(config.stream_fps, MOONLIGHT_STREAM_FPS_60);
    EXPECT_EQ(config.hdr_enabled, 0U);
    EXPECT_EQ(config.audio_configuration, MOONLIGHT_AUDIO_STEREO);
}

TEST(Configuration, MigratesVersionFourAndDefaultsToStereo)
{
    struct LegacyConfig
    {
        std::uint32_t host_count;
        std::uint32_t selected_host;
        std::uint32_t bitrate_mbps;
        std::uint32_t display_area;
        std::uint32_t video_codec;
        std::uint32_t stream_resolution;
        std::uint32_t stream_fps;
        std::uint32_t hdr_enabled;
        moonlight_config_host_t hosts[MOONLIGHT_CONFIG_MAX_HOSTS];
    };
    struct LegacyFile
    {
        std::uint32_t magic;
        std::uint32_t version;
        std::uint32_t checksum;
        std::uint32_t reserved;
        LegacyConfig config;
    } file{};
    auto checksum = [](const void *data, std::size_t size)
    {
        const auto *bytes = static_cast<const std::uint8_t *>(data);
        std::uint32_t value = UINT32_C(2166136261);
        for (std::size_t index = 0; index < size; ++index)
            value = (value ^ bytes[index]) * UINT32_C(16777619);
        return value;
    };

    file.magic = UINT32_C(0x504c4346);
    file.version = 4;
    file.config.host_count = 1;
    file.config.bitrate_mbps = 80;
    file.config.display_area = MOONLIGHT_DISPLAY_AREA_FULL;
    file.config.video_codec = MOONLIGHT_VIDEO_CODEC_HEVC;
    file.config.stream_resolution = MOONLIGHT_STREAM_RESOLUTION_2160P;
    file.config.stream_fps = MOONLIGHT_STREAM_FPS_120;
    std::snprintf(file.config.hosts[0].address, sizeof(file.config.hosts[0].address),
                  "192.168.4.20");
    file.checksum = checksum(&file.config, sizeof(file.config));
    kernel_read_data = reinterpret_cast<const std::uint8_t *>(&file);
    kernel_read_size = sizeof(file);

    moonlight_config_t config{};
    const bool loaded = moonlight_config_load(&config);
    kernel_read_data = nullptr;
    kernel_read_size = 0;

    EXPECT_TRUE(loaded);
    EXPECT_EQ(config.stream_resolution, MOONLIGHT_STREAM_RESOLUTION_2160P);
    EXPECT_EQ(config.stream_fps, MOONLIGHT_STREAM_FPS_120);
    EXPECT_EQ(config.audio_configuration, MOONLIGHT_AUDIO_STEREO);
}

TEST(Configuration, MigratesVersionThreeAndKeepsTheSavedHost)
{
    struct LegacyConfig
    {
        std::uint32_t host_count;
        std::uint32_t selected_host;
        std::uint32_t bitrate_mbps;
        std::uint32_t display_area;
        std::uint32_t video_codec;
        std::uint32_t stream_resolution;
        std::uint32_t hdr_enabled;
        moonlight_config_host_t hosts[MOONLIGHT_CONFIG_MAX_HOSTS];
    };
    struct LegacyFile
    {
        std::uint32_t magic;
        std::uint32_t version;
        std::uint32_t checksum;
        std::uint32_t reserved;
        LegacyConfig config;
    } file{};
    auto checksum = [](const void *data, std::size_t size)
    {
        const auto *bytes = static_cast<const std::uint8_t *>(data);
        std::uint32_t value = UINT32_C(2166136261);
        for (std::size_t index = 0; index < size; ++index)
            value = (value ^ bytes[index]) * UINT32_C(16777619);
        return value;
    };

    file.magic = UINT32_C(0x504c4346);
    file.version = 3;
    file.config.host_count = 1;
    file.config.bitrate_mbps = 100;
    file.config.display_area = MOONLIGHT_DISPLAY_AREA_FULL;
    file.config.video_codec = MOONLIGHT_VIDEO_CODEC_HEVC;
    file.config.stream_resolution = MOONLIGHT_STREAM_RESOLUTION_1440P;
    file.config.hdr_enabled = 1;
    std::snprintf(file.config.hosts[0].address, sizeof(file.config.hosts[0].address),
                  "192.168.4.20");
    std::snprintf(file.config.hosts[0].name, sizeof(file.config.hosts[0].name), "Gaming-PC");
    file.checksum = checksum(&file.config, sizeof(file.config));
    kernel_read_data = reinterpret_cast<const std::uint8_t *>(&file);
    kernel_read_size = sizeof(file);

    moonlight_config_t config{};
    const bool loaded = moonlight_config_load(&config);
    kernel_read_data = nullptr;
    kernel_read_size = 0;

    EXPECT_TRUE(loaded);
    EXPECT_EQ(config.host_count, 1U);
    EXPECT_STREQ(config.hosts[0].address, "192.168.4.20");
    EXPECT_STREQ(config.hosts[0].name, "Gaming-PC");
    EXPECT_EQ(config.stream_fps, MOONLIGHT_STREAM_FPS_60);
    EXPECT_EQ(config.stream_resolution, MOONLIGHT_STREAM_RESOLUTION_1440P);
    EXPECT_EQ(config.bitrate_mbps, 100U);
    EXPECT_EQ(config.hdr_enabled, 1U);
    EXPECT_EQ(config.audio_configuration, MOONLIGHT_AUDIO_STEREO);
}

TEST(Configuration, UpsertUpdatesAHostByStableIdentity)
{
    moonlight_config_t config{};
    moonlight_config_defaults(&config);

    EXPECT_EQ(moonlight_config_upsert_host(&config, "192.168.1.10", "Gaming PC", "host-1", false),
              0);
    EXPECT_EQ(moonlight_config_upsert_host(&config, "192.168.1.20", "", "host-1", true), 0);
    ASSERT_EQ(config.host_count, 1U);
    EXPECT_STREQ(config.hosts[0].address, "192.168.1.20");
    EXPECT_STREQ(config.hosts[0].name, "Gaming PC");
    EXPECT_STREQ(config.hosts[0].unique_id, "host-1");
    EXPECT_EQ(config.hosts[0].manual, 1U);
}

TEST(Configuration, UpsertRejectsAHostBeyondCapacity)
{
    moonlight_config_t config{};
    moonlight_config_defaults(&config);
    for (std::uint32_t index = 0; index < MOONLIGHT_CONFIG_MAX_HOSTS; ++index)
    {
        char address[MOONLIGHT_CONFIG_ADDRESS_SIZE]{};
        std::snprintf(address, sizeof(address), "192.168.1.%u", index + 1);
        ASSERT_EQ(moonlight_config_upsert_host(&config, address, "PC", "", false),
                  static_cast<int>(index));
    }

    EXPECT_EQ(moonlight_config_upsert_host(&config, "192.168.1.99", "Extra", "", false), -1);
    EXPECT_EQ(config.host_count, MOONLIGHT_CONFIG_MAX_HOSTS);
}

TEST(Configuration, RemovingSelectedHostCompactsAndKeepsTheOtherPC)
{
    moonlight_config_t config{};
    moonlight_config_defaults(&config);
    ASSERT_EQ(moonlight_config_upsert_host(&config, "192.168.1.10", "Old PC", "old", false), 0);
    ASSERT_EQ(moonlight_config_upsert_host(&config, "192.168.1.20", "Other PC", "other", true), 1);
    config.selected_host = 0;

    EXPECT_FALSE(moonlight_config_remove_host(&config, 2));
    EXPECT_EQ(config.host_count, 2U);
    ASSERT_TRUE(moonlight_config_remove_host(&config, config.selected_host));
    ASSERT_EQ(config.host_count, 1U);
    EXPECT_EQ(config.selected_host, 0U);
    EXPECT_STREQ(config.hosts[0].address, "192.168.1.20");
    EXPECT_EQ(config.hosts[0].manual, 1U);
    EXPECT_EQ(config.hosts[1].address[0], '\0');
    ASSERT_TRUE(moonlight_config_remove_host(&config, 0));
    EXPECT_EQ(config.host_count, 0U);
    EXPECT_EQ(config.selected_host, 0U);
}

TEST(Configuration, FailedLoadLeavesSafeDefaults)
{
    moonlight_config_t config{};
    std::memset(&config, 0xff, sizeof(config));

    EXPECT_FALSE(moonlight_config_load(&config));
    EXPECT_EQ(config.host_count, 0U);
    EXPECT_EQ(config.bitrate_mbps, 20U);
    EXPECT_EQ(config.display_area, MOONLIGHT_DISPLAY_AREA_FULL);
}

TEST(HostHealth, DebouncesTransientFailuresAndRecovers)
{
    MoonlightHealthState health{};

    EXPECT_EQ(health.Record(false), MoonlightHealthState::recovery_delay_ms);
    EXPECT_TRUE(health.Reconnecting());
    EXPECT_EQ(health.Record(false), MoonlightHealthState::recovery_delay_ms);
    EXPECT_TRUE(health.Reconnecting());
    EXPECT_EQ(health.Record(false), MoonlightHealthState::steady_delay_ms);
    EXPECT_FALSE(health.Reconnecting());
    EXPECT_EQ(health.consecutive_failures, MoonlightHealthState::offline_failure_threshold);

    EXPECT_EQ(health.Record(true), MoonlightHealthState::steady_delay_ms);
    EXPECT_EQ(health.consecutive_failures, 0U);
    EXPECT_FALSE(health.Reconnecting());
}
} // namespace
