# ps5-native-app-boilerplate - machine-readable Test Lab coordinator CLI.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import json
import os
from pathlib import Path
import time

import httpx
import typer

app = typer.Typer(no_args_is_help=True, help='ProsperoLight Test Lab coordinator')
TERMINAL = {'COMPLETED', 'FAILED', 'CANCELLED', 'INCONCLUSIVE'}


def request(method, path, **kwargs):
    headers = {}
    if os.environ.get('PLTEST_API_TOKEN'):
        headers['Authorization'] = 'Bearer ' + os.environ['PLTEST_API_TOKEN']
    try:
        response = httpx.request(method, os.environ.get('PLTEST_API_URL', 'http://127.0.0.1:8765') + path,
                                 headers=headers, timeout=25 if method == 'POST' and path.endswith('/stop') else 10, **kwargs)
        response.raise_for_status()
        return response.json()
    except httpx.HTTPStatusError as exc:
        try:
            body = exc.response.json()
            detail = body.get('detail') if isinstance(body, dict) else None
            code = detail.get('code') if isinstance(detail, dict) else None
        except ValueError:
            code = None
        typer.echo(json.dumps({'verdict': 'INCONCLUSIVE', 'stage': 'API',
                               'reason_code': 'HTTP_' + str(exc.response.status_code),
                               'api_error_code': code if isinstance(code, str) else 'REQUEST_REJECTED',
                               'reason': 'request rejected; response values omitted'}))
        raise typer.Exit(2)
    except httpx.HTTPError as exc:
        typer.echo(json.dumps({'verdict': 'INCONCLUSIVE', 'stage': 'API',
                               'reason_code': type(exc).__name__, 'reason': str(exc)}))
        raise typer.Exit(2)


def emit(value):
    typer.echo(json.dumps(value, separators=(',', ':'), ensure_ascii=False))


def exit_code(verdict):
    return {'PASS': 0, 'FAIL': 1, 'INCONCLUSIVE': 3, 'CANCELLED': 4}.get(verdict, 2)


@app.command()
def coordinator(host: str = '127.0.0.1', port: int = 8765,
                data_dir: Path = Path('build/test-lab-coordinator'), mode: str = 'fake'):
    from .server import main
    raise typer.Exit(main(['--host', host, '--port', str(port), '--data-dir', str(data_dir), '--mode', mode]))


@app.command()
def doctor():
    health = request('GET', '/api/v1/health')
    result = request('GET', '/api/v1/capabilities')
    emit({'health': health, 'capabilities': result})
    raise typer.Exit(0)


@app.command()
def run(profile: Path = typer.Option(..., exists=True, readable=True),
        suite: str = '', wait: bool = True,
        timeout: int = typer.Option(1800, min=1, max=7200)):
    try: value = json.loads(profile.read_text(encoding='utf-8'))
    except (OSError, json.JSONDecodeError) as exc:
        emit({'verdict': 'INCONCLUSIVE', 'stage': 'PROFILE', 'reason_code': type(exc).__name__, 'reason': str(exc)})
        raise typer.Exit(2)
    mode = os.environ.get('PLTEST_API_MODE')
    if mode is None:
        mode = 'real' if request('GET', '/api/v1/capabilities')['api_mode'] == 'real' else 'fake'
    suite = suite or ('decoder-smoke' if mode == 'real' else 'fake-smoke')
    created = request('POST', '/api/v1/runs', json={'schema_version': 'prosperolight-test-lab-api/1',
                    'suite': suite, 'profile': value, 'mode': mode})
    started = request('POST', '/api/v1/runs/' + created['run_id'] + '/start')
    if not wait:
        emit(started); raise typer.Exit(0)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        current = request('GET', '/api/v1/runs/' + created['run_id'])
        if current['state'] in TERMINAL:
            # Both CLI and API read this exact immutable report object.
            report = request('GET', '/api/v1/runs/' + created['run_id'] + '/report') if current['report'] else current
            emit(report)
            raise typer.Exit(exit_code(report.get('verdict', current['verdict'])))
        time.sleep(2)
    request('POST', '/api/v1/runs/' + created['run_id'] + '/stop')
    current = request('GET', '/api/v1/runs/' + created['run_id'])
    emit(current['report'] or current)
    raise typer.Exit(exit_code(current['verdict']))


@app.command()
def status(run_id: str): emit(request('GET', '/api/v1/runs/' + run_id))


@app.command()
def stop(run_id: str):
    result = request('POST', '/api/v1/runs/' + run_id + '/stop'); emit(result)
    raise typer.Exit(exit_code(result['verdict']))


@app.command()
def report(run_id: str, json_output: bool = typer.Option(True, '--json/--no-json')):
    value = request('GET', '/api/v1/runs/' + run_id + '/report')
    emit(value) if json_output else typer.echo(json.dumps(value, indent=2, ensure_ascii=False))
    raise typer.Exit(exit_code(value['verdict']))


@app.command()
def artifacts(run_id: str): emit(request('GET', '/api/v1/runs/' + run_id + '/artifacts'))


@app.command()
def compare(baseline: str, candidate: str):
    value = request('POST', '/api/v1/compare', json={'schema_version': 'prosperolight-test-lab-api/1',
                                                     'baseline': baseline, 'candidate': candidate})
    emit(value); raise typer.Exit(exit_code(value['verdict']))


@app.command()
def matrix(profile: Path = typer.Option(..., exists=True, readable=True), repetitions: int = typer.Option(3, min=1, max=20), suite: str = ''):
    results = []
    for _ in range(repetitions):
        try: run(profile=profile, suite=suite, wait=True, timeout=1800)
        except typer.Exit as exc:
            if exc.exit_code: raise
        results.append('completed')
    emit({'schema_version': 'prosperolight-test-lab-report/1', 'repetitions': repetitions, 'results': results})


def main(argv=None):
    try:
        app(args=argv, standalone_mode=False)
    except typer.Exit as exc:
        return exc.exit_code or 0
    return 0
