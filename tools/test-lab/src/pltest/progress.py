# ps5-native-app-boilerplate - bounded continuous stream progress diagnostics.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
from .decoder_analysis import distribution


def progress_diagnostics(begin, end, received, completed, scene, stall_us=1000000):
    """Use one PS5 monotonic clock, including gaps at both measurement edges."""
    if type(begin) is not int or type(end) is not int or end <= begin:
        raise ValueError('progress measurement bounds unavailable')
    def gaps(values):
        points = sorted(set(value for value in values if begin <= value < end))
        spans = [b-a for a,b in zip([begin]+points, points+[end])]
        stalls = [(a,b) for a,b in zip([begin]+points, points+[end]) if b-a >= stall_us]
        return {'unique_events': len(points), 'longest_no_progress_us': max(spans),
                'stall_count': len(stalls), 'gap_us': distribution(spans), 'stalls': stalls}
    incoming, output = gaps(received), gaps(completed)
    classification = 'CONTINUOUS'
    if output['stall_count']:
        if scene in ('static-keepalive', 'static'):
            classification = 'STATIC_CONTENT_OR_STALL_UNRESOLVED'
        elif all(any(max(start, a) < min(stop, b) for a,b in incoming['stalls'])
                 for start,stop in output['stalls']):
            classification = 'INPUT_STARVATION_OR_HOST_REDUCED_CAPTURE'
        else:
            classification = 'CLIENT_PRESENTATION_STALL'
    return {'received': incoming, 'completed': output, 'classification': classification,
            'stall_threshold_us': stall_us,
            'clock': 'PS5 monotonic; completion observation is not physical scanout',
            'host_cause': None,
            'host_cause_reason': 'RTSP counters and host telemetry must be correlated before assigning encoder versus network cause'}


def polled_progress(receipt, scene):
    """Backend-independent completion health; cadence limits its precision."""
    value = receipt.get('continuous_progress')
    required = ('longest_no_progress_us', 'stall_count', 'stall_threshold_us', 'poll_max_us', 'samples')
    if not isinstance(value, dict) or any(type(value.get(key)) is not int or value[key] < 0 for key in required):
        return {'verified': False, 'classification': 'PROGRESS_UNVERIFIED'}
    bins = value.get('gap_log2_bins')
    if (not isinstance(bins, list) or len(bins) != 64 or
            any(type(n) is not int or n < 0 for n in bins) or
            value['samples'] < 2 or value['stall_threshold_us'] != 1000000 or
            value.get('source') != 'confirmed unique completion counter polled on control worker' or
            value.get('clock') != 'PS5 monotonic'):
        return {'verified': False, 'classification': 'PROGRESS_UNVERIFIED'}
    cadence = value['poll_max_us']
    classification = ('SAMPLING_CADENCE_UNCERTAIN' if cadence > 250000 else
        'STATIC_CONTENT_OR_STALL_UNRESOLVED' if value['stall_count'] and scene in ('static', 'static-keepalive') else
        'CLIENT_OR_INPUT_STALL_UNRESOLVED' if value['stall_count'] else 'CONTINUOUS')
    return dict(value, verified=cadence <= 250000, classification=classification,
        observation_uncertainty_us=cadence,
        gap_distribution=[{'lower_us': 0 if i == 0 else 1 << i,
                           'upper_us': (1 << (i+1))-1, 'count': n} for i,n in enumerate(bins) if n],
        semantics='completion progress sampled on control worker; histogram is not per-frame jitter or physical scanout')
