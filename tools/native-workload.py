#!/usr/bin/env python3
# ps5-native-app-boilerplate / ProsperoLight - Small synthetic Annex-B controls; no desktop/game capture.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate two characterized inputs, not a PS5 benchmark or replay protocol."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def nals(data):
    starts = list(re.finditer(b'\x00\x00(?:\x00)?\x01', data))
    if not starts or starts[0].start() != 0:
        raise ValueError('Annex-B start code required')
    return [(s.start(), s.end(), starts[i + 1].start() if i + 1 < len(starts) else len(data))
            for i, s in enumerate(starts)]


def access_units(data, codec):
    """AUD-delimited generated streams only; never guess AU boundaries."""
    units = []
    for start, header, end in nals(data):
        if header >= end:
            raise ValueError('empty NAL')
        kind = data[header] & 31 if codec == 'h264' else (data[header] >> 1) & 63
        if kind == (9 if codec == 'h264' else 35):
            if units:
                units[-1]['bytes'] = start - units[-1]['offset']
            units.append(dict(offset=start, bytes=0, slices=0, idr=False))
        if not units:
            raise ValueError('explicit AUD must precede the first AU')
        if kind in (1, 5) if codec == 'h264' else kind <= 31:
            units[-1]['slices'] += 1
            units[-1]['idr'] |= kind == 5 if codec == 'h264' else kind in (19, 20)
    if not units:
        raise ValueError('no AUs')
    units[-1]['bytes'] = len(data) - units[-1]['offset']
    if any(not u['slices'] for u in units) or not units[0]['idr']:
        raise ValueError('non-picture AU or missing initial IDR')
    return units


def inspect(path, codec, fps, frames):
    data = path.read_bytes()
    units = access_units(data, codec)
    if len(units) != frames:
        raise ValueError('unexpected AU count')
    output = subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'info', '-i', str(path),
                             '-map', '0:v:0', '-c:v', 'copy', '-bsf:v', 'trace_headers',
                             '-f', 'null', '-'], capture_output=True, text=True, check=True)
    fields = ('profile_idc', 'level_idc', 'max_num_ref_frames', 'max_dec_frame_buffering',
              'num_reorder_frames', 'entropy_coding_mode_flag', 'pic_order_cnt_type',
              'general_profile_idc', 'general_level_idc', 'sps_max_dec_pic_buffering_minus1',
              'sps_max_num_reorder_pics', 'tiles_enabled_flag', 'entropy_coding_sync_enabled_flag')
    facts = {}
    for key in fields:
        values = sorted({int(v) for v in re.findall(
            r'\b' + key + r'(?:\[\d+\])?\s+[01]+\s*=\s*(-?\d+)', output.stderr)})
        facts[key] = values if values else None
    probe = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'v:0',
                            '-show_entries', 'stream=codec_name,profile,level,width,height,pix_fmt:'
                            'frame=pict_type', '-show_frames', '-of', 'json', str(path)],
                           capture_output=True, text=True, check=True)
    decoded = json.loads(probe.stdout)
    pictures = decoded.get('frames', [])
    if len(pictures) != frames or any(p.get('pict_type') not in ('I', 'P') for p in pictures):
        raise ValueError('workload must decode fully without B-frame reordering')
    for i, unit in enumerate(units):
        unit.update(pts_us=i * 1000000 // fps, picture_type=pictures[i]['pict_type'])
    return dict(file=path.name, sha256=hashlib.sha256(data).hexdigest(), bytes=len(data),
                fps=fps, duration_seconds=frames / fps,
                actual_mbps=len(data) * 8 * fps / frames / 1e6,
                stream=decoded['streams'][0], header_fields=facts, access_units=units,
                provenance='generated testsrc2; parsed Annex-B AUD/VCL plus FFmpeg trace_headers '
                           'and full software decode; no native performance evidence')


def generate(output, codec, size, frames, fps, qp):
    output.mkdir(parents=True, exist_ok=False)
    version = subprocess.check_output(['ffmpeg', '-version'], text=True).splitlines()[0]
    variants = []
    for value in ((1, 4) if codec == 'h264' else (0, 1)):
        name = f'h264-slices-{value}' if codec == 'h264' else f'hevc-wpp-{value}'
        path = output / (name + ('.h264' if codec == 'h264' else '.hevc'))
        common = f'bframes=0:ref=4:keyint={frames}:min-keyint={frames}:scenecut=0:repeat-headers=1:aud=1'
        params = (f'{common}:slices={value}:sliced-threads=0:threads=2' if codec == 'h264'
                  else f'{common}:wpp={value}:pools=2:frame-threads=1:log-level=error')
        command = ['ffmpeg', '-nostdin', '-hide_banner', '-loglevel', 'error', '-f', 'lavfi',
                   '-i', f'testsrc2=size={size}:rate={fps}', '-frames:v', str(frames),
                   '-an', '-pix_fmt', 'yuv420p', '-c:v', 'libx264' if codec == 'h264' else 'libx265',
                   '-preset', 'veryfast', '-qp', str(qp),
                   '-x264-params' if codec == 'h264' else '-x265-params', params,
                   '-f', codec, str(path)]
        subprocess.run(command, check=True)
        record = inspect(path, codec, fps, frames)
        if codec == 'h264' and any(u['slices'] != value for u in record['access_units']):
            raise ValueError('encoder did not honor the slice count')
        if codec == 'hevc' and record['header_fields']['entropy_coding_sync_enabled_flag'] != [value]:
            raise ValueError('encoder did not honor WPP')
        record['command'] = command
        variants.append(record)
    manifest = dict(schema='prosperolight-native-workload/1', ffmpeg=version, variants=variants,
                    comparison='Matched synthetic source, QP and reference request; encoded bytes '
                               'differ across partition variants. Reuse each exact SHA for depth comparisons.',
                    native_replay_available=False)
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--codec', choices=('h264', 'hevc'), default='h264')
    parser.add_argument('--size', choices=('1920x1080', '2560x1440', '3840x2160'), default='2560x1440')
    parser.add_argument('--frames', type=int, choices=range(30, 241), default=120)
    parser.add_argument('--fps', type=int, choices=(60, 120), default=60)
    parser.add_argument('--qp', type=int, choices=range(16, 33), default=22)
    args = parser.parse_args()
    result = generate(args.output, args.codec, args.size, args.frames, args.fps, args.qp)
    print(json.dumps({v['file']: dict(sha256=v['sha256'], actual_mbps=v['actual_mbps'],
                                    frames=len(v['access_units'])) for v in result['variants']}, indent=2))
