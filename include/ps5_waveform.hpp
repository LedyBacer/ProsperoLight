/* ps5-native-app-boilerplate - Bounded DualSense native PCM transport.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include <cstdint>
#include <cstddef>
namespace prosperolight::waveform
{
constexpr unsigned Channels = 2, Grain = 256, PacketFrames = 240;
#ifndef PROSPEROLIGHT_HAPTICS_QUEUE_FRAMES
#define PROSPEROLIGHT_HAPTICS_QUEUE_FRAMES 3072
#endif
struct PrimingPolicy
{
    unsigned frames;
    uint64_t minimum_us, fragment_us;
    constexpr bool Ready(unsigned queued, unsigned capacity, uint64_t elapsed) const
    {
        const unsigned target = frames < capacity ? frames : capacity;
        return (queued >= target && elapsed >= minimum_us) || (queued && elapsed >= fragment_us);
    }
};
constexpr PrimingPolicy Profiles[] = {
    {768, 16000, 20000}, {720, 12000, 16000}, {720, 10000, 15000}};
#ifndef PROSPEROLIGHT_HAPTICS_PROFILE
#define PROSPEROLIGHT_HAPTICS_PROFILE 1
#endif
static_assert(PROSPEROLIGHT_HAPTICS_PROFILE >= 0 && PROSPEROLIGHT_HAPTICS_PROFILE <= 2);
#ifndef PROSPEROLIGHT_HAPTICS_PRIME_FRAMES
#define PROSPEROLIGHT_HAPTICS_PRIME_FRAMES (Profiles[PROSPEROLIGHT_HAPTICS_PROFILE].frames)
#endif
#ifndef PROSPEROLIGHT_HAPTICS_MIN_US
#define PROSPEROLIGHT_HAPTICS_MIN_US (Profiles[PROSPEROLIGHT_HAPTICS_PROFILE].minimum_us)
#endif
#ifndef PROSPEROLIGHT_HAPTICS_FRAGMENT_US
#define PROSPEROLIGHT_HAPTICS_FRAGMENT_US (Profiles[PROSPEROLIGHT_HAPTICS_PROFILE].fragment_us)
#endif
constexpr PrimingPolicy SelectedPolicy{PROSPEROLIGHT_HAPTICS_PRIME_FRAMES,
                                       PROSPEROLIGHT_HAPTICS_MIN_US,
                                       PROSPEROLIGHT_HAPTICS_FRAGMENT_US};
constexpr unsigned PrimeFrames = SelectedPolicy.frames;
static_assert(SelectedPolicy.frames > 0 && SelectedPolicy.minimum_us > 0 &&
              SelectedPolicy.fragment_us >= SelectedPolicy.minimum_us);
struct DistributionSummary
{
    uint64_t samples = 0, p50 = 0, p95 = 0, p99 = 0, maximum = 0;
};
// Bounded histograms. Percentiles are bucket upper bounds (1ms or 32 frames),
// including a conservative maximum for the overflow bucket. No callback work.
struct Distribution
{
    static constexpr unsigned Bins = 128;
    uint64_t buckets[Bins]{}, samples = 0, maximum = 0;
    uint32_t quantum;
    explicit constexpr Distribution(uint32_t unit = 1000) : quantum(unit)
    {
    }
    void Observe(uint64_t value);
    DistributionSummary Summary() const;
};
struct RenderMetadata
{
    uint64_t arrival[Grain]{};
    unsigned frames = 0, queue_frames = 0;
};
constexpr unsigned Capacity = PROSPEROLIGHT_HAPTICS_QUEUE_FRAMES;
static_assert(Capacity >= Grain && Capacity <= 4096);
// No allocation. A discontinuity discards stale queued data, never repeats it.
struct Buffer
{
    int16_t samples[Capacity * Channels]{};
    uint64_t arrival[Capacity]{};
    unsigned head = 0, count = 0, nonzero_frames = 0;
    uint32_t sequence = 0;
    bool sequenced = false;
    uint64_t overflow = 0, padded_frames = 0, input_frames = 0, dequeued_frames = 0,
             ignored_silent_frames = 0;
    uint64_t received = 0, dropped = 0, duplicate = 0, out_of_order = 0, gaps = 0, underruns = 0;
    void Discard(); // Playout only: input sequence remains valid across idle silence.
    void Clear();   // New controller/connection epoch: also reset the input sequence.
    bool Push(uint32_t next, const uint8_t *pcm, unsigned frames, uint64_t received_us = 0,
              bool store = true);
    unsigned Pop(int16_t *output, RenderMetadata *metadata = nullptr);
};
// Pure, clock-injected playout policy shared by native workers and deterministic tests.
struct Playout
{
    Buffer buffer{};
    PrimingPolicy policy;
    Distribution receive_to_output{}, blocking{}, first_nonzero_delay{}, queue_depth{32};
    uint64_t first_packet = 0, last_packet = 0, last_nonzero = 0, pending_pad = 0;
    uint64_t first_nonzero = 0, starvation_frames = 0;
    bool effect = false, primed = false, first_pending = false;
    explicit constexpr Playout(PrimingPolicy config = SelectedPolicy) : policy(config)
    {
    }
    void Reset(bool reset_sequence);
    bool Receive(uint32_t sequence, const uint8_t *pcm, unsigned frames, uint64_t arrival);
    bool WantsOutput(uint64_t now);
    unsigned Pop(int16_t *output, RenderMetadata *metadata = nullptr);
    void RecordOutput(uint64_t start, uint64_t blocked_us, const int16_t *output,
                      const RenderMetadata &metadata, bool accepted);
};
struct Statistics
{
    uint64_t overflow, inbox_full, stale, rejected, output_stalls, padded_frames, output_calls;
    uint64_t input_frames, dequeued_frames, starvation_frames, ignored_silent_frames;
    DistributionSummary receive_to_output, blocking, first_nonzero_delay, queue_depth;
    uint64_t received, dropped, duplicate, out_of_order, gaps, underruns, contention, output_errors;
    int open_result, state_result, output_result, close_result;
    uint16_t port_output;
    uint8_t port_channels;
    bool ready, active;
};
// Configure/Disable only change desired ownership. All AudioOut I/O is on workers.
void Configure(unsigned slot, int32_t user);
void Disable(unsigned slot);
void DisableAll();
bool Ready(unsigned slot);
bool Active(unsigned slot);
uint64_t ActiveToken(unsigned slot);
// The pad owner zeros legacy motors and acknowledges the current effect token.
// Acknowledgements name the observed token: a delayed pad poll cannot authorize
// a replacement effect, stall recovery or a new connection epoch.
void Allow(unsigned slot, uint64_t observed_token);
bool AnyReady();
void Push(unsigned slot, uint32_t sequence, const uint8_t *pcm, unsigned frames);
void Contended(unsigned slot);
Statistics GetStatistics(unsigned slot);
} // namespace prosperolight::waveform
