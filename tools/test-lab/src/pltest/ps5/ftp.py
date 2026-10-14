# ps5-native-app-boilerplate - bounded FTP diagnostics and atomic uploads.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import io
import re
from ftplib import error_perm
from pathlib import PurePosixPath

LOG_ROOT = '/data/prosperolight/logs'
MAX_FILE = 64 * 1024 * 1024
MAX_ARCHIVE = 256 * 1024 * 1024


def diagnostic_name(name):
    return name in ('performance-last.json', 'debug-trace.txt', 'crash-last.txt',
                    'videoout-policy.log') or bool(re.fullmatch(
                        r'(performance-frames|pyrowave-output|(?:native|pyrowave)-(?:scanout|decisions))-mode[012]\.csv', name))


def retrieve(ftp, path, limit=MAX_FILE):
    result = bytearray()
    def append(data):
        if len(result) + len(data) > limit:
            raise ValueError('remote file exceeds byte limit')
        result.extend(data)
    ftp.retrbinary('RETR ' + path, append)
    return bytes(result)


def upload(ftp, path, data):
    if len(data) > MAX_FILE:
        raise ValueError('upload exceeds byte limit')
    ftp.storbinary('STOR ' + path + '.tmp', io.BytesIO(data))
    if retrieve(ftp, path + '.tmp', len(data)) != data:
        raise RuntimeError('upload readback mismatch')
    ftp.rename(path + '.tmp', path)


def archive_logs(ftp, destination):
    destination.mkdir(parents=True, exist_ok=False)
    index, total = {}, 0
    try:
        names = ftp.nlst(LOG_ROOT)
    except error_perm as exc:
        if str(exc).startswith('550'):
            return index
        raise
    for name in sorted({PurePosixPath(item).name for item in names}):
        if not diagnostic_name(name):
            continue
        data = retrieve(ftp, LOG_ROOT + '/' + name)
        total += len(data)
        if total > MAX_ARCHIVE:
            raise ValueError('diagnostic archive exceeds total byte limit')
        (destination / name).write_bytes(data)
        index[name] = {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
    return index


def installation_inventory(ftp, root, expected, readback=True):
    """MLSD is required: unknown files/links fail closed; never delete them."""
    actual, directories = {}, set()
    allowed_dirs = {str(parent) for name in expected for parent in PurePosixPath(name).parents if str(parent) != '.'}
    pending = [('', root)]
    total = 0
    while pending:
        prefix, path = pending.pop()
        try:
            entries = list(ftp.mlsd(path))
        except Exception as exc:
            raise RuntimeError('INSTALL_INVENTORY_UNAVAILABLE: require MLSD for ' + path) from exc
        if len(entries) > 1024:
            raise RuntimeError('INSTALL_INVENTORY_LIMIT')
        for name, facts in entries:
            kind = facts.get('type')
            if kind in ('cdir', 'pdir') and name in ('.', '..'):
                continue
            if not name or name in ('.', '..') or '/' in name or '\\' in name or any(ord(c)<32 for c in name):
                raise RuntimeError('INSTALL_UNSAFE_ENTRY')
            relative = prefix + name
            if kind == 'dir':
                if relative not in allowed_dirs or relative in directories:
                    raise RuntimeError('INSTALL_UNEXPECTED_DIRECTORY: inspect manually: ' + relative)
                directories.add(relative)
                pending.append((relative+'/', root+'/'+relative))
            elif kind == 'file':
                if relative not in expected or relative in actual:
                    raise RuntimeError('INSTALL_UNEXPECTED_FILE: inspect manually; no deletion attempted: ' + relative)
                actual[relative] = None
            else:
                raise RuntimeError('INSTALL_UNSUPPORTED_ENTRY: inspect manually: ' + relative)
            if len(actual)+len(directories)>1024:
                raise RuntimeError('INSTALL_INVENTORY_LIMIT')
    if readback:
        if set(actual) != set(expected):
            raise RuntimeError('INSTALL_FILE_SET_MISMATCH')
        for name in sorted(actual):
            data = retrieve(ftp, root+'/'+name, expected[name]['bytes'])
            total += len(data)
            if total > MAX_ARCHIVE:
                raise RuntimeError('INSTALL_INVENTORY_BYTE_LIMIT')
            item = {'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
            if item != expected[name]:
                raise RuntimeError('INSTALL_FINAL_MANIFEST_MISMATCH: '+name)
            actual[name] = item
    return actual
