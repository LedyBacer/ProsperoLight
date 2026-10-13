# ps5-native-app-boilerplate - decoder callback evidence validation.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

# Limelight.h and the checked-in PyroWave protocol patch define these identities.
FORMATS = {1: ('h264', False, '420'), 4: ('h264', False, '444'),
           0x100: ('hevc', False, '420'), 0x200: ('hevc', True, '420'),
           0x400: ('hevc', False, '444'), 0x800: ('hevc', True, '444'),
           0x10000: ('pyrowave', False, '420'), 0x20000: ('pyrowave', False, '444'),
           0x40000: ('pyrowave', True, '420'), 0x80000: ('pyrowave', True, '444')}


def validate_negotiation(client, receipt):
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
    for key in ('video_format', 'codec', 'hdr', 'chroma', 'width', 'height', 'fps'):
        verified = key in actual
        fields[key] = dict(value=actual.get(key), verified=verified,
                           source=evidence['source'] if verified else None,
                           category='negotiated', reason=None if verified else 'successful decoder setup evidence missing')
        if not verified:
            issues.append('NEGOTIATED_FIELD_UNVERIFIED:' + key)
        elif key in expected and actual[key] != expected[key]:
            issues.append('NEGOTIATED_FIELD_MISMATCH:' + key)
    if 'hdr' in actual:
        fields['bit_depth'] = dict(value=10 if actual['hdr'] else 8, verified=True,
            source=evidence['source'] + '.video_format', category='negotiated', reason=None)
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
