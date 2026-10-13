# ps5-native-app-boilerplate - PR3 agent-facing portable executor CLI.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import json
import sys
import uuid
from ftplib import Error
from pathlib import Path
from .build import capabilities
from .ps5.adapter import Ps5Adapter
from .ps5.collector import collect_reconnecting
from control_run import command


def main():
    args = sys.argv[1:]
    if args in (['--help'], ['-h']) or (args and (args[0] in {'coordinator', 'status', 'report', 'artifacts', 'compare', 'matrix', 'paired', 'prepare-variant', 'build-variant', 'benchmark-plan', 'sweep', 'export'}
                 or (args[0] in {'run', 'doctor', 'stop'} and '--ps5' not in args))):
        from .coordinator.cli import main as coordinator_cli
        return coordinator_cli(args)
    if len(sys.argv) > 1 and sys.argv[1] == 'worker':
        from .host.worker import main as worker_main
        return worker_main(args[1:])
    parser = argparse.ArgumentParser(description='Developer-only PS5 executor and Windows host worker')
    sub = parser.add_subparsers(dest='operation', required=True)
    for name in ('doctor', 'run', 'collect', 'stop', 'release-lease'):
        p = sub.add_parser(name)
        p.add_argument('--ps5', required=True)
        p.add_argument('--probe-elf', type=Path, required=True)
        p.add_argument('--launch-elf', type=Path, required=True)
        p.add_argument('--ftp-port', type=int, default=2121)
        p.add_argument('--manager-port', type=int, default=8084)
        if name == 'run':
            p.add_argument('--profile', type=Path, required=True)
            p.add_argument('--eboot-sha256', required=True)
            p.add_argument('--output', type=Path, required=True)
            p.add_argument('--deploy', type=Path)
        if name in ('collect', 'stop', 'release-lease'):
            p.add_argument('--run-id', required=True)
        if name == 'release-lease':
            p.add_argument('--owner', required=True)
        if name == 'collect':
            p.add_argument('--output', type=Path, required=True)
            p.add_argument('--timeout', type=int, default=150)
    worker = sub.add_parser('worker')
    worker.add_argument('args', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    try:
        if args.operation == 'worker':
            from .host.worker import main as worker_main
            return worker_main(args.args)
        adapter = Ps5Adapter(args.ps5, args.probe_elf, args.launch_elf,
                             ftp_port=args.ftp_port, manager_port=args.manager_port)
        if args.operation == 'doctor':
            with adapter.lease(str(uuid.uuid4())) as ownership:
                idle = adapter.idle()
                ownership['safe_to_release'] = True
                result = {'capabilities': capabilities(), 'preflight': idle, 'installed': adapter.installed()}
        elif args.operation == 'run':
            result = adapter.run(json.loads(args.profile.read_text()), args.output,
                                 args.eboot_sha256, args.deploy)
        elif args.operation == 'collect':
            if not 1 <= args.timeout <= 1900:
                raise ValueError('invalid collection timeout')
            result = collect_reconnecting(adapter.connect, args.run_id, args.output, args.timeout)
        elif args.operation == 'release-lease':
            result = adapter.release_lease(args.run_id, args.owner)
        else:
            with adapter.connect() as ftp:
                result = command(ftp, args.run_id, 'stop')
        print(json.dumps(result))
        verdict = result.get('functional_verdict')
        return {'PASS': 0, 'FAIL': 1, 'CANCELLED': 4, 'INCONCLUSIVE': 3}.get(verdict, 0)
    except (OSError, ValueError, RuntimeError, Error) as exc:
        print(json.dumps({'functional_verdict': 'INCONCLUSIVE', 'stage': args.operation,
                          'reason_code': type(exc).__name__, 'reason': str(exc)}))
        return 3 if isinstance(exc, TimeoutError) else 2


if __name__ == '__main__':
    sys.exit(main())
