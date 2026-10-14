#!/usr/bin/env python3
# ps5-native-app-boilerplate - PR0 portable ELF sender and fixed mailbox.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""Development spike. Transport completion is never proof of application launch."""
import argparse
import io
import hashlib
import sys
import json
import re
import socket
import time
import uuid
from urllib.request import urlopen
from urllib.parse import quote
from ftplib import FTP, error_perm
from pathlib import Path

# Keep direct source-tree execution working without an editable installation.
_source = Path(__file__).resolve().parent / 'src'
if _source.is_dir() and str(_source) not in sys.path:
    sys.path.insert(0, str(_source))
from pltest.ps5.ftp import directory_entries

ROOT = '/data/prosperolight/test-lab/pr0'


def request_bytes(request_id, pc):
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,127}', request_id):
        raise ValueError('unsafe request ID')
    if not pc or len(pc) > 127 or any(ord(c) < 32 or ord(c) > 126 or c in ('"', '\\') for c in pc):
        raise ValueError('PC must be a saved host name/address, 1–127 printable ASCII bytes')
    return request_id.encode().ljust(128, b'\0') + pc.encode().ljust(128, b'\0')


def elf_bytes(path):
    data = Path(path).read_bytes()
    if len(data) < 64 or len(data) > 16 * 1024 * 1024 or data[:6] != b'\x7fELF\x02\x01':
        raise ValueError('expected bounded little-endian ELF64')
    if int.from_bytes(data[18:20], 'little') != 62:
        raise ValueError('expected x86-64 ELF')
    return data


def send_elf(host, port, data):
    # SDK prospero-deploy sends raw ELF over TCP (no HTTP/length prefix).
    with socket.create_connection((host, port), timeout=5) as connection:
        connection.settimeout(10)
        connection.sendall(data)
        connection.shutdown(socket.SHUT_WR)


def stage(ftp, data):
    # Only fixed allowlisted paths. Existing request refuses concurrent use.
    for directory in ('/data/prosperolight/test-lab', ROOT):
        try:
            ftp.mkd(directory)
        except error_perm:
            ftp.cwd(directory)  # verify existing directory; do not swallow failure
    if 'request.bin' in [name for name,facts in directory_entries(ftp,ROOT)]:
        raise RuntimeError('unconsumed PR0 request exists; refusing overwrite')
    ftp.storbinary('STOR ' + ROOT + '/request.tmp', io.BytesIO(data))
    check = bytearray()
    ftp.retrbinary('RETR ' + ROOT + '/request.tmp', check.extend)
    if bytes(check) != data:
        raise RuntimeError('mailbox readback mismatch')
    ftp.rename(ROOT + '/request.tmp', ROOT + '/request.bin')


def read_receipt(ftp, path, limit=8192):
    data = bytearray()
    def append(chunk):
        if len(data) + len(chunk) > limit:
            raise ValueError('receipt exceeds byte limit')
        data.extend(chunk)
    ftp.retrbinary('RETR ' + path, append)
    result = json.loads(data)
    if not isinstance(result, dict):
        raise ValueError('receipt must be a JSON object')
    return result


def stage_profile(ftp, profile, request_id):
    root = '/data/prosperolight/test-lab/inbox'
    data = json.dumps(profile, separators=(',', ':'), ensure_ascii=True).encode()
    if len(data) > 16384 or profile.get('run_id') != request_id:
        raise ValueError('invalid profile size or run identity')
    request_bytes(request_id, profile['client']['host'])
    for directory in ('/data/prosperolight/test-lab', root):
        try:
            ftp.mkd(directory)
        except error_perm:
            ftp.cwd(directory)
    name = request_id + '.json'
    if name in [item for item,facts in directory_entries(ftp,root)]:
        raise RuntimeError('profile already exists; refusing replay')
    temporary = root + '/' + request_id + '.tmp'
    ftp.storbinary('STOR ' + temporary, io.BytesIO(data))
    if read_receipt(ftp, temporary, limit=16384) != profile:
        raise RuntimeError('profile readback mismatch')
    ftp.rename(temporary, root + '/' + name)


def application_proof(ftp, request_id, pc, timeout=45):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            proof = read_receipt(ftp, ROOT + '/' + request_id + '-app.json')
        except error_perm as exc:
            if not str(exc).startswith('550'):
                raise
        else:
            if (proof.get('schema') != 'prosperolight-launch-proof/1' or
                proof.get('request_id') != request_id or proof.get('pc') != pc or
                proof.get('app') != 'Desktop' or proof.get('exit_after_stream') is not True or
                proof.get('argv_verified') is not True or
                not isinstance(proof.get('pid'), int) or proof['pid'] <= 0):
                raise RuntimeError('application argv receipt mismatch')
            return proof
        time.sleep(.2)
    return None


def profile_status(ftp, request_id, timeout=40):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            status = read_receipt(ftp, '/data/prosperolight/test-lab/status/' + request_id + '.json')
        except error_perm as exc:
            if not str(exc).startswith('550'):
                raise
        else:
            if status.get('schema') != 'prosperolight-test-status/1' or status.get('run_id') != request_id:
                raise RuntimeError('profile status identity mismatch')
            if status.get('state') in ('launching', 'failed', 'connecting', 'waiting_frame',
                                     'stream_ready', 'warmup', 'measuring', 'cooldown', 'stopping'):
                return status
        time.sleep(.2)
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--port', type=int, default=9021)
    parser.add_argument('--payload-manager-port', type=int, help='PS5-local loader bridge, e.g. 8084')
    parser.add_argument('--ftp-port', type=int, default=2121)
    parser.add_argument('--pc', required=True)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--output', type=Path, help='local evidence directory (receipt and build manifest)')
    parser.add_argument('--execute', action='store_true', help='stage request and execute helper')
    parser.add_argument('--test-profile', type=Path, help='PR1 profile; requires helper compiled with PR0_ROUTE=4')
    args = parser.parse_args()
    data = elf_bytes(args.elf)
    request_id = str(uuid.uuid4())
    profile = None
    if args.test_profile:
        raw = args.test_profile.read_bytes()
        if len(raw) > 16384:
            raise ValueError('profile exceeds byte limit')
        profile = json.loads(raw)
        if not isinstance(profile, dict) or profile.get('schema') != 'prosperolight-test/1':
            raise ValueError('unsupported profile schema')
        if not isinstance(profile.get('client'), dict) or not isinstance(profile['client'].get('host'), str):
            raise ValueError('profile requires client.host')
        profile['run_id'] = request_id
        if profile['client']['host'] != args.pc:
            raise ValueError('profile host must match --pc')
    request = request_bytes(request_id, args.pc)
    if not args.execute:
        print(json.dumps({'request_id': request_id, 'title': 'PPSA99002', 'dry_run': True,
                          'elf_sha256': hashlib.sha256(data).hexdigest()}))
        return 0
    # Check loader before touching the mailbox; no deployment of production app.
    endpoint_port = args.payload_manager_port or args.port
    with socket.create_connection((args.host, endpoint_port), timeout=5):
        pass
    with FTP() as ftp:
        ftp.connect(args.host, args.ftp_port, timeout=5)
        ftp.login('anonymous', 'codex')
        if profile is not None:
            stage_profile(ftp, profile, request_id)
        stage(ftp, request)
        if args.payload_manager_port:
            manager_root = '/data/pldmgr/payloads/prosperolight-test-launcher-pr0'
            try:
                ftp.mkd(manager_root)
            except error_perm:
                ftp.cwd(manager_root)
            remote = manager_root + '/prosperolight-test-launcher-pr0.elf'
            ftp.storbinary('STOR ' + remote + '.tmp', io.BytesIO(data))
            check = bytearray()
            ftp.retrbinary('RETR ' + remote + '.tmp', check.extend)
            if bytes(check) != data:
                raise RuntimeError('ELF readback mismatch')
            ftp.rename(remote + '.tmp', remote)
            # Verified manager source bridges this fixed file to local elfldr.
            url = f'http://{args.host}:{args.payload_manager_port}/loadpayload:' + quote(remote, safe='/')
            with urlopen(url, timeout=10) as response:
                if response.status != 200:
                    raise RuntimeError('payload manager refused ELF')
        else:
            send_elf(args.host, args.port, data)
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            try:
                result = read_receipt(ftp, ROOT + '/' + request_id + '.json')
            except error_perm as exc:
                if not str(exc).startswith('550'):
                    raise
            else:
                if result.get('request_id') != request_id or result.get('schema') != 'prosperolight-launch-spike/1':
                    raise RuntimeError('receipt identity mismatch')
                if result.get('state') != 'preflight':
                    if (profile is not None) != (result.get('route') == 4):
                        raise RuntimeError('helper route does not match requested mode')
                    proof = application_proof(ftp, request_id, args.pc) if result.get('state') == 'LAUNCH_ACCEPTED' and profile is None else None
                    status = profile_status(ftp, request_id) if result.get('state') == 'LAUNCH_ACCEPTED' and profile is not None else None
                    summary = {'launcher_receipt': result, 'application_receipt': proof,
                               'profile_status': status,
                               'test_run_verified': status is not None,
                               'argv_verified': proof is not None, 'functional_verdict': 'FAIL' if status and status.get('state') == 'failed' else 'INCONCLUSIVE'}
                    if args.output:
                        args.output.mkdir(parents=True, exist_ok=True)
                        (args.output / (request_id + '.json')).write_text(json.dumps({
                            **summary, 'elf_sha256': hashlib.sha256(data).hexdigest(),
                            'transport': 'payload-manager' if args.payload_manager_port else 'raw-elfldr',
                            'profile': profile,
                        }, indent=2) + '\n')
                    print(json.dumps(summary))
                    return 1 if summary['functional_verdict'] == 'FAIL' else 3
            time.sleep(.2)
        raise TimeoutError('no final launcher receipt; do not retry launch automatically')


if __name__ == '__main__':
    try:
        code = main()
    except (OSError, ValueError, RuntimeError) as exc:
        print(json.dumps({'state': 'INFRASTRUCTURE_ERROR', 'reason': str(exc),
                          'argv_verified': False}), file=sys.stderr)
        code = 2
    raise SystemExit(code)
