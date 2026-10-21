# ps5-native-app-boilerplate - stage diagnosis must not tune variable-FPS sources.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import sys
import unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/test-lab/src'))
from pltest.bottleneck import diagnose, measured_host_transport
from pltest.delivery import COUNTERS, session_delivery


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

    def test_missing_enqueue_preserves_client_observations_but_not_input_claims(self):
        samples=rows()
        for r in samples:r['enqueue_us']=0
        value=self.classify(samples)
        self.assertEqual(value['classification'],'insufficient-evidence')
        self.assertIsNone(value['evidence']['observed_arrival_fps'])
        self.assertIsNone(value['evidence']['actual_compressed_mbps'])
        self.assertGreater(value['evidence']['decoded_output_fps'],50)
        self.assertTrue(value['client_delivery']['keeps_up_with_submissions'])
        self.assertFalse(value['evidence']['input_trace_complete'])
    def test_invalid_and_partial_arrival_evidence_is_not_zero_traffic(self):
        for kind in ('network-before-enqueue','negative-local-arrival','one-missing'):
            samples=rows()
            if kind=='network-before-enqueue': samples[0]['callback_network_us']=1
            elif kind=='negative-local-arrival': samples[0]['callback_network_us']=1000000000
            else:samples[0]['enqueue_us']=0
            value=self.classify(samples)
            self.assertEqual(value['classification'],'insufficient-evidence')
            self.assertIsNone(value['evidence']['actual_compressed_mbps'])
            self.assertEqual(value['evidence']['invalid_arrival_timestamp_rows'],1)
    def test_known_zero_arrivals_is_distinct_from_unknown(self):
        samples=rows()
        # All these AUs arrived before the measured window and were dequeued later.
        for r in samples:r['enqueue_us']=1
        value=self.classify(samples)
        self.assertEqual(value['evidence']['observed_arrival_fps'],0)
        self.assertEqual(value['evidence']['actual_compressed_mbps'],0)
        self.assertTrue(value['evidence']['arrival_timestamps_available'])
        self.assertEqual(value['classification'],'insufficient-evidence')
    def test_no_rows_cannot_establish_zero_arrival_rate(self):
        value=self.classify([])
        self.assertIsNone(value['evidence']['observed_arrival_fps'])
        self.assertIsNone(value['evidence']['actual_compressed_mbps'])
        self.assertEqual(value['evidence']['dequeue_fps'],0)
    def test_complete_evidence_measures_input_and_health(self):
        value=self.classify(rows())
        self.assertEqual(value['classification'],'healthy')
        self.assertEqual(value['evidence']['observed_arrival_fps'],60)
        self.assertGreater(value['evidence']['actual_compressed_mbps'],0)
    def test_independent_host_counter_cannot_replace_missing_client_arrivals(self):
        host={'verified':True,'ps5_inner_begin_us':1000000,'ps5_inner_end_us':5000000,
              'rates':{'frames_sent':{'lower_per_second':29.9,'upper_per_second':30.1}}}
        samples=rows(30)
        for r in samples:r['enqueue_us']=0
        report={'host_measurement':{'scene':'low-motion','measurement_validity':'VALID'}}
        with patch('pltest.bottleneck.measured_host_transport',return_value=host):
            value=self.classify(samples,report=report)
        self.assertEqual(value['classification'],'insufficient-evidence')
        self.assertTrue(value['host_transport']['verified'])
        self.assertIsNone(value['evidence']['actual_compressed_mbps'])

    @staticmethod
    def upstream(offered=240, dequeued=240, overflow=0):
        d={k:0 for k in COUNTERS}
        d.update(available=True,version=1,scope='whole-session',quiescent=True,
            start_us=1,end_us=4000001,direct_submit=False,video_packets=4000,video_bytes=4000000,
            completed_frame_events=offered,assembled_aus=offered,offered_aus=offered,
            queue_accepted_aus=offered-(1 if overflow else 0),dequeued_aus=dequeued,
            overflow_discarded_aus=overflow)
        return {'video_delivery':d}
    def test_counter_conservation_full_delivery_is_not_a_trace_only_end_to_end_claim(self):
        perf=self.upstream()
        delivery=session_delivery(perf)
        self.assertTrue(delivery['conservation_verified'])
        self.assertEqual(delivery['counters']['dequeued_aus'],240)
        value=self.classify(rows(),perf)
        self.assertEqual(value['classification'],'healthy')
        self.assertFalse(value['end_to_end_delivery_verified']) # session counters not phase-aligned host evidence
    def test_sixty_assembled_fortyeight_dequeued_cannot_be_reported_healthy(self):
        value=self.classify(rows(48),self.upstream(dequeued=192,overflow=48))
        self.assertEqual(value['classification'],'upstream-local-queue-loss')
        self.assertIn('local-upstream-queue-loss',value['limitations_detected'])
        self.assertTrue(value['session_delivery']['conservation_verified'])
        self.assertFalse(value['evidence']['input_trace_complete'])
    def test_bad_conservation_and_live_snapshot_are_not_verified(self):
        perf=self.upstream(dequeued=192,overflow=10)
        self.assertFalse(session_delivery(perf)['conservation_verified'])
        perf=self.upstream();perf['video_delivery']['quiescent']=False
        self.assertFalse(session_delivery(perf)['conservation_verified'])
        self.assertIsNone(session_delivery({})['counters']['offered_aus'])
    def test_missing_host_data_does_not_invent_host_production(self):
        value=self.classify(rows(48),self.upstream(offered=192,dequeued=192))
        self.assertEqual(value['classification'],'healthy')
        self.assertFalse(value['host_transport']['verified'])
        self.assertFalse(value['end_to_end_delivery_verified'])
        self.assertNotIn('host-sender-below-target',value['limitations_detected'])
    def test_frame_id_gaps_without_attribution_are_inconclusive(self):
        value=self.classify(rows(),{'decoder_frame_gaps':12})
        self.assertEqual(value['classification'],'insufficient-evidence')
        self.assertIn('UNATTRIBUTED',value['frame_id_gaps']['cause'].upper())
    def test_host_limit_can_coexist_with_native_decode_pressure(self):
        host={'verified':True,'ps5_inner_begin_us':1000000,'ps5_inner_end_us':5000000,
              'rates':{'frames_sent':{'lower_per_second':50.9,'upper_per_second':51.1}}}
        report={'host_measurement':{'scene':'low-motion','measurement_validity':'VALID'}}
        with patch('pltest.bottleneck.measured_host_transport',return_value=host):
            value=self.classify(rows(42,decode_us=23000,pending=10,queue=140000),
                                self.upstream(dequeued=168,overflow=72),report)
        self.assertEqual(value['classification'],'concurrent-or-ambiguous')
        self.assertIn('native-decode-pressure',value['limitations_detected'])
        self.assertIn('host-sender-below-target',value['limitations_detected'])
    def test_host_genuinely_sending_fortyeight_fps_is_separate_from_local_loss(self):
        host={'verified':True,'ps5_inner_begin_us':1000000,'ps5_inner_end_us':5000000,
              'rates':{'frames_sent':{'lower_per_second':47.9,'upper_per_second':48.1}}}
        report={'host_measurement':{'scene':'low-motion','measurement_validity':'VALID'}}
        with patch('pltest.bottleneck.measured_host_transport',return_value=host):
            value=self.classify(rows(48),self.upstream(offered=192,dequeued=192),report)
        self.assertEqual(value['classification'],'host-limited')
    def test_unknown_arrivals_still_unavailable_with_upstream_session_totals(self):
        samples=rows()
        for r in samples:r['enqueue_us']=0
        value=self.classify(samples,self.upstream())
        self.assertIsNone(value['evidence']['observed_arrival_fps'])
        self.assertIsNone(value['evidence']['actual_compressed_mbps'])
        self.assertEqual(value['classification'],'insufficient-evidence')

if __name__=='__main__':unittest.main()
