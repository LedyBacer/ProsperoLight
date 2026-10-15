/* ps5-native-app-boilerplate - Per-controller native waveform workers.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ps5_waveform.hpp"
#include <atomic>
#include <cstring>
#include <pthread.h>
#include <time.h>
extern "C"
{
    int32_t sceAudioOutInit(void);
    int32_t sceAudioOutOpen(int32_t user, int32_t type, int32_t index, uint32_t frames,
                            uint32_t frequency, uint32_t format);
    int32_t sceAudioOutOutput(int32_t handle, const void *samples);
    int32_t sceAudioOutClose(int32_t handle);
    int32_t sceAudioOutGetPortState(int32_t handle, void *state);
}
namespace prosperolight::waveform
{
void Buffer::Clear()
{
    head = count = 0;
    sequenced = false;
}
bool Buffer::Push(uint32_t next, const uint8_t *pcm, unsigned frames)
{
    if (!pcm || !frames || frames > PacketFrames)
    {
        ++dropped;
        return false;
    }
    if (sequenced)
    {
        const uint32_t delta = next - sequence;
        if (delta == 0)
        {
            ++duplicate;
            return false;
        }
        if (delta >= 0x80000000u)
        {
            ++out_of_order;
            return false;
        }
        if (delta != 1)
        {
            ++gaps;
            head = count = 0;
        }
    }
    sequenced = true;
    sequence = next;
    ++received;
    input_frames += frames;
    if (count + frames > Capacity)
    {
        ++dropped;
        ++overflow;
        head = count = 0;
    }
    for (unsigned f = 0; f < frames; ++f)
        for (unsigned c = 0; c < Channels; ++c)
        {
            const unsigned p = (f * Channels + c) * 2;
            const uint16_t bits = uint16_t(pcm[p]) | (uint16_t(pcm[p + 1]) << 8);
            // Preserve every signed sample bit, including -32768.
            std::memcpy(&samples[((head + count + f) % Capacity) * Channels + c], &bits, 2);
        }
    count += frames;
    return true;
}
unsigned Buffer::Pop(int16_t *output)
{
    const unsigned n = count < Grain ? count : Grain;
    for (unsigned f = 0; f < n; ++f)
        for (unsigned c = 0; c < Channels; ++c)
            output[f * Channels + c] = samples[((head + f) % Capacity) * Channels + c];
    std::memset(output + n * Channels, 0, (Grain - n) * Channels * sizeof(int16_t));
    head = (head + n) % Capacity;
    count -= n;
    dequeued_frames += n;
    if (n < Grain)
    {
        ++underruns;
        padded_frames += Grain - n;
    }
    return n;
}
namespace
{
constexpr unsigned Slots = 4;
struct PortState
{
    uint16_t output;
    uint8_t channels, reserved;
    int16_t volume;
    uint16_t reroutes;
    uint64_t flags, unused[2];
};
static_assert(sizeof(PortState) == 32);
// ControlStream serializes callbacks, but the guard also bounds accidental concurrent
// producers. Only the worker consumes; epochs are carried in every owned packet.
struct Inbox
{
    static constexpr unsigned Size = 16;
    struct Packet
    {
        uint64_t epoch, arrival;
        uint32_t sequence;
        unsigned frames;
        uint8_t pcm[PacketFrames * Channels * 2];
    } packets[Size]{};
    std::atomic<uint32_t> read{0}, write{0};
    std::atomic_flag producer = ATOMIC_FLAG_INIT;
};
struct Sink
{
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    Buffer buffer{};
    Inbox inbox{};
    int32_t user = -1;
    uint64_t epoch = 0;
    std::atomic<uint64_t> ready_epoch{0}, active_epoch{0}, contention{0}, rejected{0},
        permitted_epoch{0}, inbox_full{0};
    Statistics statistics{};
    bool started = false;
    uint64_t last_packet = 0;
};
// Process lifetime storage: even a stalled driver cannot leave a dangling queue.
// Teardown invalidates epochs asynchronously; no driver I/O is joined on an input thread.
Sink sinks[Slots];
uint64_t Now()
{
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return uint64_t(t.tv_sec) * 1000000 + t.tv_nsec / 1000;
}
void Sleep(uint64_t us)
{
    timespec t{static_cast<time_t>(us / 1000000), static_cast<long>((us % 1000000) * 1000)};
    nanosleep(&t, nullptr);
}
void *Worker(void *argument)
{
    auto &sink = *static_cast<Sink *>(argument);
    uint64_t owned_epoch = 0, deadline = 0, clock_remainder = 0, first_packet = 0, pending_pad = 0;
    bool primed = false;
    int handle = -1;
    bool failed = false;
    alignas(64) int16_t output[Grain * Channels]{};
    for (;;)
    {
        pthread_mutex_lock(&sink.mutex);
        const uint64_t epoch = sink.epoch;
        const int32_t user = sink.user;
        pthread_mutex_unlock(&sink.mutex);
        if (owned_epoch != epoch)
        {
            sink.ready_epoch.store(0);
            int closed = 0;
            if (handle >= 0)
            {
                std::memset(output, 0, sizeof(output));
                (void)sceAudioOutOutput(handle, output);
                closed = sceAudioOutClose(handle);
            }
            sink.active_epoch.store(0);
            handle = -1;
            owned_epoch = epoch;
            deadline = clock_remainder = first_packet = pending_pad = 0;
            primed = false;
            failed = false;
            if (user >= 0)
            {
                (void)sceAudioOutInit();
                handle = sceAudioOutOpen(user, 10, 0, Grain, 48000, 1);
                PortState state{};
                const int opened = handle;
                const int state_rc = handle >= 0 ? sceAudioOutGetPortState(handle, &state) : handle;
                if (handle >= 0 && (state_rc < 0 || state.channels != 2))
                {
                    closed = sceAudioOutClose(handle);
                    handle = -1;
                }
                pthread_mutex_lock(&sink.mutex);
                sink.statistics.open_result = opened;
                sink.statistics.close_result = closed;
                sink.statistics.state_result = state_rc;
                sink.statistics.port_output = state.output;
                sink.statistics.port_channels = state.channels;
                // A positive open handle alone is insufficient for PCM negotiation.
                if (sink.epoch == epoch && handle >= 0 && state_rc >= 0 && state.channels == 2)
                    sink.ready_epoch.store(epoch);
                else
                    failed = true;
                pthread_mutex_unlock(&sink.mutex);
            }
        }
        if (handle < 0 || failed || sink.ready_epoch.load() != epoch)
        {
            Sleep(2000);
            continue;
        }
        bool render = false, ending = false;
        pthread_mutex_lock(&sink.mutex);
        if (sink.epoch == owned_epoch)
        {
            uint32_t read = sink.inbox.read.load(std::memory_order_relaxed);
            const uint32_t write = sink.inbox.write.load(std::memory_order_acquire);
            // Take time after acquiring the published packets: their arrival cannot be in the
            // future.
            const uint64_t now = Now();
            while (read != write)
            {
                const auto &packet = sink.inbox.packets[read % Inbox::Size];
                if (packet.epoch == owned_epoch && now - packet.arrival < 60000)
                {
                    if (sink.buffer.Push(packet.sequence, packet.pcm, packet.frames))
                    {
                        sink.statistics.starvation_frames += pending_pad;
                        pending_pad = 0;
                        if (!first_packet)
                            first_packet = packet.arrival;
                        sink.last_packet = packet.arrival;
                    }
                }
                else
                    ++sink.statistics.stale;
                ++read;
            }
            sink.inbox.read.store(read, std::memory_order_release);
            const unsigned target = PrimeFrames < Capacity ? PrimeFrames : Capacity;
            if (!primed &&
                ((sink.buffer.count >= target && first_packet && now - first_packet >= 16000) ||
                 (sink.buffer.count && first_packet && now - first_packet >= 20000)))
                primed = true;
            if (primed &&
                (sink.buffer.count || (sink.last_packet && now - sink.last_packet < 60000)))
            {
                sink.active_epoch.store(owned_epoch);
                if (sink.permitted_epoch.load() == owned_epoch)
                {
                    pending_pad += Grain - sink.buffer.Pop(output);
                    render = true;
                }
            }
            else if (sink.active_epoch.load())
            {
                std::memset(output, 0, sizeof(output));
                render = true;
                ending = true;
                primed = false;
                first_packet = pending_pad = 0;
            }
        }
        pthread_mutex_unlock(&sink.mutex);
        if (!render)
        {
            deadline = clock_remainder = 0;
            Sleep(1000);
            continue;
        }
        // Absolute sample clock: a blocking driver call consumes this budget;
        // subsequent immediate calls catch up instead of adding a second delay.
        const uint64_t before = Now();
        if (!deadline)
            deadline = before;
        if (before < deadline)
            Sleep(deadline - before);
        const uint64_t start = Now();
        const int result = sceAudioOutOutput(handle, output);
        int closed = 0;
        if (result < 0)
        {
            closed = sceAudioOutClose(handle);
            handle = -1;
        }
        pthread_mutex_lock(&sink.mutex);
        sink.statistics.output_result = result;
        ++sink.statistics.output_calls;
        if (ending)
        {
            sink.active_epoch.store(0);
            sink.permitted_epoch.store(0);
        }
        if (result < 0)
        {
            ++sink.statistics.output_errors;
            sink.statistics.close_result = closed;
            sink.ready_epoch.store(0);
            sink.active_epoch.store(0);
            sink.buffer.Clear();
            failed = true;
        }
        // A stalled port must not play its accumulated backlog on returning.
        if (Now() - start > 60000)
        {
            sink.buffer.Clear();
            ++sink.statistics.output_stalls;
            primed = false;
            first_packet = 0;
            deadline = clock_remainder = 0;
        }
        pthread_mutex_unlock(&sink.mutex);
        clock_remainder += uint64_t(Grain) * 1000000;
        deadline += clock_remainder / 48000;
        clock_remainder %= 48000;
        // Never issue an unbounded catch-up burst after scheduling/driver stalls.
        if (Now() > deadline + 60000)
            deadline = clock_remainder = 0;
    }
    return nullptr;
}
} // namespace
void Configure(unsigned slot, int32_t user)
{
    if (slot >= Slots)
        return;
    Sink &s = sinks[slot];
    pthread_mutex_lock(&s.mutex);
    ++s.epoch;
    s.user = user;
    s.statistics.open_result = user >= 0 ? 0 : -1;
    s.ready_epoch.store(0);
    s.permitted_epoch.store(0);
    s.buffer.Clear();
    s.last_packet = 0;
    if (!s.started && user >= 0)
    {
        pthread_t thread{};
        if (pthread_create(&thread, nullptr, Worker, &s) == 0)
        {
            pthread_detach(thread);
            s.started = true;
        }
        else
            ++s.statistics.output_errors;
    }
    pthread_mutex_unlock(&s.mutex);
}
void Disable(unsigned slot)
{
    Configure(slot, -1);
}
void DisableAll()
{
    for (unsigned i = 0; i < Slots; ++i)
        Disable(i);
}
bool Ready(unsigned slot)
{
    return slot < Slots && sinks[slot].ready_epoch.load() != 0;
}
bool Active(unsigned slot)
{
    return slot < Slots && sinks[slot].active_epoch.load() != 0;
}
void Allow(unsigned slot)
{
    if (slot < Slots)
        sinks[slot].permitted_epoch.store(sinks[slot].ready_epoch.load());
}
bool AnyReady()
{
    for (unsigned i = 0; i < Slots; ++i)
        if (Ready(i))
            return true;
    return false;
}
void Push(unsigned slot, uint32_t sequence, const uint8_t *pcm, unsigned frames)
{
    if (slot >= Slots)
        return;
    Sink &s = sinks[slot];
    const uint64_t epoch = s.ready_epoch.load();
    if (!epoch || !pcm || !frames || frames > PacketFrames)
    {
        ++s.rejected;
        return;
    }
    if (s.inbox.producer.test_and_set(std::memory_order_acquire))
    {
        ++s.contention;
        return;
    }
    const uint32_t write = s.inbox.write.load(std::memory_order_relaxed);
    const uint32_t read = s.inbox.read.load(std::memory_order_acquire);
    if (write - read == Inbox::Size)
        ++s.inbox_full;
    else
    {
        auto &packet = s.inbox.packets[write % Inbox::Size];
        packet.epoch = epoch;
        packet.arrival = Now();
        packet.sequence = sequence;
        packet.frames = frames;
        std::memcpy(packet.pcm, pcm, frames * Channels * 2);
        s.inbox.write.store(write + 1, std::memory_order_release);
    }
    s.inbox.producer.clear(std::memory_order_release);
}
void Contended(unsigned slot)
{
    if (slot < Slots)
        ++sinks[slot].contention;
}
Statistics GetStatistics(unsigned slot)
{
    if (slot >= Slots)
        return {};
    Sink &s = sinks[slot];
    pthread_mutex_lock(&s.mutex);
    Statistics r = s.statistics;
    r.received = s.buffer.received;
    r.overflow = s.buffer.overflow;
    r.padded_frames = s.buffer.padded_frames;
    r.input_frames = s.buffer.input_frames;
    r.dequeued_frames = s.buffer.dequeued_frames;
    r.inbox_full = s.inbox_full.load();
    r.rejected = s.rejected.load();
    r.dropped = s.buffer.dropped + r.rejected + r.inbox_full + r.stale + r.output_stalls +
                s.contention.load();
    r.duplicate = s.buffer.duplicate;
    r.out_of_order = s.buffer.out_of_order;
    r.gaps = s.buffer.gaps;
    r.underruns = s.buffer.underruns;
    r.contention = s.contention.load();
    r.ready = Ready(slot);
    r.active = Active(slot);
    pthread_mutex_unlock(&s.mutex);
    return r;
}
} // namespace prosperolight::waveform
