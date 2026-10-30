# ps5-native-app-boilerplate / ProsperoLight - host policy verification
# Copyright (C) 2026 LedyBacer and contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the final patched C11 recovery helper, including epoch-safety fix."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / "third_party/moonlight-common-c/src/VideoDepacketizer.c"


class DeferredIdrRecoveryTest(unittest.TestCase):
    def test_throttle_defers_and_success_cancels_pending_request(self):
        source_text = SOURCE.read_text(encoding="utf-8")
        helper = source_text.split("#define IDR_REQUEST_COOLDOWN_US", 1)[1].split(
            "// Uncomment to test 3 byte", 1
        )[0]
        helper = "#define IDR_REQUEST_COOLDOWN_US" + helper
        timer = source_text.split("void PlVideoRecoveryPoll(void) {", 1)[1].split(
            "static unsigned int lastPacketInStream;", 1
        )[0]
        timer = "void PlVideoRecoveryPoll(void) {" + timer
        self.assertIn("void PlVideoRecoveryPoll(void)", timer)
        self.assertIn("deferredIdrEpoch", helper)
        self.assertIn("_Atomic bool waitingForIdrFrame", source_text)
        source = r"""
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
static uint64_t test_clock;
static int idr_requests;
static uint64_t PltGetMicroseconds(void) { return test_clock; }
static void LiRequestIdrFrame(void) { ++idr_requests; }
static _Atomic bool waitingForIdrFrame = true;
static bool waitingForNextSuccessfulFrame = false;
""" + helper + "\n" + timer + r"""
int main(void) {
    test_clock = 1000000;
    requestIdrCoalesced();
    assert(idr_requests == 1);
    test_clock += 20000;
    requestIdrCoalesced();
    assert(idr_requests == 1);
    uint64_t stale = atomic_load(&deferredIdrEpoch);
    assert(stale != 0);
    requestIdrCoalesced();
    assert(atomic_load(&deferredIdrEpoch) > stale);
    // A stale recovery observer may only cancel the epoch it inspected.
    assert(!atomic_compare_exchange_strong(&deferredIdrEpoch, &stale, 0));
    assert(atomic_load(&deferredIdrEpoch) != 0);
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
    requestIdrCoalesced();
    PlVideoRecoveryPoll();
    assert(atomic_load(&deferredIdrEpoch) == 0);
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
