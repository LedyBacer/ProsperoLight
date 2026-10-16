/* ps5-native-app-boilerplate - Bit-exact PCM and asynchronous AudioOut checks.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ps5_waveform.hpp"
#include <ControllerHaptics.h>
#include <cassert>
#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>
#include <vector>
#include <array>
#include <cstring>
#include <cstdio>
using namespace prosperolight::waveform;
static std::atomic<bool> fail_open{false}, fail_output{false}, stalled{false}, entered{false};
static std::atomic<int> opens{0}, closes{0}, burst_calls{0};
static std::atomic<bool> batch_driver{false};
static std::atomic<int> channels{2}, stall_handle{0}, blocked_handle{0};
static std::mutex records_mutex;
struct Record
{
    int user;
    std::array<int16_t, Grain * 2> samples;
};
static std::vector<Record> records;
extern "C"
{
    int32_t sceAudioOutInit(void)
    {
        return 0;
    }
    int32_t sceAudioOutOpen(int32_t user, int32_t type, int32_t index, uint32_t grain, uint32_t hz,
                            uint32_t format)
    {
        assert(type == 10 && index == 0 && grain == 256 && hz == 48000 && format == 1);
        ++opens;
        return fail_open ? -9 : user + 100;
    }
    int32_t sceAudioOutGetPortState(int32_t, void *out)
    {
        std::memset(out, 0, 32);
        static_cast<uint8_t *>(out)[2] = channels.load();
        return 0;
    }
    int32_t sceAudioOutClose(int32_t)
    {
        ++closes;
        return 0;
    }
    int32_t sceAudioOutOutput(int32_t handle, const void *pcm)
    {
        Record record{};
        record.user = handle - 100;
        std::memcpy(record.samples.data(), pcm, sizeof(record.samples));
        if (batch_driver && handle == 104 && (++burst_calls % 4) == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        entered = true;
        if (stalled && (!stall_handle || stall_handle == handle))
        {
            blocked_handle = handle;
            while (stalled)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            blocked_handle = 0;
        }
        {
            std::lock_guard<std::mutex> lock(records_mutex);
            records.push_back(record);
        }
        return fail_output ? -8 : 0;
    }
}
template <class Predicate> void Wait(Predicate predicate)
{
    for (int n = 0; n < 1500; ++n)
    {
        if (predicate())
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(false && "worker deadline");
}
int main()
{
    std::array<uint8_t, ML_HAPTICS_MAX_PAYLOAD> wire{};
    wire[0] = wire[1] = 1;
    MlHapticsWrite16(wire.data() + 8, 240);
    assert(MlHapticsValidate(wire.data(), wire.size()));
    for (unsigned field : {0u, 1u})
    {
        wire[field] = 2;
        assert(!MlHapticsValidate(wire.data(), wire.size()));
        wire[field] = 1;
    }
    assert(!MlHapticsValidate(wire.data(), 11));
    assert(!MlHapticsValidate(wire.data(), wire.size() - 1));
    for (unsigned frames : {0u, 241u})
    {
        MlHapticsWrite16(wire.data() + 8, frames);
        assert(!MlHapticsValidate(wire.data(), wire.size()));
    }
    MlHapticsWrite16(wire.data() + 8, 240);
    MlHapticsWrite16(wire.data() + 2, 16);
    assert(!MlHapticsValidate(wire.data(), wire.size()));
    MlHapticsWrite16(wire.data() + 2, 0);
    MlHapticsWrite16(wire.data() + 10, 1);
    assert(!MlHapticsValidate(wire.data(), wire.size()));
    Buffer b{};
    std::array<uint8_t, PacketFrames * 4> packet{};
    for (unsigned f = 0; f < PacketFrames; ++f)
    {
        int16_t left = static_cast<int16_t>(f == 0 ? -32768 : f * 101);
        int16_t right = static_cast<int16_t>(32767 - f * 83);
        std::memcpy(packet.data() + f * 4, &left, 2);
        std::memcpy(packet.data() + f * 4 + 2, &right, 2);
    }
    assert(b.Push(0xffffffffu, packet.data(), 240));
    assert(b.Push(0, packet.data(), 240));
    int16_t out[512]{};
    assert(b.Pop(out) == 256);
    assert(std::memcmp(out, packet.data(), 960) == 0);
    assert(std::memcmp(out + 480, packet.data(), 64) == 0);
    assert(b.Pop(out) == 224);
    assert(std::memcmp(out, packet.data() + 64, 896) == 0);
    for (unsigned i = 448; i < 512; ++i)
        assert(out[i] == 0);
    assert(!b.Push(0, packet.data(), 240) && b.duplicate == 1);
    assert(!b.Push(0xffffffffu, packet.data(), 240) && b.out_of_order == 1);
    assert(b.Push(2, packet.data(), 240) && b.gaps == 1);
    assert(b.count == 240); // loss discards old queue
    for (unsigned n = 3; n < 100; ++n)
        assert(b.Push(n, packet.data(), 240));
    assert(b.count <= Capacity && b.dropped > 0);
    b.Clear();
    assert(b.Pop(out) == 0);
    for (auto sample : out)
        assert(sample == 0);
    assert(!b.Push(0, nullptr, 240) && !b.Push(0, packet.data(), 241));
    // Isolated actuator: the right channel stays exactly silent, including negative polarity left.
    for (unsigned f = 0; f < 240; ++f)
    {
        packet[f * 4 + 2] = 0;
        packet[f * 4 + 3] = 0;
    }
    b.Push(1, packet.data(), 240);
    b.Pop(out);
    assert(out[0] == -32768);
    for (unsigned f = 0; f < 256; ++f)
        assert(out[f * 2 + 1] == 0);

    fail_open = true;
    Configure(0, 1);
    Wait([] { return GetStatistics(0).open_result == -9; });
    assert(!Ready(0));
    fail_open = false;
    channels = 1;
    Configure(0, 1);
    Wait([] { return GetStatistics(0).port_channels == 1; });
    assert(!Ready(0));
    channels = 2;
    Configure(0, 1);
    Configure(1, 2);
    Wait([] { return Ready(0) && Ready(1); });
    stalled = true;
    entered = false;
    Push(0, 0, packet.data(), 240);
    Push(0, 1, packet.data(), 240);
    Wait([] { return Active(0); });
    Allow(0, ActiveToken(0));
    Wait([] { return entered.load(); });
    auto start = std::chrono::steady_clock::now();
    for (unsigned n = 2; n < 10000; ++n)
        Push(0, n, packet.data(), 240);
    assert(std::chrono::steady_clock::now() - start < std::chrono::seconds(1));
    // A stalled old handle cannot reopen for a new user or consume the new generation's PCM.
    Configure(0, 3);
    assert(!Ready(0));
    Push(0, 0, packet.data(), 240);
    stalled = false;
    Wait([] { return Ready(0); });
    assert(GetStatistics(0).ready);
    {
        std::lock_guard<std::mutex> lock(records_mutex);
        for (const auto &record : records)
            if (record.user == 3)
                for (auto sample : record.samples)
                    assert(sample == 0);
    }
    entered = false;
    Push(1, 0, packet.data(), 240);
    Push(1, 1, packet.data(), 240);
    Wait([] { return Active(1); });
    Allow(1, ActiveToken(1));
    Wait([] { return entered.load(); });
    fail_output = true;
    Push(0, 0, packet.data(), 240);
    Push(0, 1, packet.data(), 240);
    Wait([] { return Active(0); });
    Allow(0, ActiveToken(0));
    Wait([] { return GetStatistics(0).output_errors > 0; });
    assert(!Ready(0));
    fail_output = false;
    // Worker integration for one owned four-packet burst and a blocking driver.
    // Continuous 5/10/20ms cadence lives in the virtual-clock policy suite: OS
    // sleep_for(20ms) can take substantially longer and is not a sample clock.
    Configure(2, 4);
    Wait([] { return Ready(2); });
    batch_driver = true;
    burst_calls = 0;
    const auto initial = GetStatistics(2);
    for (uint32_t sequence = 0; sequence < 4; ++sequence)
        Push(2, sequence, packet.data(), 240);
    Wait([] { return Active(2); });
    Allow(2, ActiveToken(2));
    Wait([] { return !Active(2) && GetStatistics(2).received == 4; });
    batch_driver = false;
    const auto final = GetStatistics(2);
    assert(final.received - initial.received == 4);
    assert(final.overflow == initial.overflow && final.inbox_full == initial.inbox_full);
    assert(final.contention == initial.contention && final.gaps == initial.gaps);
    assert(final.input_frames - initial.input_frames == 960);
    assert(final.dequeued_frames - initial.dequeued_frames == 960);
    assert(final.starvation_frames == initial.starvation_frames);

    // A permanently open host endpoint sends zeros without reacquiring native
    // ownership or losing the input sequence. A fresh nonzero effect still works.
    std::array<uint8_t, PacketFrames * 4> zero{};
    for (uint32_t sequence = 4; sequence < 12; ++sequence)
        Push(2, sequence, zero.data(), 240);
    Wait([] { return GetStatistics(2).received == 12; });
    assert(!Active(2) && Ready(2));
    const auto idle = GetStatistics(2);
    assert(idle.ignored_silent_frames - final.ignored_silent_frames == 8 * 240);
    Push(2, 11, packet.data(), 240); // older nonzero cannot restart after silence
    Wait([&] { return GetStatistics(2).duplicate > idle.duplicate; });
    assert(!Active(2));
    Allow(2, ActiveToken(2)); // acknowledging idle must not authorize the next effect
    Push(2, 12, packet.data(), 240);
    Push(2, 13, packet.data(), 240);
    Wait([] { return Active(2); });
    assert(GetStatistics(2).dequeued_frames == final.dequeued_frames);
    Allow(2, ActiveToken(2));
    Wait([] { return !Active(2) && GetStatistics(2).received == 14; });
    assert(GetStatistics(2).dequeued_frames - final.dequeued_frames == 480);
    // A >60ms call retires authorization as well as the playout queue.
    // Packets published near its return are still pre-reset backlog, not a
    // replacement effect. No global driver stall is imposed on the other slots.
    Configure(3, 5);
    Wait([] { return Ready(3); });
    stall_handle = 105;
    stalled = true;
    for (unsigned n = 0; n < 4; ++n)
        Push(3, n, packet.data(), 240);
    Wait([] { return Active(3); });
    const auto old_token = ActiveToken(3);
    Allow(3, old_token);
    Wait([] { return blocked_handle == 105; });
    const auto held = GetStatistics(3);
    std::this_thread::sleep_for(std::chrono::milliseconds(70));
    Push(3, 4, packet.data(), 240);
    Push(3, 5, packet.data(), 240);
    stalled = false;
    Wait([&] { return GetStatistics(3).output_stalls > held.output_stalls; });
    assert(Ready(3) && !Active(3));
    const auto reset_frames = GetStatistics(3).dequeued_frames;
    for (unsigned n = 6; n < 10; ++n)
        Push(3, n, packet.data(), 240);
    Wait([] { return Active(3); });
    const auto fresh_token = ActiveToken(3);
    assert(fresh_token && fresh_token != old_token);
    Allow(3, old_token); // delayed acknowledgement from the retired effect
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(GetStatistics(3).dequeued_frames == reset_frames);
    Allow(3, fresh_token); // only the fresh pad-owner acknowledgement releases PCM
    Allow(3, old_token);   // a late obsolete acknowledgement cannot revoke it
    Wait([] { return !Active(3); });
    assert(GetStatistics(3).dequeued_frames - reset_frames == 960);

    // Rapid user reuse followed by shutdown while the old port is blocked must
    // not open/authorize a superseded user or submit fresh PCM to the old user.
    stalled = true;
    for (unsigned n = 10; n < 14; ++n)
        Push(3, n, packet.data(), 240);
    Wait([] { return Active(3); });
    Allow(3, ActiveToken(3));
    Wait([] { return blocked_handle == 105; });
    start = std::chrono::steady_clock::now();
    Configure(3, 6);
    Disable(3);
    assert(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(100));
    assert(!Ready(3));
    Push(3, 0, packet.data(), 240);
    std::this_thread::sleep_for(std::chrono::milliseconds(70));
    stalled = false;
    Wait([] { return !Active(3); });
    {
        std::lock_guard<std::mutex> lock(records_mutex);
        for (const auto &record : records)
            assert(record.user != 6);
    }
    stall_handle = 0;
    int before = closes.load();
    DisableAll();
    Wait([&] { return closes.load() > before; });
    assert(!Ready(0) && !Ready(1));
    std::puts("Waveform bit preservation / reblocking / sequence / bounded stall / generations / "
              "multiple users / native failure PASS");
}
