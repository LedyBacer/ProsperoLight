# ps5-native-app-boilerplate - measurement validity and clock-aware trace analysis.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import csv
import io
import json
import math
import re
from pathlib import Path

from .decoder_analysis import distribution, analyze as decoder_analyze

MAX_BYTES = 64 * 1024 * 1024
CLOCKS = {
    'ps5': 'PS5 monotonic callback/VideoOut flip observation; not physical scanout',
    'network': 'paired callback clocks translate enqueue age; never subtract raw network clock from PS5 clock',
    'browser': 'Windows browser performance.now/rAF; no subtraction from PS5 clock',
    'phase_alignment': 'approximate: sequential host start ACK then PS5 start_measure ACK; no cross-clock offset measured',
    'input_to_photon': None,
}


def read_bounded(path):
    path = Path(path)
    if path.is_symlink() or path.stat().st_size > MAX_BYTES:
        raise ValueError('analysis input is unsafe or oversized')
    return path.read_bytes()


def parse_trace(data):
    if len(data) > MAX_BYTES:
        raise ValueError('trace exceeds analysis byte limit')
    lines = data.decode('utf-8').splitlines()
    header = next((line for line in lines if line.startswith('#')), '')
    fields = dict(re.findall(r'(schema|count|omitted)=(\d+)', header))
    if fields.get('schema') != '2' or 'count' not in fields or 'omitted' not in fields:
        raise ValueError('unsupported or missing trace metadata')
    rows = []
    required = {'frame', 'bytes', 'pending', 'outcome', 'enqueue_us', 'callback_network_us',
                'callback_us', 'decode_us', 'ready_us', 'completion_us'}
    for raw in csv.DictReader(line for line in lines if not line.startswith('#')):
        if not required <= raw.keys() or None in raw:
            raise ValueError('trace columns missing')
        row = {key: int(value) for key, value in raw.items()}
        if any(value < 0 for value in row.values()) or row['outcome'] not in (0, 1, 2, 3):
            raise ValueError('invalid trace sample')
        if rows and row['callback_us'] < rows[-1]['callback_us']:
            raise ValueError('nonmonotonic trace callback clock')
        rows.append(row)
        if len(rows) > 32768:
            raise ValueError('trace exceeds supported ring capacity')
    if len(rows) != int(fields['count']) or not rows:
        raise ValueError('trace count mismatch or empty trace')
    return rows, int(fields['omitted'])


def analyze_run(profile, report, directory):
    """Classifications remain separate. Missing evidence cannot become performance PASS."""
    host = report.get('host_measurement') or {}
    ps5 = report.get('ps5_measurement') or {}
    receipt = ps5.get('receipt') or {}
    result = {'schema_version': 'prosperolight-test-analysis/1',
              'functional_verdict': report.get('verdict', 'INCONCLUSIVE'),
              'measurement_validity': 'INCONCLUSIVE', 'performance_delta': 'NOT_COMPARABLE',
              'issues': [], 'metrics': {}, 'windows': [], 'clock_semantics': CLOCKS,
              'backend': profile.get('client', {}).get('codec'), 'simulated': report.get('execution_mode') != 'real'}
    issues = result['issues']
    if str(report.get('reason_code', '')).startswith('UNSUPPORTED'):
        result['measurement_validity'] = 'UNSUPPORTED'
    if result['simulated']:
        issues.append('SIMULATED_ADAPTERS')
        return result
    if host.get('measurement_validity') != 'VALID': issues.append('HOST_MEASUREMENT_INVALID')
    if not ps5.get('title_exit_verified') or ps5.get('stale_artifacts'): issues.append('INCOMPLETE_OR_STALE_PS5')
    if receipt.get('functional_verdict') == 'PASS' and ps5.get('title_exit_verified'):
        result['functional_verdict'] = 'PASS'
    if receipt.get('functional_verdict') == 'FAIL': result['functional_verdict'] = 'FAIL'
    elif receipt.get('functional_verdict') != 'PASS': issues.append('PS5_RECEIPT_NOT_PASS')
    if receipt.get('negotiated') is None: issues.append('NEGOTIATED_SETTINGS_UNVERIFIED')
    if not receipt.get('artifacts_complete'): issues.append('ARTIFACTS_INCOMPLETE')
    artifact_dir = Path(directory) / 'ps5' / 'artifacts'
    if profile.get('client', {}).get('codec') == 'pyrowave':
        try:
            result['pyrowave'] = analyze_pyrowave(artifact_dir, receipt, profile)
            begin, end = receipt['measure_start_monotonic_us'], receipt['measure_end_monotonic_us']
            if end <= begin: raise ValueError('PyroWave measurement bounds unavailable')
            unique = receipt.get('measurement_unique_frames')
            if type(unique) is not int or unique <= 0: raise ValueError('confirmed unique measurement count unavailable')
            span = (end-begin)/1e6
            result['metrics'] = {'measurement_seconds':span,'unique_presented_fps':unique/span,
                'ready_to_submit_us':result['pyrowave']['ready_to_submit_us'],
                'submit_to_post_present_observation_us':result['pyrowave']['submit_to_post_present_observation_us']}
            result['trace'] = {'count':result['pyrowave']['output_samples'], 'omitted':0}
            result['browser_phases'] = browser_phases(host)
            if not issues: result['measurement_validity'] = 'VALID'
        except (OSError,ValueError,TypeError,KeyError) as exc:
            issues.append('PYROWAVE_ANALYSIS_UNAVAILABLE: '+str(exc)[:160])
        return result
    try:
        performance = json.loads(read_bounded(artifact_dir / 'performance-last.json'))
        name = receipt.get('trace', {}).get('name', '')
        if not re.fullmatch(r'performance-frames-mode[012]\.csv', name):
            raise ValueError('missing supported trace')
        data = read_bounded(artifact_dir / name)
        rows, omitted = parse_trace(data)
        if omitted: issues.append('TRACE_TRUNCATED')
        if not performance.get('performance_detail'): issues.append('DETAILED_TIMING_DISABLED')
        if performance.get('decode_errors', 0) or performance.get('drain_faults', 0) or performance.get('present_errors', 0):
            result['functional_verdict'] = 'FAIL'; issues.append('DECODER_OR_PRESENT_FAULT')
        if performance.get('network_frame_gaps', 0) or performance.get('unrecoverable_frames', 0):
            issues.append('NETWORK_LOSS')
        if performance.get('decoder_recreations', 0): issues.append('DECODER_RECREATED')
        client = profile['client']
        if client['codec'] in ('hevc', 'h264'):
            width, height = {'1080p': (1920, 1080), '1440p': (2560, 1440), '2160p': (3840, 2160)}[client['resolution']]
            settings = {'width': width, 'height': height, 'fps': client['fps'],
                        'codec': 'HEVC' if client['codec'] == 'hevc' else 'H.264',
                        'bitrate_mbps': client['bitrate_mbps'], 'decoder': client['decoder_pipeline'],
                        'cores': client['decoder_cores']}
            # Legacy analyzer assumes SDR + VSync. Check actual profile separately.
            expected = {'width': width, 'height': height, 'fps': client['fps'],
                        'codec': 1 if client['codec'] == 'hevc' else 0,
                        'hdr': int(client['hdr']), 'bitrate_kbps': client['bitrate_mbps'] * 1000,
                        'vsync_requested': int(client['vsync']), 'decoder_mode': client['decoder_pipeline'],
                        'decoder_cores': client['decoder_cores']}
            mismatches = {k: {'expected': v, 'actual': performance.get(k)} for k, v in expected.items()
                          if performance.get(k) != v}
            result['settings_mismatches'] = mismatches
            if mismatches: issues.append('EFFECTIVE_SETTINGS_MISMATCH')
            legacy = decoder_analyze({'settings': settings}, performance, data)
            mismatches.update({k:v for k,v in legacy['mismatches'].items() if k not in ('hdr','vsync_requested')})
            if mismatches and 'EFFECTIVE_SETTINGS_MISMATCH' not in issues: issues.append('EFFECTIVE_SETTINGS_MISMATCH')
            requested_mask = (1 << (client['decoder_cores'] * 2)) - 1
            if performance.get('decoder_cpu_affinity') != requested_mask: issues.append('EFFECTIVE_CPU_MASK_MISMATCH')
            if performance.get('vsync_fallbacks',0): issues.append('OUTPUT_MODE_FALLBACK')
            result['windows'] = legacy['windows']
            result['limitations'] = legacy['limitations']
        else:
            # Backend timing is analyzed independently below; never call it Videodec latency.
            result['pyrowave'] = analyze_pyrowave(artifact_dir, receipt, profile)
        begin, end = receipt.get('measure_start_monotonic_us'), receipt.get('measure_end_monotonic_us')
        if type(begin) is not int or type(end) is not int or end <= begin:
            raise ValueError('measurement bounds unavailable')
        samples = [r for r in rows if begin <= r['callback_us'] < end]
        if len(samples) < 2: raise ValueError('insufficient measured samples')
        completions = sorted({r['completion_us'] for r in samples if r['outcome'] == 1 and begin <= r['completion_us'] < end})
        span = (end - begin) / 1e6
        metrics = result['metrics']
        metrics['measurement_seconds'] = span
        metrics['unique_presented_fps'] = len(completions) / span
        if not completions:
            result['functional_verdict'] = 'FAIL'; issues.append('ZERO_PRESENTED_MEASURED_FRAMES')
        metrics['received_fps'] = len(samples) / span
        metrics['actual_mbps'] = sum(r['bytes'] for r in samples) * 8 / (span * 1e6)
        metrics['decode_call_us'] = distribution([r['decode_us'] for r in samples])
        metrics['callback_to_ready_us'] = distribution([r['ready_us'] for r in samples if r['ready_us']])
        metrics['callback_to_flip_observed_us'] = distribution([r['completion_us'] - r['callback_us'] for r in samples
                        if r['outcome'] == 1 and r['completion_us'] >= r['callback_us']])
        metrics['frame_age_us'] = distribution([r['completion_us'] - r['callback_us'] + r['callback_network_us'] - r['enqueue_us']
                        for r in samples if r['outcome'] == 1 and r['completion_us'] >= r['callback_us']
                        and r['enqueue_us'] and r['callback_network_us'] >= r['enqueue_us']])
        metrics['flip_interarrival_us'] = distribution([b-a for a, b in zip(completions, completions[1:])])
        metrics['pending_max'] = max(r['pending'] for r in samples)
        metrics['superseded'] = sum(r['outcome'] == 2 for r in samples)
        metrics['refused'] = sum(r['outcome'] == 3 for r in samples)
        result['trace'] = {'count': len(rows), 'measured_count': len(samples), 'omitted': omitted}
        result['browser_phases'] = browser_phases(host)
        result['host'] = {'raf_fps': host.get('raf_fps'), 'scene': host.get('scene'),
                          'gpu_samples': len(host.get('gpu_samples', [])), 'encoding_latency_ms': None}
        if result['measurement_validity'] != 'UNSUPPORTED' and not issues:
            result['measurement_validity'] = 'VALID'
    except (OSError, ValueError, TypeError, KeyError, OverflowError) as exc:
        issues.append('TRACE_ANALYSIS_UNAVAILABLE: ' + str(exc)[:160])
    return result


def analyze_pyrowave(directory, receipt, profile):
    mode = {'unpaced': 0, 'paced': 1, 'vrr': 2}[profile['client']['pacing']]
    data = read_bounded(Path(directory) / f'pyrowave-output-mode{mode}.csv')
    lines = data.decode().splitlines()
    header = dict(re.findall(r'(mode|fps|count|omitted)=(\d+)', lines[0]))
    if header.get('mode') != str(mode) or header.get('fps') != str(profile['client']['fps']):
        raise ValueError('PyroWave output configuration mismatch')
    if header.get('omitted') != '0': raise ValueError('PyroWave output trace truncated')
    rows = [{k: int(v) for k, v in r.items()} for r in csv.DictReader(lines[1:])]
    if len(rows) != int(header.get('count', -1)) or len(rows) > 32768:
        raise ValueError('PyroWave trace count mismatch')
    begin, end = receipt['measure_start_monotonic_us'], receipt['measure_end_monotonic_us']
    samples = [r for r in rows if begin <= r['observed_us'] < end]
    if len(samples) < 2: raise ValueError('PyroWave measured output samples absent')
    observed = [r['observed_us'] for r in samples]
    if any(b < a for a,b in zip(observed, observed[1:])):
        raise ValueError('PyroWave output clock not monotonic')
    unique = len({r['frame'] for r in samples})
    return {'unique_pictures': unique, 'output_samples': len(samples),
            'repeat_samples': len(samples)-unique,
            'ready_to_submit_us': distribution([r['submit_us']-r['ready_us'] for r in samples
                                               if r['submit_us'] >= r['ready_us']]),
            'submit_to_post_present_observation_us': distribution([r['observed_us']-r['submit_us'] for r in samples
                                                  if r['observed_us'] >= r['submit_us']]),
            'observed_interval_us': distribution([b-a for a,b in zip(observed, observed[1:])]),
            'clock': 'PS5 monotonic post-present CPU observations; observed_us is not the completion time of that picture; not physical HDMI scanout'}


def browser_phases(host):
    """Only browser-local intervals; boundaries are approximate at one rAF interval."""
    intervals = host.get('intervals_ms', [])
    if not isinstance(intervals, list) or len(intervals) > 250000:
        raise ValueError('browser interval limit')
    phases = [('simple', 0, 20), ('noise-motion', 20, 60), ('recovery', 60, 80)] if host.get('scene') == 'decoder-regression' else [('measure',0,float('inf'))]
    elapsed = 0; samples = {name: [] for name,_,_ in phases}
    for interval in intervals:
        if type(interval) not in (int,float) or not math.isfinite(interval) or not 0 < interval <= 60000:
            raise ValueError('invalid browser interval')
        elapsed += interval/1000
        for name, start, end in phases:
            if start <= elapsed < end: samples[name].append(interval)
    return [{'phase': name, 'interval_ms': distribution(values),
             'raf_fps':len(values)*1000/sum(values) if values else None,
             'clock':'browser-local approximate rAF boundary; not synchronized to PS5'}
            for name,values in samples.items()]
