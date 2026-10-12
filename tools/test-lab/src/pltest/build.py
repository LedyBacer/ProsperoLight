# ps5-native-app-boilerplate - distinguish build capability from PS5 LAN access.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import platform
import os
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
