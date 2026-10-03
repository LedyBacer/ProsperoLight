# ps5-native-app-boilerplate - Host policy regression tests.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

class CadencePolicy(unittest.TestCase):
    def test_policy(self):
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / "policy.cpp"
            source.write_text('#include <cassert>\n#include "frame_cadence.hpp"\nint main(){moonlight::FrameCadence c;c.reset(100,1000000,60);uint64_t d=100;for(int i=0;i<60;i++)d=c.next(d);assert(d==1000100);assert(c.next(5000000)>5000000);c.reset(0,1000000,120);d=0;for(int i=0;i<120;i++)d=c.next(d);assert(d==1000000);assert(moonlight::fixed_cadence_rate(60,5994)==5994);assert(moonlight::fixed_cadence_rate(60,11988)==5994);assert(moonlight::fixed_cadence_rate(120,11988)==11988);assert(moonlight::fixed_cadence_rate(90,11988)==9000);assert(moonlight::fixed_cadence_rate(60,0)==6000);}\n')
            binary = pathlib.Path(directory) / "policy"
            subprocess.run(['c++', "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "include"), str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
