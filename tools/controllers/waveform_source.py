#!/usr/bin/env python3
# ps5-native-app-boilerplate - Opt-in, bounded Windows actuator PCM source.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""Write only channels 3/4 of an explicitly selected four-channel DualSense endpoint.

No protocol injection: Windows audio -> Vibepollo's existing composite USB device
-> encrypted 0x5601 -> client. Listing is read-only; playback requires --play.
"""
import argparse
import ctypes as c
import json
import sys
import time
import uuid


def pcm(strong_long=False):
    # Explicit stronger preset: 1.5s pulses, 1s gaps, peak 6000; no full-scale output.
    segment = 72000 if strong_long else 7680
    gap = 48000 if strong_long else 0
    tail = 24000 if strong_long else 3840
    peak = 6000 if strong_long else 1000
    frames = 3 * segment + 2 * gap + tail
    out = bytearray(frames * 8)
    for part in range(3):
        for f in range(segment):
            phase = f % 400
            sample = (phase if phase < 200 else 400 - phase) * (peak // 100) - peak
            for channel in (2, 3):
                if part == 2 or channel == part + 2:
                    offset = ((part * (segment + gap)) + f) * 8 + channel * 2
                    out[offset:offset + 2] = sample.to_bytes(2, 'little', signed=True)
    return bytes(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device-id', type=int)
    parser.add_argument('--play', action='store_true')
    parser.add_argument('--strong-long', action='store_true',
                        help='explicit 1.5s pulses with 1s gaps, peak 6000/32768')
    args = parser.parse_args()
    if sys.platform != 'win32':
        parser.error('Windows only; importing pcm() is portable for host tests')

    class Caps(c.Structure):
        _fields_ = [('mid', c.c_uint16), ('pid', c.c_uint16), ('version', c.c_uint32),
                    ('name', c.c_wchar * 32), ('formats', c.c_uint32),
                    ('channels', c.c_uint16), ('reserved', c.c_uint16), ('support', c.c_uint32)]

    class Format(c.Structure):
        _pack_ = 2
        _fields_ = [('tag', c.c_uint16), ('channels', c.c_uint16), ('hz', c.c_uint32),
                    ('bps', c.c_uint32), ('align', c.c_uint16), ('bits', c.c_uint16), ('extra', c.c_uint16)]

    class Extensible(c.Structure):
        _pack_ = 2
        _fields_ = [('base', Format), ('valid_bits', c.c_uint16),
                    ('mask', c.c_uint32), ('subtype', c.c_ubyte * 16)]

    class Header(c.Structure):
        _fields_ = [('data', c.c_void_p), ('length', c.c_uint32), ('recorded', c.c_uint32),
                    ('user', c.c_size_t), ('flags', c.c_uint32), ('loops', c.c_uint32),
                    ('next', c.c_void_p), ('reserved', c.c_size_t)]

    api = c.WinDLL('winmm')
    api.waveOutGetNumDevs.restype = c.c_uint32
    api.waveOutGetDevCapsW.argtypes = [c.c_size_t, c.POINTER(Caps), c.c_uint32]
    api.waveOutOpen.argtypes = [c.POINTER(c.c_void_p), c.c_uint32, c.c_void_p,
                                c.c_size_t, c.c_size_t, c.c_uint32]
    for name in ('waveOutPrepareHeader', 'waveOutWrite', 'waveOutUnprepareHeader'):
        getattr(api, name).argtypes = [c.c_void_p, c.POINTER(Header), c.c_uint32]
    api.waveOutReset.argtypes = api.waveOutClose.argtypes = [c.c_void_p]
    # Multichannel PCM uses WAVEFORMATEXTENSIBLE. WinMM's legacy caps may report
    # two channels even for a multichannel endpoint; query the actual exact format.
    subtype = (c.c_ubyte * 16).from_buffer_copy(uuid.UUID('00000001-0000-0010-8000-00aa00389b71').bytes_le)
    fmt = Extensible(Format(0xfffe, 4, 48000, 48000 * 8, 8, 16, 22), 16, 0x33, subtype)
    devices = []
    for i in range(min(api.waveOutGetNumDevs(), 256)):
        info = Caps()
        if api.waveOutGetDevCapsW(i, c.byref(info), c.sizeof(info)) == 0:
            query = None
            if 'dualsense' in info.name.lower():
                unused = c.c_void_p()
                query = api.waveOutOpen(c.byref(unused), i, c.byref(fmt), 0, 0, 9)
            devices.append({'id': i, 'name': info.name, 'reported_channels': info.channels,
                            'pcm4_query_rc': query})
    if not args.play:
        print(json.dumps({'devices': devices, 'playback': False}))
        return 0
    device = next((d for d in devices if d['id'] == args.device_id), None)
    if device is None or device['pcm4_query_rc'] != 0 or 'dualsense' not in device['name'].lower():
        parser.error('--play requires an exact --device-id with four channels and DualSense identity; no mapper/fallback')
    handle = c.c_void_p()
    # WAVE_FORMAT_DIRECT: no ACM conversion or stereo substitution.
    rc = api.waveOutOpen(c.byref(handle), device['id'], c.byref(fmt), 0, 0, 8)
    if rc:
        print(json.dumps({'device': device, 'open_rc': rc, 'completed': False}))
        return 1
    data = c.create_string_buffer(pcm(args.strong_long))
    header = Header(c.cast(data, c.c_void_p), len(data) - 1, 0, 0, 0, 0, None, 0)
    prepared = False
    try:
        rc = api.waveOutPrepareHeader(handle, c.byref(header), c.sizeof(header))
        if rc:
            raise RuntimeError('waveOutPrepareHeader rc=' + str(rc))
        prepared = True
        rc = api.waveOutWrite(handle, c.byref(header), c.sizeof(header))
        if rc:
            raise RuntimeError('waveOutWrite rc=' + str(rc))
        deadline = time.monotonic() + (len(data) - 1) / (48000 * 8) + 5
        while not header.flags & 1 and time.monotonic() < deadline:
            time.sleep(.005)
        if not header.flags & 1:
            raise TimeoutError('Waveform completion was not acknowledged; no automatic retry')
        print(json.dumps({'device': device, 'completed': True, 'frames': (len(data) - 1) // 8,
                          'peak_limit': 6000 if args.strong_long else 1000, 'physical_actuator_verified': False}))
        return 0
    finally:
        # Keep header and samples alive until Windows returns ownership.
        if prepared:
            api.waveOutReset(handle)
            rc = api.waveOutUnprepareHeader(handle, c.byref(header), c.sizeof(header))
            if rc:
                # An abnormal driver still owns this memory: retain references through process exit.
                globals()['_retained_audio_buffer'] = (handle, data, header)
                raise RuntimeError('waveOutUnprepareHeader rc=' + str(rc))
        api.waveOutClose(handle)


if __name__ == '__main__':
    raise SystemExit(main())
