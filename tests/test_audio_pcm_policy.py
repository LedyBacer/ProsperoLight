# ps5-native-app-boilerplate - clock-continuous PCM output policy regression.
# Copyright (C) 2026 LedyBacer and contributors
# SPDX-License-Identifier: GPL-3.0-or-later
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class AudioPcmPolicyTests(unittest.TestCase):
    def test_output_grains_crossfade_and_drift(self):
        source = r'''
#include "audio_pcm_policy.hpp"
#include "audio_jitter_policy.hpp"
#include <cassert>
#include <cstdint>

int main() {
    using namespace moonlight;
    auto first = audio_grain_decision(0, 512, 960, 480, true);
    assert(first.take == 0 && first.silence == 512 && first.priming);
    auto partial = audio_grain_decision(300, 512, 960, 480, false);
    assert(partial.take == 300 && partial.silence == 212 && partial.underrun);
    auto recovered = audio_grain_decision(1920, 512, 1440, 480, true);
    assert(recovered.take == 512 && recovered.silence == 0 && !recovered.priming);
    assert(!recovered.near_miss);
    AudioContinuityPolicy continuity;
    assert(continuity.decide(0,512,960,240).priming);
    assert(!continuity.decide(1440,512,960,240).priming);
    // A single late packet used to force 20-80 ms of manufactured silence.
    auto brief = continuity.decide(480,512,960,240);
    assert(brief.underrun && brief.take == 480 && brief.silence == 32);
    assert(!brief.priming && continuity.reprimes() == 0);
    auto resumed = continuity.decide(720,512,960,240);
    assert(resumed.take == 512 && !resumed.priming); // no whole-target wait
    assert(continuity.concealed_frames() == 32);
    // A real drought stays bounded: keep AudioOut clock running, then re-prime.
    for (unsigned i = 0; i < 4; ++i)
        continuity.decide(0,512,960,240);
    assert(continuity.reprimes() == 1);
    assert(continuity.decide(720,512,960,240).take == 0);
    assert(continuity.decide(1440,512,960,240).take == 512);
    // Refill re-seeds depth history; no spurious hollow state after drought.
    assert(!continuity.decide(480,512,960,240).priming);
    // A large target which was never banked still authorizes recovery.
    AudioContinuityPolicy hollow;
    assert(!hollow.decide(960,512,960,240).priming);
    assert(hollow.decide(480,512,3840,240).priming);
    assert(hollow.reprimes() == 1);
    auto nearly = audio_grain_decision(550, 512, 960, 480, false);
    assert(nearly.near_miss && !nearly.underrun);

    int16_t pcm[16] = {30000, -30000, 30000, -30000, 30000, -30000,
                       30000, -30000, 30000, -30000, 30000, -30000,
                       30000, -30000, 30000, -30000};
    const int16_t last[2] = {-30000, 30000};
    audio_crossfade_from_sample(pcm, 8, 2, last, 4);
    assert(pcm[0] < 30000 && pcm[0] > -30000);
    assert(pcm[1] > -30000 && pcm[1] < 30000);
    assert(pcm[8] == 30000 && pcm[9] == -30000);
    audio_fade_to_silence(pcm, 8, 2, 4);
    assert(pcm[14] == 0 && pcm[15] == 0);
    int16_t silence[8] = {};
    audio_crossfade_from_sample(silence, 4, 2, last, 4);
    assert(silence[0] < 0 && silence[1] > 0); // seamless fade on full silence
    assert(silence[6] < 0 && silence[7] > 0);

    // A stale Opus packet is discarded while older audio remains buffered.
    // Rejoining must smooth the first *new* PCM samples at the ring tail,
    // not the next hardware grain drawn from old buffered samples.
    bool gap = true;
    int16_t next[16] = {30000,-30000,30000,-30000,30000,-30000,30000,-30000,
                        30000,-30000,30000,-30000,30000,-30000,30000,-30000};
    assert(audio_rejoin_after_skip(gap,next,8,2,last,4));
    assert(!gap);
    assert(next[0] < 30000 && next[0] > -30000);
    assert(next[1] > -30000 && next[1] < 30000);
    assert(next[8] == 30000 && next[9] == -30000);
    assert(!audio_rejoin_after_skip(gap,next,8,2,last,4));

    AudioPrebufferPolicy pre;
    pre.on_near_miss(1000000);
    pre.on_healthy_grain();
    pre.on_near_miss(1100000);
    pre.on_near_miss(1200000);
    assert(pre.target_frames() == 960); // not two contiguous near misses
    pre.on_near_miss(1300000);
    assert(pre.target_frames() == 1440);
    pre.on_near_miss(1400000);
    pre.on_near_miss(1500000);
    pre.on_near_miss(1600000);
    assert(pre.target_frames() == 1440); // cooldown suppresses ratchet
    assert(pre.near_miss_events() == 7);

    AudioRingDriftPolicy drift;
    for (uint64_t i = 0; i < 130; ++i)
        assert(drift.observe(1000000 + i * 10000, 6000, 960) == 0);
    uint64_t trimmed = 0;
    for (uint64_t i = 130; i < 340; ++i)
        trimmed += drift.observe(1000000 + i * 10000, 6000, 960);
    assert(trimmed == AudioRingDriftPolicy::kShedFrames);
    assert(drift.sheds() == 1);
    assert(drift.observe(4500000, 960, 960) == 0); // re-anchor after a long pause
}
'''
        with tempfile.TemporaryDirectory() as folder:
            source_file = pathlib.Path(folder) / 'audio_policy.cpp'
            executable = pathlib.Path(folder) / 'audio_policy'
            source_file.write_text(source)
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'include'), str(source_file), '-o', str(executable)],
                           check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == '__main__':
    unittest.main()
