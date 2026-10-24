"""Linux gateway egress impairment for paired PS5 network experiments.

No persistent qdisc is left behind by a normal run. We never replace an
existing custom qdisc, and cleanup verifies ownership before deleting one.
"""
import argparse
import datetime as dt
import json
import os
import platform
import re
import signal
import subprocess
import sys
from pathlib import Path


# Values represent *one-way egress* impairment, not round-trip RTT.
SCENARIOS = {
    'lan': None,
    'wifi-light': {'delay_ms': 5, 'jitter_ms': 3, 'loss_percent': 0.1},
    'wifi-congested': {'delay_ms': 20, 'jitter_ms': 15, 'loss_percent': 1.0,
                       'loss_correlation_percent': 20, 'reorder_percent': 0.2},
    'wan-50': {'delay_ms': 25, 'jitter_ms': 10, 'loss_percent': 0.3},
    'wan-100': {'delay_ms': 50, 'jitter_ms': 25, 'loss_percent': 1.0},
    'wan-burst': {'delay_ms': 40, 'jitter_ms': 30, 'loss_percent': 2.0,
                  'loss_correlation_percent': 25, 'reorder_percent': 1.0},
}
HANDLE = '51:'
DEFAULT_ROOTS = {'fq_codel', 'pfifo_fast', 'noqueue', 'fq'}


def tc_args(scenario):
    value = SCENARIOS[scenario]
    if value is None:
        return []
    args = ['delay', f"{value['delay_ms']}ms", f"{value['jitter_ms']}ms", 'distribution', 'normal',
            'loss', f"{value['loss_percent']}%"]
    if value.get('loss_correlation_percent'):
        args.append(f"{value['loss_correlation_percent']}%")
    if value.get('reorder_percent'):
        args.extend(['reorder', f"{value['reorder_percent']}%"])
    return args


def execute(args, *, check=True):
    return subprocess.run(args, text=True, capture_output=True, check=check)


def qdiscs(interface):
    return json.loads(execute(['tc', '-j', 'qdisc', 'show', 'dev', interface]).stdout)


def root_qdisc(rows):
    return next((q for q in rows if q.get('root') is True), None)


def validate_interface(interface):
    if not re.fullmatch(r'[A-Za-z0-9_.-]{1,15}', interface) or interface in ('lo', 'all'):
        raise ValueError('invalid or unsafe network interface')
    routes = json.loads(execute(['ip', '-j', 'route', 'show', 'default']).stdout)
    if any(route.get('dev') == interface for route in routes):
        raise ValueError('refusing to shape the default-route interface; use a dedicated PS5 egress')
    root = root_qdisc(qdiscs(interface))
    if root and root.get('kind') not in DEFAULT_ROOTS:
        raise ValueError('refusing to replace a nondefault root qdisc')
    if root and root.get('kind') == 'netem':
        raise ValueError('an existing netem qdisc is not ours to replace')
    return root


def utc_now():
    return dt.datetime.now(dt.timezone.utc).isoformat()


def write_evidence(path, record):
    if path is None:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(record, indent=2, sort_keys=True) + '\n', encoding='utf-8')


def run(interface, scenario, command, evidence_path=None):
    if platform.system() != 'Linux':
        raise RuntimeError('tc netem requires a Linux gateway/host with CAP_NET_ADMIN')
    if not command:
        raise ValueError('a test command after -- is required')
    original = validate_interface(interface) if scenario != 'lan' else None
    record = {'schema': 'prosperolight-netem/1', 'scenario': scenario,
              'one_way_egress': SCENARIOS[scenario], 'interface': interface,
              'start_utc': utc_now(), 'baseline_qdisc': original,
              'command': command, 'applied': False, 'cleanup_verified': scenario == 'lan'}
    installed = False
    code = 3
    previous_term = signal.getsignal(signal.SIGTERM)

    def interrupt(_signum, _frame):
        raise KeyboardInterrupt('netem wrapper interrupted')

    try:
        signal.signal(signal.SIGTERM, interrupt)
        if scenario != 'lan':
            execute(['tc', 'qdisc', 'add', 'dev', interface, 'root', 'handle', HANDLE, 'netem',
                     *tc_args(scenario)])
            installed = True
            active = root_qdisc(qdiscs(interface))
            if not active or active.get('kind') != 'netem' or active.get('handle') != HANDLE:
                raise RuntimeError('netem installation could not be verified')
            record['applied_qdisc'] = active
        record['applied'] = True
        write_evidence(evidence_path, record)
        with subprocess.Popen(command) as child:
            try:
                code = child.wait()
            except BaseException:
                if child.poll() is None:
                    child.terminate()
                    try:
                        child.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        child.kill()
                        child.wait()
                raise
        record['command_exit_code'] = code
    finally:
        # Once rollback starts, a second TERM must not interrupt qdisc removal.
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        if installed:
            try:
                active = root_qdisc(qdiscs(interface))
                if active and active.get('kind') == 'netem' and active.get('handle') == HANDLE:
                    execute(['tc', 'qdisc', 'del', 'dev', interface, 'root'])
                else:
                    raise RuntimeError('qdisc ownership changed; refusing to delete it')
                record['cleanup_verified'] = root_qdisc(qdiscs(interface)) is None or (
                    root_qdisc(qdiscs(interface)).get('kind') != 'netem')
            except Exception as exc:
                record['cleanup_error'] = str(exc)
                record['cleanup_verified'] = False
        record['end_utc'] = utc_now()
        try:
            write_evidence(evidence_path, record)
        finally:
            signal.signal(signal.SIGTERM, previous_term)
    if not record['cleanup_verified']:
        raise RuntimeError('NETWORK_QDISC_CLEANUP_UNVERIFIED; inspect interface manually')
    return code


def main(argv=None):
    parser = argparse.ArgumentParser(description='Bounded Linux netem run for Test Lab')
    parser.add_argument('--interface', required=True, help='Dedicated egress toward the PS5')
    parser.add_argument('--scenario', choices=SCENARIOS, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    try:
        return run(args.interface, args.scenario, command, args.evidence)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError, KeyboardInterrupt) as exc:
        print(json.dumps({'verdict': 'INCONCLUSIVE', 'reason_code': 'NETWORK_IMPAIRMENT_FAILED',
                          'reason': str(exc)}), file=sys.stderr)
        return 3


if __name__ == '__main__':
    sys.exit(main())
