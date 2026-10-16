# ps5-native-app-boilerplate - Offline waveform report evidence boundaries.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('waveform_report', Path(__file__).resolve().parents[1] / 'tools/controllers/waveform_report.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def trace(profile=0, slot=0):
    frames, minimum, fragment = module.PROFILES[profile]
    lines = []
    counters = {key: 0 for key in module.FIELDS}
    counters.update(rx=10, inputs=2400, dequeued=2400)
    lines.append(f'DualSense waveform slot={slot} ' + ' '.join(f'{k}={v}' for k, v in counters.items()))
    lines.append(f'DualSense waveform policy slot={slot} profile={profile} frames={frames} min_us={minimum} fragment_us={fragment} capacity=3072 grain=256')
    for name in module.METRICS:
        unit = 'frames' if name == 'queue_depth' else 'us'
        lines.append(f'DualSense waveform timing slot={slot} metric={name} unit={unit} software=1 n=10 p50=1000 p95=2000 p99=3000 max=2900')
    return '\n'.join(lines)


class WaveformReportTests(unittest.TestCase):
    def test_profiles_and_all_four_slots(self):
        for profile in range(3):
            for slot in range(4):
                value = module.parse_trace(trace(profile, slot), profile, slot)
                self.assertEqual(value['profile'], profile)
                self.assertEqual(value['slot'], slot)
                self.assertIn('not physical', value['semantics'])

    def test_missing_profiles_are_not_reported_as_pass(self):
        value = module.markdown({})
        self.assertEqual(value.count('NOT RUN'), 3)
        self.assertNotIn('PASS', value)

    def test_wrong_custom_and_mixed_profiles_rejected(self):
        for text in (trace(1), trace().replace('min_us=16000', 'min_us=15000'), trace() + '\n' + trace(1), trace().replace('capacity=3072', 'capacity=4096')):
            with self.assertRaises(ValueError):
                module.parse_trace(text, 0)

    def test_old_missing_and_unordered_instrumentation_rejected(self):
        for text in ('DualSense waveform slot=0 rx=1', trace().replace('n=10', 'n=0'),
                     trace().replace('p95=2000', 'p95=4000'), trace().replace('software=1', 'software=0'),
                     trace() + '\nDualSense waveform slot=0 rx=11'):
            with self.assertRaises(ValueError):
                module.parse_trace(text, 0)

    def test_counter_failures_visible_without_invented_hardware_verdict(self):
        value = module.parse_trace(trace().replace('starvation=0', 'starvation=7'), 0)
        report = module.markdown({0: value})
        self.assertIn('COUNTERS SHOW ISSUES', report)
        self.assertNotIn('PASS', report)
        self.assertIn('NOT RUN', report)
