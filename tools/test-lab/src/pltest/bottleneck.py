# ps5-native-app-boilerplate - evidence-bounded native video diagnosis.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
from .decoder_analysis import distribution
from .session_evidence import host_transport_rates
from .phases import _offset, bounded_alignment
from .delivery import session_delivery


def measured_host_transport(report, begin, end):
    """Reuse validated counter rates, selecting only the bounded PS5 measure phase."""
    unavailable = {'verified': False, 'reason': 'HOST_MEASUREMENT_ALIGNMENT_UNAVAILABLE'}
    if not bounded_alignment(report).get('steady_state_comparable'):
        return unavailable
    try:
        anchors = report['phase_anchors']
        offsets = [_offset(anchors[key], anchors[key]['ack']['monotonic_us'])
                   for key in ('ps5_start', 'ps5_end_probe')]
        low, high = min(v[0] for v in offsets), max(v[1] for v in offsets)
        samples = [s for s in report['host_measurement']['session_samples']
                   if s['coordinator_start_us'] + low >= begin and
                   s['coordinator_end_us'] + high <= end]
        value = host_transport_rates(samples)
        if not value['verified']:
            return value
        coverage = value['observation_elapsed_us_bounds'][0] / (end - begin)
        if coverage < .6:
            return dict(unavailable, reason='HOST_MEASUREMENT_COVERAGE_TOO_SHORT')
        return dict(value, measurement_phase_bounded=True, coverage=coverage,
                    semantics='host sender counters strictly inside ACK-bounded PS5 measurement; not isolated capture/encode FPS',
                    ps5_inner_begin_us=samples[0]['coordinator_end_us'] + high,
                    ps5_inner_end_us=samples[-1]['coordinator_start_us'] + low)
    except (KeyError, TypeError, ValueError, OverflowError):
        return unavailable


def diagnose(profile, report, rows, performance, begin, end, omitted=0):
    """No verdict/quality changes. Suspected stages are not physical-latency claims."""
    result = {'classification': 'insufficient-evidence', 'confidence': 'unknown',
              'reasons': [], 'automatic_quality_change': False,
              'semantics': 'native video stage diagnosis; low source FPS is valid; decode call time is not output latency'}
    host = measured_host_transport(report, begin, end)
    result['host_transport'] = host
    delivery = session_delivery(performance)
    result['session_delivery'] = delivery
    result['end_to_end_delivery_verified'] = False
    result['frame_id_gaps'] = {
        'network_observation': performance.get('network_frame_gaps'),
        'decode_observation': performance.get('decoder_frame_gaps'),
        'cause': 'unattributed; frame IDs alone cannot identify sender/network/local loss',
    }
    result['diagnostic_scope'] = 'client trace plus separately scoped host/session evidence; not all transmitted frames'
    result['limitations_detected'] = []
    if host.get('verified'):
        left, right = host['ps5_inner_begin_us'], host['ps5_inner_end_us']
    else:
        left, right = begin, end
    if right <= left or omitted:
        result['reasons'].append('TRACE_INCOMPLETE_OR_INVALID_WINDOW')
        return result
    def enqueue_time(r):
        if r['enqueue_us'] <= 0 or r['callback_network_us'] < r['enqueue_us']:
            return None
        value = r['callback_us'] - (r['callback_network_us'] - r['enqueue_us'])
        return value if value > 0 else None
    arrivals = [(r, enqueue_time(r)) for r in rows]
    arrival_evidence = bool(rows) and all(t is not None for _, t in arrivals)
    arrived = [r for r, t in arrivals if t is not None and left <= t < right]
    submitted = [r for r in rows if left <= r['callback_us'] < right]
    decoded = [r for r in rows if r['ready_us'] and left <= r['callback_us'] + r['ready_us'] < right]
    shown = {r['completion_us'] for r in rows if r['outcome'] == 1 and left <= r['completion_us'] < right}
    seconds = (right - left) / 1e6
    queue = [r['callback_network_us'] - r['enqueue_us'] for r in submitted
             if r['enqueue_us'] and r['callback_network_us'] >= r['enqueue_us']]
    budget = 1e6 / profile['client']['fps']
    queue_mean = sum(queue) / len(queue) if queue else None
    busy = sum(r['decode_us'] for r in submitted) / (seconds * 1e6)
    pending_end = submitted[-1]['pending'] if submitted else 0
    discards = performance.get('queue_overflows', 0)
    backlog = (pending_end >= 2 or discards > 0) and queue_mean is not None and queue_mean > budget
    ready = distribution([r['ready_us'] for r in submitted if r['ready_us']])
    buffered = performance.get('decoder_pipeline_depth', 1) > 1 and ready.get('mean', 0) > 2 * budget
    local_pressure = backlog and (busy >= .7 or buffered)
    loss = performance.get('network_frame_gaps', 0) + performance.get('unrecoverable_frames', 0)
    discards = performance.get('queue_overflows', 0)
    facts = dict(window_seconds=seconds, observed_arrival_fps=len(arrived) / seconds if arrival_evidence else None,
                 dequeue_fps=len(submitted) / seconds, decoded_output_fps=len(decoded) / seconds,
                 unique_presented_fps=len(shown) / seconds, decode_call_busy_fraction=busy,
                 frame_budget_us=budget, pending_begin=submitted[0]['pending'] if submitted else 0, pending_end=pending_end,
                 queue_delay_us=distribution(queue), callback_to_ready_us=ready,
                 decoded_to_flip_us=distribution([r['completion_us'] - r['callback_us'] - r['ready_us']
                     for r in submitted if r['outcome'] == 1 and r['ready_us'] and
                     r['completion_us'] >= r['callback_us'] + r['ready_us']]),
                 au_size_bytes=distribution([r['bytes'] for r in submitted]),
                 actual_compressed_mbps=(sum(r['bytes'] for r in arrived) * 8 / (seconds * 1e6)
                                         if arrival_evidence else None),
                 arrival_timestamps_available=arrival_evidence,
                 invalid_arrival_timestamp_rows=sum(t is None for _, t in arrivals),
                 input_trace_complete=arrival_evidence and not discards and not omitted and
                     not (delivery.get('conservation_verified') and delivery.get('local_predecode_discards')),
                 network_loss_session_total=loss, queue_overflows_session_total=discards)
    result['evidence'] = facts
    capture = report.get('host_measurement', {}).get('capture_evidence') or {}
    result['encoder_implementations'] = capture.get('encoders') if capture.get('verified') else None
    result['encoder_identity_semantics'] = 'current-session creation log names; multiple names mean selection is ambiguous'
    result['client_delivery'] = {
        'scope': 'observed submissions through unique presentation only; not host/network verification',
        'keeps_up_with_submissions': bool(submitted) and
            len(decoded) + max(2, len(submitted) * .05) >= len(submitted) and
            len(shown) + max(2, len(decoded) * .05) >= len(decoded),
    }
    # These are independent session totals, not phase-local rates. Positive
    # local discards rule out a blanket 'every received frame was shown' claim.
    upstream_loss = (delivery.get('conservation_verified') and
                     delivery['counters']['overflow_discarded_aus'] > 0)
    if upstream_loss:
        result['limitations_detected'].append('local-upstream-queue-loss')
    host_below_target = (host.get('verified') and
        host['rates']['frames_sent']['upper_per_second'] < profile['client']['fps'] * .95 and
        report.get('host_measurement', {}).get('scene') not in ('static', 'static-keepalive') and
        report.get('host_measurement', {}).get('measurement_validity') == 'VALID')
    if host_below_target:
        result['limitations_detected'].append('host-sender-below-target')
    if local_pressure:
        result['limitations_detected'].append('native-decode-pressure')
    if arrival_evidence and host_below_target and (local_pressure or upstream_loss):
        result.update(classification='concurrent-or-ambiguous', confidence='suspected')
        result['reasons'].append('HOST_BELOW_TARGET_AND_CLIENT_BACKLOG_OR_SESSION_QUEUE_LOSS')
        return result
    if not arrival_evidence:
        result['reasons'].append('ARRIVAL_TIMESTAMPS_UNAVAILABLE_OR_INVALID')
        return result
    if len(submitted) < 16 or seconds < 2:
        result['reasons'].append('TOO_FEW_ACTIVE_SAMPLES')
        return result
    if not arrived:
        result['reasons'].append('NO_OBSERVED_ARRIVALS_IN_WINDOW_SUBMISSIONS_MAY_BE_QUEUED')
        return result
    if performance.get('decode_errors', 0) or performance.get('drain_faults', 0):
        result['reasons'].append('DECODER_FAULT_OR_RECOVERY_REQUIRES_SEPARATE_ANALYSIS')
        return result
    if performance.get('decoder_frame_gaps', 0) and not (loss or local_pressure or upstream_loss or discards):
        result['reasons'].append('FRAME_ID_GAPS_WITHOUT_RELIABLE_CAUSE')
        return result
    if loss and local_pressure:
        result['reasons'].append('NETWORK_LOSS_AND_CLIENT_PRESSURE_COEXIST')
        return result
    if local_pressure:
        result.update(classification='decode-limited', confidence='suspected')
        result['reasons'].append('SUSTAINED_CLIENT_QUEUE_WITH_DECODE_SERVICE_OR_BUFFERED_OUTPUT_PRESSURE')
    elif loss and host.get('verified') and not discards and host['rates']['frames_sent']['lower_per_second'] > facts['observed_arrival_fps'] * 1.1:
        result.update(classification='network-limited', confidence='suspected')
        result['reasons'].append('HOST_SEND_RATE_EXCEEDS_OBSERVED_INPUT_WITH_NETWORK_LOSS')
    elif loss:
        result['reasons'].append('NETWORK_LOSS_PRESENT_WITHOUT_CONCLUSIVE_RATE_SEPARATION')
    elif upstream_loss:
        result.update(classification='upstream-local-queue-loss', confidence='observed-session-loss')
        result['reasons'].append('PREDECODE_DISCARDS_VERIFIED_FOR_SESSION_PHASE_CAUSALITY_UNRESOLVED')
    elif discards:
        result['reasons'].append('SESSION_QUEUE_DISCARD_INPUT_COMPLETENESS_UNVERIFIED')
    elif len(decoded) > len(shown) + max(2, len(decoded) * .05):
        result.update(classification='presentation-limited', confidence='suspected')
        result['reasons'].append('DECODED_OUTPUT_EXCEEDS_UNIQUE_PRESENTATION')
    elif not backlog and len(decoded) + max(2, len(arrived) * .05) >= len(arrived) and len(shown) + max(2, len(decoded) * .05) >= len(decoded):
        result.update(classification='healthy', confidence='observed-delivery')
        result['reasons'].append('CLIENT_DELIVERS_OBSERVED_DEQUEUED_AUS_ONLY')
        h = report.get('host_measurement') or {}
        if host.get('verified') and h.get('scene') not in ('static', 'static-keepalive') and h.get('measurement_validity') == 'VALID':
            lo = host['rates']['frames_sent']['lower_per_second']
            hi = host['rates']['frames_sent']['upper_per_second']
            observed = facts['observed_arrival_fps']
            if hi < profile['client']['fps'] * .95 and lo * .95 <= observed <= hi * 1.05:
                result.update(classification='host-limited', confidence='observed-source-rate')
                result['reasons'].append('ACTIVE_HOST_SENDS_BELOW_TARGET_CLIENT_KEEPS_UP_CAPTURE_VS_ENCODE_UNRESOLVED')
    else:
        result['reasons'].append('AVAILABLE_COUNTERS_DO_NOT_ISOLATE_ONE_STAGE')
    result['limitations'] = [
        'Host sender FPS is not isolated capture/encoding throughput; browser rAF is not encoder FPS.',
        'Trace observes dequeued AUs; upstream discards can hide arrivals. Session loss totals are not phase-local.',
        'Byte rates use actual compressed AUs; configured bitrate is only a ceiling.',
        'Presentation pacing and display refresh limits can legitimately discard decoded pictures.',
    ]
    return result
