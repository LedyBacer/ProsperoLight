#!/usr/bin/env python3
# ps5-native-app-boilerplate - PR2 bounded final-receipt collector.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""Read a completed test run. Does not deploy, launch, stop or kill a title."""
import argparse
import json
import re
import time
from ftplib import FTP, error_perm, Error
from pathlib import Path
from urllib.request import urlopen
from launch_spike import read_receipt, request_bytes


def artifact_name(name):
    return name in ('performance-last.json', 'debug-trace.txt') or bool(
        re.fullmatch(r'(?:performance-frames|pyrowave-output)-mode[012]\.csv', name))


def collect(ftp, run_id, output, timeout):
    request_bytes(run_id, 'unused')
    deadline = time.monotonic() + timeout
    while True:
        try:
            receipt = read_receipt(ftp, '/data/prosperolight/test-lab/receipts/' + run_id + '.json')
        except error_perm as exc:
            if not str(exc).startswith('550'):
                raise
            if time.monotonic() >= deadline:
                raise TimeoutError('final receipt absent; run is incomplete; do not relaunch automatically')
            time.sleep(2)
        else:
            break
    if receipt.get('schema') not in ('prosperolight-test-receipt/1', 'prosperolight-test-receipt/2') or receipt.get('run_id') != run_id:
        raise ValueError('terminal receipt identity mismatch')
    if not isinstance(receipt.get('artifacts', []), list) or not all(
            isinstance(item, dict) for item in receipt.get('artifacts', [])) or (
            'trace' in receipt and not isinstance(receipt['trace'], dict)):
        raise ValueError('invalid artifact list')
    output.mkdir(parents=True, exist_ok=False)
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    artifacts = receipt.get('artifacts', []) + ([receipt['trace']] if 'trace' in receipt else [])
    for artifact in artifacts:
        if receipt.get('artifacts_complete') is not True and artifact.get('fresh') is not True:
            continue
        name, expected = artifact.get('name'), artifact.get('bytes')
        if not isinstance(name, str) or not artifact_name(name) or not isinstance(expected, int) or not 0 < expected <= 64 * 1024 * 1024:
            if receipt.get('artifacts_complete') is not True:
                continue
            raise ValueError('invalid artifact descriptor')
        temporary = output / (name + '.tmp')
        size = 0
        with temporary.open('wb') as destination:
            def append(data):
                nonlocal size
                size += len(data)
                if size > expected:
                    raise ValueError('artifact grew after completion')
                destination.write(data)
            ftp.retrbinary('RETR /data/prosperolight/logs/' + name, append)
        if size != expected:
            raise ValueError('artifact byte count mismatch')
        temporary.rename(output / name)
    return receipt


def title_exit(host, port, pid, timeout=15):
    if not isinstance(pid, int) or pid <= 0:
        return False
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        with urlopen(f'http://{host}:{port}/processes_list', timeout=5) as response:
            data = response.read(1024 * 1024 + 1)
            if len(data) > 1024 * 1024:
                raise ValueError('process inventory exceeds limit')
            inventory = json.loads(data)
        if not any(process.get('pid') == pid for process in inventory['processes']):
            return True
        time.sleep(1)
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--run-id', required=True)
    parser.add_argument('--output', type=Path, required=True, help='new per-run directory')
    parser.add_argument('--timeout', type=int, default=150)
    parser.add_argument('--ftp-port', type=int, default=2121)
    parser.add_argument('--payload-manager-port', type=int, default=8084)
    args = parser.parse_args()
    if not 1 <= args.timeout <= 1900:
        raise ValueError('timeout must be 1–1900 seconds')
    with FTP() as ftp:
        ftp.connect(args.host, args.ftp_port, timeout=5)
        ftp.login('anonymous', 'codex')
        receipt = collect(ftp, args.run_id, args.output, args.timeout)
    exited = title_exit(args.host, args.payload_manager_port, receipt.get('pid'))
    summary = {'receipt': receipt, 'title_exit_verified': exited,
               'functional_verdict': receipt.get('functional_verdict') if exited else 'INCONCLUSIVE',
               'measurement_validity': receipt.get('measurement_validity', 'INCONCLUSIVE')}
    (args.output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary))
    return 0 if summary['functional_verdict'] == 'PASS' else 3


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, Error) as exc:
        print(json.dumps({'functional_verdict': 'INCONCLUSIVE', 'reason': str(exc)}))
        raise SystemExit(3)
