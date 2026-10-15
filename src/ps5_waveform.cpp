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
    if (count + frames > Capacity)
    {
        ++dropped;
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
    if (n < Grain)
        ++underruns;
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
struct Sink
{
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    Buffer buffer{};
    int32_t user = -1;
    uint64_t epoch = 0;
    std::atomic<uint64_t> ready_epoch{0}, active_epoch{0}, contention{0}, rejected{0},
        permitted_epoch{0};
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
    uint64_t owned_epoch = 0, last_output = 0;
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
        const uint64_t now = Now();
        if (sink.epoch == owned_epoch)
        {
            if (sink.buffer.count >= Grain ||
                (sink.active_epoch.load() && sink.last_packet && now - sink.last_packet < 60000) ||
                (sink.buffer.count && now - sink.last_packet >= 10000))
            {
                sink.active_epoch.store(owned_epoch);
                if (sink.permitted_epoch.load() == owned_epoch)
                {
                    sink.buffer.Pop(output);
                    render = true;
                }
            }
            else if (sink.active_epoch.load())
            {
                // Last zero grain before restoring host's remembered legacy state.
                std::memset(output, 0, sizeof(output));
                render = true;
                ending = true;
            }
        }
        pthread_mutex_unlock(&sink.mutex);
        if (!render)
        {
            Sleep(2000);
            continue;
        }
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
        if (Now() - start > 20000)
        {
            sink.buffer.Clear();
            ++sink.buffer.dropped;
        }
        pthread_mutex_unlock(&sink.mutex);
        // Some implementations return immediately: never busy-loop AudioOut.
        last_output = Now() - start;
        if (last_output < 5333)
            Sleep(5333 - last_output);
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
    if (!Ready(slot))
    {
        ++s.rejected;
        return;
    }
    if (pthread_mutex_trylock(&s.mutex) != 0)
    {
        ++s.contention;
        return;
    }
    if (s.ready_epoch.load() == s.epoch && s.buffer.Push(sequence, pcm, frames))
        s.last_packet = Now();
    pthread_mutex_unlock(&s.mutex);
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
    r.dropped = s.buffer.dropped + s.rejected.load();
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
