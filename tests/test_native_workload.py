# ps5-native-app-boilerplate / ProsperoLight - AUD boundaries and actual slice accounting for synthetic fixtures.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    'native_workload', Path(__file__).resolve().parents[1] / 'tools/native-workload.py')
workload = importlib.util.module_from_spec(spec)
spec.loader.exec_module(workload)


class NativeWorkloadTests(unittest.TestCase):
    def test_actual_h264_slices_and_contiguous_offsets(self):
        aud = b'\0\0\0\1\x09\xf0'
        one = aud + b'\0\0\1\x65\x80'
        four = aud + b'\0\0\1\x41\x80' * 4
        units = workload.access_units(one + four, 'h264')
        self.assertEqual([u['slices'] for u in units], [1, 4])
        self.assertEqual([u['idr'] for u in units], [True, False])
        self.assertEqual(units[0]['offset'], 0)
        self.assertEqual(units[0]['bytes'], units[1]['offset'])
        self.assertEqual(sum(u['bytes'] for u in units), len(one + four))

    def test_hevc_idr_is_distinct_from_cra(self):
        aud = b'\0\0\1\x46\x01\x50'
        data = aud + b'\0\0\1\x26\x01\x80' + aud + b'\0\0\1\x2a\x01\x80'
        units = workload.access_units(data, 'hevc')
        self.assertEqual([u['idr'] for u in units], [True, False])

    def test_missing_aud_empty_nal_and_missing_bootstrap_refused(self):
        for data in (b'', b'\0\0\1\x65\x80', b'\0\0\1\x09\xf0\0\0\1',
                     b'\0\0\1\x09\xf0\0\0\1\x41\x80'):
            with self.subTest(data=data), self.assertRaises(ValueError):
                workload.access_units(data, 'h264')
