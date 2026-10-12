# ps5-native-app-boilerplate - read-only coordinator environment diagnostics.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import json
from urllib.request import Request, urlopen


def read_json(url, headers=None):
    with urlopen(Request(url,headers=headers or {}),timeout=5) as response:
        data=response.read(1024*1024+1)
    if len(data)>1024*1024: raise ValueError('probe response exceeds limit')
    return json.loads(data)


def check(scheduler):
    result={'schema_version':'prosperolight-test-doctor/1','mode':scheduler.execution_mode,'verdict':'PASS','checks':[]}
    if scheduler.execution_mode=='fake':
        result['checks'].append({'component':'adapters','status':'SIMULATED','reason':'no hardware probed'})
        return result
    ps5,host=scheduler.ps5,scheduler.host
    def probe(component, function):
        try: value=function();result['checks'].append({'component':component,'status':'PASS','details':value})
        except Exception as exc:
            result['verdict']='INCONCLUSIVE'
            result['checks'].append({'component':component,'status':'INCONCLUSIVE','reason_code':type(exc).__name__})
    def installation():
        value=ps5.ps5.installed()
        if value['eboot_sha256']!=ps5.expected: raise ValueError('installed hash mismatch')
        return value
    def title():
        inventory=read_json(f'http://{ps5.ps5.host}:{ps5.ps5.manager_port}/processes_list')
        if any(p.get('name')=='eboot.bin' for p in inventory['processes']):
            raise RuntimeError('title already active')
        return {'title_idle':True}
    def worker():
        value=read_json(host.url+'/health',{'Authorization':'Bearer '+host.token})
        desktop=value.get('interactive_desktop') or {}
        if not desktop.get('interactive'):raise RuntimeError('interactive desktop unavailable')
        return {'state':value.get('state'),'desktop':desktop}
    probe('PS5_FTP_INSTALLED_BUILD',installation)
    probe('PS5_PAYLOAD_MANAGER_TITLE_IDLE',title)
    probe('WINDOWS_WORKER_INTERACTIVE_DESKTOP',worker)
    result['limitations']=['Pairing and exact requested profile are validated at run preflight.',
                          'Read-only: no launch, deployment, lease, display-mode or configuration changes.']
    return result
