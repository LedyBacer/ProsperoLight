/*
 * ps5-native-app-boilerplate - Bounded, source-independent video pressure evidence.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include "video_stream_requirements.hpp"
namespace moonlight::video
{
enum class Bottleneck
{
    Insufficient,
    Healthy,
    Network,
    Decode,
    Presentation
};
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
    uint64_t losses{}, overflows{}, errors{}, ready_calls{}, stream_identity{},
        parameter_identity{};
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
    const bool backlog = (w.pending_end >= 2 || w.overflows) && w.queue_mean_us > budget;
    const bool busy = w.busy_us >= w.elapsed_us * 7 / 10;
    const bool buffered = w.in_flight > 1 && w.ready_mean_us > budget * 2;
    const bool decode = backlog && (busy || buffered);
    // Loss and local pressure can coexist; do not assign a single root cause.
    if (w.losses)
        return decode ? Bottleneck::Insufficient : Bottleneck::Network;
    if (decode)
        return Bottleneck::Decode;
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
    bool due(uint64_t now) const
    {
        return !initialized || now < since || now - since >= window_us;
    }
    bool update(uint64_t now, const PipelineCounters &c, uint32_t fps)
    {
        if (!due(now))
            return false;
        if (!initialized || now < since || c.generation != previous.generation ||
            c.stream_identity != previous.stream_identity ||
            c.parameter_identity != previous.parameter_identity ||
            (c.recovery_epoch != previous.recovery_epoch &&
             (c.recovery_epoch < previous.recovery_epoch || c.overflows < previous.overflows ||
              c.recovery_epoch - previous.recovery_epoch != c.overflows - previous.overflows)) ||
            c.errors != previous.errors || c.submitted < previous.submitted ||
            c.decoded < previous.decoded || c.presented < previous.presented ||
            c.bytes < previous.bytes || c.busy_us < previous.busy_us ||
            c.ready_us < previous.ready_us || c.queue_us < previous.queue_us ||
            c.ready_calls < previous.ready_calls || c.losses < previous.losses ||
            c.overflows < previous.overflows)
        {
            initialized = true;
            previous = c;
            since = now;
            last = {};
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
        w.pending_begin = previous.pending;
        w.pending_end = c.pending;
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
        last = w;
        previous = c;
        since = now;
        ++windows;
        return true;
    }
};
// No brand keys or speculative concurrency. Keep the requested low-latency mode
// within existing pool bounds and derive DPB exclusively from validated SPS facts.
inline Plan initial_plan(const Requirements &r, Limits limits, uint32_t input_slots,
                         uint32_t frame_slots)
{
    limits.depth = std::min(
        3u, std::min(limits.depth, std::min(input_slots, frame_slots > 1 ? frame_slots - 1 : 0)));
    auto p = plan(r, limits);
    if (!limits.depth)
    {
        p.supported = false;
        p.reason = "insufficient frame/input slots";
    }
    return p;
}
enum class PolicyAction
{
    None,
    Resync,
    DepthOne,
    RestoreDepth
};
inline const char *name(PolicyAction a)
{
    constexpr const char *names[]{"none", "bounded-resync", "try-depth-one", "restore-depth"};
    return names[static_cast<unsigned>(a)];
}
// One optional depth trial, one rollback, one latency resync per stream. No
// bitrate/FPS/resolution changes, core expansion or upward depth speculation.
struct RuntimePolicy
{
    PipelineWindow baseline{};
    uint64_t identity{}, pps_identity{}, previous_window_us{}, cooldown_until{}, trial_started{};
    uint32_t pressure_windows{}, trial_windows{}, pending_depth{}, original_depth{};
    uint32_t trials{}, rollbacks{}, resyncs{}, rejected{}, kept{};
    bool evaluating{}, depth_locked{}, throughput_exceeded{};
    bool allow_reconfiguration{}; // Experimental until a real native transition is validated.
    PolicyAction pending_action{PolicyAction::None}, last_action{PolicyAction::None};
    static constexpr uint64_t cooldown_us = 30000000, evaluation_us = 20000000;
    void reject()
    {
        pending_depth = 0;
        pending_action = PolicyAction::None;
        evaluating = false;
        depth_locked = true;
        pressure_windows = 0;
        ++rejected;
    }
    void discontinuity(uint64_t now, bool native_fault)
    {
        pressure_windows = trial_windows = 0;
        if (native_fault)
        {
            // A decoder fault invalidates a performance comparison. Never undo
            // a fault-driven fallback based on an older successful sample.
            if (evaluating || pending_depth)
                reject();
            depth_locked = true;
            cooldown_until = now + cooldown_us;
        }
    }
    void configured(uint64_t now, uint32_t effective_depth)
    {
        const auto action = pending_action;
        const auto target = pending_depth;
        pending_depth = 0;
        pending_action = PolicyAction::None;
        pressure_windows = 0;
        if (effective_depth != target)
        {
            reject();
            return;
        }
        if (action == PolicyAction::DepthOne)
        {
            evaluating = true;
            trial_started = now;
            trial_windows = 0;
            cooldown_until = now + 6000000; // Exclude reconfiguration/startup.
        }
        else
        {
            evaluating = false;
            depth_locked = true;
            cooldown_until = now + cooldown_us;
        }
    }
    PolicyAction request_rollback()
    {
        pending_depth = original_depth;
        pending_action = last_action = PolicyAction::RestoreDepth;
        evaluating = false;
        depth_locked = true;
        ++rollbacks;
        return pending_action;
    }
    PolicyAction observe(uint64_t now, const PipelineWindow &w, const Requirements &r, uint32_t fps,
                         uint32_t depth, uint64_t pps_version = 0)
    {
        if (!r.verified || !fps || pending_depth)
            return PolicyAction::None;
        if (identity && (identity != r.identity || pps_identity != pps_version))
        {
            // Do not compare different SPS generations or retune indefinitely.
            reject();
            identity = r.identity;
            pps_identity = pps_version;
            previous_window_us = now;
            return PolicyAction::None;
        }
        identity = r.identity;
        pps_identity = pps_version;
        if (previous_window_us && (now < previous_window_us || now - previous_window_us > 5000000))
        {
            pressure_windows = trial_windows = 0;
        }
        previous_window_us = now;
        // Lifetime is independent of observation eligibility. The existing
        // decode-owner loop reaches this on resumed input after a source pause.
        // Startup is excluded from comparisons, not from this absolute bound.
        if (evaluating && (now < trial_started || now - trial_started >= evaluation_us))
            return request_rollback();
        if (now < cooldown_until || w.elapsed_us < 2000000 || w.elapsed_us > 5000000 ||
            w.submitted < 16 || w.losses)
        {
            pressure_windows = 0;
            return PolicyAction::None;
        }
        if (evaluating)
        {
            const bool comparable =
                w.incoming_estimate_valid && baseline.incoming_estimate_valid &&
                uint64_t(w.incoming_fps_x100) * 10 >= uint64_t(baseline.incoming_fps_x100) * 9 &&
                uint64_t(w.incoming_fps_x100) * 10 <= uint64_t(baseline.incoming_fps_x100) * 11 &&
                w.submitted && baseline.submitted &&
                (w.bytes / w.submitted) * 4 >= (baseline.bytes / baseline.submitted) * 3 &&
                (w.bytes / w.submitted) * 4 <= (baseline.bytes / baseline.submitted) * 5;
            if (comparable)
                ++trial_windows;
            const uint64_t old_age = baseline.queue_mean_us + baseline.ready_mean_us;
            const uint64_t age = w.queue_mean_us + w.ready_mean_us;
            const bool delivery_ok = uint64_t(w.presented_fps_x100) * 100 + 200 >=
                                     uint64_t(baseline.presented_fps_x100) * 95;
            const bool improvement = age + std::max<uint64_t>(1000, old_age / 10) <= old_age;
            if (comparable && trial_windows >= 2 && delivery_ok && improvement &&
                w.pending_end <= baseline.pending_end && !w.overflows)
            {
                evaluating = false;
                depth_locked = true;
                ++kept;
                return PolicyAction::None;
            }
            const bool worse =
                comparable &&
                (!delivery_ok || age > old_age + std::max<uint64_t>(1000, old_age / 10));
            if (trial_windows >= 2 && worse)
            {
                // No demonstrated latency/delivery benefit: restore the user's
                // previous working mode once, at another complete IDR.
                return request_rollback();
            }
            return PolicyAction::None;
        }
        if (w.bottleneck != Bottleneck::Decode)
        {
            pressure_windows = 0;
            return PolicyAction::None;
        }
        if (++pressure_windows < 2)
            return PolicyAction::None; // >=4s, not one slow picture.
        throughput_exceeded = true;
        const uint64_t budget = 1000000 / fps;
        if (allow_reconfiguration && depth > 1 && !r.reorder && !depth_locked && !trials &&
            w.ready_mean_us > budget * 2)
        {
            baseline = w;
            original_depth = depth;
            pending_depth = 1;
            pending_action = last_action = PolicyAction::DepthOne;
            ++trials;
            pressure_windows = 0;
            return pending_action;
        }
        if (depth == 1 && !resyncs && w.queue_mean_us > std::max<uint64_t>(100000, budget * 4) &&
            (w.pending_end >= 4 || w.overflows))
        {
            // Drop obsolete queued pictures via the existing IDR path. This
            // bounds latency once; it does not claim more decoder throughput.
            ++resyncs;
            pressure_windows = 0;
            cooldown_until = now + cooldown_us;
            return last_action = PolicyAction::Resync;
        }
        return PolicyAction::None;
    }
};
} // namespace moonlight::video
