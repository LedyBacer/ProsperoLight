/*
 * ps5-native-app-boilerplate - Bounded, source-independent video pressure evidence.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include "video_stream_requirements.hpp"
namespace moonlight::video
{
enum class Bottleneck { Insufficient, Healthy, Network, Decode, Presentation };
inline const char *name(Bottleneck b)
{
    constexpr const char *names[]{"insufficient-evidence", "healthy", "network-limited",
                                  "decode-limited", "presentation-limited"};
    return names[static_cast<unsigned>(b)];
}
// Decode-owner totals. The only presenter fields supplied here are atomic counters.
struct PipelineCounters
{
    uint64_t submitted{}, decoded{}, presented{}, bytes{}, busy_us{}, ready_us{}, queue_us{};
    uint64_t losses{}, overflows{}, errors{}, ready_calls{};
    uint32_t pending{}, in_flight{}, generation{}, recovery_epoch{};
};
struct PipelineWindow
{
    uint64_t elapsed_us{}, submitted{}, decoded{}, presented{}, bytes{}, busy_us{};
    uint64_t ready_mean_us{}, queue_mean_us{}, losses{}, overflows{};
    uint32_t pending_begin{}, pending_end{}, in_flight{};
    uint32_t incoming_fps_x100{}, decoded_fps_x100{}, presented_fps_x100{};
    bool incoming_estimate_valid{};
    Bottleneck bottleneck{Bottleneck::Insufficient};
};
inline bool behind(uint64_t output, uint64_t input)
{
    return output + std::max<uint64_t>(2, input / 20) < input;
}
inline Bottleneck classify(const PipelineWindow &w, uint32_t fps)
{
    if (!fps || w.elapsed_us < 2000000 || w.elapsed_us > 5000000 || w.submitted < 16)
        return Bottleneck::Insufficient;
    const uint64_t budget = 1000000 / fps;
    const bool backlog = w.pending_end >= 2 && w.queue_mean_us > budget;
    const bool busy = w.busy_us >= w.elapsed_us * 7 / 10;
    const bool buffered = w.in_flight > 1 && w.ready_mean_us > budget * 2;
    const bool decode = backlog && (busy || buffered);
    // Loss and local pressure can coexist; do not assign a single root cause.
    if (w.losses) return decode ? Bottleneck::Insufficient : Bottleneck::Network;
    if (decode) return Bottleneck::Decode;
    if (behind(w.presented, w.decoded) && w.pending_end < 2)
        return Bottleneck::Presentation;
    if (!w.overflows && !backlog && !behind(w.decoded, w.submitted) &&
        !behind(w.presented, w.decoded))
        return Bottleneck::Healthy; // Delivering the source; low source FPS is valid.
    return Bottleneck::Insufficient;
}
struct PipelineSampler
{
    static constexpr uint64_t window_us = 2000000;
    PipelineCounters previous{};
    PipelineWindow last{};
    uint64_t since{}, windows{};
    bool initialized{};
    bool due(uint64_t now) const { return !initialized || now < since || now - since >= window_us; }
    bool update(uint64_t now, const PipelineCounters &c, uint32_t fps)
    {
        if (!due(now)) return false;
        if (!initialized || now < since || c.generation != previous.generation ||
            c.recovery_epoch != previous.recovery_epoch || c.errors != previous.errors ||
            c.submitted < previous.submitted || c.decoded < previous.decoded ||
            c.presented < previous.presented || c.bytes < previous.bytes ||
            c.busy_us < previous.busy_us || c.ready_us < previous.ready_us ||
            c.queue_us < previous.queue_us || c.ready_calls < previous.ready_calls ||
            c.losses < previous.losses || c.overflows < previous.overflows)
        {
            initialized = true; previous = c; since = now; last = {};
            return false;
        }
        PipelineWindow w{};
        w.elapsed_us = now - since;
        w.submitted = c.submitted - previous.submitted;
        w.decoded = c.decoded - previous.decoded;
        w.presented = c.presented - previous.presented;
        w.bytes = c.bytes - previous.bytes;
        w.busy_us = c.busy_us - previous.busy_us;
        w.losses = c.losses - previous.losses;
        w.overflows = c.overflows - previous.overflows;
        w.pending_begin = previous.pending; w.pending_end = c.pending;
        w.in_flight = c.in_flight;
        const auto outputs = c.ready_calls - previous.ready_calls;
        w.ready_mean_us = outputs ? (c.ready_us - previous.ready_us) / outputs : 0;
        w.queue_mean_us = w.submitted ? (c.queue_us - previous.queue_us) / w.submitted : 0;
        // Queue conservation estimates arrivals, not host capture FPS. Local
        // queue discard makes the estimate unavailable rather than inventing AUs.
        const int64_t received = int64_t(w.submitted) + int64_t(c.pending) - previous.pending;
        w.incoming_estimate_valid = !w.overflows && received >= 0;
        if (w.incoming_estimate_valid)
            w.incoming_fps_x100 = uint64_t(received) * 100000000 / w.elapsed_us;
        w.decoded_fps_x100 = w.decoded * 100000000 / w.elapsed_us;
        w.presented_fps_x100 = w.presented * 100000000 / w.elapsed_us;
        w.bottleneck = classify(w, fps);
        last = w; previous = c; since = now; ++windows;
        return true;
    }
};
// No brand keys or speculative concurrency. Keep the requested low-latency mode
// within existing pool bounds and derive DPB exclusively from validated SPS facts.
inline Plan initial_plan(const Requirements &r, Limits limits, uint32_t input_slots,
                         uint32_t frame_slots)
{
    limits.depth = std::min(limits.depth, std::min(input_slots, frame_slots > 1 ? frame_slots - 1 : 0));
    auto p = plan(r, limits);
    if (!limits.depth) { p.supported = false; p.reason = "insufficient frame/input slots"; }
    return p;
}
} // namespace moonlight::video
