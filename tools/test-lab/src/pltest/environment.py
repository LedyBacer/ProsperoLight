# ps5-native-app-boilerplate - observed comparison environment and explicit operator topology.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import re


def environment(profile, report, analysis):
    probe=report.get('ps5_preflight') or {}
    host=report.get('host_measurement') or {}
    advertised=(report.get('host_session_preflight') or {}).get('advertised') or {}
    variant=(report.get('ps5_measurement') or {}).get('build_variant') or {}
    supplied=profile.get('analysis_environment') or {}
    settings=analysis.get('session_settings') or {}
    missing=[]
    firmware=probe.get('firmware_u32')
    if probe.get('firmware_verified') is not True or probe.get('firmware_source')!='kernel_get_fw_version' or type(firmware) is not int or firmware<=0:
        firmware=None;missing.append('firmware')
    version=advertised.get('version');commit=advertised.get('commit')
    if advertised.get('verified') is not True or not isinstance(version,str) or not isinstance(commit,str) or not re.fullmatch(r'[0-9a-f]{40}',commit):
        host_version=None;missing.append('host_version')
    else: host_version=version+'@'+commit
    encoder={}
    for key in ('codec','hdr','chroma','host_encoder_bitrate_kbps','host_requested_bitrate_kbps'):
        field=settings.get(key) or {}
        if field.get('verified') is not True:missing.append('encoder_settings.'+key)
        else:encoder[key]=field.get('value')
    capture=host.get('capture_evidence') or {}
    if capture.get('verified') is not True or not capture.get('encoders'):
        missing.append('encoder_settings.implementation')
    encoder['implementation']=capture.get('encoders')
    network=supplied.get('network_class') if supplied.get('network_source')=='operator-confirmed' else None
    if not isinstance(network,str) or not network:network=None;missing.append('network_class')
    flags=variant.get('build_flags')
    if not isinstance(flags,dict) or flags.get('TEST_LAB')!=1 or flags.get('PERFORMANCE_DETAIL')!=1:
        flags=None;missing.append('build_flags')
    codec=(settings.get('codec') or {}).get('value')
    renderer='PyroWave/Vulkan' if codec=='pyrowave' else 'VideoDec2/native' if codec in ('hevc','h264') else None
    if renderer is None:missing.append('renderer')
    return {'verified':not missing,'missing':missing,'firmware':hex(firmware) if firmware else None,
            'host_tls_peer_sha256':advertised.get('tls_peer_sha256'),
            'host_version':host_version,'encoder_settings':encoder,'network_class':network,
            'build_flags':flags,'renderer':renderer,
            'sources':{'firmware':'PS5 preflight kernel_get_fw_version',
                       'host_version':'authenticated Vibepollo metadata',
                       'encoder_settings':'authenticated RTSP settings and current-session capture log',
                       'network_class':'operator-confirmed; not inferred from throughput',
                       'build_flags':'validated build provenance',
                       'renderer':'successful decoder setup evidence'}}
