# ps5-native-app-boilerplate - per-run immutable artifacts and read-only reconnect.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import json
import time
from ftplib import Error
from collect_run import collect


def collect_reconnecting(connect, run_id, output, timeout):
    deadline = time.monotonic() + timeout
    # RETR reconnect is safe; mutation/launch is deliberately never retried.
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError('collection deadline expired; final receipt missing or transport lost')
        try:
            with connect() as ftp:
                receipt = collect(ftp, run_id, output, remaining)
                break
        except (ConnectionError, EOFError, TimeoutError, Error):
            if output.exists():
                raise RuntimeError('partial archive retained; resume requires a new destination')
            time.sleep(min(1, max(0, deadline - time.monotonic())))
    index = {p.name: {'bytes': p.stat().st_size,
                     'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
             for p in output.iterdir() if p.is_file()}
    (output / 'artifact-index.json').write_text(json.dumps(index, indent=2) + '\n')
    return receipt
