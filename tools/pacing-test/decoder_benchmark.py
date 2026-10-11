# ps5-native-app-boilerplate - Decoder benchmark capture and analysis.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""Read-only PS5 FTP capture. Browser labels never change client settings."""
import csv
import hashlib
import io
import json
import math
from ftplib import FTP, error_perm
from pathlib import Path
import threading
import time
import uuid

LOGS = '/data/prosperolight/logs/'
MAX_CAPTURE_BYTES = 32 * 1024 * 1024


def retrieve(ftp, name, optional=False):
    output = bytearray()

    def append(chunk):
        if len(output) + len(chunk) > MAX_CAPTURE_BYTES:
            raise ValueError('PS5 file exceeds capture limit')
        output.extend(chunk)

    try:
        ftp.retrbinary('RETR ' + LOGS + name, append)
    except error_perm as exc:
        if optional and str(exc).startswith('550'):
            return None
        raise
    return bytes(output)


def fingerprint(data):
    return hashlib.sha256(data).hexdigest() if data is not None else None


def distribution(values):
    values = sorted(values)
    if not values:
        return {'count': 0}
    return {'count': len(values), 'mean': sum(values) / len(values),
            'p95': values[math.ceil(len(values) * .95) - 1],
            'p99': values[math.ceil(len(values) * .99) - 1], 'max': values[-1]}


def analyze(browser, performance, frame_bytes):
    settings = browser['settings']
    issues = list(browser.get('warnings', []))
    expected = {'width': settings['width'], 'height': settings['height'],
                'fps': settings['fps'], 'codec': 0 if settings['codec'] == 'H.264' else 1,
                'hdr': 0, 'vsync_requested': 1, 'bitrate_kbps': settings['bitrate_mbps'] * 1000,
                'decoder_mode': settings['decoder'], 'decoder_cores': settings['cores'],
                'decoder_pipeline_depth': 1 if settings['decoder'] == 'classic' else 3}
    mismatches = {key: {'expected': value, 'actual': performance.get(key)}
                  for key, value in expected.items() if performance.get(key) != value}
    requested_mask = (1 << (settings['cores'] * 2)) - 1
    if performance.get('decoder_cpu_affinity_requested') != requested_mask:
        mismatches['decoder_cpu_affinity_requested'] = {
            'expected': requested_mask, 'actual': performance.get('decoder_cpu_affinity_requested')}
    if mismatches:
        issues.append('Settings mismatch or decoder fallback; do not compare as the requested configuration.')
    if browser.get('aborted'):
        issues.append('Browser test aborted.')
    if performance.get('network_frame_gaps', 0) or performance.get('unrecoverable_frames', 0):
        issues.append('Network loss present; decoder-only conclusion is unsafe.')
    if performance.get('decoder_recreations', 0) or performance.get('drain_faults', 0):
        issues.append('Decoder recreated or drain failed during the run.')
    if not performance.get('performance_detail'):
        issues.append('Detailed frame timing disabled in this PS5 build.')
    if performance.get('decoder_cpu_affinity') != requested_mask:
        issues.append('Effective decoder CPU mask differs from requested mask.')
    lines = frame_bytes.decode('utf-8').splitlines()
    comment = next((line for line in lines if line.startswith('#')), '')
    omitted = 'omitted=0' not in comment
    if omitted:
        issues.append('Frame trace truncated; reconnect and start the test promptly.')
    rows = [{key: int(value) for key, value in row.items()}
            for row in csv.DictReader(line for line in lines if not line.startswith('#'))]
    if not rows:
        raise ValueError('Empty frame trace')
    origin = rows[0]['callback_us']
    buckets = {}
    for row in rows:
        buckets.setdefault((row['callback_us'] - origin) // 5000000, []).append(row)
    windows = []
    budget_us = 1000000 / settings['fps']
    for index, samples in sorted(buckets.items()):
        received = [s['enqueue_us'] for s in samples if s.get('enqueue_us')]
        displayed = [s['completion_us'] for s in samples if s['outcome'] == 1 and s['completion_us']]
        ready = [s['callback_us'] + s['ready_us'] for s in samples if s['ready_us']]
        windows.append({
            'session_start_s': index * 5, 'samples': len(samples),
            'incoming_fps': (len(received) - 1) * 1e6 / (max(received) - min(received))
                if len(received) > 1 and max(received) > min(received) else None,
            'ready_fps': (len(ready) - 1) * 1e6 / (max(ready) - min(ready))
                if len(ready) > 1 and max(ready) > min(ready) else None,
            'decode_call_us': distribution([s['decode_us'] for s in samples]),
            'callback_to_ready_us': distribution([s['ready_us'] for s in samples if s['ready_us']]),
            'queue_delay_us': distribution([s['callback_network_us'] - s['enqueue_us'] for s in samples
                                            if s['enqueue_us'] and s['callback_network_us'] >= s['enqueue_us']]),
            # Translate using paired callback clocks; do not subtract network
            # clock timestamps directly from CLOCK_MONOTONIC completion times.
            'frame_age_us': distribution([
                s['completion_us'] - s['callback_us'] + s['callback_network_us'] - s['enqueue_us']
                for s in samples if s['outcome'] == 1 and s['completion_us'] >= s['callback_us']
                and s['enqueue_us'] and s['callback_network_us'] >= s['enqueue_us']]),
            'actual_mbps': sum(s['bytes'] for s in samples) * 8 / 5e6,
            'decode_over_budget_percent': 100 * sum(s['decode_us'] > budget_us for s in samples) / len(samples),
            'pending_max': max(s['pending'] for s in samples),
            'superseded': sum(s['outcome'] == 2 for s in samples),
            'refused': sum(s['outcome'] == 3 for s in samples),
            'displayed_samples': len(displayed),
            'partial_window': (samples[-1]['callback_us'] - samples[0]['callback_us']) < 4500000,
        })
    return {'schema': 1, 'settings': settings, 'mismatches': mismatches,
            'comparison_eligible': not issues, 'issues': issues, 'frame_budget_us': budget_us,
            'windows': windows, 'performance': performance,
            'limitations': [
                'Windows are relative to PS5 session start, not browser phase start; clocks are not synchronized.',
                'Videodec2 call time is not picture latency in Adaptive; compare ready throughput and callback-to-ready too.',
                'Accepted CPU mask is configuration evidence, not measured worker utilization.',
                'Actual Mbps in partial windows is underestimated; exclude first/last partial windows.',
                'Browser scene does not fix encoder GOP, slices, actual bitrate or host processing load.',
            ]}


class DecoderBenchmarks:
    def __init__(self, root, host, port=2121, timeout=180, poll=3):
        self.root, self.host, self.port = Path(root), host, port
        self.timeout, self.poll = timeout, poll
        self.lock = threading.Lock()
        self.active = None
        self.baseline = None
        self.trace_baseline = None

    def connect(self):
        ftp = FTP()
        ftp.connect(self.host, self.port, timeout=15)
        ftp.login()
        return ftp

    def write(self, run_id, value):
        folder = self.root / run_id
        folder.mkdir(parents=True, exist_ok=True)
        temporary = folder / 'status.tmp'
        temporary.write_text(json.dumps(value, ensure_ascii=False), encoding='utf-8')
        temporary.replace(folder / 'status.json')

    def start(self, settings):
        if settings.get('suite') != 'decoder-v1' or settings.get('codec') not in ('H.264', 'HEVC'):
            raise ValueError('Unsupported decoder test')
        if (settings.get('decoder') not in ('classic', 'adaptive') or
                settings.get('cores') not in (3, 4, 5) or settings.get('fps') not in (60, 120) or
                (settings.get('width'), settings.get('height')) not in ((1920, 1080), (3840, 2160)) or
                settings.get('bitrate_mbps') not in (20, 80)):
            raise ValueError('Unsupported settings')
        if settings.get('mode') != 'Unpaced' or not settings.get('vsync') or settings.get('hdr'):
            raise ValueError('Use Unpaced, VSync On and SDR')
        with self.lock:
            if self.active:
                raise ValueError('Previous run is still waiting for PS5 logs; end that stream first.')
            with self.connect() as ftp:
                self.baseline = fingerprint(retrieve(ftp, 'performance-last.json', optional=True))
                self.trace_baseline = fingerprint(retrieve(ftp, 'performance-frames-mode0.csv', optional=True))
            self.active = uuid.uuid4().hex
            self.write(self.active, {'status': 'running', 'settings': settings, 'started_server_utc_s': time.time()})
            timer = threading.Timer(300, self.expire, args=(self.active,))
            timer.daemon = True
            timer.start()
            return self.active

    def expire(self, run_id):
        with self.lock:
            if self.active == run_id:
                self.write(run_id, {'status': 'timeout', 'error': 'No completed capture; restart this test.'})
                self.active = None

    def finish(self, browser):
        run_id = browser.get('decoder_run_id')
        with self.lock:
            if run_id != self.active:
                raise ValueError('Unknown or expired run; start again.')
            state = self.status(run_id)
            if state['status'] != 'running' or browser['settings'] != state['settings']:
                raise ValueError('Duplicate report or changed settings')
            (self.root / run_id / 'browser.json').write_text(json.dumps(browser, ensure_ascii=False), encoding='utf-8')
            self.write(run_id, {'status': 'waiting_for_stream_end', 'settings': browser['settings']})
            worker = threading.Thread(target=self.capture, args=(run_id, browser, self.baseline, self.trace_baseline), daemon=True)
            worker.start()
        return run_id

    def status(self, run_id):
        if len(run_id) != 32 or any(c not in '0123456789abcdef' for c in run_id):
            raise ValueError('Invalid run ID')
        return json.loads((self.root / run_id / 'status.json').read_text(encoding='utf-8'))

    def capture(self, run_id, browser, baseline, trace_baseline):
        deadline = time.monotonic() + self.timeout
        error = 'No new performance-last.json; end the stream and enable Diagnostic logs.'
        try:
            while time.monotonic() < deadline:
                try:
                    with self.connect() as ftp:
                        raw = retrieve(ftp, 'performance-last.json', optional=True)
                        if raw is None or fingerprint(raw) == baseline:
                            time.sleep(self.poll)
                            continue
                    # Summary is written before CSV. Wait for the CSV write/rename.
                    time.sleep(self.poll)
                    with self.connect() as ftp:
                        trace = retrieve(ftp, 'performance-frames-mode0.csv')
                        if fingerprint(trace) == trace_baseline:
                            raise ValueError('No fresh Unpaced frame CSV; check pacing mode and Diagnostic logs.')
                        current_raw = retrieve(ftp, 'performance-last.json')
                        if fingerprint(current_raw) != fingerprint(raw):
                            raise ValueError('Session changed during capture; finish one run before starting the next.')
                        folder = self.root / run_id
                        (folder / 'performance-last.json').write_bytes(raw)
                        (folder / 'performance-frames-mode0.csv').write_bytes(trace)
                        for name in ('debug-trace.txt', 'videoout-policy.log'):
                            data = retrieve(ftp, name, optional=True)
                            if data is not None:
                                (folder / name).write_bytes(data)
                    summary = analyze(browser, json.loads(raw), trace)
                    (folder / 'analysis.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding='utf-8')
                    with self.lock:
                        if self.active == run_id:
                            self.write(run_id, {'status': 'saved', 'comparison_eligible': summary['comparison_eligible'],
                                                'issues': summary['issues'], 'mismatches': summary['mismatches']})
                            self.active = None
                    return
                except (OSError, EOFError, error_perm, ValueError) as exc:
                    error = str(exc)
                    time.sleep(self.poll)
            raise TimeoutError(error)
        except Exception as exc:
            with self.lock:
                if self.active == run_id:
                    self.write(run_id, {'status': 'error', 'error': str(exc)})
                    self.active = None
