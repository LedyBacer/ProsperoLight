# ps5-native-app-boilerplate - opt-in authenticated Nonary-compatible link probe.
# Copyright (C) 2026 LedyBacer and contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Explicit, pre-stream diagnostic; never a background bitrate controller.

Only hosts implementing the optional PyroWave HTTPS/UDP handshake respond.
An unsupported host is not an error in ordinary Moonlight streaming.
"""
import argparse
import hashlib
import http.client
import ipaddress
import json
import math
from pathlib import Path
import secrets
import select
import socket
import ssl
import struct
import sys
import threading
import time
from urllib.parse import urlencode, urlsplit
import xml.etree.ElementTree as ET


DURATION_MS = 2000
WINDOWS = 20
MAX_KBPS = 100000  # Explicit laboratory cap; never the multi-Gbps Nonary search.
PROBE_TIMEOUT_S = 7.0


def expected_packets(kbps, packet_size):
    if (type(kbps) is not int or not 5000 <= kbps <= MAX_KBPS or
            type(packet_size) is not int or not 256 <= packet_size <= 1392):
        raise ValueError('probe rate/packet size outside safe laboratory bounds')
    return kbps * DURATION_MS // (8 * (packet_size + 134))


def pinned_origin(url):
    parsed = urlsplit(url)
    if (parsed.scheme != 'https' or not parsed.hostname or parsed.username or parsed.password or
            parsed.path not in ('', '/') or parsed.query or parsed.fragment):
        raise ValueError('HTTPS origin with no path, URL credentials or query required')
    # Fixed IPv4 address avoids an HTTPS/UDP DNS rebinding or dual-stack split.
    try:
        address = str(ipaddress.IPv4Address(parsed.hostname))
    except ipaddress.AddressValueError as exc:
        raise ValueError('provide an explicit IPv4 host address for the paired endpoint') from exc
    port = parsed.port or 47984
    if not 1 <= port <= 65535:
        raise ValueError('invalid paired HTTPS port')
    return address, port


def pinned_tls_context(host_pem, client_cert, client_key):
    # SSL CA verification plus an exact leaf fingerprint check before sending
    # the authenticated request. The user supplies existing paired identities;
    # this utility never extracts keys from a PS5 or disables TLS validation.
    certificate = Path(host_pem).read_text(encoding='ascii')
    digest = hashlib.sha256(ssl.PEM_cert_to_DER_cert(certificate)).digest()
    context = ssl.create_default_context(cafile=str(host_pem))
    context.check_hostname = False  # Sunshine certificates may not contain the IP SAN.
    context.load_cert_chain(str(client_cert), str(client_key))
    return context, digest


def parse_host_result(body, expected):
    if len(body) > 4096:
        raise ValueError('oversized host probe response')
    root = ET.fromstring(body)
    if root.tag != 'root' or root.attrib.get('status_code') != '200':
        raise ValueError('host rejected probe or returned invalid response')
    count = int(root.findtext('expected', '-1'))
    sent = int(root.findtext('sent', '-1'))
    elapsed = float(root.findtext('elapsedMs', 'nan'))
    if count != expected or not 0 <= sent <= count or not math.isfinite(elapsed):
        raise ValueError('host packet count/sending duration is inconsistent')
    return sent, elapsed


def grade_probe(kbps, packet_size, arrivals_us, sent, sender_ms):
    """Reproduce Nonary's ordered-sequence 100 ms loss windows and p99 drift.

    Relative monotonic receive times avoid cross-machine clock subtraction,
    but these Python socket timestamps are user-space observations (not kernel).
    """
    expected = expected_packets(kbps, packet_size)
    if (len(arrivals_us) != expected or type(sent) is not int or not 0 <= sent <= expected or
            not math.isfinite(sender_ms)):
        raise ValueError('inconsistent probe observations')
    received = sum(at is not None for at in arrivals_us)
    if received > sent:
        raise ValueError('receiver counted more unique packets than the host sent')
    transit, first, last = [], [], []
    worst_window_loss = 0.0
    for window in range(WINDOWS):
        begin, end = expected * window // WINDOWS, expected * (window + 1) // WINDOWS
        seen = 0
        for seq in range(begin, end):
            arrival = arrivals_us[seq]
            if arrival is None:
                continue
            seen += 1
            tick = ((seq + 1) * DURATION_MS + expected - 1) // expected - 1
            delay = arrival / 1000 - tick
            transit.append(delay)
            if window == 0:
                first.append(delay)
            if window == WINDOWS - 1:
                last.append(delay)
        if end > begin:
            worst_window_loss = max(worst_window_loss, 100 * (end - begin - seen) / (end - begin))
    loss = 100 * (expected - received) / expected
    transit.sort()
    p99 = transit[(len(transit)-1) * 99 // 100] - transit[0] if transit else None
    growth = (sum(last) / len(last) - sum(first) / len(first)) if first and last else None
    host_ok = sent >= math.ceil(expected * .98) and 1960 <= sender_ms <= 2040
    capacity_qualified = (host_ok and received > 0 and loss < 2 and worst_window_loss < 5 and
                          growth is not None and growth <= 2)
    smooth = capacity_qualified and p99 is not None and p99 <= 4
    return {'classification': ('SMOOTH_AT_REQUESTED_RATE' if smooth else
                               'CAPACITY_QUALIFIED_JITTER_WARNING' if capacity_qualified else
                               'RATE_NOT_QUALIFIED'),
            'rate_kbps': kbps, 'packet_size': packet_size, 'expected': expected,
            'sent': sent, 'received_unique': received, 'sender_ms': sender_ms,
            'loss_percent': loss, 'worst_window_loss_percent': worst_window_loss,
            'arrival_variation_p99_ms': p99, 'queue_growth_ms': growth,
            'capacity_qualified': capacity_qualified,
            'timestamps': 'receiver Python monotonic user-space; not kernel packet timestamps',
            'semantics': 'one rate pre-stream; not the maximum available Mbps or a bitrate setting'}


def run_probe(url, kbps, packet_size, host_pem, client_cert, client_key):
    address, port = pinned_origin(url)
    expected = expected_packets(kbps, packet_size)
    context, pinned_sha256 = pinned_tls_context(host_pem, client_cert, client_key)
    receiver = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    receiver.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 2 * 1024 * 1024)
    try:
        receiver.bind(('0.0.0.0', 0))
        receiver.setblocking(False)
        local_port = receiver.getsockname()[1]
        token = secrets.token_hex(16).encode('ascii')
        path = '/pyrowave-udp-probe?' + urlencode({
            'kbps': kbps, 'port': local_port, 'packetsize': packet_size,
            'token': token.decode('ascii'), 'handshake': 1})
        progress = {'port': None, 'status': None, 'body': None, 'error': None}
        complete = threading.Event()

        def https_request():
            connection = http.client.HTTPSConnection(address, port, timeout=6, context=context)
            try:
                connection.connect()
                peer = connection.sock.getpeercert(binary_form=True)
                if hashlib.sha256(peer).digest() != pinned_sha256:
                    raise ssl.SSLError('paired host TLS certificate fingerprint mismatch')
                connection.request('GET', path, headers={'Connection': 'close'})
                response = connection.getresponse()
                progress['status'] = response.status
                header = response.getheader('X-PyroWave-Udp-Port')
                if response.status == 200 and header and header.isascii() and header.isdecimal():
                    sender_port = int(header)
                    if 1024 <= sender_port <= 65535:
                        progress['port'] = sender_port
                progress['body'] = response.read(4097)
            except (OSError, ValueError, http.client.HTTPException) as exc:
                progress['error'] = str(exc)[:200]
            finally:
                complete.set()
                connection.close()

        worker = threading.Thread(target=https_request, name='pltest-udp-probe', daemon=True)
        started_ns = time.monotonic_ns()
        worker.start()
        arrivals = [None] * expected
        authenticated = 0
        next_handshake = 0
        deadline = time.monotonic() + PROBE_TIMEOUT_S
        finished_at = None
        while time.monotonic() < deadline:
            now = time.monotonic()
            if (progress['port'] and authenticated == 0 and now >= next_handshake):
                receiver.sendto(token, (address, progress['port']))
                next_handshake = now + .1
            ready, _, _ = select.select([receiver], [], [], min(.05, max(0, deadline-now)))
            if ready:
                # Drain a burst, not one datagram per select() syscall. At
                # 100 Mbps a single frame can carry hundreds of UDP packets.
                for _ in range(512):
                    try:
                        payload, peer = receiver.recvfrom(2048)
                    except BlockingIOError:
                        break
                    timestamp_us = (time.monotonic_ns() - started_ns) // 1000
                    if (progress['port'] and peer == (address, progress['port']) and
                            len(payload) == packet_size + 48 and payload[:32] == token):
                        authenticated += 1
                        seq = struct.unpack_from('!I', payload, 32)[0]
                        if seq < expected and arrivals[seq] is None:
                            arrivals[seq] = timestamp_us
            if complete.is_set():
                if finished_at is None:
                    finished_at = now
                if now - finished_at >= .1:
                    break
        worker.join(timeout=.1)
        if progress['status'] in (404, 405, 501):
            return {'classification': 'UNSUPPORTED', 'reason': 'host lacks optional PyroWave UDP probe'}
        if progress['error']:
            return {'classification': 'INCONCLUSIVE', 'reason': progress['error']}
        if not complete.is_set() or progress['status'] != 200 or not progress['port']:
            return {'classification': 'INCONCLUSIVE', 'reason': 'HTTP rejected/timed out or no authenticated UDP handshake'}
        try:
            sent, elapsed = parse_host_result(progress['body'], expected)
            result = grade_probe(kbps, packet_size, arrivals, sent, elapsed)
        except (ET.ParseError, ValueError, TypeError) as exc:
            return {'classification': 'INCONCLUSIVE', 'reason': str(exc)[:200]}
        return dict(result, authenticated_datagrams=authenticated,
                    transport='paired HTTPS client certificate + pinned leaf + UDP token/port')
    finally:
        receiver.close()


def main(argv=None):
    parser = argparse.ArgumentParser(description='Opt-in, pre-stream Nonary-compatible UDP link probe')
    parser.add_argument('--host', required=True, help='Paired NVHTTP HTTPS origin with literal IPv4')
    parser.add_argument('--host-cert', type=Path, required=True, help='Trusted paired server PEM')
    parser.add_argument('--client-cert', type=Path, required=True, help='Existing paired client PEM')
    parser.add_argument('--client-key', type=Path, required=True, help='Existing paired private key PEM')
    parser.add_argument('--kbps', type=int, required=True, help='Explicit requested test rate 5000..100000')
    parser.add_argument('--packet-size', type=int, default=1200)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--allow-active-traffic-on-idle-host', action='store_true',
                        help='Acknowledge 2s of UDP traffic; only when the host is idle')
    args = parser.parse_args(argv)
    if not args.allow_active_traffic_on_idle_host:
        parser.error('explicit --allow-active-traffic-on-idle-host is required')
    try:
        result = run_probe(args.host, args.kbps, args.packet_size,
                           args.host_cert, args.client_cert, args.client_key)
    except (OSError, ValueError, ssl.SSLError) as exc:
        result = {'classification': 'INCONCLUSIVE', 'reason': str(exc)[:200]}
    args.evidence.parent.mkdir(parents=True, exist_ok=True)
    args.evidence.write_text(json.dumps({'schema': 'prosperolight-active-link-probe/1',
                                         **result}, indent=2, sort_keys=True) + '\n')
    print(json.dumps(result, sort_keys=True))
    return 0 if result['classification'] in ('SMOOTH_AT_REQUESTED_RATE',
                                              'CAPACITY_QUALIFIED_JITTER_WARNING') else (
           1 if result['classification'] == 'RATE_NOT_QUALIFIED' else 3)


if __name__ == '__main__':
    sys.exit(main())
