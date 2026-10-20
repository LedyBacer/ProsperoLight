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
    w.elapsed_us = 2000000; w.submitted = w.decoded = w.presented = 120;
    w.incoming_estimate_valid = true;
    return w;
}
int main()
{
    auto w = window(); assert(classify(w, 60) == Bottleneck::Healthy);
    w.submitted = w.decoded = w.presented = 60; // Valid 30 FPS source, not overload.
    assert(classify(w, 60) == Bottleneck::Healthy);
    w = window(); w.pending_end = 8; w.queue_mean_us = 80000; w.busy_us = 1900000;
    assert(classify(w, 60) == Bottleneck::Decode);
    w.losses = 1; assert(classify(w, 60) == Bottleneck::Insufficient);
    w = window(); w.losses = 1; assert(classify(w, 60) == Bottleneck::Network);
    w = window(); w.presented = 90; assert(classify(w, 60) == Bottleneck::Presentation);
    w = window(); w.submitted = 1; assert(classify(w, 60) == Bottleneck::Insufficient);
    PipelineSampler sampler;
    PipelineCounters c{}; c.generation = 1;
    assert(!sampler.update(1000000, c, 60));
    c.submitted = c.decoded = c.presented = 100; c.pending = 2;
    assert(sampler.update(3000000, c, 60));
    assert(sampler.last.incoming_fps_x100 == 5100 && sampler.last.decoded_fps_x100 == 5000);
    c.submitted += 100; c.decoded += 100; c.presented += 100; c.pending = 4; ++c.overflows;
    assert(sampler.update(5000000, c, 60) && !sampler.last.incoming_estimate_valid);
    ++c.generation; assert(!sampler.update(7000000, c, 60));
    auto sp = fixture::h264(1920,1080,1,42,true,2);
    Requirements r{}; assert(parse_sps(Codec::H264,sp.data(),sp.size(),r) == ParseResult::Valid);
    assert(initial_plan(r, {}, 5, 7).depth == 1);
    Limits limits; limits.depth = 3;
    assert(initial_plan(r, limits, 2, 7).depth == 2);
    assert(!initial_plan(r, limits, 5, 1).supported);
    fixture::Writer avc;
    avc.ue(0); avc.ue(0); avc.put(1); avc.put(0); avc.ue(0); avc.ue(0); avc.ue(0);
    auto pp = avc.nal(Codec::H264); pp[4] = 0x68;
    auto f = pps_features(Codec::H264,pp.data(),pp.size()); assert(f.known && f.cabac);
    fixture::Writer hevc;
    hevc.ue(0); hevc.ue(0); hevc.zero(7); hevc.ue(0); hevc.ue(0); hevc.ue(0);
    hevc.zero(2); hevc.put(0); hevc.ue(0); hevc.ue(0); hevc.zero(4);
    hevc.put(1); hevc.put(1); hevc.ue(1); hevc.ue(0);
    pp = hevc.nal(Codec::Hevc); pp[4] = 0x44;
    f = pps_features(Codec::Hevc,pp.data(),pp.size());
    assert(f.known && f.tiles && f.wpp && f.tile_columns == 2 && f.tile_rows == 1);
    for (size_t n=0;n<7;++n) assert(!pps_features(Codec::Hevc,pp.data(),n).known);
}
