# ps5-native-app-boilerplate / ProsperoLight - actual audio FEC deadline regression.
# Copyright (C) 2026 LedyBacer and contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the applied Common-C decision, not a parallel deadline model."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class AudioFecDeadline(unittest.TestCase):
    def test_negotiated_milliseconds_and_bounded_oos_wait(self):
        text = (ROOT / 'third_party/moonlight-common-c/src/RtpAudioQueue.c').read_text()
        start = text.index('static void handleMissingPackets(')
        helpers = text[text.index('static uint64_t audioReorderHorizonUs('):
                       text.index('static bool queueHasPacketReady(')]
        decision = helpers + text[start:text.index('\nint RtpaAddPacket(', start)]
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define RTPA_DATA_SHARDS 4
#define RTPQ_OOS_WAIT_TIME_MS 10
#define LC_ASSERT(x) assert(x)
#define LC_ASSERT_VT(x) assert(x)
#define Limelog(...) ((void)0)
static int AudioPacketDuration = 5;
static uint64_t now;
static uint64_t PltGetMicroseconds(void) { return now; }
static bool isBefore16(uint16_t a,uint16_t b) { return (int16_t)(a-b)<0; }
typedef struct Block {
    struct {uint16_t baseSequenceNumber;} fecHeader;
    uint64_t queueTimeUs;
    bool allowDiscontinuity;
    unsigned dataShardsReceived,fecShardsReceived;
} Block;
typedef struct Queue {
    Block *blockHead,*blockTail;
    uint16_t nextRtpSequenceNumber,oldestRtpBaseSequenceNumber;
    bool receivedOosData;
    struct {unsigned packetCountFecFailed;} stats;
} *PRTP_AUDIO_QUEUE;
static bool queueHasPacketReady(PRTP_AUDIO_QUEUE q) {return q->blockHead->allowDiscontinuity;}
''' + decision + r'''
int main(void) {
    Block head={.fecHeader={100},.queueTimeUs=1000000}, tail={.fecHeader={104}};
    struct Queue q={.blockHead=&head,.blockTail=&tail,.nextRtpSequenceNumber=101,
        .oldestRtpBaseSequenceNumber=100,.receivedOosData=true};
    now=1025000; handleMissingPackets(&q);
    assert(!head.allowDiscontinuity && q.stats.packetCountFecFailed==0);
    now=1030000; handleMissingPackets(&q);
    assert(!head.allowDiscontinuity);
    now++; handleMissingPackets(&q);
    assert(head.allowDiscontinuity && q.stats.packetCountFecFailed==1);
    head.allowDiscontinuity=false; q.stats.packetCountFecFailed=0;
    AudioPacketDuration=10; now=1040000; handleMissingPackets(&q);
    assert(!head.allowDiscontinuity); // 4*10+10=50 ms, not 10.04 ms
    now=1050001; handleMissingPackets(&q);
    assert(head.allowDiscontinuity);
    head.allowDiscontinuity=false; q.receivedOosData=false; now=1000001;
    handleMissingPackets(&q); assert(head.allowDiscontinuity); // clean fast path unchanged
    head.allowDiscontinuity=false; q.receivedOosData=true;
    q.nextRtpSequenceNumber=96; now=1025000;
    handleMissingPackets(&q); assert(q.nextRtpSequenceNumber==96 && !audioGapExpired(&q));
    now=1050001; assert(audioGapExpired(&q));
    q.nextRtpSequenceNumber=80; handleMissingPackets(&q);
    assert(q.nextRtpSequenceNumber==100); // large discontinuity retains fast resync
    q.nextRtpSequenceNumber=101;
    head.allowDiscontinuity=false; q.blockTail=&head; q.receivedOosData=true;
    now=2000000; handleMissingPackets(&q);
    assert(!head.allowDiscontinuity); // no speculative loss without a second block
}
'''
        with tempfile.TemporaryDirectory() as folder:
            source = pathlib.Path(folder) / 'audio_fec.c'
            binary = pathlib.Path(folder) / 'audio_fec'
            source.write_text(code)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
