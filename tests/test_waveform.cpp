/* ps5-native-app-boilerplate - Bit-exact PCM and asynchronous AudioOut checks.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ps5_waveform.hpp"
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
static std::atomic<int> opens{0}, closes{0};
static std::atomic<int> channels{2};
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
        entered = true;
        while (stalled)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
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
    Allow(0);
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
    Allow(1);
    Wait([] { return entered.load(); });
    fail_output = true;
    Push(0, 0, packet.data(), 240);
    Push(0, 1, packet.data(), 240);
    Wait([] { return Active(0); });
    Allow(0);
    Wait([] { return GetStatistics(0).output_errors > 0; });
    assert(!Ready(0));
    fail_output = false;
    int before = closes.load();
    DisableAll();
    Wait([&] { return closes.load() > before; });
    assert(!Ready(0) && !Ready(1));
    std::puts("Waveform bit preservation / reblocking / sequence / bounded stall / generations / "
              "multiple users / native failure PASS");
}
