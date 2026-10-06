# ps5-native-app-boilerplate / ProsperoLight - Source-clock pacing and bounded ready queue regression checks.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

class FramePacingPolicy(unittest.TestCase):
    def test_replay(self):
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / 'pacing.cpp'
            source.write_text(r'''
#include "frame_pacing.hpp"
#include "moonlight_pipeline.hpp"
#include <cassert>
#include <cstring>
int main() {
    moonlight::SourceTimestamp clock;
    const auto before=clock.update(0xfffffff0u,0);
    const auto after=clock.update(734u,0);
    assert(after>before && after-before>=8333 && after-before<=8334);
    assert(clock.update(734u,42)==42);
    moonlight::ReadyMailbox<int> q;
    q.capacity=2;
    int displaced=0, out=0;
    assert(!q.publish(1,&displaced) && !q.publish(2,&displaced));
    assert(q.publish(3,&displaced) && displaced==1);
    assert(q.take(&out) && out==2);
    assert(q.take(&out) && out==3 && !q.full);
    q.capacity=1;
    assert(!q.publish(4,&displaced));
    assert(q.publish(5,&displaced) && displaced==4);
    assert(q.take(&out) && out==5);
    for(unsigned fps: {30u,60u,75u,90u,120u}) {
        moonlight::FramePacing p;
        p.reset(fps);
        uint64_t previous=0;
        for(int frame=1;frame<1000;frame++) {
            const uint64_t pts=uint64_t(frame)*1000000/fps;
            const uint64_t jitter=frame%53==0?11000:frame%3*300;
            uint64_t ready=1000000+pts+jitter;
            if(frame>1 && ready<previous) ready=previous;
            auto target=p.target(frame,pts,ready);
            assert(target>=ready);
            if(previous) assert(target>=previous+uint64_t(1000000/fps)*97/100);
            assert(p.stats.reserve_us<=10000 && p.stats.reserve_us<=p.stats.period_us);
            p.submitted(target,target, target-ready);
            previous=target;
        }
        // Timestamp discontinuity and long host stall recover without an
        // unbounded backlog, and the only remaining image can still be shown.
        auto target=p.target(2000,1,previous+2000000);
        assert(target>=previous+2000000 && target<=previous+2010000);
        assert(p.stats.resets);
    }
    // Capture jitter alternates long and short PTS deltas for twenty minutes.
    // The source is still 60 FPS; cadence must not drift into a 40 FPS cap.
    moonlight::FramePacing jittered;
    jittered.reset(60);
    uint64_t previous_jittered=0;
    for(int frame=1;frame<72000;frame++) {
        uint64_t source=uint64_t(frame)*1000000/60+(frame%2?10000:0);
        uint64_t ready=1000000+uint64_t(frame)*1000000/60;
        auto target=jittered.target(frame,source,ready);
        jittered.submitted(target,target,target-ready);
        previous_jittered=target;
    }
    assert(previous_jittered>0);
    assert(jittered.stats.period_us>16500 && jittered.stats.period_us<16800);
    // Fixed 60-on-120 must not rush two frames into adjacent refreshes.
    moonlight::FramePacing fixed;
    fixed.reset(60);
    auto first=fixed.target(1,16666,1000000,11988,991660);
    fixed.submitted(first,first,0);
    auto second=fixed.target(2,33333,1000001,11988,first+250);
    assert(second-first>=16000);
    moonlight::FramePacing p;
    p.reset(120);
    uint64_t last=0;
    for(int f=1;f<400;f++) {
        uint64_t source=f<150?uint64_t(f)*8333:149*8333+uint64_t(f-149)*16666;
        auto target=p.target(f,source,1000000+source);
        p.submitted(target,target,0);
        assert(!last || target>last);
        last=target;
    }
    assert(p.stats.period_us>16000 && p.stats.period_us<17000);
    // 120fps on a matching 119.88Hz display, ready just after the flip.
    // VSync already waits for the next vblank. A deadline near that vblank
    // misses it and the picture stays at half rate.
    moonlight::FramePacing vsync;
    vsync.reset(120);
    const uint64_t anchor=5000000;
    const uint64_t display_period=UINT64_C(100000000)/11988;
    const uint64_t ready=anchor+200;
    const auto early=vsync.target(1,8333,ready,11988,anchor,0,1000);
    assert(early>=ready);
    assert(early<anchor+display_period/2);
    assert(std::strcmp(moonlight::effective_pacing_name(2, false), "Paced")==0);
    assert(std::strcmp(moonlight::effective_pacing_name(2, true), "Paced+VRR")==0);
    assert(std::strcmp(moonlight::effective_pacing_name(1, false), "Paced")==0);
    assert(std::strcmp(moonlight::effective_pacing_name(0, true), "Unpaced")==0);
}
''')
            binary = pathlib.Path(directory) / 'pacing'
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'include'), str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
