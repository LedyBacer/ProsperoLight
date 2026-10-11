# ps5-native-app-boilerplate - Decoder benchmark capture regressions.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import csv
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import time
import unittest
from unittest.mock import patch
from ftplib import error_perm

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('decoder_benchmark', ROOT / 'tools/pacing-test/decoder_benchmark.py')
bench = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bench)


def fixture():
    settings = dict(suite='decoder-v1', trial_id='H4', codec='HEVC', width=3840, height=2160,
                    fps=120, bitrate_mbps=80, decoder='adaptive', cores=3, mode='Unpaced', vsync=True, hdr=False)
    browser = dict(schema=1, settings=settings, aborted=False, warnings=[])
    performance = dict(width=3840, height=2160, fps=120, codec=1, hdr=0, vsync_requested=1,
                       bitrate_kbps=80000, decoder_mode='adaptive', decoder_cores=3,
                       decoder_pipeline_depth=3, decoder_cpu_affinity=63,
                       decoder_cpu_affinity_requested=63, performance_detail=1)
    # The network clock intentionally has a huge offset from monotonic time.
    rows = []
    for n in range(3):
        callback = 1000000 + n * 8333
        rows.append(dict(frame=n, bytes=8000, pending=2, outcome=1,
                         enqueue_us=callback + 1000000000 - 2000,
                         callback_network_us=callback + 1000000000, callback_us=callback,
                         decode_us=900, ready_us=15000, completion_us=callback + 18000))
    output = io.StringIO()
    output.write('# schema=2,count=3,omitted=0\n')
    writer = csv.DictWriter(output, fieldnames=rows[0].keys())
    writer.writeheader()
    writer.writerows(rows)
    return browser, performance, output.getvalue().encode()


class FakeFTP:
    def __init__(self, files):
        self.files = files

    def __enter__(self):
        return self

    def __exit__(self, *args):
        pass

    def retrbinary(self, command, callback):
        name = command.rsplit('/', 1)[-1]
        if name not in self.files:
            raise error_perm('550 missing')
        callback(self.files[name])


class DecoderBenchmarkTests(unittest.TestCase):
    def test_picture_latency_and_cross_clock_age_are_not_decode_call_time(self):
        browser, performance, trace = fixture()
        result = bench.analyze(browser, performance, trace)
        window = result['windows'][0]
        self.assertEqual(window['decode_call_us']['mean'], 900)
        self.assertEqual(window['callback_to_ready_us']['mean'], 15000)
        self.assertEqual(window['queue_delay_us']['mean'], 2000)
        self.assertEqual(window['frame_age_us']['mean'], 20000)
        self.assertGreater(window['incoming_fps'], 119)

    def test_configuration_fallback_is_not_compared_as_adaptive(self):
        browser, performance, trace = fixture()
        performance.update(decoder_pipeline_depth=1, decoder_cores=3)
        browser['settings']['cores'] = 5
        result = bench.analyze(browser, performance, trace)
        self.assertFalse(result['comparison_eligible'])
        self.assertIn('decoder_cores', result['mismatches'])
        self.assertIn('decoder_pipeline_depth', result['mismatches'])

    def test_network_browser_and_truncated_trace_are_flagged(self):
        browser, performance, trace = fixture()
        browser['warnings'] = ['Browser FPS too low']
        performance['network_frame_gaps'] = 2
        result = bench.analyze(browser, performance, trace.replace(b'omitted=0', b'omitted=9'))
        self.assertFalse(result['comparison_eligible'])
        self.assertEqual(len(result['issues']), 3)

    def test_archive_waits_for_new_summary_and_new_trace(self):
        browser, performance, trace = fixture()
        with tempfile.TemporaryDirectory() as directory:
            capture = bench.DecoderBenchmarks(directory, 'example', timeout=.3, poll=.005)
            files = {'performance-last.json': b'{}', 'performance-frames-mode0.csv': b'old'}
            with patch.object(capture, 'connect', side_effect=lambda: FakeFTP(files)):
                run_id = capture.start(browser['settings'])
                browser['decoder_run_id'] = run_id
                capture.finish(browser)
                self.assertEqual(capture.status(run_id)['status'], 'waiting_for_stream_end')
                files['performance-last.json'] = json.dumps(performance).encode()
                time.sleep(.02)
                # A newly saved summary does not authorize archiving stale CSV.
                self.assertNotEqual(capture.status(run_id)['status'], 'saved')
                files['performance-frames-mode0.csv'] = trace
                deadline = time.monotonic() + 1
                while time.monotonic() < deadline and capture.status(run_id)['status'] != 'saved':
                    time.sleep(.005)
                self.assertEqual(capture.status(run_id)['status'], 'saved')
                folder = Path(directory) / run_id
                self.assertEqual((folder / 'performance-frames-mode0.csv').read_bytes(), trace)
                self.assertTrue((folder / 'analysis.json').exists())
                self.assertIsNone(capture.active)

    def test_reject_overlapping_tests_and_wrong_pacing(self):
        browser, _, _ = fixture()
        with tempfile.TemporaryDirectory() as directory:
            capture = bench.DecoderBenchmarks(directory, 'example')
            with patch.object(capture, 'connect', return_value=FakeFTP({})):
                run_id = capture.start(browser['settings'])
                with self.assertRaises(ValueError):
                    capture.start(browser['settings'])
                capture.expire(run_id)
                wrong = dict(browser['settings'], mode='Paced+VRR')
                with self.assertRaises(ValueError):
                    capture.start(wrong)


if __name__ == '__main__':
    unittest.main()
