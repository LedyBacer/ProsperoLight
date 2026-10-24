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
    policy.on_underrun(2000000);
    policy.on_underrun(2500000);
    policy.on_underrun(3000000);
    assert(policy.target_frames() == 2400); // no unbounded latency
    policy.on_underrun(3500000);
    assert(policy.target_frames() == 2400);
    assert(policy.peak_frames() == 2400 && policy.increases() == 3);
    policy.on_output(46000000);
    assert(policy.target_frames() == 2400); // 45 s quiet not reached
    policy.on_output(48500000);
    assert(policy.target_frames() == 1920);
    policy.on_output(49000000);
    assert(policy.target_frames() == 1920);
    policy.on_output(78500000);
    assert(policy.target_frames() == 1440);
    policy.on_output(108500000);
    assert(policy.target_frames() == 960);
    assert(policy.decreases() == 3);
    policy.on_underrun(120000000);
    assert(policy.target_frames() == 960); // old failure doesn't count
    policy.on_underrun(121000000);
    assert(policy.target_frames() == 1440);
    assert(policy.underrun_events() == 8);
}
'''
        with tempfile.TemporaryDirectory() as folder:
            src = pathlib.Path(folder) / 'policy.cpp'
            binary = pathlib.Path(folder) / 'policy'
            src.write_text(source)
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'include'), str(src), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
