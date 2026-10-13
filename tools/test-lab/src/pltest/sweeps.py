# ps5-native-app-boilerplate - deterministic benchmark plans and sweep screening.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import copy
import math


def bitrate_profiles(profile, bitrates):
    if (not isinstance(bitrates, list) or not 2 <= len(bitrates) <= 20 or
            any(type(rate) is not int or not 1 <= rate <= 1000 for rate in bitrates) or
            any(a >= b for a,b in zip(bitrates,bitrates[1:]))):
        raise ValueError('BITRATES_REQUIRE_2_TO_20_INCREASING_INTEGER_MBPS')
    result=[]
    for rate in bitrates:
        value=copy.deepcopy(profile);value.pop('run_id',None)
        value['client']['bitrate_mbps']=rate
        value.setdefault('host_stimulus',{}).update(scene='gpu-entropy',entropy=1,movement=1)
        result.append(value)
    return result


def saturation(previous, current):
    """A throughput plateau plus FPS drop is a screening stop, not a cause."""
    def metrics(report):
        values=(report.get('analysis') or {}).get('metrics') or {}
        return values.get('actual_mbps'),values.get('unique_presented_fps')
    a,af=metrics(previous);b,bf=metrics(current)
    if any(type(v) not in (int,float) or not math.isfinite(v) or v<=0 for v in (a,af,b,bf)):
        return None
    return b <= a*1.03 and bf < af*.95


def benchmark_profiles(profile, name):
    """Plans request modes; each real run independently checks support."""
    names={'native-4k120','pyrowave-4k120','hevc-hdr10','pyrowave-chroma-hdr','pacing','soak','decoder-regression'}
    if name not in names:raise ValueError('UNKNOWN_BENCHMARK_PLAN')
    base=copy.deepcopy(profile);base.pop('run_id',None)
    client=base['client'];timing=base.setdefault('timing',{})
    timing.update(warmup_seconds=10,measure_seconds=30)
    timing['hard_seconds']=max(timing.get('hard_seconds',0),600)
    base.setdefault('host_stimulus',{}).update(scene='gpu-entropy',entropy=1,movement=1)
    if name.startswith('native') or name=='hevc-hdr10':
        client.update(codec='hevc',chroma='420',hdr=name=='hevc-hdr10',decoder_pipeline='classic',decoder_cores=3)
    if name.startswith('pyrowave'):
        client.update(codec='pyrowave');client.pop('decoder_pipeline',None);client.pop('decoder_cores',None)
    if name.endswith('4k120'):
        client.update(resolution='2160p',fps=120,hdr=False,chroma='420',bitrate_mbps=80)
        base['host_stimulus'].update(render_width=3840,render_height=2160,fps=240,update_hz=240)
    combinations=[{}]
    if name=='pyrowave-chroma-hdr':combinations=[dict(chroma=chroma,hdr=hdr) for chroma in ('420','444') for hdr in (False,True)]
    if name=='pacing':combinations=[dict(pacing=pacing,vsync=pacing!='unpaced') for pacing in ('unpaced','paced','vrr')]
    if name=='soak':timing['measure_seconds']=120
    if name=='decoder-regression':
        timing['measure_seconds']=80
        base['host_stimulus'].update(scene='decoder-regression',workload_backend='webgl2')
    result=[]
    for changes in combinations:
        value=copy.deepcopy(base);value['client'].update(changes)
        value['host_stimulus']['require_hdr']=bool(value['client'].get('hdr'))
        result.append(value)
    return result
