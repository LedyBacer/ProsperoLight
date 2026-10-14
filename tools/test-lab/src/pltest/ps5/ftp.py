# ps5-native-app-boilerplate - bounded FTP diagnostics and atomic uploads.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import io
import re
from ftplib import error_perm
from pathlib import PurePosixPath

LOG_ROOT = '/data/prosperolight/logs'
MAX_FILE = 64 * 1024 * 1024
MAX_ARCHIVE = 256 * 1024 * 1024


def diagnostic_name(name):
    return name in ('performance-last.json', 'debug-trace.txt', 'crash-last.txt',
                    'videoout-policy.log') or bool(re.fullmatch(
                        r'(performance-frames|pyrowave-output|(?:native|pyrowave)-(?:scanout|decisions))-mode[012]\.csv', name))


def raw_transfer(ftp):
    # drakmor toggles SELF extraction per connection; hashes must cover stored bytes.
    if not getattr(ftp, '_pltest_raw_transfer', False) and 'Welcome to ftpsrv.elf' in getattr(ftp, 'getwelcome', lambda:'')():
        response=ftp.sendcmd('SELF')
        if response in ('200 SELF transfer mode enabled','226 SELF transfer mode enabled'):
            response=ftp.sendcmd('SELF')
        if response not in ('200 SELF transfer mode disabled','226 SELF transfer mode disabled'):
            raise RuntimeError('FTP_RAW_TRANSFER_UNCONFIRMED')
        ftp._pltest_raw_transfer=True
        ftp._pltest_listing_backend='LIST'


def retrieve(ftp, path, limit=MAX_FILE):
    raw_transfer(ftp)
    result = bytearray()
    def append(data):
        if len(result) + len(data) > limit:
            raise ValueError('remote file exceeds byte limit')
        result.extend(data)
    ftp.retrbinary('RETR ' + path, append)
    return bytes(result)


def upload(ftp, path, data):
    if len(data) > MAX_FILE:
        raise ValueError('upload exceeds byte limit')
    ftp.storbinary('STOR ' + path + '.tmp', io.BytesIO(data))
    if retrieve(ftp, path + '.tmp', len(data)) != data:
        raise RuntimeError('upload readback mismatch')
    ftp.rename(path + '.tmp', path)


def archive_logs(ftp, destination):
    destination.mkdir(parents=True, exist_ok=False)
    index, total = {}, 0
    try:
        entries = directory_entries(ftp,LOG_ROOT)
    except error_perm as exc:
        if str(exc).startswith('550') and not installation_exists(ftp,LOG_ROOT):
            return index
        raise
    for name,facts in sorted(entries):
        if facts['type']!='file' or not diagnostic_name(name):
            continue
        data = retrieve(ftp, LOG_ROOT + '/' + name)
        total += len(data)
        if total > MAX_ARCHIVE:
            raise ValueError('diagnostic archive exceeds total byte limit')
        (destination / name).write_bytes(data)
        index[name] = {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
    return index


def directory_entries(ftp, path, evidence=None):
    """Bounded typed MLSD or Unix LIST; LIST is visible-entry evidence only."""
    parts=PurePosixPath(path).parts
    if (not path.startswith('/') or str(PurePosixPath(path))!=path or len(parts)>17 or
            len(path.encode())>1024 or any(p in ('.','..') for p in parts) or
            any(ord(c)<32 or ord(c)==127 for c in path)):
        raise ValueError('FTP_UNSAFE_LISTING_PATH')
    raw_transfer(ftp)
    backend=getattr(ftp,'_pltest_listing_backend','MLSD')
    entries=[];seen=set();total=0;lines=0
    def accept(line):
        nonlocal total,lines
        total+=len(line.encode());lines+=1
        if len(line.encode())>4096 or total>1024*1024 or lines>1026:
            raise RuntimeError('INSTALL_INVENTORY_LIMIT')
        if backend=='MLSD':
            fields,sep,name=line.partition(' ')
            if not sep or not fields.endswith(';'):raise RuntimeError('FTP_MALFORMED_MLSD')
            facts={}
            for field in fields[:-1].split(';'):
                key,eq,value=field.partition('=');key=key.lower()
                if not eq or key in facts:raise RuntimeError('FTP_MALFORMED_MLSD')
                facts[key]=value
        else:
            match=re.fullmatch(r'([bcdlps-][r-][w-][xSs-][r-][w-][xSs-][r-][w-][xTt-]) ([1-9][0-9]*) ([0-9]+) ([0-9]+) ([0-9]+) '
                r'(Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) ([ 0][1-9]|[12][0-9]|3[01]) '
                r'([01][0-9]|2[0-3]):([0-5][0-9]) (.+)',line)
            if not match:raise RuntimeError('FTP_MALFORMED_LIST')
            mode=match[1][0];name=match[10]
            if mode not in ('d','-'):raise RuntimeError('INSTALL_UNSUPPORTED_ENTRY')
            facts={'type':'dir' if mode=='d' else 'file','size':match[5]}
        kind=facts.get('type')
        if name in ('.','..') and kind in ('dir','cdir','pdir'):
            return
        if (not name or name in ('.','..') or name!=name.strip() or '/' in name or '\\' in name or
                ' -> ' in name or len(name.encode())>255 or
                any(ord(c)<32 or 127<=ord(c)<160 for c in name) or name in seen):
            raise RuntimeError('INSTALL_UNSAFE_ENTRY')
        if kind not in ('dir','file'):raise RuntimeError('INSTALL_UNSUPPORTED_ENTRY')
        seen.add(name);entries.append((name,facts))
        if len(entries)>1024:raise RuntimeError('INSTALL_INVENTORY_LIMIT')
    try:
        ftp.retrlines(backend+' '+path,accept)
    except error_perm as exc:
        if backend!='MLSD' or entries or lines or str(exc)[:3] not in ('500','501','502','504'):
            raise
        backend='LIST'
        try:
            ftp.retrlines('LIST '+path,accept)
        except error_perm as fallback:
            if str(fallback)[:3] in ('500','501','502','504'):
                raise RuntimeError('FTP_LISTING_CAPABILITY_REQUIRED: need MLSD or Unix LIST') from fallback
            raise
    ftp._pltest_listing_backend=backend
    if evidence is not None:
        evidence.update(source=backend+'_READBACK',enumeration_backend=backend,
            enumeration_limitations=['LIST follows stat(): symlinks may appear as their targets and dangling/unstatable entries may be omitted; trusted managed installation required'] if backend=='LIST' else [],
            filesystem_attestation=False)
    return entries


def installation_exists(ftp, root, evidence=None):
    """Prove absence from a readable parent, never infer it from a 550 alone."""
    parent=str(PurePosixPath(root).parent)
    try:
        entries=directory_entries(ftp,parent,evidence)
    except error_perm as exc:
        if str(exc).startswith('550') and parent!='/' and not installation_exists(ftp,parent,evidence):
            return False
        raise
    for name,facts in entries:
        if name==PurePosixPath(root).name:
            if facts.get('type')!='dir':raise RuntimeError('INSTALL_UNSAFE_ROOT')
            ftp.cwd(root)
            return True
    try:
        ftp.cwd(root)
    except error_perm as exc:
        if str(exc).startswith('550'):return False
        raise
    raise RuntimeError('FTP_LISTING_CWD_DISAGREEMENT')


def installation_inventory(ftp, root, expected, readback=True, evidence=None):
    """Read typed inventory; callers preflight capabilities before mutation."""
    if not installation_exists(ftp,root,evidence):
        if readback:raise RuntimeError('INSTALL_DIRECTORY_MISSING')
        return {}
    actual, directories = {}, set()
    allowed_dirs = {str(parent) for name in expected for parent in PurePosixPath(name).parents if str(parent) != '.'}
    pending = [('', root)]
    total = 0
    while pending:
        prefix, path = pending.pop()
        entries = directory_entries(ftp,path,evidence)
        for name, facts in entries:
            kind = facts.get('type')
            if kind in ('cdir', 'pdir') and name in ('.', '..'):
                continue
            if not name or name in ('.', '..') or '/' in name or '\\' in name or any(ord(c)<32 for c in name):
                raise RuntimeError('INSTALL_UNSAFE_ENTRY')
            relative = prefix + name
            if kind == 'dir':
                if relative not in allowed_dirs or relative in directories:
                    raise RuntimeError('INSTALL_UNEXPECTED_DIRECTORY: inspect manually: ' + relative)
                directories.add(relative)
                pending.append((relative+'/', root+'/'+relative))
            elif kind == 'file':
                if relative not in expected or relative in actual:
                    raise RuntimeError('INSTALL_UNEXPECTED_FILE: inspect manually; no deletion attempted: ' + relative)
                actual[relative] = None
            else:
                raise RuntimeError('INSTALL_UNSUPPORTED_ENTRY: inspect manually: ' + relative)
            if len(actual)+len(directories)>1024:
                raise RuntimeError('INSTALL_INVENTORY_LIMIT')
    if readback:
        if set(actual) != set(expected):
            raise RuntimeError('INSTALL_FILE_SET_MISMATCH')
        for name in sorted(actual):
            raw_transfer(ftp)
            try:
                ftp.voidcmd('TYPE I')
                size=ftp.size(root+'/'+name)
            except error_perm as exc:
                if str(exc)[:3] not in ('500','501','502','504'):raise
                size=None
            if size is not None and size!=expected[name]['bytes']:
                raise RuntimeError('INSTALL_FINAL_SIZE_MISMATCH: '+name)
            data = retrieve(ftp, root+'/'+name, expected[name]['bytes'])
            total += len(data)
            if total > MAX_ARCHIVE:
                raise RuntimeError('INSTALL_INVENTORY_BYTE_LIMIT')
            item = {'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
            if item != expected[name]:
                raise RuntimeError('INSTALL_FINAL_MANIFEST_MISMATCH: '+name)
            actual[name] = item
    return actual
