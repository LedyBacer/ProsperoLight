#!/usr/bin/env python3
# ps5-native-app-boilerplate / ProsperoLight - Firmware startup experiment.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare a stock SDK ELF object's code with the exact plaintext release ELF.
This is relocation-aware code matching, NOT symbolization with a replacement ELF.
Use the repository's native tool to extract the public homebrew SELF first.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct

def sections(data):
    off=struct.unpack_from('<Q',data,40)[0];size,count,names_index=struct.unpack_from('<HHH',data,58)
    headers=[struct.unpack_from('<IIQQQQIIQQ',data,off+i*size) for i in range(count)]
    at,length=headers[names_index][4:6];names=data[at:at+length]
    return {names[h[0]:].split(b'\0')[0].decode():h for h in headers}

def text(data):
    at=struct.unpack_from('<Q',data,32)[0];size,count=struct.unpack_from('<HH',data,54)
    for i in range(count):
        kind,flags,offset,va,_,length,_,_=struct.unpack_from('<IIQQQQQQ',data,at+i*size)
        if kind==1 and flags&1:return va,data[offset:offset+length]
    raise ValueError('No executable segment')

def code_mask(data, section):
    ss=sections(data);h=ss[section];code=data[h[4]:h[4]+h[5]];mask=bytearray(b'\1'*len(code))
    if '.rela'+section in ss:
        r=ss['.rela'+section]
        for pos in range(r[4],r[4]+r[5],24):
            offset,info,_=struct.unpack_from('<QQq',data,pos)
            kind=info&0xffffffff
            if kind not in (1,2,4,9,10,11,41,42):raise ValueError(f'Unhandled x86 relocation: {kind}')
            width=8 if kind==1 else 4
            # Linker GOT relaxation can change the three bytes preceding a displacement.
            start=max(0,offset-3);end=offset+width
            mask[start:end]=b'\0'*(end-start)
    return code,mask

def compare(obj, release, rip, offset, section=".text.ps5_agc_gate2_run"):
    code,mask=code_mask(obj,section)
    va,image=text(release);start=rip-0x400000-offset-va
    candidate=image[start:start+len(code)]
    if len(candidate)!=len(code) or any(a!=b for a,b,m in zip(code,candidate,mask) if m):
        raise ValueError('Release code does not match this SDK object')
    return dict(method='full function code match excluding relocation/relaxation bytes',
        symbol=section.removeprefix('.text.'),symbol_absolute=hex(rip-offset),offset=hex(offset),rip=hex(rip),
        function_bytes=len(code),compared_bytes=sum(mask),mismatches=0,
        instruction_at_fault=candidate[offset:offset+3].hex(),
        sdk_object_sha256=hashlib.sha256(obj).hexdigest(),extracted_release_elf_sha256=hashlib.sha256(release).hexdigest())
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('sdk_object',type=Path)
    p.add_argument('extracted_release_elf',type=Path);p.add_argument('--rip',type=lambda x:int(x,0),required=True)
    p.add_argument('--function-offset',type=lambda x:int(x,0),default=0x19a6)
    p.add_argument('--section',default='.text.ps5_agc_gate2_run')
    a=p.parse_args();print(json.dumps(compare(a.sdk_object.read_bytes(),a.extracted_release_elf.read_bytes(),a.rip,a.function_offset,a.section),indent=2))
