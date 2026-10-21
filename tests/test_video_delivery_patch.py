# ps5-native-app-boilerplate - Patched Limelight delivery API mapping regression.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DeliveryPatchTests(unittest.TestCase):
    @unittest.skipUnless(sys.platform.startswith('linux'), 'ELF section-GC harness uses the Linux builder')
    def test_real_patched_getter_and_datagram_hook(self):
        common = ROOT / 'third_party/moonlight-common-c'
        source = common / 'src/VideoDepacketizer.c'
        self.assertIn('LiGetVideoDeliveryStats', source.read_text())
        fields = ('video_packets', 'video_bytes', 'completed_frame_events', 'recovery_filtered_aus',
                  'empty_aus', 'assembled_aus', 'offered_aus', 'queue_accepted_aus', 'dequeued_aus',
                  'overflow_discarded_aus', 'refresh_discarded_aus', 'shutdown_discarded_aus',
                  'interrupted_discarded_aus', 'allocation_discarded_aus')
        code = '#include <assert.h>\n#include "VideoDepacketizer.c"\nint main(void) {\n'
        code += 'LI_VIDEO_DELIVERY_STATS s; assert(!LiGetVideoDeliveryStats(&s));\n'
        code += 'atomic_store(&deliveryStartUs,1); atomic_store(&deliveryEndUs,2);\n'
        for i, name in enumerate(fields, 1):
            code += f'atomic_store(&delivery[VD_{name.upper()}],{i});\n'
        code += 'assert(LiGetVideoDeliveryStats(&s));assert(s.start_us==1 && s.end_us==2);\n'
        for i, name in enumerate(fields, 1):
            code += f'assert(s.{name}=={i});\n'
        code += 'notifyVideoDatagramReceived(1200); assert(LiGetVideoDeliveryStats(&s));\n'
        code += 'assert(s.video_packets==2 && s.video_bytes==1202); return 0;}\n'
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory); (p/'test.c').write_text(code)
            subprocess.run([os.environ.get('HOST_CC','clang'), '-std=c11', '-O2',
                '-D_DEFAULT_SOURCE', '-DUSE_MBEDTLS', '-ffunction-sections', '-fdata-sections',
                '-I'+str(common/'src'), '-I'+str(common/'enet/include'),
                '-I'+str(ROOT/'third_party/mbedtls/include'), str(p/'test.c'),
                '-Wl,--gc-sections', '-o', str(p/'check')], check=True)
            subprocess.run([str(p/'check')], check=True)
