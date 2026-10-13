# ps5-native-app-boilerplate - read-only Windows interactive display inventory.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import ctypes
import os
import platform
from ctypes import wintypes


def probe():
    if platform.system() != 'Windows':
        return {'supported': False, 'interactive': False, 'reason': 'WINDOWS_REQUIRED', 'displays': []}
    user = ctypes.WinDLL('user32', use_last_error=True)
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    session = wintypes.DWORD()
    kernel.ProcessIdToSessionId(os.getpid(), ctypes.byref(session))
    user.OpenInputDesktop.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    user.OpenInputDesktop.restype = wintypes.HANDLE
    user.GetUserObjectInformationW.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p,
                                              wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
    user.CloseDesktop.argtypes = [wintypes.HANDLE]
    desktop = user.OpenInputDesktop(0, False, 1)
    name, needed = ctypes.create_unicode_buffer(256), wintypes.DWORD()
    desktop_name = None
    if desktop:
        if user.GetUserObjectInformationW(desktop, 2, name, ctypes.sizeof(name), ctypes.byref(needed)):
            desktop_name = name.value
        user.CloseDesktop(desktop)
    interactive = session.value != 0 and desktop_name == 'Default'

    class Device(ctypes.Structure):
        _fields_ = [('cb', wintypes.DWORD), ('name', wintypes.WCHAR * 32),
                    ('description', wintypes.WCHAR * 128), ('flags', wintypes.DWORD),
                    ('device_id', wintypes.WCHAR * 128), ('key', wintypes.WCHAR * 128)]
    class Position(ctypes.Structure):
        _fields_ = [('x', wintypes.LONG), ('y', wintypes.LONG),
                    ('orientation', wintypes.DWORD), ('fixed_output', wintypes.DWORD)]
    class Mode(ctypes.Structure):
        _fields_ = [('name', wintypes.WCHAR * 32)] + [(key, wintypes.WORD) for key in
                    ('spec', 'driver', 'size', 'extra')] + [('fields', wintypes.DWORD),
                    ('position', Position)] + [(key, ctypes.c_short) for key in
                    ('color', 'duplex', 'y_resolution', 'tt_option', 'collate')] + [
                    ('form', wintypes.WCHAR * 32), ('log_pixels', wintypes.WORD)] + [
                    (key, wintypes.DWORD) for key in ('bits', 'width', 'height', 'flags', 'frequency',
                    'icm_method', 'icm_intent', 'media', 'dither', 'reserved1', 'reserved2',
                    'panning_width', 'panning_height')]
    if ctypes.sizeof(Mode) != 220:
        raise RuntimeError('unexpected Windows DEVMODEW ABI')
    user.EnumDisplayDevicesW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, ctypes.POINTER(Device), wintypes.DWORD]
    user.EnumDisplaySettingsW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, ctypes.POINTER(Mode)]
    from .advanced_color import inventory
    try: advanced = inventory(user)
    except (OSError,AttributeError,RuntimeError) as exc:
        advanced = {}; advanced_reason = str(exc)[:128]
    else: advanced_reason = 'active display path not independently matched'
    displays = []
    for index in range(64):
        adapter = Device(); adapter.cb = ctypes.sizeof(adapter)
        if not user.EnumDisplayDevicesW(None, index, ctypes.byref(adapter), 0):
            break
        if not adapter.flags & 1:
            continue
        mode = Mode(); mode.size = ctypes.sizeof(mode)
        if not user.EnumDisplaySettingsW(adapter.name, 0xffffffff, ctypes.byref(mode)):
            continue
        monitor = Device(); monitor.cb = ctypes.sizeof(monitor)
        user.EnumDisplayDevicesW(adapter.name, 0, ctypes.byref(monitor), 1)
        description = adapter.description + ' / ' + monitor.description
        identified_virtual = any(word in description.lower() for word in ('virtual', 'vibepollo', 'apollo', 'iddsample'))
        displays.append({'id': adapter.name, 'description': description,
                         'monitor_device_id': monitor.device_id, 'primary': bool(adapter.flags & 4),
                         'x': mode.position.x, 'y': mode.position.y, 'width': mode.width,
                         'height': mode.height, 'refresh_hz': mode.frequency if mode.frequency > 1 else None,
                         'refresh_source': 'EnumDisplaySettings current integer mode',
                         'virtual': True if identified_virtual else None,
                         'virtual_reason': None if identified_virtual else 'driver identity does not establish virtual/physical',
                         **advanced.get(adapter.name, {'hdr':None,'hdr_active':None,'hdr_verified':False,'hdr_reason':advanced_reason})})
    return {'supported': True, 'interactive': interactive, 'session_id': session.value,
            'input_desktop': desktop_name, 'reason': None if interactive else 'NONINTERACTIVE_OR_LOCKED',
            'displays': displays}


def select(inventory, target):
    if not inventory.get('interactive'):
        raise RuntimeError(inventory.get('reason') or 'NONINTERACTIVE_DESKTOP')
    choices = inventory['displays']
    matches = [d for d in choices if d['id'] == target] if target != 'virtual' else [d for d in choices if d['virtual'] is True]
    if len(matches) != 1:
        raise RuntimeError('DISPLAY_AMBIGUOUS_OR_ABSENT')
    return matches[0]
