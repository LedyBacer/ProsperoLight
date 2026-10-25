# ps5-native-app-boilerplate / ProsperoLight - host policy verification
# Copyright (C) 2026 LedyBacer and contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile and run the actual deferred-IDR helper extracted from patch 0009."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
PATCH = ROOT / "tools/pyrowave/patches/moonlight/0009-deferred-idr-recovery.patch"


def additions(text, start, end):
    part = text.split(start, 1)[1].split(end, 1)[0]
    return "\n".join(line[1:] for line in part.splitlines() if line.startswith("+"))


class DeferredIdrRecoveryTest(unittest.TestCase):
    def test_throttle_defers_and_success_cancels_pending_request(self):
        patch = PATCH.read_text()
        helper = additions(patch, "@@ -38,", "@@ -90,")
        timer = additions(patch, "@@ -90,", "@@ -119,")
        self.assertIn("void PlVideoRecoveryPoll(void)", timer)
        source = r"""
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
static uint64_t test_clock;
static int idr_requests;
static uint64_t PltGetMicroseconds(void) { return test_clock; }
static void LiRequestIdrFrame(void) { ++idr_requests; }
static bool waitingForIdrFrame = true;
static bool waitingForNextSuccessfulFrame = false;
""" + helper + "\n" + timer + r"""
int main(void) {
    test_clock = 1000000;
    requestIdrCoalesced();
    assert(idr_requests == 1);
    test_clock += 20000;
    requestIdrCoalesced();
    assert(idr_requests == 1);
    test_clock = 1099999;
    PlVideoRecoveryPoll();
    assert(idr_requests == 1);
    test_clock = 1100000;
    PlVideoRecoveryPoll();
    assert(idr_requests == 2);
    PlVideoRecoveryPoll();
    assert(idr_requests == 2);
    test_clock = 1110000;
    requestIdrCoalesced();
    assert(idr_requests == 2);
    waitingForIdrFrame = false;
    PlVideoRecoveryPoll();
    test_clock = 1300000;
    PlVideoRecoveryPoll();
    assert(idr_requests == 2); // Already recovered: no delayed IDR.
    waitingForIdrFrame = true;
    requestIdrCoalesced();
    assert(idr_requests == 3);
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as temp:
            file = pathlib.Path(temp) / "idr.c"
            binary = pathlib.Path(temp) / "idr"
            file.write_text(source)
            subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra",
                            "-Werror", str(file), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
