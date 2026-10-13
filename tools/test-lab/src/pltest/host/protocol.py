# ps5-native-app-boilerplate - versioned worker commands and measured Ready gate.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import math
import re

SCHEMA = 'prosperolight-host-worker/1'
ACTIONS = {'prepare', 'start', 'stop', 'status', 'clock_probe'}
SCENES = {'low-motion', 'noise', 'scrolling', 'static-keepalive', 'stress', 'recover', 'decoder-regression', 'gpu-entropy', 'test-pattern'}


def request(value):
    if not isinstance(value, dict) or set(value) - {'schema', 'run_id', 'command_id', 'action', 'settings'}:
        raise ValueError('invalid worker command')
    if value.get('schema') != SCHEMA or value.get('action') not in ACTIONS:
        raise ValueError('unsupported worker command')
    for key in ('run_id', 'command_id'):
        if not isinstance(value.get(key), str) or not re.fullmatch(r'[A-Za-z0-9_-]{1,127}', value[key]):
            raise ValueError('unsafe worker identity')
    if value['action'] == 'prepare':
        s = value.get('settings')
        if not isinstance(s, dict) or set(s) - {'scene', 'seed', 'fps', 'seconds', 'warmup_seconds', 'monitor', 'require_hdr',
                                               'entropy', 'movement', 'update_hz', 'render_width', 'render_height', 'workload_backend'}:
            raise ValueError('invalid stimulus settings')
        if s.get('workload_backend', 'canvas') not in ('canvas','webgl2'):
            raise ValueError('invalid workload backend')
        if s.get('scene') not in SCENES or not isinstance(s.get('monitor'), str):
            raise ValueError('unknown scene/monitor')
        for key, low, high in (('seed', 0, 0xffffffff), ('fps', 30, 240), ('seconds', 1, 1800), ('warmup_seconds', 0, 120)):
            val = s.get(key, 0 if key == 'warmup_seconds' else None)
            if type(val) is not int or not low <= val <= high:
                raise ValueError('invalid stimulus range: ' + key)
        if type(s.get('require_hdr', False)) is not bool:
            raise ValueError('invalid HDR requirement')
        for key, low, high in (('entropy', 0, 1), ('movement', 0, 8), ('update_hz', 1, 240),
                               ('render_width', 320, 3840), ('render_height', 240, 2160)):
            if key in s and (type(s[key]) not in (int, float) or not math.isfinite(s[key]) or not low <= s[key] <= high):
                raise ValueError('invalid GPU workload range: ' + key)
        for key in ('render_width', 'render_height'):
            if key in s and type(s[key]) is not int:
                raise ValueError('GPU workload resolution must be integer')
    return value


def raf_rate_reason(fps, display, target_fps):
    if type(fps) not in (float, int) or not math.isfinite(fps) or fps < target_fps * .95:
        return 'RAF_BELOW_TARGET'
    if fps > max(target_fps, display.get('refresh_hz') or target_fps) * 1.05:
        return 'RAF_ABOVE_TARGET'
    return None


def readiness(page, display, target_fps, desktop):
    reasons = []
    if not desktop.get('interactive'):
        reasons.append('NONINTERACTIVE_OR_LOCKED')
    if page.get('visibility') != 'visible' or page.get('focused') is not True:
        reasons.append('BROWSER_HIDDEN_OR_UNFOCUSED')
    if page.get('fullscreen') is not True:
        reasons.append('FULLSCREEN_REQUIRED')
    rate_reason = raf_rate_reason(page.get('raf_fps'), display, target_fps)
    if rate_reason: reasons.append(rate_reason)
    # Screen coordinates are CSS pixels; compare physical coordinates using reported DPR.
    dpr = page.get('dpr', 0)
    if type(dpr) not in (float, int) or not math.isfinite(dpr) or dpr <= 0:
        reasons.append('INVALID_SCREEN_GEOMETRY')
    else:
        center_x = (page.get('screen_x', 0) + page.get('outer_width', 0) / 2) * dpr
        center_y = (page.get('screen_y', 0) + page.get('outer_height', 0) / 2) * dpr
        if not (display['x'] <= center_x < display['x'] + display['width'] and
                display['y'] <= center_y < display['y'] + display['height']):
            reasons.append('WRONG_DISPLAY')
        if abs(page.get('screen_width', 0) * dpr - display['width']) > 4 or abs(page.get('screen_height', 0) * dpr - display['height']) > 4:
            reasons.append('SCREEN_MODE_MISMATCH')
    return {'ready': not reasons, 'measurement_validity': 'VALID' if not reasons else 'INCONCLUSIVE', 'reasons': reasons}


def validate_report(value):
    if not isinstance(value, dict) or value.get('type') != 'report':
        raise ValueError('invalid page report')
    intervals = value.get('intervals_ms')
    if not isinstance(intervals, list) or len(intervals) > 250000 or any(
            type(v) not in (int, float) or not math.isfinite(v) or not 0 < v <= 60000 for v in intervals):
        raise ValueError('invalid bounded frame intervals')
    total = sum(intervals)
    value['raf_fps'] = len(intervals) * 1000 / total if total else 0
    if value.get('measured_intervals') != len(intervals):
        raise ValueError('interval count mismatch')
    return value
