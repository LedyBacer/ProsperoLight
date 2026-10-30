# ps5-native-app-boilerplate / ProsperoLight - host-native presentation replay.
# Copyright (C) 2026 LedyBacer and contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile and run the actual C++ pacer and two-slot mailbox together."""

import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class PresentationMailboxReplay(unittest.TestCase):
    def test_mailbox_catchup_and_source_rate(self):
        with tempfile.TemporaryDirectory() as temp:
            executable = pathlib.Path(temp) / "presentation"
            subprocess.run([
                "c++", "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT / "include"),
                str(ROOT / "tests/test_presentation_mailbox_replay.cpp"),
                "-o", str(executable),
            ], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
