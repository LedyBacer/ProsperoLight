# ps5-native-app-boilerplate - explicitly configured clean Linux SDK builds.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import inspect
import json
from pathlib import Path
import re
import subprocess
from urllib.parse import urlsplit


def validate_config(value):
    keys = {'schema','ssh_host','ssh_port','identity_file','repository','repository_url','workspace_root','dependencies','runtime_bin','jobs'}
    if not isinstance(value,dict) or set(value)-keys or value.get('schema') != 'prosperolight-linux-builder/1':
        raise ValueError('BUILDER_CONFIGURATION_INVALID')
    if not re.fullmatch(r'[A-Za-z0-9_.@-]{1,255}',value.get('ssh_host','')) or value['ssh_host'].startswith('-'):
        raise ValueError('BUILDER_HOST_INVALID')
    endpoint = urlsplit(value.get('repository_url',''))
    if endpoint.scheme != 'https' or not endpoint.hostname or endpoint.username or endpoint.password or endpoint.query or endpoint.fragment:
        raise ValueError('BUILDER_REPOSITORY_URL_INVALID')
    for key in ('repository','workspace_root','dependencies','runtime_bin'):
        path=value.get(key)
        if not isinstance(path,str) or not path.startswith('/') or not re.fullmatch(r'[/A-Za-z0-9_.-]+',path) or '..' in Path(path).parts:
            raise ValueError('BUILDER_PATH_INVALID:'+key)
    if type(value.get('jobs',2)) is not int or not 1<=value.get('jobs',2)<=4:
        raise ValueError('BUILDER_JOBS_INVALID')
    if type(value.get('ssh_port',22)) is not int or not 1<=value.get('ssh_port',22)<=65535:
        raise ValueError('BUILDER_PORT_INVALID')
    if value.get('identity_file') and not Path(value['identity_file']).expanduser().is_file():
        raise ValueError('BUILDER_IDENTITY_FILE_UNAVAILABLE')
    return value


def remote_build(config, source_sha, label):
    # This function is serialized into the configured Linux host, using stdlib only.
    import fcntl
    import hashlib
    import json
    import os
    from pathlib import Path
    import subprocess
    import uuid
    root=Path(config['workspace_root'])
    if root == Path('/') or any(path.is_symlink() for path in (root,*root.parents)):
        raise RuntimeError('BUILDER_WORKSPACE_UNSAFE')
    root.mkdir(parents=True,exist_ok=True)
    with (root/'.builder.lock').open('a') as lease:
        try: fcntl.flock(lease,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except BlockingIOError: raise RuntimeError('BUILDER_BUSY')
        repository=Path(config['repository']); dependencies=Path(config['dependencies'])
        runtime=Path(config['runtime_bin'])
        if not (repository/'.git').exists() or not dependencies.is_dir() or not runtime.is_dir():
            raise RuntimeError('BUILDER_TOOLCHAIN_OR_REPOSITORY_UNAVAILABLE')
        checkout=root/(source_sha+'-'+uuid.uuid4().hex[:8])
        log=root/(checkout.name+'.log')
        with log.open('wb') as output:
            def run(args,cwd=None,env=None):
                subprocess.run(args,cwd=cwd,env=env,stdout=output,stderr=subprocess.STDOUT,check=True,timeout=3600)
            run(['git','fetch',config['repository_url'],source_sha],repository)
            run(['git','worktree','add','--detach',str(checkout),source_sha],repository)
            run(['git','submodule','update','--init','--recursive','--jobs',str(config.get('jobs',2))],checkout)
            if subprocess.check_output(['git','status','--porcelain'],cwd=checkout).strip():
                raise RuntimeError('BUILDER_CHECKOUT_NOT_CLEAN')
            (checkout/'.deps').symlink_to(dependencies,target_is_directory=True)
            env=dict(os.environ,PATH=str(runtime)+os.pathsep+os.environ.get('PATH',''),
                     BUILD_JOBS=str(config.get('jobs',2)),CMAKE_BUILD_PARALLEL_LEVEL=str(config.get('jobs',2)))
            run(['make','app','TEST_LAB=1','PERFORMANCE_DETAIL=1','LAN_TELEMETRY=0','BUILD_LABEL='+label],checkout,env)
        app=checkout/'dist/PPSA99002';provenance=checkout/'build/build-provenance.json'
        paths=list(app.rglob('*'))
        files=[p for p in paths if p.is_file()]
        if any(p.is_symlink() for p in paths) or len(files)>512 or sum(p.stat().st_size for p in files)>512*1024*1024:
            raise RuntimeError('BUILDER_ARTIFACT_LIMIT')
        p=json.loads(provenance.read_bytes())
        if p['commit']!=source_sha or p['eboot_sha256']!=hashlib.sha256((app/'eboot.bin').read_bytes()).hexdigest():
            raise RuntimeError('BUILDER_PROVENANCE_MISMATCH')
        return {'directory':str(app),'provenance':str(provenance),'log':str(log),
                'checkout':str(checkout),'source_sha':source_sha,'build_label':label}


def build(config, source_sha, target, provenance):
    config=validate_config(config)
    if not re.fullmatch(r'[0-9a-f]{40}',source_sha): raise ValueError('BUILD_SOURCE_SHA_REQUIRED')
    target,provenance=Path(target),Path(provenance)
    if target.exists() or provenance.exists(): raise ValueError('BUILD_OUTPUT_ALREADY_EXISTS')
    label='test-lab-'+source_sha[:12]
    ssh=['ssh','-o','BatchMode=yes','-o','ConnectTimeout=10','-p',str(config.get('ssh_port',22))]
    scp=['scp','-q','-o','BatchMode=yes','-o','ConnectTimeout=10','-P',str(config.get('ssh_port',22))]
    if config.get('identity_file'):
        identity=str(Path(config['identity_file']).expanduser())
        ssh+=['-i',identity,'-o','IdentitiesOnly=yes'];scp+=['-i',identity,'-o','IdentitiesOnly=yes']
    remote={key:value for key,value in config.items() if key not in ('identity_file','ssh_host','ssh_port','schema')}
    script=inspect.getsource(remote_build)+'\nimport json\nprint(json.dumps(remote_build('+repr(remote)+','+repr(source_sha)+','+repr(label)+')))\n'
    result=subprocess.run(ssh+[config['ssh_host'],'python3 -'],input=script.encode(),capture_output=True,timeout=3900)
    if result.returncode: raise RuntimeError('REMOTE_BUILD_FAILED: inspect configured builder workspace logs')
    if len(result.stdout)>16384: raise ValueError('BUILDER_RESPONSE_LIMIT')
    value=json.loads(result.stdout)
    if value.get('source_sha') != source_sha or value.get('build_label') != label:
        raise ValueError('BUILDER_RESPONSE_IDENTITY_MISMATCH')
    root=Path(config['workspace_root'])
    for key in ('directory','provenance','log','checkout'):
        path=Path(value[key])
        if root not in path.parents or '..' in path.parts or not re.fullmatch(r'[/A-Za-z0-9_.-]+',str(path)):
            raise ValueError('BUILDER_RESPONSE_PATH_INVALID')
    target.mkdir(parents=True,exist_ok=False)
    subprocess.run(scp+['-r',config['ssh_host']+':'+value['directory']+'/.',str(target)],check=True,timeout=600)
    subprocess.run(scp+[config['ssh_host']+':'+value['provenance'],str(provenance)],check=True,timeout=60)
    return value
