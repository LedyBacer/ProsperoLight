# ps5-native-app-boilerplate - portable PS5 lease, preflight, run and collection.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import json
import time
import uuid
import re
from contextlib import contextmanager
from ftplib import FTP, error_perm
from pathlib import Path, PurePosixPath
from collect_run import title_exit
from control_run import command
from launch_spike import request_bytes, read_receipt
from .ftp import retrieve, upload, archive_logs, installation_inventory, installation_exists
from .elfldr import ElfLoader
from .launcher import helper, launch
from .collector import collect_reconnecting

ROOT = '/data/prosperolight/test-lab'
INSTALL = '/data/homebrew/PPSA99002'


class DeploymentPreflightError(RuntimeError):
    """No installation mutation has been attempted; lease may be released."""


def save(path, value):
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.replace(path)


class Ps5Adapter:
    def __init__(self, host, probe_elf, launch_elf, ftp_port=2121, loader_port=9021,
                 manager_port=8084, connect=None, loader=None):
        if not host or any(c in host for c in '/\\?#@\r\n '):
            raise ValueError('expected PS5 hostname or address')
        if any(not isinstance(port, int) or not 1 <= port <= 65535
               for port in (ftp_port, loader_port) + ((manager_port,) if manager_port else ())):
            raise ValueError('invalid transport port')
        self.host, self.manager_port = host, manager_port
        self.probe_elf, self.launch_elf = Path(probe_elf), Path(launch_elf)
        def default_connect():
            ftp = FTP()
            ftp.connect(host, ftp_port, timeout=5)
            ftp.login('anonymous', 'codex')
            return ftp
        self.connect = connect or default_connect
        self.loader = loader or ElfLoader(host, loader_port, manager_port)
        self.lease_held = False

    @contextmanager
    def lease(self, run_id):
        request_bytes(run_id, 'unused')
        owner = {'schema': 'prosperolight-test-lease/1', 'run_id': run_id,
                 'owner': str(uuid.uuid4()), 'created_unix': time.time()}
        path = ROOT + '/executor-lease'
        with self.connect() as ftp:
            try:
                ftp.mkd(ROOT)
            except error_perm:
                ftp.cwd(ROOT)
            # MKD is the shared lock across coordinators; existing/stale lease refuses.
            ftp.mkd(path)
            upload(ftp, path + '/owner.json', json.dumps(owner).encode())
        self.lease_held = True
        terminal = {'safe_to_release': False, 'owner': owner}
        try:
            yield terminal
        finally:
            self.lease_held = False
            if terminal['safe_to_release']:
                with self.connect() as ftp:
                    if read_receipt(ftp, path + '/owner.json') != owner:
                        raise RuntimeError('lease owner changed; refusing release')
                    ftp.delete(path + '/owner.json')
                    ftp.rmd(path)
            # Uncertain launch/deploy/crash retains lease; no timer-based takeover.

    def release_lease(self, run_id, owner_id):
        request_bytes(run_id, 'unused')
        path = ROOT + '/executor-lease'
        with self.connect() as ftp:
            owner = read_receipt(ftp, path + '/owner.json')
        if owner.get('run_id') != run_id or owner.get('owner') != owner_id:
            raise RuntimeError('lease identity mismatch; refusing release')
        self.idle()
        with self.connect() as ftp:
            if read_receipt(ftp, path + '/owner.json') != owner:
                raise RuntimeError('lease changed during recovery')
            ftp.delete(path + '/owner.json'); ftp.rmd(path)
        return {'run_id': run_id, 'state': 'LEASE_RELEASED', 'launch_retried': False}

    def idle(self, pc='unused'):
        with self.connect() as ftp:
            result = helper(ftp, self.loader, self.probe_elf, str(uuid.uuid4()), pc, 3)
        if result.get('state') != 'TITLE_IDLE':
            raise RuntimeError('PS5 is busy or uninspectable: ' + str(result.get('state')))
        return result

    def installed(self):
        with self.connect() as ftp:
            try:
                image = retrieve(ftp, INSTALL + '/eboot.bin')
            except error_perm as exc:
                if str(exc).startswith('550') and not installation_exists(ftp,INSTALL):
                    return None
                raise
            label = retrieve(ftp, INSTALL + '/build-label.txt', 256).decode('ascii').strip()
        return {'eboot_sha256': hashlib.sha256(image).hexdigest(), 'build_label': label}

    def deploy(self, directory, expected_eboot_sha256):
        if not self.lease_held:
            raise RuntimeError('deployment requires executor lease')
        try:
            directory = Path(directory)
            files = sorted(p for p in directory.rglob('*') if p.is_file())
            if not files or len(files) > 512 or any(p.is_symlink() for p in directory.rglob('*')):
                raise ValueError('invalid or unbounded app folder')
            if hashlib.sha256((directory / 'eboot.bin').read_bytes()).hexdigest() != expected_eboot_sha256:
                raise ValueError('build artifact hash mismatch')
            index = {p.relative_to(directory).as_posix(): {'bytes':p.stat().st_size,
                     'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in files}
            with self.connect() as ftp:
                installation_inventory(ftp, INSTALL, index, readback=False)
        except Exception as exc:
            raise DeploymentPreflightError(str(exc)) from exc
        # ELF execution is outside the provably safe read-only preflight.
        self.idle()
        try:
            ftp_connection = self.connect()
        except Exception as exc:
            raise DeploymentPreflightError(str(exc)) from exc
        with ftp_connection as ftp:
            for source in files:
                name = source.relative_to(directory).as_posix()
                if any(not part or part in ('.', '..') for part in PurePosixPath(name).parts):
                    raise ValueError('unsafe deployment path')
                path = INSTALL + '/' + name
                parent = PurePosixPath(path).parent
                for ancestor in reversed(parent.parents):
                    if str(ancestor) in ('/', '.', '/data'):
                        continue
                    try:
                        ftp.mkd(str(ancestor))
                    except error_perm:
                        ftp.cwd(str(ancestor))
                try:
                    ftp.mkd(str(parent))
                except error_perm:
                    ftp.cwd(str(parent))
                data = source.read_bytes()
                upload(ftp, path, data)
            evidence={}
            actual=installation_inventory(ftp, INSTALL, index, evidence=evidence)
            evidence.update(expected_file_hashes_verified=True,expected_file_count=len(actual))
            self.installation_inventory_evidence=evidence
            return actual

    def run(self, profile, output, expected_eboot_sha256, deploy_directory=None):
        if not re.fullmatch(r'[0-9a-f]{64}', expected_eboot_sha256):
            raise ValueError('expected exact SHA-256 of developer eboot.bin')
        profile = json.loads(json.dumps(profile))
        profile['run_id'] = str(uuid.uuid4())
        run_id = profile['run_id']
        request_bytes(run_id, profile['client']['host'])
        if profile.get('schema') != 'prosperolight-test/1' or not profile.get('build_label'):
            raise ValueError('versioned profile and expected build_label required')
        hard = profile.get('timing', {}).get('hard_seconds', 120)
        if not isinstance(hard, int) or not 1 <= hard <= 1800:
            raise ValueError('invalid hard deadline')
        output = Path(output) / run_id
        output.mkdir(parents=True, exist_ok=False)
        save(output / 'profile.json', profile)
        state = {'run_id': run_id, 'stage': 'PREFLIGHT', 'functional_verdict': 'INCONCLUSIVE'}
        save(output / 'executor.json', state)
        with self.lease(run_id) as ownership:
            state['lease'] = ownership['owner']
            save(output / 'executor.json', state)
            started = False
            try:
                ownership['safe_to_release'] = False
                state['preflight'] = self.idle(profile['client']['host'])
                ownership['safe_to_release'] = True
                with self.connect() as ftp:
                    if deploy_directory:
                        installation_exists(ftp,INSTALL)
                    state['previous_artifacts'] = archive_logs(ftp, output / 'prelaunch')
                if deploy_directory:
                    ownership['safe_to_release'] = False
                    state['stage'] = 'DEPLOY'
                    save(output / 'executor.json', state)
                    try:
                        state['deployed'] = self.deploy(deploy_directory, expected_eboot_sha256)
                        state['installation_inventory_evidence']=self.installation_inventory_evidence
                    except DeploymentPreflightError:
                        ownership['safe_to_release'] = True
                        raise
                    ownership['safe_to_release'] = True
                actual = self.installed()
                state['installed'] = actual
                if actual != {'eboot_sha256': expected_eboot_sha256,
                              'build_label': profile['build_label']}:
                    raise RuntimeError('installed build hash/label mismatch')
                state['stage'] = 'START_PS5'
                save(output / 'executor.json', state)
                ownership['safe_to_release'] = False
                with self.connect() as ftp:
                    state['launch'] = launch(ftp, self.loader, self.launch_elf, profile)
                started = True
                state['stage'] = 'COLLECT'
                save(output / 'executor.json', state)
                receipt = collect_reconnecting(self.connect, run_id, output / 'artifacts', hard + 30)
                final_index = json.loads((output / 'artifacts/artifact-index.json').read_text())
                stale = [name for name, item in final_index.items()
                         if name in state['previous_artifacts'] and
                         item['sha256'] == state['previous_artifacts'][name]['sha256']]
                exited = bool(self.manager_port and title_exit(self.host, self.manager_port, receipt.get('pid')))
                ownership['safe_to_release'] = exited
                state.update(stage='COMPLETED', receipt=receipt, title_exit_verified=exited,
                             stale_artifacts=stale,
                             functional_verdict=receipt.get('functional_verdict') if exited and not stale else 'INCONCLUSIVE')
                save(output / 'executor.json', state)
                return state
            except BaseException as exc:
                state.update(stage=state['stage'], reason_code=type(exc).__name__,
                             reason=str(exc), functional_verdict='INCONCLUSIVE')
                save(output / 'executor.json', state)
                if started:
                    try:
                        with self.connect() as ftp:
                            command(ftp, run_id, 'cancel', timeout=5)
                    except (OSError, ValueError, RuntimeError, error_perm):
                        pass
                raise
