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
        elif incoming['stall_count']:
            classification = 'INPUT_STARVATION_OR_HOST_REDUCED_CAPTURE'
        else:
            classification = 'CLIENT_PRESENTATION_STALL'
    return {'received': incoming, 'completed': output, 'classification': classification,
            'stall_threshold_us': stall_us,
            'clock': 'PS5 monotonic; completion observation is not physical scanout',
            'host_cause': None,
            'host_cause_reason': 'RTSP counters and host telemetry must be correlated before assigning encoder versus network cause'}
