#!/usr/bin/env python3
# ps5-native-app-boilerplate / ProsperoLight - isolated netem UDP calibration.
# Copyright (C) 2026 LedyBacer and contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Qualify a tc netem loss profile without any PS5, host routes or live NIC.

Explicitly opt-in and requires CAP_NET_ADMIN. Creates a private network
namespace, shapes only its loopback and deletes the owned namespace in finally.
This calibrates the Linux gateway's qdisc implementation, not the actual PS5
video route; use --verify-video-udp separately for that evidence.
"""

import argparse
import json
import os
from pathlib import Path
import secrets
import socket
import struct
import subprocess
import sys
import threading
import time

sys.path.insert(0, str(Path(__file__).resolve().parent / "src"))
from pltest import netem  # noqa: E402


def command(args):
    return subprocess.run(args, check=True, text=True, capture_output=True)


def udp_worker(count):
    receiver = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    receiver.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
    receiver.bind(("127.0.0.1", 0))
    receiver.settimeout(0.1)
    target = receiver.getsockname()
    received = set()
    finished = threading.Event()

    def collect():
        while not finished.is_set():
            try:
                data, _ = receiver.recvfrom(256)
            except socket.timeout:
                continue
            if len(data) >= 4:
                sequence = struct.unpack("!I", data[:4])[0]
                if sequence < count:
                    received.add(sequence)

    thread = threading.Thread(target=collect, daemon=True)
    thread.start()
    try:
        for seq in range(count):
            sender.sendto(struct.pack("!I", seq) + bytes(92), target)
            if seq % 16 == 0:
                time.sleep(0.001)  # Keep receiver and loopback queue below saturation.
        time.sleep(1.0)
    finally:
        finished.set()
        thread.join(timeout=2.0)
        sender.close()
        receiver.close()
    return {"sent": count, "unique_received": len(received),
            "missing": count - len(received)}


def calibrate(scenario, count):
    if scenario == "lan" or scenario not in netem.SCENARIOS:
        raise ValueError("calibration requires a named non-LAN netem scenario")
    if not 1000 <= count <= 100000:
        raise ValueError("packet count must be between 1000 and 100000")
    name = f"pl-cal-{os.getpid()}-{secrets.token_hex(3)}"
    created = False
    try:
        command(["ip", "netns", "add", name])
        created = True
        command(["ip", "-n", name, "link", "set", "lo", "up"])
        command(["ip", "netns", "exec", name, "tc", "qdisc", "add", "dev", "lo",
                 "root", "handle", netem.HANDLE, "netem", *netem.tc_args(scenario)])
        before = json.loads(command(["ip", "netns", "exec", name, "tc", "-s", "-j",
                                     "qdisc", "show", "dev", "lo"]).stdout)
        trial = json.loads(command(["ip", "netns", "exec", name, sys.executable,
                                    str(Path(__file__).resolve()), "--worker", str(count)]).stdout)
        after = json.loads(command(["ip", "netns", "exec", name, "tc", "-s", "-j",
                                    "qdisc", "show", "dev", "lo"]).stdout)
        direction = {"scenario": scenario, "packet_delta": count,
                     "drops_at_start": netem.owned_drop_count(before)}
        verdict = netem.loss_counter_verdict(direction, after)
        discrepancy = abs(trial["missing"] - direction.get("drop_delta", -count))
        counters_match = discrepancy <= max(3, count // 2000)
        return {"scenario": scenario, "configured": netem.SCENARIOS[scenario],
                "synthetic_udp": trial, "qdisc": direction,
                "loss_counter_verdict": verdict,
                "udp_qdisc_consistent": counters_match,
                "qualified": verdict == "PLAUSIBLE" and trial["missing"] > 0 and counters_match,
                "scope": "isolated_loopback_only"}
    finally:
        if created:
            # Delete exactly our own random network namespace, not a host route
            # or any non-owned interface. Report cleanup errors to the caller.
            command(["ip", "netns", "del", name])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scenario", choices=sorted(netem.SCENARIOS),
                        default="wifi-loss-independent")
    parser.add_argument("--count", type=int, default=12000)
    parser.add_argument("--worker", type=int, help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.worker is not None:
        print(json.dumps(udp_worker(args.worker), sort_keys=True))
        return 0
    try:
        outcome = calibrate(args.scenario, args.count)
    except (ValueError, subprocess.CalledProcessError, OSError) as error:
        print(json.dumps({"qualified": False, "reason": str(error)}))
        return 3
    print(json.dumps(outcome, indent=2, sort_keys=True))
    return 0 if outcome["qualified"] else 3


if __name__ == "__main__":
    raise SystemExit(main())
