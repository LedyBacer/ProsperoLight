#!/usr/bin/env python3
"""Build one patched AGC runtime object in an independent SDK derivative.
All other SDK bytes remain those of the checksum-pinned 1.0.2 release.
"""
from pathlib import Path
import argparse
import hashlib
import json
import shlex
import shutil
import subprocess
import tarfile
import sys

ROOT=Path(__file__).resolve().parents[2]
ARCHIVE_SHA='30d8e51dd062739b629346102be5932728ba714bc50b1bdcdf08306c8843d9a4'
SOURCE_SHA='a519561626933257213d64a4a8bd5e02b55dceb2'
PATCH=ROOT/'tools/fwcompat/opengl-1.0.2.patch'
def digest(p):
    with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def run(*args,**kw):subprocess.run([str(a) for a in args],check=True,**kw)
def build(folder, payload, archive=None):
    folder=folder.resolve();payload=payload.resolve()
    if folder.exists():raise ValueError('Use a new SDK output directory; never overwrite a verified SDK')
    folder.mkdir(parents=True)
    archive=archive or folder/'upstream-1.0.2.tar.gz'
    if not archive.exists():
        run('curl','-fL','--retry','3','-o',archive,'https://github.com/blackbearreloaded/ps5-opengl/releases/download/v1.0.2/ps5-opengl-sdk-1.0.2.tar.gz')
    if digest(archive)!=ARCHIVE_SHA:raise ValueError('SDK release archive checksum mismatch')
    with tarfile.open(archive) as tar:tar.extractall(folder,filter='data')
    release=folder/'ps5-opengl-sdk-1.0.2'
    run('sha256sum','--check','--strict','--quiet','SHA256SUMS',cwd=release)
    provenance=json.loads((release/'provenance.json').read_text())
    if provenance['source_snapshot_commit']!=SOURCE_SHA:raise ValueError('SDK source revision mismatch')
    for name in ('ps5-opengl','opengnm-psbc'):
        with tarfile.open(release/f'sources/{name}.tar') as tar:tar.extractall(folder/'source',filter='data')
    source=folder/'source/ps5-opengl'
    third=source/'third_party';third.mkdir(exist_ok=True)
    shutil.move(str(folder/'source/opengnm-psbc'),third/'opengnm-psbc')
    with tarfile.open(release/'sources/mesa-26.2.0.tar.xz') as tar:tar.extractall(third,filter='data')
    run('git','apply',source/'toolchain/opengnm-psbc-ps5.patch',cwd=third/'opengnm-psbc')
    run('git','apply','--check',PATCH,cwd=source)
    run('git','apply',PATCH,cwd=source)
    run(sys.executable,ROOT/'tools/fwcompat/test-sdk-runtime.py',source/'src/platform/ps5_agc_native_runtime.c')
    lines=(release/'runtime-config.txt').read_text().splitlines()
    original_root='/home/runner/work/ps5-opengl/ps5-opengl'
    # Same common and native-runtime flags as the released object; only source paths relocate.
    flags=shlex.split(lines[1].replace(original_root,str(source)))
    flags+=['-femulated-tls','-DHAVE_PTHREAD=1','-DHAVE_STRUCT_TIMESPEC=1',
            '-I'+str(third/'opengnm-psbc/libpsbc'),'-Wno-error=unused-function']
    flags+=shlex.split(lines[-1])
    flags+=['-Dmain=ps5_agc_gate2_run','-DAGC_TRIANGLE_SUBMIT=1','-DAGC_RUNTIME_PACKAGES=1']
    obj=folder/'ps5_agc_runtime_backend.o'
    command=[str(payload/'bin/prospero-clang'),*flags,'-c',str(source/'src/platform/ps5_agc_runtime_backend.c'),'-o',str(obj)]
    (folder/'compile-command.json').write_text(json.dumps(command,indent=2)+'\n')
    run(*command)
    sdk=folder/'sdk';shutil.copytree(release/'sdk',sdk)
    core=sdk/'lib/libps5_opengl_core33.a'
    # ar replaces only this member. No production archive is modified.
    run(payload/'bin/prospero-ar','r',core,obj)
    ar=str(payload/'bin/prospero-ar')
    members=subprocess.check_output([ar,'t',str(core)],text=True).splitlines()
    original=release/'sdk/lib/libps5_opengl_core33.a'
    assert members==subprocess.check_output([ar,'t',str(original)],text=True).splitlines()
    for member in members:
        if member!='ps5_agc_runtime_backend.o':
            assert subprocess.check_output([ar,'p',str(core),member])==subprocess.check_output([ar,'p',str(original),member])
    receipt=dict(schema=1,kind='FWCompat diagnostic SDK derivative; NOT hardware validated',
        upstream_version='1.0.2',source_sha=SOURCE_SHA,upstream_archive_sha256=ARCHIVE_SHA,
        patch_sha256=digest(PATCH),object_sha256=digest(obj),runtime_archive_sha256=digest(core),
        compile_command=command)
    (sdk/'fwcompat-sdk.json').write_text(json.dumps(receipt,indent=2)+'\n')
    files=sorted(p for p in sdk.rglob('*') if p.is_file() and p.name!='manifest.sha256')
    (sdk/'manifest.sha256').write_text(''.join(f'{digest(p)}  {p.relative_to(sdk)}\n' for p in files))
    run('sha256sum','--check','--strict','--quiet','manifest.sha256',cwd=sdk)
    print(sdk)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--payload-sdk',type=Path,required=True);p.add_argument('--archive',type=Path)
    a=p.parse_args();build(a.output,a.payload_sdk,a.archive)
