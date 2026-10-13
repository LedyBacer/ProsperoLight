# ps5-native-app-boilerplate - read-only active display HDR evidence.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import ctypes as c

U32=c.c_uint32

class Luid(c.Structure):
    _fields_=[('low',U32),('high',c.c_int32)]
class Source(c.Structure):
    _fields_=[('adapter',Luid),('id',U32),('mode',U32),('flags',U32)]
class Rational(c.Structure):
    _fields_=[('numerator',U32),('denominator',U32)]
class Target(c.Structure):
    _fields_=[('adapter',Luid),('id',U32),('mode',U32),('technology',U32),('rotation',U32),
              ('scaling',U32),('refresh',Rational),('scanline',U32),('available',c.c_int32),('flags',U32)]
class DisplayPath(c.Structure):
    _fields_=[('source',Source),('target',Target),('flags',U32)]
class ModeUnion(c.Union):
    _fields_=[('opaque',c.c_ubyte*48),('alignment',c.c_uint64)]
class ModeInfo(c.Structure):
    _fields_=[('type',U32),('id',U32),('adapter',Luid),('mode',ModeUnion)]
class Header(c.Structure):
    _fields_=[('type',U32),('size',U32),('adapter',Luid),('id',U32)]
class SourceName(c.Structure):
    _fields_=[('header',Header),('name',c.c_uint16*32)]
class ColorInfo(c.Structure):
    _fields_=[('header',Header),('value',U32),('encoding',U32),('bits',U32),('active_mode',U32)]


def decode(info):
    return {'hdr':bool(info.value & (1<<4)), 'hdr_active':info.active_mode==2,
            'hdr_user_enabled':bool(info.value & (1<<5)), 'color_bits':info.bits,
            'active_color_mode':info.active_mode, 'hdr_verified':True,
            'hdr_source':'DisplayConfigGetDeviceInfo GET_ADVANCED_COLOR_INFO_2', 'hdr_reason':None}


def inventory(user):
    expected=((Luid,8),(Source,20),(Target,48),(DisplayPath,72),(ModeInfo,64),
              (Header,20),(SourceName,84),(ColorInfo,36))
    if any(c.sizeof(kind)!=size for kind,size in expected):raise RuntimeError('DISPLAYCONFIG_ABI_MISMATCH')
    user.GetDisplayConfigBufferSizes.argtypes=[U32,c.POINTER(U32),c.POINTER(U32)]
    user.QueryDisplayConfig.argtypes=[U32,c.POINTER(U32),c.POINTER(DisplayPath),c.POINTER(U32),c.POINTER(ModeInfo),c.c_void_p]
    user.DisplayConfigGetDeviceInfo.argtypes=[c.c_void_p]
    for _ in range(3):
        count,modes=U32(),U32()
        if user.GetDisplayConfigBufferSizes(2,c.byref(count),c.byref(modes)):raise RuntimeError('DISPLAYCONFIG_SIZE_UNAVAILABLE')
        if not 0<count.value<=128 or not 0<modes.value<=4096:raise RuntimeError('DISPLAYCONFIG_SIZE_LIMIT')
        paths=(DisplayPath*count.value)();info=(ModeInfo*modes.value)()
        result=user.QueryDisplayConfig(2,c.byref(count),paths,c.byref(modes),info,None)
        if result==122:continue
        if result:raise RuntimeError('DISPLAYCONFIG_QUERY_UNAVAILABLE')
        values={}
        for path in paths[:count.value]:
            if not path.flags & 1 or not path.target.available:continue
            name=SourceName();name.header=Header(1,c.sizeof(SourceName),path.source.adapter,path.source.id)
            if user.DisplayConfigGetDeviceInfo(c.byref(name)):continue
            gdi=bytes(name.name).decode('utf-16-le').split('\0',1)[0]
            color=ColorInfo();color.header=Header(15,c.sizeof(ColorInfo),path.target.adapter,path.target.id)
            rc=user.DisplayConfigGetDeviceInfo(c.byref(color))
            value=decode(color) if rc==0 else {'hdr':None,'hdr_active':None,'hdr_verified':False,
                'hdr_source':'DisplayConfigGetDeviceInfo GET_ADVANCED_COLOR_INFO_2',
                'hdr_reason':'HDR-specific query unavailable; older advanced-color flags cannot distinguish HDR from WCG'}
            value['display_adapter_luid']='%08x%08x'%(path.source.adapter.high & 0xffffffff,path.source.adapter.low)
            if gdi in values:value={'hdr':None,'hdr_active':None,'hdr_verified':False,'hdr_reason':'AMBIGUOUS_CLONED_DISPLAY_PATHS'}
            values[gdi]=value
        return values
    raise RuntimeError('DISPLAYCONFIG_TOPOLOGY_CHANGED')
