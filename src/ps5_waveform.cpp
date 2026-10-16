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
    Playout playout{};
    Inbox inbox{};
    int32_t user = -1;
    uint64_t epoch = 0, next_token = 0;
    std::atomic<uint64_t> ready_epoch{0}, active_token{0}, contention{0}, rejected{0},
        permitted_token{0}, inbox_full{0};
    Statistics statistics{};
    bool started = false;
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
    uint64_t owned_epoch = 0, deadline = 0, clock_remainder = 0, accept_after = 0;
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
            sink.active_token.store(0);
            handle = -1;
            owned_epoch = epoch;
            accept_after = 0;
            deadline = clock_remainder = 0;
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
        // Wait before consuming the queue so packets arriving before the output
        // deadline can still fill this grain. No callback/queue lock is held.
        const uint64_t before = Now();
        if (deadline && before < deadline)
            Sleep(deadline - before);
        bool render = false, ending = false;
        RenderMetadata metadata{};
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
                if (packet.epoch == owned_epoch && packet.arrival > accept_after &&
                    now - packet.arrival < 60000)
                {
                    sink.playout.Receive(packet.sequence, packet.pcm, packet.frames,
                                         packet.arrival);
                }
                else
                    ++sink.statistics.stale;
                ++read;
            }
            sink.inbox.read.store(read, std::memory_order_release);
            if (sink.playout.WantsOutput(now))
            {
                uint64_t token = sink.active_token.load();
                if (!token)
                {
                    if (++sink.next_token == 0)
                        ++sink.next_token;
                    token = sink.next_token;
                    sink.active_token.store(token);
                }
                if (sink.permitted_token.load() == token)
                {
                    sink.playout.Pop(output, &metadata);
                    render = true;
                }
            }
            else if (sink.active_token.load())
            {
                std::memset(output, 0, sizeof(output));
                render = true;
                ending = true;
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
        const uint64_t start = Now();
        if (!deadline)
            deadline = start;
        const int result = sceAudioOutOutput(handle, output);
        const uint64_t blocked_us = Now() - start;
        int closed = 0;
        if (result < 0)
        {
            closed = sceAudioOutClose(handle);
            handle = -1;
        }
        pthread_mutex_lock(&sink.mutex);
        sink.statistics.output_result = result;
        ++sink.statistics.output_calls;
        if (sink.epoch == owned_epoch)
            sink.playout.RecordOutput(start, blocked_us, output, metadata, result >= 0);
        else
            sink.playout.blocking.Observe(blocked_us);
        if (ending && sink.epoch == owned_epoch)
        {
            sink.active_token.store(0);
            sink.permitted_token.store(0);
        }
        if (result < 0)
        {
            ++sink.statistics.output_errors;
            sink.statistics.close_result = closed;
            if (sink.epoch == owned_epoch)
            {
                sink.ready_epoch.store(0);
                sink.active_token.store(0);
                sink.permitted_token.store(0);
                sink.playout.Reset(false);
            }
            failed = true;
        }
        // A stalled port must not play its accumulated backlog on returning.
        if (blocked_us > 60000)
        {
            if (sink.epoch == owned_epoch)
            {
                // Retire this effect before accepting another. Tokens never reset
                // across effects/epochs, so late pad acknowledgements cannot match
                // its replacement. Keep the port ready for fresh ownership.
                sink.active_token.store(0);
                sink.permitted_token.store(0);
                sink.playout.Reset(false);
                // Drop published in-flight backlog. A producer currently copying
                // an older packet can publish later; its arrival cutoff also
                // prevents that packet from becoming a new effect.
                accept_after = Now();
                sink.inbox.read.store(sink.inbox.write.load(std::memory_order_acquire),
                                      std::memory_order_release);
            }
            ++sink.statistics.output_stalls;
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
    s.permitted_token.store(0);
    s.playout.Reset(true);
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
    return ActiveToken(slot) != 0;
}
uint64_t ActiveToken(unsigned slot)
{
    return slot < Slots ? sinks[slot].active_token.load() : 0;
}
void Allow(unsigned slot, uint64_t observed_token)
{
    // A reset can race this store, but an old immutable token never authorizes
    // the new effect: the worker compares it with its fresh monotonic token.
    if (slot < Slots && observed_token && Ready(slot) && ActiveToken(slot) == observed_token)
        sinks[slot].permitted_token.store(observed_token);
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
    r.starvation_frames = s.playout.starvation_frames;
    r.ignored_silent_frames = s.playout.buffer.ignored_silent_frames;
    r.receive_to_output = s.playout.receive_to_output.Summary();
    r.blocking = s.playout.blocking.Summary();
    r.first_nonzero_delay = s.playout.first_nonzero_delay.Summary();
    r.queue_depth = s.playout.queue_depth.Summary();
    r.received = s.playout.buffer.received;
    r.overflow = s.playout.buffer.overflow;
    r.padded_frames = s.playout.buffer.padded_frames;
    r.input_frames = s.playout.buffer.input_frames;
    r.dequeued_frames = s.playout.buffer.dequeued_frames;
    r.inbox_full = s.inbox_full.load();
    r.rejected = s.rejected.load();
    r.dropped = s.playout.buffer.dropped + r.rejected + r.inbox_full + r.stale + r.output_stalls +
                s.contention.load();
    r.duplicate = s.playout.buffer.duplicate;
    r.out_of_order = s.playout.buffer.out_of_order;
    r.gaps = s.playout.buffer.gaps;
    r.underruns = s.playout.buffer.underruns;
    r.contention = s.contention.load();
    r.ready = Ready(slot);
    r.active = Active(slot);
    pthread_mutex_unlock(&s.mutex);
    return r;
}
} // namespace prosperolight::waveform
