# ps5-native-app-boilerplate - dashboard boundary, read-only doctor and local exports.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import io
import json
import tempfile
import time
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

from fastapi.testclient import TestClient
from pltest.coordinator.api import create_app
from pltest.export import scrub


class DashboardTests(unittest.TestCase):
    def test_bootstrap_is_public_api_requires_bearer_and_origins_are_restricted(self):
        with tempfile.TemporaryDirectory() as directory, TestClient(create_app(directory,token='x'*32)) as client:
            page=client.get('/')
            self.assertEqual(page.status_code,200)
            self.assertIn("frame-ancestors 'none'",page.headers['content-security-policy'])
            self.assertEqual(client.get('/dashboard/app.js').status_code,200)
            self.assertEqual(client.get('/dashboard/../models.py',headers={'Authorization':'Bearer '+'x'*32}).status_code,404)
            self.assertEqual(client.get('/api/v1/runs').status_code,401)
            self.assertEqual(client.get('/api/v1/runs',headers={'Authorization':'Bearer '+'x'*32}).status_code,200)
            for origin in ('https://untrusted.example','null'):
                self.assertEqual(client.post('/api/v1/runs',headers={'Origin':origin,'Authorization':'Bearer '+'x'*32},json={}).status_code,403)
            self.assertEqual(client.get('/api/v1/health',headers={'Host':'untrusted.example'}).status_code,400)
            self.assertEqual(client.post('/api/v1/compare',headers={'Sec-Fetch-Site':'cross-site'},json={}).status_code,403)

    def test_history_limits_and_doctor_are_read_only(self):
        with tempfile.TemporaryDirectory() as directory, TestClient(create_app(directory)) as client:
            before=list(Path(directory).rglob('*'))
            result=client.get('/api/v1/doctor').json()
            self.assertEqual(result['checks'][0]['status'],'SIMULATED')
            self.assertEqual(before,list(Path(directory).rglob('*')))
            self.assertEqual(client.get('/api/v1/runs?limit=201').status_code,422)
            self.assertEqual(client.get('/api/v1/runs?limit=0').status_code,422)

    def test_terminal_bundle_hashes_transformed_bytes_and_preserves_raw(self):
        with tempfile.TemporaryDirectory() as directory, TestClient(create_app(directory)) as client:
            run=client.post('/api/v1/runs',json={'suite':'fake-smoke','profile':{'schema':'prosperolight-test/1'}}).json()
            rid=run['run_id']
            self.assertEqual(client.get(f'/api/v1/runs/{rid}/bundle').status_code,409)
            client.post(f'/api/v1/runs/{rid}/start')
            for _ in range(100):
                if client.get(f'/api/v1/runs/{rid}').json()['state']=='COMPLETED':break
                time.sleep(.01)
            root=Path(directory)/'runs'/rid
            private=root/'private.json';private.write_text(json.dumps({'host':'192.168.1.5','password':'never-export'}))
            from pltest.report import finalize_manifest
            finalize_manifest(root,rid)
            response=client.get(f'/api/v1/runs/{rid}/bundle')
            self.assertEqual(response.status_code,200)
            with zipfile.ZipFile(io.BytesIO(response.content)) as archive:
                exported=json.loads(archive.read('private.json'))
                self.assertEqual(exported,{'host':'[private-ip]','password':'[redacted]'})
                manifest=json.loads(archive.read('export-manifest.json'))
                for name,item in manifest['files'].items():
                    self.assertEqual(hashlib.sha256(archive.read(name)).hexdigest(),item['sha256'])
                self.assertIn('report.html',archive.namelist())
            self.assertIn('never-export',private.read_text())
            private.write_text('{}')
            rejected=client.get(f'/api/v1/runs/{rid}/bundle')
            self.assertEqual(rejected.status_code,409)
            self.assertIn('ARTIFACT_MANIFEST_HASH_MISMATCH',rejected.text)
            local=json.loads((root/'manifest.json').read_text())
            self.assertEqual(local['files']['report.json']['sha256'],hashlib.sha256((root/'report.json').read_bytes()).hexdigest())
            self.assertEqual(len((root/'events.jsonl').read_text().splitlines()),len(client.get(f'/api/v1/runs/{rid}/events').json()['items']))

    def test_redaction_keeps_public_addresses(self):
        self.assertEqual(scrub({'x':'1.1.1.1 192.168.0.1','token':'secret'}),{'x':'1.1.1.1 [private-ip]','token':'[redacted]'})

    def test_websocket_origin_rejected_even_with_token(self):
        from starlette.websockets import WebSocketDisconnect
        with tempfile.TemporaryDirectory() as directory, TestClient(create_app(directory,token='x'*32)) as client:
            with self.assertRaises(WebSocketDisconnect) as context:
                with client.websocket_connect('/api/v1/ws',headers={'Origin':'https://untrusted.example','Authorization':'Bearer '+'x'*32}):pass
            self.assertEqual(context.exception.code,4403)
