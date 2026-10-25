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
    assert(policy.target_frames() == 960);
    policy.on_underrun(1000000);
    assert(policy.target_frames() == 960);  // isolated failure
    policy.on_underrun(1500000);
    assert(policy.target_frames() == 1440);
    for (unsigned i = 0; i < 8; ++i)
        policy.on_underrun(2000000 + i * 500000);
    assert(policy.target_frames() == 3840); // bounded at 80 ms
    assert(policy.peak_frames() == 3840 && policy.increases() == 6);
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

    def test_long_recovery_releases_wan_buffer_without_new_underruns(self):
        # Earlier 100-second PS5 replays cannot observe the full 45-second
        # quiet period plus several 30-second cautious shrink probes.
        source = r'''
#include "audio_jitter_policy.hpp"
#include <cassert>
#include <cstdint>
int main() {
    moonlight::AudioPrebufferPolicy policy;
    // Six recurring WAN disruptions establish the bounded 80 ms protection.
    for (uint64_t t = 1000000; t <= 13000000; t += 2000000)
        policy.on_underrun(t);
    assert(policy.target_frames() == 3840);
    assert(policy.peak_frames() == 3840);
    // Simulate six minutes of healthy AudioOut grains at 48 kHz/512 samples.
    // No new underrun is fabricated by decoder or source-clock jitter.
    for (uint64_t t = 14000000; t <= 374000000; t += 10667)
        policy.on_output(t);
    assert(policy.target_frames() == 960);
    assert(policy.decreases() == 6);
    assert(policy.failed_shrinks() == 0);
    // One isolated scheduler miss after recovery must not grow the buffer.
    policy.on_underrun(380000000);
    assert(policy.target_frames() == 960);
    policy.on_underrun(381000000);
    assert(policy.target_frames() == 1440);
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
