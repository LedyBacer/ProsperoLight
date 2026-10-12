# ps5-native-app-boilerplate - local immutable analysis archive and safe HTML report.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import html
import json
from pathlib import Path


def atomic_json(path, value):
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, indent=2, allow_nan=False) + '\n', encoding='utf-8')
    temporary.replace(path)


def archive_analysis(directory, profile, report, events):
    directory = Path(directory)
    for name in ('host', 'stimulus'):
        (directory / name).mkdir(exist_ok=True)
    host = report.get('host_measurement') or {}
    atomic_json(directory / 'host/report.json', host)
    atomic_json(directory / 'stimulus/settings.json', profile.get('host_stimulus', {}))
    atomic_json(directory / 'analysis.json', report['analysis'])
    (directory / 'events.jsonl').write_text(''.join(json.dumps(e, allow_nan=False) + '\n' for e in events), encoding='utf-8')
    body = html.escape(json.dumps({'run_id': report['run_id'], 'verdict': report['verdict'],
                                 'analysis': report['analysis']}, indent=2))
    (directory / 'report.html').write_text('<!doctype html><html lang="en"><meta charset="utf-8">'
        '<meta name="viewport" content="width=device-width"><title>ProsperoLight Test Lab report</title>'
        '<h1>ProsperoLight Test Lab</h1><pre>' + body + '</pre></html>', encoding='utf-8')
    finalize_manifest(directory, report['run_id'])


def finalize_manifest(directory, run_id):
    directory = Path(directory)
    files = {}
    for path in sorted(directory.rglob('*')):
        if path.is_file() and not path.is_symlink() and not path.name.endswith(('.tmp', '.pending')) and path.name != 'manifest.json':
            files[path.relative_to(directory).as_posix()] = {'bytes': path.stat().st_size,
                'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
    atomic_json(directory / 'manifest.json', {'schema_version': 'prosperolight-test-manifest/1',
                 'run_id': run_id, 'files': files,
                 'note': 'manifest excludes itself; regenerated after immutable report publication'})
