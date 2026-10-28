# ps5-native-app-boilerplate / ProsperoLight - Adaptive trigger build contract.
# Copyright (C) 2026 Brian Valente
# SPDX-License-Identifier: GPL-3.0-or-later
import pathlib
import re
import hashlib
import os
import shlex
import struct
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class TriggerSdkTests(unittest.TestCase):
    def test_application_enables_multizone_trigger_modes(self):
        script = (ROOT / "tools/build.sh").read_text()
        match = re.search(r"^module_sdk=(0x[0-9a-fA-F]+)$", script, re.MULTILINE)
        if match is None:
            self.fail("Application SDK must be explicit in tools/build.sh")
        self.assertGreaterEqual(int(match.group(1), 16), 0x04000000)
        self.assertIn('--module-sdk "$module_sdk"', script)

    def test_generated_runtime_matches_application_sdk(self):
        script = (ROOT / "tools/build.sh").read_text()
        app_match = re.search(r"^module_sdk=(0x[0-9a-fA-F]+)$", script, re.MULTILINE)
        if app_match is None:
            self.fail("Application SDK must be explicit")
        with tempfile.TemporaryDirectory(prefix="trigger-sdk-") as temporary:
            directory = pathlib.Path(temporary)
            builder = directory / "libc-builder"
            compiler = shlex.split(os.environ.get("HOST_CXX", "c++"))
            subprocess.run(compiler + ["-std=c++20", "-O1", str(ROOT / "tooling/native/libc_builder.cpp"), "-o", str(builder)], check=True, capture_output=True)
            output = directory / "libc.elf"
            subprocess.run([str(builder), str(ROOT / "tooling/native/runtime/api-surface.txt"), str(ROOT / "tooling/native/runtime/imports.txt"), str(output)], check=True, capture_output=True)
            raw = output.read_bytes()
        phoff = struct.unpack_from("<Q", raw, 32)[0]
        stride, count = struct.unpack_from("<HH", raw, 54)
        headers = [struct.unpack_from("<IIQQQQQQ", raw, phoff + i * stride) for i in range(count)]
        module = next(p for p in headers if p[0] == 0x61000002)
        self.assertEqual(struct.unpack_from("<I", raw, module[2] + 0x14)[0], int(app_match.group(1), 16))
        expected = re.search(r'^expected_raw="([0-9a-f]{64})"$', (ROOT / "tools/rebuild-libc.sh").read_text(), re.MULTILINE)
        if expected is None:
            self.fail("Runtime reproduction must pin its raw digest")
        self.assertEqual(hashlib.sha256(raw).hexdigest(), expected.group(1))


if __name__ == "__main__":
    unittest.main()
