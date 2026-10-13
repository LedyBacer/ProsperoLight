# ps5-native-app-boilerplate - bounded start ACK clock alignment.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import math


def _offset(anchor, clock):
    begin,end=anchor['coordinator_start_us'],anchor['coordinator_end_us']
    if any(type(v) not in (int,float) or not math.isfinite(v) for v in (begin,end,clock)) or begin>end:
        raise ValueError('invalid ACK bounds')
    narrow=(anchor.get('ack') or {}).get('coordinator_ack_bounds')
    if narrow:
        left,right=narrow['begin_us'],narrow['end_us']
        if (narrow.get('source') != 'coordinator monotonic; mailbox rename through matching ACK read' or
                type(left) is not int or type(right) is not int or not begin<=left<=right<=end):
            raise ValueError('transport ACK bounds outside enclosing request')
        begin,end=left,right
    return clock-end,clock-begin


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
            offsets.append(_offset(anchor,clock))
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


def bounded_alignment(report):
    result = start_alignment(report)
    if not result['verified']: return result
    anchors = report.get('phase_anchors') or {}
    try:
        browser, ps5 = anchors['browser_end_probe'], anchors['ps5_end_probe']
        ba, pa = browser['ack']['browser_clock'], ps5['ack']
        if (ba['run_id'] != report['run_id'] or ba['command_id'] != browser['ack']['command_id'] or
                pa['run_id'] != report['run_id'] or pa['command_action'] != 'clock_probe' or
                pa['command_result'] != 'accepted'):
            raise ValueError('end probe identity mismatch')
        offsets = []
        for anchor, clock in ((browser, ba['browser_now_ms']*1000), (ps5, pa['monotonic_us'])):
            offsets.append(_offset(anchor,clock))
        low, high = offsets[1][0]-offsets[0][1], offsets[1][1]-offsets[0][0]
        start_low, start_high = result['ps5_minus_browser_us']
        host = report.get('host_measurement') or {}
        receipt = (report.get('ps5_measurement') or {}).get('receipt') or {}
        phase_acks = all(value.get('measure_'+edge+'_phase_id') == report['run_id']+':measure-'+edge
            for value in (host,receipt) for edge in ('start','end'))
        phase_acks = phase_acks and host.get('reason') == 'COMPLETE' and receipt.get('measurement_complete') is True
        result.update(clock_drift_verified=True, end_ps5_minus_browser_us=[low,high],
            endpoint_drift_bounds_us=[low-start_high, high-start_low],
            conservative_offset_us=[min(low,start_low),max(high,start_high)],
            end_uncertainty_us=high-low, measurement_phase_acks_verified=phase_acks,
            steady_state_comparable=phase_acks and max(high,start_high)-min(low,start_low)<=500000,
            reason='matching start and late measurement clock-probe ACK bounds; monotonic clock rate assumed between anchors')
    except (KeyError, TypeError, ValueError, OverflowError):
        result['reason'] = 'start ACK bounds only; matching late measurement clock-probe evidence unavailable'
    return result


def steady_windows(report, alignment):
    """Translate observed browser phases conservatively into the PS5 clock."""
    if not alignment.get('steady_state_comparable'): return []
    host = report.get('host_measurement') or {}
    receipt = (report.get('ps5_measurement') or {}).get('receipt') or {}
    events = host.get('phase_events') or []
    low, high = alignment['conservative_offset_us']
    begin, end = receipt.get('measure_start_monotonic_us'), receipt.get('measure_end_monotonic_us')
    if type(begin) is not int or type(end) is not int or end<=begin: return []
    previous = -1; boundaries = []
    for event in events:
        clock = event.get('browser_ms')
        if (event.get('run_id') != report['run_id'] or not event.get('phase_id') or
                type(clock) not in (int,float) or not math.isfinite(clock) or clock<=previous): return []
        previous = clock; boundaries.append((event,clock*1000))
    finish = host.get('measure_end_ms')
    if type(finish) not in (int,float) or not math.isfinite(finish): return []
    windows = []
    for index,(event,clock) in enumerate(boundaries):
        stop = boundaries[index+1][1] if index+1<len(boundaries) else finish*1000
        left,right = max(begin,clock+high),min(end,stop+low)
        if left<right:
            windows.append({'phase':event.get('phase'),'phase_id':event['phase_id'],
                'start_ps5_us':left,'end_ps5_us':right,'excluded_transition_uncertainty_us':high-low})
    return windows
