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
    'phase_alignment': 'see phase_alignment ACK evidence; approximate fallback when independent clock bounds unavailable',
    'input_to_photon': None,
}


def network_audio_metrics(performance):
    """Optional measured counters; never infer RTT from a netem scenario name."""
    keys = ('video_network_jitter_peak_us', 'video_network_jitter_bursts',
            'video_arrival_p98_us', 'video_arrival_samples',
            'video_arrival_missing_frames', 'video_arrival_discontinuities',
            'rtp_video_packets', 'rtp_video_fec_packets',
            'rtp_video_fec_recovered', 'rtp_video_fec_failed',
            'rtp_video_out_of_sequence', 'rtp_video_invalid',
            'rtp_video_fec_invalid',
            'audio_pending_ms_high_water', 'audio_ring_frames_high_water',
            'audio_catchup_frames', 'audio_ring_overruns', 'audio_underruns',
            'audio_underrun_wait_total_us', 'audio_underrun_wait_max_us',
            'audio_startup_prebuffer_wait_us', 'audio_ring_mean_frames',
            'audio_output_errors', 'audio_prebuffer_target_frames',
            'audio_prebuffer_peak_frames', 'audio_prebuffer_increases',
            'audio_prebuffer_decreases', 'audio_first_output_call_us',
            'audio_pcm_hard_cap_frames', 'audio_first_pcm_output_us',
            'audio_silence_grains', 'audio_short_grains', 'audio_crossfades',
            'audio_drift_shed_frames', 'audio_ring_ewma_frames',
            'audio_near_miss_events', 'audio_failed_shrink_probes',
            'audio_last_output_call_us', 'audio_first_packet_call_us',
            'video_first_present_observed_us', 'rtp_audio_packets',
            'rtp_audio_fec_recovered', 'rtp_audio_fec_failed',
            'rtp_audio_out_of_sequence')
    data = {key: performance[key] for key in keys
            if type(performance.get(key)) is int and performance[key] >= 0}
    # Optional native RTP reorder counters. A missing versioned object is
    # unavailable evidence, never a zero-loss claim.
    # These counters describe fixed-VSync presentation waiting, not RTP
    # recovery or physical scanout latency. Absence is not a zero measurement.
    burst = performance.get('video_pacing_burst')
    if isinstance(burst, dict) and burst.get('version') == 1:
        for key in ('catchups', 'wait_saved_us'):
            value = burst.get(key)
            if type(value) is int and value >= 0:
                data['video_pacing_burst_' + key] = value
    reorder = performance.get('video_reorder')
    reorder_fields = ('late_packets_accepted', 'late_packets_rejected',
                      'reorder_wait_total_us', 'reorder_wait_max_us',
                      'reorder_timeouts', 'fec_recovered_after_reorder',
                      'rescued_frames', 'held_packets', 'capacity_flushes')
    if isinstance(reorder, dict) and reorder.get('available') is True and reorder.get('version') == 1:
        for key in reorder_fields:
            value = reorder.get(key)
            if type(value) is int and value >= 0:
                data['video_reorder_' + key] = value
    if type(performance.get('network_rtt_valid')) is int and \
            performance['network_rtt_valid'] == 1 and \
            type(performance.get('network_rtt_ms')) is int and performance['network_rtt_ms'] >= 0:
        data['network_rtt_ms_observed'] = performance['network_rtt_ms']
        if type(performance.get('network_rtt_variance_ms')) is int and \
                performance['network_rtt_variance_ms'] >= 0:
            data['network_rtt_variance_ms_observed'] = performance['network_rtt_variance_ms']
    return data


def passive_link_quality(performance):
    """Nonary-inspired passive tail/recovery assessment, never a bandwidth probe.

    Whole-session FEC counters cannot locate a burst and completed-frame
    arrival variation includes host encoding. This is diagnostic only: the
    client must not reduce bitrate from these signals automatically.
    """
    result = {'classification': 'UNKNOWN', 'confidence': 'insufficient',
              'bitrate_adaptation': 'disabled',
              'semantics': 'passive observations; no UDP capacity probe, no packet-time p99, '
                           'no direct estimate of available Mbps'}
    valid = (performance.get('rtp_video_stats_valid') == 1 and
             type(performance.get('rtp_video_packets')) is int and
             performance['rtp_video_packets'] >= 100 and
             type(performance.get('video_arrival_samples')) is int and
             performance['video_arrival_samples'] >= 24 and
             type(performance.get('video_arrival_p98_us')) is int)
    if not valid:
        return result
    recovered = performance.get('rtp_video_fec_recovered')
    failed = performance.get('rtp_video_fec_failed')
    oos = performance.get('rtp_video_out_of_sequence')
    if any(type(x) is not int or x < 0 for x in (recovered, failed, oos)):
        return result
    tail = performance['video_arrival_p98_us']
    rtt_variance = (performance.get('network_rtt_variance_ms')
                    if performance.get('network_rtt_valid') == 1 else None)
    unstable = tail >= 12000 or (type(rtt_variance) is int and rtt_variance >= 20)
    if failed:
        label = 'FEC_RECOVERY_FAILURES_WITH_VARIATION' if unstable else 'FEC_RECOVERY_FAILURES'
    elif unstable:
        label = 'ELEVATED_DELIVERY_VARIATION'
    elif recovered or oos:
        label = 'RECOVERY_OR_REORDER_OBSERVED'
    else:
        label = 'NO_IMPAIRMENT_OBSERVED'
    return dict(result, classification=label, confidence='limited',
                observations={'completed_frame_arrival_p98_us': tail,
                              'fec_recovered': recovered, 'fec_failed': failed,
                              'out_of_sequence': oos,
                              'rtt_variance_ms': rtt_variance})


def av_startup_observation(performance):
    """Relative local callback timing only, not decoded source A/V sync."""
    # A zero-filled hardware grain does not mean the source's first PCM played.
    audio = performance.get('audio_first_pcm_output_us')
    clock_point = 'first_real_pcm_output' if type(audio) is int and audio > 0 else 'first_audioout_call'
    if clock_point == 'first_audioout_call':
        audio = performance.get('audio_first_output_call_us')
    video = performance.get('video_first_present_observed_us')
    result = {'available': False, 'semantics': 'PS5 first PCM AudioOut call (legacy: first output call) minus '
              'video first-presentation observation; different physical output delays, '
              'unknown source timestamp alignment; NOT lip-sync error',
              'audio_clock_point': clock_point}
    if (type(audio) is int and type(video) is int and audio > 0 and video > 0):
        return dict(result, available=True, offset_us=audio-video)
    return result


def native_overload_metrics(performance):
    """Keep opt-in native queue resets separate from delivery impairment."""
    source = performance.get('native_overload')
    if not isinstance(source, dict) or type(source.get('enabled')) is not bool:
        return {'available': False}
    keys = ('queue_limit_us', 'resyncs', 'stops', 'attempts', 'peak_queue_age_us',
            'last_action_age_us')
    values = {key: source[key] for key in keys
              if type(source.get(key)) is int and source[key] >= 0}
    return {'available': True, 'enabled': source['enabled'], **values}


def phase_clock_metrics(performance):
    """Experimental network/source phase clock; not an A/V clock or scanout."""
    source = performance.get('video_phase_clock')
    if not isinstance(source, dict) or type(source.get('enabled')) is not bool:
        return {'available': False}
    keys = ('samples', 'reanchors', 'late', 'mad_us', 'tail_us',
            'cushion_us', 'hold_us')
    values = {key: source[key] for key in keys
              if type(source.get(key)) is int and source[key] >= 0}
    for key in ('skew_us', 'min_slack_us'):
        if type(source.get(key)) is int:
            values[key] = source[key]
    return {'available': True, 'enabled': source['enabled'],
            'semantics': 'source-PTS to network-enqueue phase only; not HDMI or audio sync',
            **values}


def cadence_lane_metrics(rows, source_fps):
    """Adjacent frame interval *variation* within each independent clock domain.

    We never subtract clocks across callback/network/PS5 epochs, and never
    label an interval error input latency or glass-to-glass latency.
    """
    if type(source_fps) is not int or source_fps <= 0:
        return {'available': False}
    nominal = 1000000.0 / source_fps
    lanes = {'source_pts': 'pts_us', 'network_enqueue': 'enqueue_us',
             'decoder_ready': 'ready_us', 'present_submit': 'submit_us'}

    def clock_stamp(row, lane, field):
        # Trace schema 2 stores ready_us as callback-to-ready DURATION.
        # Only callback_us + ready_us yields a PS5-clock timestamp.
        if lane == 'decoder_ready':
            callback = row.get('callback_us', 0)
            elapsed = row.get('ready_us', 0)
            return callback + elapsed if callback > 0 and elapsed > 0 else 0
        return row.get(field, 0)
    samples = {key: [] for key in lanes}
    previous = None
    for row in rows:
        if row.get('outcome') != 1:
            previous = None
            continue
        if previous and row.get('frame') == previous.get('frame', -2) + 1:
            source_delta = row.get('pts_us', 0) - previous.get('pts_us', 0)
            if nominal * 0.5 <= source_delta <= nominal * 1.5:
                for key, field in lanes.items():
                    current_stamp = clock_stamp(row, key, field)
                    previous_stamp = clock_stamp(previous, key, field)
                    if current_stamp > 0 and previous_stamp > 0 and current_stamp >= previous_stamp:
                        samples[key].append(abs((current_stamp - previous_stamp) - nominal))
        previous = row
    metrics = {}
    for key, values in samples.items():
        if values:
            ordered = sorted(values)
            metrics[key] = {
                'samples': len(values),
                'mean_abs_interval_error_us': round(sum(values) / len(values)),
                'p95_abs_interval_error_us': round(ordered[math.ceil(len(ordered) * .95) - 1]),
                'max_abs_interval_error_us': round(ordered[-1]),
            }
        else:
            metrics[key] = {'samples': 0}
    return {'available': any(samples.values()), 'nominal_interval_us': round(nominal),
            'semantics': 'within-clock adjacent frame interval error, not cross-clock latency',
            'lanes': metrics}


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
    result = _analyze_run(profile, report, directory)
    if not result['simulated']:
        from .environment import environment
        result['environment'] = environment(profile, report, result)
    return result


def _analyze_run(profile, report, directory):
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
    from .phases import bounded_alignment, steady_windows
    result['phase_alignment'] = bounded_alignment(report)
    result['steady_phase_windows'] = steady_windows(report, result['phase_alignment'])
    if profile.get('suite') == 'decoder-matrix' and not result['steady_phase_windows']:
        issues.append('PHASE_ALIGNMENT_UNVERIFIED')
    if str(report.get('reason_code', '')).startswith('UNSUPPORTED'):
        result['measurement_validity'] = 'UNSUPPORTED'
    if result['simulated']:
        issues.append('SIMULATED_ADAPTERS')
        return result
    if host.get('measurement_validity') != 'VALID': issues.append('HOST_MEASUREMENT_INVALID')
    capture = host.get('capture_evidence') or {}
    if capture.get('verified') is not True:
        issues.append('HOST_CAPTURE_OUTPUT_UNVERIFIED')
    elif capture.get('output') != (host.get('selected_display') or {}).get('id'):
        issues.append('HOST_CAPTURE_DISPLAY_MISMATCH')
    if report.get('host_session_preflight',{}).get('verified') is False:
        issues.append('HOST_SESSION_EXCLUSIVITY_UNVERIFIED')
    if not ps5.get('title_exit_verified') or ps5.get('stale_artifacts'): issues.append('INCOMPLETE_OR_STALE_PS5')
    if receipt.get('functional_verdict') == 'PASS' and ps5.get('title_exit_verified'):
        result['functional_verdict'] = 'PASS'
    if receipt.get('functional_verdict') == 'FAIL': result['functional_verdict'] = 'FAIL'
    elif receipt.get('functional_verdict') != 'PASS': issues.append('PS5_RECEIPT_NOT_PASS')
    from .session_evidence import validate_negotiation, host_transport_rates
    result['host_transport'] = host_transport_rates(host.get('session_samples'))
    if result['host_transport'].get('reason') in ('HOST_SENDER_COUNTER_RESET','HOST_COUNTER_SESSION_CHANGED'):
        issues.append(result['host_transport']['reason'])
    result['session_settings'], negotiation_issues = validate_negotiation(profile.get('client', {}), receipt, host.get('session_samples'))
    issues.extend(negotiation_issues)
    if not receipt.get('artifacts_complete'): issues.append('ARTIFACTS_INCOMPLETE')
    artifact_dir = Path(directory) / 'ps5' / 'artifacts'
    if profile.get('client', {}).get('codec') == 'pyrowave':
        from .session_evidence import backend_effective_settings
        backend_fields, backend_issues = backend_effective_settings(receipt, profile['client'])
        result['session_settings'].update(backend_fields)
        issues.extend(backend_issues)
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
            from .progress import polled_progress
            result['progress'] = polled_progress(receipt, host.get('scene'))
            if not result['progress']['verified'] or result['progress']['classification'] != 'CONTINUOUS':
                issues.append(result['progress']['classification'])
                result['functional_verdict'] = 'INCONCLUSIVE'
            if not issues: result['measurement_validity'] = 'VALID'
        except (OSError,ValueError,TypeError,KeyError) as exc:
            issues.append('PYROWAVE_ANALYSIS_UNAVAILABLE: '+str(exc)[:160])
        return result
    try:
        performance = json.loads(read_bounded(artifact_dir / 'performance-last.json'))
        result['network_audio_metrics'] = network_audio_metrics(performance)
        result['passive_link_quality'] = passive_link_quality(performance)
        result['av_startup_observation'] = av_startup_observation(performance)
        result['native_overload'] = native_overload_metrics(performance)
        result['video_phase_clock'] = phase_clock_metrics(performance)
        if profile.get('capture', {}).get('experimental_phase_clock') and not (
                result['video_phase_clock'].get('available') and
                result['video_phase_clock'].get('enabled')):
            issues.append('EXPERIMENTAL_PHASE_CLOCK_NOT_ACTIVE')
        if (profile.get('suite') == 'network-jitter' and
                result['native_overload'].get('enabled')):
            # Network-only A/B cannot attribute this additional IDR/reset
            # policy to delivery jitter, even when the stream looks smoother.
            issues.append('NETWORK_TEST_NATIVE_OVERLOAD_CONFOUNDED')
        from .session_evidence import native_effective_settings
        from .decoder_lifecycle import decoder_lifecycle
        lifecycle = decoder_lifecycle(performance)
        result['decoder_lifecycle'] = lifecycle
        probe = bool(profile.get('capture', {}).get('decoder_capacity_probe', False))
        injected = performance.get('decoder_test_capacity_errors', 0)
        if injected:
            issues.append('DEVELOPER_DECODER_CAPACITY_PROBE')
        if probe and result['functional_verdict'] == 'PASS' and (injected != 1 or performance.get('decoder_generation', 0) < 2 or
                      performance.get('decoder_recovery_successes', 0) != 1):
            result['functional_verdict'] = 'FAIL'
            issues.append('DECODER_PROBE_NOT_RECOVERED')
        if profile.get('capture', {}).get('decoder_capacity_probe_first') and (
                type(performance.get('decoder_test_capacity_decoded_before')) is not int or
                type(performance.get('decoder_test_capacity_presented_before')) is not int or
                performance.get('decoder_test_capacity_decoded_before') != 0 or
                performance.get('decoder_test_capacity_presented_before') != 0):
            result['functional_verdict'] = 'FAIL'
            issues.append('DECODER_PROBE_NOT_BEFORE_FIRST_OUTPUT')
        if lifecycle['available'] and not lifecycle.get('valid'):
            if result['functional_verdict'] == 'PASS':
                result['functional_verdict'] = 'FAIL'
            issues.append('DECODER_LIFECYCLE_INCOMPLETE')
        if performance.get('decoder_lifecycle_failures', 0):
            result['functional_verdict'] = 'FAIL'
            issues.append('DECODER_LIFECYCLE_FAILED')
        if performance.get('decoder_recovery_failures', 0):
            result['functional_verdict'] = 'FAIL'
            issues.append('DECODER_RECOVERY_FAILED')
        if performance.get('decoder_reconfiguration_count', 0):
            issues.append('DECODER_RECONFIGURED')
        effective = native_effective_settings(performance)
        result['session_settings'].update(effective)
        issues.extend('EFFECTIVE_FIELD_UNVERIFIED:' + key for key, field in effective.items()
                      if not field['verified'])
        name = receipt.get('trace', {}).get('name', '')
        if not re.fullmatch(r'performance-frames-mode[012]\.csv', name):
            raise ValueError('missing supported trace')
        pacing = int(name.removesuffix('.csv')[-1])
        result['session_settings']['output_pacing'] = dict(value=pacing, verified=True,
            source='final renderer trace filename', category='effective', reason=None)
        requested_pacing = profile.get('client', {}).get('pacing')
        if requested_pacing is not None and pacing != {'unpaced':0,'paced':1,'vrr':2}.get(requested_pacing):
            issues.append('EFFECTIVE_FIELD_MISMATCH:output_pacing')
        data = read_bounded(artifact_dir / name)
        rows, omitted = parse_trace(data)
        result['cadence_lanes'] = cadence_lane_metrics(rows, profile['client']['fps'])
        if omitted: issues.append('TRACE_TRUNCATED')
        if not performance.get('performance_detail'): issues.append('DETAILED_TIMING_DISABLED')
        faults = performance.get('decode_errors', 0) + performance.get('drain_faults', 0)
        recovered = (lifecycle.get('valid') and faults > 0 and
                     performance.get('decoder_recovery_successes', 0) == faults and
                     performance.get('decoder_recovery_failures', 0) == 0 and
                     performance.get('decoder_lifecycle_state') in ('ready', 'stopping', 'stopped'))
        if recovered:
            issues.append('RECOVERED_DECODER_FAULT')
        if (faults and not recovered) or performance.get('present_errors', 0):
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
        from .progress import progress_diagnostics
        result['progress'] = progress_diagnostics(begin, end,
            [r['callback_us'] for r in samples], completions, host.get('scene'))
        if result['progress']['classification'] == 'CLIENT_PRESENTATION_STALL':
            result['functional_verdict'] = 'FAIL'
            issues.append('CLIENT_PRESENTATION_STALL')
        elif result['progress']['classification'] != 'CONTINUOUS':
            issues.append(result['progress']['classification'])
        if not completions:
            result['functional_verdict'] = 'FAIL'; issues.append('ZERO_PRESENTED_MEASURED_FRAMES')
        # Compatibility key: this has always counted dequeued trace AUs.
        metrics['received_fps'] = len(samples) / span
        metrics['dequeued_au_fps'] = len(samples) / span
        result['trace_scope'] = 'dequeued AUs only; receive_us describes retained rows, not all network arrivals'
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
        result['steady_phase_metrics'] = []
        for window in result['steady_phase_windows']:
            left,right = window['start_ps5_us'],window['end_ps5_us']
            phase_rows = [r for r in samples if left <= r['callback_us'] < right]
            phase_completions = sorted({r['completion_us'] for r in phase_rows
                if r['outcome'] == 1 and left <= r['completion_us'] < right})
            duration = (right-left)/1e6
            result['steady_phase_metrics'].append(dict(window,
                received_fps=len(phase_rows)/duration, unique_presented_fps=len(phase_completions)/duration,
                actual_mbps=sum(r['bytes'] for r in phase_rows)*8/(duration*1e6),
                callback_to_ready_us=distribution([r['ready_us'] for r in phase_rows if r['ready_us']]),
                callback_to_flip_observed_us=distribution([r['completion_us']-r['callback_us']
                    for r in phase_rows if r['outcome']==1 and left<=r['completion_us']<right and r['completion_us']>=r['callback_us']])))
        result['trace'] = {'count': len(rows), 'measured_count': len(samples), 'omitted': omitted}
        if client['codec'] in ('hevc', 'h264'):
            from .bottleneck import diagnose
            result['pipeline_diagnosis'] = diagnose(profile, report, rows, performance, begin, end, omitted)
            result['stream_characteristics'] = {key: performance.get(key) for key in (
                'stream_profile', 'stream_level', 'stream_bit_depth', 'stream_reference_frames',
                'stream_reorder_frames', 'stream_required_dpb_frames', 'max_dpb_frames',
                'requested_slices_per_frame', 'observed_slices_min', 'observed_slices_max',
                'stream_pps_features_known', 'stream_cabac', 'stream_hevc_tiles', 'stream_hevc_wpp',
                'stream_hevc_tile_columns', 'stream_hevc_tile_rows', 'h264_b_pictures',
                'decoder_pipeline_depth', 'decoder_cpu_affinity')}
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
    """Intervals crossing observed phase transitions are excluded."""
    intervals = host.get('intervals_ms', [])
    if not isinstance(intervals, list) or len(intervals) > 250000:
        raise ValueError('browser interval limit')
    events = host.get('phase_events') or []
    observed = bool(events)
    origin = host.get('measure_start_ms', 0)
    if type(origin) not in (int,float) or not math.isfinite(origin):
        raise ValueError('browser measurement origin unavailable')
    if observed:
        if len(events) > 16: raise ValueError('browser phase limit')
        previous = -float('inf')
        phases = []
        for event in events:
            point = event.get('browser_ms')
            if type(point) not in (int,float) or not math.isfinite(point) or point < previous:
                raise ValueError('browser phase boundaries invalid')
            previous = point
            if event.get('phase') != 'warmup':
                phases.append((event['phase'], point, event.get('phase_id')))
        end = host.get('measure_end_ms')
        if type(end) not in (int,float) or not math.isfinite(end) or end < previous:
            raise ValueError('browser measurement end invalid')
        bounds = [(name,start,phases[i+1][1] if i+1<len(phases) else end,identity)
                  for i,(name,start,identity) in enumerate(phases)]
    else:
        schedule = [('low-motion',0,20),('high-entropy',20,60),('recovery',60,80)] if host.get('scene') == 'decoder-regression' else [('measure',0,float('inf'))]
        bounds = [(name,origin+start*1000,origin+end*1000,None) for name,start,end in schedule]
    elapsed = origin
    samples = {name: [] for name,_,_,_ in bounds}
    excluded = 0
    for interval in intervals:
        if type(interval) not in (int,float) or not math.isfinite(interval) or not 0 < interval <= 60000:
            raise ValueError('invalid browser interval')
        end = elapsed + interval
        matched = False
        for name,start,finish,_ in bounds:
            if start <= elapsed and end < finish:
                samples[name].append(interval); matched = True; break
        if not matched: excluded += 1
        elapsed = end
    return [{'phase': name, 'phase_id': identity, 'start_browser_ms': start,
             'end_browser_ms': finish if math.isfinite(finish) else None,
             'boundaries_verified': observed, 'excluded_transition_intervals': excluded,
             'interval_ms': distribution(samples[name]),
             'raf_fps':len(samples[name])*1000/sum(samples[name]) if samples[name] else None,
             'clock':'browser-local observed rAF boundaries; cross-clock alignment is reported separately'}
            for name,start,finish,identity in bounds]
