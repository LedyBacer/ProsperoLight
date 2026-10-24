/*
 * ps5-native-app-boilerplate / ProsperoLight - Stream shortcut tests.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "connecting_plate.hpp"
#include "lan_http_report.hpp"
#include "native_agc_output.hpp"
#include "moonlight_stream_input.hpp"
#include "moonlight_stream_keyboard.hpp"
#include "moonlight_config.hpp"
#include "moonlight_discovery.hpp"
#include "moonlight_health.hpp"
#include "moonlight_wol.hpp"
#include "moonlight_physical_input.hpp"
#include "moonlight_performance.hpp"
#include "moonlight_pipeline.hpp"
#include "moonlight_tuning.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

TEST(Performance, SliceHeadersAreCountedWithoutReadingTruncatedNals)
{
    const uint8_t h264[] = {0, 0, 0, 1, 0x67, 0x42, 0, 0, 1, 0x65, 0x80, 0, 0, 1, 0x41, 0x80};
    EXPECT_EQ(moonlight::count_video_slices(h264, sizeof(h264), false), 2u);
    const uint8_t hevc[] = {0, 0, 1, 0x40, 1, 0, 0, 0, 1, 0x26, 1, 0x80, 0, 0, 1, 0x02, 1, 0};
    EXPECT_EQ(moonlight::count_video_slices(hevc, sizeof(hevc), true), 2u);
    EXPECT_EQ(moonlight::count_video_slices(nullptr, 99, true), 0u);
    EXPECT_EQ(moonlight::count_video_slices(hevc + 13, 3, true), 0u);
}

TEST(Performance, InputPollSubtractsWorkAndYieldsAfterAnOverrun)
{
    EXPECT_EQ(moonlight::input_poll_delay(100, 600, 2000), 1500u);
    EXPECT_EQ(moonlight::input_poll_delay(100, 5000, 2000), 100u);
    EXPECT_EQ(moonlight::input_poll_delay(100, 50, 2000), 2000u);
}

TEST(Pipeline, DecoderMaskTakesWholeCoresAndLeavesRoomForTheStream)
{
    using namespace moonlight;
    EXPECT_EQ(decoder_cpu_mask(3, kTitleCpuMask), kClassicDecoderCpuMask);
    EXPECT_EQ(decoder_cpu_mask(4, kTitleCpuMask), 0xffu);
    EXPECT_EQ(decoder_cpu_mask(5, kTitleCpuMask), 0x3ffu);
    // Out-of-range requests are clamped, never widened past five cores.
    EXPECT_EQ(decoder_cpu_mask(0, kTitleCpuMask), kClassicDecoderCpuMask);
    EXPECT_EQ(decoder_cpu_mask(9, kTitleCpuMask), 0x3ffu);
    // A smaller allocation keeps two CPUs for receive, decode and presentation.
    EXPECT_EQ(decoder_cpu_mask(5, 0xffu), 0x3fu);
    EXPECT_EQ(decoder_cpu_mask(5, 0xfu), 0x3u);
    // A half-available core is skipped rather than split.
    EXPECT_EQ(decoder_cpu_mask(3, 0x1ffdu), 0xfcu);
}

TEST(Pipeline, ThreadLayoutKeepsStreamThreadsOffDecoderCpus)
{
    using namespace moonlight;
    for (unsigned cores = kMinDecoderCores; cores <= kMaxDecoderCores; ++cores)
    {
        const uint64_t decoder = decoder_cpu_mask(cores, kTitleCpuMask);
        const ThreadLayout layout = plan_thread_layout(kTitleCpuMask, decoder);
        for (uint64_t mask : {layout.receive, layout.decode, layout.present, layout.other})
        {
            EXPECT_NE(mask, 0u);
            EXPECT_EQ(mask & decoder, 0u);
            EXPECT_EQ(mask & ~kTitleCpuMask, 0u);
        }
        // The receive thread never shares its CPU with another stream thread.
        EXPECT_EQ(__builtin_popcountll(layout.receive), 1);
        EXPECT_EQ(layout.receive & (layout.decode | layout.present | layout.other), 0u);
    }
    const ThreadLayout five = plan_thread_layout(kTitleCpuMask, 0x3ffu);
    EXPECT_EQ(five.receive, 0x400u);
    EXPECT_EQ(five.decode, 0x1000u);
    EXPECT_EQ(five.present, 0x800u);
    EXPECT_EQ(five.other, 0x1800u);
    const ThreadLayout three = plan_thread_layout(kTitleCpuMask, kClassicDecoderCpuMask);
    EXPECT_EQ(three.receive, 0x40u);
    EXPECT_EQ(three.other, 0x1f80u);
    // One CPU left is shared; none left means "leave the inherited masks".
    const ThreadLayout single = plan_thread_layout(0x7u, 0x3u);
    EXPECT_EQ(single.receive, 0x4u);
    EXPECT_EQ(single.other, 0x4u);
    EXPECT_EQ(plan_thread_layout(0x3u, 0x3u).other, 0u);
}

TEST(Pipeline, DrainStartsOnlyWhenNothingIsQueued)
{
    using namespace moonlight;
    EXPECT_TRUE(should_drain(true, 3, 1, 0));
    EXPECT_FALSE(should_drain(true, 3, 1, 1)); // Behind: keep the pipeline full.
    EXPECT_FALSE(should_drain(true, 3, 0, 0)); // Nothing held.
    EXPECT_FALSE(should_drain(true, 1, 1, 0)); // Depth one never holds a picture.
    EXPECT_FALSE(should_drain(false, 3, 1, 0));
}

TEST(Pipeline, CatchUpNeedsASustainedBacklog)
{
    moonlight::CatchUpGuard guard;
    EXPECT_FALSE(guard.update(1000, 14, 0, 250000)); // Disabled.
    EXPECT_FALSE(guard.update(1000, 6, 6, 250000));
    EXPECT_FALSE(guard.update(200000, 7, 6, 250000));
    EXPECT_FALSE(guard.update(210000, 2, 6, 250000)); // Recovered: start over.
    EXPECT_FALSE(guard.update(220000, 6, 6, 250000));
    EXPECT_TRUE(guard.update(470000, 6, 6, 250000));
    EXPECT_FALSE(guard.update(480000, 6, 6, 250000)); // One request per episode.
    EXPECT_FALSE(guard.update(100, 6, 6, 250000));    // Clock discontinuity.
}

TEST(Pipeline, SlotsAreOnlyReusedAfterReleaseAndNeverTheLatestOutput)
{
    using Pool = moonlight::SlotPool<4>;
    Pool pool;
    EXPECT_EQ(pool.acquire_free(), 0);
    pool.state[0] = Pool::Decoding;
    pool.state[1] = Pool::Ready;
    pool.state[2] = Pool::Presenting;
    EXPECT_EQ(pool.acquire_free(), 3);
    pool.state[3] = Pool::Decoding;
    EXPECT_EQ(pool.acquire_free(), -1);
    EXPECT_EQ(pool.count(Pool::Decoding), 2u);
    // The newest picture stays intact until a newer one exists.
    pool.last_output = 2;
    pool.release(2);
    EXPECT_EQ(pool.acquire_free(), -1);
    pool.release(1);
    EXPECT_EQ(pool.acquire_free(), 1);
    pool.last_output = 1;
    EXPECT_EQ(pool.acquire_free(), 2);
    // Oldest release first, so a slot rests as long as possible.
    pool.last_output = -1;
    EXPECT_EQ(pool.acquire_free(), 2);
    pool.release_decoding();
    EXPECT_EQ(pool.count(Pool::Decoding), 0u);
    EXPECT_EQ(pool.count(Pool::Free), 4u);
    pool.release(-1);
    pool.release(4);
    EXPECT_EQ(pool.count(Pool::Free), 4u);
}

TEST(Pipeline, MailboxKeepsOnlyTheNewestPicture)
{
    moonlight::ReadyMailbox<int> mailbox;
    int out = 0, displaced = 0;
    EXPECT_FALSE(mailbox.take(&out));
    EXPECT_FALSE(mailbox.publish(1, &displaced));
    EXPECT_TRUE(mailbox.publish(2, &displaced));
    EXPECT_EQ(displaced, 1);
    EXPECT_TRUE(mailbox.take(&out));
    EXPECT_EQ(out, 2);
    EXPECT_FALSE(mailbox.take(&out));
}

TEST(Pipeline, FrameGapsAreAttributedToTheNetworkOrTheDecoder)
{
    moonlight::DropAttribution drops;
    drops.observe(10, 0);
    drops.observe(11, 0);
    drops.observe(14, 0); // Two frames never arrived.
    EXPECT_EQ(drops.network, 2u);
    EXPECT_EQ(drops.decoder, 0u);
    drops.observe(30, 1); // The queue was discarded for decoder backpressure.
    EXPECT_EQ(drops.network, 2u);
    EXPECT_EQ(drops.decoder, 15u);
    drops.observe(31, 1);
    drops.observe(33, 1);
    EXPECT_EQ(drops.network, 3u);
    drops.observe(5, 1); // A restarted numbering is not a gap.
    EXPECT_EQ(drops.network, 3u);
    EXPECT_EQ(drops.decoder, 15u);
}

TEST(Pipeline, ArrivalRateCountsFramesThatWereNeverDecoded)
{
    moonlight::ArrivalRate rate;
    EXPECT_FALSE(rate.update(1000000, 100));
    EXPECT_FALSE(rate.update(1500000, 160));
    // Only 3 of 120 frames were dequeued, yet the host sent 120 in a second.
    EXPECT_TRUE(rate.update(2000000, 220));
    EXPECT_EQ(rate.fps_x100, 12000u);
    EXPECT_FALSE(rate.update(1000, 5)); // Restart.
    EXPECT_EQ(rate.fps_x100, 12000u);
}

TEST(Pipeline, DecodeWindowReportsLastSecondMeanPercentileAndLoad)
{
    moonlight::WindowedTiming window;
    uint64_t now = 5000000;
    for (unsigned i = 0; i < 119; ++i)
    {
        window.add(now, i < 113 ? 4000u : 9000u);
        now += 8333;
    }
    EXPECT_EQ(window.last_count, 0u); // The first second is still open.
    now = 5000000 + 1000000;
    window.add(now, 9000u);
    EXPECT_EQ(window.last_count, 120u);
    EXPECT_EQ(window.last_mean_us, (113u * 4000u + 7u * 9000u) / 120u);
    EXPECT_EQ(window.last_max_us, 9000u);
    EXPECT_EQ(window.last_p95_us, 9000u);
    EXPECT_EQ(window.last_load_permille, 515u);
    // A saturated decoder is reported as fully loaded, never more.
    for (unsigned i = 0; i < 100; ++i)
    {
        now += 10000;
        window.add(now, 12000u);
    }
    EXPECT_EQ(window.last_load_permille, 1000u);
    EXPECT_EQ(window.last_p95_us, 12000u);
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

TEST(Performance, AudioBacklogThresholdIsExclusiveAndOptIn)
{
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
// Saving fails unless a test captures the file here.
std::vector<std::uint8_t> *kernel_write_capture;

// Frozen historical PC records use literal dimensions so the fixtures cannot
// grow when the live public host type changes.
struct LegacyHost
{
    char address[64];
    char name[64];
    char unique_id[48];
    std::uint32_t manual;
};

struct LegacyHostWithPort
{
    char address[64];
    char name[64];
    char unique_id[48];
    std::uint32_t manual;
    std::uint32_t http_port;
};

static_assert(sizeof(LegacyHost) == 180);
static_assert(sizeof(LegacyHostWithPort) == 184);

struct Version8Config
{
    std::uint32_t fields[13];
    LegacyHostWithPort hosts[8];
};

struct Version8File
{
    std::uint32_t magic, version, checksum, reserved;
    Version8Config config;
};

struct Version9Host
{
    char address[64];
    char name[64];
    char unique_id[48];
    std::uint32_t manual;
    std::uint32_t http_port;
    std::uint8_t mac[6];
    std::uint8_t reserved[2];
};

struct Version9Config
{
    std::uint32_t fields[13];
    Version9Host hosts[8];
};

struct Version9File
{
    std::uint32_t magic, version, checksum, reserved;
    Version9Config config;
};

static_assert(sizeof(Version8Config) == 1524);
static_assert(sizeof(Version8File) == 1540);
static_assert(sizeof(Version9Host) == 192);
static_assert(sizeof(Version9Config) == 1588);
static_assert(sizeof(Version9File) == 1604);

bool MacIsUnknown(const moonlight_config_host_t &host)
{
    for (std::uint8_t octet : host.mac)
        if (octet)
            return false;
    return true;
}

std::uint32_t ConfigChecksum(const void *data, std::size_t size)
{
    const auto *bytes = static_cast<const std::uint8_t *>(data);
    std::uint32_t value = UINT32_C(2166136261);
    for (std::size_t index = 0; index < size; ++index)
        value = (value ^ bytes[index]) * UINT32_C(16777619);
    return value;
}

// One mDNS reply waiting on the discovery socket, and where it came from.
struct MdnsReply
{
    std::vector<std::uint8_t> packet;
    std::uint8_t source[4];
};
std::deque<MdnsReply> mdns_replies;
} // namespace

extern "C"
{
    int sceKernelOpen(const char *, int flags, std::uint16_t)
    {
        kernel_read_offset = 0;
        if (flags != 0)
        {
            if (kernel_write_capture)
                kernel_write_capture->clear();
            return kernel_write_capture ? 2 : -1;
        }
        return kernel_read_data ? 1 : -1;
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

    std::int64_t sceKernelWrite(int, const void *buffer, std::size_t length)
    {
        if (!kernel_write_capture)
            return -1;
        const auto *bytes = static_cast<const std::uint8_t *>(buffer);
        kernel_write_capture->insert(kernel_write_capture->end(), bytes, bytes + length);
        return static_cast<std::int64_t>(length);
    }

    int sceKernelRename(const char *, const char *)
    {
        return kernel_write_capture ? 0 : -1;
    }

    int sceKernelUnlink(const char *)
    {
        return -1;
    }

    // The discovery socket: queued replies arrive at once, then nothing.
    int sceNetSocket(const char *, int, int, int)
    {
        return 3;
    }
    int sceNetSocketClose(int)
    {
        return 0;
    }
    int sceNetSendto(int, const void *, std::size_t length, int, const void *, std::uint32_t)
    {
        return static_cast<int>(length);
    }
    int sceNetRecvfrom(int, void *buffer, std::size_t length, int, void *address, std::uint32_t *)
    {
        if (mdns_replies.empty())
            return -1;
        const MdnsReply reply = mdns_replies.front();
        mdns_replies.pop_front();
        auto *source = static_cast<std::uint8_t *>(address);
        source[0] = 16;
        source[1] = 2; // IPv4
        std::memcpy(source + 4, reply.source, sizeof(reply.source));
        const std::size_t size = reply.packet.size() < length ? reply.packet.size() : length;
        std::memcpy(buffer, reply.packet.data(), size);
        return static_cast<int>(size);
    }
    int sceNetSetsockopt(int, int, int, const void *, std::uint32_t)
    {
        return 0;
    }
    int sceNetEpollCreate(const char *, int)
    {
        return 4;
    }
    int sceNetEpollControl(int, int, int, void *)
    {
        return 0;
    }
    int sceNetEpollWait(int, void *, int, int)
    {
        return mdns_replies.empty() ? 0 : 1;
    }
    int sceNetEpollDestroy(int)
    {
        return 0;
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

TEST(StreamShortcuts, TouchpadL1ReturnsToLauncher)
{
    EXPECT_TRUE(
        moonlight_stream_disconnect_requested(MOONLIGHT_PS5_PAD_TOUCHPAD | MOONLIGHT_PS5_PAD_L1));
    EXPECT_FALSE(moonlight_stream_disconnect_requested(MOONLIGHT_PS5_PAD_TOUCHPAD));
}

TEST(StreamShortcuts, TouchpadR1TogglesMetrics)
{
    EXPECT_TRUE(
        moonlight_stream_hud_toggle_requested(MOONLIGHT_PS5_PAD_TOUCHPAD | MOONLIGHT_PS5_PAD_R1));
    EXPECT_FALSE(moonlight_stream_hud_toggle_requested(MOONLIGHT_PS5_PAD_R1));
}

TEST(StreamShortcuts, TouchpadTriangleTogglesStreamKeyboard)
{
    EXPECT_TRUE(moonlight_stream_keyboard_requested(MOONLIGHT_PS5_PAD_TOUCHPAD |
                                                    MOONLIGHT_PS5_PAD_TRIANGLE));
    EXPECT_FALSE(moonlight_stream_keyboard_requested(MOONLIGHT_PS5_PAD_TRIANGLE));
}

TEST(StreamShortcuts, TouchpadSquareTogglesMouseMode)
{
    EXPECT_TRUE(moonlight_stream_mouse_toggle_requested(MOONLIGHT_PS5_PAD_TOUCHPAD |
                                                        MOONLIGHT_PS5_PAD_SQUARE));
    EXPECT_FALSE(moonlight_stream_mouse_toggle_requested(MOONLIGHT_PS5_PAD_SQUARE));
    EXPECT_FALSE(moonlight_stream_mouse_toggle_requested(MOONLIGHT_PS5_PAD_TOUCHPAD));
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
    EXPECT_EQ(config.vsync_enabled, 1U);
    EXPECT_EQ(config.decoder_pipeline, MOONLIGHT_DECODER_PIPELINE_CLASSIC);
    EXPECT_EQ(config.decoder_cores, MOONLIGHT_DECODER_CORES_DEFAULT);
}

TEST(Configuration, MigratesVersionFiveAndDefaultsTheNewStreamSettings)
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
        std::uint32_t audio_configuration;
        LegacyHost hosts[8];
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
    file.version = 5;
    file.config.host_count = 1;
    file.config.bitrate_mbps = 80;
    file.config.display_area = MOONLIGHT_DISPLAY_AREA_FULL;
    file.config.video_codec = MOONLIGHT_VIDEO_CODEC_HEVC;
    file.config.stream_resolution = MOONLIGHT_STREAM_RESOLUTION_2160P;
    file.config.stream_fps = MOONLIGHT_STREAM_FPS_120;
    file.config.audio_configuration = MOONLIGHT_AUDIO_51_SURROUND;
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
    EXPECT_EQ(config.bitrate_mbps, 80U);
    EXPECT_EQ(config.stream_resolution, MOONLIGHT_STREAM_RESOLUTION_2160P);
    EXPECT_EQ(config.stream_fps, MOONLIGHT_STREAM_FPS_120);
    EXPECT_EQ(config.audio_configuration, MOONLIGHT_AUDIO_51_SURROUND);
    EXPECT_EQ(config.vsync_enabled, 1U);
    EXPECT_EQ(config.decoder_pipeline, MOONLIGHT_DECODER_PIPELINE_CLASSIC);
    EXPECT_EQ(config.decoder_cores, MOONLIGHT_DECODER_CORES_DEFAULT);
    // The persisted reader must retain every entered target, not just presets.
    kernel_read_data = reinterpret_cast<const std::uint8_t *>(&file);
    kernel_read_size = sizeof(file);
    for (uint32_t fps = 30; fps <= 120; ++fps)
    {
        file.config.stream_fps = fps;
        file.checksum = checksum(&file.config, sizeof(file.config));
        ASSERT_TRUE(moonlight_config_load(&config));
        EXPECT_EQ(config.stream_fps, fps);
    }
    for (uint32_t fps : {0u, 29u, 121u, UINT32_MAX})
    {
        file.config.stream_fps = fps;
        file.checksum = checksum(&file.config, sizeof(file.config));
        ASSERT_TRUE(moonlight_config_load(&config));
        EXPECT_EQ(config.stream_fps, MOONLIGHT_STREAM_FPS_60);
    }
    kernel_read_data = nullptr;
    kernel_read_size = 0;
    EXPECT_EQ(config.hosts[0].http_port, MOONLIGHT_CONFIG_DEFAULT_HTTP_PORT);
    EXPECT_TRUE(MacIsUnknown(config.hosts[0]));
}

TEST(Configuration, MigratesVersionsOneAndTwoWithUnknownMac)
{
    struct Version1Config
    {
        std::uint32_t host_count;
        std::uint32_t selected_host;
        std::uint32_t bitrate_mbps;
        std::uint32_t display_area;
        LegacyHost hosts[8];
    };
    struct Version1File
    {
        std::uint32_t magic, version, checksum, reserved;
        Version1Config config;
    } version_1{};
    struct Version2Config
    {
        std::uint32_t host_count;
        std::uint32_t selected_host;
        std::uint32_t bitrate_mbps;
        std::uint32_t display_area;
        std::uint32_t video_codec;
        std::uint32_t stream_resolution;
        LegacyHost hosts[8];
    };
    struct Version2File
    {
        std::uint32_t magic, version, checksum, reserved;
        Version2Config config;
    } version_2{};

    static_assert(sizeof(Version1File) == 1472);
    static_assert(sizeof(Version2File) == 1480);
    version_1.magic = version_2.magic = UINT32_C(0x504c4346);
    version_1.version = 1;
    version_1.config.host_count = 1;
    version_1.config.bitrate_mbps = 15;
    version_1.config.display_area = MOONLIGHT_DISPLAY_AREA_TV_SAFE;
    std::snprintf(version_1.config.hosts[0].address, sizeof(version_1.config.hosts[0].address),
                  "192.168.1.1");
    std::snprintf(version_1.config.hosts[0].name, sizeof(version_1.config.hosts[0].name),
                  "Version One");
    version_1.checksum = ConfigChecksum(&version_1.config, sizeof(version_1.config));

    version_2.version = 2;
    version_2.config.host_count = 1;
    version_2.config.bitrate_mbps = 25;
    version_2.config.display_area = MOONLIGHT_DISPLAY_AREA_FULL;
    version_2.config.video_codec = MOONLIGHT_VIDEO_CODEC_HEVC;
    version_2.config.stream_resolution = MOONLIGHT_STREAM_RESOLUTION_1440P;
    std::snprintf(version_2.config.hosts[0].address, sizeof(version_2.config.hosts[0].address),
                  "192.168.1.2");
    std::snprintf(version_2.config.hosts[0].unique_id, sizeof(version_2.config.hosts[0].unique_id),
                  "version-two-id");
    version_2.config.hosts[0].manual = 1;
    version_2.checksum = ConfigChecksum(&version_2.config, sizeof(version_2.config));

    moonlight_config_t loaded{};
    kernel_read_data = reinterpret_cast<const std::uint8_t *>(&version_1);
    kernel_read_size = sizeof(version_1);
    ASSERT_TRUE(moonlight_config_load(&loaded));
    ASSERT_EQ(loaded.host_count, 1u);
    EXPECT_STREQ(loaded.hosts[0].name, "Version One");
    EXPECT_EQ(loaded.bitrate_mbps, 15u);
    EXPECT_TRUE(MacIsUnknown(loaded.hosts[0]));

    kernel_read_data = reinterpret_cast<const std::uint8_t *>(&version_2);
    kernel_read_size = sizeof(version_2);
    ASSERT_TRUE(moonlight_config_load(&loaded));
    ASSERT_EQ(loaded.host_count, 1u);
    EXPECT_STREQ(loaded.hosts[0].address, "192.168.1.2");
    EXPECT_STREQ(loaded.hosts[0].unique_id, "version-two-id");
    EXPECT_EQ(loaded.hosts[0].manual, 1u);
    EXPECT_EQ(loaded.video_codec, MOONLIGHT_VIDEO_CODEC_HEVC);
    EXPECT_EQ(loaded.stream_resolution, MOONLIGHT_STREAM_RESOLUTION_1440P);
    EXPECT_TRUE(MacIsUnknown(loaded.hosts[0]));
    kernel_read_data = nullptr;
    kernel_read_size = 0;
}

TEST(Configuration, MigratesBothVersionSevenLayoutsWithoutLosingPyroWaveOrPorts)
{
    struct UpstreamConfig
    {
        std::uint32_t fields[12];
        LegacyHostWithPort hosts[8];
    };
    struct PyroWaveConfig
    {
        std::uint32_t fields[13];
        LegacyHost hosts[8];
    };
    struct UpstreamFile
    {
        std::uint32_t magic, version, checksum, reserved;
        UpstreamConfig config;
    } upstream{};
    struct PyroWaveFile
    {
        std::uint32_t magic, version, checksum, reserved;
        PyroWaveConfig config;
    } pyrowave{};
    static_assert(sizeof(PyroWaveConfig) == 1492);
    static_assert(sizeof(PyroWaveFile) == 1508);
    static_assert(sizeof(UpstreamConfig) == 1520);
    static_assert(sizeof(UpstreamFile) == 1536);
    const std::uint32_t frozen_settings[13] = {
        1,
        0,
        600,
        MOONLIGHT_DISPLAY_AREA_FULL,
        MOONLIGHT_VIDEO_CODEC_PYROWAVE,
        MOONLIGHT_STREAM_RESOLUTION_1080P,
        117,
        1,
        MOONLIGHT_AUDIO_STEREO,
        1,
        MOONLIGHT_DECODER_PIPELINE_CLASSIC,
        MOONLIGHT_DECODER_CORES_DEFAULT,
        MOONLIGHT_CHROMA_444,
    };
    upstream.magic = pyrowave.magic = UINT32_C(0x504c4346);
    upstream.version = pyrowave.version = 7;
    memcpy(upstream.config.fields, frozen_settings, sizeof(upstream.config.fields));
    memcpy(pyrowave.config.fields, frozen_settings, sizeof(pyrowave.config.fields));
    // Upstream's layout has a separate port and no chroma field.
    upstream.config.fields[4] = MOONLIGHT_VIDEO_CODEC_HEVC;
    snprintf(upstream.config.hosts[0].address, sizeof(upstream.config.hosts[0].address),
             "192.168.1.5");
    upstream.config.hosts[0].http_port = 48000;
    // Our previous layout puts the custom port inside the address.
    snprintf(pyrowave.config.hosts[0].address, sizeof(pyrowave.config.hosts[0].address),
             "192.168.1.5:49000");
    pyrowave.config.hosts[0].manual = 1;
    upstream.checksum = ConfigChecksum(&upstream.config, sizeof(upstream.config));
    pyrowave.checksum = ConfigChecksum(&pyrowave.config, sizeof(pyrowave.config));
    for (int variant = 0; variant < 2; ++variant)
    {
        kernel_read_data = variant ? reinterpret_cast<const std::uint8_t *>(&pyrowave)
                                   : reinterpret_cast<const std::uint8_t *>(&upstream);
        kernel_read_size = variant ? sizeof(pyrowave) : sizeof(upstream);
        moonlight_config_t loaded{};
        ASSERT_TRUE(moonlight_config_load(&loaded));
        EXPECT_EQ(loaded.host_count, 1u);
        EXPECT_STREQ(loaded.hosts[0].address, "192.168.1.5");
        EXPECT_EQ(loaded.hosts[0].http_port, variant ? 49000u : 48000u);
        EXPECT_EQ(loaded.stream_fps, 117u);
        EXPECT_EQ(loaded.video_codec,
                  variant ? MOONLIGHT_VIDEO_CODEC_PYROWAVE : MOONLIGHT_VIDEO_CODEC_HEVC);
        EXPECT_EQ(loaded.chroma_sampling, variant ? MOONLIGHT_CHROMA_444 : MOONLIGHT_CHROMA_420);
        EXPECT_EQ(loaded.hdr_enabled, 1u);
        EXPECT_TRUE(MacIsUnknown(loaded.hosts[0]));
    }
    ++pyrowave.checksum;
    kernel_read_data = reinterpret_cast<const std::uint8_t *>(&pyrowave);
    kernel_read_size = sizeof(pyrowave);
    moonlight_config_t loaded{};
    EXPECT_FALSE(moonlight_config_load(&loaded));
    kernel_read_data = nullptr;
    kernel_read_size = 0;
}

TEST(Configuration, MigratesVersionEightWithoutChangingItsFrozenLayout)
{
    Version8File file{};
    file.magic = UINT32_C(0x504c4346);
    file.version = 8;
    file.config.fields[0] = 1;
    file.config.fields[1] = 0;
    file.config.fields[2] = 75;
    file.config.fields[3] = MOONLIGHT_DISPLAY_AREA_TV_SAFE;
    file.config.fields[4] = MOONLIGHT_VIDEO_CODEC_HEVC;
    file.config.fields[5] = MOONLIGHT_STREAM_RESOLUTION_1440P;
    file.config.fields[6] = 90;
    file.config.fields[7] = 1;
    file.config.fields[8] = MOONLIGHT_AUDIO_51_SURROUND;
    file.config.fields[9] = 0;
    file.config.fields[10] = MOONLIGHT_DECODER_PIPELINE_CLASSIC;
    file.config.fields[11] = MOONLIGHT_DECODER_CORES_DEFAULT;
    file.config.fields[12] = MOONLIGHT_CHROMA_444;
    std::snprintf(file.config.hosts[0].address, sizeof(file.config.hosts[0].address),
                  "192.168.1.8");
    std::snprintf(file.config.hosts[0].name, sizeof(file.config.hosts[0].name), "Version Eight");
    std::snprintf(file.config.hosts[0].unique_id, sizeof(file.config.hosts[0].unique_id),
                  "version-eight-id");
    file.config.hosts[0].manual = 1;
    file.config.hosts[0].http_port = 48989;
    file.checksum = ConfigChecksum(&file.config, sizeof(file.config));
    kernel_read_data = reinterpret_cast<const std::uint8_t *>(&file);
    kernel_read_size = sizeof(file);

    moonlight_config_t loaded{};
    ASSERT_TRUE(moonlight_config_load(&loaded));
    ASSERT_EQ(loaded.host_count, 1u);
    EXPECT_EQ(loaded.bitrate_mbps, 75u);
    EXPECT_EQ(loaded.display_area, MOONLIGHT_DISPLAY_AREA_TV_SAFE);
    EXPECT_EQ(loaded.video_codec, MOONLIGHT_VIDEO_CODEC_HEVC);
    EXPECT_EQ(loaded.stream_resolution, MOONLIGHT_STREAM_RESOLUTION_1440P);
    EXPECT_EQ(loaded.stream_fps, 90u);
    EXPECT_EQ(loaded.hdr_enabled, 1u);
    EXPECT_EQ(loaded.audio_configuration, MOONLIGHT_AUDIO_51_SURROUND);
    EXPECT_EQ(loaded.vsync_enabled, 0u);
    EXPECT_EQ(loaded.chroma_sampling, MOONLIGHT_CHROMA_444);
    EXPECT_STREQ(loaded.hosts[0].address, "192.168.1.8");
    EXPECT_STREQ(loaded.hosts[0].name, "Version Eight");
    EXPECT_STREQ(loaded.hosts[0].unique_id, "version-eight-id");
    EXPECT_EQ(loaded.hosts[0].manual, 1u);
    EXPECT_EQ(loaded.hosts[0].http_port, 48989u);
    EXPECT_TRUE(MacIsUnknown(loaded.hosts[0]));

    ++file.checksum;
    kernel_read_data = reinterpret_cast<const std::uint8_t *>(&file);
    kernel_read_size = sizeof(file);
    EXPECT_FALSE(moonlight_config_load(&loaded));
    kernel_read_data = nullptr;
    kernel_read_size = 0;
}

TEST(Configuration, MigratesVersionSixAndGivesSavedPcsTheDefaultPort)
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
        std::uint32_t audio_configuration;
        std::uint32_t vsync_enabled;
        std::uint32_t decoder_pipeline;
        std::uint32_t decoder_cores;
        LegacyHost hosts[8];
    };
    struct LegacyFile
    {
        std::uint32_t magic;
        std::uint32_t version;
        std::uint32_t checksum;
        std::uint32_t reserved;
        LegacyConfig config;
    } file{};

    // The layout written by 01.000.070.
    static_assert(sizeof(LegacyFile) == 16 + 48 + MOONLIGHT_CONFIG_MAX_HOSTS * 180);
    file.magic = UINT32_C(0x504c4346);
    file.version = 6;
    file.config.host_count = 2;
    file.config.selected_host = 1;
    file.config.bitrate_mbps = 80;
    file.config.display_area = MOONLIGHT_DISPLAY_AREA_FULL;
    file.config.video_codec = MOONLIGHT_VIDEO_CODEC_HEVC;
    file.config.stream_resolution = MOONLIGHT_STREAM_RESOLUTION_2160P;
    file.config.stream_fps = MOONLIGHT_STREAM_FPS_120;
    file.config.audio_configuration = MOONLIGHT_AUDIO_51_SURROUND;
    file.config.vsync_enabled = 0;
    file.config.decoder_pipeline = MOONLIGHT_DECODER_PIPELINE_ADAPTIVE;
    file.config.decoder_cores = 5;
    std::snprintf(file.config.hosts[0].address, sizeof(file.config.hosts[0].address),
                  "192.168.4.20");
    std::snprintf(file.config.hosts[0].name, sizeof(file.config.hosts[0].name), "Gaming-PC");
    std::snprintf(file.config.hosts[1].address, sizeof(file.config.hosts[1].address),
                  "192.168.4.21");
    std::snprintf(file.config.hosts[1].name, sizeof(file.config.hosts[1].name), "Office-PC");
    std::snprintf(file.config.hosts[1].unique_id, sizeof(file.config.hosts[1].unique_id),
                  "office-id");
    file.config.hosts[1].manual = 1;
    file.checksum = ConfigChecksum(&file.config, sizeof(file.config));
    kernel_read_data = reinterpret_cast<const std::uint8_t *>(&file);
    kernel_read_size = sizeof(file);

    moonlight_config_t config{};
    const bool loaded = moonlight_config_load(&config);
    kernel_read_data = nullptr;
    kernel_read_size = 0;

    ASSERT_TRUE(loaded);
    ASSERT_EQ(config.host_count, 2U);
    EXPECT_EQ(config.selected_host, 1U);
    EXPECT_EQ(config.bitrate_mbps, 80U);
    EXPECT_EQ(config.stream_fps, MOONLIGHT_STREAM_FPS_120);
    EXPECT_EQ(config.audio_configuration, MOONLIGHT_AUDIO_51_SURROUND);
    EXPECT_EQ(config.vsync_enabled, 0U);
    EXPECT_EQ(config.decoder_pipeline, MOONLIGHT_DECODER_PIPELINE_CLASSIC);
    EXPECT_EQ(config.decoder_cores, MOONLIGHT_DECODER_CORES_DEFAULT);
    EXPECT_STREQ(config.hosts[0].address, "192.168.4.20");
    EXPECT_STREQ(config.hosts[0].name, "Gaming-PC");
    EXPECT_EQ(config.hosts[0].manual, 0U);
    EXPECT_EQ(config.hosts[0].http_port, MOONLIGHT_CONFIG_DEFAULT_HTTP_PORT);
    EXPECT_TRUE(MacIsUnknown(config.hosts[0]));
    EXPECT_STREQ(config.hosts[1].address, "192.168.4.21");
    EXPECT_STREQ(config.hosts[1].name, "Office-PC");
    EXPECT_STREQ(config.hosts[1].unique_id, "office-id");
    EXPECT_EQ(config.hosts[1].manual, 1U);
    EXPECT_EQ(config.hosts[1].http_port, MOONLIGHT_CONFIG_DEFAULT_HTTP_PORT);
    EXPECT_TRUE(MacIsUnknown(config.hosts[1]));
}

TEST(Configuration, SavesAndReloadsVersionNineMacAndPorts)
{
    const std::uint8_t saved_mac[MOONLIGHT_WOL_MAC_BYTES] = {0x02, 0x10, 0x20, 0x30, 0x40, 0x50};
    moonlight_config_t saved{};
    moonlight_config_defaults(&saved);
    saved.bitrate_mbps = 60;
    ASSERT_EQ(
        moonlight_config_upsert_host(&saved, "192.168.1.10", 0, "Default", "one", nullptr, false),
        0);
    ASSERT_EQ(moonlight_config_upsert_host(&saved, "192.168.1.10", 48989, "Second", "two",
                                           saved_mac, true),
              1);
    saved.selected_host = 1;

    std::vector<std::uint8_t> written;
    kernel_write_capture = &written;
    const bool stored = moonlight_config_save(&saved);
    kernel_write_capture = nullptr;
    ASSERT_TRUE(stored);
    ASSERT_EQ(written.size(), sizeof(Version9File));
    Version9File disk{};
    std::memcpy(&disk, written.data(), sizeof(disk));
    EXPECT_EQ(disk.magic, UINT32_C(0x504c4346));
    EXPECT_EQ(disk.version, 9u);
    EXPECT_EQ(disk.checksum, ConfigChecksum(&disk.config, sizeof(disk.config)));
    EXPECT_EQ(disk.config.hosts[1].reserved[0], 0u);
    EXPECT_EQ(disk.config.hosts[1].reserved[1], 0u);

    kernel_read_data = written.data();
    kernel_read_size = written.size();
    moonlight_config_t config{};
    const bool loaded = moonlight_config_load(&config);
    kernel_read_data = nullptr;
    kernel_read_size = 0;

    ASSERT_TRUE(loaded);
    ASSERT_EQ(config.host_count, 2U);
    EXPECT_EQ(config.selected_host, 1U);
    EXPECT_EQ(config.bitrate_mbps, 60U);
    EXPECT_EQ(config.hosts[0].http_port, MOONLIGHT_CONFIG_DEFAULT_HTTP_PORT);
    EXPECT_STREQ(config.hosts[1].address, "192.168.1.10");
    EXPECT_STREQ(config.hosts[1].name, "Second");
    EXPECT_EQ(config.hosts[1].http_port, 48989U);
    EXPECT_EQ(config.hosts[1].manual, 1U);
    EXPECT_EQ(std::memcmp(config.hosts[1].mac, saved_mac, sizeof(saved_mac)), 0);
    EXPECT_EQ(config.hosts[1].reserved[0], 0u);
    EXPECT_EQ(config.hosts[1].reserved[1], 0u);

    ++disk.checksum;
    kernel_read_data = reinterpret_cast<const std::uint8_t *>(&disk);
    kernel_read_size = sizeof(disk);
    EXPECT_FALSE(moonlight_config_load(&config));
    kernel_read_data = nullptr;
    kernel_read_size = 0;
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
        LegacyHost hosts[8];
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
    EXPECT_TRUE(MacIsUnknown(config.hosts[0]));
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
        LegacyHost hosts[8];
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
    EXPECT_TRUE(MacIsUnknown(config.hosts[0]));
}

TEST(Configuration, UpsertUpdatesAHostByStableIdentity)
{
    const std::uint8_t mac[MOONLIGHT_WOL_MAC_BYTES] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    moonlight_config_t config{};
    moonlight_config_defaults(&config);

    EXPECT_EQ(
        moonlight_config_upsert_host(&config, "192.168.1.10", 0, "Gaming PC", "host-1", mac, false),
        0);
    EXPECT_EQ(moonlight_config_upsert_host(&config, "192.168.1.20", 0, "", "host-1", nullptr, true),
              0);
    ASSERT_EQ(config.host_count, 1U);
    EXPECT_STREQ(config.hosts[0].address, "192.168.1.20");
    EXPECT_STREQ(config.hosts[0].name, "Gaming PC");
    EXPECT_STREQ(config.hosts[0].unique_id, "host-1");
    EXPECT_EQ(config.hosts[0].manual, 1U);
    EXPECT_EQ(config.hosts[0].http_port, MOONLIGHT_CONFIG_DEFAULT_HTTP_PORT);
    EXPECT_EQ(std::memcmp(config.hosts[0].mac, mac, sizeof(mac)), 0);
}

TEST(Configuration, MacUpdatesReplaceWhileMissingOrInvalidDataPreserves)
{
    const std::uint8_t first[MOONLIGHT_WOL_MAC_BYTES] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    const std::uint8_t replacement[MOONLIGHT_WOL_MAC_BYTES] = {0x0a, 0xaa, 0xbb, 0xcc, 0xdd, 0xee};
    const std::uint8_t all_zero[MOONLIGHT_WOL_MAC_BYTES] = {};
    const std::uint8_t multicast[MOONLIGHT_WOL_MAC_BYTES] = {0x01, 0x11, 0x22, 0x33, 0x44, 0x55};
    moonlight_config_t config{};
    moonlight_config_defaults(&config);

    ASSERT_EQ(
        moonlight_config_upsert_host(&config, "192.168.1.10", 47989, "PC", "stable", first, false),
        0);
    ASSERT_EQ(moonlight_config_upsert_host(&config, "192.168.1.11", 47989, "PC", "stable", nullptr,
                                           false),
              0);
    EXPECT_EQ(std::memcmp(config.hosts[0].mac, first, sizeof(first)), 0);
    ASSERT_EQ(moonlight_config_upsert_host(&config, "192.168.1.11", 47989, "PC", "stable", all_zero,
                                           false),
              0);
    EXPECT_EQ(std::memcmp(config.hosts[0].mac, first, sizeof(first)), 0);
    ASSERT_EQ(moonlight_config_upsert_host(&config, "192.168.1.11", 47989, "PC", "stable",
                                           multicast, false),
              0);
    EXPECT_EQ(std::memcmp(config.hosts[0].mac, first, sizeof(first)), 0);
    ASSERT_EQ(moonlight_config_upsert_host(&config, "192.168.1.11", 47989, "PC", "stable",
                                           replacement, false),
              0);
    EXPECT_EQ(std::memcmp(config.hosts[0].mac, replacement, sizeof(replacement)), 0);
}

TEST(Configuration, DuplicateMergeKeepsFreshMacOrInheritsAValidOldOne)
{
    const std::uint8_t old_mac[MOONLIGHT_WOL_MAC_BYTES] = {0x02, 0x10, 0x20, 0x30, 0x40, 0x50};
    const std::uint8_t fresh_mac[MOONLIGHT_WOL_MAC_BYTES] = {0x0a, 0x11, 0x21, 0x31, 0x41, 0x51};
    for (bool fresh : {false, true})
    {
        moonlight_config_t config{};
        moonlight_config_defaults(&config);
        ASSERT_EQ(moonlight_config_upsert_host(&config, "192.168.1.10", 47989, "Survivor", "stable",
                                               nullptr, false),
                  0);
        ASSERT_EQ(moonlight_config_upsert_host(&config, "192.168.1.20", 47989, "Duplicate", "",
                                               old_mac, true),
                  1);
        config.selected_host = 1;
        ASSERT_EQ(moonlight_config_upsert_host(&config, "192.168.1.20", 47989, "Survivor", "stable",
                                               fresh ? fresh_mac : nullptr, false),
                  0);
        ASSERT_EQ(config.host_count, 1u);
        EXPECT_EQ(config.selected_host, 0u);
        EXPECT_EQ(
            std::memcmp(config.hosts[0].mac, fresh ? fresh_mac : old_mac, MOONLIGHT_WOL_MAC_BYTES),
            0);
    }
}

TEST(Configuration, ChangedIdentityAtSameEndpointCannotKeepAStaleMac)
{
    const std::uint8_t stale[MOONLIGHT_WOL_MAC_BYTES] = {0x02, 0x10, 0x20, 0x30, 0x40, 0x50};
    const std::uint8_t replacement[MOONLIGHT_WOL_MAC_BYTES] = {0x0a, 0x11, 0x21, 0x31, 0x41, 0x51};
    moonlight_config_t config{};
    moonlight_config_defaults(&config);
    ASSERT_EQ(
        moonlight_config_upsert_host(&config, "192.168.1.10", 47989, "Old", "old-id", stale, false),
        0);

    ASSERT_EQ(moonlight_config_upsert_host(&config, "192.168.1.10", 47989, "New", "new-id", nullptr,
                                           false),
              0);
    EXPECT_STREQ(config.hosts[0].unique_id, "new-id");
    EXPECT_TRUE(MacIsUnknown(config.hosts[0]));

    ASSERT_EQ(moonlight_config_upsert_host(&config, "192.168.1.10", 47989, "Newest", "newest-id",
                                           replacement, false),
              0);
    EXPECT_STREQ(config.hosts[0].unique_id, "newest-id");
    EXPECT_EQ(std::memcmp(config.hosts[0].mac, replacement, sizeof(replacement)), 0);
}

TEST(Configuration, OneAddressCanHoldSunshineOnSeveralPorts)
{
    moonlight_config_t config{};
    moonlight_config_defaults(&config);

    EXPECT_EQ(
        moonlight_config_upsert_host(&config, "192.168.1.10", 47989, "Desk", "one", nullptr, false),
        0);
    EXPECT_EQ(
        moonlight_config_upsert_host(&config, "192.168.1.10", 48989, "TV", "two", nullptr, true),
        1);
    // A refresh of either endpoint updates that entry only.
    EXPECT_EQ(moonlight_config_upsert_host(&config, "192.168.1.10", 48989, "TV room", "two",
                                           nullptr, false),
              1);
    ASSERT_EQ(config.host_count, 2U);
    EXPECT_STREQ(config.hosts[0].name, "Desk");
    EXPECT_EQ(config.hosts[0].http_port, 47989U);
    EXPECT_STREQ(config.hosts[1].name, "TV room");
    EXPECT_EQ(config.hosts[1].http_port, 48989U);
    EXPECT_EQ(config.hosts[1].manual, 1U);
}

TEST(Configuration, AnUnknownPortKeepsTheSavedOne)
{
    moonlight_config_t config{};
    moonlight_config_defaults(&config);
    ASSERT_EQ(
        moonlight_config_upsert_host(&config, "192.168.1.10", 48989, "Desk", "", nullptr, true), 0);

    // Discovery without a port record must not add a second, unreachable entry.
    EXPECT_EQ(
        moonlight_config_upsert_host(&config, "192.168.1.10", 0, "DESK-PC", "", nullptr, false), 0);
    ASSERT_EQ(config.host_count, 1U);
    EXPECT_EQ(config.hosts[0].http_port, 48989U);
    EXPECT_STREQ(config.hosts[0].name, "DESK-PC");
    // A PC seen for the first time without a port gets Sunshine's default.
    EXPECT_EQ(moonlight_config_upsert_host(&config, "192.168.1.30", 0, "Other", "", nullptr, false),
              1);
    EXPECT_EQ(config.hosts[1].http_port, MOONLIGHT_CONFIG_DEFAULT_HTTP_PORT);
}

TEST(Configuration, APcIsFollowedToItsNewPort)
{
    moonlight_config_t config{};
    moonlight_config_defaults(&config);
    ASSERT_EQ(
        moonlight_config_upsert_host(&config, "192.168.1.10", 47989, "Desk", "one", nullptr, false),
        0);
    // Discovery sees the changed port first, without an identity...
    ASSERT_EQ(
        moonlight_config_upsert_host(&config, "192.168.1.10", 48989, "Desk", "", nullptr, false),
        1);
    config.selected_host = 1;
    // ...and the refresh of that endpoint recognises the same Sunshine.
    EXPECT_EQ(
        moonlight_config_upsert_host(&config, "192.168.1.10", 48989, "Desk", "one", nullptr, false),
        0);
    ASSERT_EQ(config.host_count, 1U);
    EXPECT_EQ(config.selected_host, 0U);
    EXPECT_EQ(config.hosts[0].http_port, 48989U);
    EXPECT_EQ(config.hosts[1].address[0], '\0');
}

TEST(Configuration, ChangingAPortEditsThatPcOnly)
{
    const std::uint8_t mac[MOONLIGHT_WOL_MAC_BYTES] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    moonlight_config_t config{};
    moonlight_config_defaults(&config);
    ASSERT_EQ(moonlight_config_upsert_host(&config, "192.168.1.10", 0, "Desk", "one", mac, false),
              0);
    ASSERT_EQ(
        moonlight_config_upsert_host(&config, "192.168.1.20", 0, "Other", "two", nullptr, true), 1);
    config.selected_host = 1;

    EXPECT_EQ(moonlight_config_set_host_port(&config, 0, 48989), 0);
    ASSERT_EQ(config.host_count, 2U);
    EXPECT_EQ(config.hosts[0].http_port, 48989U);
    // The identity belonged to the old port: the next refresh fills it in again.
    EXPECT_EQ(config.hosts[0].unique_id[0], '\0');
    EXPECT_TRUE(MacIsUnknown(config.hosts[0]));
    EXPECT_STREQ(config.hosts[0].name, "Desk");
    EXPECT_EQ(config.hosts[1].http_port, MOONLIGHT_CONFIG_DEFAULT_HTTP_PORT);
    EXPECT_STREQ(config.hosts[1].unique_id, "two");
    EXPECT_EQ(config.selected_host, 1U);
    // Setting the same port again is not a change.
    ASSERT_EQ(
        moonlight_config_upsert_host(&config, "192.168.1.10", 48989, "", "one", nullptr, false), 0);
    EXPECT_EQ(moonlight_config_set_host_port(&config, 0, 48989), 0);
    EXPECT_STREQ(config.hosts[0].unique_id, "one");

    EXPECT_EQ(moonlight_config_set_host_port(&config, 0, 0), -1);
    EXPECT_EQ(moonlight_config_set_host_port(&config, 2, 48989), -1);
    EXPECT_EQ(moonlight_config_set_host_port(nullptr, 0, 48989), -1);
}

TEST(Configuration, ChangingAPortOntoASavedEndpointMergesThePcs)
{
    const std::uint8_t first_mac[MOONLIGHT_WOL_MAC_BYTES] = {0x02, 0x10, 0x20, 0x30, 0x40, 0x50};
    const std::uint8_t other_mac[MOONLIGHT_WOL_MAC_BYTES] = {0x0a, 0x11, 0x21, 0x31, 0x41, 0x51};
    moonlight_config_t config{};
    moonlight_config_defaults(&config);
    ASSERT_EQ(
        moonlight_config_upsert_host(&config, "192.168.1.10", 47989, "Old", "", first_mac, false),
        0);
    ASSERT_EQ(
        moonlight_config_upsert_host(&config, "192.168.1.10", 48989, "New", "", other_mac, true),
        1);
    config.selected_host = 0;

    EXPECT_EQ(moonlight_config_set_host_port(&config, 0, 48989), 0);
    ASSERT_EQ(config.host_count, 1U);
    EXPECT_EQ(config.selected_host, 0U);
    EXPECT_STREQ(config.hosts[0].name, "Old");
    EXPECT_EQ(config.hosts[0].http_port, 48989U);
    EXPECT_EQ(config.hosts[0].manual, 1U);
    EXPECT_TRUE(MacIsUnknown(config.hosts[0]));
}

TEST(Configuration, HostPortFallsBackToTheDefault)
{
    moonlight_config_host_t host{};
    EXPECT_EQ(moonlight_config_host_port(nullptr), MOONLIGHT_CONFIG_DEFAULT_HTTP_PORT);
    EXPECT_EQ(moonlight_config_host_port(&host), MOONLIGHT_CONFIG_DEFAULT_HTTP_PORT);
    host.http_port = 70000;
    EXPECT_EQ(moonlight_config_host_port(&host), MOONLIGHT_CONFIG_DEFAULT_HTTP_PORT);
    host.http_port = 1029;
    EXPECT_EQ(moonlight_config_host_port(&host), 1029U);
}

TEST(Configuration, ParsesAnAddressWithAnOptionalPort)
{
    char address[MOONLIGHT_CONFIG_ADDRESS_SIZE]{};
    std::uint16_t port = 1;

    ASSERT_TRUE(moonlight_config_parse_endpoint("192.168.1.50", address, &port));
    EXPECT_STREQ(address, "192.168.1.50");
    EXPECT_EQ(port, 0U);
    ASSERT_TRUE(moonlight_config_parse_endpoint("  192.168.001.050:48989 ", address, &port));
    EXPECT_STREQ(address, "192.168.1.50");
    EXPECT_EQ(port, 48989U);
    ASSERT_TRUE(moonlight_config_parse_endpoint("10.0.0.1:65535", address, &port));
    EXPECT_EQ(port, 65535U);

    for (const char *text :
         {"", " ", "192.168.1", "192.168.1.256", "192.168.1.50.1",
          "192.168.1.50:", "192.168.1.50:0", "192.168.1.50:65536", "192.168.1.50:-1",
          "192.168.1.50 :80", "192.168.1.50:80:1", "192.168.1.50 x", "-1.2.3.4", "a.b.c.d",
          "1..2.3", "pc.local:80", "192.168.1.50:99999999999999999999"})
        EXPECT_FALSE(moonlight_config_parse_endpoint(text, address, &port)) << text;
    EXPECT_FALSE(moonlight_config_parse_endpoint(nullptr, address, &port));
}

TEST(Configuration, ParsesAPortNumber)
{
    std::uint16_t port = 0;

    ASSERT_TRUE(moonlight_config_parse_port("47989", &port));
    EXPECT_EQ(port, 47989U);
    ASSERT_TRUE(moonlight_config_parse_port(" 1 ", &port));
    EXPECT_EQ(port, 1U);
    ASSERT_TRUE(moonlight_config_parse_port("65535", &port));
    EXPECT_EQ(port, 65535U);
    for (const char *text : {"", " ", "0", "65536", "-1", "+80", "80a", "4 7", "0x50", "1e3"})
        EXPECT_FALSE(moonlight_config_parse_port(text, &port)) << text;
    EXPECT_FALSE(moonlight_config_parse_port(nullptr, &port));
}

TEST(Configuration, DiscoveryPreservesAnExplicitCustomPort)
{
    moonlight_config_t config{};
    moonlight_config_defaults(&config);
    ASSERT_EQ(
        moonlight_config_upsert_host(&config, "192.168.1.10", 48000, "PC", "host-1", nullptr, true),
        0);
    ASSERT_EQ(moonlight_config_upsert_host(&config, "192.168.1.10", 0, "PC discovered", "host-1",
                                           nullptr, false),
              0);
    EXPECT_EQ(config.host_count, 1u);
    EXPECT_STREQ(config.hosts[0].address, "192.168.1.10");
    EXPECT_EQ(config.hosts[0].http_port, 48000u);
    ASSERT_EQ(moonlight_config_set_host_port(&config, 0, 49000), 0);
    EXPECT_EQ(config.hosts[0].http_port, 49000u);
}

TEST(Configuration, UpsertRejectsAHostBeyondCapacity)
{
    moonlight_config_t config{};
    moonlight_config_defaults(&config);
    for (std::uint32_t index = 0; index < MOONLIGHT_CONFIG_MAX_HOSTS; ++index)
    {
        char address[MOONLIGHT_CONFIG_ADDRESS_SIZE]{};
        std::snprintf(address, sizeof(address), "192.168.1.%u", index + 1);
        ASSERT_EQ(moonlight_config_upsert_host(&config, address, 0, "PC", "", nullptr, false),
                  static_cast<int>(index));
    }

    EXPECT_EQ(moonlight_config_upsert_host(&config, "192.168.1.99", 0, "Extra", "", nullptr, false),
              -1);
    EXPECT_EQ(config.host_count, MOONLIGHT_CONFIG_MAX_HOSTS);
}

TEST(Configuration, RemovingSelectedHostCompactsAndKeepsTheOtherPC)
{
    moonlight_config_t config{};
    moonlight_config_defaults(&config);
    ASSERT_EQ(
        moonlight_config_upsert_host(&config, "192.168.1.10", 0, "Old PC", "old", nullptr, false),
        0);
    ASSERT_EQ(moonlight_config_upsert_host(&config, "192.168.1.20", 0, "Other PC", "other", nullptr,
                                           true),
              1);
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

// Builds mDNS replies the way a responder does: the PTR answer names the
// instance and later records refer back to earlier names by offset.
struct MdnsPacket
{
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(12, 0);
    int service_offset = -1;

    MdnsPacket()
    {
        bytes[2] = 0x84; // response, authoritative
    }
    void U16(unsigned value)
    {
        bytes.push_back(static_cast<std::uint8_t>(value >> 8));
        bytes.push_back(static_cast<std::uint8_t>(value));
    }
    void Pointer(int offset)
    {
        U16(0xc000U | static_cast<unsigned>(offset));
    }
    // Writes the labels, then the end of the name or a pointer to its remainder.
    std::size_t Name(const std::string &name, int pointer = -1)
    {
        const std::size_t offset = bytes.size();
        std::size_t start = 0;
        while (start < name.size())
        {
            std::size_t end = name.find('.', start);
            if (end == std::string::npos)
                end = name.size();
            bytes.push_back(static_cast<std::uint8_t>(end - start));
            bytes.insert(bytes.end(), name.begin() + static_cast<std::ptrdiff_t>(start),
                         name.begin() + static_cast<std::ptrdiff_t>(end));
            start = end + 1;
        }
        if (pointer >= 0)
            Pointer(pointer);
        else
            bytes.push_back(0);
        return offset;
    }
    // Type, class, TTL and a length to fill in; returns where the length is.
    std::size_t Record(unsigned type)
    {
        U16(type);
        U16(1); // class IN
        U16(0); // TTL, high half
        U16(120);
        ++bytes[type == 12 ? 7 : 11]; // PTR is an answer, the rest are additional
        U16(0);
        return bytes.size() - 2;
    }
    void Close(std::size_t length_at)
    {
        const std::size_t length = bytes.size() - length_at - 2;
        bytes[length_at] = static_cast<std::uint8_t>(length >> 8);
        bytes[length_at + 1] = static_cast<std::uint8_t>(length);
    }
    // PTR _nvstream._tcp.local -> <instance>._nvstream._tcp.local; returns the instance offset.
    int Ptr(const std::string &instance)
    {
        if (service_offset < 0)
            service_offset = static_cast<int>(Name("_nvstream._tcp.local"));
        else
            Pointer(service_offset);
        const std::size_t length_at = Record(12);
        const std::size_t target = Name(instance, service_offset);
        Close(length_at);
        return static_cast<int>(target);
    }
    void SrvBody(unsigned port)
    {
        const std::size_t length_at = Record(33);
        U16(0); // priority
        U16(0); // weight
        U16(port);
        Name("host.local");
        Close(length_at);
    }
    // The port record of an instance named earlier in the packet...
    void Srv(int instance_offset, unsigned port)
    {
        Pointer(instance_offset);
        SrvBody(port);
    }
    // ...or of one spelled out in full.
    void Srv(const std::string &owner, unsigned port)
    {
        Name(owner);
        SrvBody(port);
    }
};

TEST(Discovery, ReadsTheAdvertisedPortOfAnInstance)
{
    MdnsPacket packet;
    packet.Srv(packet.Ptr("GAMING-PC"), 48989);
    moonlight_discovered_service_t services[MOONLIGHT_DISCOVERY_MAX_SERVICES]{};

    ASSERT_EQ(moonlight_discovery_parse_response(packet.bytes.data(), packet.bytes.size(), services,
                                                 MOONLIGHT_DISCOVERY_MAX_SERVICES),
              1U);
    EXPECT_STREQ(services[0].name, "GAMING-PC");
    EXPECT_STREQ(services[0].instance, "GAMING-PC._nvstream._tcp.local");
    EXPECT_EQ(services[0].http_port, 48989U);
}

TEST(Discovery, AReplyWithoutAPortRecordLeavesThePortUnknown)
{
    MdnsPacket packet;
    packet.Ptr("GAMING-PC");
    moonlight_discovered_service_t services[MOONLIGHT_DISCOVERY_MAX_SERVICES]{};

    ASSERT_EQ(moonlight_discovery_parse_response(packet.bytes.data(), packet.bytes.size(), services,
                                                 MOONLIGHT_DISCOVERY_MAX_SERVICES),
              1U);
    EXPECT_STREQ(services[0].name, "GAMING-PC");
    EXPECT_EQ(services[0].http_port, 0U);
}

TEST(Discovery, SeparatesTwoInstancesOfOnePc)
{
    MdnsPacket packet;
    // Port records may come first and spell their owner in another case.
    packet.Srv("tv._NVSTREAM._tcp.LOCAL", 48989);
    packet.Srv("printer._ipp._tcp.local", 631);
    const int desk = packet.Ptr("Desk");
    packet.Ptr("TV");
    packet.Srv(desk, 47989);
    moonlight_discovered_service_t services[MOONLIGHT_DISCOVERY_MAX_SERVICES]{};

    const std::uint32_t count = moonlight_discovery_parse_response(
        packet.bytes.data(), packet.bytes.size(), services, MOONLIGHT_DISCOVERY_MAX_SERVICES);
    ASSERT_EQ(count, 2U);
    EXPECT_STREQ(services[0].name, "tv");
    EXPECT_EQ(services[0].http_port, 48989U);
    EXPECT_STREQ(services[1].name, "Desk");
    EXPECT_EQ(services[1].http_port, 47989U);
}

TEST(Discovery, RejectsQueriesAndSurvivesTruncatedReplies)
{
    MdnsPacket packet;
    packet.Srv(packet.Ptr("GAMING-PC"), 48989);
    moonlight_discovered_service_t services[MOONLIGHT_DISCOVERY_MAX_SERVICES]{};

    for (std::size_t length = 0; length < packet.bytes.size(); ++length)
    {
        const std::uint32_t count = moonlight_discovery_parse_response(
            packet.bytes.data(), length, services, MOONLIGHT_DISCOVERY_MAX_SERVICES);
        EXPECT_LE(count, 1U) << length;
        if (count)
            EXPECT_EQ(services[0].http_port, 0U) << length;
    }
    packet.bytes[2] = 0; // a query, not a reply
    EXPECT_EQ(moonlight_discovery_parse_response(packet.bytes.data(), packet.bytes.size(), services,
                                                 MOONLIGHT_DISCOVERY_MAX_SERVICES),
              0U);
    EXPECT_EQ(moonlight_discovery_parse_response(nullptr, 64, services, 1), 0U);
}

TEST(Discovery, ListsEveryEndpointOnceWithItsPort)
{
    MdnsPacket plain;
    plain.Ptr("Desk");
    MdnsPacket with_port;
    with_port.Srv(with_port.Ptr("Desk"), 48989);
    MdnsPacket second;
    second.Srv(second.Ptr("TV"), 50000);
    MdnsPacket other;
    other.Ptr("Laptop");

    // The first PC answers twice (the port arrives late) and runs two instances.
    mdns_replies = {{plain.bytes, {192, 168, 1, 10}},
                    {with_port.bytes, {192, 168, 1, 10}},
                    {second.bytes, {192, 168, 1, 10}},
                    {with_port.bytes, {192, 168, 1, 10}},
                    {other.bytes, {192, 168, 1, 30}}};
    moonlight_discovered_host_t hosts[MOONLIGHT_DISCOVERY_MAX_HOSTS]{};

    ASSERT_EQ(moonlight_discover_hosts(hosts, MOONLIGHT_DISCOVERY_MAX_HOSTS), 3U);
    EXPECT_TRUE(mdns_replies.empty());
    EXPECT_STREQ(hosts[0].address, "192.168.1.10");
    EXPECT_STREQ(hosts[0].name, "Desk");
    EXPECT_EQ(hosts[0].http_port, 48989U);
    EXPECT_STREQ(hosts[1].address, "192.168.1.10");
    EXPECT_STREQ(hosts[1].name, "TV");
    EXPECT_EQ(hosts[1].http_port, 50000U);
    EXPECT_STREQ(hosts[2].address, "192.168.1.30");
    EXPECT_STREQ(hosts[2].name, "Laptop");
    EXPECT_EQ(hosts[2].http_port, 0U);
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

namespace
{

std::vector<std::uint8_t> picture_of(std::uint8_t red, std::uint8_t green, std::uint8_t blue)
{
    std::vector<std::uint8_t> picture(static_cast<std::size_t>(connecting::kWidth) *
                                      connecting::kHeight * 4u);
    for (std::size_t at = 0; at < picture.size(); at += 4)
    {
        picture[at] = red;
        picture[at + 1] = green;
        picture[at + 2] = blue;
        picture[at + 3] = 255;
    }
    return picture;
}

constexpr std::size_t kPlateLuma =
    static_cast<std::size_t>(connecting::kWidth) * connecting::kSurfaceHeight;

} // namespace

TEST(ConnectingScreen, PictureBecomesLimitedRangeVideo)
{
    const std::vector<std::uint8_t> white = picture_of(255, 255, 255);
    connecting::Plate plate;
    std::vector<std::uint8_t> surface(connecting::surface_bytes(false));

    plate.build(white.data(), {}, false);
    EXPECT_FALSE(plate.has_bar());
    plate.compose(surface.data(), 0.0f, 1.0f);
    EXPECT_EQ(surface[0], 235);
    EXPECT_EQ(surface[kPlateLuma], 128);
    // The rows below the picture are padding: black.
    EXPECT_EQ(surface[static_cast<std::size_t>(connecting::kHeight) * connecting::kWidth], 16);

    // Without a picture the plate is a black screen.
    plate.build(nullptr, {}, false);
    plate.compose(surface.data(), 1.0f, 1.0f);
    EXPECT_EQ(surface[0], 16);
    EXPECT_EQ(surface[kPlateLuma + 1], 128);
}

TEST(ConnectingScreen, BarFillsFromTheLeftAndNeverPastItsProgress)
{
    const std::vector<std::uint8_t> dark = picture_of(10, 10, 10);
    connecting::Bar bar;
    bar.x = 96.0f;
    bar.y = 974.0f;
    bar.width = 1728.0f;
    bar.height = 10.0f;
    connecting::Plate plate;
    std::vector<std::uint8_t> whole(connecting::surface_bytes(false));
    std::vector<std::uint8_t> rows(connecting::surface_bytes(false));
    const auto luma = [](const std::vector<std::uint8_t> &surface, int x)
    { return surface[979u * connecting::kWidth + static_cast<std::size_t>(x)]; };

    plate.build(dark.data(), bar, false);
    ASSERT_TRUE(plate.has_bar());
    plate.compose(whole.data(), 0.5f, 1.0f);
    EXPECT_EQ(luma(whole, 500), 235); // inside the fill
    EXPECT_LT(luma(whole, 1200), 30); // beyond it: the picture
    EXPECT_LT(luma(whole, 90), 30);   // left of the bar
    // Redrawing only the bar's rows gives the same picture, and says what it wrote.
    plate.compose(rows.data(), 0.2f, 1.0f);
    connecting::Range written[2];
    plate.compose_bar(rows.data(), 0.5f, written);
    EXPECT_EQ(rows, whole);
    EXPECT_GT(written[0].bytes, 0u);
    EXPECT_LT(written[0].bytes, 40u * connecting::kWidth);
    EXPECT_GE(written[1].offset, kPlateLuma);
    // Nothing is filled at zero.
    plate.compose(whole.data(), 0.0f, 1.0f);
    EXPECT_LT(luma(whole, 100), 30);
}

TEST(ConnectingScreen, FadesToVideoBlack)
{
    const std::vector<std::uint8_t> colour = picture_of(200, 40, 90);
    connecting::Bar bar;
    bar.x = 96.0f;
    bar.y = 974.0f;
    bar.width = 1728.0f;
    bar.height = 10.0f;
    connecting::Plate plate;
    std::vector<std::uint8_t> surface(connecting::surface_bytes(false));

    plate.build(colour.data(), bar, false);
    plate.compose(surface.data(), 1.0f, 1.0f);
    const int bright = surface[0];
    plate.compose(surface.data(), 1.0f, 0.5f);
    EXPECT_LT(surface[0], bright);
    EXPECT_GT(surface[0], 16);
    plate.compose(surface.data(), 1.0f, 0.0f);
    for (std::size_t at = 0; at < surface.size(); at += 977)
        EXPECT_EQ(surface[at], at < kPlateLuma ? 16 : 128) << at;
}

TEST(ConnectingScreen, HdrPlateUsesTenBitPqLevels)
{
    const std::vector<std::uint8_t> white = picture_of(255, 255, 255);
    connecting::Plate plate;
    std::vector<std::uint16_t> surface(connecting::surface_bytes(true) / 2u);

    plate.build(white.data(), {}, true);
    EXPECT_TRUE(plate.hdr());
    plate.compose(surface.data(), 0.0f, 1.0f);
    // White at 203 nits is 58% of the PQ signal: far from the top of the range.
    EXPECT_GT(surface[0], 555);
    EXPECT_LT(surface[0], 590);
    EXPECT_EQ(surface[kPlateLuma], 512);
    plate.compose(surface.data(), 0.0f, 0.0f);
    EXPECT_EQ(surface[0], 64);
}

TEST(WakeOnLan, ParsesLowercaseColonMac)
{
    std::uint8_t parsed[MOONLIGHT_WOL_MAC_BYTES] = {};
    ASSERT_EQ(moonlight_wol_parse_mac("02:ab:cd:ef:12:34", parsed), 1);
    const std::uint8_t expected[] = {0x02, 0xab, 0xcd, 0xef, 0x12, 0x34};
    for (std::size_t index = 0; index < MOONLIGHT_WOL_MAC_BYTES; ++index)
        EXPECT_EQ(parsed[index], expected[index]) << index;
}

TEST(WakeOnLan, ParsesUppercaseHyphenMac)
{
    std::uint8_t parsed[MOONLIGHT_WOL_MAC_BYTES] = {};
    ASSERT_EQ(moonlight_wol_parse_mac("02-AB-CD-EF-12-34", parsed), 1);
    const std::uint8_t expected[] = {0x02, 0xab, 0xcd, 0xef, 0x12, 0x34};
    for (std::size_t index = 0; index < MOONLIGHT_WOL_MAC_BYTES; ++index)
        EXPECT_EQ(parsed[index], expected[index]) << index;
}

TEST(WakeOnLan, ParsesUppercaseColonMac)
{
    std::uint8_t parsed[MOONLIGHT_WOL_MAC_BYTES] = {};
    ASSERT_EQ(moonlight_wol_parse_mac("02:AB:CD:EF:12:34", parsed), 1);
    const std::uint8_t expected[] = {0x02, 0xab, 0xcd, 0xef, 0x12, 0x34};
    for (std::size_t index = 0; index < MOONLIGHT_WOL_MAC_BYTES; ++index)
        EXPECT_EQ(parsed[index], expected[index]) << index;
}

TEST(WakeOnLan, RejectsWrongOctetCounts)
{
    std::uint8_t parsed[MOONLIGHT_WOL_MAC_BYTES] = {0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa};
    EXPECT_EQ(moonlight_wol_parse_mac("02:11:22:33:44", parsed), 0);
    EXPECT_EQ(moonlight_wol_parse_mac("02:11:22:33:44:55:66", parsed), 0);
    EXPECT_EQ(moonlight_wol_parse_mac("", parsed), 0);
    EXPECT_EQ(moonlight_wol_parse_mac(nullptr, parsed), 0);
    EXPECT_EQ(moonlight_wol_parse_mac("02:11:22:33:44:55", nullptr), 0);
    for (std::size_t index = 0; index < MOONLIGHT_WOL_MAC_BYTES; ++index)
        EXPECT_EQ(parsed[index], 0xaa) << index;
}

TEST(WakeOnLan, RejectsMixedSeparators)
{
    std::uint8_t parsed[MOONLIGHT_WOL_MAC_BYTES] = {};
    EXPECT_EQ(moonlight_wol_parse_mac("02:11-22:33:44:55", parsed), 0);
    EXPECT_EQ(moonlight_wol_parse_mac("02-11:22-33:44:55", parsed), 0);
}

TEST(WakeOnLan, RejectsBadSeparators)
{
    std::uint8_t parsed[MOONLIGHT_WOL_MAC_BYTES] = {};
    EXPECT_EQ(moonlight_wol_parse_mac("02/11/22/33/44/55", parsed), 0);
    EXPECT_EQ(moonlight_wol_parse_mac("02 11 22 33 44 55", parsed), 0);
    EXPECT_EQ(moonlight_wol_parse_mac("02,11,22,33,44,55", parsed), 0);
}

TEST(WakeOnLan, RejectsBadHex)
{
    std::uint8_t parsed[MOONLIGHT_WOL_MAC_BYTES] = {};
    EXPECT_EQ(moonlight_wol_parse_mac("02:11:22:33:44:GG", parsed), 0);
    EXPECT_EQ(moonlight_wol_parse_mac("02:11:22:33:44:5", parsed), 0);
    EXPECT_EQ(moonlight_wol_parse_mac("02:11:22:33:44:5G", parsed), 0);
}

TEST(WakeOnLan, RejectsLeadingAndTrailingJunk)
{
    std::uint8_t parsed[MOONLIGHT_WOL_MAC_BYTES] = {};
    EXPECT_EQ(moonlight_wol_parse_mac("x02:11:22:33:44:55", parsed), 0);
    EXPECT_EQ(moonlight_wol_parse_mac("02:11:22:33:44:55x", parsed), 0);
    EXPECT_EQ(moonlight_wol_parse_mac("02:11:22:33:44:55 ", parsed), 0);
}

TEST(WakeOnLan, RejectsAllZeroBroadcastAndMulticast)
{
    std::uint8_t parsed[MOONLIGHT_WOL_MAC_BYTES] = {};
    EXPECT_EQ(moonlight_wol_parse_mac("00:00:00:00:00:00", parsed), 0);
    EXPECT_EQ(moonlight_wol_parse_mac("ff:ff:ff:ff:ff:ff", parsed), 0);
    EXPECT_EQ(moonlight_wol_parse_mac("01:00:00:00:00:01", parsed), 0);
    EXPECT_EQ(moonlight_wol_parse_mac("03:00:00:00:00:01", parsed), 0);
}

TEST(WakeOnLan, AcceptsLocallyAdministeredUnicast)
{
    std::uint8_t parsed[MOONLIGHT_WOL_MAC_BYTES] = {};
    ASSERT_EQ(moonlight_wol_parse_mac("02:00:00:00:00:01", parsed), 1);
    EXPECT_EQ(moonlight_wol_mac_is_valid(parsed), 1);
    EXPECT_EQ(parsed[0], 0x02);
    EXPECT_EQ(parsed[5], 0x01);
}

TEST(WakeOnLan, BinaryValidityRejectsMalformedValues)
{
    const std::uint8_t locally_administered[] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    const std::uint8_t global_unicast[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    const std::uint8_t all_zero[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    const std::uint8_t broadcast[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    const std::uint8_t multicast[] = {0x01, 0x11, 0x22, 0x33, 0x44, 0x55};
    EXPECT_EQ(moonlight_wol_mac_is_valid(locally_administered), 1);
    EXPECT_EQ(moonlight_wol_mac_is_valid(global_unicast), 1);
    EXPECT_EQ(moonlight_wol_mac_is_valid(all_zero), 0);
    EXPECT_EQ(moonlight_wol_mac_is_valid(broadcast), 0);
    EXPECT_EQ(moonlight_wol_mac_is_valid(multicast), 0);
    EXPECT_EQ(moonlight_wol_mac_is_valid(nullptr), 0);
}

TEST(WakeOnLan, BuildsExact102BytePacket)
{
    const std::uint8_t mac[] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    std::uint8_t packet[MOONLIGHT_WOL_PACKET_BYTES] = {};
    ASSERT_EQ(MOONLIGHT_WOL_PACKET_BYTES, 102U);
    ASSERT_EQ(moonlight_wol_build_packet(mac, packet), 1);
    for (std::size_t index = 0; index < MOONLIGHT_WOL_MAC_BYTES; ++index)
        EXPECT_EQ(packet[index], 0xff) << index;
    for (std::size_t repetition = 0; repetition < 16U; ++repetition)
    {
        for (std::size_t index = 0; index < MOONLIGHT_WOL_MAC_BYTES; ++index)
            EXPECT_EQ(
                packet[MOONLIGHT_WOL_MAC_BYTES + repetition * MOONLIGHT_WOL_MAC_BYTES + index],
                mac[index])
                << repetition << index;
    }
}

TEST(WakeOnLan, RejectsInvalidMacForPacket)
{
    const std::uint8_t invalid[] = {0x01, 0x11, 0x22, 0x33, 0x44, 0x55};
    std::uint8_t packet[MOONLIGHT_WOL_PACKET_BYTES];
    for (std::size_t index = 0; index < MOONLIGHT_WOL_PACKET_BYTES; ++index)
        packet[index] = 0xaa;
    EXPECT_EQ(moonlight_wol_build_packet(invalid, packet), 0);
    for (std::size_t index = 0; index < MOONLIGHT_WOL_PACKET_BYTES; ++index)
        EXPECT_EQ(packet[index], 0xaa) << index;
    EXPECT_EQ(moonlight_wol_build_packet(nullptr, packet), 0);
    EXPECT_EQ(moonlight_wol_build_packet(invalid, nullptr), 0);
}

TEST(WakeOnLan, DerivesDefaultPorts)
{
    std::uint16_t ports[MOONLIGHT_WOL_MAX_PORTS] = {};
    const std::uint16_t expected[] = {9U, 47009U, 47998U, 47999U, 48000U, 48002U, 48010U};
    const std::size_t count =
        moonlight_wol_derive_ports(MOONLIGHT_WOL_DEFAULT_HTTP_PORT, ports, MOONLIGHT_WOL_MAX_PORTS);
    ASSERT_EQ(count, 7U);
    for (std::size_t index = 0; index < count; ++index)
    {
        EXPECT_EQ(ports[index], expected[index]) << index;
        EXPECT_NE(ports[index], 7U);
    }
}

TEST(WakeOnLan, DerivesCustomPorts)
{
    std::uint16_t ports[MOONLIGHT_WOL_MAX_PORTS] = {};
    const std::uint16_t expected[] = {9U, 47009U, 48009U, 48010U, 48011U, 48013U, 48021U};
    const std::size_t count = moonlight_wol_derive_ports(48000U, ports, MOONLIGHT_WOL_MAX_PORTS);
    ASSERT_EQ(count, 7U);
    for (std::size_t index = 0; index < count; ++index)
        EXPECT_EQ(ports[index], expected[index]) << index;
}

TEST(WakeOnLan, DeduplicatesPortCollisions)
{
    std::uint16_t ports[MOONLIGHT_WOL_MAX_PORTS] = {};
    const std::uint16_t expected[] = {9U, 47009U, 10U, 11U, 13U, 21U};
    const std::size_t count = moonlight_wol_derive_ports(0U, ports, MOONLIGHT_WOL_MAX_PORTS);
    ASSERT_EQ(count, 6U);
    for (std::size_t index = 0; index < count; ++index)
        EXPECT_EQ(ports[index], expected[index]) << index;

    const std::uint16_t duplicate_static[] = {9U, 47009U, 47010U, 47011U, 47013U, 47021U};
    const std::size_t duplicate_count =
        moonlight_wol_derive_ports(47000U, ports, MOONLIGHT_WOL_MAX_PORTS);
    ASSERT_EQ(duplicate_count, 6U);
    for (std::size_t index = 0; index < duplicate_count; ++index)
        EXPECT_EQ(ports[index], duplicate_static[index]) << index;
}

TEST(WakeOnLan, SkipsOverflowingPorts)
{
    std::uint16_t ports[MOONLIGHT_WOL_MAX_PORTS] = {};
    const std::uint16_t overflow_expected[] = {9U, 47009U};
    const std::size_t overflow_count =
        moonlight_wol_derive_ports(65535U, ports, MOONLIGHT_WOL_MAX_PORTS);
    ASSERT_EQ(overflow_count, 2U);
    EXPECT_EQ(ports[0], overflow_expected[0]);
    EXPECT_EQ(ports[1], overflow_expected[1]);

    const std::uint16_t boundary_expected[] = {9U, 47009U, 65523U, 65524U, 65525U, 65527U, 65535U};
    const std::size_t boundary_count =
        moonlight_wol_derive_ports(65514U, ports, MOONLIGHT_WOL_MAX_PORTS);
    ASSERT_EQ(boundary_count, 7U);
    for (std::size_t index = 0; index < boundary_count; ++index)
        EXPECT_EQ(ports[index], boundary_expected[index]) << index;

    const std::size_t skipped_count =
        moonlight_wol_derive_ports(65515U, ports, MOONLIGHT_WOL_MAX_PORTS);
    ASSERT_EQ(skipped_count, 6U);
    EXPECT_EQ(ports[5], 65528U);
}

TEST(WakeOnLan, BoundsPortOutputByCapacity)
{
    std::uint16_t ports[MOONLIGHT_WOL_MAX_PORTS];
    for (std::size_t index = 0; index < MOONLIGHT_WOL_MAX_PORTS; ++index)
        ports[index] = 0xaaaa;
    const std::size_t count =
        moonlight_wol_derive_ports(MOONLIGHT_WOL_DEFAULT_HTTP_PORT, ports, 3U);
    ASSERT_EQ(count, 7U);
    EXPECT_EQ(ports[0], 9U);
    EXPECT_EQ(ports[1], 47009U);
    EXPECT_EQ(ports[2], 47998U);
    for (std::size_t index = 3; index < MOONLIGHT_WOL_MAX_PORTS; ++index)
        EXPECT_EQ(ports[index], 0xaaaa) << index;
    EXPECT_EQ(moonlight_wol_derive_ports(MOONLIGHT_WOL_DEFAULT_HTTP_PORT, nullptr,
                                         MOONLIGHT_WOL_MAX_PORTS),
              7U);
}

TEST(WakeOnLan, PlansLimitedBroadcastFallbackWithoutInputs)
{
    moonlight_wol_target_plan plan{};
    ASSERT_EQ(moonlight_wol_plan_targets(nullptr, 0U, nullptr, 0U, MOONLIGHT_WOL_DEFAULT_HTTP_PORT,
                                         &plan),
              1);
    ASSERT_EQ(plan.address_count, 1U);
    EXPECT_EQ(plan.addresses[0], 0xFFFFFFFFU);
    const std::uint16_t expected_ports[] = {9U, 47009U, 47998U, 47999U, 48000U, 48002U, 48010U};
    ASSERT_EQ(plan.port_count, 7U);
    for (std::size_t index = 0; index < plan.port_count; ++index)
        EXPECT_EQ(plan.ports[index], expected_ports[index]) << index;
}

TEST(WakeOnLan, DeduplicatesPlanAddressesAndPorts)
{
    const std::uint32_t required[] = {0xC000020AU, 0xC000020AU, 0xC6336414U};
    const std::uint32_t interfaces[] = {0xCB00711EU, 0xC000020AU, 0xCB00711EU};
    moonlight_wol_target_plan plan{};
    ASSERT_EQ(moonlight_wol_plan_targets(required, 3U, interfaces, 3U, 48000U, &plan), 1);
    ASSERT_EQ(plan.address_count, 4U);
    EXPECT_EQ(plan.addresses[0], 0xFFFFFFFFU);
    EXPECT_EQ(plan.addresses[1], 0xC000020AU);
    EXPECT_EQ(plan.addresses[2], 0xC6336414U);
    EXPECT_EQ(plan.addresses[3], 0xCB00711EU);
    const std::uint16_t expected_ports[] = {9U, 47009U, 48009U, 48010U, 48011U, 48013U, 48021U};
    ASSERT_EQ(plan.port_count, 7U);
    for (std::size_t index = 0; index < plan.port_count; ++index)
        EXPECT_EQ(plan.ports[index], expected_ports[index]) << index;
}

TEST(WakeOnLan, RejectsInvalidTargetPlanInputs)
{
    moonlight_wol_target_plan plan{};
    EXPECT_EQ(moonlight_wol_plan_targets(nullptr, 1U, nullptr, 0U, MOONLIGHT_WOL_DEFAULT_HTTP_PORT,
                                         &plan),
              0);
    EXPECT_EQ(moonlight_wol_plan_targets(nullptr, 0U, nullptr, 1U, MOONLIGHT_WOL_DEFAULT_HTTP_PORT,
                                         &plan),
              0);
    EXPECT_EQ(moonlight_wol_plan_targets(nullptr, 0U, nullptr, 0U, MOONLIGHT_WOL_DEFAULT_HTTP_PORT,
                                         nullptr),
              0);
}

namespace wol_test
{
struct SentDatagram
{
    std::uint32_t address = 0;
    std::uint16_t port = 0;
    std::uint8_t packet[MOONLIGHT_WOL_PACKET_BYTES] = {};
};

struct FakeTransport
{
    std::uint32_t resolved[MOONLIGHT_WOL_MAX_ADDRESSES] = {};
    std::size_t resolved_count = 0;
    bool resolve_success = true;
    bool resolver_called = false;
    std::string requested_host;

    std::uint32_t interfaces[MOONLIGHT_WOL_MAX_ADDRESSES] = {};
    std::size_t interface_count = 0;
    bool enumerate_success = true;
    bool enumerate_called = false;

    int socket_value = 7;
    bool socket_success = true;
    bool open_called = false;

    bool broadcast_success = true;
    int broadcast_calls = 0;

    std::size_t fail_from = static_cast<std::size_t>(-1);
    std::size_t short_from = static_cast<std::size_t>(-1);
    std::vector<SentDatagram> sent;

    bool close_success = true;
    int close_calls = 0;
};

static int resolve_ipv4(void *context, const char *host, std::uint32_t *out_addresses,
                        std::size_t capacity, std::size_t *out_count)
{
    auto *fake = static_cast<FakeTransport *>(context);
    fake->resolver_called = true;
    fake->requested_host = host == nullptr ? "" : host;
    if (!fake->resolve_success)
        return 0;
    const std::size_t copied = fake->resolved_count < capacity ? fake->resolved_count : capacity;
    for (std::size_t index = 0; index < copied; ++index)
        out_addresses[index] = fake->resolved[index];
    *out_count = copied;
    return 1;
}

static int enumerate_interface_broadcasts(void *context, std::uint32_t *out_addresses,
                                          std::size_t capacity, std::size_t *out_count)
{
    auto *fake = static_cast<FakeTransport *>(context);
    fake->enumerate_called = true;
    if (!fake->enumerate_success)
        return 0;
    const std::size_t copied = fake->interface_count < capacity ? fake->interface_count : capacity;
    for (std::size_t index = 0; index < copied; ++index)
        out_addresses[index] = fake->interfaces[index];
    *out_count = copied;
    return 1;
}

static int open_udp_socket(void *context)
{
    auto *fake = static_cast<FakeTransport *>(context);
    fake->open_called = true;
    return fake->socket_success ? fake->socket_value : -1;
}

static int enable_broadcast(void *context, int socket_id)
{
    (void)socket_id;
    auto *fake = static_cast<FakeTransport *>(context);
    ++fake->broadcast_calls;
    return fake->broadcast_success ? 0 : -1;
}

static int send_datagram(void *context, int socket_id, std::uint32_t address, std::uint16_t port,
                         const std::uint8_t *packet, std::size_t packet_bytes)
{
    (void)socket_id;
    auto *fake = static_cast<FakeTransport *>(context);
    const std::size_t index = fake->sent.size();
    if (index >= fake->fail_from)
        return -1;
    if (index >= fake->short_from)
        return MOONLIGHT_WOL_PACKET_BYTES - 1;
    if (packet == nullptr || packet_bytes != MOONLIGHT_WOL_PACKET_BYTES)
        return -1;
    SentDatagram sent;
    sent.address = address;
    sent.port = port;
    std::memcpy(sent.packet, packet, packet_bytes);
    fake->sent.push_back(sent);
    return static_cast<int>(packet_bytes);
}

static int close_udp_socket(void *context, int socket_id)
{
    (void)socket_id;
    auto *fake = static_cast<FakeTransport *>(context);
    ++fake->close_calls;
    return fake->close_success ? 0 : -1;
}

static moonlight_wol_send_operations operations()
{
    return {resolve_ipv4,    enumerate_interface_broadcasts,
            open_udp_socket, enable_broadcast,
            send_datagram,   close_udp_socket};
}
} // namespace wol_test

TEST(WakeOnLan, SendsLiteralTargetWithBroadcastAndExactPacket)
{
    wol_test::FakeTransport fake;
    fake.interfaces[0] = 0xCB00711EU;
    fake.interfaces[1] = 0xCB00711EU;
    fake.interface_count = 2U;
    const std::uint8_t mac[] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    std::uint8_t expected_packet[MOONLIGHT_WOL_PACKET_BYTES] = {};
    ASSERT_EQ(moonlight_wol_build_packet(mac, expected_packet), 1);

    const auto ops = wol_test::operations();
    moonlight_wol_send_result result{};
    ASSERT_EQ(moonlight_wol_send_with_operations(mac, "192.0.2.10", MOONLIGHT_WOL_DEFAULT_HTTP_PORT,
                                                 &ops, &fake, &result),
              1);
    EXPECT_FALSE(fake.resolver_called);
    EXPECT_TRUE(fake.enumerate_called);
    EXPECT_EQ(fake.broadcast_calls, 1);
    EXPECT_EQ(fake.close_calls, 1);
    ASSERT_EQ(result.attempted_datagrams, 21U);
    EXPECT_EQ(result.successful_datagrams, 21U);
    EXPECT_EQ(result.failed_datagrams, 0U);
    EXPECT_EQ(result.dns_failed, 0);
    EXPECT_EQ(result.interface_enumeration_failed, 0);
    EXPECT_EQ(result.socket_failed, 0);
    EXPECT_EQ(result.broadcast_option_failed, 0);
    EXPECT_EQ(result.close_failed, 0);
    EXPECT_EQ(result.error, MOONLIGHT_WOL_SEND_ERROR_NONE);
    ASSERT_EQ(fake.sent.size(), 21U);

    const std::uint32_t expected_addresses[] = {0xFFFFFFFFU, 0xC000020AU, 0xCB00711EU};
    const std::uint16_t expected_ports[] = {9U, 47009U, 47998U, 47999U, 48000U, 48002U, 48010U};
    for (std::size_t address_index = 0; address_index < 3U; ++address_index)
    {
        for (std::size_t port_index = 0; port_index < 7U; ++port_index)
        {
            const std::size_t index = address_index * 7U + port_index;
            EXPECT_EQ(fake.sent[index].address, expected_addresses[address_index]);
            EXPECT_EQ(fake.sent[index].port, expected_ports[port_index]);
            EXPECT_EQ(
                std::memcmp(fake.sent[index].packet, expected_packet, MOONLIGHT_WOL_PACKET_BYTES),
                0);
        }
    }
}

TEST(WakeOnLan, SendsHostnameTargetViaResolver)
{
    wol_test::FakeTransport fake;
    fake.resolved[0] = 0xC6336414U;
    fake.resolved_count = 1U;
    const std::uint8_t mac[] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    const auto ops = wol_test::operations();
    moonlight_wol_send_result result{};
    ASSERT_EQ(moonlight_wol_send_with_operations(
                  mac, "media.example", MOONLIGHT_WOL_DEFAULT_HTTP_PORT, &ops, &fake, &result),
              1);
    EXPECT_TRUE(fake.resolver_called);
    EXPECT_EQ(fake.requested_host, "media.example");
    EXPECT_EQ(result.attempted_datagrams, 14U);
    EXPECT_EQ(result.successful_datagrams, 14U);
    EXPECT_EQ(result.error, MOONLIGHT_WOL_SEND_ERROR_NONE);
    EXPECT_EQ(fake.close_calls, 1);
}

TEST(WakeOnLan, DnsFailureStillSendsLimitedBroadcast)
{
    wol_test::FakeTransport fake;
    fake.resolve_success = false;
    const std::uint8_t mac[] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    const auto ops = wol_test::operations();
    moonlight_wol_send_result result{};
    ASSERT_EQ(moonlight_wol_send_with_operations(mac, "unresolvable.example",
                                                 MOONLIGHT_WOL_DEFAULT_HTTP_PORT, &ops, &fake,
                                                 &result),
              1);
    EXPECT_EQ(result.attempted_datagrams, 7U);
    EXPECT_EQ(result.successful_datagrams, 7U);
    EXPECT_EQ(result.dns_failed, 1);
    EXPECT_EQ(result.error, MOONLIGHT_WOL_SEND_ERROR_PARTIAL);
    EXPECT_EQ(fake.close_calls, 1);
    ASSERT_EQ(fake.sent.size(), 7U);
    const std::uint16_t expected_ports[] = {9U, 47009U, 47998U, 47999U, 48000U, 48002U, 48010U};
    for (std::size_t index = 0; index < fake.sent.size(); ++index)
    {
        EXPECT_EQ(fake.sent[index].address, 0xFFFFFFFFU);
        EXPECT_EQ(fake.sent[index].port, expected_ports[index]);
    }
}

TEST(WakeOnLan, SenderSocketFailureClosesNothing)
{
    wol_test::FakeTransport fake;
    fake.socket_success = false;
    const std::uint8_t mac[] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    const auto ops = wol_test::operations();
    moonlight_wol_send_result result{};
    EXPECT_EQ(moonlight_wol_send_with_operations(mac, nullptr, MOONLIGHT_WOL_DEFAULT_HTTP_PORT,
                                                 &ops, &fake, &result),
              0);
    EXPECT_TRUE(fake.open_called);
    EXPECT_EQ(fake.close_calls, 0);
    EXPECT_TRUE(fake.sent.empty());
    EXPECT_EQ(result.attempted_datagrams, 0U);
    EXPECT_EQ(result.socket_failed, 1);
    EXPECT_EQ(result.error, MOONLIGHT_WOL_SEND_ERROR_SOCKET);
}

TEST(WakeOnLan, SenderBroadcastOptionFailureClosesSocket)
{
    wol_test::FakeTransport fake;
    fake.broadcast_success = false;
    const std::uint8_t mac[] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    const auto ops = wol_test::operations();
    moonlight_wol_send_result result{};
    EXPECT_EQ(moonlight_wol_send_with_operations(mac, nullptr, MOONLIGHT_WOL_DEFAULT_HTTP_PORT,
                                                 &ops, &fake, &result),
              0);
    EXPECT_EQ(fake.close_calls, 1);
    EXPECT_TRUE(fake.sent.empty());
    EXPECT_EQ(result.attempted_datagrams, 0U);
    EXPECT_EQ(result.broadcast_option_failed, 1);
    EXPECT_EQ(result.error, MOONLIGHT_WOL_SEND_ERROR_BROADCAST_OPTION);
}

TEST(WakeOnLan, SenderPartialSuccessKeepsDiagnostics)
{
    wol_test::FakeTransport fake;
    fake.fail_from = 5U;
    const std::uint8_t mac[] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    const auto ops = wol_test::operations();
    moonlight_wol_send_result result{};
    EXPECT_EQ(moonlight_wol_send_with_operations(mac, nullptr, MOONLIGHT_WOL_DEFAULT_HTTP_PORT,
                                                 &ops, &fake, &result),
              1);
    EXPECT_EQ(result.attempted_datagrams, 7U);
    EXPECT_EQ(result.successful_datagrams, 5U);
    EXPECT_EQ(result.failed_datagrams, 2U);
    EXPECT_EQ(result.error, MOONLIGHT_WOL_SEND_ERROR_PARTIAL);
    EXPECT_EQ(fake.close_calls, 1);
}

TEST(WakeOnLan, SenderAllSendFailureIsFailure)
{
    wol_test::FakeTransport fake;
    fake.fail_from = 0U;
    const std::uint8_t mac[] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    const auto ops = wol_test::operations();
    moonlight_wol_send_result result{};
    EXPECT_EQ(moonlight_wol_send_with_operations(mac, nullptr, MOONLIGHT_WOL_DEFAULT_HTTP_PORT,
                                                 &ops, &fake, &result),
              0);
    EXPECT_EQ(result.attempted_datagrams, 7U);
    EXPECT_EQ(result.successful_datagrams, 0U);
    EXPECT_EQ(result.failed_datagrams, 7U);
    EXPECT_EQ(result.error, MOONLIGHT_WOL_SEND_ERROR_ALL_DATAGRAMS_FAILED);
    EXPECT_EQ(fake.close_calls, 1);
}

TEST(WakeOnLan, SenderShortSendIsNotSuccess)
{
    wol_test::FakeTransport fake;
    fake.short_from = 0U;
    const std::uint8_t mac[] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    const auto ops = wol_test::operations();
    moonlight_wol_send_result result{};
    EXPECT_EQ(moonlight_wol_send_with_operations(mac, nullptr, MOONLIGHT_WOL_DEFAULT_HTTP_PORT,
                                                 &ops, &fake, &result),
              0);
    EXPECT_EQ(result.attempted_datagrams, 7U);
    EXPECT_EQ(result.successful_datagrams, 0U);
    EXPECT_EQ(result.failed_datagrams, 7U);
    EXPECT_EQ(result.error, MOONLIGHT_WOL_SEND_ERROR_ALL_DATAGRAMS_FAILED);
    EXPECT_EQ(fake.close_calls, 1);
}

TEST(WakeOnLan, SenderRejectsInvalidMacBeforeSocket)
{
    wol_test::FakeTransport fake;
    const std::uint8_t invalid[] = {0x01, 0x11, 0x22, 0x33, 0x44, 0x55};
    const auto ops = wol_test::operations();
    moonlight_wol_send_result result{};
    EXPECT_EQ(moonlight_wol_send_with_operations(invalid, nullptr, MOONLIGHT_WOL_DEFAULT_HTTP_PORT,
                                                 &ops, &fake, &result),
              0);
    EXPECT_FALSE(fake.open_called);
    EXPECT_EQ(fake.close_calls, 0);
    EXPECT_EQ(result.attempted_datagrams, 0U);
    EXPECT_EQ(result.error, MOONLIGHT_WOL_SEND_ERROR_INVALID_MAC);
}

TEST(WakeOnLan, SenderCloseFailureIsPartialDiagnostic)
{
    wol_test::FakeTransport fake;
    fake.close_success = false;
    const std::uint8_t mac[] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    const auto ops = wol_test::operations();
    moonlight_wol_send_result result{};
    EXPECT_EQ(moonlight_wol_send_with_operations(mac, nullptr, MOONLIGHT_WOL_DEFAULT_HTTP_PORT,
                                                 &ops, &fake, &result),
              1);
    EXPECT_EQ(result.successful_datagrams, 7U);
    EXPECT_EQ(result.close_failed, 1);
    EXPECT_EQ(result.error, MOONLIGHT_WOL_SEND_ERROR_PARTIAL);
    EXPECT_EQ(fake.close_calls, 1);
}

TEST(WakeOnLan, SenderInterfaceFailureKeepsBroadcast)
{
    wol_test::FakeTransport fake;
    fake.enumerate_success = false;
    const std::uint8_t mac[] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    const auto ops = wol_test::operations();
    moonlight_wol_send_result result{};
    EXPECT_EQ(moonlight_wol_send_with_operations(mac, nullptr, MOONLIGHT_WOL_DEFAULT_HTTP_PORT,
                                                 &ops, &fake, &result),
              1);
    EXPECT_EQ(result.attempted_datagrams, 7U);
    EXPECT_EQ(result.successful_datagrams, 7U);
    EXPECT_EQ(result.interface_enumeration_failed, 1);
    EXPECT_EQ(result.error, MOONLIGHT_WOL_SEND_ERROR_PARTIAL);
}
