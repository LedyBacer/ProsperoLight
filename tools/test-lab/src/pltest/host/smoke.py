# ps5-native-app-boilerplate - standalone host-worker smoke, no PS5 required.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import asyncio
import json
import uuid
from pathlib import Path
from .client import HostWorkerAdapter


async def run(args):
    run_id = str(uuid.uuid4())
    root = args.output / run_id; root.mkdir(parents=True, exist_ok=False)
    settings = json.loads(args.settings.read_text())
    async with HostWorkerAdapter(args.worker, args.token_file.read_text().strip()) as worker:
        try:
            ready = await worker.command(run_id, 'prepare', settings)
            (root / 'ready.json').write_text(json.dumps(ready, indent=2))
            await worker.command(run_id, 'start')
            report = await worker.wait_report(run_id, timeout=settings['seconds'] + settings.get('warmup_seconds', 0) + 15)
            (root / 'host-report.json').write_text(json.dumps(report, indent=2))
        except Exception as exc:
            (root / 'failure.json').write_text(json.dumps({'run_id': run_id, 'reason': str(exc)}))
            raise
    print(json.dumps({'run_id': run_id, 'output': str(root), 'measurement_validity': report['measurement_validity'],
                      'raf_fps': report.get('raf_fps'), 'reason': report.get('reason')}))
    return 0 if report['measurement_validity'] == 'VALID' else 3


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--worker', required=True)
    parser.add_argument('--token-file', type=Path, required=True)
    parser.add_argument('--settings', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    try:
        return asyncio.run(run(parser.parse_args()))
    except Exception as exc:
        print(json.dumps({'measurement_validity': 'INCONCLUSIVE', 'reason': str(exc)})); return 3


if __name__ == '__main__':
    raise SystemExit(main())
