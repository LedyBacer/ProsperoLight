# ps5-native-app-boilerplate - decoder callback evidence validation.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

# Limelight.h and the checked-in PyroWave protocol patch define these identities.
FORMATS = {1: ('h264', False, '420'), 4: ('h264', False, '444'),
           0x100: ('hevc', False, '420'), 0x200: ('hevc', True, '420'),
           0x400: ('hevc', False, '444'), 0x800: ('hevc', True, '444'),
           0x10000: ('pyrowave', False, '420'), 0x20000: ('pyrowave', False, '444'),
           0x40000: ('pyrowave', True, '420'), 0x80000: ('pyrowave', True, '444')}


def host_transport_rates(samples):
    """Bound API counter rates in the coordinator clock; never call them capture FPS."""
    result = {'verified':False,'source':'authenticated Vibepollo /api/rtsp/sessions counters',
              'capture_fps':None,'capture_fps_reason':'no per-frame capture timestamps',
              'semantics':'API-reported counters over the host observation interval, which can include warmup; not wire throughput or exact PS5 phase'}
    if not isinstance(samples,list) or not 2 <= len(samples) <= 2048:
        return dict(result,reason='HOST_COUNTER_SAMPLES_UNAVAILABLE')
    previous=None; identity=None
    for sample in samples:
        if not isinstance(sample,dict) or sample.get('verified') is not True or sample.get('source')!='Vibepollo /api/rtsp/sessions':
            return dict(result,reason='HOST_COUNTER_SOURCE_UNVERIFIED')
        session=sample.get('session') or {}
        if not isinstance(session,dict) or not isinstance(session.get('uuid'),str) or not 1<=len(session['uuid'])<=127:
            return dict(result,reason='HOST_COUNTER_IDENTITY_UNVERIFIED')
        if identity is not None and session['uuid']!=identity:
            return dict(result,reason='HOST_COUNTER_SESSION_CHANGED')
        identity=session['uuid']
        begin,end=sample.get('coordinator_start_us'),sample.get('coordinator_end_us')
        if any(type(v) is not int or not 0<=v<2**63 for v in (begin,end)) or begin>end or previous and begin<previous['coordinator_end_us']:
            return dict(result,reason='HOST_COUNTER_CLOCK_BOUNDS_INVALID')
        for key in ('frames_sent','bytes_sent'):
            value=session.get(key)
            if type(value) is not int or not 0<=value<2**63:
                return dict(result,reason='HOST_COUNTER_FIELD_UNAVAILABLE:'+key)
            if previous and value<previous['session'][key]:
                return dict(result,reason='HOST_SENDER_COUNTER_RESET')
        previous=sample
    first,last=samples[0],samples[-1]
    minimum=last['coordinator_start_us']-first['coordinator_end_us']
    maximum=last['coordinator_end_us']-first['coordinator_start_us']
    if minimum<=0:return dict(result,reason='HOST_COUNTER_OBSERVATION_TOO_SHORT')
    rates={}
    for key in ('frames_sent','bytes_sent'):
        delta=last['session'][key]-first['session'][key]
        rates[key]={'delta':delta,'per_second':delta*2e6/(minimum+maximum),
                    'lower_per_second':delta*1e6/maximum,'upper_per_second':delta*1e6/minimum}
    return dict(result,verified=True,reason=None,api_uuid=identity,
                observation_elapsed_us_bounds=[minimum,maximum],rates=rates)


def validate_negotiation(client, receipt, host_samples=None):
    evidence = receipt.get('negotiated')
    fields, issues = {}, []
    expected_size = {'1080p': (1920, 1080), '1440p': (2560, 1440),
                     '2160p': (3840, 2160)}.get(client.get('resolution'))
    expected = dict(codec=client.get('codec'), hdr=client.get('hdr'),
                    chroma=client.get('chroma'), fps=client.get('fps'))
    if expected_size:
        expected.update(width=expected_size[0], height=expected_size[1])
    valid = (isinstance(evidence, dict) and evidence.get('verified') is True and
             evidence.get('source') == 'Limelight.renderer.setup.success')
    actual = {}
    if valid:
        for key in ('width', 'height', 'fps', 'video_format'):
            value = evidence.get(key)
            if type(value) is int and value > 0:
                actual[key] = value
        if actual.get('video_format') in FORMATS:
            actual.update(zip(('codec', 'hdr', 'chroma'), FORMATS[actual['video_format']]))
    sessions = []
    for sample in host_samples or []:
        if isinstance(sample, dict) and sample.get('verified') is True and sample.get('source') == 'Vibepollo /api/rtsp/sessions' and isinstance(sample.get('session'), dict):
            sessions.append(sample['session'])
    # Limelight's setup width/height/FPS come from client StreamConfig. Host-side
    # accepted session configuration independently confirms these values.
    for key in ('width', 'height', 'fps'):
        values = [session.get(key) for session in sessions]
        if not values or any(type(value) is not int or value <= 0 for value in values):
            actual.pop(key, None)
        elif any(value != actual.get(key) for value in values):
            issues.append('HOST_CLIENT_SETTINGS_MISMATCH:' + key)
            actual.pop(key, None)
    for key in ('video_format', 'codec', 'hdr', 'chroma', 'width', 'height', 'fps'):
        verified = key in actual
        fields[key] = dict(value=actual.get(key), verified=verified,
                           source=(evidence['source'] + ' + Vibepollo /api/rtsp/sessions' if key in ('width', 'height', 'fps') else evidence['source']) if verified else None,
                           category='negotiated', reason=None if verified else 'successful decoder setup evidence missing')
        if not verified:
            issues.append('NEGOTIATED_FIELD_UNVERIFIED:' + key)
        elif key in expected and actual[key] != expected[key]:
            issues.append('NEGOTIATED_FIELD_MISMATCH:' + key)
    if 'hdr' in actual:
        fields['bit_depth'] = dict(value=10 if actual['hdr'] else 8, verified=True,
            source=evidence['source'] + '.video_format', category='negotiated', reason=None)
    host_mapping = {'codec': 'video_format', 'hdr': 'hdr', 'chroma': 'yuv444'}
    for key, host_key in host_mapping.items():
        values = [session.get(host_key) for session in sessions]
        converted = ([{0:'h264', 1:'hevc', 3:'pyrowave'}.get(value) if type(value) is int else None for value in values]
                     if key == 'codec' else [('444' if value else '420') if type(value) is bool else None for value in values]
                     if key == 'chroma' else [value if type(value) is bool else None for value in values])
        if not converted or any(value is None for value in converted):
            issues.append('HOST_SESSION_FIELD_UNVERIFIED:' + key)
        elif any(value != actual.get(key) for value in converted):
            issues.append('HOST_CLIENT_SETTINGS_MISMATCH:' + key)
    for key in ('encoder_bitrate_kbps', 'requested_bitrate_kbps'):
        values = [session.get(key) for session in sessions]
        verified = bool(values) and all(type(value) is int and value > 0 for value in values) and len(set(values)) == 1
        fields['host_' + key] = dict(value=values[0] if verified else None, verified=verified,
            source='Vibepollo /api/rtsp/sessions.' + key if verified else None,
            category='effective' if key == 'encoder_bitrate_kbps' else 'configured',
            reason=None if verified else 'host session bitrate configuration missing or changed')
        if not verified:
            issues.append('HOST_SESSION_FIELD_UNVERIFIED:' + key)
    if sessions and fields['host_requested_bitrate_kbps']['verified'] and fields['host_requested_bitrate_kbps']['value'] != client.get('bitrate_mbps', 0) * 1000:
        issues.append('HOST_CLIENT_SETTINGS_MISMATCH:requested_bitrate_kbps')
    return fields, issues


def native_effective_settings(performance):
    """The final renderer snapshot records accepted decoder configuration, including fallback."""
    mapping = {'decoder_pipeline': 'decoder_mode', 'decoder_depth': 'decoder_pipeline_depth',
               'decoder_affinity': 'decoder_cpu_affinity', 'decoder_cores': 'decoder_cores',
               'vsync': 'vsync_active', 'output_width': 'output_width',
               'output_height': 'output_height', 'output_refresh_x100': 'refresh_x100',
               'bitrate_ceiling_kbps': 'bitrate_kbps'}
    result = {}
    for name, key in mapping.items():
        value = performance.get(key)
        verified = ((value in ('classic', 'adaptive')) if name == 'decoder_pipeline' else
                    type(value) is int and value >= 0)
        result[name] = dict(value=value if verified else None, verified=verified,
            source='performance-last.json.' + key if verified else None,
            category='configured' if name == 'bitrate_ceiling_kbps' else 'effective',
            reason=None if verified else 'final renderer configuration snapshot missing')
    return result


def backend_effective_settings(receipt, client):
    evidence = receipt.get('backend_output') or {}
    trusted = evidence.get('verified') is True and evidence.get('source') == 'PyroWave.backend.initialize.success'
    result, issues = {}, []
    for key in ('width', 'height', 'refresh_x100', 'pacing', 'vsync', 'hdr'):
        value = evidence.get(key)
        valid = trusted and (type(value) is bool if key in ('vsync', 'hdr') else
                              type(value) is int and (value in (0,1,2) if key == 'pacing' else value > 0))
        result['output_' + key] = dict(value=value if valid else None, verified=valid,
            source=evidence.get('source') if valid else None, category='effective',
            reason=None if valid else 'successful backend output configuration missing')
        if not valid: issues.append('EFFECTIVE_FIELD_UNVERIFIED:output_' + key)
    expected = dict(pacing={'unpaced':0,'paced':1,'vrr':2}.get(client.get('pacing')),
                    hdr=client.get('hdr'), vsync=client.get('vsync') or client.get('pacing') == 'vrr')
    for key, value in expected.items():
        if result['output_' + key]['verified'] and evidence[key] != value:
            issues.append('EFFECTIVE_FIELD_MISMATCH:output_' + key)
    # Integer requested FPS includes the standard 1000/1001 display-rate alias.
    # Keep the measured refresh unchanged; 59.94/119.88 are nominal 60/120,
    # while a genuine 60 Hz output for a 120 FPS request remains a fallback.
    minimum_refresh = client.get('fps', 0) * 100 * 1000 // 1001
    if result['output_refresh_x100']['verified'] and evidence['refresh_x100'] < minimum_refresh:
        issues.append('OUTPUT_MODE_FALLBACK')
    return result, issues
