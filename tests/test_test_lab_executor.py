# ps5-native-app-boilerplate - portable executor lease, archive and deploy guards.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import json
from ftplib import error_perm
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools/test-lab/src'), str(ROOT / 'tools/test-lab')]
from pltest.ps5.adapter import Ps5Adapter
from pltest.ps5.ftp import archive_logs, upload
from pltest.ps5.launcher import helper
from pltest.ps5.collector import collect_reconnecting


class FakeFtp:
    def __init__(self):
        self.dirs = {'/data/prosperolight/test-lab', '/data/prosperolight/logs'}
        self.files = {'/data/prosperolight/logs/performance-last.json': b'old',
                      '/data/prosperolight/logs/pairing-key.pem': b'private'}
        self.writes = []
    def __enter__(self): return self
    def __exit__(self, *args): return False
    def mkd(self, path):
        if path in self.dirs: raise error_perm('550 exists')
        self.dirs.add(path)
    def cwd(self, path):
        if path not in self.dirs: raise error_perm('550 missing')
    def nlst(self, path): return [name for name in self.files if name.startswith(path + '/')]
    def retrbinary(self, cmd, append):
        path = cmd.removeprefix('RETR ')
        if path not in self.files: raise error_perm('550 missing')
        append(self.files[path])
    def storbinary(self, cmd, source):
        path = cmd.removeprefix('STOR '); self.files[path] = source.read(); self.writes.append(path)
    def rename(self, src, dst): self.files[dst] = self.files.pop(src)
    def delete(self, path): del self.files[path]
    def rmd(self, path): self.dirs.remove(path)


class ExecutorTests(unittest.TestCase):
    def test_variant_manifest_hash_provenance_and_path_guards(self):
        from pltest.build import validate_variant, load_variant
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            app = root / 'baseline'; app.mkdir()
            (app / 'eboot.bin').write_bytes(b'baseline')
            (app / 'build-label.txt').write_text('test-lab-baseline\n')
            identity = hashlib.sha256(b'baseline').hexdigest()
            provenance = dict(commit='a'*40, eboot_sha256=identity,
                definitions=['PROSPEROLIGHT_TEST_LAB=1', 'PROSPEROLIGHT_PERFORMANCE_DETAIL=1'])
            (root / 'provenance.json').write_text(json.dumps(provenance))
            value = dict(schema='prosperolight-build-variant/1', source_sha='a'*40,
                eboot_sha256=identity, build_label='test-lab-baseline', directory='baseline',
                provenance='provenance.json', build_flags={'TEST_LAB':1, 'PERFORMANCE_DETAIL':1},
                files={p.name:dict(bytes=p.stat().st_size, sha256=hashlib.sha256(p.read_bytes()).hexdigest())
                       for p in app.iterdir()})
            self.assertEqual(validate_variant(value, root)['eboot_sha256'], identity)
            (root / 'baseline.json').write_text(json.dumps(value))
            reference = dict(manifest='baseline.json', sha256=hashlib.sha256((root/'baseline.json').read_bytes()).hexdigest())
            self.assertEqual(load_variant(reference, root)['source_sha'], 'a'*40)
            with self.assertRaisesRegex(ValueError, 'HASH_MISMATCH'):
                load_variant(dict(reference, sha256='0'*64), root)
            with self.assertRaisesRegex(ValueError, 'PROVENANCE_IDENTITY_MISMATCH'):
                validate_variant(dict(value, source_sha='b'*40), root)
            (app/'extra').write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError, 'FILE_MANIFEST_MISMATCH'):
                validate_variant(value, root)
            (app/'extra').unlink()
            (app/'link').symlink_to(app/'eboot.bin')
            with self.assertRaisesRegex(ValueError, 'SYMLINK'):
                validate_variant(value, root)

    def test_remote_exclusion_owner_and_uncertain_run_retains_lease(self):
        ftp = FakeFtp()
        adapter = Ps5Adapter('fake', 'probe', 'launch', connect=lambda: ftp)
        other = Ps5Adapter('fake', 'probe', 'launch', connect=lambda: ftp)
        with adapter.lease('run') as ownership:
            with self.assertRaises(error_perm):
                with other.lease('other'): pass
            ownership['safe_to_release'] = True
        self.assertNotIn('/data/prosperolight/test-lab/executor-lease', ftp.dirs)
        with adapter.lease('unknown'): pass
        self.assertIn('/data/prosperolight/test-lab/executor-lease', ftp.dirs)
        self.assertFalse(adapter.lease_held)
        owner = json.loads(ftp.files['/data/prosperolight/test-lab/executor-lease/owner.json'])
        with self.assertRaises(RuntimeError): adapter.release_lease('unknown', 'different')
        adapter.idle = lambda: (_ for _ in ()).throw(RuntimeError('TITLE_BUSY'))
        with self.assertRaises(RuntimeError): adapter.release_lease('unknown', owner['owner'])
        self.assertIn('/data/prosperolight/test-lab/executor-lease', ftp.dirs)
        adapter.idle = lambda: {'state': 'TITLE_IDLE'}
        adapter.release_lease('unknown', owner['owner'])
        self.assertNotIn('/data/prosperolight/test-lab/executor-lease', ftp.dirs)

    def test_busy_title_never_writes_installation(self):
        ftp = FakeFtp()
        adapter = Ps5Adapter('fake', 'probe', 'launch', connect=lambda: ftp)
        adapter.lease_held = True
        adapter.idle = lambda: (_ for _ in ()).throw(RuntimeError('TITLE_BUSY'))
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory) / 'eboot.bin').write_bytes(b'app')
            with self.assertRaises(RuntimeError):
                adapter.deploy(directory, hashlib.sha256(b'app').hexdigest())
        self.assertEqual(ftp.writes, [])

    def test_archive_allowlist_verified_upload_and_route_before_execution(self):
        ftp = FakeFtp()
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory)
            index = archive_logs(ftp, p / 'old')
            self.assertEqual(list(index), ['performance-last.json'])
            self.assertEqual(index['performance-last.json']['sha256'], hashlib.sha256(b'old').hexdigest())
            upload(ftp, '/data/test', b'new')
            self.assertEqual(ftp.files['/data/test'], b'new')
            image = bytearray(64); image[:6] = b'\x7fELF\x02\x01'; image[18:20] = (62).to_bytes(2, 'little')
            image += b'PLTEST_HELPER_ROUTE=4\x00'; (p / 'wrong.elf').write_bytes(image)
            class Loader:
                def execute(self, *args): raise AssertionError('unsafe ELF executed')
            before = list(ftp.writes)
            with self.assertRaises(ValueError): helper(ftp, Loader(), p / 'wrong.elf', 'id', 'pc', 3)
            self.assertEqual(ftp.writes, before)

    def test_run_build_hash_guard_success_and_no_implicit_relaunch(self):
        ftp = FakeFtp()
        adapter = Ps5Adapter('fake', 'probe', 'launch', connect=lambda: ftp)
        adapter.idle = lambda *args: {'state': 'TITLE_IDLE'}
        adapter.installed = lambda: {'eboot_sha256': 'a' * 64, 'build_label': 'test-lab-fake'}
        profile = {'schema': 'prosperolight-test/1', 'build_label': 'test-lab-fake',
                   'client': {'host': 'pc'}, 'timing': {'hard_seconds': 5}}
        def capture(connect, run_id, output, timeout):
            output.mkdir(); (output / 'performance-last.json').write_bytes(b'new')
            (output / 'artifact-index.json').write_text(json.dumps({'performance-last.json': {
                'sha256': hashlib.sha256(b'new').hexdigest()}}))
            return {'run_id': run_id, 'pid': 123, 'functional_verdict': 'PASS'}
        with tempfile.TemporaryDirectory() as directory:
            with patch('pltest.ps5.adapter.launch', return_value={'state': 'LAUNCH_ACCEPTED'}) as launch, patch('pltest.ps5.adapter.collect_reconnecting', side_effect=capture), patch('pltest.ps5.adapter.title_exit', return_value=True):
                with self.assertRaises(RuntimeError): adapter.run(profile, directory, 'b' * 64)
                launch.assert_not_called()
                result = adapter.run(profile, directory, 'a' * 64)
                self.assertEqual(result['functional_verdict'], 'PASS'); self.assertEqual(launch.call_count, 1)
            with patch('pltest.ps5.adapter.launch', side_effect=TimeoutError('unknown')) as launch:
                with self.assertRaises(TimeoutError): adapter.run(profile, directory, 'a' * 64)
                self.assertEqual(launch.call_count, 1)
                self.assertIn('/data/prosperolight/test-lab/executor-lease', ftp.dirs)
        with self.assertRaises(RuntimeError): adapter.deploy('unused', 'abc')

    def test_read_only_reconnect_then_archive(self):
        ftp = FakeFtp(); calls = []
        def connect():
            calls.append(1)
            if len(calls) == 1: raise ConnectionError('lost')
            return ftp
        def capture(ftp, run_id, output, timeout):
            output.mkdir(); (output / 'receipt.json').write_text('{}'); return {'run_id': run_id}
        with tempfile.TemporaryDirectory() as directory, patch('pltest.ps5.collector.collect', side_effect=capture), patch('pltest.ps5.collector.time.sleep'):
            output = Path(directory) / 'run'
            self.assertEqual(collect_reconnecting(connect, 'run', output, 5)['run_id'], 'run')
            self.assertEqual(len(calls), 2)
            self.assertTrue((output / 'artifact-index.json').is_file())
