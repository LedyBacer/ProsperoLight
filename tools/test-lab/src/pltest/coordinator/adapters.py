# ps5-native-app-boilerplate - real coordinator adapters for PS5 and Windows.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""Coordinator bridges to the validated PR3 PS5 protocol and PR4 host worker."""
import asyncio
import hashlib
import json
import re
import time
from ftplib import Error, error_perm
from pathlib import Path
from urllib.parse import urlsplit

from ..ps5.adapter import Ps5Adapter, DeploymentPreflightError, INSTALL
from ..ps5.collector import collect
from ..ps5.ftp import archive_logs, installation_exists
from ..ps5.launcher import launch
from ..host.client import HostWorkerAdapter
import collect_run
import control_run
from launch_spike import read_receipt, request_bytes

ROOT = '/data/prosperolight/test-lab'


async def _blocking(function, *args):
    """Wait for bounded blocking I/O to settle before allowing cancellation."""
    task = asyncio.create_task(asyncio.to_thread(function, *args))
    try:
        return await asyncio.shield(task)
    except asyncio.CancelledError:
        # Repeated scheduler cancellation must not detach an I/O thread that
        # may still be launching or sending a command to hardware.
        while not task.done():
            try:
                await asyncio.shield(task)
            except asyncio.CancelledError:
                continue
            except Exception:
                break
        if task.done() and not task.cancelled():
            task.exception()  # retrieve any failure without masking cancellation
        raise



class RealPs5CoordinatorAdapter:
    """Stepwise PS5 adapter. Launch is issued once; uncertain ownership is retained."""
    execution_mode = 'real'
    cleanup_timeout_seconds = 45

    def __init__(self, host, probe_elf, launch_elf, expected_eboot_sha256,
                 artifacts_root, ftp_port=2121, loader_port=9021, manager_port=8084,
                 build_root=None):
        if not re.fullmatch(r'[0-9a-f]{64}', expected_eboot_sha256 or ''):
            raise ValueError('real coordinator requires the expected installed eboot SHA-256')
        self.ps5 = Ps5Adapter(host, probe_elf, launch_elf, ftp_port, loader_port, manager_port)
        self.expected = expected_eboot_sha256
        self.artifacts_root = Path(artifacts_root)
        self.runs = {}
        self.build_root = build_root

    def _run(self, profile):
        run_id = profile['run_id']
        if run_id not in self.runs:
            raise RuntimeError('PS5_RUN_NOT_PREPARED')
        return self.runs[run_id]

    async def stage(self, stage, profile, cancel):
        run_id = profile.get('run_id')
        if stage == 'PREFLIGHT':
            if profile.get('schema') != 'prosperolight-test/1' or not profile.get('build_label'):
                raise ValueError('real run requires a versioned PS5 profile and build_label')
            request_bytes(run_id, profile['client']['host'])
            variant = None
            if 'build_variant' in profile:
                from ..build import load_variant
                if profile.get('allow_deploy') is not True:
                    raise ValueError('DEPLOY_PERMISSION_REQUIRED')
                variant = await _blocking(load_variant, profile['build_variant'], self.build_root)
                if any(profile.get(key) != variant[key] for key in ('source_sha', 'build_label')):
                    raise ValueError('PROFILE_BUILD_IDENTITY_MISMATCH')
            directory = self.artifacts_root / run_id / 'ps5'
            directory.mkdir(mode=0o700, exist_ok=False)
            await _blocking(self._acquire_lease, profile, directory)
            state = self._run(profile)['state']
            try:
                self._run(profile)['safe'] = False
                state['preflight'] = await _blocking(self.ps5.idle, profile['client']['host'])
                self._run(profile)['safe'] = True
                state['previous_artifacts'], installed_image = await _blocking(
                    self._archive_and_inspect, directory / 'prelaunch', bool(variant))
                current = self._run(profile)
                expected = self.expected
                if variant:
                    expected = variant['eboot_sha256']
                    state['build_variant'] = variant
                    current['safe'] = False
                    state['stage'] = 'DEPLOY'
                    self._save(current)
                    if cancel.is_set():
                        current['safe'] = True
                        raise asyncio.CancelledError
                    state['deployment_attempted'] = True
                    self._save(current)
                    try:
                        state['deployed_files'] = await _blocking(self.ps5.deploy, variant['directory'], expected)
                    except DeploymentPreflightError:
                        current['safe'] = True
                        raise
                    state['installation_inventory_evidence']=getattr(self.ps5,'installation_inventory_evidence',None)
                    if state['deployed_files'] != variant['files']:
                        raise RuntimeError('DEPLOY_READBACK_MANIFEST_MISMATCH')
                    installed_image = await _blocking(self.ps5.installed)
                state['installed'] = installed_image
                if installed_image != {'eboot_sha256': expected, 'build_label': profile['build_label']}:
                    raise RuntimeError('installed build hash/label mismatch')
                current = self._run(profile)
                current['safe'] = True
                state['stage'] = stage
                self._save(current)
            except BaseException:
                await self._release_if_safe(profile)
                raise
            return self._run(profile)['state']['preflight']
        current = self._run(profile)
        if cancel.is_set():
            raise asyncio.CancelledError
        if stage == 'START_PS5':
            if current['started']:
                raise RuntimeError('PS5 launch replay refused')
            current['safe'] = False
            current['state']['stage'] = stage
            self._save(current)
            run_profile = dict(profile, run_id=run_id, coordinated=True)
            run_profile.pop('host_stimulus', None)
            run_profile.pop('analysis_environment', None)
            run_profile.pop('build_variant', None)
            run_profile.pop('allow_deploy', None)
            run_profile['timing'] = {k: v for k, v in profile.get('timing', {}).items()
                                     if k != 'stage_timeout_seconds'}
            # A lost reply can leave launch outcome ambiguous. Mark it active
            # before sending so cleanup never releases the remote lease early.
            current['started'] = True
            current['state']['launch_attempted'] = True
            self._save(current)
            await _blocking(self._launch_once, current, run_profile)
        elif stage == 'WAIT_STREAM_READY':
            return await _blocking(self._wait_stream_ready, current,
                                   profile.get('timing', {}).get('ready_seconds', 30) +
                                   profile.get('timing', {}).get('startup_seconds', 30) + 15)
        elif stage == 'WARMUP':
            return await _blocking(self._command, run_id, 'start_measure',
                            profile.get('timing', {}).get('ready_seconds', 30))
        elif stage == 'COOLDOWN':
            await _blocking(self._wait_receipt, current,
                             profile.get('timing', {}).get('cooldown_seconds', 0) + 30)
        elif stage == 'STOP_STREAM':
            if not await _blocking(self._has_receipt, run_id):
                await _blocking(self._command, run_id, 'stop', 10)
        elif stage == 'COLLECT':
            result = await _blocking(self._collect, current,
                                     profile.get('timing', {}).get('hard_seconds', 120) + 30)
            current['collected'] = result
            current['state'].update(stage='COLLECT', receipt=result['receipt'],
                                    title_exit_verified=result['title_exit_verified'],
                                    stale_artifacts=result['stale_artifacts'],
                                    functional_verdict=(result['receipt'].get('functional_verdict')
                                                        if result['title_exit_verified'] and not result['stale_artifacts']
                                                        else 'INCONCLUSIVE'))
            current['safe'] = result['title_exit_verified']
            self._save(current)
            return result
        elif stage == 'ANALYZE':
            return current.get('collected', {})
        return

    def _acquire_lease(self, profile, directory):
        ownership = self.ps5.lease(profile['run_id'])
        owner = ownership.__enter__()
        self.runs[profile['run_id']] = {
            'lease': ownership, 'owner': owner, 'safe': True, 'started': False,
            'directory': directory, 'profile': dict(profile),
            'state': {'run_id': profile['run_id'], 'stage': 'PREFLIGHT', 'lease': owner,
                      'functional_verdict': 'INCONCLUSIVE'}}

    def _save(self, current):
        path = current['directory'] / 'executor.json'
        temporary = path.with_suffix('.tmp')
        temporary.write_text(json.dumps(current['state'], indent=2) + '\n', encoding='utf-8')
        temporary.replace(path)

    def _archive_and_inspect(self, destination, deploying=False):
        with self.ps5.connect() as ftp:
            if deploying:
                installation_exists(ftp,INSTALL)
            previous = archive_logs(ftp, destination)
        return previous, self.ps5.installed()

    def _launch_once(self, current, profile):
        with self.ps5.connect() as ftp:
            current['state']['launch'] = launch(ftp, self.ps5.loader, self.ps5.launch_elf, profile)
        current['state']['launch_accepted'] = True
        self._save(current)

    def _command(self, run_id, action, timeout):
        with self.ps5.connect() as ftp:
            status = control_run.command(ftp, run_id, action, timeout=timeout)
        if status.get('command_result') != 'accepted':
            raise RuntimeError('PS5_COMMAND_REJECTED:' + action)
        return status

    def _read_status(self, run_id):
        with self.ps5.connect() as ftp:
            value = read_receipt(ftp, ROOT + '/status/' + run_id + '.json')
        if value.get('schema') != 'prosperolight-test-status/1' or value.get('run_id') != run_id:
            raise ValueError('PS5 status identity mismatch')
        return value

    async def clock_probe(self, profile, cancel):
        if cancel.is_set(): raise asyncio.CancelledError
        run_id = profile['run_id']
        status = await _blocking(self._read_status, run_id)
        if 'clock_probe' not in status.get('capabilities', []):
            return {'unsupported': True, 'reason': 'installed title lacks clock_probe capability'}
        return await _blocking(self._command, run_id, 'clock_probe', 10)

    def _wait_stream_ready(self, current, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                status = self._read_status(current['state']['run_id'])
            except error_perm as exc:
                if not str(exc).startswith('550'):
                    raise
                status = None
            if status:
                if status.get('state') == 'failed':
                    raise RuntimeError(status.get('reason') or 'PS5_STREAM_FAILED')
                if status.get('stream_ready') is True:
                    current['state']['stream_ready'] = status
                    self._save(current)
                    return status
            time.sleep(.5)
        raise TimeoutError('PS5 stream-ready status was not observed')

    def _has_receipt(self, run_id):
        try:
            with self.ps5.connect() as ftp:
                value = read_receipt(ftp, ROOT + '/receipts/' + run_id + '.json')
            if value.get('run_id') != run_id or value.get('schema') not in ('prosperolight-test-receipt/1', 'prosperolight-test-receipt/2'):
                raise ValueError('PS5 receipt identity mismatch')
            return True
        except error_perm as exc:
            if str(exc).startswith('550'):
                return False
            raise

    def _wait_receipt(self, current, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self._has_receipt(current['state']['run_id']):
                return
            status = self._read_status(current['state']['run_id'])
            if status.get('state') == 'failed':
                raise RuntimeError(status.get('reason') or 'PS5_STREAM_FAILED')
            time.sleep(.5)
        raise TimeoutError('PS5 terminal receipt was not published before deadline')

    def _collect(self, current, timeout):
        run_id = current['state']['run_id']
        output = current['directory'] / 'artifacts'
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                with self.ps5.connect() as ftp:
                    receipt = collect(ftp, run_id, output, max(1, deadline - time.monotonic()))
                break
            except (ConnectionError, EOFError, TimeoutError, Error) as exc:
                if output.exists():
                    raise RuntimeError('partial PS5 collection retained; refusing replay') from exc
                time.sleep(.5)
        else:
            raise TimeoutError('PS5 collection deadline expired')
        with self.ps5.connect() as ftp:
            diagnostics = archive_logs(ftp, current['directory'] / 'diagnostics')
        previous = current['state'].get('previous_artifacts', {})
        for name, item in diagnostics.items():
            if name.startswith(('pyrowave-', 'native-')) and item['sha256'] != previous.get(name, {}).get('sha256'):
                (output / name).write_bytes((current['directory'] / 'diagnostics' / name).read_bytes())
        index = {p.name: {'bytes': p.stat().st_size,
                          'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
                 for p in output.iterdir() if p.is_file()}
        (output / 'artifact-index.json').write_text(json.dumps(index, indent=2) + '\n')
        previous = current['state'].get('previous_artifacts', {})
        stale = [name for name, item in index.items()
                 if name in previous and item['sha256'] == previous[name]['sha256']]
        exited = bool(self.ps5.manager_port and collect_run.title_exit(
            self.ps5.host, self.ps5.manager_port, receipt.get('pid')))
        return {'receipt': receipt, 'artifact_index': index, 'title_exit_verified': exited,
                'stale_artifacts': stale, 'installed': current['state'].get('installed'),
                'installed_identity_verified': True, 'installed_identity_source': 'leased FTP eboot and label readback before launch',
                'build_variant': current['state'].get('build_variant'),
                'verified_installation_inventory': current['state'].get('deployed_files'),
                'installation_inventory_source': (current['state'].get('installation_inventory_evidence') or {}).get('source'),
                'installation_inventory_evidence': current['state'].get('installation_inventory_evidence')}

    async def cleanup(self, profile):
        current = self.runs.get(profile.get('run_id'))
        if not current:
            return
        if current['started'] and not current['safe']:
            try:
                if not await _blocking(self._has_receipt, profile['run_id']):
                    status = await _blocking(self._read_status, profile['run_id'])
                    if status.get('state') == 'stopping' and status.get('reason') == 'COMPLETE':
                        await _blocking(self._wait_receipt, current, 15)
                    else:
                        await _blocking(self._command, profile['run_id'], 'cancel', 5)
                        await _blocking(self._wait_receipt, current, 15)
                current['safe'] = bool(await _blocking(self._verify_exit, current))
                if current['safe'] and not (current['directory'] / 'artifacts').exists():
                    # Preserve terminal failure/cancel evidence before releasing ownership.
                    try:
                        evidence = await _blocking(self._collect, current, 10)
                        current['state']['collected'] = evidence
                        self._save(current)
                    except (OSError, ValueError, RuntimeError, TimeoutError, Error) as exc:
                        current['state']['forensic_collection_error'] = type(exc).__name__
                        self._save(current)
            except (OSError, ValueError, RuntimeError, TimeoutError, Error):
                current['safe'] = False
        await self._release_if_safe(profile)

    def _verify_exit(self, current):
        try:
            with self.ps5.connect() as ftp:
                receipt = read_receipt(ftp, ROOT + '/receipts/' + current['state']['run_id'] + '.json')
            return bool(self.ps5.manager_port and collect_run.title_exit(
                self.ps5.host, self.ps5.manager_port, receipt.get('pid')))
        except (OSError, ValueError, Error):
            return False

    async def _release_if_safe(self, profile, force=False):
        run_id = profile.get('run_id')
        current = self.runs.pop(run_id, None)
        if not current:
            return
        safe = current['safe'] or (force and not current['started'])
        if safe:
            current['owner']['safe_to_release'] = True
        # Always unwind the local context; unsafe contexts retain the remote lease.
        await _blocking(current['lease'].__exit__, None, None, None)
        if not safe:
            raise RuntimeError('PS5_REMOTE_LEASE_RETAINED')


class RealHostCoordinatorAdapter:
    """Persistent authenticated connection to the existing Windows host worker."""
    execution_mode = 'real'
    cleanup_timeout_seconds = 30

    def __init__(self, url, token, session_guard=None):
        if not token or len(token) < 32:
            raise ValueError('real host worker requires a token of at least 32 characters')
        endpoint = urlsplit(url)
        if endpoint.scheme not in ('http', 'https') or not endpoint.hostname or endpoint.username \
                or endpoint.password or endpoint.query or endpoint.fragment:
            raise ValueError('worker URL must be HTTP(S) without credentials or query')
        self.url, self.token = url, token
        self.session_guard = session_guard
        self.worker = None
        self.profile = None
        self.run_id = None
        self.report = None

    async def preflight(self, profile, cancel):
        from aiohttp import ClientSession, ClientTimeout
        from ..host.session_guard import bounded_response_json
        async with ClientSession(timeout=ClientTimeout(total=5)) as session:
            async with session.get(self.url+'/health', headers={'Authorization':'Bearer '+self.token},
                                   allow_redirects=False) as response:
                if response.status != 200: raise RuntimeError('HOST_WORKER_UNAVAILABLE')
                health = await bounded_response_json(response)
        if health.get('schema') != 'prosperolight-host-worker/1' or not health.get('interactive_desktop',{}).get('interactive'):
            raise RuntimeError('HOST_WORKER_DESKTOP_UNAVAILABLE')
        if health.get('state') not in ('IDLE','COMPLETED','INCONCLUSIVE'):
            raise RuntimeError('HOST_WORKER_BUSY')
        if cancel.is_set(): raise asyncio.CancelledError
        if self.session_guard:
            inventory = await self.session_guard.ensure_idle(cancel)
            inventory['advertised'] = await self.session_guard.metadata()
            data = await self.session_guard.log_bytes()
            self.log_snapshot = {'bytes':len(data), 'sha256':hashlib.sha256(data).hexdigest()}
            inventory['log_snapshot'] = self.log_snapshot
            return inventory
        return {'verified':False,'reason':'HOST_SESSION_API_NOT_CONFIGURED'}

    async def prepare(self, profile, cancel):
        if cancel.is_set():
            raise asyncio.CancelledError
        self.profile = profile
        self.terminal_session_absence = None
        self.run_id = profile['run_id']
        self.worker = HostWorkerAdapter(self.url, self.token)
        try:
            await self.worker.__aenter__()
            settings = dict(profile.get('host_stimulus', {}))
            timing = profile.get('timing', {})
            client = profile['client']
            settings.setdefault('scene', 'low-motion')
            settings.setdefault('seed', 137)
            settings.setdefault('fps', client['fps'])
            settings.setdefault('seconds', timing.get('measure_seconds', 30))
            settings.setdefault('warmup_seconds', timing.get('warmup_seconds', 0))
            settings.setdefault('monitor', 'virtual')
            settings.setdefault('require_hdr', bool(client.get('hdr', False)))
            response = await self.worker.command(self.run_id, 'prepare', settings,
                                                 timeout=timing.get('ready_seconds', 45) + 75)
            if response.get('state') != 'READY' or not (response.get('ready') or {}).get('ready'):
                raise RuntimeError('HOST_NOT_READY')
        except BaseException:
            await self.worker.__aexit__(None, None, None)
            self.worker = None
            raise

    async def start(self, cancel):
        if cancel.is_set():
            raise asyncio.CancelledError
        response = await self.worker.command(self.run_id, 'start')
        if response.get('state') != 'RUNNING':
            raise RuntimeError('HOST_START_NOT_ACKNOWLEDGED')
        self.start_ack_monotonic = time.monotonic()
        return response

    async def clock_probe(self, cancel):
        if cancel.is_set(): raise asyncio.CancelledError
        return await self.worker.command(self.run_id, 'clock_probe', timeout=10)

    async def measure(self, profile, cancel):
        if cancel.is_set():
            raise asyncio.CancelledError
        timing = profile.get('timing', {})
        samples = []
        capture = await self.session_guard.capture_since(self.log_snapshot) if self.session_guard else None
        pending = asyncio.create_task(self.worker.wait_report(self.run_id,
            timeout=timing.get('warmup_seconds', 0) + timing.get('measure_seconds', 30) + 90))
        try:
            while self.session_guard and not pending.done():
                if cancel.is_set():
                    raise asyncio.CancelledError
                sample = await self.session_guard.sample_session()
                if sample:
                    if samples and sample['session']['uuid'] != samples[0]['session']['uuid']:
                        raise RuntimeError('HOST_SESSION_IDENTITY_CHANGED')
                    samples.append(sample)
                elif samples:
                    duration = timing.get('warmup_seconds', 0) + timing.get('measure_seconds', 30)
                    elapsed = time.monotonic() - getattr(self, 'start_ack_monotonic', time.monotonic())
                    if elapsed < duration:
                        raise RuntimeError('HOST_SESSION_LOST_DURING_MEASUREMENT')
                    # A terminal PS5 exit may precede browser report delivery.
                    # Absence before the complete ACK-bounded window still fails.
                    self.terminal_session_absence = {'elapsed_since_start_ack_seconds': elapsed,
                        'source': 'authenticated RTSP inventory after complete start-ACK-bounded window'}
                    break
                await asyncio.wait({pending}, timeout=1)
            self.report = await pending
        finally:
            if not pending.done():
                pending.cancel()
                await asyncio.gather(pending, return_exceptions=True)
        self.report['terminal_session_absence'] = getattr(self, 'terminal_session_absence', None)
        self.report['session_samples'] = samples
        self.report['capture_evidence'] = capture
        if self.report.get('run_id') != self.run_id:
            raise ValueError('host report identity mismatch')
        return self.report

    async def cleanup(self, profile):
        if not self.worker:
            return
        try:
            result = await self.worker.command(self.run_id, 'status', timeout=5)
            if result.get('state') in ('PREPARING', 'READY', 'STARTING', 'RUNNING'):
                await self.worker.command(self.run_id, 'stop', timeout=20)
        finally:
            await self.worker.__aexit__(None, None, None)
            self.worker = None
