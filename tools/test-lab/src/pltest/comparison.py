# ps5-native-app-boilerplate - paired screening with strict environmental compatibility.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import json
import math
import re
import statistics

from .suites import DEFINITIONS

REQUIRED_ENVIRONMENT = ('firmware', 'host_version', 'encoder_settings', 'network_class', 'build_flags', 'renderer')


def fingerprint(profile, report):
    environment = (report.get('analysis') or {}).get('environment') or {}
    host = report.get('host_measurement') or {}
    display = host.get('selected_display') or {}
    client = {k: v for k, v in profile.get('client', {}).items() if k not in ('host', 'app')}
    required_display = ('id', 'width', 'height', 'refresh_hz')
    missing = [key for key in REQUIRED_ENVIRONMENT if not environment.get(key)]
    missing += ['display.' + k for k in required_display if display.get(k) is None]
    for key in ('content_sha256', 'pixel_sample_sha256', 'seed', 'scene'):
        if host.get(key) is None: missing.append('stimulus.' + key)
    effective = (report.get('analysis') or {}).get('session_settings') or {}
    if not effective or any(field.get('verified') is not True for field in effective.values()):
        missing.append('effective_settings')
    if environment.get('verified') is not True: missing.append('environment.verified')
    value = {'environment': {k: environment.get(k) for k in REQUIRED_ENVIRONMENT},
             'stream': client, 'display': {k: display.get(k) for k in required_display},
             'stimulus': {k: host.get(k) for k in ('content_sha256', 'pixel_sample_sha256', 'seed', 'scene')},
             'workload': profile.get('host_stimulus', {}),
             'effective':{key:field.get('value') for key,field in effective.items()},
             'timing': {k: profile.get('timing', {}).get(k) for k in ('warmup_seconds', 'measure_seconds')},
             'detail': (report.get('analysis') or {}).get('trace', {}).get('omitted')}
    return {'value': value, 'missing': missing,
            'sha256': hashlib.sha256(json.dumps(value, sort_keys=True).encode()).hexdigest(),
            'source_sha': profile.get('source_sha'), 'build_label': profile.get('build_label')}


def build_identity(profile, report):
    ps5 = report.get('ps5_measurement') or {}
    installed, variant = ps5.get('installed') or {}, ps5.get('build_variant') or {}
    identity = dict(source_sha=profile.get('source_sha'), build_label=installed.get('build_label'),
                    eboot_sha256=installed.get('eboot_sha256'))
    if (ps5.get('installed_identity_verified') is not True or
        not re.fullmatch(r'[0-9a-f]{40}', identity['source_sha'] or '') or
        not re.fullmatch(r'[0-9a-f]{64}', identity['eboot_sha256'] or '') or
        not identity['build_label'] or identity['build_label'] != profile.get('build_label') or
        any(variant.get(key) != value for key,value in identity.items())):
        raise ValueError('BUILD_IDENTITY_UNVERIFIED')
    return identity


def compare_pairs(pairs, calibration=False, screening=None):
    result = _compare_pairs(pairs, calibration, screening)
    result['overall_verdict'] = result['summary_verdict'] = result['verdict']
    result['execution_state'] = 'COMPLETED'
    return result


def _compare_pairs(pairs, calibration=False, screening=None):
    result = {'schema_version': 'prosperolight-test-comparison/1', 'verdict': 'INCONCLUSIVE',
              'functional_verdict': 'INCONCLUSIVE', 'measurement_validity': 'INCONCLUSIVE',
              'performance_delta': 'NOT_COMPARABLE', 'reason_code': None, 'pairs': [],
              'screening': dict(DEFINITIONS['screening']), 'calibration': calibration, 'limitations': [
                  'Paired screening heuristics are not statistical proof.',
                  'Run interleaved A/B pairs; distribution and ordered drift are reported.',
                  'Build/source identities are recorded separately; changed code is allowed only with identical build flags/environment.']}
    if screening:
        if not isinstance(screening, dict) or set(screening) - set(result['screening']):
            result['reason_code'] = 'SCREENING_CONFIGURATION_INVALID'; return result
        for key,value in screening.items():
            if (type(value) not in (int,float) or not math.isfinite(value) or value <= 0 or
                    key == 'minimum_pairs' and (type(value) is not int or not 3 <= value <= 20) or
                    key.endswith('fraction') and value >= 1):
                result['reason_code'] = 'SCREENING_CONFIGURATION_INVALID'; return result
        result['screening'].update(screening)
    if len(pairs) < result['screening']['minimum_pairs']:
        result['reason_code'] = 'INSUFFICIENT_REPEAT_PAIRS'; return result
    seen = set(); expected = None
    latency_keys = None
    expected_builds = [None, None]
    for a, b in pairs:
        identities = []
        for side, run in enumerate((a, b)):
            rid = run['run_id']
            if rid in seen:
                result['reason_code'] = 'REUSED_RUN_IN_PAIRS'; return result
            seen.add(rid)
            report, profile = run['report'], run['profile']['profile']
            if report is None:
                result['reason_code'] = 'REPORT_NOT_READY'; return result
            analysis = report.get('analysis') or {}
            if analysis.get('functional_verdict') == 'FAIL':
                result.update(verdict='FAIL', functional_verdict='FAIL', reason_code='FUNCTIONAL_FAILURE'); return result
            if report.get('execution_mode') != 'real' or analysis.get('measurement_validity') != 'VALID':
                result['reason_code'] = 'INVALID_MEASUREMENT'; return result
            try:
                identity = build_identity(profile, report)
            except ValueError as exc:
                result['reason_code'] = str(exc); return result
            if expected_builds[side] is None: expected_builds[side] = identity
            if expected_builds[side] != identity:
                result['reason_code'] = 'BUILD_CHANGED_WITHIN_VARIANT'; return result
            identities.append(identity)
            fp = fingerprint(profile, report)
            if fp['missing']:
                result.update(reason_code='FINGERPRINT_INCOMPLETE', missing=fp['missing']); return result
            if expected is None: expected = fp['sha256']
            if expected != fp['sha256']:
                result['reason_code'] = 'ENVIRONMENT_MISMATCH'; return result
        equal = identities[0]['eboot_sha256'] == identities[1]['eboot_sha256']
        if calibration and identities[0] != identities[1] or not calibration and (equal or identities[0]['source_sha'] == identities[1]['source_sha']):
            result['reason_code'] = 'CALIBRATION_REQUIRES_SAME_BUILD' if calibration else 'DISTINCT_BUILDS_REQUIRED'; return result
        am, bm = a['report']['analysis']['metrics'], b['report']['analysis']['metrics']
        latency_keys = ('ready_to_submit_us', 'submit_to_post_present_observation_us') if a['profile']['profile']['client']['codec'] == 'pyrowave' else ('frame_age_us', 'callback_to_ready_us')
        metrics = {}
        for key, quantile in [('unique_presented_fps', None)] + [(k, 'p95') for k in latency_keys]:
            av = am.get(key); bv = bm.get(key)
            if quantile: av = (av or {}).get(quantile); bv = (bv or {}).get(quantile)
            if type(av) not in (int, float) or type(bv) not in (int, float) or not math.isfinite(av) or not math.isfinite(bv) or av <= 0 or bv < 0:
                result['reason_code'] = 'METRIC_UNAVAILABLE'; return result
            metrics[key] = {'baseline': av, 'candidate': bv, 'delta': bv-av, 'fraction': (bv-av)/av}
        result['pairs'].append({'baseline': a['run_id'], 'candidate': b['run_id'], 'metrics': metrics,
            'baseline_identity': identities[0], 'candidate_identity': identities[1]})
    screening = result['screening']; summaries = {}
    for key in ('unique_presented_fps', *latency_keys):
        vals = [p['metrics'][key] for p in result['pairs']]
        fractions = [v['fraction'] for v in vals]; deltas = [v['delta'] for v in vals]
        summaries[key] = {'median_fraction': statistics.median(fractions), 'median_delta': statistics.median(deltas),
                          'ordered_fractions': fractions, 'ordered_deltas': deltas, 'range_fraction': [min(fractions), max(fractions)],
                          'baseline_first_to_last': vals[-1]['baseline']-vals[0]['baseline'],
                          'candidate_first_to_last': vals[-1]['candidate']-vals[0]['candidate']}
    fps = summaries['unique_presented_fps']['median_fraction']
    if calibration:
        noise = {key: max(abs(value) for value in metric['ordered_fractions']) for key,metric in summaries.items()}
        usable = noise['unique_presented_fps'] <= screening['fps_drop_fraction'] and all(
            noise[key] <= screening['latency_increase_fraction'] or max(abs(value) for value in summaries[key]['ordered_deltas']) <= screening['latency_increase_us']
            for key in latency_keys)
        result.update(verdict='PASS' if usable else 'INCONCLUSIVE', functional_verdict='PASS',
            measurement_validity='VALID', performance_delta='CALIBRATION', reason_code='AA_NOISE_CALIBRATION',
            metrics=summaries, noise_floor_fraction=noise, screening_usable=usable, sample_pairs=len(pairs))
        return result
    regression = fps < -screening['fps_drop_fraction']
    improved = fps > screening['fps_drop_fraction']
    for key in latency_keys:
        v = summaries[key]
        regression |= v['median_fraction'] > screening['latency_increase_fraction'] and v['median_delta'] > screening['latency_increase_us']
        improved |= v['median_fraction'] < -screening['latency_increase_fraction'] and v['median_delta'] < -screening['latency_increase_us']
    # Every pair must support the aggregate direction to screen a regression/improvement.
    inconsistent = any(min(v['ordered_fractions']) < 0 < max(v['ordered_fractions']) for v in summaries.values())
    delta = 'NO_CLEAR_CHANGE' if inconsistent or regression and improved else 'REGRESSED' if regression else 'IMPROVED' if improved else 'NO_CLEAR_CHANGE'
    result.update(verdict='FAIL' if delta == 'REGRESSED' else 'PASS', functional_verdict='PASS',
                  measurement_validity='VALID', performance_delta=delta, reason_code='PAIRED_SCREENING',
                  metrics=summaries, fingerprint_sha256=expected, sample_pairs=len(pairs))
    return result
