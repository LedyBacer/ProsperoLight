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
    analysis = report['analysis']
    escape = lambda value: html.escape(str(value))
    rows = []
    for name, value in analysis.get('metrics', {}).items():
        if isinstance(value, dict):
            cells = [name, value.get('count'), value.get('median'), value.get('p95'), value.get('p99')]
        else:
            cells = [name, '', value, '', '']
        rows.append('<tr>'+''.join('<td>'+escape(v)+'</td>' for v in cells)+'</tr>')
    classifications = ' · '.join(escape(analysis.get(key, 'INCONCLUSIVE')) for key in
                               ('functional_verdict','measurement_validity','performance_delta'))
    body = ('<!doctype html><html lang="en"><meta charset="utf-8">'
            '<meta name="viewport" content="width=device-width"><title>ProsperoLight Test Lab report</title>'
            '<h1>ProsperoLight Test Lab</h1><p>'+escape(report['run_id'])+'</p><p>'+classifications+'</p>'
            '<p>Timing fields ending in _us are microseconds. Software observations are not physical scanout.</p>'
            '<table><thead><tr><th>Metric</th><th>Count</th><th>Median / value</th><th>p95</th><th>p99</th></tr></thead><tbody>'+
            ''.join(rows)+'</tbody></table><h2>Evidence limits</h2><ul>'+
            ''.join('<li>'+escape(issue)+'</li>' for issue in analysis.get('issues',[]))+'</ul>'+
            '<details><summary>Complete analysis and clock semantics</summary><pre>'+
            escape(json.dumps(analysis,indent=2))+'</pre></details></html>')
    (directory / 'report.html').write_text(body, encoding='utf-8')
    finalize_manifest(directory, report['run_id'])


def finalize_manifest(directory, run_id):
    directory = Path(directory)
    files = {}
    for path in sorted(directory.rglob('*')):
        if path.is_file() and not path.is_symlink() and not path.name.endswith(('.tmp', '.pending')) and path != directory / 'manifest.json':
            files[path.relative_to(directory).as_posix()] = {'bytes': path.stat().st_size,
                'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
    atomic_json(directory / 'manifest.json', {'schema_version': 'prosperolight-test-manifest/1',
                 'run_id': run_id, 'files': files,
                 'note': 'manifest excludes itself; regenerated after immutable report publication'})
