#!/usr/bin/env python3
# ps5-native-app-boilerplate - Compressed package integrity check.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the exact inner exFAT bytes after a full PFSC decompression."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(4 * 1024 * 1024), b''):
            result.update(chunk)
    return result.hexdigest()


def main():
    runner, image, inner = sys.argv[1:]
    inner = Path(inner)
    with tempfile.TemporaryDirectory(prefix='prosperolight-pfsc-') as folder:
        output = Path(folder) / 'unpacked'
        subprocess.run([runner, 'unpack', image, str(output), '--no-progress'], check=True)
        files = sorted(p for p in output.rglob('*') if p.is_file())
        if len(files) != 1 or files[0].name != inner.name:
            raise SystemExit('PFSC must contain exactly the named inner exFAT volume')
        if files[0].stat().st_size != inner.stat().st_size or digest(files[0]) != digest(inner):
            raise SystemExit('PFSC decompression changed the verified inner exFAT bytes')
        print('PFSC inner exFAT SHA-256 verified:', digest(inner))


if __name__ == '__main__':
    main()
