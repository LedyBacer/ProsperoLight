# ps5-native-app-boilerplate - decoder callback evidence validation.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

# Limelight.h and the checked-in PyroWave protocol patch define these identities.
FORMATS = {1: ('h264', False, '420'), 4: ('h264', False, '444'),
           0x100: ('hevc', False, '420'), 0x200: ('hevc', True, '420'),
           0x400: ('hevc', False, '444'), 0x800: ('hevc', True, '444'),
           0x10000: ('pyrowave', False, '420'), 0x20000: ('pyrowave', False, '444'),
           0x40000: ('pyrowave', True, '420'), 0x80000: ('pyrowave', True, '444')}


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
