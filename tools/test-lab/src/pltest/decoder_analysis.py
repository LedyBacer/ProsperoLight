# ps5-native-app-boilerplate - shared decoder trace calculations.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import csv
import math
import statistics

def distribution(values):
    values = sorted(values)
    if not values:
        return {'count': 0}
    return {'count': len(values), 'mean': sum(values) / len(values), 'median': statistics.median(values),
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
