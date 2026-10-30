# ps5-native-app-boilerplate - native overload and WAN burst integration checks.
# Copyright (C) 2026 LedyBacer and contributors
# SPDX-License-Identifier: GPL-3.0-or-later
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class VideoOverloadPolicyTests(unittest.TestCase):
    def test_fresh_bursts_do_not_trigger_native_resync(self):
        source = r'''
#include "video_overload.hpp"
#include <cassert>
int main() {
    using namespace moonlight::video;
    OverloadGuard g;
    assert(g.observe(100000, 0, true, 14, 80000, true) == OverloadAction::Continue);
    assert(g.observe(101000, 5000, true, 13, 80000, true) == OverloadAction::Continue);
    assert(g.attempts == 0);
    assert(g.observe(125000, 24000, true, 13, 80000, true) == OverloadAction::Resync);
    assert(g.observe(126000, 0, true, 14, 80000, false) == OverloadAction::Continue);
    assert(g.observe(150000, 85000, true, 4, 80000, true) == OverloadAction::Resync);
    assert(g.observe(175000, 90000, true, 4, 80000, true) == OverloadAction::Stop);
    assert(g.resyncs == 2 && g.stops == 1);
    assert(g.observe(200000, 100000, true, 14, 0, true) == OverloadAction::Continue);
}
'''
        with tempfile.TemporaryDirectory() as folder:
            src = pathlib.Path(folder) / 'overload.cpp'
            binary = pathlib.Path(folder) / 'overload'
            src.write_text(source)
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'include'), str(src), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
