# ps5-native-app-boilerplate - distinguish build capability from PS5 LAN access.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import platform
import os
import json
import re
from pathlib import Path


def capabilities():
    system = platform.system()
    sdk = os.environ.get('PS5_PAYLOAD_SDK')
    native = system == 'Linux' and bool(sdk and (Path(sdk) / 'bin/prospero-clang').is_file())
    return {'platform': system, 'ps5_executor': True,
            'native_ps5_build': native,
            'native_build_reason': None if native else 'requires Linux and configured installed PS5 Payload SDK',
            'prebuilt': True, 'external': 'operator-provided build; no implicit SSH credentials',
            'wsl': system == 'Windows'}


def prebuilt(directory):
    directory = Path(directory)
    label = (directory / 'build-label.txt').read_text(encoding='ascii').strip()
    if not label.startswith('test-lab-'):
        raise ValueError('expected explicitly marked developer build')
    return {'directory': str(directory.resolve()), 'build_label': label,
            'eboot_sha256': hashlib.sha256((directory / 'eboot.bin').read_bytes()).hexdigest()}


def validate_variant(value, root):
    """Validate a prepared build under an explicitly configured deployment root."""
    if not isinstance(value, dict) or value.get('schema') != 'prosperolight-build-variant/1':
        raise ValueError('BUILD_MANIFEST_REQUIRED')
    if not re.fullmatch(r'[0-9a-f]{40}', value.get('source_sha', '')):
        raise ValueError('BUILD_SOURCE_SHA_REQUIRED')
    if not re.fullmatch(r'[0-9a-f]{64}', value.get('eboot_sha256', '')):
        raise ValueError('BUILD_HASH_REQUIRED')
    flags = value.get('build_flags')
    if not isinstance(flags, dict) or flags.get('TEST_LAB') != 1 or flags.get('PERFORMANCE_DETAIL') != 1:
        raise ValueError('DEVELOPER_DETAILED_BUILD_REQUIRED')
    if root is None:
        raise ValueError('BUILD_ROOT_NOT_CONFIGURED: set PLTEST_BUILD_ROOT to the prepared artifact root')
    root = Path(root).resolve(strict=True)
    raw = root / value.get('directory', '')
    if any(path.is_symlink() for path in (raw, *raw.parents) if path != root):
        raise ValueError('BUILD_SYMLINK_REFUSED')
    directory = raw.resolve(strict=True)
    if root not in directory.parents or not directory.is_dir():
        raise ValueError('BUILD_OUTSIDE_CONFIGURED_ROOT')
    actual = prebuilt(directory)
    if any(actual[key] != value.get(key) for key in ('build_label', 'eboot_sha256')):
        raise ValueError('BUILD_IDENTITY_MISMATCH')
    paths = sorted(directory.rglob('*'))
    if any(path.is_symlink() for path in paths):
        raise ValueError('BUILD_SYMLINK_REFUSED')
    files = [path for path in paths if path.is_file()]
    if not files or len(files) > 512 or sum(path.stat().st_size for path in files) > 512 * 1024 * 1024:
        raise ValueError('BUILD_SIZE_LIMIT')
    index = {path.relative_to(directory).as_posix(): {
        'bytes': path.stat().st_size, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
        for path in files}
    if value.get('files') != index:
        raise ValueError('BUILD_FILE_MANIFEST_MISMATCH')
    provenance_path = root / value.get('provenance', '')
    if any(path.is_symlink() for path in (provenance_path, *provenance_path.parents) if path != root):
        raise ValueError('BUILD_SYMLINK_REFUSED')
    provenance_path = provenance_path.resolve(strict=True)
    if root not in provenance_path.parents or not provenance_path.is_file() or provenance_path.stat().st_size > 256 * 1024:
        raise ValueError('BUILD_PROVENANCE_REQUIRED')
    provenance_bytes = provenance_path.read_bytes()
    if value.get('provenance_sha256') != hashlib.sha256(provenance_bytes).hexdigest():
        raise ValueError('BUILD_PROVENANCE_HASH_MISMATCH')
    provenance = json.loads(provenance_bytes)
    digest = value.get('source_tree_sha256')
    if not isinstance(digest, str) or not re.fullmatch(r'[0-9a-f]{64}', digest) or digest != provenance.get('source_sha256'):
        raise ValueError('BUILD_SOURCE_TREE_DIGEST_MISMATCH')
    if provenance.get('commit') != value['source_sha'] or provenance.get('eboot_sha256') != value['eboot_sha256']:
        raise ValueError('BUILD_PROVENANCE_IDENTITY_MISMATCH')
    if definition_flags(provenance) != flags:
        raise ValueError('BUILD_PROVENANCE_FLAGS_MISMATCH')
    return {**value, 'directory': str(directory), 'files': index}


def definition_flags(provenance):
    result = {}
    for definition in provenance.get('definitions', []):
        name, separator, value = definition.partition('=')
        if not separator or name in result:
            raise ValueError('BUILD_DEFINITIONS_INVALID')
        name = name.removeprefix('PROSPEROLIGHT_')
        try:
            value = int(value, 0)
        except ValueError:
            pass
        if name in result:
            raise ValueError('BUILD_DEFINITIONS_INVALID')
        result[name] = value
    return result


def load_variant(reference, root):
    if root is None:
        raise ValueError('BUILD_ROOT_NOT_CONFIGURED')
    root = Path(root).resolve(strict=True)
    if not isinstance(reference, dict) or set(reference) != {'manifest', 'sha256'}:
        raise ValueError('BUILD_MANIFEST_REFERENCE_REQUIRED')
    raw = root / reference['manifest']
    if any(path.is_symlink() for path in (raw, *raw.parents) if path != root):
        raise ValueError('BUILD_SYMLINK_REFUSED')
    path = raw.resolve(strict=True)
    if root not in path.parents or not path.is_file() or path.stat().st_size > 256 * 1024:
        raise ValueError('BUILD_MANIFEST_PATH_REFUSED')
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != reference['sha256']:
        raise ValueError('BUILD_MANIFEST_HASH_MISMATCH')
    return validate_variant(json.loads(data), root)
