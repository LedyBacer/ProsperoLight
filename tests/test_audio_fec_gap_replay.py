# ps5-native-app-boilerplate / ProsperoLight - real audio FEC sequence continuity.
# Copyright (C) 2026 LedyBacer and contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Applied Common-C queue with real nanors, synthetic RTP headers (not Opus/audio QA)."""
import os
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMON = ROOT / 'third_party/moonlight-common-c'


class AudioGapReplay(unittest.TestCase):
    def test_late_entire_block_and_expired_gap_preserve_pcm_time(self):
        code = r'''
#include "Limelight-internal.h"
#undef NDEBUG
#include <assert.h>
STREAM_CONFIGURATION StreamConfig = {0};
CONNECTION_LISTENER_CALLBACKS ListenerCallbacks = {0};
int AppVersionQuad[4] = {7,1,500,0};
int AudioPacketDuration = 5;
static uint64_t now=1000000;
uint64_t PltGetMicroseconds(void) {return now;}
static RTP_AUDIO_QUEUE q;
static unsigned real, plc;
static uint16_t last;
static int feed(uint16_t seq) {
    char memory[sizeof(RTP_PACKET)+16]={0};
    PRTP_PACKET p=(PRTP_PACKET)memory;
    p->packetType=97; p->sequenceNumber=seq; p->timestamp=seq*5; p->ssrc=42;
    int rc=RtpaAddPacket(&q,p,sizeof(memory));
    if (RTPQ_HANDLE_NOW(rc)) {++real; last=seq;}
    return rc;
}
static void drain(void) {
    uint16_t len;
    PRTP_PACKET p;
    while ((p=RtpaGetQueuedPacket(&q,8,&len))!=NULL) {
        if (len) {++real; last=((PRTP_PACKET)((char*)p+8))->sequenceNumber;}
        else ++plc;
        free(p);
    }
}
int main(void) {
    RtpaInitializeQueue(&q);
    feed(100); // ordinary synchronization skips the first partial block
    for (unsigned i=104;i<108;i++) feed(i);
    assert(q.nextRtpSequenceNumber==108 && real==4);
    q.receivedOosData=true; // already established reordered link
    for (unsigned i=112;i<116;i++) feed(i);
    drain(); assert(q.nextRtpSequenceNumber==108 && plc==0 && real==4);
    now+=19000;
    for (unsigned i=108;i<112;i++) feed(i);
    drain();
    assert(q.nextRtpSequenceNumber==116 && plc==0 && real==12 && last==115);
    // True loss: preserve all four 5ms slots through normal PLC, bounded at30ms.
    for (unsigned i=120;i<124;i++) feed(i);
    now+=30000; drain(); assert(q.nextRtpSequenceNumber==116);
    ++now; drain();
    assert(q.nextRtpSequenceNumber==124 && plc==4 && real==16 && last==123);
    feed(116); drain(); assert(q.nextRtpSequenceNumber==124 && real==16); // no stale replay
    RtpaCleanupQueue(&q);

    // Sequence wrap uses the same bounded PLC and preserves channel/sample order.
    RtpaInitializeQueue(&q);
    q.synchronizing=false; q.receivedOosData=true;
    q.oldestRtpBaseSequenceNumber=q.nextRtpSequenceNumber=65528;
    q.lastOosSequenceNumber=65528; // real OOS history at wrap, not 32768 packets old
    for (unsigned i=65532;i<65536;i++) feed((uint16_t)i);
    now+=30001; drain(); assert(q.nextRtpSequenceNumber==0 && plc==8);
    feed(0); assert(q.nextRtpSequenceNumber==1);
    RtpaCleanupQueue(&q);
}
'''
        with tempfile.TemporaryDirectory() as folder:
            source = pathlib.Path(folder) / 'audio_gap.c'
            binary = pathlib.Path(folder) / 'audio_gap'
            source.write_text(code)
            sources = [source, COMMON / 'src/RtpAudioQueue.c', COMMON / 'nanors/rs.c',
                       COMMON / 'nanors/deps/obl/oblas_common.c',
                       COMMON / 'nanors/deps/obl/oblas_lite.c']
            subprocess.run(['cc', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                            '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            '-I', str(COMMON / 'src'), '-I', str(COMMON / 'enet/include'),
                            '-I', str(COMMON / 'nanors'), '-I', str(COMMON / 'nanors/deps/obl'),
                            *map(str, sources), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True,
                           env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))


if __name__ == '__main__':
    unittest.main()
