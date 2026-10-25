# ps5-native-app-boilerplate - bounded network impairment for Test Lab.
# Copyright (C) 2026 LedyBacer and contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Linux gateway egress impairment for paired PS5 network experiments.

No persistent qdisc is left behind by a normal run. We never replace an
existing custom qdisc, and cleanup verifies ownership before deleting one.
"""
import argparse
import datetime as dt
import ipaddress
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



def owned_drop_count(rows):
    """Read netem's drop counter instead of guessing from packet totals."""
    root = root_qdisc(rows)
    if not root or root.get('kind') != 'netem' or root.get('handle') != HANDLE:
        return None
    value = root.get('drops')
    if value is None:
        value = (root.get('stats2') or {}).get('queue', {}).get('drops')
    return value if type(value) is int and value >= 0 else None


def loss_counter_verdict(direction, end_stats):
    """Reject gross loss mismatches, not normal statistical variation.

    Very small samples do not establish an empirical loss rate. With at
    least 30 expected drops, fewer than a tenth as many merits investigation.
    """
    loss = (SCENARIOS[direction['scenario']] or {}).get('loss_percent', 0)
    if not loss:
        return 'NOT_APPLICABLE'
    sent = direction.get('packet_delta', 0)
    if sent <= 0:
        return 'INSUFFICIENT_TRAFFIC'
    before = direction.get('drops_at_start')
    after = owned_drop_count(end_stats)
    expected_floor = sent * loss / 100
    if before is None or after is None or after < before:
        direction['loss_reason'] = 'qdisc drop counter unavailable'
        return 'INCONCLUSIVE' if expected_floor >= 30 else 'INSUFFICIENT_SAMPLE'
    dropped = after - before
    direction['drop_delta'] = dropped
    expected = (sent + dropped) * loss / 100
    direction['expected_drops_at_configured_rate'] = round(expected, 2)
    if expected < 30:
        return 'INSUFFICIENT_SAMPLE'
    if dropped < expected * 0.1:
        direction['loss_reason'] = (
            'netem drops implausibly below configured loss; verify traffic '
            'direction, qdisc accounting and PS5 UDP capture')
        return 'INCONCLUSIVE'
    return 'PLAUSIBLE'


def parse_video_udp_target(value):
    """Require an explicit endpoint; the game-stream UDP port can vary."""
    try:
        host, port = value.rsplit(':', 1)
        address = str(ipaddress.IPv4Address(host))
        number = int(port)
        if not 1 <= number <= 65535:
            raise ValueError('port out of range')
        return address, number
    except (ValueError, AttributeError) as exc:
        raise ValueError('expected --verify-video-udp IPv4:port') from exc


def validate_video_udp_route(interface, ip):
    """Verify this gateway namespace routes the PS5 IP to shaped egress."""
    routes = json.loads(execute(['ip', '-j', 'route', 'get', ip]).stdout)
    if not routes or routes[0].get('dev') != interface:
        raise ValueError('PS5 UDP destination does not route through selected egress')
    return routes[0].get('dev')


def captured_udp_count(stderr):
    match = re.search(r'(\d+) packets captured', stderr or '')
    return int(match.group(1)) if match else None

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
        return_scenario=None, verify_video_udp=None):
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
    video_target = parse_video_udp_target(verify_video_udp) if verify_video_udp else None
    if video_target and scenario == 'lan':
        raise ValueError('UDP flow verification requires an impaired scenario')
    if video_target:
        validate_video_udp_route(interface, video_target[0])
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
              'cleanup_verified': False,
              'video_flow_verdict': 'UNVERIFIED' if video_target else 'NOT_REQUESTED'}
    installed = []
    video_capture = None
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
                    direction['drops_at_start'] = owned_drop_count(start_stats)
                except Exception as exc:
                    direction['start_stats_error'] = str(exc)
            direction['applied'] = True
        record['applied'] = True
        if video_target:
            target_ip, target_port = video_target
            record['video_udp_target'] = f'{target_ip}:{target_port}'
            # tcpdump stops after 64 matches and writes no packet headers or
            # payloads to the evidence; this is route/flow qualification only.
            video_capture = subprocess.Popen(
                ['tcpdump', '-n', '-q', '-i', interface, '-c', '64', '-w', '/dev/null',
                 f'udp and dst host {target_ip} and dst port {target_port}'],
                stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
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
        if video_capture is not None:
            try:
                if video_capture.poll() is None:
                    video_capture.send_signal(signal.SIGINT)
                _out, capture_stderr = video_capture.communicate(timeout=5)
                captured = captured_udp_count(capture_stderr)
                record['video_udp_samples'] = captured
                if captured is not None and captured >= 8:
                    record['video_flow_verdict'] = 'UDP_EGRESS_VERIFIED'
                else:
                    record['video_flow_verdict'] = 'INCONCLUSIVE'
                    record['video_flow_reason'] = 'fewer than 8 matching UDP packets on shaped egress'
            except (OSError, subprocess.TimeoutExpired) as exc:
                record['video_flow_verdict'] = 'INCONCLUSIVE'
                record['video_flow_reason'] = str(exc)
                try:
                    video_capture.kill()
                    video_capture.communicate(timeout=2)
                except (OSError, subprocess.TimeoutExpired):
                    pass
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
                    direction['loss_counter_verdict'] = loss_counter_verdict(
                        direction, direction['qdisc_stats_before_cleanup'])
                    if direction['loss_counter_verdict'] == 'INCONCLUSIVE':
                        direction['traffic_verdict'] = 'INCONCLUSIVE'
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
            record['reason_code'] = (
                'NETWORK_LOSS_UNVERIFIED'
                if any(d.get('loss_counter_verdict') == 'INCONCLUSIVE' for d in directions)
                else 'NETWORK_TRAFFIC_UNVERIFIED')
        if record['video_flow_verdict'] == 'INCONCLUSIVE':
            record['reason_code'] = 'VIDEO_UDP_FLOW_UNVERIFIED'
        if not record['cleanup_verified']:
            record['cleanup_error'] = 'one or more directions failed ownership-verified cleanup'
        record['end_utc'] = utc_now()
        try:
            write_evidence(evidence_path, record)
        finally:
            signal.signal(signal.SIGTERM, previous_term)
    if not record['cleanup_verified']:
        raise RuntimeError('NETWORK_QDISC_CLEANUP_UNVERIFIED; inspect interface manually')
    if record['traffic_verdict'] == 'INCONCLUSIVE' or record['video_flow_verdict'] == 'INCONCLUSIVE':
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
    parser.add_argument('--verify-video-udp', metavar='PS5_IP:PORT',
                        help='Require matching UDP flow on the shaped PS5 egress')
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    try:
        return run(args.interface, args.scenario, command, args.evidence,
                   return_interface=args.return_interface, return_scenario=args.return_scenario,
                   verify_video_udp=args.verify_video_udp)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError, KeyboardInterrupt) as exc:
        print(json.dumps({'verdict': 'INCONCLUSIVE', 'reason_code': 'NETWORK_IMPAIRMENT_FAILED',
                          'reason': str(exc)}), file=sys.stderr)
        return 3


if __name__ == '__main__':
    sys.exit(main())
