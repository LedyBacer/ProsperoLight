# ps5-native-app-boilerplate - coordinator API, durability and lease tests.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import json
from pathlib import Path
import threading
import tempfile
import time
import unittest
from unittest.mock import patch

from fastapi.testclient import TestClient
from typer.testing import CliRunner

ROOT = Path(__file__).resolve().parents[1]
import sys
sys.path[:0] = [str(ROOT / 'tools/test-lab/src'), str(ROOT / 'tools/test-lab')]
from pltest.coordinator.api import create_app
from pltest.coordinator.cli import app as cli_app
from pltest.coordinator.scheduler import Scheduler
from pltest.coordinator.store import RunStore
from pltest.coordinator.adapters import RealPs5CoordinatorAdapter


def profile(**extra):
    return {'schema': 'prosperolight-test/1', 'build_label': 'test-lab-fake',
            'client': {'host': 'fake'}, 'timing': {'hard_seconds': 30}, **extra}


class CoordinatorTests(unittest.TestCase):
    def test_host_transport_answers_pings_during_idle_ps5_stage(self):
        import asyncio
        from aiohttp import web
        from pltest.host.client import HostWorkerAdapter
        from pltest.host.protocol import SCHEMA
        async def exercise():
            async def peer(request):
                ws = web.WebSocketResponse(heartbeat=.2)
                await ws.prepare(request)
                async for message in ws:
                    value = message.json()
                    await ws.send_json({'schema': SCHEMA, 'command_id': value['command_id'],
                                        'run_id': value['run_id'], 'ack': 'accepted', 'state': 'READY'})
                return ws
            app = web.Application(); app.router.add_get('/ws', peer)
            runner = web.AppRunner(app); await runner.setup()
            site = web.TCPSite(runner, '127.0.0.1', 0); await site.start()
            port = site._server.sockets[0].getsockname()[1]
            try:
                async with HostWorkerAdapter('http://127.0.0.1:' + str(port), 't' * 48) as client:
                    await client.command('run', 'status')
                    await asyncio.sleep(.8)  # More than two peer heartbeat deadlines.
                    results = await asyncio.gather(client.command('run', 'status'), client.command('run', 'status'))
                    self.assertEqual([r['ack'] for r in results], ['accepted', 'accepted'])
                    self.assertFalse(client.pending)
            finally:
                await runner.cleanup()
        asyncio.run(exercise())

    def test_real_collect_returns_evidence_to_scheduler(self):
        import asyncio
        adapter = RealPs5CoordinatorAdapter.__new__(RealPs5CoordinatorAdapter)
        current = {'state': {}, 'safe': False}
        adapter.runs = {'run': current}; adapter._save = lambda current: None
        evidence = {'receipt': {'functional_verdict': 'PASS'},
                    'title_exit_verified': True, 'stale_artifacts': []}
        with patch.object(adapter, '_collect', return_value=evidence):
            result = asyncio.run(adapter.stage('COLLECT', {'run_id': 'run'}, asyncio.Event()))
        self.assertEqual(result, evidence)
        self.assertTrue(current['safe'])

    def test_profile_structure_limit_and_cli_remote_mode_detection(self):
        from pltest.coordinator.models import RunCreate
        from pltest.coordinator.adapters import RealHostCoordinatorAdapter
        with self.assertRaises(ValueError):
            RealHostCoordinatorAdapter('http://user:password@localhost:8096', 't' * 48)
        with self.assertRaises(ValueError):
            RunCreate(suite='fake-smoke', profile=profile(rikey='do-not-persist'))
        deep = {}
        value = deep
        for _ in range(10):
            value['nested'] = {}; value = value['nested']
        with self.assertRaises(ValueError):
            RunCreate(suite='fake-smoke', profile=profile(environment=deep))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'profile.json'; path.write_text(json.dumps(profile()))
            def transport(method, route, **kwargs):
                if route.endswith('capabilities'): return {'api_mode': 'real'}
                if route == '/api/v1/runs':
                    self.assertEqual(kwargs['json']['mode'], 'real')
                    self.assertEqual(kwargs['json']['suite'], 'decoder-smoke')
                    return {'run_id': 'run'}
                return {'state': 'CREATED'}
            with patch.dict('os.environ', {}, clear=True), patch('pltest.coordinator.cli.request', side_effect=transport):
                result = CliRunner().invoke(cli_app, ['run', '--profile', str(path), '--no-wait'])
            self.assertEqual(result.exit_code, 0, result.output)

    def test_two_coordinators_cannot_fail_or_release_same_active_run(self):
        import asyncio
        async def exercise(directory):
            store = RunStore(directory)
            a = Scheduler(store); b = Scheduler(RunStore(directory))
            run = await a.create('fake-smoke', profile(fake_stage_delay_seconds=.03))
            await a.start(run['run_id']); await b.start(run['run_id'])
            await asyncio.gather(*list(a.tasks.values()), *list(b.tasks.values()))
            result = store.get(run['run_id'])
            await a.close(); await b.close()
            self.assertEqual(result['state'], 'COMPLETED')
            self.assertNotIn('lease_rejected', [e['kind'] for e in store.list_events(run['run_id'])])
        with tempfile.TemporaryDirectory() as directory:
            asyncio.run(exercise(directory))

    def test_stop_from_other_coordinator_is_durable(self):
        import asyncio
        async def exercise(directory):
            a = Scheduler(RunStore(directory), lease_ttl=.3)
            b = Scheduler(RunStore(directory), lease_ttl=.3)
            run = await a.create('fake-smoke', profile(fake_measure_seconds=3))
            await a.start(run['run_id'])
            await asyncio.sleep(.04)
            await b.stop(run['run_id'])
            await asyncio.gather(*list(a.tasks.values()))
            self.assertEqual(a.store.get(run['run_id'])['state'], 'CANCELLED')
            await a.close(); await b.close()
        with tempfile.TemporaryDirectory() as directory:
            asyncio.run(exercise(directory))

    def test_terminal_report_restores_after_publication_crash_and_stale_writer(self):
        with tempfile.TemporaryDirectory() as directory:
            store = RunStore(directory); run = store.create('fake-smoke', profile())
            good = {'verdict': 'CANCELLED', 'run_id': run['run_id']}
            stale = store.report_artifact(run['run_id'], {'verdict': 'PASS'})
            store.finish(run['run_id'], 'CANCELLED', 'CREATED', 'stopped', 'CANCELLED', report=good)
            store.publish_report(run['run_id'], stale)
            store.restore_reports()
            path = store.artifact_path(run['run_id'], 'report.json')
            self.assertEqual(json.loads(path.read_text()), good)
            hidden = store.report_artifact(run['run_id'], good)
            self.assertIsNone(store.artifact_path(run['run_id'], hidden.name))

    def test_real_launch_strips_coordinator_only_profile_fields(self):
        import asyncio
        adapter = RealPs5CoordinatorAdapter.__new__(RealPs5CoordinatorAdapter)
        adapter.runs = {'run': {'state': {}, 'started': False, 'safe': True}}
        adapter._save = lambda value: None
        value = profile(run_id='run', host_stimulus={'fps': 240},
                        timing={'hard_seconds': 120, 'stage_timeout_seconds': 45})
        with patch.object(adapter, '_launch_once') as launch:
            asyncio.run(adapter.stage('START_PS5', value, asyncio.Event()))
            passed = launch.call_args.args[1]
            self.assertNotIn('host_stimulus', passed)
            self.assertNotIn('stage_timeout_seconds', passed['timing'])
            self.assertEqual(value['host_stimulus']['fps'], 240)

    def test_cancelled_launch_waits_for_blocking_launch_and_keeps_run_unsafe(self):
        import asyncio
        adapter = RealPs5CoordinatorAdapter.__new__(RealPs5CoordinatorAdapter)
        current = {'state': {'run_id': 'run'}, 'started': False, 'safe': False}
        adapter.runs = {'run': current}
        adapter._save = lambda value: None
        launch_entered, finish_launch = threading.Event(), threading.Event()
        def blocked_launch(value, profile_value):
            launch_entered.set()
            finish_launch.wait(2)
        with patch.object(adapter, '_launch_once', side_effect=blocked_launch):
            async def exercise():
                cancel = asyncio.Event()
                stage = asyncio.create_task(adapter.stage('START_PS5', {'run_id': 'run'}, cancel))
                deadline = time.monotonic() + 1
                while not launch_entered.is_set() and time.monotonic() < deadline:
                    await asyncio.sleep(.005)
                self.assertTrue(launch_entered.is_set())
                self.assertTrue(current['started'])
                stage.cancel()
                await asyncio.sleep(.01)
                stage.cancel()
                await asyncio.sleep(.02)
                self.assertFalse(stage.done(), 'cancellation must wait for the launch thread')
                finish_launch.set()
                with self.assertRaises(asyncio.CancelledError):
                    await stage
                self.assertTrue(current['started'])
                self.assertFalse(current['safe'])
            asyncio.run(exercise())

    def test_real_api_mode_constructs_real_adapters_and_persists_real_mode(self):
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as directory:
            token_file = Path(directory) / 'worker-token'
            token_file.write_text('t' * 48, encoding='utf-8')
            config = {
                'PLTEST_API_MODE': 'real',
                'PLTEST_PS5_HOST': '192.0.2.1',
                'PLTEST_PS5_PROBE_ELF': str(Path(directory) / 'probe.elf'),
                'PLTEST_PS5_LAUNCH_ELF': str(Path(directory) / 'launch.elf'),
                'PLTEST_PS5_EBOOT_SHA256': 'a' * 64,
                'PLTEST_HOST_WORKER_URL': 'http://192.0.2.2:8096',
                'PLTEST_HOST_WORKER_TOKEN_FILE': str(token_file),
            }
            with patch.dict('os.environ', config, clear=True):
                app = create_app(Path(directory) / 'db')
            with TestClient(app) as client:
                caps = client.get('/api/v1/capabilities').json()
                self.assertEqual((caps['api_mode'], caps['ps5_adapter'], caps['host_adapter']),
                                 ('real', 'real', 'real'))
                self.assertIn('decoder-matrix', client.get('/api/v1/suites').json()['items'])
                made = client.post('/api/v1/runs', json={'suite': 'decoder-smoke',
                    'mode': 'real', 'profile': profile()})
                self.assertEqual(made.status_code, 201, made.text)
                stored = client.app.state.store.get(made.json()['run_id'])
                self.assertEqual(stored['profile']['mode'], 'real')

    def test_real_host_adapter_uses_authenticated_worker_protocol(self):
        import asyncio
        from aiohttp import web
        from pltest.coordinator.adapters import RealHostCoordinatorAdapter
        from pltest.host.protocol import SCHEMA

        async def exercise():
            observed = []
            run_id = 'a' * 36
            report = {'schema': SCHEMA, 'run_id': run_id,
                      'measurement_validity': 'VALID', 'raf_fps': 60, 'frames': 1800}
            async def worker_socket(request):
                self.assertEqual(request.headers.get('Authorization'), 'Bearer ' + 'w' * 48)
                ws = web.WebSocketResponse()
                await ws.prepare(request)
                state = 'IDLE'
                async for message in ws:
                    value = message.json()
                    observed.append(value)
                    action = value['action']
                    if action == 'prepare':
                        state = 'READY'
                    elif action == 'start':
                        state = 'RUNNING'
                    elif action == 'status':
                        state = 'COMPLETED'
                    answer = {'schema': SCHEMA, 'command_id': value['command_id'],
                              'run_id': run_id, 'ack': 'accepted', 'state': state,
                              'ready': {'ready': True} if action == 'prepare' else None,
                              'report': report if state == 'COMPLETED' else None}
                    await ws.send_json(answer)
                return ws

            app = web.Application()
            app.router.add_get('/ws', worker_socket)
            runner = web.AppRunner(app)
            await runner.setup()
            site = web.TCPSite(runner, '127.0.0.1', 0)
            await site.start()
            port = site._server.sockets[0].getsockname()[1]
            adapter = RealHostCoordinatorAdapter('http://127.0.0.1:' + str(port), 'w' * 48)
            profile_value = {'run_id': run_id,
                'client': {'fps': 60, 'hdr': False},
                'timing': {'ready_seconds': 1, 'warmup_seconds': 0, 'measure_seconds': 30},
                'host_stimulus': {'scene': 'stress', 'seed': 9, 'monitor': 'DISPLAY2'}}
            cancel = asyncio.Event()
            try:
                await adapter.prepare(profile_value, cancel)
                started = await adapter.start(cancel)
                measured = await adapter.measure(profile_value, cancel)
                await adapter.cleanup(profile_value)
            finally:
                await runner.cleanup()
            return observed, started, measured

        observed, started, measured = asyncio.run(exercise())
        self.assertEqual([command['action'] for command in observed],
                         ['prepare', 'start', 'status', 'status'])
        settings = observed[0]['settings']
        self.assertEqual((settings['scene'], settings['seed'], settings['fps'], settings['monitor']),
                         ('stress', 9, 60, 'DISPLAY2'))
        self.assertEqual(started['state'], 'RUNNING')
        self.assertEqual(measured['measurement_validity'], 'VALID')

    def test_real_scheduler_runs_ordered_adapter_contract_and_reports_real_mode(self):
        class Ps5:
            execution_mode = 'real'
            def __init__(self): self.stages = []
            async def stage(self, stage, profile, cancel):
                self.stages.append(stage)
                if stage == 'COLLECT':
                    return {'receipt': {'functional_verdict': 'PASS'},
                            'title_exit_verified': True, 'stale_artifacts': []}
            async def cleanup(self, profile): self.stages.append('CLEANUP')

        class Host:
            execution_mode = 'real'
            def __init__(self): self.stages = []
            async def prepare(self, profile, cancel): self.stages.append('PREPARE')
            async def start(self, cancel): self.stages.append('START')
            async def measure(self, profile, cancel):
                self.stages.append('MEASURE')
                return {'measurement_validity': 'VALID', 'raf_fps': 60}
            async def cleanup(self, profile): self.stages.append('CLEANUP')

        async def exercise(directory):
            store = RunStore(directory)
            ps5, host = Ps5(), Host()
            scheduler = Scheduler(store, ps5=ps5, host=host)
            made = await scheduler.create('decoder-smoke', profile(timing={'hard_seconds': 30}))
            await scheduler.start(made['run_id'])
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                result = store.get(made['run_id'])
                if result['state'] in ('COMPLETED', 'FAILED', 'INCONCLUSIVE'): break
                await __import__('asyncio').sleep(.01)
            await scheduler.close()
            return result, ps5, host

        with tempfile.TemporaryDirectory() as directory:
            result, ps5, host = __import__('asyncio').run(exercise(directory))
        self.assertEqual(result['state'], 'COMPLETED')
        self.assertEqual(result['report']['execution_mode'], 'real')
        self.assertEqual(result['report']['verdict'], 'PASS')
        self.assertLess(host.stages.index('START'), ps5.stages.index('WARMUP'))
        self.assertEqual(ps5.stages[-2:], ['ANALYZE', 'CLEANUP'])

    def test_real_ps5_ready_wait_uses_persistent_status_signal(self):
        import asyncio
        from unittest.mock import patch
        adapter = RealPs5CoordinatorAdapter.__new__(RealPs5CoordinatorAdapter)
        adapter.runs = {'run': {'state': {'run_id': 'run'}, 'directory': Path('/tmp'),
                                'profile': {}, 'safe': False}}
        adapter._save = lambda current: None
        observed = iter(({'schema': 'prosperolight-test-status/1', 'run_id': 'run',
                          'state': 'warmup', 'stream_ready': True},))
        with patch.object(adapter, '_read_status', side_effect=lambda run_id: next(observed)):
            result = asyncio.run(adapter.stage('WAIT_STREAM_READY', {'run_id': 'run',
                'timing': {'ready_seconds': 1, 'startup_seconds': 1}}, __import__('asyncio').Event()))
        self.assertTrue(result['stream_ready'])

    def test_api_run_report_and_cli_return_same_immutable_report(self):
        with tempfile.TemporaryDirectory() as directory:
            app = create_app(Path(directory) / 'db')
            with TestClient(app) as client:
                self.assertEqual(client.get('/api/v1/health').json()['status'], 'ok')
                self.assertEqual(client.get('/api/v1/capabilities').json()['api_mode'], 'fake-adapters-only')
                made = client.post('/api/v1/runs', json={'suite': 'fake-smoke', 'profile': profile()})
                self.assertEqual(made.status_code, 201)
                run_id = made.json()['run_id']
                client.post(f'/api/v1/runs/{run_id}/start').raise_for_status()
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    current = client.get(f'/api/v1/runs/{run_id}').json()
                    if current['state'] == 'COMPLETED': break
                    time.sleep(.01)
                api_report = client.get(f'/api/v1/runs/{run_id}/report').json()
                def transport(method, path, **kwargs):
                    response = client.request(method, path, **kwargs)
                    response.raise_for_status()
                    return response.json()
                with patch('pltest.coordinator.cli.request', side_effect=transport):
                    cli = CliRunner().invoke(cli_app, ['report', run_id, '--json'])
                self.assertEqual(cli.exit_code, 0, cli.output)
                self.assertEqual(json.loads(cli.output), api_report)
                events = client.get(f'/api/v1/runs/{run_id}/events').json()['items']
                self.assertIn('MEASURE', [event['stage'] for event in events])
                manifest = client.get(f'/api/v1/runs/{run_id}/artifacts').json()['items']
                self.assertIn('report.json', [item['name'] for item in manifest])
                compare = client.post('/api/v1/compare', json={'baseline': run_id, 'candidate': run_id}).json()
                self.assertEqual(compare['reason_code'], 'INSUFFICIENT_REPEAT_PAIRS')
                self.assertEqual(client.get(f'/api/v1/runs/{run_id}/artifacts/../profile.json').status_code, 404)
            with TestClient(create_app(Path(directory) / 'db')) as restarted:
                recovered = restarted.get(f'/api/v1/runs/{run_id}').json()
                self.assertEqual(recovered['state'], 'COMPLETED')
                self.assertEqual(restarted.get(f'/api/v1/runs/{run_id}/report').json(), api_report)

    def test_cross_store_ps5_lease_excludes_second_run(self):
        with tempfile.TemporaryDirectory() as directory:
            first, second = RunStore(directory), RunStore(directory)
            a = first.create('fake-smoke', profile()); b = second.create('fake-smoke', profile())
            self.assertTrue(first.acquire(a['run_id']))
            self.assertFalse(second.acquire(b['run_id']))
            first.release(a['run_id'])
            self.assertTrue(second.acquire(b['run_id']))

    def test_expired_lease_recovers_as_inconclusive_without_replay(self):
        with tempfile.TemporaryDirectory() as directory:
            store = RunStore(directory); run = store.create('fake-smoke', profile())
            store.acquire(run['run_id'])
            for stage in ('PREFLIGHT', 'PREPARE_STIMULUS', 'START_PS5', 'WAIT_STREAM_READY', 'WARMUP', 'MEASURE'):
                store.transition(run['run_id'], stage, stage, 'stage_started')
            recovered = store.recover_expired(ttl=0)
            self.assertEqual(recovered, [run['run_id']])
            value = RunStore(directory).get(run['run_id'])
            self.assertEqual((value['state'], value['reason_code']), ('INCONCLUSIVE', 'COORDINATOR_RESTART'))
            self.assertEqual(RunStore(directory).list_events(run['run_id'])[-1]['kind'], 'coordinator_restarted')
            self.assertEqual(value['report']['reason_code'], 'COORDINATOR_RESTART')
            self.assertTrue((Path(directory) / 'runs' / run['run_id'] / 'report.json').is_file())

    def test_busy_lease_and_low_level_stage_failure_are_classified(self):
        with tempfile.TemporaryDirectory() as directory:
            store = RunStore(directory); scheduler = Scheduler(store)
            app = create_app(directory, scheduler=scheduler)
            with TestClient(app) as client:
                first = client.post('/api/v1/runs', json={'suite': 'fake-smoke', 'profile': profile()}).json()
                second = client.post('/api/v1/runs', json={'suite': 'fake-smoke', 'profile': profile()}).json()
                store.acquire(first['run_id'])
                client.post(f"/api/v1/runs/{second['run_id']}/start")
                deadline = time.monotonic() + 1
                while time.monotonic() < deadline and client.get(f"/api/v1/runs/{second['run_id']}").json()['state'] != 'FAILED': time.sleep(.01)
                busy = client.get(f"/api/v1/runs/{second['run_id']}").json()
                self.assertEqual(busy['reason_code'], 'PS5_LEASE_BUSY')
                store.release(first['run_id'])
                third = client.post('/api/v1/runs', json={'suite': 'fake-smoke', 'profile': profile(fake_fail_stage='WAIT_STREAM_READY')}).json()
                client.post(f"/api/v1/runs/{third['run_id']}/start")
                deadline = time.monotonic() + 2
                while time.monotonic() < deadline and client.get(f"/api/v1/runs/{third['run_id']}").json()['state'] not in ('FAILED','INCONCLUSIVE'): time.sleep(.01)
                failed = client.get(f"/api/v1/runs/{third['run_id']}").json()
                self.assertEqual((failed['state'], failed['stage'], failed['verdict']), ('FAILED', 'WAIT_STREAM_READY', 'FAIL'))

    def test_stop_created_run_is_idempotent_and_durable(self):
        with tempfile.TemporaryDirectory() as directory:
            app = create_app(directory)
            with TestClient(app) as client:
                value = client.post('/api/v1/runs', json={'suite': 'fake-smoke', 'profile': profile()}).json()
                one = client.post(f"/api/v1/runs/{value['run_id']}/stop").json()
                two = client.post(f"/api/v1/runs/{value['run_id']}/stop").json()
                self.assertEqual(one['state'], 'CANCELLED'); self.assertEqual(two['state'], 'CANCELLED')
                self.assertEqual(client.get(f"/api/v1/runs/{value['run_id']}/report").json()['verdict'], 'CANCELLED')
            recovered = RunStore(directory).get(value['run_id'])
            self.assertEqual(recovered['state'], 'CANCELLED')

    def test_stop_active_run_cancels_without_restarting(self):
        with tempfile.TemporaryDirectory() as directory:
            with TestClient(create_app(directory)) as client:
                value = client.post('/api/v1/runs', json={'suite': 'fake-smoke',
                    'profile': profile(fake_stage_delay_seconds=0.2)}).json()
                client.post(f"/api/v1/runs/{value['run_id']}/start")
                time.sleep(.03)
                stopped = client.post(f"/api/v1/runs/{value['run_id']}/stop").json()
                self.assertEqual(stopped['state'], 'CANCELLED')
                events = client.get(f"/api/v1/runs/{value['run_id']}/events").json()['items']
                self.assertNotIn('COMPLETED', [event['state'] for event in events])

    def test_adapter_cleanup_failure_cannot_be_reported_as_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            with TestClient(create_app(directory)) as client:
                value = client.post('/api/v1/runs', json={'suite': 'fake-smoke',
                    'profile': profile(fake_cleanup_fails=True)}).json()
                client.post(f"/api/v1/runs/{value['run_id']}/start")
                deadline = time.monotonic() + 2
                while time.monotonic() < deadline:
                    current = client.get(f"/api/v1/runs/{value['run_id']}").json()
                    if current['state'] in ('COMPLETED', 'FAILED', 'INCONCLUSIVE'): break
                    time.sleep(.01)
                self.assertEqual(current['state'], 'INCONCLUSIVE')
                self.assertEqual(current['reason_code'], 'CLEANUP_FAILED')
                self.assertEqual(current['verdict'], 'INCONCLUSIVE')

    def test_lost_ps5_lease_cancels_without_touching_new_owner(self):
        with tempfile.TemporaryDirectory() as directory:
            store = RunStore(directory); scheduler = Scheduler(store, lease_ttl=.03)
            with TestClient(create_app(directory, scheduler=scheduler)) as client:
                value = client.post('/api/v1/runs', json={'suite': 'fake-smoke',
                    'profile': profile(fake_stage_delay_seconds=2)}).json()
                client.post(f"/api/v1/runs/{value['run_id']}/start")
                time.sleep(.01)
                store.release(value['run_id'])
                deadline = time.monotonic() + 2
                while time.monotonic() < deadline:
                    current = client.get(f"/api/v1/runs/{value['run_id']}").json()
                    if current['state'] in ('INCONCLUSIVE', 'COMPLETED', 'FAILED'): break
                    time.sleep(.01)
                self.assertEqual((current['state'], current['reason_code']), ('INCONCLUSIVE', 'LEASE_LOST'))
                self.assertEqual(current['report']['cleanup_errors'], [])

    def test_api_token_and_schema_validation(self):
        with tempfile.TemporaryDirectory() as directory:
            token = 'secret-token-value-0123456789-xyz'
            with TestClient(create_app(directory, token=token)) as client:
                self.assertEqual(client.get('/api/v1/health').status_code, 200)
                self.assertEqual(client.get('/api/v1/runs/nope').status_code, 401)
                headers = {'Authorization': 'Bearer ' + token}
                self.assertEqual(client.get('/api/v1/runs/nope', headers=headers).status_code, 404)
                bad = client.post('/api/v1/runs', headers=headers,
                                  json={'suite': '../', 'profile': profile(), 'extra': 1})
                self.assertEqual(bad.status_code, 422)
                secret = client.post('/api/v1/runs', headers=headers,
                                     json={'suite': 'fake-smoke', 'profile': profile(api_token='do-not-store')})
                self.assertEqual(secret.status_code, 422)
                real = client.post('/api/v1/runs', headers=headers,
                                   json={'suite': 'fake-smoke', 'profile': profile(), 'mode': 'real'})
                self.assertEqual(real.status_code, 422)
                self.assertEqual(real.json()['detail']['code'], 'ADAPTER_MODE_UNAVAILABLE')


if __name__ == '__main__': unittest.main()
