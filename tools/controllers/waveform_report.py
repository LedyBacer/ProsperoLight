#!/usr/bin/env python3
# ps5-native-app-boilerplate - Offline, numeric-only waveform comparison report.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""Read archived PS5 traces; never connect, deploy, launch or change defaults."""
import argparse
import json
import re
from pathlib import Path

PROFILES = ((768, 16000, 20000), (720, 12000, 16000), (720, 10000, 15000))
METRICS = ('receive_to_audioout', 'audioout_blocking', 'first_nonzero', 'queue_depth')
FIELDS = ('rx', 'drop', 'gaps', 'contention', 'errors', 'overflow', 'inbox_full', 'stale',
          'rejected', 'stalls', 'inputs', 'dequeued', 'starvation', 'ignored_silent')


def parse_trace(text, expected_profile, slot=0):
    """Latest counters per slot, with an unambiguous matching compile-time policy."""
    policies, metrics, counters, current_policy = set(), {}, None, None
    for line in text.splitlines():
        if 'DualSense waveform ' not in line:
            continue
        fields = dict(re.findall(r'(\w+)=([^\s]+)', line))
        if fields.get('slot') != str(slot):
            continue
        if 'waveform policy ' in line:
            values = tuple(int(fields.get(k, '-1')) for k in ('profile', 'frames', 'min_us', 'fragment_us', 'capacity', 'grain'))
            policies.add(values)
            current_policy = values
        elif 'waveform timing ' in line and fields.get('metric') in METRICS:
            metric = fields['metric']
            unit = 'frames' if metric == 'queue_depth' else 'us'
            if fields.get('software') != '1' or fields.get('unit') != unit:
                raise ValueError('unsupported timing semantics')
            summary = {k: int(fields[k]) for k in ('n', 'p50', 'p95', 'p99', 'max')}
            if any(v < 0 or v >= 2**64 for v in summary.values()):
                raise ValueError('invalid timing values')
            if not summary['p50'] <= summary['p95'] <= summary['p99']:
                raise ValueError('unordered timing percentiles')
            metrics[metric] = summary
        elif 'waveform slot=' in line:
            metrics = {}
            current_policy = None
            counters = {k: int(fields[k]) for k in FIELDS if k in fields}
    if policies != {(expected_profile, *PROFILES[expected_profile], 3072, 256)} or current_policy != (expected_profile, *PROFILES[expected_profile], 3072, 256):
        raise ValueError('missing, mixed or custom profile; cannot label this comparison row')
    if counters is None or set(counters) != set(FIELDS) or set(metrics) != set(METRICS):
        raise ValueError('incomplete waveform instrumentation')
    if any(v < 0 or v >= 2**64 for v in counters.values()):
        raise ValueError('invalid counters')
    if not counters['rx'] or any(not m['n'] for m in metrics.values()):
        raise ValueError('no measured effect')
    # Snapshot only: it cannot prove normal title exit, run identity, settings or
    # full drain. Keep those checks separate in the real Test Lab report.
    return {'profile': expected_profile, 'slot': slot, 'counters': counters, 'metrics': metrics,
            'semantics': 'software call-start age; bucket upper bounds, not physical actuator latency'}


def markdown(values):
    lines = ['# PS5 waveform software timing comparison', '',
             'Offline trace counters only. Run identity, matching build/settings, normal cleanup and physical acceptance must be checked separately.', '',
             'Times are approximate percentile bucket upper bounds (1 ms); queue depth uses 32-frame buckets. AudioOut duration is API wall time, including preemption.', '',
             '| Profile | Status | RX / first samples | RX → call p50/p95/p99 ms | First effect p50/p95/p99 ms | API duration p50/p95/p99 ms | Queue p50/p95/p99 frames | Drop / gaps / starvation |',
             '|---|---|---|---|---|---|---|---|']
    for index, name in enumerate(('Baseline', 'Balanced', 'Low latency')):
        value = values.get(index)
        if value is None:
            lines.append(f'| {name} | NOT RUN | — | — | — | — | — | — |')
            continue
        metrics, counters = value['metrics'], value['counters']
        def percentiles(metric, divisor):
            return '/'.join(f'{metrics[metric][p] / divisor:g}' for p in ('p50', 'p95', 'p99'))
        status = 'COUNTERS ONLY'
        if any(counters[k] for k in ('drop', 'gaps', 'starvation', 'errors')):
            status = 'COUNTERS SHOW ISSUES'
        lines.append(f'| {name} | {status} | {metrics["receive_to_audioout"]["n"]} / {metrics["first_nonzero"]["n"]} | {percentiles("receive_to_audioout", 1000)} | '
                     f'{percentiles("first_nonzero", 1000)} | {percentiles("audioout_blocking", 1000)} | '
                     f'{percentiles("queue_depth", 1)} | {counters["drop"]} / {counters["gaps"]} / '
                     f'{counters["starvation"]} |')
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('baseline', 'balanced', 'low-latency'):
        parser.add_argument('--' + name, type=Path, help='archived debug-trace.txt from one real PS5 run')
    parser.add_argument('--slot', type=int, choices=range(4), default=0)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    values = {}
    for index, path in enumerate((args.baseline, args.balanced, args.low_latency)):
        if path is not None:
            if path.stat().st_size > 64 * 1024 * 1024:
                parser.error('trace exceeds bounded input size')
            values[index] = parse_trace(path.read_text(encoding='utf-8', errors='replace'), index, args.slot)
    # Refuse to overwrite an existing result or silently relabel old/uninstrumented traces.
    with args.output.open('x', encoding='utf-8') as destination:
        destination.write(markdown(values))
    print(json.dumps({'profiles_read': len(values), 'hardware_verdict': 'NOT_ESTABLISHED_BY_TRACE_COUNTERS'}))


if __name__ == '__main__':
    main()
