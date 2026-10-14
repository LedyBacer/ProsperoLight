# ps5-native-app-boilerplate - PR0 safety and wire regression tests.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('launch_spike', ROOT / 'tools/test-lab/launch_spike.py')
spike = importlib.util.module_from_spec(spec)
spec.loader.exec_module(spike)


class LaunchSpikeTests(unittest.TestCase):
    def test_fields(self):
        for identifier in ('../x', '', 'a' * 128, 'x\0y'):
            with self.assertRaises(ValueError):
                spike.request_bytes(identifier, 'Gaming PC')
        for pc in ('', 'x\0y', 'a' * 128, 'x\ny', '"host"'):
            with self.assertRaises(ValueError):
                spike.request_bytes('safe', pc)
        data = spike.request_bytes('safe', '192.168.1.2:47989')
        self.assertEqual(len(data), 256)
        self.assertEqual(data[:128].rstrip(b'\0'), b'safe')

    def test_c_parser_rejects_malformed(self):
        compiler = shutil.which('cc')
        if not compiler:
            self.skipTest('C compiler unavailable')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'test.c').write_text('''#include "protocol.h"
#include <assert.h>
int main(void) {
char r[256] = {0}; strcpy(r, "run-1"); strcpy(r+128, "Gaming PC");
assert(valid_request(r)); r[127]=1; assert(!valid_request(r)); r[127]=0;
r[0]='/'; assert(!valid_request(r)); r[0]='r';
memset(r+128, 'x',128); assert(!valid_request(r));
return 0; }
''')
            subprocess.run([compiler, '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'tools/test-lab/ps5/launcher'), str(path / 'test.c'),
                            '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)

    def test_stage_refuses_existing_request(self):
        class Fake:
            def mkd(self, path): pass
            def retrlines(self, command, callback): callback('type=file; request.bin')
            def storbinary(self, *args): raise AssertionError('must not upload')
        with self.assertRaises(RuntimeError):
            spike.stage(Fake(), spike.request_bytes('safe', 'PC'))

    def test_elf_validation(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'bad.elf'
            path.write_bytes(b'not ELF')
            with self.assertRaises(ValueError): spike.elf_bytes(path)

    def test_profile_stage_refuses_replay_and_checks_readback(self):
        import json
        class Fake:
            def __init__(self): self.data = b''; self.existing = []; self.renamed = False
            def mkd(self, path): pass
            def retrlines(self, command, callback):
                for name in self.existing:callback('type=file; '+name)
            def storbinary(self, command, stream): self.data = stream.read()
            def retrbinary(self, command, append): append(self.data)
            def rename(self, source, target): self.renamed = True
        profile = {'schema': 'prosperolight-test/1', 'run_id': 'safe', 'client': {'host': 'PC'}}
        ftp = Fake()
        spike.stage_profile(ftp, profile, 'safe')
        self.assertTrue(ftp.renamed)
        self.assertEqual(json.loads(ftp.data), profile)
        ftp = Fake(); ftp.existing = ['safe.json']
        with self.assertRaises(RuntimeError): spike.stage_profile(ftp, profile, 'safe')
        self.assertEqual(ftp.data, b'')
        with self.assertRaises(ValueError): spike.stage_profile(Fake(), profile, '../bad')
        with self.assertRaises(ValueError):
            spike.stage_profile(Fake(), {**profile, 'padding': 'x' * 17000}, 'safe')

    def test_profile_status_identity_and_failure(self):
        import json
        class Fake:
            def __init__(self, identifier): self.identifier = identifier
            def retrbinary(self, command, append):
                append(json.dumps({'schema': 'prosperolight-test-status/1',
                                   'run_id': self.identifier, 'state': 'failed',
                                   'reason': 'BUILD_LABEL_MISMATCH'}).encode())
        self.assertEqual(spike.profile_status(Fake('safe'), 'safe')['state'], 'failed')
        with self.assertRaises(RuntimeError): spike.profile_status(Fake('wrong'), 'safe')

    def test_payload_lifecycle_with_system_service_mocks(self):
        compiler = shutil.which('cc')
        if not compiler:
            self.skipTest('C compiler unavailable')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'sys').mkdir()
            (path / 'sys/proc.h').write_text('')
            (path / 'sys/user.h').write_text('#include <sys/types.h>\nstruct kinfo_proc { int ki_structsize; pid_t ki_pid; };\n')
            (path / 'sys/sysctl.h').write_text('''#include <stddef.h>
#define CTL_KERN 1
#define KERN_PROC 2
#define KERN_PROC_PROC 3
#define KERN_PROC_ARGS 7
int sysctl(const int *, unsigned, void *, size_t *, const void *, size_t);
''')
            (path / 'test.c').write_text(r'''
#define main payload_main
#include "main.c"
#undef main
#include <assert.h>
static int active = -1, title_rc = 0, user_rc = 0, launch_rc = 42, calls = 0;
int sceLncUtilGetAppIdOfRunningBigApp(void) { return active; }
int sceLncUtilGetAppTitleId(uint32_t a, char *t) {
    (void)a; strcpy(t,"PPSA99002"); return title_rc;
}
int sceKernelGetAppInfo(pid_t p, struct app_info *i) { (void)p; memset(i,0,sizeof(*i)); return 0; }
int sysctl(const int *m, unsigned count, void *p, size_t *size, const void *v, size_t n) {
    (void)m; (void)count; (void)v; (void)n;
    if (!p) { *size = sizeof(struct kinfo_proc); return 0; }
    struct kinfo_proc k = { sizeof(k), 0 }; memcpy(p,&k,sizeof(k)); return 0;
}
int sceUserServiceInitialize(void *p) { (void)p; return 0; }
int sceUserServiceGetForegroundUser(int32_t *u) { *u = 12; return user_rc; }
int sceSystemServiceLaunchApp(const char *t, char *const argv[], struct launch_param *p) {
    assert(!strcmp(t,"PPSA99002")); assert(p->user_id==12);
    assert(!strcmp(argv[0],"--pc")); assert(!strcmp(argv[1],"PC"));
    assert(!strcmp(argv[2],"--app")); assert(!strcmp(argv[3],"Desktop"));
    assert(!strcmp(argv[4],"--exit-after-stream")); assert(!strcmp(argv[5],"--launch-proof")); assert(argv[6]); assert(argv[7]==NULL);
    ++calls; return launch_rc;
}
static void run(const char *id, const char *expected) {
    char r[256] = {0}; strcpy(r,id); strcpy(r+128,"PC");
    FILE *f=fopen(ROOT "/request.bin","wb"); assert(f); assert(fwrite(r,1,256,f)==256); fclose(f);
    assert(payload_main()==0);
    char name[512], text[1024]={0}; snprintf(name,sizeof(name),ROOT "/%s.json",id);
    f=fopen(name,"rb"); assert(f); assert(fread(text,1,sizeof(text)-1,f)>0); fclose(f);
    assert(strstr(text,expected)); assert(payload_main()!=0); /* consumed request */
}
int main(void) {
    active=42; run("active","ALREADY_RUNNING"); assert(calls==0);
    title_rc=-1; run("probe","PROCESS_PROBE_FAILED"); assert(calls==0);
    active=-1; title_rc=0; user_rc=-1; run("user","NO_USER"); assert(calls==0);
    user_rc=0; launch_rc=(int32_t)0x80940010u;
    run("queued","LAUNCH_QUEUED_UNVERIFIED"); assert(calls==1);
    launch_rc=42; run("accepted","LAUNCH_ACCEPTED"); assert(calls==2);
    return 0;
}
''')
            subprocess.run([compiler, '-std=c11', '-D_POSIX_C_SOURCE=200809L',
                            '-DROOT="' + str(path) + '"', '-Wall', '-Wextra', '-Werror',
                            '-I', str(path), '-I', str(ROOT / 'tools/test-lab/ps5/launcher'),
                            str(path / 'test.c'), '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)

    def test_application_receipt_proves_received_arguments(self):
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler unavailable')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'test.cpp').write_text(r'''
#include "launch_proof.hpp"
#include <cassert>
int main() {
    char *argv[] = {const_cast<char *>("eboot.bin"), const_cast<char *>("--pc"),
        const_cast<char *>("PC"), const_cast<char *>("--app"), const_cast<char *>("Desktop"),
        const_cast<char *>("--exit-after-stream"), const_cast<char *>("--launch-proof"),
        const_cast<char *>("run-1")};
    assert(!prosperolight::write_launch_proof(0,nullptr));
    argv[7]=const_cast<char *>("../invalid"); assert(!prosperolight::write_launch_proof(8,argv));
    argv[7]=const_cast<char *>("run-1"); argv[3]=nullptr;
    assert(!prosperolight::write_launch_proof(8,argv)); argv[3]=const_cast<char *>("--app");
    assert(prosperolight::write_launch_proof(8,argv));
    assert(!prosperolight::write_launch_proof(8,argv));
}
''')
            subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror',
                            '-DPROSPEROLIGHT_LAUNCH_PROOF_ROOT="' + str(path) + '"',
                            '-I', str(ROOT / 'include'), str(path / 'test.cpp'),
                            '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)
            import json
            receipt = json.loads((path / 'run-1-app.json').read_text())
            self.assertTrue(receipt['argv_verified'])
            self.assertEqual(receipt['request_id'], 'run-1')
            self.assertEqual(receipt['pc'], 'PC')
            self.assertEqual(receipt['app'], 'Desktop')
