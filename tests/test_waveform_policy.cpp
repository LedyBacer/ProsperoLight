/* ps5-native-app-boilerplate - Virtual-clock waveform policy regressions.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ps5_waveform.hpp"
#include <array>
#include <vector>
#include <deque>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <algorithm>
using namespace prosperolight::waveform;
using Packet = std::array<uint8_t, PacketFrames * 4>;
static Packet Signal(unsigned packet)
{
    Packet result{};
    for (unsigned f = 0; f < PacketFrames; ++f)
    {
        const int16_t left = static_cast<int16_t>((packet * PacketFrames + f) % 30000 + 1);
        const int16_t right = -left;
        std::memcpy(result.data() + f * 4, &left, 2);
        std::memcpy(result.data() + f * 4 + 2, &right, 2);
    }
    return result;
}
struct Arrival
{
    uint64_t time;
    unsigned sequence;
    Packet pcm;
};
static void Simulate(const char *name, unsigned batch, bool jitter, bool blocking)
{
    Playout player;
    std::vector<Arrival> arrivals;
    const unsigned count = 200;
    const unsigned offsets[] = {0, 2000, 1000, 3000, 0, 1000};
    for (unsigned n = 0; n < count; ++n)
        arrivals.push_back(
            {100000 + uint64_t(n / batch) * batch * 5000 + (jitter ? offsets[(n / batch) % 6] : 0),
             n, Signal(n)});
    uint64_t now = arrivals.front().time, deadline = 0, remainder = 0, busy_until = 0;
    unsigned next = 0, calls = 0;
    std::deque<Arrival> inbox;
    std::vector<int16_t> delivered;
    for (; now < 1400000; now += 100)
    {
        while (next < arrivals.size() && arrivals[next].time <= now)
            inbox.push_back(arrivals[next++]);
        if (now < busy_until)
            continue;
        while (!inbox.empty())
        {
            const auto &packet = inbox.front();
            assert(now - packet.time < 60000);
            assert(player.Receive(packet.sequence, packet.pcm.data(), PacketFrames, packet.time));
            inbox.pop_front();
        }
        if (!player.WantsOutput(now))
        {
            deadline = remainder = 0;
            continue;
        }
        if (!deadline)
            deadline = now;
        if (now < deadline)
            continue;
        int16_t output[Grain * 2]{};
        RenderMetadata metadata{};
        player.Pop(output, &metadata);
        delivered.insert(delivered.end(), output, output + metadata.frames * 2);
        const uint64_t duration = blocking && (++calls % 4) == 0 ? 20000 : 0;
        player.RecordOutput(now, duration, output, metadata, true);
        busy_until = now + duration;
        remainder += uint64_t(Grain) * 1000000;
        deadline += remainder / 48000;
        remainder %= 48000;
    }
    assert(next == count && inbox.empty());
    assert(!player.effect && !player.WantsOutput(now));
    assert(player.buffer.input_frames == count * PacketFrames);
    assert(player.buffer.dequeued_frames == count * PacketFrames);
    assert(!player.buffer.dropped && !player.buffer.gaps && !player.starvation_frames);
    assert(delivered.size() == count * PacketFrames * 2);
    for (unsigned n = 0; n < count; ++n)
    {
        const auto expected = Signal(n);
        assert(!std::memcmp(delivered.data() + n * PacketFrames * 2, expected.data(),
                            expected.size()));
    }
    const auto rx = player.receive_to_output.Summary();
    const auto first = player.first_nonzero_delay.Summary();
    const auto blocked = player.blocking.Summary();
    const auto depth = player.queue_depth.Summary();
    assert(rx.samples == count * PacketFrames && first.samples == 1);
    assert(first.maximum >= SelectedPolicy.minimum_us);
    std::printf("{\"clock\":\"virtual\",\"profile\":%d,\"scenario\":\"%s\","
                "\"frames\":%llu,\"drops\":0,\"starvation\":0,"
                "\"receive_us\":[%llu,%llu,%llu],\"first_us\":%llu,"
                "\"blocking_p99_us\":%llu,\"depth_p99_frames\":%llu}\n",
                PROSPEROLIGHT_HAPTICS_PROFILE, name,
                (unsigned long long)player.buffer.dequeued_frames, (unsigned long long)rx.p50,
                (unsigned long long)rx.p95, (unsigned long long)rx.p99,
                (unsigned long long)first.maximum, (unsigned long long)blocked.p99,
                (unsigned long long)depth.p99);
}
int main()
{
    static_assert(Capacity == 3072 && Grain == 256);
    const auto selected = Profiles[PROSPEROLIGHT_HAPTICS_PROFILE];
    assert(SelectedPolicy.frames == selected.frames &&
           SelectedPolicy.minimum_us == selected.minimum_us &&
           SelectedPolicy.fragment_us == selected.fragment_us);
    Distribution histogram;
    for (unsigned n = 1; n <= 100; ++n)
        histogram.Observe(n * 1000);
    auto summary = histogram.Summary();
    assert(summary.samples == 100 && summary.p50 == 50000 && summary.p95 == 95000 &&
           summary.p99 == 99000);
    histogram.Observe(900000);
    assert(histogram.Summary().maximum == 900000);
    Distribution empty;
    assert(!empty.Summary().samples && !empty.Summary().p99);

    Simulate("individual-5ms", 1, false, false);
    Simulate("two-per-10ms", 2, false, false);
    Simulate("four-per-20ms", 4, false, false);
    Simulate("variable-jitter", 1, true, false);
    Simulate("batched-blocking-driver", 4, false, true);

    // Infinite idle zeros advance sequence without ever claiming the actuators.
    Playout silence;
    Packet zero{};
    for (unsigned n = 0; n < 200; ++n)
    {
        assert(silence.Receive(n, zero.data(), PacketFrames, 100000 + n * 5000));
        assert(!silence.WantsOutput(100000 + n * 5000));
    }
    auto nonzero = Signal(0);
    assert(!silence.Receive(150, nonzero.data(), PacketFrames, 1100000));
    assert(!silence.effect && silence.buffer.out_of_order == 1);
    assert(silence.buffer.sequence == 199 && silence.buffer.sequenced);

    // A short impulse uses the configurable fragment timeout; continuous zeros
    // afterwards cannot refresh last_nonzero or repeatedly re-acquire ownership.
    Playout impulse;
    Packet pulse{};
    pulse[0] = 1;
    assert(impulse.Receive(0xffffffffu, pulse.data(), PacketFrames, 100000));
    assert(!impulse.WantsOutput(100000 + SelectedPolicy.fragment_us - 1));
    assert(impulse.WantsOutput(100000 + SelectedPolicy.fragment_us));
    int16_t output[Grain * 2]{};
    RenderMetadata metadata{};
    impulse.Pop(output, &metadata);
    assert(output[0] == 1 && output[1] == 0);
    impulse.RecordOutput(100000 + SelectedPolicy.fragment_us, 123, output, metadata, true);
    assert(impulse.first_nonzero_delay.Summary().maximum == SelectedPolicy.fragment_us);
    for (unsigned n = 0; n < 200; ++n)
    {
        const uint64_t now = 125000 + n * 5000;
        assert(impulse.Receive(n, zero.data(), PacketFrames, now));
        const bool native = impulse.WantsOutput(now);
        if (now >= 160000)
            assert(!native); // legacy rumble can resume despite continuous PCM arrival
        else if (native)
            impulse.Pop(output);
    }
    assert(impulse.last_nonzero == 100000 && !impulse.effect && impulse.buffer.sequenced);
    assert(impulse.last_packet > 160000);
    assert(!impulse.Receive(100, pulse.data(), PacketFrames, 1200000));
    assert(!impulse.WantsOutput(1200000));
    assert(impulse.Receive(200, pulse.data(), PacketFrames, 1210000));
    assert(impulse.WantsOutput(1210000 + SelectedPolicy.fragment_us));
    impulse.Pop(output);
    assert(output[0] == 1);

    // Fault/stall flushes preserve sequence; a controller reconnect resets it.
    impulse.Reset(false);
    assert(!impulse.Receive(199, pulse.data(), PacketFrames, 1300000));
    assert(!impulse.WantsOutput(1300000));
    impulse.Reset(true);
    assert(impulse.Receive(0, pulse.data(), PacketFrames, 1400000));
    assert(!impulse.WantsOutput(1400000));
    assert(impulse.WantsOutput(1400000 + SelectedPolicy.fragment_us));
    impulse.Pop(output);
    assert(output[0] == 1);
    // Invalid or late packets must not refresh a live effect's activity timer.
    const auto activity = impulse.last_nonzero;
    assert(!impulse.Receive(0, pulse.data(), PacketFrames, 1500000));
    assert(!impulse.Receive(1, pulse.data(), 241, 1500000));
    assert(impulse.last_nonzero == activity);
    impulse.RecordOutput(1500000, 70000, output, metadata, false);
    assert(impulse.blocking.Summary().maximum == 70000);
    std::puts("Waveform priming / virtual jitter / sample fidelity / continuous silence / legacy "
              "recovery / reconnect PASS");
}
