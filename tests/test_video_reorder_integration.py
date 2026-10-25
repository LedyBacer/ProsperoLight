# ps5-native-app-boilerplate / ProsperoLight - exact downstream RTP staging replay.
# Copyright (C) 2026 LedyBacer and contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the applied VideoStream.c reorder functions with a controllable FEC queue.

The functions come from the *actual patched dependency*, not a parallel Python
model. The synthetic RtpvAddPacket endpoint deliberately does not claim to
simulate Reed-Solomon decoding; actual codec/FEC wire behavior remains hardware
gated. This checks bounded ownership, ordering, expiry and stale releases.
"""

import os
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / "third_party/moonlight-common-c/src/VideoStream.c"


class VideoReorderIntegration(unittest.TestCase):
    def test_applied_reorder_window_with_injected_udp_and_fec_events(self):
        source = SOURCE.read_text(encoding="utf-8")
        start = source.index("#define VIDEO_REORDER_WAIT_US")
        end = source.index("// PyroWave frames", start)
        applied = source[start:end]
        self.assertIn("reorderTail->next = entry;", applied)
        self.assertIn("VIDEO_REORDER_MAX_PACKETS 2048u", applied)
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
typedef struct {
    uint32_t frame;
    uint8_t block;
    uint8_t marker;
} FakePacket;
typedef FakePacket* PRTP_PACKET;
typedef void* PRTPV_QUEUE_ENTRY;
typedef struct {
    uint32_t currentFrameNumber;
    uint8_t multiFecCurrentBlockNumber;
    uint64_t bufferFirstRecvTimeUs;
    struct { int count; } pendingFecBlockList, completedFecBlockList;
    struct { uint32_t packetCountFecRecovered, packetCountFecFailed; } stats;
} FakeQueue;
typedef struct {
    uint64_t late_packets_accepted, late_packets_rejected;
    uint64_t reorder_wait_total_us, reorder_wait_max_us, reorder_timeouts;
    uint64_t fec_recovered_after_reorder, rescued_frames, held_packets, capacity_flushes;
    uint64_t late_data_rejected, late_parity_rejected;
} LI_VIDEO_REORDER_STATS;
static FakeQueue rtpQueue;
static uint64_t testNow;
static uint32_t dispatchFrames[4096];
static uint8_t dispatchBlocks[4096];
static int dispatchCount;
static uint64_t PltGetMicroseconds(void) { return testNow; }
static bool isBefore32(uint32_t a, uint32_t b) { return (int32_t)(a - b) < 0; }
#define RTPF_RET_QUEUED 1
static int RtpvAddPacket(FakeQueue* q, PRTP_PACKET p, int length, PRTPV_QUEUE_ENTRY entry) {
    (void)length; (void)entry;
    assert(dispatchCount < 4096);
    dispatchFrames[dispatchCount] = p->frame;
    dispatchBlocks[dispatchCount++] = p->block;
    if (p->marker == 1) q->stats.packetCountFecRecovered++;
    if (p->marker == 2) q->stats.packetCountFecFailed++;
    return 0; /* rejected ownership: reorderDispatch must free it */
}
''' + applied + r'''
static char* packet(uint32_t frame, uint8_t block, uint8_t marker) {
    FakePacket* p = calloc(1, sizeof(FakePacket) + 64);
    assert(p);
    p->frame = frame; p->block = block; p->marker = marker;
    return (char*)p;
}
static void reset(uint32_t frame) {
    reorderCleanup();
    rtpQueue = (FakeQueue){0};
    rtpQueue.currentFrameNumber = frame;
    rtpQueue.pendingFecBlockList.count = 1;
    rtpQueue.bufferFirstRecvTimeUs = 1000000;
    testNow = 1000000;
    dispatchCount = 0;
    reorderEntryOffset = sizeof(FakePacket);
    reorderRescueFrame = 0;
    reorderLastTimeoutFrame = UINT32_MAX;
    reorderLastTimeoutBlock = UINT8_MAX;
    for (unsigned i = 0; i < RV_COUNT; i++)
        atomic_store_explicit(&reorderCounters[i], 0, memory_order_relaxed);
}
int main(void) {
    reset(10);
    assert(reorderHold(packet(12, 0, 0), sizeof(FakePacket), 12, 0, false));
    assert(reorderHold(packet(11, 1, 0), sizeof(FakePacket), 11, 1, true));
    assert(reorderHold(packet(11, 0, 1), sizeof(FakePacket), 11, 0, false));
    assert(reorderHold(packet(13, 0, 0), sizeof(FakePacket), 13, 0, false));
    assert(reorderCount == 4 && reorderHead->frame == 11 && reorderTail->frame == 13);
    testNow += VIDEO_REORDER_WAIT_US - 1;
    reorderRelease(false);
    assert(dispatchCount == 0 && reorderCount == 4);
    testNow++;
    reorderRelease(false);
    assert(reorderCount == 0 && reorderTail == NULL && dispatchCount == 4);
    assert(dispatchFrames[0] == 11 && dispatchBlocks[0] == 0);
    assert(dispatchFrames[1] == 11 && dispatchBlocks[1] == 1);
    assert(dispatchFrames[2] == 12 && dispatchFrames[3] == 13);
    LI_VIDEO_REORDER_STATS stats;
    assert(LiGetVideoReorderStats(&stats));
    assert(stats.reorder_wait_max_us == VIDEO_REORDER_WAIT_US);
    assert(stats.reorder_timeouts == 1 && stats.fec_recovered_after_reorder == 1);

    reset(UINT32_MAX - 1);
    assert(reorderHold(packet(1, 0, 0), sizeof(FakePacket), 1, 0, false));
    assert(reorderHold(packet(UINT32_MAX, 0, 0), sizeof(FakePacket), UINT32_MAX, 0, false));
    assert(reorderHold(packet(0, 0, 0), sizeof(FakePacket), 0, 0, false));
    rtpQueue.pendingFecBlockList.count = 0;
    reorderRelease(false);
    assert(dispatchCount == 3);
    assert(dispatchFrames[0] == UINT32_MAX && dispatchFrames[1] == 0 && dispatchFrames[2] == 1);

    reset(10);
    for (unsigned i = 0; i < VIDEO_REORDER_MAX_PACKETS; i++)
        assert(reorderHold(packet(11 + i / 32, i % 4, 0), sizeof(FakePacket),
                           11 + i / 32, i % 4, false));
    assert(reorderCount == VIDEO_REORDER_MAX_PACKETS);
    char* extra = packet(100, 0, 0);
    assert(!reorderHold(extra, sizeof(FakePacket), 100, 0, false));
    free(extra); /* The receive fast path retains ownership if hold is refused. */
    reorderCleanup();
    assert(reorderHead == NULL && reorderTail == NULL && reorderCount == 0);

    reset(10);
    assert(reorderHold(packet(11, 0, 0), sizeof(FakePacket), 11, 0, true));
    assert(reorderHold(packet(11, 1, 0), sizeof(FakePacket), 11, 1, false));
    rtpQueue.currentFrameNumber = 12;
    reorderRelease(false);
    assert(dispatchCount == 0 && reorderCount == 0);
    assert(LiGetVideoReorderStats(&stats) && stats.late_packets_rejected == 2);
    assert(stats.late_data_rejected == 1 && stats.late_parity_rejected == 1);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            path = pathlib.Path(temp)
            source_path = path / "reorder.c"
            binary = path / "reorder"
            source_path.write_text(code, encoding="utf-8")
            subprocess.run([
                "cc", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                str(source_path), "-o", str(binary),
            ], check=True)
            # LeakSanitizer cannot inspect /proc under some CI ptrace sandboxes;
            # AddressSanitizer and UBSan remain active for ownership checks.
            env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0")
            subprocess.run([str(binary)], check=True, env=env)


if __name__ == "__main__":
    unittest.main()
