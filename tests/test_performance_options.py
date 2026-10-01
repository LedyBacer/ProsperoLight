# ps5-native-app-boilerplate / ProsperoLight - Reject unsafe development options.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import json
import os
import re
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PerformanceOptions(unittest.TestCase):
    def test_beta_release_is_opt_in_and_not_latest(self):
        workflow = (ROOT / ".github/workflows/tooling.yml").read_text()
        self.assertIn('release_flags=(--prerelease --latest=false)', workflow)
        # Both steps select the same beta versions, including the current one.
        version = json.loads((ROOT / "sce_sys/param.json").read_text())["contentVersion"]
        betas = re.findall(r"^ +(01\.000\.062(?:\|[0-9.]+)+)\)$", workflow, re.MULTILINE)
        self.assertEqual(len(betas), 2)
        self.assertEqual(betas[0], betas[1])
        self.assertIn(version, betas[0].split("|"))
        self.assertEqual(workflow.count('"${release_flags[@]}"'), 2)
        self.assertIn('make ffpfsc FEC_SIMD=1 OPUS_SIMD=1 PERFORMANCE_DETAIL=1 FLIP_POLL_US=200 INPUT_POLL_US=2000 VIDEO_SLICES_PER_FRAME=8', workflow)

    def test_compile_time_bounds(self):
        cases = {
            "VIDEO_SLICES_PER_FRAME": (8, 9),
            "DECODER_PIPELINE_DEPTH": (3, 4),
            "DECODER_CPU_PRIORITY": (720, 256),
            "INPUT_POLL_US": (1000, 0),
        }
        for name, values in cases.items():
            for index, value in enumerate(values):
                with self.subTest(option=name, value=value):
                    result = subprocess.run(
                        [os.environ.get("HOST_CXX", "clang++"), "-std=c++20",
                         "-fsyntax-only", "-x", "c++", f"-D{name}={value}",
                         "-Iinclude", "-"], cwd=ROOT,
                        input='#include "moonlight_tuning.hpp"\n', text=True,
                        capture_output=True,
                    )
                    self.assertEqual(result.returncode == 0, index == 0, result.stderr)
                    if index:
                        self.assertIn("static assertion", result.stderr)


if __name__ == "__main__":
    unittest.main()
