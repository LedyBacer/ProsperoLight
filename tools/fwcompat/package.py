#!/usr/bin/env python3
"""Freeze full ZIP, exact symbols and the byte-level artifact association."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys
import zipfile
ROOT=Path(__file__).resolve().parents[2]
def digest(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def package(folder, variant, sha):
    app=folder/'PPSA99002'
    assert (app/'eboot.bin').stat().st_size and (folder/'llvm-pie.elf').stat().st_size
    assert (app/'build-label.txt').read_text().strip()==f'FWCompat {variant} {sha[:7]}'
    provenance=json.loads((folder/'build-provenance.json').read_text())
    assert provenance['commit']==sha
    assert provenance['eboot_sha256']==digest(app/'eboot.bin')
    assert 'FWCOMPAT_DIAGNOSTICS=1' in provenance['definitions']
    assert f'FWCOMPAT_VARIANT_ID={variant.replace("-","_")}' in provenance['definitions']
    archive=folder/f'ProsperoLight-FWCompat-{variant}.zip'
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
        for p in sorted(app.rglob('*')):
            if p.is_file():
                if p.is_symlink():raise ValueError('Symlink in app')
                z.write(p,p.relative_to(folder))
    subprocess.run([sys.executable,str(ROOT/'tools/zip-open-modes.py'),str(archive)],check=True)
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        assert all(n.startswith('PPSA99002/') and '..' not in Path(n).parts for n in z.namelist())
        assert json.loads(z.read('PPSA99002/sce_sys/param.json'))['titleId']=='PPSA99002'
        for n in ('sce_module/libc.prx','sce_sys/param.json','eboot.bin','build-label.txt'):
            assert z.read('PPSA99002/'+n)
    receipt=dict(schema=1,variant=variant,source_sha=sha,
        artifacts={p.name:digest(p) for p in (archive,folder/'llvm-pie.elf',folder/'build-provenance.json',folder/'fwcompat-sdk.json')},
        eboot_sha256=digest(app/'eboot.bin'),hardware_validation='NOT RUN')
    (folder/'artifact-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    files=sorted(p for p in folder.iterdir() if p.is_file() and p.name!='SHA256SUMS')
    (folder/'SHA256SUMS').write_text(''.join(f'{digest(p)}  {p.name}\n' for p in files))
if __name__=='__main__':package(Path(sys.argv[1]),sys.argv[2],sys.argv[3])
