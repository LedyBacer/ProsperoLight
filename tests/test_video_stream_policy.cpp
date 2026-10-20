/*
 * ps5-native-app-boilerplate - Source-independent video policy regressions.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "video_stream_policy.hpp"
#include "video_parameter_sets.hpp"
#include "video_sps_fixtures.hpp"
#include <cassert>
using namespace moonlight::video;
static PipelineWindow window()
{
    PipelineWindow w{};
    w.elapsed_us = 2000000;
    w.submitted = w.decoded = w.presented = 120;
    w.incoming_estimate_valid = true;
    return w;
}
static RuntimePolicy experimental_policy()
{
    RuntimePolicy p;
    p.allow_reconfiguration = true;
    return p;
}
int main()
{
    auto w = window();
    assert(classify(w, 60) == Bottleneck::Healthy);
    w.submitted = w.decoded = w.presented = 60; // Valid 30 FPS source, not overload.
    assert(classify(w, 60) == Bottleneck::Healthy);
    w = window();
    w.pending_end = 8;
    w.queue_mean_us = 80000;
    w.busy_us = 1900000;
    assert(classify(w, 60) == Bottleneck::Decode);
    w.losses = 1;
    assert(classify(w, 60) == Bottleneck::Insufficient);
    w = window();
    w.losses = 1;
    assert(classify(w, 60) == Bottleneck::Network);
    w = window();
    w.presented = 90;
    assert(classify(w, 60) == Bottleneck::Presentation);
    w = window();
    w.submitted = 1;
    assert(classify(w, 60) == Bottleneck::Insufficient);
    PipelineSampler sampler;
    PipelineCounters c{};
    c.generation = 1;
    assert(!sampler.update(1000000, c, 60));
    c.submitted = c.decoded = c.presented = 100;
    c.pending = 2;
    assert(sampler.update(3000000, c, 60));
    assert(sampler.last.incoming_fps_x100 == 5100 && sampler.last.decoded_fps_x100 == 5000);
    c.submitted += 100;
    c.decoded += 100;
    c.presented += 100;
    c.pending = 4;
    ++c.overflows;
    assert(sampler.update(5000000, c, 60) && !sampler.last.incoming_estimate_valid);
    ++c.generation;
    assert(!sampler.update(7000000, c, 60));
    auto sp = fixture::h264(1920, 1080, 1, 42, true, 2);
    Requirements r{};
    assert(parse_sps(Codec::H264, sp.data(), sp.size(), r) == ParseResult::Valid);
    assert(initial_plan(r, {}, 5, 7).depth == 1);
    Limits limits;
    limits.depth = 3;
    assert(initial_plan(r, limits, 2, 7).depth == 2);
    assert(!initial_plan(r, limits, 5, 1).supported);
    // Source names do not exist in this interface. Healthy/source-limited input
    // cannot tune; pressure must persist, and a trial cannot oscillate.
    auto policy = experimental_policy();
    w = window();
    w.pending_end = 8;
    w.queue_mean_us = 120000;
    w.busy_us = 1900000;
    w.ready_mean_us = 40000;
    w.in_flight = 2;
    w.incoming_fps_x100 = w.decoded_fps_x100 = w.presented_fps_x100 = 6000;
    w.bytes = 1200000;
    w.bottleneck = Bottleneck::Decode;
    assert(policy.observe(2000000, w, r, 60, 3) == PolicyAction::None);
    assert(policy.observe(4000000, w, r, 60, 3) == PolicyAction::DepthOne);
    assert(policy.trials == 1 && policy.pending_depth == 1);
    policy.configured(4000000, 1);
    assert(policy.observe(6000000, w, r, 60, 1) == PolicyAction::None);
    auto better = w;
    better.ready_mean_us = 5000;
    better.queue_mean_us = 1000;
    better.pending_end = 0;
    better.bottleneck = Bottleneck::Healthy;
    assert(policy.observe(10000000, better, r, 60, 1) == PolicyAction::None);
    assert(policy.observe(12000000, better, r, 60, 1) == PolicyAction::None && policy.kept == 1);
    assert(policy.observe(14000000, w, r, 60, 1) == PolicyAction::None);
    assert(policy.observe(16000000, w, r, 60, 1) == PolicyAction::Resync);
    for (uint64_t now = 18000000; now < 100000000; now += 2000000)
        assert(policy.observe(now, w, r, 60, 1) == PolicyAction::None);
    assert(policy.resyncs == 1 && policy.trials == 1);
    policy = experimental_policy();
    policy.observe(2000000, w, r, 60, 3);
    policy.observe(4000000, w, r, 60, 3);
    policy.configured(4000000, 1);
    auto worse = w;
    worse.queue_mean_us = 200000;
    worse.presented_fps_x100 = 4800;
    policy.observe(10000000, worse, r, 60, 1);
    assert(policy.observe(12000000, worse, r, 60, 1) == PolicyAction::RestoreDepth);
    assert(policy.pending_depth == 3 && policy.rollbacks == 1);
    policy.configured(12000000, 3);
    for (uint64_t now = 14000000; now < 100000000; now += 2000000)
        assert(policy.observe(now, w, r, 60, 3) == PolicyAction::None);
    policy = experimental_policy();
    auto no_pressure = window();
    no_pressure.submitted = no_pressure.decoded = no_pressure.presented = 50;
    no_pressure.bottleneck = classify(no_pressure, 60);
    for (uint64_t now = 2000000; now < 20000000; now += 2000000)
        assert(policy.observe(now, no_pressure, r, 60, 1) == PolicyAction::None);
    assert(!policy.throughput_exceeded && !policy.resyncs);
    policy = experimental_policy();
    auto with_loss = w;
    with_loss.losses = 1;
    for (uint64_t now = 2000000; now < 20000000; now += 2000000)
        assert(policy.observe(now, with_loss, r, 60, 3) == PolicyAction::None);
    policy = experimental_policy();
    auto reordered = r;
    reordered.reorder = 2;
    policy.observe(2000000, w, reordered, 60, 3);
    assert(policy.observe(4000000, w, reordered, 60, 3) == PolicyAction::None && !policy.trials);
    policy = experimental_policy();
    policy.observe(2000000, w, r, 60, 3, 123);
    policy.observe(4000000, w, r, 60, 3, 123);
    policy.configured(4000000, 1);
    policy.discontinuity(6000000, true);
    assert(!policy.evaluating && policy.depth_locked && !policy.pending_depth);
    policy = experimental_policy();
    policy.observe(2000000, w, r, 60, 3, 123);
    assert(policy.observe(4000000, w, r, 60, 3, 124) == PolicyAction::None);
    assert(!policy.pending_depth && policy.depth_locked);
    // Production cannot start an unverified decoder recreation, even under pressure.
    RuntimePolicy production;
    for (uint64_t now = 2000000; now < 50000000; now += 2000000)
        assert(production.observe(now, w, r, 60, 3) == PolicyAction::None);
    assert(!production.trials && !production.pending_depth);
    // Invalid observations never establish benefit, but cannot stop the deadline.
    for (unsigned invalid = 0; invalid < 5; ++invalid)
    {
        policy = experimental_policy();
        policy.observe(2000000, w, r, 60, 3);
        policy.observe(4000000, w, r, 60, 3);
        policy.configured(4000000, 1);
        auto bad = w;
        if (invalid == 0)
            bad.losses = 1;
        if (invalid == 1)
            bad.submitted = 0;
        if (invalid == 2)
            bad.elapsed_us = 6000000;
        if (invalid == 3)
            bad.incoming_estimate_valid = false;
        if (invalid == 4)
            bad.elapsed_us = 0;
        for (uint64_t now = 6000000; now < 24000000; now += 2000000)
            assert(policy.observe(now, bad, r, 60, 1) == PolicyAction::None);
        assert(policy.evaluating && !policy.kept);
        assert(policy.observe(24000000, bad, r, 60, 1) == PolicyAction::RestoreDepth);
        assert(policy.pending_depth == 3 && policy.rollbacks == 1 && !policy.evaluating);
        assert(policy.observe(26000000, w, r, 60, 1) == PolicyAction::None);
        policy.configured(26000000, 3);
        for (uint64_t now = 28000000; now < 100000000; now += 2000000)
            assert(policy.observe(now, w, r, 60, 3) == PolicyAction::None);
        assert(policy.rollbacks == 1 && policy.trials == 1);
    }
    // No timer/worker spins during a source pause; first resumed observation expires it.
    policy = experimental_policy();
    policy.observe(2000000, w, r, 60, 3);
    policy.observe(4000000, w, r, 60, 3);
    policy.configured(4000000, 1);
    policy.discontinuity(40000000, false);
    assert(policy.observe(40000000, {}, r, 60, 1) == PolicyAction::RestoreDepth);
    c.parameter_identity = 124;
    assert(!sampler.update(9000000, c, 60));
    fixture::Writer avc;
    avc.ue(0);
    avc.ue(0);
    avc.put(1);
    avc.put(0);
    avc.ue(0);
    avc.ue(0);
    avc.ue(0);
    auto pp = avc.nal(Codec::H264);
    pp[4] = 0x68;
    auto f = pps_features(Codec::H264, pp.data(), pp.size());
    assert(f.known && f.cabac);
    fixture::Writer hevc;
    hevc.ue(0);
    hevc.ue(0);
    hevc.zero(7);
    hevc.ue(0);
    hevc.ue(0);
    hevc.ue(0);
    hevc.zero(2);
    hevc.put(0);
    hevc.ue(0);
    hevc.ue(0);
    hevc.zero(4);
    hevc.put(1);
    hevc.put(1);
    hevc.ue(1);
    hevc.ue(0);
    pp = hevc.nal(Codec::Hevc);
    pp[4] = 0x44;
    f = pps_features(Codec::Hevc, pp.data(), pp.size());
    assert(f.known && f.tiles && f.wpp && f.tile_columns == 2 && f.tile_rows == 1);
    for (size_t n = 0; n < 7; ++n)
        assert(!pps_features(Codec::Hevc, pp.data(), n).known);
}
