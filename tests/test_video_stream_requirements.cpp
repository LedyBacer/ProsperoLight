/*
 * ps5-native-app-boilerplate - Native video lifecycle.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "video_decoder_lifecycle.hpp"
#include <cassert>
#include <vector>
using namespace moonlight::video;
#include "video_sps_fixtures.hpp"
using namespace fixture;
template <class Bytes> static Requirements parsed(Codec codec, const Bytes &nal)
{
    Requirements r{};
    assert(parse_sps(codec, nal.data(), nal.size(), r) == ParseResult::Valid);
    return r;
}
int main()
{
    const auto vaapi = h264(2560, 1440, 9, 51);
    auto r = parsed(Codec::H264, vaapi);
    assert(r.width == 2560 && r.height == 1440 && r.references == 9 && r.buffering == 10 &&
           r.standard_dpb == 12);
    auto p = plan(r, {});
    assert(p.supported && p.dpb == 11);
    r = parsed(Codec::H264, h264(2560, 1440, 9, 52, false));
    assert(r.standard_dpb == 12 && r.buffering == 12 && plan(r, {}).dpb == 13);
    r = parsed(Codec::H264, h264(3840, 2160, 4, 51, false));
    assert(r.standard_dpb == 5 && r.visible_height == 2160 && r.height == 2160);
    r = parsed(Codec::H264, h264(3840, 2160, 4, 60, false));
    assert(r.standard_dpb == 16);
    Limits high{};
    high.level_h264 = 60;
    assert(plan(r, high).dpb == 17);
    r = parsed(Codec::H264, h264(1920, 1080, 1, 42, false));
    assert(r.height == 1088 && r.visible_height == 1080 && r.standard_dpb == 4);
    assert(h264_dpb_mbs(51) == 184320 && h264_dpb_mbs(52) == 184320 && h264_dpb_mbs(60) == 696320 &&
           !h264_dpb_mbs(99));
    Requirements unchanged = r;
    for (size_t n = 0; n < vaapi.size(); ++n)
    {
        Requirements scratch{};
        assert(parse_sps(Codec::H264, vaapi.data(), n, scratch) != ParseResult::Valid);
        assert(!scratch.verified);
    }
    auto bad = h264(2560, 1440, 14, 51, true, 10);
    assert(parse_sps(Codec::H264, bad.data(), bad.size(), r) == ParseResult::Invalid);
    const uint8_t illegal[]{0x67, 0, 0, 3, 4};
    assert(parse_sps(Codec::H264, illegal, sizeof(illegal), r) == ParseResult::Invalid);
    for (auto depth : {8u, 10u})
    {
        auto nal = hevc(3840, 2160, depth, 5);
        auto h = parsed(Codec::Hevc, nal);
        assert(h.buffering == 5 && h.standard_dpb == 6 && h.bit_depth == depth);
        Limits l{};
        l.bit_depth = depth;
        assert(plan(h, l).supported && plan(h, l).dpb == 6);
        l.bit_depth = depth == 8 ? 10 : 8;
        assert(!plan(h, l).supported);
        for (size_t n = 0; n < nal.size(); ++n)
        {
            Requirements scratch{};
            assert(parse_sps(Codec::Hevc, nal.data(), n, scratch) != ParseResult::Valid);
        }
    }
    // Deterministic bounded mutation corpus: malformed UE fields must never
    // escape their bounds into dimension arithmetic or syntax loop counts.
    uint32_t seed = 137;
    for (unsigned trial = 0; trial < 4000; ++trial)
    {
        auto mutated = trial & 1 ? vaapi : hevc(3840, 2160, 10, 5);
        seed = seed * 1664525u + 1013904223u;
        const auto at = 5 + seed % (mutated.size() - 5);
        mutated[at] ^= uint8_t(seed >> 24);
        Requirements scratch{};
        (void)parse_sps(trial & 1 ? Codec::H264 : Codec::Hevc, mutated.data(), mutated.size(),
                        scratch);
    }
    const auto encoded = parsed(Codec::H264, encoded_h264_9refs);
    assert(encoded.references == 9 && encoded.buffering == 9 && encoded.reorder == 2);
    assert(encoded.width == 2560 && encoded.height == 1440 && encoded.standard_dpb == 12);
    assert(plan(encoded, {}).supported && plan(encoded, {}).dpb == 10);
    auto main = parsed(Codec::Hevc, encoded_hevc_main);
    auto main10 = parsed(Codec::Hevc, encoded_hevc_main10);
    assert(main.profile == 1 && main10.profile == 2 && main.bit_depth == 8 &&
           main10.bit_depth == 10);
    assert(main.width == 1920 && main.height == 1088 && main.visible_height == 1080);
    assert(main.buffering == 5 && main.reorder == 2 && main.standard_dpb == 6);
    Limits hdr_limits{};
    hdr_limits.bit_depth = 10;
    assert(plan(main10, hdr_limits).supported && !plan(main10, {}).supported);
    Lifecycle life;
    life.wait(100);
    assert(!life.expired(100));
    assert(life.expired(100 + Lifecycle::startup_timeout_us));
    life.configured(unchanged, plan(unchanged, {}), 200, 10);
    assert(life.generation == 1 && life.initialization_us == 100);
    auto same = unchanged;
    ++same.identity;
    assert(!life.changed(same));
    ++same.buffering;
    assert(life.changed(same));
    assert(life.recover(Error::Capacity));
    life.configured(unchanged, plan(unchanged, {}), 300, 20);
    assert(life.generation == 2 && life.reconfigurations == 1);
    assert(life.recover(Error::Reset));
    assert(!life.recover(Error::Reset));
    assert(life.state == State::Failed);
    life.state = State::Stopping;
    assert(!life.recover(Error::Capacity));
    Lifecycle transient;
    transient.wait(1);
    transient.configured(unchanged, plan(unchanged, {}), 2, 1);
    assert(transient.corruption() && transient.state == State::Recovering &&
           transient.reason == Error::Corruption);
    transient.output(false);
    assert(transient.state == State::Ready && transient.corruptions == 0 &&
           transient.attempts == 0);
    assert(transient.recover(Error::Capacity));
    transient.output(true);
    assert(transient.state == State::Ready && transient.successes == 1);
    transient.state = State::Stopping;
    transient.output(true);
    assert(transient.state == State::Stopping && !transient.corruption());
    Lifecycle rejected;
    rejected.wait(1);
    rejected.fail(Error::Unsupported);
    assert(rejected.failures == 1 && rejected.failed_recoveries == 0);
    Lifecycle failed_recovery;
    failed_recovery.wait(1);
    assert(failed_recovery.recover(Error::Capacity));
    failed_recovery.fail(Error::Allocation);
    assert(failed_recovery.failures == 1 && failed_recovery.failed_recoveries == 1);
    Lifecycle corruption;
    for (unsigned i = 0; i < Lifecycle::corruption_budget; ++i)
        assert(corruption.corruption());
    assert(!corruption.corruption());
}
