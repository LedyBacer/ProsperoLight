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
constexpr unsigned PrimeFrames = 768;
constexpr unsigned Capacity = PROSPEROLIGHT_HAPTICS_QUEUE_FRAMES;
static_assert(Capacity >= Grain && Capacity <= 4096);
// No allocation. A discontinuity discards stale queued data, never repeats it.
struct Buffer
{
    int16_t samples[Capacity * Channels]{};
    unsigned head = 0, count = 0;
    uint32_t sequence = 0;
    bool sequenced = false;
    uint64_t overflow = 0, padded_frames = 0, input_frames = 0, dequeued_frames = 0;
    uint64_t received = 0, dropped = 0, duplicate = 0, out_of_order = 0, gaps = 0, underruns = 0;
    void Clear();
    bool Push(uint32_t next, const uint8_t *pcm, unsigned frames);
    unsigned Pop(int16_t *output);
};
struct Statistics
{
    uint64_t overflow, inbox_full, stale, rejected, output_stalls, padded_frames, output_calls;
    uint64_t input_frames, dequeued_frames, starvation_frames;
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
// The pad-output owner zeros remembered legacy motors before acknowledging PCM ownership.
void Allow(unsigned slot);
bool AnyReady();
void Push(unsigned slot, uint32_t sequence, const uint8_t *pcm, unsigned frames);
void Contended(unsigned slot);
Statistics GetStatistics(unsigned slot);
} // namespace prosperolight::waveform
