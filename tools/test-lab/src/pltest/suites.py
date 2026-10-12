# ps5-native-app-boilerplate - bounded executable suite definitions.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import json
from pathlib import Path

DEFINITIONS = json.loads('\n'.join(line for line in Path(__file__).with_name('suites.yaml').read_text().splitlines()
                                   if not line.startswith('#')))
REAL_SUITES = tuple(DEFINITIONS['suites'])


def apply_suite(suite, profile):
    result = json.loads(json.dumps(profile))
    definition = DEFINITIONS['suites'][suite]
    result['suite'] = suite
    timing = result.setdefault('timing', {})
    for key in ('warmup_seconds', 'measure_seconds'):
        timing.setdefault(key, definition[key])
    host = result.setdefault('host_stimulus', {})
    host.setdefault('scene', definition['scene'])
    if suite == 'decoder-matrix' and (host['scene'] != 'decoder-regression' or
            timing['warmup_seconds'] != 10 or timing['measure_seconds'] != 80):
        raise ValueError('decoder-matrix requires 10s warmup and 20/40/20s measured phases')
    if suite == 'pyrowave-matrix' and result.get('client', {}).get('codec') != 'pyrowave':
        raise ValueError('pyrowave-matrix requires the pyrowave codec')
    if result.get('client', {}).get('codec') == 'pyrowave' and any(
            key in result['client'] for key in ('decoder_pipeline', 'decoder_cores')):
        raise ValueError('PyroWave does not support native decoder controls; omit them explicitly')
    # Full trace ring fits one segment, even at 120 FPS. A soak is repeated runs.
    if timing['measure_seconds'] + timing['warmup_seconds'] > 240:
        raise ValueError('split long measurements into <=240s trace segments')
    if suite == 'soak' and timing['measure_seconds'] > 120:
        raise ValueError('soak uses repeated <=120s measurement segments')
    return result
