# ps5-native-app-boilerplate - decoder lifecycle evidence.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import unittest
from pltest.decoder_lifecycle import decoder_lifecycle

class DecoderEvidenceTests(unittest.TestCase):
    def sample(self):
        keys = ('decoder_generation decoder_initialization_us decoder_reconfiguration_count '
                'decoder_reconfiguration_us decoder_recovery_attempts decoder_recovery_successes '
                'decoder_recovery_failures decoder_ready_us decoder_first_decoded_us '
                'decoder_first_presented_us stream_coded_width stream_coded_height '
                'stream_required_dpb_frames max_dpb_frames decoder_cpu_memory_bytes '
                'decoder_gpu_memory_bytes decoder_shared_memory_bytes decoder_frame_pool_bytes').split()
        value = dict.fromkeys(keys, 0)
        value.update(decoder_generation=1, decoder_lifecycle_state='ready',
                     stream_parameters_verified=1, decoder_ready_us=100,
                     decoder_first_decoded_us=120, decoder_first_presented_us=150)
        return value

    def test_legacy_is_explicitly_unavailable(self):
        self.assertFalse(decoder_lifecycle({})['available'])

    def test_rtsp_or_creation_alone_does_not_establish_decode(self):
        value = self.sample()
        value['decoder_first_decoded_us'] = 0
        self.assertFalse(decoder_lifecycle(value)['valid'])

    def test_monotonic_stages_and_reconfiguration(self):
        value = self.sample()
        value['decoder_generation'] = 2
        evidence = decoder_lifecycle(value)
        self.assertTrue(evidence['valid'])
        self.assertEqual(evidence['ready_to_first_decode_us'], 20)
        self.assertEqual(evidence['decode_to_first_present_us'], 30)
        value['decoder_first_presented_us'] = 110
        self.assertFalse(decoder_lifecycle(value)['valid'])
        value['decoder_first_presented_us'] = True
        self.assertFalse(decoder_lifecycle(value)['valid'])

    def test_probe_counter_is_explicit_and_typed(self):
        value = self.sample()
        value['decoder_test_capacity_errors'] = 1
        self.assertEqual(decoder_lifecycle(value)['counters']['decoder_test_capacity_errors'], 1)
        value['decoder_test_capacity_errors'] = True
        self.assertFalse(decoder_lifecycle(value)['valid'])
