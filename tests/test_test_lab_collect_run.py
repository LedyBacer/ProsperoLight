# ps5-native-app-boilerplate - bounded artifact collection contracts.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from ftplib import error_perm
from unittest.mock import patch
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/test-lab'))
try:
    spec = importlib.util.spec_from_file_location('collect_run', ROOT / 'tools/test-lab/collect_run.py')
    collector = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(collector)
finally:
    sys.path.pop(0)

class CollectionTests(unittest.TestCase):
    def test_missing_receipt_disconnect_and_fresh_partial_artifacts(self):
        class Missing:
            def retrbinary(self, command, append):
                raise error_perm('550 absent')
        class Disconnected:
            def retrbinary(self, command, append):
                raise ConnectionError('disconnected')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with patch.object(collector.time, 'monotonic', side_effect=[0, 2]):
                with self.assertRaises(TimeoutError):
                    collector.collect(Missing(), 'run', root / 'missing', 1)
            self.assertFalse((root / 'missing').exists())
            with self.assertRaises(ConnectionError):
                collector.collect(Disconnected(), 'run', root / 'disconnect', 1)
            receipt = {'schema': 'prosperolight-test-receipt/1', 'run_id': 'run',
                       'artifacts_complete': False, 'artifacts': [
                           {'name': 'debug-trace.txt', 'bytes': 2, 'fresh': True},
                           {'name': 'performance-last.json', 'bytes': 2, 'fresh': False}]}
            class Partial:
                def retrbinary(self, command, append):
                    if 'receipts/' in command:
                        append(json.dumps(receipt).encode())
                    else:
                        self.assert_debug(command)
                        append(b'ok')
                def assert_debug(self, command):
                    if not command.endswith('/debug-trace.txt'):
                        raise AssertionError('stale artifact requested')
            collector.collect(Partial(), 'run', root / 'partial', 1)
            self.assertEqual((root / 'partial/debug-trace.txt').read_bytes(), b'ok')
            self.assertFalse((root / 'partial/performance-last.json').exists())

    def test_identity_allowlist_byte_counts_and_archive(self):
        receipt = {'schema': 'prosperolight-test-receipt/1', 'run_id': 'run',
                   'functional_verdict': 'PASS', 'artifacts_complete': True,
                   'artifacts': [{'name': 'performance-last.json', 'bytes': 2}]}
        class Fake:
            def retrbinary(self, command, append):
                append(json.dumps(receipt).encode() if 'receipts/' in command else b'{}')
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory)
            result = collector.collect(Fake(), 'run', p / 'valid', 1)
            self.assertEqual(result['run_id'], 'run')
            receipt['schema'] = 'prosperolight-test-receipt/2'
            self.assertEqual(collector.collect(Fake(), 'run', p / 'valid-v2', 1)['schema'], receipt['schema'])
            self.assertEqual((p / 'valid/performance-last.json').read_bytes(), b'{}')
            with self.assertRaises(FileExistsError): collector.collect(Fake(), 'run', p / 'valid', 1)
            receipt['run_id'] = 'different'
            with self.assertRaises(ValueError): collector.collect(Fake(), 'run', p / 'wrong', 1)
            self.assertFalse((p / 'wrong').exists())
            receipt['run_id'] = 'run'; receipt['artifacts'][0]['name'] = '../pairing/key.pem'
            with self.assertRaises(ValueError): collector.collect(Fake(), 'run', p / 'unsafe', 1)
            receipt['artifacts'][0]['name'] = 'performance-last.json'
            receipt['artifacts'][0]['bytes'] = 3
            with self.assertRaises(ValueError): collector.collect(Fake(), 'run', p / 'short', 1)
            receipt['artifacts_complete'] = False
            collector.collect(Fake(), 'run', p / 'incomplete', 1)
            self.assertEqual(list((p / 'incomplete').iterdir()), [p / 'incomplete/receipt.json'])
