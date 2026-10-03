/*
 * ps5-native-app-boilerplate / ProsperoLight - Decode/present pipeline policies.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace moonlight
{
// A title owns logical CPUs 0-12. SMT siblings are adjacent: (0,1) ... (10,11);
// CPU 12's sibling belongs to the system. Videodec2's historical 0x3f is
// therefore three physical cores, not six.
constexpr uint64_t kTitleCpuMask = 0x1fffu;
constexpr uint64_t kClassicDecoderCpuMask = 0x3fu;
constexpr unsigned kMinDecoderCores = 3u;
constexpr unsigned kMaxDecoderCores = 5u;
constexpr unsigned kMaxDecoderDepth = 3u;

// Whole physical cores (both SMT threads) from the lowest CPU upward, always
// leaving at least two CPUs of the title for receive, decode and presentation.
inline uint64_t decoder_cpu_mask(unsigned cores, uint64_t process_mask)
{
    if (cores < kMinDecoderCores)
        cores = kMinDecoderCores;
    if (cores > kMaxDecoderCores)
        cores = kMaxDecoderCores;
    uint64_t mask = 0;
    unsigned taken = 0;
    for (unsigned cpu = 0; cpu + 1u < 64u && taken < cores; cpu += 2u)
    {
        const uint64_t pair = UINT64_C(3) << cpu;
        if ((process_mask & pair) != pair)
            continue;
        const uint64_t candidate = mask | pair;
        if (__builtin_popcountll(process_mask & ~candidate) < 2)
            break;
        mask = candidate;
        ++taken;
    }
    return mask;
}

struct ThreadLayout
{
    uint64_t receive{}, decode{}, present{}, other{};
};

// Keep every stream thread off the decoder's CPUs. The receive thread gets the
// lowest remaining CPU to itself; the decode thread, which mostly blocks in
// Videodec2, takes the highest. Keep background workers off the decode caller
// and presenter when capacity permits, including their SMT siblings if a whole
// spare core remains. Scarce layouts still share to keep audio/input running.
inline ThreadLayout plan_thread_layout(uint64_t process_mask, uint64_t decoder_mask)
{
    ThreadLayout layout{};
    const uint64_t rest = process_mask & ~decoder_mask;
    if (!rest)
        return layout;
    const uint64_t lowest = rest & (~rest + 1u);
    const uint64_t highest = UINT64_C(1) << (63 - __builtin_clzll(rest));
    if (lowest == highest)
    {
        layout.receive = layout.decode = layout.present = layout.other = rest;
        return layout;
    }
    layout.receive = lowest;
    layout.decode = highest;
    layout.other = rest & ~lowest;
    const uint64_t below_highest = layout.other & ~highest;
    layout.present = below_highest ? UINT64_C(1) << (63 - __builtin_clzll(below_highest)) : highest;
    const uint64_t critical = layout.receive | layout.decode | layout.present;
    const uint64_t spare = rest & ~critical;
    if (spare)
    {
        const uint64_t siblings = ((critical & UINT64_C(0x5555555555555555)) << 1u) |
                                  ((critical & UINT64_C(0xaaaaaaaaaaaaaaaa)) >> 1u);
        const uint64_t isolated = spare & ~siblings;
        layout.other = isolated ? isolated : spare;
    }
    return layout;
}

// Drain pending decoder output only while nothing else is queued: pipelined
// throughput when behind, depth-one latency when keeping up.
inline bool should_drain(bool drain_enabled, unsigned depth, unsigned in_flight, int pending_frames)
{
    return drain_enabled && depth > 1u && in_flight > 0u && pending_frames <= 0;
}

// Optional bounded catch-up: a sustained backlog of at least `threshold`
// queued frames for `hold_us` requests a keyframe instead of letting latency
// climb to moonlight-common-c's 15-frame overflow. Zero disables it.
struct CatchUpGuard
{
    uint64_t since_us{};

    bool update(uint64_t now_us, int pending, unsigned threshold, uint64_t hold_us)
    {
        if (!threshold || pending < static_cast<int>(threshold))
        {
            since_us = 0;
            return false;
        }
        if (!since_us || now_us < since_us)
        {
            since_us = now_us ? now_us : 1u;
            return false;
        }
        if (now_us - since_us < hold_us)
            return false;
        since_us = 0;
        return true;
    }
};

// Fixed-size slot ownership. Slots are only handed out while Free, and the
// most recent decoder output is never handed back to the decoder before a
// newer output exists.
template <size_t N> struct SlotPool
{
    enum State : uint8_t
    {
        Free,
        Decoding,
        Ready,
        Presenting
    };
    std::array<State, N> state{};
    std::array<uint64_t, N> released{};
    uint64_t sequence{};
    int last_output = -1;

    int acquire_free() const
    {
        int best = -1;
        for (size_t i = 0; i < N; ++i)
        {
            if (state[i] != Free || static_cast<int>(i) == last_output)
                continue;
            if (best < 0 || released[i] < released[static_cast<size_t>(best)])
                best = static_cast<int>(i);
        }
        return best;
    }

    void release(int slot)
    {
        if (slot < 0 || static_cast<size_t>(slot) >= N)
            return;
        state[static_cast<size_t>(slot)] = Free;
        released[static_cast<size_t>(slot)] = ++sequence;
    }

    unsigned count(State wanted) const
    {
        unsigned total = 0;
        for (State value : state)
            total += value == wanted;
        return total;
    }

    // Decoder reset: every picture the decoder owned is abandoned.
    void release_decoding()
    {
        for (size_t i = 0; i < N; ++i)
            if (state[i] == Decoding)
                release(static_cast<int>(i));
    }
};

// One-slot "newest wins" handoff from decoding to presentation.
template <typename T> struct ReadyMailbox
{
    bool full{};
    T item{}, following{};
    unsigned capacity = 1;
    bool second{};

    bool publish(const T &next, T *displaced)
    {
        if (!full)
        {
            item = next;
            full = true;
            return false;
        }
        if (capacity > 1 && !second)
        {
            following = next;
            second = true;
            return false;
        }
        if (displaced)
            *displaced = item;
        if (second)
        {
            item = following;
            following = next;
        }
        else
            item = next;
        return true;
    }

    bool take(T *out)
    {
        if (!full)
            return false;
        if (out)
            *out = item;
        if (second)
        {
            item = following;
            second = false;
        }
        else
            full = false;
        return true;
    }
};

// Frame-number gaps are either network loss or frames discarded by decoder
// backpressure (moonlight-common-c's queue overflow, or a decoder refresh we
// requested). The recovery epoch changes whenever the latter happens.
struct DropAttribution
{
    bool initialized{};
    int32_t last_frame{};
    uint32_t last_epoch{};
    uint64_t network{}, decoder{};

    void observe(int32_t frame, uint32_t recovery_epoch)
    {
        if (initialized && frame > last_frame + 1)
        {
            const uint64_t gap = static_cast<uint64_t>(frame - last_frame - 1);
            if (recovery_epoch != last_epoch)
                decoder += gap;
            else
                network += gap;
        }
        initialized = true;
        last_frame = frame;
        last_epoch = recovery_epoch;
    }
};

// Host send rate from frame numbers over enqueue time, so frames discarded
// before decoding still count as arrivals.
struct ArrivalRate
{
    uint64_t start_us{};
    int32_t start_frame{};
    bool initialized{};
    uint32_t fps_x100{};

    bool update(uint64_t enqueue_us, int32_t frame)
    {
        if (!initialized || enqueue_us < start_us || frame < start_frame)
        {
            start_us = enqueue_us;
            start_frame = frame;
            initialized = true;
            return false;
        }
        const uint64_t elapsed = enqueue_us - start_us;
        if (elapsed < 1000000u)
            return false;
        fps_x100 = static_cast<uint32_t>(static_cast<uint64_t>(frame - start_frame) *
                                         UINT64_C(100000000) / elapsed);
        start_us = enqueue_us;
        start_frame = frame;
        return true;
    }
};

// One-second windows for the HUD: mean, p95 and max of the last full window,
// plus the busy share of wall time (decoder load).
struct WindowedTiming
{
    static constexpr unsigned kBuckets = 160; // 0.25 ms buckets up to 40 ms
    std::array<uint32_t, kBuckets> buckets{};
    uint64_t start_us{}, total_us{}, max_us{};
    uint32_t count{};
    uint32_t last_count{};
    uint64_t last_mean_us{}, last_p95_us{}, last_max_us{};
    uint32_t last_load_permille{};

    void add(uint64_t now_us, uint64_t sample_us)
    {
        if (!start_us || now_us < start_us)
            start_us = now_us ? now_us : 1u;
        const uint64_t bucket = sample_us / 250u;
        ++buckets[bucket < kBuckets ? bucket : kBuckets - 1u];
        total_us += sample_us;
        if (sample_us > max_us)
            max_us = sample_us;
        ++count;
        const uint64_t elapsed = now_us - start_us;
        if (elapsed < 1000000u)
            return;
        last_count = count;
        last_mean_us = total_us / count;
        last_max_us = max_us;
        last_load_permille =
            static_cast<uint32_t>(total_us >= elapsed ? 1000u : total_us * 1000u / elapsed);
        const uint64_t rank = (static_cast<uint64_t>(count) * 95u + 99u) / 100u;
        uint64_t cumulative = 0;
        last_p95_us = max_us;
        for (unsigned i = 0; i < kBuckets; ++i)
        {
            cumulative += buckets[i];
            if (cumulative >= rank)
            {
                const uint64_t upper = (i + 1u) * 250u;
                last_p95_us = i + 1u == kBuckets || upper > max_us ? max_us : upper;
                break;
            }
        }
        buckets.fill(0);
        start_us = now_us;
        total_us = max_us = 0;
        count = 0;
    }
};
} // namespace moonlight
