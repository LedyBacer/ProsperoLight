# ps5-native-app-boilerplate - bounded local report bundle with optional redaction.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import ipaddress
import json
import re
import tempfile
import zipfile
from pathlib import Path

MAX_BUNDLE = 256 * 1024 * 1024
SECRET = re.compile(r'password|passwd|token|secret|authorization|private_key|rikey|certificate', re.I)
IPV4 = re.compile(r'(?<![\w.])(?:\d{1,3}\.){3}\d{1,3}(?![\w.])')


def scrub_text(value):
    def address(match):
        try:
            return '[private-ip]' if ipaddress.ip_address(match[0]).is_private else match[0]
        except ValueError:
            return match[0]
    value = IPV4.sub(address, value)
    value = re.sub(r'(?<![\w:])(?:[0-9a-fA-F]{0,4}:){2,7}[0-9a-fA-F]{0,4}(?![\w:])', address, value)
    value = re.sub(r'(?i)(authorization\s*[:=]\s*)(?:Bearer\s+)?[^\s,;"<]+', r'\1[redacted]', value)
    return re.sub(r'(?i)((?:password|passwd|token|secret|rikey)\s*[:=]\s*)[^\s,;"<]+',r'\1[redacted]',value)


def scrub(value):
    if isinstance(value,dict): return {k:'[redacted]' if SECRET.search(k) else scrub(v) for k,v in value.items()}
    if isinstance(value,list): return [scrub(v) for v in value]
    if isinstance(value,str): return scrub_text(value)
    return value


def bundle(store, run_id, redact=True):
    """Only validated artifact paths; export manifest hashes the transformed bytes."""
    temporary = tempfile.TemporaryDirectory(prefix='pltest-export-')
    path = Path(temporary.name) / (run_id+'.zip')
    total=0; manifest={}
    try:
        index_path=store.artifact_path(run_id,'manifest.json')
        if index_path is None or index_path.stat().st_size>1024*1024:
            raise ValueError('ARTIFACT_MANIFEST_UNAVAILABLE')
        index_bytes=index_path.read_bytes()
        index=json.loads(index_bytes)
        if index.get('schema_version')!='prosperolight-test-manifest/1' or index.get('run_id')!=run_id or not isinstance(index.get('files'),dict):
            raise ValueError('ARTIFACT_MANIFEST_INVALID')
        items=store.files(run_id) or []
        if {item['name'] for item in items if item['name']!='manifest.json'} != set(index['files']):
            raise ValueError('ARTIFACT_MANIFEST_FILE_SET_MISMATCH')
        with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED) as archive:
            for item in items:
                name=item['name']; source=store.artifact_path(run_id,name)
                if source is None: raise ValueError('unsafe export path')
                total += source.stat().st_size
                if total > MAX_BUNDLE: raise ValueError('bundle exceeds total byte limit')
                expected=index['files'].get(name) if name!='manifest.json' else None
                if name!='manifest.json' and (not isinstance(expected,dict) or type(expected.get('bytes')) is not int or not 0<=expected['bytes']<=MAX_BUNDLE):
                    raise ValueError('ARTIFACT_MANIFEST_ENTRY_MISSING')
                with source.open('rb') as stream:data=stream.read((expected['bytes'] if expected else 1024*1024)+1)
                if expected and (len(data)!=expected['bytes'] or hashlib.sha256(data).hexdigest()!=expected.get('sha256')):
                    raise ValueError('ARTIFACT_MANIFEST_HASH_MISMATCH')
                if name=='manifest.json' and data!=index_bytes:
                    raise ValueError('ARTIFACT_MANIFEST_CHANGED')
                if redact:
                    if source.suffix=='.json':
                        data=(json.dumps(scrub(json.loads(data)),indent=2)+'\n').encode()
                    elif source.suffix=='.jsonl':
                        data=(''.join(json.dumps(scrub(json.loads(line)))+'\n' for line in data.decode().splitlines())).encode()
                    elif source.suffix in ('.txt','.log','.csv','.html'):
                        data=scrub_text(data.decode('utf-8',errors='replace')).encode()
                archive.writestr(name,data)
                manifest[name]={'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
            archive.writestr('export-manifest.json',json.dumps({'schema_version':'prosperolight-test-export/1',
                'redacted':redact,'run_id':run_id,'files':manifest},indent=2))
        if path.stat().st_size > MAX_BUNDLE: raise ValueError('compressed bundle exceeds byte limit')
        return temporary,path
    except BaseException:
        temporary.cleanup();raise
