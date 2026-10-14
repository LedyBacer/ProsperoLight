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
    return {'PASS': 0, 'SIMULATED': 3, 'FAIL': 1, 'INCONCLUSIVE': 3, 'CANCELLED': 4}.get(verdict, 2)


@app.command()
def coordinator(host: str = '127.0.0.1', port: int = 8765,
                data_dir: Path = Path('build/test-lab-coordinator'), mode: str = 'fake'):
    from .server import main
    raise typer.Exit(main(['--host', host, '--port', str(port), '--data-dir', str(data_dir), '--mode', mode]))


@app.command()
def doctor():
    health = request('GET', '/api/v1/health')
    result = request('GET', '/api/v1/capabilities')
    diagnostics = request('GET', '/api/v1/doctor')
    emit({'health': health, 'capabilities': result, 'diagnostics': diagnostics})
    raise typer.Exit(exit_code(diagnostics['verdict']))


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
            raise typer.Exit(exit_code(report.get('summary_verdict', report.get('verdict', current['verdict']))))
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
    raise typer.Exit(exit_code(value.get('summary_verdict', value['verdict'])))


@app.command()
def artifacts(run_id: str): emit(request('GET', '/api/v1/runs/' + run_id + '/artifacts'))


@app.command()
def compare(baseline: str = '', candidate: str = '', pairs: Path | None = typer.Option(None, exists=True, readable=True),
            calibration: bool = False, screening: Path | None = typer.Option(None, exists=True, readable=True),
            calibration_pairs: Path | None = typer.Option(None, exists=True, readable=True)):
    payload = {'pairs': json.loads(pairs.read_text())} if pairs else {'baseline': baseline, 'candidate': candidate}
    value = request('POST', '/api/v1/compare', json={'schema_version': 'prosperolight-test-lab-api/1',
                                                     **payload, 'calibration':calibration,
                                                     'screening':json.loads(screening.read_text()) if screening else None,
                                                     'calibration_pairs':json.loads(calibration_pairs.read_text()) if calibration_pairs else None})
    emit(value); raise typer.Exit(exit_code(value.get('summary_verdict', value['verdict'])))


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
        result = app(args=argv, standalone_mode=False)
        return result if type(result) is int else 0
    except typer.Exit as exc:
        return exc.exit_code or 0
    return 0


@app.command()
def prepare_variant(directory: Path = typer.Option(..., exists=True, file_okay=False),
                    provenance: Path = typer.Option(..., exists=True, dir_okay=False),
                    profile: Path = typer.Option(..., exists=True, dir_okay=False),
                    manifest_root: Path = typer.Option(..., exists=True, file_okay=False),
                    output: Path = typer.Option(...)):
    """Index prepared Linux/WSL artifacts; never build or deploy implicitly."""
    from ..build import prebuilt, definition_flags, validate_variant
    import hashlib
    try:
        root = manifest_root.resolve()
        identity = prebuilt(directory)
        provenance_bytes = provenance.read_bytes()
        provenance_value = json.loads(provenance_bytes)
        value = dict(schema='prosperolight-build-variant/1', source_sha=provenance_value['commit'],
            build_label=identity['build_label'], eboot_sha256=identity['eboot_sha256'],
            directory=directory.resolve().relative_to(root).as_posix(),
            provenance=provenance.resolve().relative_to(root).as_posix(),
            provenance_sha256=hashlib.sha256(provenance_bytes).hexdigest(),
            source_tree_sha256=provenance_value['source_sha256'],
            build_flags=definition_flags(provenance_value), profile=json.loads(profile.read_bytes()))
        value['files'] = {path.relative_to(directory).as_posix(): {
            'bytes':path.stat().st_size, 'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
            for path in directory.rglob('*') if path.is_file()}
        validate_variant(value, root)
        with output.open('x', encoding='utf-8') as stream:
            json.dump(value, stream, indent=2); stream.write('\n')
    except (OSError, ValueError, KeyError, TypeError) as exc:
        emit({'verdict':'INCONCLUSIVE','reason_code':'BUILD_MANIFEST_INVALID','reason':str(exc)}); raise typer.Exit(3)
    emit({'manifest':str(output), 'source_sha':value['source_sha'], 'eboot_sha256':value['eboot_sha256']})


@app.command()
def build_variant(builder: Path = typer.Option(..., exists=True, dir_okay=False),
                  source_sha: str = typer.Option(...),
                  directory: Path = typer.Option(...), provenance: Path = typer.Option(...),
                  profile: Path = typer.Option(..., exists=True, dir_okay=False),
                  manifest_root: Path = typer.Option(..., exists=True, file_okay=False),
                  output: Path = typer.Option(...)):
    """Build a clean source commit using an explicitly configured Linux SDK host."""
    from ..remote_builder import build
    import subprocess
    try:
        root = manifest_root.resolve()
        if any(root not in path.resolve().parents for path in (directory,provenance,output)):
            raise ValueError('BUILD_OUTPUT_OUTSIDE_MANIFEST_ROOT')
        if output.exists() or builder.stat().st_size > 16384:
            raise ValueError('BUILD_CONFIGURATION_OR_OUTPUT_INVALID')
        build(json.loads(builder.read_bytes()),source_sha,directory,provenance)
    except (OSError,ValueError,KeyError,TypeError,RuntimeError,subprocess.SubprocessError) as exc:
        emit({'verdict':'INCONCLUSIVE','reason_code':'BUILD_FAILED','reason':str(exc)[:256]})
        raise typer.Exit(3)
    prepare_variant(directory,provenance,profile,manifest_root,output)


@app.command()
def paired(baseline: Path = typer.Option(..., exists=True, readable=True),
           candidate: Path = typer.Option(..., exists=True, readable=True),
           repetitions: int = typer.Option(3, min=3, max=20), suite: str = 'decoder-smoke',
           allow_deploy: bool = False, calibration: bool = False,
           manifest_root: Path = typer.Option(..., exists=True, file_okay=False),
           screening: Path | None = typer.Option(None, exists=True, readable=True),
           calibration_pairs: Path | None = typer.Option(None, exists=True, readable=True)):
    """Interleave A/B, then compare unique repeated run pairs; never parallelize PS5."""
    from ..build import validate_variant
    import hashlib
    if not allow_deploy:
        emit({'verdict':'INCONCLUSIVE','reason_code':'DEPLOY_PERMISSION_REQUIRED'}); raise typer.Exit(3)
    prepared = []
    limits = None; reference = None
    try:
        from .models import CompareRequest
        limits = json.loads(screening.read_text()) if screening else None
        reference = json.loads(calibration_pairs.read_text()) if calibration_pairs else None
        from ..comparison import compare_pairs
        if compare_pairs([], screening=limits)['reason_code'] == 'SCREENING_CONFIGURATION_INVALID':
            raise ValueError('SCREENING_CONFIGURATION_INVALID')
        if calibration and reference is not None:
            raise ValueError('A/A cannot reference another calibration')
        if reference is not None:
            CompareRequest(pairs=reference, calibration=True, screening=limits)
        for path in (baseline, candidate):
            data = path.read_bytes()
            value = json.loads(data)
            variant = validate_variant(value, manifest_root)
            profile = dict(value['profile'], source_sha=variant['source_sha'], build_label=variant['build_label'],
                allow_deploy=True, build_variant={'manifest':path.resolve().relative_to(manifest_root.resolve()).as_posix(),
                                                'sha256':hashlib.sha256(data).hexdigest()})
            prepared.append((variant, profile))
        if calibration:
            if any(prepared[0][0][key] != prepared[1][0][key] for key in ('source_sha','build_label','eboot_sha256')):
                raise ValueError('CALIBRATION_REQUIRES_SAME_BUILD')
        elif prepared[0][0]['source_sha'] == prepared[1][0]['source_sha'] or prepared[0][0]['eboot_sha256'] == prepared[1][0]['eboot_sha256']:
            raise ValueError('DISTINCT_SOURCE_AND_BINARY_REQUIRED')
        if prepared[0][0]['build_flags'] != prepared[1][0]['build_flags']:
            raise ValueError('BUILD_FLAGS_MISMATCH')
    except (OSError, ValueError, KeyError, TypeError) as exc:
        emit({'verdict':'INCONCLUSIVE','reason_code':'BUILD_MANIFEST_INVALID','reason':str(exc)}); raise typer.Exit(3)
    capabilities = request('GET', '/api/v1/capabilities')
    if capabilities['api_mode'] != 'real':
        emit({'verdict':'INCONCLUSIVE','reason_code':'REAL_ADAPTERS_REQUIRED'}); raise typer.Exit(3)
    if reference is not None:
        noise = request('POST','/api/v1/compare',json={'pairs':reference,'calibration':True,'screening':limits})
        if not noise.get('screening_usable'):
            emit(noise); raise typer.Exit(3)
    pairs = []
    for _ in range(repetitions):
        ids = []
        for variant, profile in prepared:
            created = request('POST','/api/v1/runs',json={'suite':suite,'mode':'real','profile':profile})
            rid = created['run_id']; request('POST','/api/v1/runs/'+rid+'/start')
            deadline = time.monotonic()+1800
            while time.monotonic()<deadline:
                current=request('GET','/api/v1/runs/'+rid)
                if current['state'] in TERMINAL: break
                time.sleep(2)
            else:
                request('POST','/api/v1/runs/'+rid+'/stop'); raise typer.Exit(3)
            if current['verdict'] != 'PASS':
                emit(current['report'] or current); raise typer.Exit(exit_code(current['verdict']))
            ids.append(rid)
        pairs.append(ids)
    comparison = {'pairs':pairs,'calibration':calibration}
    if limits is not None: comparison['screening'] = limits
    if reference is not None: comparison['calibration_pairs'] = reference
    value=request('POST','/api/v1/compare',json=comparison)
    emit(value); raise typer.Exit(exit_code(value['verdict']))


@app.command()
def export(run_id: str, output: Path = typer.Option(...), redact: bool = True):
    """Download a local archive; raw files remain untouched at the coordinator."""
    if output.exists():
        emit({'verdict':'INCONCLUSIVE','reason_code':'OUTPUT_EXISTS'}); raise typer.Exit(2)
    headers={'Authorization':'Bearer '+os.environ['PLTEST_API_TOKEN']} if os.environ.get('PLTEST_API_TOKEN') else {}
    temporary=output.with_name(output.name+'.tmp')
    total=0; created_temporary=False
    try:
        with httpx.stream('GET',os.environ.get('PLTEST_API_URL','http://127.0.0.1:8765')+
                '/api/v1/runs/'+run_id+'/bundle?redact='+str(redact).lower(),headers=headers,timeout=30) as response:
            response.raise_for_status()
            with temporary.open('xb') as destination:
                created_temporary=True
                os.chmod(temporary, 0o600)
                for data in response.iter_bytes():
                    total+=len(data)
                    if total>256*1024*1024:raise ValueError('export limit exceeded')
                    destination.write(data)
        os.link(temporary,output)
        temporary.unlink()
    except (OSError,ValueError,httpx.HTTPError) as exc:
        if created_temporary: temporary.unlink(missing_ok=True)
        emit({'verdict':'INCONCLUSIVE','reason_code':type(exc).__name__});raise typer.Exit(2)
    emit({'output':str(output),'bytes':total,'redacted':redact})


@app.command()
def benchmark_plan(profile: Path = typer.Option(..., exists=True, readable=True),
                   name: str = typer.Option(...), output_dir: Path = typer.Option(...)):
    """Write explicit mode profiles; does not claim hardware support or run them."""
    from ..sweeps import benchmark_profiles
    values = benchmark_profiles(json.loads(profile.read_text()), name)
    output_dir.mkdir(parents=True, exist_ok=False)
    paths=[]
    for index,value in enumerate(values):
        path=output_dir/('profile-'+str(index+1)+'.json')
        path.write_text(json.dumps(value,indent=2)+'\n');paths.append(str(path))
    emit({'schema':'prosperolight-benchmark-plan/1','profiles':paths,
          'capability_verification':'each real run required; requested modes are not verified support'})


@app.command()
def sweep(profile: Path = typer.Option(..., exists=True, readable=True),
          bitrates: str = '10,20,40,60,80', stop_on_saturation: bool = True):
    """Run ordered high-entropy bitrate points, preserving each exact report."""
    from ..sweeps import bitrate_profiles, saturation
    try:
        values=bitrate_profiles(json.loads(profile.read_text()),[int(rate) for rate in bitrates.split(',')])
    except (ValueError,KeyError,TypeError) as exc:
        emit({'overall_verdict':'INCONCLUSIVE','reason_code':'SWEEP_CONFIGURATION_INVALID','reason':str(exc)})
        raise typer.Exit(3)
    if request('GET','/api/v1/capabilities')['api_mode'] != 'real':
        emit({'overall_verdict':'INCONCLUSIVE','reason_code':'REAL_ADAPTERS_REQUIRED'});raise typer.Exit(3)
    reports=[];reason='ALL_POINTS_COMPLETED';active_run_id=None
    infrastructure_error=False
    try:
        for value in values:
            created=request('POST','/api/v1/runs',json={'suite':'bitrate-ramp','mode':'real','profile':value})
            rid=created['run_id'];active_run_id=rid;request('POST','/api/v1/runs/'+rid+'/start')
            deadline=time.monotonic()+1800
            while time.monotonic()<deadline:
                current=request('GET','/api/v1/runs/'+rid)
                if current['state'] in TERMINAL:break
                time.sleep(2)
            else:
                request('POST','/api/v1/runs/'+rid+'/stop')
                reason='SWEEP_POINT_TIMEOUT';break
            report=request('GET','/api/v1/runs/'+rid+'/report');reports.append(report);active_run_id=None;emit(report)
            if report.get('overall_verdict') != 'PASS':
                if report.get('execution_state')=='INCONCLUSIVE' or report.get('cleanup_errors'):
                    reason='POINT_INFRASTRUCTURE_FAILURE'
                elif report.get('functional_verdict',(report.get('analysis') or {}).get('functional_verdict'))=='FAIL':
                    reason='POINT_FUNCTIONAL_FAILURE'
                else:
                    reason='POINT_NOT_VALID'
                break
            plateau=saturation(reports[-2] if len(reports)>1 else report,report)
            if plateau is None:reason='THROUGHPUT_METRIC_UNAVAILABLE';break
            if stop_on_saturation and plateau and len(reports)<len(values):reason='THROUGHPUT_PLATEAU_WITH_FPS_DROP';break
    except typer.Exit as exc:
        if exc.exit_code != 2:raise
        infrastructure_error=True;reason='SWEEP_API_FAILURE'
    last=reports[-1] if reports else {}
    verdict=last.get('overall_verdict','INCONCLUSIVE')
    if reason!='ALL_POINTS_COMPLETED' and verdict=='PASS':verdict='INCONCLUSIVE'
    emit({'schema':'prosperolight-bitrate-sweep/1','overall_verdict':verdict,'verdict':verdict,'summary_verdict':verdict,
          'execution_state':'COMPLETED' if verdict=='PASS' else 'FAILED' if verdict=='FAIL' else 'INCONCLUSIVE',
          'functional_verdict':last.get('functional_verdict',(last.get('analysis') or {}).get('functional_verdict','INCONCLUSIVE')),
          'measurement_validity':'VALID' if verdict=='PASS' else 'INCONCLUSIVE','reason_code':reason,
          'run_ids':[r['run_id'] for r in reports],'requested_mbps':[v['client']['bitrate_mbps'] for v in values],
          'requested_points':len(values),'completed_points':len(reports),'active_run_id':active_run_id,
          'completed_mbps':[v['client']['bitrate_mbps'] for v in values[:len(reports)]],
          'sweep_outcome':('COMPLETED' if reason=='ALL_POINTS_COMPLETED' else 'SATURATION_DETECTED' if reason=='THROUGHPUT_PLATEAU_WITH_FPS_DROP' else 'INFRASTRUCTURE_FAILURE' if reason in ('POINT_INFRASTRUCTURE_FAILURE','SWEEP_POINT_TIMEOUT','SWEEP_API_FAILURE') else 'FUNCTIONAL_FAILURE' if reason=='POINT_FUNCTIONAL_FAILURE' else 'INVALID_MEASUREMENT'),
          'performance_delta':'NOT_COMPARABLE',
          'limitations':['plateau is screening evidence, not proof of encoder saturation; each point retains its own report']})
    raise typer.Exit(2 if infrastructure_error else exit_code(verdict))
