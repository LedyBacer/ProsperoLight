# ps5-native-app-boilerplate - bounded network impairment for Test Lab.
# Copyright (C) 2026 LedyBacer and contributors
# SPDX-License-Identifier: GPL-3.0-or-later
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


def qdiscs(interface, *, statistics=False):
    command = ['tc', '-s', '-j'] if statistics else ['tc', '-j']
    return json.loads(execute([*command, 'qdisc', 'show', 'dev', interface]).stdout)


def root_qdisc(rows):
    return next((q for q in rows if q.get('root') is True), None)


def owned_packet_count(rows):
    """Read netem's own packet counter, never a sibling/parent qdisc total."""
    root = root_qdisc(rows)
    if not root or root.get('kind') != 'netem' or root.get('handle') != HANDLE:
        return None
    value = root.get('packets')
    return value if type(value) is int and value >= 0 else None


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


def run(interface, scenario, command, evidence_path=None, *, return_interface=None,
        return_scenario=None):
    if platform.system() != 'Linux':
        raise RuntimeError('tc netem requires a Linux gateway/host with CAP_NET_ADMIN')
    if not command:
        raise ValueError('a test command after -- is required')
    if (return_interface is None) != (return_scenario is None):
        raise ValueError('return interface and scenario must be provided together')
    if return_interface == interface:
        raise ValueError('forward and return egress must be separate interfaces')
    directions = [{'direction': 'host_to_ps5', 'interface': interface, 'scenario': scenario,
                   'one_way_egress': SCENARIOS[scenario]}]
    if return_interface is not None:
        directions.append({'direction': 'ps5_to_host', 'interface': return_interface,
                           'scenario': return_scenario,
                           'one_way_egress': SCENARIOS[return_scenario]})
    # Refuse unsafe/foreign configurations in *either* direction before adding
    # any qdisc. No partial modification if validation of the second fails.
    for direction in directions:
        direction['baseline_qdisc'] = (validate_interface(direction['interface'])
                                       if direction['scenario'] != 'lan' else None)
        direction['applied'] = False
        direction['cleanup_verified'] = direction['scenario'] == 'lan'
        direction['traffic_verdict'] = 'NOT_APPLICABLE' if direction['scenario'] == 'lan' else 'UNVERIFIED'
    record = {'schema': 'prosperolight-netem/1', 'scenario': scenario,
              'one_way_egress': SCENARIOS[scenario], 'interface': interface,
              'start_utc': utc_now(), 'baseline_qdisc': directions[0]['baseline_qdisc'],
              'directions': directions, 'command': command, 'applied': False,
              'cleanup_verified': False}
    installed = []
    code = 3
    previous_term = signal.getsignal(signal.SIGTERM)

    def interrupt(_signum, _frame):
        raise KeyboardInterrupt('netem wrapper interrupted')

    try:
        signal.signal(signal.SIGTERM, interrupt)
        for direction in directions:
            if direction['scenario'] != 'lan':
                dev = direction['interface']
                execute(['tc', 'qdisc', 'add', 'dev', dev, 'root', 'handle', HANDLE, 'netem',
                         *tc_args(direction['scenario'])])
                installed.append(direction)
                active = root_qdisc(qdiscs(dev))
                if not active or active.get('kind') != 'netem' or active.get('handle') != HANDLE:
                    raise RuntimeError(f'netem installation could not be verified on {dev}')
                direction['applied_qdisc'] = active
                if direction is directions[0]:
                    record['applied_qdisc'] = active
                try:
                    start_stats = qdiscs(dev, statistics=True)
                    direction['qdisc_stats_at_start'] = start_stats
                    direction['packets_at_start'] = owned_packet_count(start_stats)
                except Exception as exc:
                    direction['start_stats_error'] = str(exc)
            direction['applied'] = True
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
        for direction in directions:
            if direction not in installed:
                direction['cleanup_verified'] = True
        for direction in reversed(installed):
            dev = direction['interface']
            # Evidence collection is best effort; it must never prevent the
            # ownership-verified rollback of a live network impairment.
            try:
                direction['qdisc_stats_before_cleanup'] = qdiscs(dev, statistics=True)
                end_packets = owned_packet_count(direction['qdisc_stats_before_cleanup'])
                start_packets = direction.get('packets_at_start')
                if start_packets is not None and end_packets is not None and end_packets > start_packets:
                    direction['traffic_verdict'] = 'EGRESS_ACTIVITY_VERIFIED'
                    direction['packet_delta'] = end_packets - start_packets
                else:
                    direction['traffic_reason'] = ('netem packet counter missing or did not increase '
                                                   'during the test; routing is unverified')
            except Exception as exc:
                direction['stats_error'] = str(exc)
            try:
                active = root_qdisc(qdiscs(dev))
                if active and active.get('kind') == 'netem' and active.get('handle') == HANDLE:
                    execute(['tc', 'qdisc', 'del', 'dev', dev, 'root'])
                else:
                    raise RuntimeError('qdisc ownership changed; refusing to delete it')
                restored = root_qdisc(qdiscs(dev))
                direction['cleanup_verified'] = restored is None or restored.get('kind') != 'netem'
            except Exception as exc:
                direction['cleanup_error'] = str(exc)
                direction['cleanup_verified'] = False
        record['cleanup_verified'] = all(d['cleanup_verified'] for d in directions)
        record['traffic_verdict'] = (
            'NOT_APPLICABLE' if all(d['traffic_verdict'] == 'NOT_APPLICABLE' for d in directions)
            else 'EGRESS_ACTIVITY_VERIFIED' if all(d['traffic_verdict'] in (
                'EGRESS_ACTIVITY_VERIFIED', 'NOT_APPLICABLE') for d in directions)
            else 'INCONCLUSIVE')
        if record['traffic_verdict'] == 'INCONCLUSIVE':
            record['reason_code'] = 'NETWORK_TRAFFIC_UNVERIFIED'
        if not record['cleanup_verified']:
            record['cleanup_error'] = 'one or more directions failed ownership-verified cleanup'
        record['end_utc'] = utc_now()
        try:
            write_evidence(evidence_path, record)
        finally:
            signal.signal(signal.SIGTERM, previous_term)
    if not record['cleanup_verified']:
        raise RuntimeError('NETWORK_QDISC_CLEANUP_UNVERIFIED; inspect interface manually')
    if record['traffic_verdict'] == 'INCONCLUSIVE':
        return 3
    return code


def main(argv=None):
    parser = argparse.ArgumentParser(description='Bounded Linux netem run for Test Lab')
    parser.add_argument('--interface', required=True, help='Dedicated egress toward the PS5')
    parser.add_argument('--scenario', choices=SCENARIOS, required=True)
    parser.add_argument('--return-interface', help='Dedicated return egress toward the host')
    parser.add_argument('--return-scenario', choices=SCENARIOS,
                        help='Independent PS5-to-host impairment scenario')
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    try:
        return run(args.interface, args.scenario, command, args.evidence,
                   return_interface=args.return_interface, return_scenario=args.return_scenario)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError, KeyboardInterrupt) as exc:
        print(json.dumps({'verdict': 'INCONCLUSIVE', 'reason_code': 'NETWORK_IMPAIRMENT_FAILED',
                          'reason': str(exc)}), file=sys.stderr)
        return 3


if __name__ == '__main__':
    sys.exit(main())
