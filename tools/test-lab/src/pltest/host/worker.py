# ps5-native-app-boilerplate - Windows interactive worker with measured Ready.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import asyncio
import hashlib
import json
import secrets
import time
from pathlib import Path
from . import display
from .protocol import SCHEMA, request, readiness, validate_report, raf_rate_reason
from .browser import ManagedBrowser
from .metrics import GpuSampler
from .logs import LogSlice, server_inventory

STATIC = Path(__file__).resolve().parent / 'static'


class Worker:
    def __init__(self, token, output, mode='managed', port=8096, host_log=None, browser_channel=None):
        self.token, self.output, self.mode, self.port = token, Path(output), mode, port
        self.runtime_identity = {'schema':'prosperolight-worker-runtime/1',
            'source':'resolved worker package source files at process startup',
            'semantics':'on-disk source hashes; not process-memory attestation',
            'files':{name:hashlib.sha256((Path(__file__).parent/name).read_bytes()).hexdigest()
                     for name in ('worker.py','protocol.py','display.py','advanced_color.py','metrics.py','browser.py','logs.py')}}
        self.page_token = secrets.token_urlsafe(32)
        self.host_log = host_log
        self.browser_channel = browser_channel
        self.browser_info = {'mode': mode, 'channel': (browser_channel or 'bundled Chromium') if mode == 'managed' else None,
                             'version': None, 'version_source': 'managed browser' if mode == 'managed' else 'page user_agent only'}
        self.page = self.control = None
        self.browser = None
        self.state = 'IDLE'
        self.run_id = self.settings = self.monitor = self.folder = None
        self.ready_event = asyncio.Event()
        self.prepared_event = asyncio.Event()
        self.report_event = asyncio.Event()
        self.page_event = asyncio.Event()
        self.report = self.ready = None
        self.samples = []
        self.events = []
        self.commands = {}
        self.deadline = None
        self.last_page_heartbeat = 0
        self.gpu = None
        self.logs = None
        self.shutdown = asyncio.Event()
        self.finalizing = False
        self.latest_page = None
        self.server = None

    def snapshot(self):
        return {'schema': SCHEMA, 'run_id': self.run_id, 'state': self.state,
                'mode': self.mode, 'ready': self.ready, 'report': self.report,
                'monitor': self.monitor, 'interactive_desktop': display.probe(),
                'worker_runtime_identity': self.runtime_identity,
                'worker_module_path': str(Path(__file__).resolve()),
                'host_monotonic_ns': time.monotonic_ns()}

    def event(self, kind, detail=None):
        if len(self.events) < 4096:
            self.events.append({'type': kind, 'host_monotonic_ns': time.monotonic_ns(), 'detail': detail})

    async def prepare(self, value):
        if self.state not in ('IDLE', 'COMPLETED', 'INCONCLUSIVE'):
            raise RuntimeError('HOST_LEASE_BUSY')
        if self.run_id == value['run_id']:
            raise RuntimeError('RUN_ID_REPLAY')
        s = value['settings']
        inventory = display.probe()
        monitor = display.select(inventory, s['monitor'])
        if s.get('require_hdr'):
            if monitor.get('hdr_verified') is not True:
                raise RuntimeError('HDR_CAPABILITY_UNVERIFIED')
            if monitor.get('hdr') is not True:
                raise RuntimeError('UNSUPPORTED_HDR_DISPLAY')
            if monitor.get('hdr_active') is not True:
                raise RuntimeError('UNSUPPORTED_HDR_CURRENT_DISPLAY_MODE')
        if not 320 <= monitor['width'] <= 3840 or not 240 <= monitor['height'] <= 2160:
            raise RuntimeError('UNSUPPORTED_DISPLAY_RESOLUTION')
        folder = self.output / value['run_id']
        folder.mkdir(parents=True, exist_ok=False)
        self.run_id, self.folder, self.monitor = value['run_id'], folder, monitor
        self.settings = dict(s, run_id=self.run_id, width=monitor['width'], height=monitor['height'],
                             warmup_seconds=s.get('warmup_seconds', 0),
                             display_refresh_hz=monitor.get('refresh_hz'))
        self.ready = self.report = None
        self.start_ack = None
        self.page_error = None
        self.commands.clear()
        self.finalizing = False
        self.latest_page = None
        self.events, self.samples = [], []
        self.ready_event.clear(); self.prepared_event.clear(); self.report_event.clear()
        self.state = 'PREPARING'
        self.deadline = time.monotonic() + s['seconds'] + s.get('warmup_seconds', 0) + 90
        self.logs = LogSlice(self.host_log)
        self.server = await asyncio.to_thread(server_inventory)
        self.gpu = GpuSampler()
        hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in STATIC.iterdir() if p.is_file()}
        self.content_hash = hashlib.sha256(json.dumps(hashes, sort_keys=True).encode()).hexdigest()
        (folder / 'request.json').write_text(json.dumps({'schema': SCHEMA, 'run_id': self.run_id,
            'settings': self.settings, 'display': monitor, 'desktop': inventory,
            'content_sha256': self.content_hash, 'assets': hashes}, indent=2), encoding='utf-8')
        if self.mode == 'managed':
            self.browser = ManagedBrowser(self.browser_channel)
            await self.browser.open(f'http://127.0.0.1:{self.port}/', monitor)
            self.browser_info['version'] = self.browser.version
        await asyncio.wait_for(self.page_event.wait(), 30)
        await self.page.send_json({'action': 'prepare', 'settings': self.settings})
        await asyncio.wait_for(self.prepared_event.wait(), 30)
        if self.page_error:
            raise RuntimeError(self.page_error)
        if self.browser:
            await self.browser.ready()
        await asyncio.wait_for(self.ready_event.wait(), 45)
        if self.page_error:
            raise RuntimeError(self.page_error)
        self.event('ready', self.ready)
        if not self.ready['ready']:
            raise RuntimeError(','.join(self.ready['reasons']))
        self.state = 'READY'

    async def start(self):
        if self.state != 'READY' or not self.page:
            raise RuntimeError('HOST_NOT_READY')
        if not self.latest_page or time.monotonic() - self.last_page_heartbeat > 3 or not readiness(
                self.latest_page, self.monitor, self.settings['fps'], display.probe())['ready']:
            raise RuntimeError('READY_EXPIRED')
        self.state = 'STARTING'
        self.started_event = asyncio.Event()
        import uuid
        self.page_start_command = str(uuid.uuid4())
        await self.page.send_json({'action': 'start', 'command_id': self.page_start_command})
        await asyncio.wait_for(self.started_event.wait(), 5)
        if self.page_error:
            raise RuntimeError(self.page_error)
        if self.state == 'STARTING':
            self.state = 'RUNNING'
            self.deadline = time.monotonic() + self.settings['seconds'] + self.settings['warmup_seconds'] + 10
        self.event('started')

    async def clock_probe(self, command_id):
        if not self.page or self.state != 'RUNNING':
            raise RuntimeError('CLOCK_PROBE_NOT_RUNNING')
        self.clock_probe_id = command_id
        self.clock_probe_ack = None
        self.clock_probe_event = asyncio.Event()
        await self.page.send_json({'action': 'clock_probe', 'command_id': command_id})
        await asyncio.wait_for(self.clock_probe_event.wait(), 5)
        if self.page_error or not self.clock_probe_ack:
            raise RuntimeError('CLOCK_PROBE_ACK_UNAVAILABLE')
        return self.clock_probe_ack

    async def stop(self, reason='STOP_REQUESTED'):
        if self.state == 'IDLE' or self.finalizing or (self.state in ('COMPLETED', 'INCONCLUSIVE') and self.report):
            return
        self.event('stop_requested', reason)
        if self.page and not self.report_event.is_set():
            try:
                await self.page.send_json({'action': 'stop'})
                await asyncio.wait_for(self.report_event.wait(), 5)
            except (ConnectionError, asyncio.TimeoutError):
                pass
        await self.finalize(reason)

    async def finalize(self, reason):
        if self.finalizing or self.state == 'COMPLETED':
            return
        self.finalizing = True
        report = self.report or {'reason': reason, 'measurement_validity': 'INCONCLUSIVE'}
        reasons = []
        rate_reason = raf_rate_reason(report.get('raf_fps'), self.monitor or {}, self.settings['fps'])
        if rate_reason: reasons.append(rate_reason)
        if report.get('reason') != 'COMPLETE': reasons.append('HOST_REPORT_NOT_COMPLETE')
        if sum(report.get('intervals_ms', [])) < self.settings['seconds'] * 1000 * .95:
            reasons.append('HOST_MEASUREMENT_TOO_SHORT')
        if report.get('hidden_events'): reasons.append('BROWSER_HIDDEN')
        if report.get('fullscreen_lost'): reasons.append('FULLSCREEN_LOST')
        if report.get('interval_overflow'): reasons.append('INTERVAL_OVERFLOW')
        if not self.ready or not self.ready['ready']: reasons.append('HOST_NOT_READY')
        valid = not reasons
        final_state = 'COMPLETED' if valid else 'INCONCLUSIVE'
        try:
            logs = self.logs.finish(self.folder / 'host-log.txt')
        except OSError as exc:
            logs = {'supported': False, 'reason': type(exc).__name__}
        report = dict(report, schema=SCHEMA, run_id=self.run_id,
                      host_termination_reason=reason,
                      measurement_validity='VALID' if valid else 'INCONCLUSIVE',
                      validity_reasons=reasons, worker_runtime_identity=self.runtime_identity,
                      content_sha256=self.content_hash, selected_display=self.monitor,
                      browser=self.browser_info,
                      gpu_samples=self.samples, events=self.events,
                      host_logs=logs)
        report['host_server'] = self.server
        cleanup_errors = []
        if self.gpu:
            try:
                self.gpu.close()
            except Exception as exc:
                cleanup_errors.append('gpu:' + type(exc).__name__)
            self.gpu = None
        if self.browser:
            try:
                await asyncio.wait_for(self.browser.close(), 10)
            except Exception as exc:
                cleanup_errors.append('browser:' + type(exc).__name__)
            self.browser = None
        report['resource_cleanup'] = {'complete': not cleanup_errors, 'errors': cleanup_errors}
        if cleanup_errors:
            final_state = 'INCONCLUSIVE'
            report['measurement_validity'] = 'INCONCLUSIVE'
        self.report = report
        temporary = self.folder / 'host-report.tmp'
        temporary.write_text(json.dumps(report, indent=2), encoding='utf-8')
        temporary.replace(self.folder / 'host-report.json')
        self.state = final_state
        self.deadline = None

    async def watchdog(self):
        while not self.shutdown.is_set():
            await asyncio.sleep(1)
            if self.state in ('PREPARING', 'READY', 'STARTING', 'RUNNING'):
                if self.gpu and len(self.samples) < 2000:
                    self.samples.append(self.gpu.sample())
                if not display.probe()['interactive']:
                    await self.stop('DESKTOP_LOCKED')
                elif self.deadline and time.monotonic() >= self.deadline:
                    await self.stop('HOST_HARD_TIMEOUT')
                elif self.state == 'RUNNING' and time.monotonic() - self.last_page_heartbeat > 5:
                    await self.stop('PAGE_HEARTBEAT_LOST')
            if self.control:
                try:
                    await self.control.send_json({'schema': SCHEMA, 'event': 'heartbeat',
                                                   'run_id': self.run_id, 'state': self.state})
                except ConnectionError:
                    pass

    def app(self):
        from aiohttp import web, WSMsgType
        app = web.Application(client_max_size=2 * 1024 * 1024)
        def authorized(req):
            return secrets.compare_digest(req.headers.get('Authorization', ''), 'Bearer ' + self.token)
        async def health(req):
            if not authorized(req):
                raise web.HTTPUnauthorized()
            return web.json_response(self.snapshot())
        async def shutdown(req):
            if not authorized(req) or req.headers.get('Origin'):
                raise web.HTTPForbidden()
            self.shutdown.set()
            return web.json_response({'schema': SCHEMA, 'state': 'SHUTTING_DOWN'})
        async def index(req):
            return web.FileResponse(STATIC / 'stimulus.html')
        async def page_token(req):
            if req.remote not in ('127.0.0.1', '::1') or req.headers.get('Sec-Fetch-Site') != 'same-origin':
                raise web.HTTPForbidden()
            return web.json_response({'token': self.page_token}, headers={'Cache-Control': 'no-store'})
        async def assets(req):
            name = req.match_info['name']
            if name not in ('patterns.js', 'gpu-scene.js', 'stimulus.js'):
                raise web.HTTPNotFound()
            return web.FileResponse(STATIC / name)
        async def page_ws(req):
            if req.remote not in ('127.0.0.1', '::1') or req.headers.get('Origin') != f'http://127.0.0.1:{self.port}':
                raise web.HTTPForbidden()
            ws = web.WebSocketResponse(heartbeat=5, max_msg_size=8 * 1024 * 1024)
            await ws.prepare(req)
            try:
                auth = await ws.receive_json(timeout=5)
                if not isinstance(auth, dict) or not secrets.compare_digest(str(auth.get('token', '')), self.page_token) or self.page:
                    await ws.close(code=1008); return ws
                self.page = ws; self.page_event.set()
                async for message in ws:
                    if message.type != WSMsgType.TEXT:
                        continue
                    data = json.loads(message.data)
                    if not isinstance(data, dict):
                        raise ValueError('invalid page event')
                    kind = data.get('type')
                    if data.get('run_id') != self.run_id:
                        raise ValueError('page run identity mismatch')
                    if kind == 'prepared':
                        self.prepared_event.set()
                    elif kind == 'ready':
                        self.ready = readiness(data['snapshot'], self.monitor, self.settings['fps'], display.probe())
                        self.ready_event.set()
                    elif kind == 'heartbeat':
                        self.last_page_heartbeat = time.monotonic()
                        self.latest_page = data['snapshot']
                        if self.state == 'RUNNING':
                            gate = readiness(data['snapshot'], self.monitor, self.settings['fps'], display.probe())
                            if not gate['ready']:
                                asyncio.create_task(self.stop(','.join(gate['reasons'])))
                    elif kind == 'started' and data.get('run_id') == self.run_id:
                        if data.get('command_id') != self.page_start_command:
                            raise ValueError('PAGE_START_ACK_IDENTITY_MISMATCH')
                        self.start_ack = data
                        self.started_event.set()
                    elif kind == 'clock_probe':
                        if data.get('command_id') != getattr(self, 'clock_probe_id', None):
                            raise ValueError('CLOCK_PROBE_ACK_IDENTITY_MISMATCH')
                        self.clock_probe_ack = data
                        self.clock_probe_event.set()
                    elif kind == 'report' and data.get('run_id') == self.run_id:
                        self.report = validate_report(data); self.report_event.set()
                        if self.state in ('STARTING', 'RUNNING'):
                            asyncio.create_task(self.finalize('COMPLETE'))
                    elif kind == 'error':
                        self.page_error = str(data.get('reason'))[:256]
                        self.event('page_error', self.page_error)
                        self.prepared_event.set()
                        self.ready_event.set()
                        if hasattr(self, 'started_event'):
                            self.started_event.set()
            finally:
                if self.page is ws:
                    self.page = None; self.page_event.clear()
                    if self.state in ('READY', 'STARTING', 'RUNNING'):
                        asyncio.create_task(self.stop('PAGE_DISCONNECTED'))
            return ws
        async def control_ws(req):
            if not authorized(req) or req.headers.get('Origin') or self.control:
                raise web.HTTPForbidden()
            ws = web.WebSocketResponse(heartbeat=5, max_msg_size=16384)
            await ws.prepare(req); self.control = ws
            try:
                async for message in ws:
                    if message.type != WSMsgType.TEXT:
                        continue
                    value = {}
                    try:
                        value = request(json.loads(message.data)); key = value['command_id']
                        if key in self.commands:
                            prior, ack = self.commands[key]
                            if prior != value:
                                raise RuntimeError('COMMAND_ID_REUSE')
                            await ws.send_json(ack); continue
                        if len(self.commands) >= 64 and value['action'] not in ('status', 'stop'):
                            raise RuntimeError('COMMAND_QUOTA')
                        if value['action'] != 'prepare' and value['run_id'] != self.run_id:
                            raise RuntimeError('RUN_ID_MISMATCH')
                        if value['action'] == 'prepare':
                            await self.prepare(value)
                        elif value['action'] == 'start':
                            if self.state != 'RUNNING':
                                await self.start()
                        elif value['action'] == 'clock_probe':
                            await self.clock_probe(key)
                        elif value['action'] == 'stop':
                            await self.stop()
                        ack = {'schema': SCHEMA, 'command_id': key, 'run_id': value['run_id'],
                               'ack': 'accepted', 'state': self.state, 'ready': self.ready, 'report': self.report,
                               'browser_start': getattr(self, 'start_ack', None),
                               'browser_clock': getattr(self, 'clock_probe_ack', None)}
                        if value['action'] != 'status' and len(self.commands) < 65:
                            self.commands[key] = (value, ack)
                        await ws.send_json(ack)
                    except Exception as exc:
                        if self.browser:
                            observed = await self.browser.diagnostics()
                            (self.folder / 'browser-diagnostics.json').write_text(json.dumps(observed, indent=2), encoding='utf-8')
                        await ws.send_json({'schema': SCHEMA, 'command_id': value.get('command_id') if 'value' in locals() else None,
                                            'ack': 'rejected', 'reason': (str(exc) or type(exc).__name__)[:512],
                                            'reason_code': type(exc).__name__})
                        if self.state == 'PREPARING':
                            await self.stop('PREPARATION_FAILED')
            finally:
                self.control = None
                if self.state in ('PREPARING', 'READY', 'STARTING', 'RUNNING'):
                    await self.stop('CONTROL_DISCONNECTED')
            return ws
        app.router.add_get('/', index)
        app.router.add_get('/assets/{name}', assets)
        app.router.add_get('/page-token', page_token)
        app.router.add_get('/page', page_ws)
        app.router.add_get('/health', health)
        app.router.add_get('/ws', control_ws)
        app.router.add_post('/shutdown', shutdown)
        return app


async def serve(args):
    from aiohttp import web
    token_path = Path(args.token_file)
    if token_path.exists():
        token = token_path.read_text().strip()
    else:
        token_path.parent.mkdir(parents=True, exist_ok=True)
        token = secrets.token_urlsafe(32)
        with token_path.open('x') as f:
            f.write(token)
        token_path.chmod(0o600)
    if len(token) < 32:
        raise ValueError('worker token must contain at least 32 characters')
    worker = Worker(token, args.output, args.mode, args.port, args.host_log, args.browser_channel)
    runner = web.AppRunner(worker.app(), access_log=None)
    await runner.setup(); await web.TCPSite(runner, args.bind, args.port).start()
    if args.bind not in ('127.0.0.1', '0.0.0.0'):
        await web.TCPSite(runner, '127.0.0.1', args.port).start()
    task = asyncio.create_task(worker.watchdog())
    print(json.dumps({'schema': SCHEMA, 'state': 'LISTENING', 'mode': args.mode,
                      'port': args.port, 'operator_url': f'http://127.0.0.1:{args.port}/',
                      'desktop': display.probe()}), flush=True)
    try:
        try:
            await asyncio.wait_for(worker.shutdown.wait(), args.duration)
        except asyncio.TimeoutError:
            pass
    finally:
        worker.shutdown.set(); task.cancel()
        await worker.stop('WORKER_SHUTDOWN')
        await runner.cleanup()


def main(argv=None):
    p = argparse.ArgumentParser(description='Run inside an unlocked Windows interactive desktop')
    p.add_argument('--bind', default='127.0.0.1')
    p.add_argument('--port', type=int, default=8096)
    p.add_argument('--mode', choices=('managed', 'attached'), default='managed')
    p.add_argument('--token-file', required=True)
    p.add_argument('--output', required=True)
    p.add_argument('--host-log')
    p.add_argument('--browser-channel', choices=('chrome', 'msedge'), help='explicit installed browser alternative')
    p.add_argument('--duration', type=int, default=3600, help='bounded worker lifetime in seconds')
    args = p.parse_args(argv)
    if not 30 <= args.duration <= 86400 or not 1024 <= args.port <= 65535:
        raise ValueError('invalid worker duration/port')
    inventory = display.probe()
    if not inventory['interactive']:
        print(json.dumps(inventory)); return 2
    try:
        asyncio.run(serve(args))
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
