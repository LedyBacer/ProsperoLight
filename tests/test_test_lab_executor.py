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
        if path!='/' and path not in self.dirs and not any(p.startswith(path+'/') for p in self.files.keys() | self.dirs): raise error_perm('550 missing')
    def nlst(self, path): return [name for name in self.files if name.startswith(path + '/')]
    def mlsd(self, path):
        if path!='/' and path not in self.dirs and not any(p.startswith(path+'/') for p in self.files.keys() | self.dirs):
            raise error_perm('550 missing')
        entries={}
        for item in self.files.keys() | self.dirs:
            if item.startswith(path+'/'):
                relative=item[len(path)+1:]; name=relative.split('/')[0]
                entries[name]={'type':'dir' if '/' in relative or item in self.dirs else 'file'}
        return list(entries.items())
    def retrlines(self, command, callback):
        if not command.startswith('MLSD '):raise error_perm('502 unsupported')
        for name,facts in self.mlsd(command[5:]):
            callback(';'.join(k+'='+v for k,v in facts.items())+'; '+name)
    def voidcmd(self, command):
        if command!='TYPE I':raise AssertionError(command)
        return '200 Type set to I'
    def size(self, path):
        if path not in self.files:raise error_perm('550 missing')
        return len(self.files[path])
    def retrbinary(self, cmd, append):
        path = cmd.removeprefix('RETR ')
        if path not in self.files: raise error_perm('550 missing')
        append(self.files[path])
    def storbinary(self, cmd, source):
        path = cmd.removeprefix('STOR '); self.files[path] = source.read(); self.writes.append(path)
    def rename(self, src, dst): self.files[dst] = self.files.pop(src)
    def delete(self, path): del self.files[path]
    def rmd(self, path): self.dirs.remove(path)


class DrakmorFtp(FakeFtp):
    """Actual command set/format: conveniences fail if accidentally called."""
    def __init__(self):
        super().__init__();self.self2elf=True;self.self_calls=0;self.list_commands=[];self.self_reply_code='200'
    def getwelcome(self):return '220-Welcome to ftpsrv.elf running on pid 123'
    def sendcmd(self, command):
        if command=='SELF':
            self.self_calls+=1;self.self2elf=not self.self2elf
            return self.self_reply_code+' SELF transfer mode '+('enabled' if self.self2elf else 'disabled')
        raise error_perm('502 Command not recognized')
    def mlsd(self, *args):raise AssertionError('MLSD convenience is unsupported')
    def nlst(self, *args):raise AssertionError('NLST is unsupported')
    def retrlines(self, command, callback):
        if command.startswith('MLSD '):raise error_perm('502 Command not recognized')
        if not command.startswith('LIST '):raise AssertionError(command)
        self.list_commands.append(command)
        path=command[5:];entries=FakeFtp.mlsd(self,path)
        callback('drwxr-xr-x 2 0 0 4096 Oct 14 12:34 .')
        callback('drwxr-xr-x 2 0 0 4096 Oct 14 12:34 ..')
        for name,facts in entries:
            directory=facts['type']=='dir';size=4096 if directory else len(self.files[path+'/'+name])
            callback(('drwxr-xr-x' if directory else '-rw-r--r--')+' 1 0 0 '+str(size)+' Oct  4 12:34 '+name)


class ExecutorTests(unittest.TestCase):
    def test_explicit_remote_builder_configuration_and_source_guard(self):
        from pltest.remote_builder import validate_config, build
        value = dict(schema='prosperolight-linux-builder/1', ssh_host='root@builder',
            repository_url='https://example.com/ProsperoLight.git',
            repository='/repo',workspace_root='/work',dependencies='/sdk/deps',runtime_bin='/sdk/bin',jobs=2)
        self.assertEqual(validate_config(value)['jobs'],2)
        for invalid in (dict(value,ssh_host='-oProxyCommand=bad'),dict(value,jobs=8),
                        dict(value,workspace_root='/tmp/../production'),dict(value,repository='/repo;id'),
                        dict(value,password='unsupported')):
            with self.assertRaises(ValueError): validate_config(invalid)
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaisesRegex(ValueError,'BUILD_SOURCE_SHA_REQUIRED'):
                build(value,'main',Path(folder)/'app',Path(folder)/'provenance.json')
            with self.assertRaisesRegex(ValueError,'BUILD_OUTPUT_ALREADY_EXISTS'):
                build(value,'a'*40,Path(folder),Path(folder)/'provenance.json')

    def test_variant_manifest_hash_provenance_and_path_guards(self):
        from pltest.build import validate_variant, load_variant
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            app = root / 'baseline'; app.mkdir()
            (app / 'eboot.bin').write_bytes(b'baseline')
            (app / 'build-label.txt').write_text('test-lab-baseline\n')
            identity = hashlib.sha256(b'baseline').hexdigest()
            provenance = dict(commit='a'*40, eboot_sha256=identity, source_sha256='d'*64,
                definitions=['PROSPEROLIGHT_TEST_LAB=1', 'PROSPEROLIGHT_PERFORMANCE_DETAIL=1'])
            (root / 'provenance.json').write_text(json.dumps(provenance))
            value = dict(schema='prosperolight-build-variant/1', source_sha='a'*40,
                eboot_sha256=identity, build_label='test-lab-baseline', directory='baseline',
                provenance='provenance.json', build_flags={'TEST_LAB':1, 'PERFORMANCE_DETAIL':1},
                provenance_sha256=hashlib.sha256((root/'provenance.json').read_bytes()).hexdigest(),
                source_tree_sha256='d'*64,
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

    def test_missing_install_and_no_mlsd_capability_are_prewrite(self):
        from pltest.ps5.adapter import INSTALL, DeploymentPreflightError
        ftp=FakeFtp();adapter=Ps5Adapter('fake','probe','launch',connect=lambda:ftp)
        adapter.idle=lambda:None
        self.assertIsNone(adapter.installed())
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory)/'eboot.bin').write_bytes(b'app')
            with adapter.lease('clean') as owner:
                adapter.deploy(directory,hashlib.sha256(b'app').hexdigest())
                owner['safe_to_release']=True
            self.assertIn(INSTALL+'/eboot.bin',ftp.files)
            ftp.writes.clear()
            def unsupported(path):raise error_perm('502 MLSD not implemented')
            ftp.mlsd=unsupported
            with adapter.lease('unsupported') as owner:
                try:adapter.deploy(directory,hashlib.sha256(b'app').hexdigest())
                except DeploymentPreflightError as exc:
                    owner['safe_to_release']=True
                    self.assertIn('FTP_LISTING_CAPABILITY_REQUIRED',str(exc))
                else:self.fail('unsupported inventory accepted')
            self.assertFalse(any(p.startswith(INSTALL+'/') for p in ftp.writes))
            self.assertNotIn('/data/prosperolight/test-lab/executor-lease',ftp.dirs)

    def test_drakmor_list_install_replay_archive_and_collection(self):
        import launch_spike
        from pltest.ps5.adapter import INSTALL
        from pltest.ps5.ftp import directory_entries
        ftp=DrakmorFtp();adapter=Ps5Adapter('fake','probe','launch',connect=lambda:ftp);adapter.idle=lambda:None
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);app=root/'app';app.mkdir();(app/'eboot.bin').write_bytes(b'app')
            (app/'build-label.txt').write_bytes(b'test-lab-A')
            (app/'assets').mkdir();(app/'assets'/'with spaces.bin').write_bytes(b'asset')
            digest=hashlib.sha256(b'app').hexdigest()
            for label in ('clean','same','different'):
                if label=='different':(app/'eboot.bin').write_bytes(b'new');digest=hashlib.sha256(b'new').hexdigest()
                with adapter.lease(label) as owner:
                    index=adapter.deploy(app,digest);owner['safe_to_release']=True
                self.assertEqual(index['eboot.bin']['sha256'],digest)
                self.assertEqual(adapter.installation_inventory_evidence['source'],'LIST_READBACK')
                self.assertTrue(adapter.installation_inventory_evidence['enumeration_limitations'])
            ftp.files[INSTALL+'/unknown']=b'keep'
            with adapter.lease('unknown'):
                from pltest.ps5.adapter import DeploymentPreflightError
                with self.assertRaises(DeploymentPreflightError):adapter.deploy(app,digest)
            self.assertIn(INSTALL+'/unknown',ftp.files)
            owner=json.loads(ftp.files['/data/prosperolight/test-lab/executor-lease/owner.json'])
            adapter.release_lease('unknown',owner['owner']);del ftp.files[INSTALL+'/unknown']
            original=ftp.rename
            def corrupt(src,dst):
                original(src,dst)
                if dst.endswith('/eboot.bin'):ftp.files[dst]=b'bad'
            ftp.rename=corrupt
            with adapter.lease('corrupt'):
                with self.assertRaisesRegex(RuntimeError,'FINAL_MANIFEST_MISMATCH'):adapter.deploy(app,digest)
            self.assertIn('/data/prosperolight/test-lab/executor-lease',ftp.dirs)
            ftp.rename=original
            launch_spike.stage(ftp,launch_spike.request_bytes('r','PC'))
            with self.assertRaisesRegex(RuntimeError,'unconsumed'):launch_spike.stage(ftp,launch_spike.request_bytes('r','PC'))
            profile={'schema':'prosperolight-test/1','run_id':'r','client':{'host':'PC'}}
            launch_spike.stage_profile(ftp,profile,'r')
            with self.assertRaisesRegex(RuntimeError,'replay'):launch_spike.stage_profile(ftp,profile,'r')
            self.assertEqual(list(archive_logs(ftp,root/'logs')),['performance-last.json'])
            ftp.files['/data/prosperolight/logs/debug-trace.txt']=b'trace'
            ftp.files['/data/prosperolight/test-lab/receipts/r.json']=json.dumps({
                'schema':'prosperolight-test-receipt/2','run_id':'r','artifacts_complete':True,
                'artifacts':[{'name':'debug-trace.txt','bytes':5}]}).encode()
            collect_reconnecting(lambda:ftp,'r',root/'collected',1)
            self.assertEqual((root/'collected/debug-trace.txt').read_bytes(),b'trace')
            self.assertFalse(ftp.self2elf);self.assertEqual(ftp.self_calls,1)
            self.assertTrue(all(command.startswith('LIST ') for command in ftp.list_commands))

    def test_drakmor_raw_self_reply_codes_and_unknown_state(self):
        from pltest.ps5.ftp import raw_transfer
        for code in ('200','226'):
            for initial in (False,True):
                with self.subTest(code=code,initial=initial):
                    ftp=DrakmorFtp();ftp.self_reply_code=code;ftp.self2elf=initial
                    raw_transfer(ftp)
                    self.assertFalse(ftp.self2elf)
                    calls=ftp.self_calls;raw_transfer(ftp)
                    self.assertEqual(ftp.self_calls,calls)
        ftp=DrakmorFtp();ftp.self_reply_code='250'
        with self.assertRaisesRegex(RuntimeError,'FTP_RAW_TRANSFER_UNCONFIRMED'):
            raw_transfer(ftp)
        self.assertFalse(getattr(ftp,'_pltest_raw_transfer',False))

    def test_list_rejects_malformed_ambiguous_and_unbounded_entries(self):
        from pltest.ps5.ftp import directory_entries
        prefix='-rw-r--r-- 1 0 0 1 Oct 14 12:34 '
        for lines in ([prefix+'../bad'],[prefix+'x',prefix+'x'],['total 3'],
                      ['-rrrrrrrrr 1 0 0 1 Oct 14 12:34 bad'],[prefix+' bad'],[prefix+'x\tbad'],[prefix+'x'*4097],
                      ['lrwxrwxrwx 1 0 0 1 Oct 14 12:34 link -> target'],[prefix+str(i) for i in range(1025)]):
            class Invalid(DrakmorFtp):
                def retrlines(self,command,callback):
                    if command.startswith('MLSD '):raise error_perm('502 unsupported')
                    for line in lines:callback(line)
            with self.assertRaises(RuntimeError):directory_entries(Invalid(),'/data')
        with self.assertRaises(ValueError):directory_entries(DrakmorFtp(),'/'+ '/'.join(['deep']*18))

    def test_final_installation_readback_and_unknown_files_fail_closed(self):
        from pltest.ps5.adapter import INSTALL
        ftp=FakeFtp();adapter=Ps5Adapter('fake','probe','launch',connect=lambda:ftp)
        adapter.lease_held=True;adapter.idle=lambda:None
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);(root/'eboot.bin').write_bytes(b'app');(root/'z.asset').write_bytes(b'asset')
            digest=hashlib.sha256(b'app').hexdigest()
            index=adapter.deploy(root,digest)
            self.assertEqual(index['eboot.bin']['sha256'],digest)
            self.assertEqual(adapter.installation_inventory_evidence['source'],'MLSD_READBACK')
            ftp.files[INSTALL+'/old.asset']=b'old'
            before=list(ftp.writes)
            with self.assertRaisesRegex(RuntimeError,'INSTALL_UNEXPECTED_FILE'):
                adapter.deploy(root,digest)
            self.assertEqual(ftp.writes,before);self.assertIn(INSTALL+'/old.asset',ftp.files)
            del ftp.files[INSTALL+'/old.asset']
            original=ftp.rename
            def corrupt_after_last(src,dst):
                original(src,dst)
                if dst.endswith('/z.asset'):ftp.files[INSTALL+'/eboot.bin']=b'bad'
            ftp.rename=corrupt_after_last
            with self.assertRaisesRegex(RuntimeError,'INSTALL_FINAL_MANIFEST_MISMATCH'):
                adapter.deploy(root,digest)
        ftp=FakeFtp();adapter.connect=lambda:ftp
        def corrupt_destination(src,dst):ftp.files[dst]=b'bad';del ftp.files[src]
        ftp.rename=corrupt_destination
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory)/'eboot.bin').write_bytes(b'new')
            with self.assertRaisesRegex(RuntimeError,'INSTALL_FINAL_MANIFEST_MISMATCH'):
                adapter.deploy(directory,hashlib.sha256(b'new').hexdigest())

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
