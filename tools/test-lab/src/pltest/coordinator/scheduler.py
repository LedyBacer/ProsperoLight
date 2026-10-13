# ps5-native-app-boilerplate - cancellable, leased Test Lab scheduler.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import asyncio
import json
import math
import time

from .store import TERMINAL, RunStore

STAGES = ('PREFLIGHT', 'PREPARE_STIMULUS', 'START_PS5', 'WAIT_STREAM_READY',
          'WARMUP', 'MEASURE', 'COOLDOWN', 'STOP_STREAM', 'COLLECT', 'ANALYZE')


class FakePs5Adapter:
    """Deterministic no-hardware adapter used by coordinator acceptance tests."""
    async def stage(self, stage, profile, cancel):
        if cancel.is_set(): raise asyncio.CancelledError
        await asyncio.sleep(float(profile.get('fake_stage_delay_seconds', 0.01)))
        if cancel.is_set(): raise asyncio.CancelledError
        if profile.get('fake_fail_stage') == stage:
            raise RuntimeError('FAKE_' + stage + '_ERROR')
        if stage == 'WAIT_STREAM_READY' and profile.get('fake_no_ready'):
            raise TimeoutError('STREAM_READY_TIMEOUT')

    async def cleanup(self, profile):
        if profile.get('fake_cleanup_fails'):
            raise RuntimeError('FAKE_PS5_CLEANUP_ERROR')


class FakeHostWorkerAdapter:
    async def prepare(self, profile, cancel):
        if cancel.is_set(): raise asyncio.CancelledError
        if profile.get('fake_host_not_ready'): raise RuntimeError('HOST_NOT_READY')

    async def measure(self, profile, cancel):
        seconds = float(profile.get('fake_measure_seconds', 0.02))
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if cancel.is_set(): raise asyncio.CancelledError
            await asyncio.sleep(min(.05, deadline - time.monotonic()))
        return {'frames': int(profile.get('fake_frames', 120)),
                'raf_fps': float(profile.get('fake_raf_fps', 60)),
                'measurement_validity': 'VALID'}

    async def cleanup(self, profile):
        if profile.get('fake_cleanup_fails'):
            raise RuntimeError('FAKE_HOST_CLEANUP_ERROR')


class Scheduler:
    def __init__(self, store: RunStore, ps5=None, host=None, lease_ttl=10):
        self.store = store
        self.ps5 = ps5 or FakePs5Adapter()
        self.host = host or FakeHostWorkerAdapter()
        self.execution_mode = ('real' if getattr(self.ps5, 'execution_mode', None) == 'real' and
                               getattr(self.host, 'execution_mode', None) == 'real' else 'fake')
        if (getattr(self.ps5, 'execution_mode', 'fake') !=
                getattr(self.host, 'execution_mode', 'fake')):
            raise ValueError('PS5 and host adapters must use the same execution mode')
        self.lease_ttl = lease_ttl
        self.tasks = {}
        self.cancellations = {}
        self.lost_leases = set()
        self.shutdown_requested = set()
        self.lock = asyncio.Lock()
        self.closed = False
        self.recovery_task = None

    async def start_recovery(self):
        if not self.recovery_task:
            self.store.restore_reports()
            self.recovery_task = asyncio.create_task(self._heartbeat_loop(), name='pltest-lease-heartbeat')

    async def _heartbeat_loop(self):
        while not self.closed:
            async with self.lock:
                active = list(self.tasks)
            for run_id in active:
                if self.store.stop_requested(run_id):
                    self.cancellations[run_id].set()
                if not self.store.heartbeat(run_id):
                    self.lost_leases.add(run_id)
                    cancel = self.cancellations.get(run_id)
                    task = self.tasks.get(run_id)
                    if cancel: cancel.set()
                    if task and not task.done(): task.cancel()
            # Refresh known live owners before recovering orphaned leases. A
            # delayed event loop must not classify its own task as a restart.
            # heartbeat still refuses a lease deleted/replaced by another owner.
            self.store.recover_expired(self.lease_ttl)
            await asyncio.sleep(min(2, self.lease_ttl / 3))

    async def close(self):
        run_ids = list(self.tasks)
        self.shutdown_requested.update(run_ids)
        for run_id in run_ids:
            cancel = self.cancellations.get(run_id)
            if cancel: cancel.set()
        tasks = list(self.tasks.values())
        if tasks:
            _, pending = await asyncio.wait(tasks, timeout=8)
            for task in pending: task.cancel()
            if pending: await asyncio.wait(pending, timeout=8)
        # Keep refreshing leases until cleanup tasks stop. If an adapter ignores
        # cancellation, leave its lease to expire after this process exits.
        self.closed = True
        if self.recovery_task:
            self.recovery_task.cancel()
            await asyncio.gather(self.recovery_task, return_exceptions=True)

    async def create(self, suite, profile):
        if profile.get('schema') != 'prosperolight-test/1':
            raise ValueError('profile must use prosperolight-test/1')
        if self.execution_mode == 'real':
            from ..suites import apply_suite
            profile = apply_suite(suite, profile)
        timing = profile.get('timing', {})
        if not isinstance(timing, dict) or type(timing.get('hard_seconds', 120)) is not int \
                or not 1 <= timing.get('hard_seconds', 120) <= 1800:
            raise ValueError('invalid profile deadline')
        if type(timing.get('stage_timeout_seconds', timing.get('hard_seconds', 120))) is not int \
                or not 1 <= timing.get('stage_timeout_seconds', timing.get('hard_seconds', 120)) <= 1800:
            raise ValueError('invalid per-stage timeout')
        for key, low, high in (('fake_measure_seconds', 0, 600), ('fake_stage_delay_seconds', 0, 10),
                               ('fake_raf_fps', 0, 1000), ('fake_frames', 0, 10_000_000)):
            types = (int,) if key == 'fake_frames' else (int, float)
            if key in profile and (type(profile[key]) not in types or
                                   not math.isfinite(profile[key]) or not low <= profile[key] <= high):
                raise ValueError('invalid bounded test parameter: ' + key)
        for key in ('fake_no_ready', 'fake_host_not_ready', 'fake_cleanup_fails'):
            if key in profile and type(profile[key]) is not bool:
                raise ValueError('invalid boolean test parameter: ' + key)
        if 'fake_fail_stage' in profile and profile['fake_fail_stage'] not in STAGES:
            raise ValueError('unknown fake failure stage')
        if self.execution_mode == 'real' and any(key.startswith('fake_') for key in profile):
            raise ValueError('fake-only test controls are not accepted by real adapters')
        return self.store.create(suite, profile, mode=self.execution_mode)

    async def start(self, run_id):
        async with self.lock:
            run = self.store.get(run_id)
            if not run: return None
            if run['state'] in TERMINAL or run_id in self.tasks: return run
            if run['state'] != 'CREATED': return run
            cancel = asyncio.Event()
            self.cancellations[run_id] = cancel
            profile = dict(run['profile']['profile'])
            profile['run_id'] = run_id
            task = asyncio.create_task(self._execute(run_id, profile, run['profile']['suite'], cancel),
                                       name='pltest-run-' + run_id)
            self.tasks[run_id] = task
            return self.store.get(run_id)

    async def stop(self, run_id):
        run = self.store.get(run_id)
        if not run: return None
        if run['state'] in TERMINAL: return run
        self.store.request_stop(run_id)
        cancel = self.cancellations.get(run_id)
        if cancel: cancel.set()
        task = self.tasks.get(run_id)
        if task:
            try: await asyncio.wait_for(asyncio.shield(task), timeout=8)
            except asyncio.TimeoutError:
                task.cancel()
                _, pending = await asyncio.wait({task}, timeout=8)
                if pending:
                    # Preserve the lease while an adapter has not acknowledged
                    # cancellation; releasing it here could overlap hardware.
                    return self.store.get(run_id)
                current = self.store.get(run_id)
                if current and current['state'] not in TERMINAL:
                    self.store.finish(run_id, 'INCONCLUSIVE', 'STOP_STREAM', 'bounded_stop_timeout',
                                      verdict='INCONCLUSIVE', reason_code='STOP_TIMEOUT',
                                      reason='Adapter did not acknowledge stop before deadline')
        elif run['state'] == 'CREATED':
            report = {'schema_version': 'prosperolight-test-lab-report/1', 'execution_mode': self.execution_mode, 'run_id': run_id,
                      'verdict': 'CANCELLED', 'stage': 'CREATED',
                      'reason_code': 'CANCELLED_BY_OPERATOR',
                      'artifacts': ['profile.json', 'report.json'], 'stage_results': {}}
            pending = self.store.report_artifact(run_id, report)
            result = self.store.finish(run_id, 'CANCELLED', 'CANCELLED', 'cancelled_before_start',
                                       verdict='CANCELLED', reason_code='CANCELLED_BY_OPERATOR', report=report, expected_state='CREATED')
            self.store.publish_report(run_id, pending)
            return result
        return self.store.get(run_id)

    async def _execute(self, run_id, profile, suite, cancel):
        claim = self.store.claim(run_id)
        if claim == 'active':
            self.tasks.pop(run_id, None); self.cancellations.pop(run_id, None)
            return
        if claim == 'busy':
            report = {'schema_version': 'prosperolight-test-lab-report/1', 'execution_mode': self.execution_mode, 'run_id': run_id,
                      'verdict': 'FAIL', 'stage': 'PREFLIGHT', 'reason_code': 'PS5_LEASE_BUSY',
                      'reason': 'Another coordinator owns the PS5 lease',
                      'artifacts': ['profile.json', 'report.json'], 'stage_results': {}}
            pending = self.store.report_artifact(run_id, report)
            self.store.finish(run_id, 'FAILED', 'PREFLIGHT', 'lease_rejected',
                              verdict='FAIL', reason_code='PS5_LEASE_BUSY', reason=report['reason'], report=report, expected_state='CREATED')
            self.store.publish_report(run_id, pending)
            self.tasks.pop(run_id, None); self.cancellations.pop(run_id, None)
            return
        report = {'schema_version': 'prosperolight-test-lab-report/1', 'execution_mode': self.execution_mode, 'run_id': run_id,
                  'suite': suite, 'verdict': 'INCONCLUSIVE', 'stage_results': {},
                  'artifacts': []}
        deadline = time.monotonic() + profile.get('timing', {}).get('hard_seconds', 120)
        try:
            await self.start_recovery()
            for stage in STAGES:
                if cancel.is_set() or self.store.stop_requested(run_id): raise asyncio.CancelledError
                if not self.store.heartbeat(run_id):
                    self.lost_leases.add(run_id)
                    raise asyncio.CancelledError
                self.store.transition(run_id, stage, stage, 'stage_started')
                began = time.monotonic()
                try:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0: raise TimeoutError('RUN_DEADLINE_EXCEEDED')
                    async def perform():
                        if stage == 'PREFLIGHT':
                            report['ps5_preflight'] = await self.ps5.stage(stage, profile, cancel)
                            if self.execution_mode == 'real':
                                report['host_session_preflight'] = await self.host.preflight(profile, cancel)
                        elif stage == 'PREPARE_STIMULUS':
                            if self.execution_mode == 'real' and profile.get('host_stimulus',{}).get('monitor') == 'virtual':
                                report['stimulus_preparation'] = 'deferred until own stream creates virtual display'
                            else: await self.host.prepare(profile, cancel)
                        elif stage == 'START_PS5': await self.ps5.stage(stage, profile, cancel)
                        elif stage == 'WAIT_STREAM_READY': await self.ps5.stage(stage, profile, cancel)
                        elif stage == 'WARMUP':
                            if self.execution_mode == 'real':
                                if profile.get('host_stimulus',{}).get('monitor') == 'virtual':
                                    await self.host.prepare(profile, cancel)
                                anchors = report['phase_anchors'] = {}
                                for name, operation in (('browser_start', self.host.start(cancel)),
                                                        ('ps5_start', None)):
                                    begin = time.monotonic_ns() // 1000
                                    ack = await operation if operation is not None else await self.ps5.stage(stage, profile, cancel)
                                    anchors[name] = {'coordinator_start_us': begin,
                                                     'coordinator_end_us': time.monotonic_ns() // 1000,
                                                     'ack': ack}
                            else:
                                await self.ps5.stage(stage, profile, cancel)
                        elif stage == 'MEASURE':
                            report['host_measurement'] = await self._measure_with_phase_anchors(profile, cancel, report)
                        elif stage == 'STOP_STREAM':
                            await self.ps5.stage(stage, profile, cancel)
                            await self.host.cleanup(profile)
                            if self.execution_mode == 'fake': await self.ps5.cleanup(profile)
                        elif stage == 'COLLECT':
                            report['ps5_measurement'] = await self.ps5.stage(stage, profile, cancel)
                        elif stage == 'ANALYZE':
                            if self.execution_mode == 'real':
                                await self.ps5.stage(stage, profile, cancel)
                                host_result = report.get('host_measurement') or {}
                                ps5_result = report.get('ps5_measurement') or {}
                                receipt = ps5_result.get('receipt') or {}
                                report['verdict'] = ('FAIL' if receipt.get('functional_verdict') == 'FAIL' else
                                                     'PASS' if host_result.get('measurement_validity') == 'VALID' and
                                                     receipt.get('functional_verdict') == 'PASS' and
                                                     ps5_result.get('title_exit_verified') and
                                                     not ps5_result.get('stale_artifacts') else 'INCONCLUSIVE')
                                await self.ps5.cleanup(profile)
                            else:
                                await self.ps5.stage(stage, profile, cancel)
                        else: await self.ps5.stage(stage, profile, cancel)
                    stage_limit = profile.get('timing', {}).get('stage_timeout_seconds',
                                                                  profile.get('timing', {}).get('hard_seconds', 120))
                    await asyncio.wait_for(perform(), timeout=min(remaining, stage_limit))
                except Exception as exc:
                    report['stage_results'][stage] = {'elapsed_seconds': time.monotonic() - began,
                                                      'error': str(exc)[:512] or type(exc).__name__}
                    raise
                report['stage_results'][stage] = {'elapsed_seconds': time.monotonic() - began, 'acknowledged': True}
                self.store.transition(run_id, stage, stage, 'stage_acknowledged',
                                      {'elapsed_seconds': report['stage_results'][stage]['elapsed_seconds']})
            if self.execution_mode == 'fake':
                report['verdict'] = 'PASS'
            self._archive_report(run_id, profile, report)
            report['finished_unix'] = time.time()
            report['artifacts'] = [item['name'] for item in self.store.files(run_id) or []]
            if 'report.json' not in report['artifacts']:
                report['artifacts'].append('report.json')
            terminal_verdict = report.get('verdict', 'INCONCLUSIVE')
            terminal_state = report['execution_state']
            if terminal_state != 'COMPLETED':
                report.setdefault('reason_code', 'MEASUREMENT_INCONCLUSIVE')
            pending = self.store.report_artifact(run_id, report)
            final_stage = 'COMPLETED' if terminal_state == 'COMPLETED' else 'ANALYZE'
            final = self.store.finish(run_id, terminal_state, final_stage,
                                      'completed' if terminal_state == 'COMPLETED' else 'analysis_inconclusive',
                                      verdict=terminal_verdict, reason_code=report.get('reason_code'), report=report)
            if final and final['state'] == terminal_state and final['report'] == json.loads(json.dumps(report, allow_nan=False)):
                self.store.publish_report(run_id, pending)
        except asyncio.CancelledError:
            lease_lost = run_id in self.lost_leases
            shutting_down = run_id in self.shutdown_requested
            errors = [] if lease_lost else await self._cleanup(profile, run_id)
            lease_lost = run_id in self.lost_leases
            terminal_verdict = 'INCONCLUSIVE' if lease_lost or errors or shutting_down else 'CANCELLED'
            reason_code = ('LEASE_LOST' if lease_lost else 'CLEANUP_FAILED' if errors else
                           'COORDINATOR_SHUTDOWN' if shutting_down else 'CANCELLED_BY_OPERATOR')
            report.update(verdict=terminal_verdict,
                          reason_code=reason_code,
                          cleanup_errors=errors, finished_unix=time.time())
            report['artifacts'] = ['profile.json', 'report.json']
            self._archive_report(run_id, profile, report)
            pending = self.store.report_artifact(run_id, report)
            current = self.store.get(run_id)
            if current and current['state'] not in TERMINAL:
                terminal = report['verdict']
                state = 'CANCELLED' if terminal == 'CANCELLED' else 'INCONCLUSIVE'
                final = self.store.finish(run_id, state, 'STOP_STREAM', 'cancelled' if state == 'CANCELLED' else 'cleanup_failed',
                                          verdict=terminal, reason_code=report['reason_code'], report=report)
                if final and final['state'] == state and final['report'] == json.loads(json.dumps(report, allow_nan=False)):
                    self.store.publish_report(run_id, pending)
        except Exception as exc:
            current = self.store.get(run_id)
            if current and current['state'] not in TERMINAL:
                cleanup_errors = await self._cleanup(profile, run_id)
                lease_lost = run_id in self.lost_leases
                stage = current['stage']
                code = 'STAGE_TIMEOUT' if isinstance(exc, TimeoutError) and not str(exc) else str(exc).upper()
                if not code or len(code) > 64 or not code.replace('_', '').isalnum():
                    code = type(exc).__name__
                report.update(verdict='FAIL' if str(exc).startswith('FAKE_') else 'INCONCLUSIVE',
                              stage=stage, reason_code=code, reason=str(exc)[:512],
                              cleanup_errors=cleanup_errors, finished_unix=time.time())
                if cleanup_errors:
                    report.update(verdict='INCONCLUSIVE', reason_code='CLEANUP_FAILED')
                if lease_lost:
                    report.update(verdict='INCONCLUSIVE', reason_code='LEASE_LOST',
                                  reason='PS5 lease ownership was lost during cleanup')
                report['artifacts'] = ['profile.json', 'report.json']
                self._archive_report(run_id, profile, report)
                pending = self.store.report_artifact(run_id, report)
                current = self.store.get(run_id)
                if not current or current['state'] in TERMINAL:
                    return
                terminal = 'FAILED' if report['verdict'] == 'FAIL' else 'INCONCLUSIVE'
                final = self.store.finish(run_id, terminal, stage, 'stage_failed', verdict=report['verdict'],
                                          reason_code=report['reason_code'], reason=report.get('reason', '')[:512], report=report)
                if final and final['state'] == terminal and final['report'] == json.loads(json.dumps(report, allow_nan=False)):
                    self.store.publish_report(run_id, pending)
        finally:
            self.store.release(run_id)
            self.tasks.pop(run_id, None); self.cancellations.pop(run_id, None); self.lost_leases.discard(run_id)
            self.shutdown_requested.discard(run_id)

    async def _measure_with_phase_anchors(self, profile, cancel, report):
        if self.execution_mode != 'real':
            return await self.host.measure(profile, cancel)
        async def probe():
            if not hasattr(self.ps5, 'clock_probe') or not hasattr(self.host, 'clock_probe'):
                report.setdefault('phase_anchors', {})['end_probe_unsupported'] = True
                return
            timing = profile.get('timing', {})
            duration = timing.get('warmup_seconds', 0) + timing.get('measure_seconds', 30)
            start = getattr(self.host, 'start_ack_monotonic', time.monotonic())
            await asyncio.sleep(max(0, start + duration - 3 - time.monotonic()))
            anchors = report.setdefault('phase_anchors', {})
            for name, operation in (('ps5_end_probe', self.ps5.clock_probe(profile, cancel)),
                                    ('browser_end_probe', None)):
                begin = time.monotonic_ns() // 1000
                ack = await operation if operation is not None else await self.host.clock_probe(cancel)
                anchors[name] = {'coordinator_start_us': begin,
                    'coordinator_end_us': time.monotonic_ns() // 1000, 'ack': ack}
                if ack.get('unsupported'): break
        pending = asyncio.create_task(probe())
        try:
            value = await self.host.measure(profile, cancel)
            await pending
            return value
        finally:
            if not pending.done():
                pending.cancel()
            await asyncio.gather(pending, return_exceptions=True)

    def _archive_report(self, run_id, profile, report):
        from ..analysis import analyze_run
        from ..report import archive_analysis
        directory = self.store.artifacts / run_id
        if not report.get('ps5_measurement'):
            from ..analysis import read_bounded
            try:
                evidence = json.loads(read_bounded(directory / 'ps5/executor.json')).get('collected')
                if evidence and evidence.get('receipt', {}).get('run_id') == run_id:
                    report['ps5_measurement'] = evidence
            except (OSError, ValueError, TypeError):
                pass
        report['analysis'] = analyze_run(profile, report, directory)
        if report['analysis']['functional_verdict'] == 'FAIL' and not report.get('cleanup_errors') and report.get('reason_code') not in ('LEASE_LOST','COORDINATOR_SHUTDOWN'):
            report['verdict'] = 'FAIL'
        from ..verdicts import apply_classification
        apply_classification(report)
        archive_analysis(self.store.artifacts / run_id, profile, report, self.store.list_events(run_id) or [])

    async def _cleanup(self, profile, run_id):
        errors = []
        for name, adapter in (('ps5', self.ps5), ('host', self.host)):
            if run_id in self.lost_leases or not self.store.heartbeat(run_id):
                self.lost_leases.add(run_id)
                return errors
            try:
                await asyncio.wait_for(adapter.cleanup(profile), timeout=min(45, max(4, getattr(adapter, 'cleanup_timeout_seconds', 4))))
            except Exception as exc:
                errors.append({'adapter': name, 'reason_code': type(exc).__name__, 'reason': str(exc)[:256]})
        return errors
