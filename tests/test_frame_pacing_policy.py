# ProsperoLight - Source-clock pacing and bounded ready queue regression checks.
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
int main() {
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
}
''')
            binary = pathlib.Path(directory) / 'pacing'
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'include'), str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
