# ps5-native-app-boilerplate - isolate encoder overrides and preserve host apps.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import copy
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/test-lab/src'))
from pltest.host.encoder_profile import software_profile, IDENTITY, NAME, OVERRIDES


class Api:
    def __init__(self):
        self.apps = [dict(name='Desktop', uuid='desktop', id='1', index=0),
                     dict(name='A game', uuid='game', id='2', index=1, cmd='original')]
        self.calls = []; self.busy = False
    async def ensure_idle(self, cancel):
        if self.busy: raise RuntimeError('HOST_STREAM_BUSY')
    async def request(self, method, path, body=None):
        self.calls.append((method, path))
        if method == 'GET': return {'apps': copy.deepcopy(self.apps)}
        if path == '/api/apps':
            app=copy.deepcopy(body);app.update(id='3');self.apps.append(app)
            self.apps.sort(key=lambda a:a['name'])
        else:
            self.apps.sort(key=lambda a:body['order'].index(a['uuid']))
        for index,app in enumerate(self.apps):app['index']=index
        return {'status':True}


class ProfileTests(unittest.IsolatedAsyncioTestCase):
    async def test_plan_does_not_mutate(self):
        api=Api();before=copy.deepcopy(api.apps)
        self.assertEqual((await software_profile(api))['state'],'PLANNED')
        self.assertEqual(api.apps,before)
        self.assertFalse(any(c[0]=='POST' for c in api.calls))
    async def test_create_restores_original_apps_and_is_idempotent(self):
        api=Api();before=copy.deepcopy(api.apps)
        self.assertEqual((await software_profile(api,True))['state'],'CREATED')
        self.assertEqual(api.apps[:2],before)
        self.assertEqual(api.apps[-1]['config-overrides'],OVERRIDES)
        posts=sum(c[0]=='POST' for c in api.calls)
        self.assertEqual((await software_profile(api,True))['state'],'PRESENT')
        self.assertEqual(sum(c[0]=='POST' for c in api.calls),posts)
    async def test_busy_host_and_conflicting_profile_are_not_modified(self):
        api=Api();api.busy=True
        with self.assertRaisesRegex(RuntimeError,'BUSY'):await software_profile(api,True)
        self.assertFalse(any(c[0]=='POST' for c in api.calls))
        api.busy=False;api.apps.append(dict(name=NAME,uuid=IDENTITY,index=2))
        with self.assertRaisesRegex(ValueError,'CONFLICT'):await software_profile(api,True)

if __name__=='__main__':unittest.main()
