/*
 * ps5-native-app-boilerplate / ProsperoLight - Stream-aware native video
 * decoding. Copyright (C) 2026 BlackBearReloaded SPDX-License-Identifier:
 * GPL-3.0-or-later
 */
#include "video_decoder_lifecycle.hpp"
#include <cassert>
#include <vector>
using namespace moonlight::video;
#include "video_sps_fixtures.hpp"
using namespace fixture;
static Requirements parsed(Codec codec, const std::vector<uint8_t> &nal)
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
    Lifecycle corruption;
    for (unsigned i = 0; i < Lifecycle::corruption_budget; ++i)
        assert(corruption.corruption());
    assert(!corruption.corruption());
}
