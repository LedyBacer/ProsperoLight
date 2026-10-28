# ps5-native-app-boilerplate - deterministic bounded audio jitter policy checks.
# Copyright (C) 2026 LedyBacer and contributors
# SPDX-License-Identifier: GPL-3.0-or-later
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class AudioJitterPolicyTest(unittest.TestCase):
    def test_burst_recovery_and_slow_latency_release(self):
        source = r'''
#include "audio_jitter_policy.hpp"
#include <cassert>
int main() {
    moonlight::AudioPrebufferPolicy policy;
    assert(policy.target_frames() == 480);
    policy.on_underrun(1000000);
    assert(policy.target_frames() == 480);  // isolated failure
    policy.on_underrun(1500000);
    assert(policy.target_frames() == 960);
    for (unsigned i = 0; i < 8; ++i)
        policy.on_underrun(2000000 + i * 500000);
    assert(policy.target_frames() == 3840); // bounded at 80 ms
    assert(policy.peak_frames() == 3840 && policy.increases() == 7);
    policy.on_output(46000000);
    assert(policy.target_frames() == 3840); // 45 s quiet not reached
    policy.on_output(51000000);
    assert(policy.target_frames() == 3360);
    policy.on_output(52000000);
    assert(policy.target_frames() == 3360);
    policy.on_underrun(53000000); // failed 5 s shrink probe restores proven depth
    assert(policy.target_frames() == 3840);
    assert(policy.failed_shrinks() == 1);
    policy.on_output(100000000); // backoff; do not immediately probe again
    assert(policy.target_frames() == 3840);
    policy.on_output(114000000);
    assert(policy.target_frames() == 3360);
    policy.on_output(144000000);
    assert(policy.target_frames() == 2880);
    assert(policy.decreases() == 3);
    policy.on_underrun(175000000);
    assert(policy.target_frames() == 2880); // old failure doesn't count
    policy.on_underrun(176000000);
    assert(policy.target_frames() == 3360);
    assert(policy.underrun_events() == 13);
}
'''
        with tempfile.TemporaryDirectory() as folder:
            src = pathlib.Path(folder) / 'policy.cpp'
            binary = pathlib.Path(folder) / 'policy'
            src.write_text(source)
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'include'), str(src), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_startup_drought_reprime_and_clean_link_isolation(self):
        source = r'''
#include "audio_jitter_policy.hpp"
#include "audio_pcm_policy.hpp"
#include <cassert>
#include <utility>
using namespace moonlight;
struct Replay { unsigned misses, target; uint64_t concealed, reprime_silence; };
Replay replay(bool fast, unsigned packet, bool jitter) {
    AudioPrebufferPolicy buffer;
    AudioContinuityPolicy output;
    unsigned depth=0, next=0, misses=0;
    uint64_t first=0, last=0, received=0, consumed=0, reprime_silence=0;
    for (uint64_t frame=0; frame<48000*4; frame+=256) {
        const uint64_t now=1000000+frame*1000000/48000;
        // Repeated 35 ms delivery droughts followed by catch-up bursts.
        // Same sample clock and packet sequence for both policy versions.
        while (true) {
            uint64_t delivery=uint64_t(next)*packet;
            if (jitter && delivery>=4800 && delivery%4800<1680)
                delivery+=1680-delivery%4800;
            if (delivery>frame) break;
            depth+=packet; received+=packet; ++next; last=now;
        }
        auto d=output.decide(depth,256,buffer.target_frames(),packet);
        if (d.underrun) {
            ++misses;buffer.on_underrun(now);
            if (fast && buffer.on_startup_drought(now,first,last,packet,256)) {
                output.request_reprime();d.priming=true;
            }
        }
        if (first && !d.take) reprime_silence+=d.silence;
        if (d.take && !first) first=now;
        depth-=d.take;consumed+=d.take;
        assert(received==consumed+depth); // no dropped or duplicated PCM
        assert(buffer.target_frames()<=3840);
        buffer.on_output(now);
    }
    return {misses,buffer.target_frames(),output.concealed_frames(),reprime_silence};
}
int main() {
    for (unsigned packet : {240u,480u}) {
        auto clean=replay(true,packet,false);
        assert(clean.misses==0 && clean.target==480 && clean.reprime_silence==0);
        auto before=replay(false,packet,true), after=replay(true,packet,true);
        assert(after.misses < before.misses);
        assert(after.concealed < before.concealed);
        assert(after.concealed+after.reprime_silence <=
               before.concealed+before.reprime_silence);
        // Deliberate rebuffer silence is distinct from short-read concealment.
        // This four-second replay must not buy fewer underruns by repeatedly
        // stopping output: at most one 80 ms reserve plus one rounding grain.
        assert(after.reprime_silence<=3840+256);
    }
    AudioPrebufferPolicy p;
    assert(!p.on_startup_drought(1050000,1000000,1040000,240,256));
    assert(!p.on_startup_drought(1050000,0,1000000,240,256));
    assert(!p.on_startup_drought(1050000,1100000,1000000,240,256));
    assert(!p.on_startup_drought(7000000,1000000,6000000,240,256));
    assert(p.target_frames()==480); // established streams use slow hysteresis
    assert(p.on_startup_drought(1050000,1000000,1010000,240,256));
    assert(p.target_frames()==2880); // 40 ms gap + packet + grain, rounded
    assert(!p.on_startup_drought(1050000,1000000,1010000,240,256));
    assert(p.on_startup_drought(5000000,1000000,1000000,240,256));
    assert(p.target_frames()==3840); // bounded sender outage
    AudioContinuityPolicy c;
    assert(c.decide(960,256,480,240).take==256);
    c.request_reprime();c.request_reprime();
    assert(c.reprimes()==1);
    assert(c.decide(960,256,1440,240).take==0);
    assert(c.decide(1440,256,1440,240).take==256);
    p.on_output(50000000);
    assert(p.target_frames()==3360); // startup growth still releases normally
}
'''
        with tempfile.TemporaryDirectory() as folder:
            path = pathlib.Path(folder)
            (path / 'startup.cpp').write_text(source)
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'include'), str(path / 'startup.cpp'),
                            '-o', str(path / 'startup')], check=True)
            subprocess.run([str(path / 'startup')], check=True)

    def test_persistent_excess_survives_short_depth_dips(self):
        source = r'''
#include "audio_jitter_policy.hpp"
#include <cassert>
int main() {
    moonlight::AudioRingDriftPolicy drift;
    unsigned removed = 0;
    for (unsigned i=0; i<1000; ++i) {
        // A 49 ms standing queue with periodic healthy 20 ms troughs.
        // The old instantaneous gate reset every 21 ms and never trimmed.
        unsigned depth = i%4 == 0 ? 960 : 2600;
        unsigned trim = drift.observe(1000000 + uint64_t(i)*5333,depth,480);
        if (depth==960) assert(trim==0); // no trim at the low point
        removed += trim;
    }
    assert(removed >= 240 && removed <= 480);
    assert(drift.sheds() >= 1);
    // A quiet shallow LAN does not authorize a trim.
    moonlight::AudioRingDriftPolicy lan;
    for (unsigned i=0; i<1000; ++i)
        assert(lan.observe(1000000+uint64_t(i)*5333,480+i%240,480)==0);
}
'''
        with tempfile.TemporaryDirectory() as folder:
            path=pathlib.Path(folder)
            (path/'drift.cpp').write_text(source)
            subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',
                            '-I',str(ROOT/'include'),str(path/'drift.cpp'),
                            '-o',str(path/'drift')],check=True)
            subprocess.run([str(path/'drift')],check=True)

    def test_long_recovery_releases_wan_buffer_without_new_underruns(self):
        # Earlier 100-second PS5 replays cannot observe the full 45-second
        # quiet period plus several 30-second cautious shrink probes.
        source = r'''
#include "audio_jitter_policy.hpp"
#include <cassert>
#include <cstdint>
int main() {
    moonlight::AudioPrebufferPolicy policy;
    // Seven recurring WAN disruptions establish the bounded 80 ms protection.
    for (uint64_t t = 1000000; t <= 15000000; t += 2000000)
        policy.on_underrun(t);
    assert(policy.target_frames() == 3840);
    assert(policy.peak_frames() == 3840);
    // Simulate six minutes of healthy AudioOut grains at 48 kHz/512 samples.
    // No new underrun is fabricated by decoder or source-clock jitter.
    for (uint64_t t = 16000000; t <= 374000000; t += 10667)
        policy.on_output(t);
    assert(policy.target_frames() == 480);
    assert(policy.decreases() == 7);
    assert(policy.failed_shrinks() == 0);
    // One isolated scheduler miss after recovery must not grow the buffer.
    policy.on_underrun(380000000);
    assert(policy.target_frames() == 480);
    policy.on_underrun(381000000);
    assert(policy.target_frames() == 960);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as folder:
            path = pathlib.Path(folder)
            src = path / "long_audio.cpp"
            binary = path / "long_audio"
            src.write_text(source, encoding="utf-8")
            subprocess.run(["c++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT / "include"), str(src), "-o", str(binary)],
                           check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
