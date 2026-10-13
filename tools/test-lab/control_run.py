#!/usr/bin/env python3
# ps5-native-app-boilerplate - fixed test control mailbox and matching ACK.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import io
import json
import time
import uuid
from ftplib import FTP, error_perm, Error
from launch_spike import read_receipt, request_bytes


def command(ftp, run_id, action, timeout=10):
    request_bytes(run_id, 'unused')
    if action not in ('start_measure', 'stop', 'cancel', 'clock_probe'):
        raise ValueError('unsupported control action')
    status_path = '/data/prosperolight/test-lab/status/' + run_id + '.json'
    status = read_receipt(ftp, status_path)
    if status.get('schema') != 'prosperolight-test-status/1' or status.get('run_id') != run_id:
        raise ValueError('status identity mismatch')
    try:
        read_receipt(ftp, '/data/prosperolight/test-lab/receipts/' + run_id + '.json')
    except error_perm as exc:
        if not str(exc).startswith('550'):
            raise
    else:
        raise RuntimeError('run already terminal; refusing command')
    root = '/data/prosperolight/test-lab/control'
    try:
        ftp.mkd(root)
    except error_perm:
        ftp.cwd(root)
    path = root + '/' + run_id + '.json'
    try:
        prior = read_receipt(ftp, path)
    except error_perm as exc:
        if not str(exc).startswith('550'):
            raise
    else:
        if prior.get('command_id') != status.get('command_id'):
            raise RuntimeError('unacknowledged command exists; refusing overwrite')
    identifier = str(uuid.uuid4())
    payload = {'schema': 'prosperolight-test-control/1', 'run_id': run_id,
               'command_id': identifier, 'action': action}
    data = json.dumps(payload).encode()
    ftp.storbinary('STOR ' + path + '.tmp', io.BytesIO(data))
    if read_receipt(ftp, path + '.tmp') != payload:
        raise RuntimeError('control readback mismatch')
    send_begin = time.monotonic_ns() // 1000
    ftp.rename(path + '.tmp', path)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        status = read_receipt(ftp, status_path)
        if status.get('run_id') != run_id or status.get('schema') != 'prosperolight-test-status/1':
            raise ValueError('ACK identity mismatch')
        if status.get('command_id') == identifier and status.get('command_action') == action:
            status['coordinator_ack_bounds'] = {'begin_us':send_begin, 'end_us':time.monotonic_ns()//1000,
                'source':'coordinator monotonic; mailbox rename through matching ACK read'}
            return status
        time.sleep(.2)
    raise TimeoutError('control ACK absent; do not escalate to process kill')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--ftp-port', type=int, default=2121)
    parser.add_argument('--run-id', required=True)
    parser.add_argument('--action', choices=('start_measure', 'stop', 'cancel', 'clock_probe'), required=True)
    args = parser.parse_args()
    with FTP() as ftp:
        ftp.connect(args.host, args.ftp_port, timeout=5)
        ftp.login('anonymous', 'codex')
        status = command(ftp, args.run_id, args.action)
    print(json.dumps(status))
    return 0 if status.get('command_result') == 'accepted' else 1


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, Error) as exc:
        print(json.dumps({'state': 'INCONCLUSIVE', 'reason': str(exc)}))
        raise SystemExit(3)
