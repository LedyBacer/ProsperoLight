# ps5-native-app-boilerplate - bounded opt-in Nonary UDP probe regression checks.
# Copyright (C) 2026 LedyBacer and contributors
# SPDX-License-Identifier: GPL-3.0-or-later
import importlib
import contextlib
import io
import json
import pathlib
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools/test-lab/src'))
probe = importlib.import_module('pltest.active_link_probe')


class ActiveLinkProbeTests(unittest.TestCase):
    def test_safe_explicit_rate_and_pinned_address_required(self):
        self.assertEqual(probe.expected_packets(5000, 1200), 937)
        for rate in (0, 4999, 100001, 3000000):
            with self.assertRaises(ValueError):
                probe.expected_packets(rate, 1200)
        self.assertEqual(probe.pinned_origin('https://192.0.2.15:47984'),
                         ('192.0.2.15', 47984))
        for url in ('http://192.0.2.15', 'https://example.com',
                    'https://192.0.2.15/launch', 'https://user:pass@192.0.2.15',
                    'https://192.0.2.15?redirect=1'):
            with self.assertRaises(ValueError):
                probe.pinned_origin(url)

    def test_reordered_complete_packet_sequence_meets_requested_rate(self):
        rate, size = 10000, 1200
        expected = probe.expected_packets(rate, size)
        arrivals = [10000 + (((index + 1) * probe.DURATION_MS + expected - 1) // expected - 1) * 1000
                    for index in range(expected)]
        # Arrival *sequence* may be reordered; indexed IDs cannot hide a loss.
        assessed = probe.grade_probe(rate, size, arrivals, expected, 2000.0)
        self.assertEqual(assessed['classification'], 'SMOOTH_AT_REQUESTED_RATE')
        self.assertEqual(assessed['received_unique'], expected)
        self.assertLess(assessed['arrival_variation_p99_ms'], 1)

    def test_bursty_loss_and_unsent_host_packets_fail_qualification(self):
        rate, size = 5000, 1200
        expected = probe.expected_packets(rate, size)
        arrivals = [10000 + (index * 2000000 // expected) for index in range(expected)]
        for index in range(expected // 2, expected // 2 + 50):
            arrivals[index] = None
        result = probe.grade_probe(rate, size, arrivals, expected, 2000.0)
        self.assertEqual(result['classification'], 'RATE_NOT_QUALIFIED')
        self.assertGreater(result['worst_window_loss_percent'], 5)
        unsent = [10000 + (index * 2000000 // expected) if index < expected - 30 else None
                  for index in range(expected)]
        self.assertEqual(probe.grade_probe(rate, size, unsent,
                                           expected - 30, 2000)['classification'],
                         'RATE_NOT_QUALIFIED')

    def test_host_response_identity_and_counts_are_strict(self):
        self.assertEqual(probe.parse_host_result(
            b'<root status_code="200"><expected>936</expected><sent>935</sent>'
            b'<elapsedMs>2000.2</elapsedMs></root>', 936), (935, 2000.2))
        for body in (b'<root status_code="500"/>', b'<root status_code="200">'
                     b'<expected>7</expected><sent>7</sent><elapsedMs>2000</elapsedMs></root>'):
            with self.assertRaises(ValueError):
                probe.parse_host_result(body, 936)

    def test_no_default_network_traffic_and_unsupported_is_not_failure(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(probe, 'run_probe') as run:
            output = pathlib.Path(folder) / 'test.json'
            argv = ['--host', 'https://192.0.2.15:47984',
                    '--host-cert', 'host.pem', '--client-cert', 'client.pem',
                    '--client-key', 'client.key', '--kbps', '5000', '--evidence', str(output)]
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                probe.main(argv)
            run.assert_not_called()
            run.return_value = {'classification': 'UNSUPPORTED', 'reason': 'not implemented'}
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(probe.main([*argv, '--allow-active-traffic-on-idle-host']), 3)
            self.assertEqual(json.loads(output.read_text())['classification'], 'UNSUPPORTED')


if __name__ == '__main__':
    unittest.main()
