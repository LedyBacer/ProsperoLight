# ps5-native-app-boilerplate - bounded start ACK clock alignment.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import math


def start_alignment(report):
    """Local ACK timestamps bracketed by coordinator request/response clocks."""
    anchors = report.get('phase_anchors') or {}
    host = report.get('host_measurement') or {}
    try:
        browser, ps5 = anchors['browser_start'], anchors['ps5_start']
        browser_ack = browser['ack']['browser_start']
        ps5_ack = ps5['ack']
        if browser_ack['run_id'] != report['run_id'] or browser_ack['command_id'] != host['start_command_id']:
            raise ValueError('browser phase identity mismatch')
        if ps5_ack['run_id'] != report['run_id'] or ps5_ack['command_action'] != 'start_measure' or ps5_ack['command_result'] != 'accepted':
            raise ValueError('PS5 phase identity mismatch')
        remote = [browser_ack['browser_origin_ms']*1000, ps5_ack['monotonic_us']]
        offsets = []
        for anchor, clock in zip((browser, ps5), remote):
            begin, end = anchor['coordinator_start_us'], anchor['coordinator_end_us']
            if any(type(v) not in (int,float) or not math.isfinite(v) for v in (begin,end,clock)) or begin > end:
                raise ValueError('invalid ACK time bounds')
            offsets.append((clock-end,clock-begin))
        low = offsets[1][0]-offsets[0][1]
        high = offsets[1][1]-offsets[0][0]
        return {'verified': True, 'ps5_minus_browser_us': [low,high],
                'start_uncertainty_us': high-low, 'clock_drift_verified': False,
                'steady_state_comparable': False,
                'reason': 'start ACK bounds only; end clock drift has not been independently bounded',
                'ps5_start_command_id': ps5_ack['command_id'],
                'browser_start_command_id': browser_ack['command_id']}
    except (KeyError, TypeError, ValueError, OverflowError):
        return {'verified': False, 'steady_state_comparable': False,
                'reason': 'matching start ACK identities and local time bounds unavailable'}
