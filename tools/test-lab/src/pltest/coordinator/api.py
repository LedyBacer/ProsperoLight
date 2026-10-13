# ps5-native-app-boilerplate - versioned local coordinator REST API.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
from contextlib import asynccontextmanager
from pathlib import Path
import os
import secrets

from fastapi import FastAPI, Header, HTTPException, Request, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, JSONResponse
from starlette.background import BackgroundTask

from .models import CompareRequest, RunCreate, RunView
from .scheduler import Scheduler
from .store import RunStore
from ..suites import REAL_SUITES
from ..comparison import compare_pairs

SUITES = ['fake-smoke']


def create_app(data_dir=None, scheduler=None, token=None):
    data_dir = Path(data_dir or os.environ.get('PLTEST_DATA_DIR', 'build/test-lab-coordinator'))
    store = scheduler.store if scheduler else RunStore(data_dir)
    configured_mode = os.environ.get('PLTEST_API_MODE', 'fake')
    if configured_mode not in ('fake', 'real'):
        raise ValueError('PLTEST_API_MODE must be fake or real')
    if scheduler is None and configured_mode == 'real':
        scheduler = _real_scheduler(store, data_dir)
    scheduler = scheduler or Scheduler(store)
    configured_mode = scheduler.execution_mode
    token = token if token is not None else os.environ.get('PLTEST_API_TOKEN')
    if token and len(token) < 32:
        raise ValueError('PLTEST_API_TOKEN must contain at least 32 characters')

    @asynccontextmanager
    async def lifespan(app):
        await scheduler.start_recovery()
        yield
        await scheduler.close()

    app = FastAPI(title='ProsperoLight Test Lab Coordinator', version='1.0.0', lifespan=lifespan)
    app.state.scheduler = scheduler
    app.state.store = store
    app.state.api_token = token

    allowed_hosts = set(os.environ.get('PLTEST_ALLOWED_HOSTS', '127.0.0.1,localhost,::1,testserver').split(','))
    allowed_origins = set(filter(None, os.environ.get('PLTEST_ALLOWED_ORIGINS', '').split(',')))
    static = Path(__file__).with_name('static')
    public = {'/', '/dashboard/app.js', '/dashboard/style.css', '/api/v1/health'}

    @app.middleware('http')
    async def require_api_token(request: Request, call_next):
        if request.url.hostname not in allowed_hosts:
            return JSONResponse(status_code=400, content={'detail': {'code': 'HOST_NOT_ALLOWED'}})
        origin = request.headers.get('origin')
        same_origin = str(request.base_url).rstrip('/')
        if origin and origin not in allowed_origins and origin != same_origin:
            return JSONResponse(status_code=403, content={'detail': {'code': 'ORIGIN_NOT_ALLOWED'}})
        if request.headers.get('sec-fetch-site') == 'cross-site':
            return JSONResponse(status_code=403, content={'detail': {'code': 'CROSS_SITE_REQUEST'}})
        if token and request.url.path not in public and not authorized(request.headers.get('authorization')):
            return JSONResponse(status_code=401, content={'detail': {'code': 'UNAUTHORIZED'}})
        response = await call_next(request)
        response.headers['Cache-Control'] = 'no-store'
        response.headers['X-Content-Type-Options'] = 'nosniff'
        response.headers['Content-Security-Policy'] = "default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'"
        return response

    @app.get('/', include_in_schema=False)
    async def dashboard(): return FileResponse(static / 'index.html', media_type='text/html')

    @app.get('/dashboard/{name}', include_in_schema=False)
    async def dashboard_asset(name: str):
        if name not in ('app.js', 'style.css'): raise HTTPException(404)
        return FileResponse(static / name, media_type='text/javascript' if name.endswith('.js') else 'text/css')

    def authorized(value):
        return token is None or secrets.compare_digest(value or '', 'Bearer ' + token)

    def run_or_404(run_id):
        value = store.get(run_id)
        if value is None: raise HTTPException(404, detail={'code': 'RUN_NOT_FOUND'})
        return value

    @app.get('/api/v1/health')
    async def health():
        return {'schema_version': 'prosperolight-test-lab-api/1', 'status': 'ok'}

    @app.get('/api/v1/capabilities')
    async def capabilities():
        from ..build import capabilities as build_capabilities
        return {'schema_version': 'prosperolight-test-lab-api/1', 'coordinator': True,
                'api_mode': 'fake-adapters-only' if configured_mode == 'fake' else 'real',
                'build': build_capabilities(),
                'ps5_adapter': configured_mode, 'host_adapter': configured_mode,
                'max_health_poll_seconds': 2}

    @app.get('/api/v1/suites')
    async def suites():
        return {'schema_version': 'prosperolight-test-lab-api/1',
                'items': list(REAL_SUITES) if configured_mode == 'real' else SUITES}

    @app.post('/api/v1/runs', response_model=RunView, status_code=201)
    async def create_run(body: RunCreate):
        available_suites = list(REAL_SUITES) if configured_mode == 'real' else SUITES
        if body.suite not in available_suites:
            raise HTTPException(422, detail={'code': 'UNKNOWN_SUITE'})
        if body.mode != configured_mode:
            raise HTTPException(422, detail={'code': 'ADAPTER_MODE_UNAVAILABLE',
                                             'message': 'requested mode does not match the configured coordinator adapters'})
        try:
            run = await scheduler.create(body.suite, body.profile)
        except ValueError as exc:
            raise HTTPException(422, detail={'code': 'INVALID_PROFILE', 'message': str(exc)}) from exc
        return _view(run)

    @app.get('/api/v1/runs')
    async def recent_runs(limit: int = 50):
        if not 1 <= limit <= 200: raise HTTPException(422, detail={'code':'INVALID_LIMIT'})
        with store.connect() as c:
            rows=c.execute('SELECT run_id,state,stage,verdict,created FROM runs ORDER BY created DESC LIMIT ?', (limit,)).fetchall()
        items = []
        for row in rows:
            run = store.get(row['run_id'])
            items.append({**dict(row), **{key: run.get(key) for key in
                ('execution_state', 'functional_verdict', 'measurement_validity',
                 'performance_delta', 'overall_verdict')}})
        return {'schema_version':'prosperolight-test-lab-api/1', 'items':items}

    @app.get('/api/v1/doctor')
    async def doctor():
        from ..doctor import check
        import asyncio
        return await asyncio.to_thread(check, scheduler)

    @app.get('/api/v1/runs/{run_id}/bundle')
    async def run_bundle(run_id: str, redact: bool = True):
        from ..export import bundle
        from .store import TERMINAL
        run=run_or_404(run_id)
        if run['state'] not in TERMINAL: raise HTTPException(409, detail={'code':'RUN_NOT_TERMINAL'})
        try:
            import asyncio
            temporary,path=await asyncio.to_thread(bundle,store,run_id,redact)
        except ValueError as exc:
            raise HTTPException(409 if str(exc).startswith('ARTIFACT_MANIFEST') else 413,detail={'code':'EXPORT_UNAVAILABLE','message':str(exc)}) from exc
        return FileResponse(path,filename=run_id+'.zip',media_type='application/zip',background=BackgroundTask(temporary.cleanup))

    @app.get('/api/v1/runs/{run_id}', response_model=RunView)
    async def get_run(run_id: str): return _view(run_or_404(run_id))

    @app.post('/api/v1/runs/{run_id}/start', response_model=RunView)
    async def start_run(run_id: str):
        run = await scheduler.start(run_id)
        if run is None: raise HTTPException(404, detail={'code': 'RUN_NOT_FOUND'})
        return _view(run)

    @app.post('/api/v1/runs/{run_id}/stop', response_model=RunView)
    async def stop_run(run_id: str):
        run = await scheduler.stop(run_id)
        if run is None: raise HTTPException(404, detail={'code': 'RUN_NOT_FOUND'})
        return _view(run)

    @app.get('/api/v1/runs/{run_id}/events')
    async def events(run_id: str, after: int = 0):
        if after < 0: raise HTTPException(422, detail={'code': 'INVALID_CURSOR'})
        values = store.list_events(run_id, after)
        if values is None: raise HTTPException(404, detail={'code': 'RUN_NOT_FOUND'})
        return {'schema_version': 'prosperolight-test-lab-api/1', 'items': values, 'next': values[-1]['seq'] if values else after}

    @app.get('/api/v1/runs/{run_id}/report')
    async def report(run_id: str):
        run = run_or_404(run_id)
        if run['report'] is None: raise HTTPException(409, detail={'code': 'REPORT_NOT_READY'})
        return run['report']

    @app.get('/api/v1/runs/{run_id}/artifacts')
    async def artifacts(run_id: str):
        values = store.files(run_id)
        if values is None: raise HTTPException(404, detail={'code': 'RUN_NOT_FOUND'})
        return {'schema_version': 'prosperolight-test-lab-api/1', 'items': values}

    @app.get('/api/v1/runs/{run_id}/artifacts/{name:path}')
    async def artifact(run_id: str, name: str):
        path = store.artifact_path(run_id, name)
        if path is None: raise HTTPException(404, detail={'code': 'ARTIFACT_NOT_FOUND'})
        return FileResponse(path, filename=path.name)

    @app.post('/api/v1/compare')
    async def compare(body: CompareRequest):
        pairs = body.pairs or [(body.baseline, body.candidate)]
        reference = ([(run_or_404(a), run_or_404(b)) for a,b in body.calibration_pairs]
                     if body.calibration_pairs is not None else None)
        return compare_pairs([(run_or_404(a), run_or_404(b)) for a, b in pairs], body.calibration, body.screening, reference)

    @app.websocket('/api/v1/ws')
    async def worker_ws(ws: WebSocket):
        origin = ws.headers.get('origin')
        expected = ('https' if ws.url.scheme == 'wss' else 'http') + '://' + ws.url.netloc
        if ws.url.hostname not in allowed_hosts or (origin and origin != expected and origin not in allowed_origins):
            await ws.close(code=4403); return
        if not authorized(ws.headers.get('authorization')):
            await ws.close(code=4401); return
        await ws.accept()
        try:
            await ws.send_json({'schema_version': 'prosperolight-test-lab-api/1', 'type': 'connected'})
            while True:
                raw = await ws.receive_text()
                if len(raw.encode('utf-8')) > 16 * 1024:
                    await ws.send_json({'type': 'error', 'code': 'MESSAGE_TOO_LARGE'}); continue
                import json
                try: message = json.loads(raw)
                except json.JSONDecodeError:
                    await ws.send_json({'type': 'error', 'code': 'INVALID_JSON'}); continue
                if not isinstance(message, dict) or message.get('schema_version') != 'prosperolight-test-lab-api/1':
                    await ws.send_json({'type': 'error', 'code': 'SCHEMA_MISMATCH'}); continue
                await ws.send_json({'type': 'ack', 'event_id': message.get('event_id'), 'accepted': True})
        except WebSocketDisconnect:
            return

    return app


def _real_scheduler(store, data_dir):
    """Load production endpoints from environment; never accept secrets in profiles."""
    required = ('PLTEST_PS5_HOST', 'PLTEST_PS5_PROBE_ELF', 'PLTEST_PS5_LAUNCH_ELF',
                'PLTEST_PS5_EBOOT_SHA256', 'PLTEST_HOST_WORKER_URL', 'PLTEST_HOST_WORKER_TOKEN_FILE')
    missing = [name for name in required if not os.environ.get(name)]
    if missing:
        raise ValueError('real adapter configuration is incomplete: ' + ', '.join(missing))
    token_path = Path(os.environ['PLTEST_HOST_WORKER_TOKEN_FILE'])
    if not token_path.is_file() or token_path.stat().st_size > 4096:
        raise ValueError('host worker token file is unavailable or oversized')
    worker_token = token_path.read_text(encoding='utf-8').strip()
    from .adapters import RealHostCoordinatorAdapter, RealPs5CoordinatorAdapter
    ps5 = RealPs5CoordinatorAdapter(
        os.environ['PLTEST_PS5_HOST'], os.environ['PLTEST_PS5_PROBE_ELF'],
        os.environ['PLTEST_PS5_LAUNCH_ELF'], os.environ['PLTEST_PS5_EBOOT_SHA256'],
        store.artifacts, build_root=os.environ.get('PLTEST_BUILD_ROOT'))
    guard = None
    if os.environ.get('PLTEST_HOST_API_URL'):
        from ..host.session_guard import SessionGuard
        path = Path(os.environ['PLTEST_HOST_API_TOKEN_FILE'])
        if not path.is_file() or path.stat().st_size > 4096: raise ValueError('host API token file unavailable/oversized')
        guard = SessionGuard(os.environ['PLTEST_HOST_API_URL'], path.read_text().strip(),
            os.environ.get('PLTEST_HOST_SESSION_POLICY','reject'), os.environ.get('PLTEST_HOST_API_CA_FILE'),
            os.environ.get('PLTEST_HOST_API_CERT_SHA256'))
    host = RealHostCoordinatorAdapter(os.environ['PLTEST_HOST_WORKER_URL'], worker_token, guard)
    return Scheduler(store, ps5=ps5, host=host)


def _view(run):
    return {'schema_version': 'prosperolight-test-lab-api/1', 'run_id': run['run_id'],
            'state': run['state'], 'stage': run['stage'], 'verdict': run['verdict'],
            'reason_code': run['reason_code'], 'report': run['report'],
            **{key: run.get(key) for key in ('execution_state', 'functional_verdict',
                'measurement_validity', 'performance_delta', 'overall_verdict')}}
