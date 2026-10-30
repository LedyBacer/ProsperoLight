# ps5-native-app-boilerplate / ProsperoLight - real Common-C FEC/reorder host replay.
# Copyright (C) 2026 LedyBacer and contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the applied reorder helpers with the actual RtpVideoQueue and nanors.

Unlike the bounded-ownership fake-queue test, this test exercises the real
Reed-Solomon reconstruction, queue admission, frame advancement and ownership.
Synthetic parity shims supply valid packet headers while preserving the encoded
FEC payload; this is a depacketizer test, not a byte-for-byte host wire fixture.
"""

import os
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMON = ROOT / 'third_party/moonlight-common-c'


class VideoFecQueueReplay(unittest.TestCase):
    def test_actual_reassembly_after_future_packet_and_late_shard(self):
        source = (COMMON / 'src/VideoStream.c').read_text(encoding='utf-8')
        helpers = source[source.index('#define VIDEO_REORDER_WAIT_US'):
                         source.index('// PyroWave frames')]
        self.assertIn('reorderStagingEligible(', helpers)
        self.assertIn('reorderRelease(bool force)', helpers)

        harness = r'''
#include "Limelight-internal.h"
#include "rs.h"
#include <stdatomic.h>
#undef NDEBUG
#include <assert.h>

#define DATA 4
#define PARITY 2
#define PERCENT 50
#define PAYLOAD 128
#define RECEIVE (PAYLOAD + MAX_RTP_HEADER_SIZE)

STREAM_CONFIGURATION StreamConfig = {.packetSize = PAYLOAD};
CONNECTION_LISTENER_CALLBACKS ListenerCallbacks = {0};
int AppVersionQuad[4] = {7, 1, 500, 0};
int NegotiatedVideoFormat = VIDEO_FORMAT_H264;
static uint64_t testNow = 1000000;
static unsigned displayedCount, recoveredCount, lostCount, observed[64];
static RTP_VIDEO_QUEUE rtpQueue;

uint64_t PltGetMicroseconds(void) { return testNow; }
bool isReferenceFrameInvalidationEnabled(void) { return false; }
void connectionSawFrame(uint32_t frame) { (void)frame; }
void connectionSendFrameFecStatus(PSS_FRAME_FEC_STATUS status) { (void)status; }
void notifyFrameLost(unsigned int frame, bool speculative) {
    (void)frame; (void)speculative; ++lostCount;
}
void queueRtpPacket(PRTPV_QUEUE_ENTRY entry) {
    assert(displayedCount < 64);
    observed[displayedCount++] = entry->packet->sequenceNumber;
    if (entry->packet->sequenceNumber == 1001) ++recoveredCount;
    free(entry->packet);
}
''' + helpers + r'''

static char* makePacket(unsigned frame, unsigned index, unsigned base,
                        uint8_t blocks) {
    char* memory = calloc(1, RECEIVE + sizeof(RTPV_QUEUE_ENTRY));
    assert(memory);
    PRTP_PACKET rtp = (PRTP_PACKET)memory;
    rtp->header = FLAG_EXTENSION;
    rtp->sequenceNumber = (uint16_t)(base + index);
    rtp->timestamp = 90000 * frame;
    PNV_VIDEO_PACKET nv = (PNV_VIDEO_PACKET)(memory + MAX_RTP_HEADER_SIZE);
    nv->frameIndex = frame;
    nv->streamPacketIndex = index;
    nv->fecInfo = (DATA << 22) | (index << 12) | (PERCENT << 4);
    nv->multiFecBlocks = blocks;
    nv->flags = FLAG_CONTAINS_PIC_DATA |
                (index == 0 ? FLAG_SOF : 0) |
                (index == DATA - 1 ? FLAG_EOF : 0);
    for (int i = MAX_RTP_HEADER_SIZE + (int)sizeof(*nv); i < RECEIVE; ++i)
        memory[i] = (char)(frame * 17 + index * 31 + i);
    return memory;
}

static void makeFecFrame(char* shards[DATA + PARITY], unsigned frame,
                         unsigned base, uint8_t blocks) {
    for (unsigned i = 0; i < DATA + PARITY; ++i)
        shards[i] = makePacket(frame, i, base, blocks);
    reed_solomon* rs = reed_solomon_new(DATA, PARITY);
    assert(rs);
    assert(reed_solomon_encode(rs, (uint8_t**)shards, DATA + PARITY, RECEIVE) == 0);
    reed_solomon_release(rs);
    for (unsigned i = DATA; i < DATA + PARITY; ++i) {
        // The sender carries these headers separately from the FEC symbols.
        // Only the parity header bytes are overwritten; parity payload bytes
        // still exercise the genuine RS decoder in RtpVideoQueue.c.
        PRTP_PACKET rtp = (PRTP_PACKET)shards[i];
        rtp->header = FLAG_EXTENSION;
        rtp->sequenceNumber = (uint16_t)(base + i);
        rtp->timestamp = 90000 * frame;
        PNV_VIDEO_PACKET nv = (PNV_VIDEO_PACKET)(shards[i] + MAX_RTP_HEADER_SIZE);
        nv->frameIndex = frame;
        nv->fecInfo = (DATA << 22) | (i << 12) | (PERCENT << 4);
        nv->multiFecBlocks = blocks;
    }
}

static void feed_packet(char* memory) {
    PRTP_PACKET rtp = (PRTP_PACKET)memory;
    PNV_VIDEO_PACKET nv = (PNV_VIDEO_PACKET)(memory + MAX_RTP_HEADER_SIZE);
    const uint32_t frame = nv->frameIndex;
    const uint8_t block = rtpQueue.multiFecCapable ?
        (uint8_t)((nv->multiFecBlocks >> 4) & 3) : 0;
    if (reorderShouldHold(frame, block) &&
        reorderStagingEligible(rtp->sequenceNumber,
                               rtpQueue.nextContiguousSequenceNumber,
                               (rtp->header & FLAG_EXTENSION) != 0,
                               nv->fecInfo, nv->multiFecBlocks,
                               rtpQueue.multiFecCapable)) {
        assert(reorderHold(memory, RECEIVE, frame, block, false));
        reorderRelease(false);
        return;
    }
    const int status = RtpvAddPacket(&rtpQueue, rtp, RECEIVE,
                                    (PRTPV_QUEUE_ENTRY)(memory + RECEIVE));
    if (status != RTPF_RET_QUEUED) free(memory);
    reorderRelease(false);
}

int main(void) {
    RtpvInitializeQueue(&rtpQueue);
    reorderEntryOffset = RECEIVE;
    reorderLastTimeoutFrame = UINT32_MAX;
    reorderLastTimeoutBlock = UINT8_MAX;
    char* frameOne[DATA + PARITY];
    char* frameTwo[DATA + PARITY];
    makeFecFrame(frameOne, 1, 1000, 0);
    makeFecFrame(frameTwo, 2, 1006, 0);

    feed_packet(frameOne[0]);
    feed_packet(frameOne[2]);
    feed_packet(frameTwo[0]); // stage future, active frame can still be rescued
    assert(reorderCount == 1 && rtpQueue.currentFrameNumber == 1);
    feed_packet(frameOne[4]); // parity arrives before the last data shard
    assert(rtpQueue.stats.packetCountFecRecovered == 0);
    feed_packet(frameOne[3]); // RS reconstructs missing data index 1
    assert(rtpQueue.stats.packetCountFecRecovered == 1);
    assert(rtpQueue.currentFrameNumber == 2 && reorderCount == 0);
    assert(recoveredCount == 1 && displayedCount == DATA);
    for (unsigned i = 1; i < DATA; ++i) feed_packet(frameTwo[i]);
    assert(displayedCount == 2 * DATA);
    assert(lostCount == 0);
    const unsigned before = displayedCount;
    feed_packet(frameOne[1]); // stale data cannot revive a completed frame
    assert(displayedCount == before);

    // A later FEC block must wait for the current block to be recovered;
    // completing the first block releases the next block in exact order.
    char* multiFirst[DATA + PARITY];
    char* multiLast[DATA + PARITY];
    makeFecFrame(multiFirst, 3, 1012, 0x40); // block 0 of 2
    makeFecFrame(multiLast, 3, 1018, 0x50);  // block 1 of 2
    feed_packet(multiFirst[0]);
    feed_packet(multiFirst[2]);
    feed_packet(multiLast[0]);
    assert(reorderCount == 1 && rtpQueue.multiFecCurrentBlockNumber == 0);
    feed_packet(multiFirst[4]);
    feed_packet(multiFirst[3]);
    assert(rtpQueue.stats.packetCountFecRecovered == 2);
    assert(reorderCount == 0 && rtpQueue.multiFecCurrentBlockNumber == 1);
    for (unsigned i = 1; i < DATA; ++i) feed_packet(multiLast[i]);
    assert(rtpQueue.currentFrameNumber == 4);
    assert(displayedCount == 4 * DATA);
    assert(lostCount == 0);

    // An unrecoverable block eventually releases its future frame, rather
    // than holding the entire stream indefinitely or reporting a fake rescue.
    char* failed[DATA + PARITY];
    char* successor[DATA + PARITY];
    makeFecFrame(failed, 4, 1024, 0);
    makeFecFrame(successor, 5, 1030, 0);
    feed_packet(failed[0]);
    feed_packet(failed[2]);
    feed_packet(successor[0]);
    assert(reorderCount == 1);
    testNow += VIDEO_REORDER_WAIT_US + 1;
    reorderRelease(false);
    assert(reorderCount == 0 && rtpQueue.currentFrameNumber == 5);
    assert(rtpQueue.stats.packetCountFecFailed >= 1);
    for (unsigned i = 1; i < DATA; ++i) feed_packet(successor[i]);
    assert(rtpQueue.currentFrameNumber == 6);
    assert(displayedCount == 5 * DATA);

    // A complete future frame arrives before ANY packet of the expected one.
    // Old staging dispatched it immediately because the active queue was empty.
    char* missingStart[DATA + PARITY];
    char* earlyFuture[DATA + PARITY];
    makeFecFrame(missingStart, 6, 1036, 0);
    makeFecFrame(earlyFuture, 7, 1042, 0);
    for (unsigned i = 0; i < DATA; ++i) feed_packet(earlyFuture[i]);
    assert(rtpQueue.currentFrameNumber == 6 && reorderCount == DATA);
    testNow += 19000;
    for (unsigned i = 0; i < DATA; ++i) feed_packet(missingStart[i]);
    assert(rtpQueue.currentFrameNumber == 8 && reorderCount == 0);
    assert(displayedCount == 7 * DATA);
    for (unsigned i = DATA; i < DATA + PARITY; ++i) {
        free(missingStart[i]); free(earlyFuture[i]);
    }

    // Entirely lost frame: holding cannot reset its own deadline on arrivals.
    char* holeSuccessor[DATA + PARITY];
    makeFecFrame(holeSuccessor, 9, 1054, 0);
    for (unsigned i = 0; i < DATA; ++i) {
        testNow += 1000;
        feed_packet(holeSuccessor[i]);
    }
    assert(rtpQueue.currentFrameNumber == 8 && reorderCount == DATA);
    testNow = reorderDeadlineUs() - 1;
    reorderRelease(false);
    assert(rtpQueue.currentFrameNumber == 8);
    ++testNow;
    reorderRelease(false);
    assert(rtpQueue.currentFrameNumber == 10 && reorderCount == 0);
    assert(displayedCount == 8 * DATA);
    free(holeSuccessor[4]); free(holeSuccessor[5]);

    free(frameOne[5]);
    free(frameTwo[4]);
    free(frameTwo[5]);
    free(multiFirst[1]);
    free(multiFirst[5]);
    free(multiLast[4]);
    free(multiLast[5]);
    free(failed[1]);
    free(failed[3]);
    free(failed[4]);
    free(failed[5]);
    free(successor[4]);
    free(successor[5]);
    reorderCleanup();
    RtpvCleanupQueue(&rtpQueue);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            source_file = pathlib.Path(temp) / 'real_fec.c'
            binary = pathlib.Path(temp) / 'real_fec'
            source_file.write_text(harness, encoding='utf-8')
            sources = [source_file, COMMON / 'src/RtpVideoQueue.c',
                       COMMON / 'nanors/rs.c', COMMON / 'nanors/deps/obl/oblas_common.c',
                       COMMON / 'nanors/deps/obl/oblas_lite.c']
            subprocess.run([
                'cc', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                '-I', str(COMMON / 'src'), '-I', str(COMMON / 'enet/include'),
                '-I', str(COMMON / 'nanors'),
                '-I', str(COMMON / 'nanors/deps/obl'),
                *map(str, sources), '-o', str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True,
                           env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))


if __name__ == '__main__':
    unittest.main()
