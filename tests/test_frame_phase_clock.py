# ps5-native-app-boilerplate - experimental clock-phase replay tests.
# Copyright (C) 2026 LedyBacer and contributors
# SPDX-License-Identifier: GPL-3.0-or-later
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class PhaseClockTests(unittest.TestCase):
    def test_source_clock_drift_and_bursts(self):
        source = r'''
#include "frame_phase_clock.hpp"
#include <cassert>
#include <cstdint>
int main() {
    moonlight::CadencePhaseClock clock;
    const uint64_t source_origin = 1000000, recv_origin = 900000000;
    assert(clock.observe(1, source_origin, recv_origin, 16666, 0) == 0);
    for (uint64_t i = 2; i <= 200; ++i) {
        const auto pts = source_origin + i * 16666;
        const auto recv = recv_origin + i * 16666 + i * 2; // 120 ppm skew
        assert(clock.observe(static_cast<uint32_t>(i), pts, recv, 16666, 0) == 0);
    }
    assert(clock.stats.samples == 199);
    assert(clock.stats.reanchors == 0);
    assert(clock.stats.late == 0 || clock.stats.late <= 199);
    auto source = source_origin + 201 * 16666;
    auto receive = recv_origin + 201 * 16666 + 201 * 2 + 10000;
    assert(clock.observe(201, source, receive, 16666, 30000) <= 16666);
    const uint32_t hold = clock.observe(202, source_origin + 202 * 16666,
        recv_origin + 202 * 16666 + 202 * 2, 16666, 30000);
    assert(hold > 0 && hold <= 16666); // observed late tail protects next frames
    assert(clock.stats.tail_us >= 9000);
    assert(clock.observe(203, source_origin + 900000, recv_origin + 900000,
                         16666, 30000) == 0);
    assert(clock.stats.reanchors == 1);
    assert(clock.observe(204, source_origin + 100, recv_origin + 990000,
                         16666, 30000) == 0);
    assert(clock.stats.reanchors == 2);
}
'''
        with tempfile.TemporaryDirectory() as folder:
            path = pathlib.Path(folder) / 'phase.cpp'
            exe = pathlib.Path(folder) / 'phase'
            path.write_text(source)
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'include'), str(path), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
