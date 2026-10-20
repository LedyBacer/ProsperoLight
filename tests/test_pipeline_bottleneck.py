# ps5-native-app-boilerplate - stage diagnosis must not tune variable-FPS sources.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import sys
import unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/test-lab/src'))
from pltest.bottleneck import diagnose, measured_host_transport


def rows(fps=60, seconds=4, decode_us=5000, pending=0, queue=0, shown=True):
    values=[]
    for i in range(fps*seconds):
        now=1000000+i*1000000//fps
        values.append(dict(frame=i,bytes=10000,pending=pending,outcome=1 if shown else 2,
            enqueue_us=now-queue,callback_network_us=now,callback_us=now,
            decode_us=decode_us,ready_us=decode_us,completion_us=now+decode_us+1000 if shown else 0))
    return values


class BottleneckTests(unittest.TestCase):
    profile={'client':{'fps':60}}
    def classify(self,samples,performance=None,report=None):
        return diagnose(self.profile,report or {},samples,performance or {},1000000,5000000)
    def test_low_source_rate_is_healthy_not_decoder_overload(self):
        value=self.classify(rows(30))
        self.assertEqual(value['classification'],'healthy')
        self.assertFalse(value['automatic_quality_change'])
    def test_decode_pressure_needs_backlog_and_busy_service(self):
        value=self.classify(rows(48,decode_us=20000,pending=7,queue=140000))
        self.assertEqual(value['classification'],'decode-limited')
        self.assertEqual(value['confidence'],'suspected')
        self.assertEqual(self.classify(rows(30,decode_us=20000))['classification'],'healthy')
    def test_network_and_decode_pressure_is_ambiguous(self):
        value=self.classify(rows(48,decode_us=20000,pending=7,queue=140000),{'network_frame_gaps':2})
        self.assertEqual(value['classification'],'insufficient-evidence')
    def test_presenter_deficit(self):
        value=self.classify(rows(shown=False))
        self.assertEqual(value['classification'],'presentation-limited')
    def test_host_reported_low_rate_requires_independent_bounded_evidence(self):
        host={'verified':True,'ps5_inner_begin_us':1000000,'ps5_inner_end_us':5000000,
              'rates':{'frames_sent':{'lower_per_second':29.9,'upper_per_second':30.1}}}
        report={'host_measurement':{'scene':'low-motion','measurement_validity':'VALID'}}
        with patch('pltest.bottleneck.measured_host_transport',return_value=host):
            self.assertEqual(self.classify(rows(30),report=report)['classification'],'host-limited')
            report['host_measurement']['scene']='static-keepalive'
            self.assertEqual(self.classify(rows(30),report=report)['classification'],'healthy')
    def test_loss_without_rate_separation_cannot_prove_network_bottleneck(self):
        self.assertEqual(self.classify(rows(50),{'network_frame_gaps':2})['classification'],'insufficient-evidence')
    def test_missing_alignment_and_incomplete_trace(self):
        self.assertFalse(measured_host_transport({},1000000,5000000)['verified'])
        value=diagnose(self.profile,{},rows(),{},1000000,5000000,omitted=1)
        self.assertEqual(value['classification'],'insufficient-evidence')
    def test_decoder_failure_not_disguised_as_performance_shortfall(self):
        value=self.classify(rows(48,decode_us=20000,pending=7,queue=140000),{'decode_errors':1})
        self.assertEqual(value['classification'],'insufficient-evidence')

if __name__=='__main__':unittest.main()
