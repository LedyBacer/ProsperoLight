# ps5-native-app-boilerplate - worker protocol, display ambiguity and Ready proofs.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools/test-lab/src'), str(ROOT / 'tools/test-lab')]
from pltest.host.protocol import request, readiness, validate_report, SCHEMA
from pltest.host.display import select
from pltest.host.logs import LogSlice


class WorkerTests(unittest.TestCase):
    def test_webgl2_unsupported_and_workload_bounds(self):
        value = {'schema':SCHEMA,'run_id':'gpu','command_id':'prepare','action':'prepare',
                 'settings':{'scene':'gpu-entropy','seed':9,'fps':240,'seconds':30,'monitor':'virtual',
                             'entropy':1,'movement':2,'update_hz':120,'render_width':1920,'render_height':1080}}
        request(value)
        value['settings']['entropy'] = float('nan')
        with self.assertRaises(ValueError): request(value)
        node = shutil.which('node')
        if not node: self.skipTest('Node unavailable')
        source = ROOT/'tools/test-lab/src/pltest/host/static/gpu-scene.js'
        code = """const fs=require('fs'),vm=require('vm');const s={};vm.createContext(s);
vm.runInContext(fs.readFileSync(process.argv[1],'utf8'),s);
try{vm.runInContext('new PLTestGpuScene({getContext(){return null;}},{})',s);throw Error('unsupported context accepted');}
catch(e){if(e.message!=='UNSUPPORTED_WEBGL2')throw e;}"""
        subprocess.run([node,'-e',code,str(source)],check=True)

    def test_multi_gpu_requires_identity_and_reports_sampling_periods(self):
        from pltest.host.metrics import GpuSampler
        from types import SimpleNamespace
        class Library:
            def nvmlInit(self): pass
            def nvmlShutdown(self): pass
            def nvmlDeviceGetCount(self): return 2
            def nvmlDeviceGetHandleByIndex(self, index): return index
            def nvmlDeviceGetUUID(self, index): return ['GPU-A','GPU-B'][index]
            def nvmlDeviceGetName(self, index): return 'adapter '+str(index)
            def nvmlDeviceGetHandleByUUID(self, uuid):
                self.selected = uuid; return uuid
            def nvmlDeviceGetUtilizationRates(self, gpu): return SimpleNamespace(gpu=23)
            def nvmlDeviceGetMemoryInfo(self, gpu): return SimpleNamespace(used=1024)
            def nvmlDeviceGetEncoderUtilization(self, gpu): return 17, 16667
        library = Library()
        with patch.dict('os.environ', {}, clear=True):
            automatic = GpuSampler(library=library)
            result = automatic.sample()
            self.assertIsNone(result['encoder_utilization_pct'])
            self.assertEqual(len(result['gpu_inventory']), 2)
            explicit = GpuSampler('GPU-B', library=library)
            with patch('pltest.host.metrics.time.monotonic_ns', side_effect=[1000000,2000000]):
                explicit.sample(); result = explicit.sample()
            self.assertEqual(library.selected, 'GPU-B')
            self.assertEqual(result['sampler_period_us'], 1000)
            self.assertEqual(result['encoder_sample_period_us'], 16667)
            self.assertFalse(result['encode_adapter_verified'])
            self.assertIsNone(result['encoding_latency_ms'])
            self.assertEqual(GpuSampler('absent',library=library).sample()['reason'], 'SELECTED_GPU_UUID_NOT_FOUND')

    def test_commands_display_and_actual_ready(self):
        value = {'schema': SCHEMA, 'run_id': 'run', 'command_id': 'cmd', 'action': 'prepare',
                 'settings': {'scene': 'noise', 'seed': 1, 'fps': 60, 'seconds': 2, 'monitor': 'virtual'}}
        request(value)
        value['settings']['fps'] = True
        with self.assertRaises(ValueError): request(value)
        value['settings']['fps'] = 60; value['run_id'] = '../x'
        with self.assertRaises(ValueError): request(value)
        monitor = {'id': 'display', 'virtual': True, 'x': 0, 'y': 0, 'width': 1920, 'height': 1080}
        inventory = {'interactive': True, 'displays': [monitor]}
        self.assertEqual(select(inventory, 'virtual'), monitor)
        inventory['displays'].append(dict(monitor, id='other'))
        with self.assertRaises(RuntimeError): select(inventory, 'virtual')
        page = {'visibility': 'visible', 'focused': True, 'fullscreen': True, 'raf_fps': 60,
                'dpr': 1, 'screen_x': 0, 'screen_y': 0, 'outer_width': 1920, 'outer_height': 1080,
                'screen_width': 1920, 'screen_height': 1080}
        self.assertTrue(readiness(page, monitor, 60, inventory)['ready'])
        high_refresh = dict(monitor,refresh_hz=480)
        self.assertTrue(readiness(dict(page,raf_fps=479),high_refresh,240,inventory)['ready'])
        self.assertFalse(readiness(dict(page,raf_fps=720),high_refresh,240,inventory)['ready'])
        for key, value in [('visibility', 'hidden'), ('fullscreen', False), ('raf_fps', 30),
                           ('raf_fps', 120), ('screen_x', 1920), ('dpr', float('nan'))]:
            changed = dict(page); changed[key] = value
            self.assertFalse(readiness(changed, monitor, 60, inventory)['ready'])
        self.assertFalse(readiness(page, monitor, 60, {'interactive': False})['ready'])

    def test_reports_use_actual_intervals_and_redacted_log_slice(self):
        report = {'type': 'report', 'intervals_ms': [20, 20], 'measured_intervals': 2, 'raf_fps': 999}
        self.assertEqual(validate_report(report)['raf_fps'], 50)
        report['intervals_ms'] = [float('nan'), 20]
        with self.assertRaises(ValueError): validate_report(report)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'host.log'; path.write_text('old\n')
            logs = LogSlice(path)
            with path.open('a') as f: f.write('Authorization: Bearer private\npassword=private token=private\nnew\n')
            out = Path(directory) / 'slice.txt'; result = logs.finish(out)
            self.assertTrue(result['supported']); self.assertNotIn('private', out.read_text()); self.assertNotIn('old', out.read_text())

    def test_cleanup_failure_invalidates_completed_measurement(self):
        import asyncio
        from pltest.host.worker import Worker
        observed = []
        class Browser:
            async def close(self):
                observed.append(worker.state)
                raise RuntimeError('close failed')
        worker = None
        async def check(folder):
            nonlocal worker
            worker = Worker('x' * 32, folder)
            worker.folder = Path(folder); worker.run_id = 'cleanup'
            worker.state = 'RUNNING'
            worker.settings = {'fps': 60, 'seconds': 1}
            worker.ready = {'ready': True}; worker.monitor = {}; worker.content_hash = 'hash'
            worker.logs = LogSlice(None); worker.browser = Browser()
            worker.report = {'reason': 'COMPLETE', 'raf_fps': 60, 'intervals_ms': [1000]}
            await worker.finalize('COMPLETE')
            self.assertEqual(worker.report['measurement_validity'], 'INCONCLUSIVE')
            self.assertFalse(worker.report['resource_cleanup']['complete'])
            self.assertEqual(worker.state, 'INCONCLUSIVE')
            self.assertEqual(observed, ['RUNNING'])
        with tempfile.TemporaryDirectory() as folder:
            asyncio.run(check(folder))

    def test_shared_noise_bytes_preserve_original_pattern(self):
        node = shutil.which('node')
        if not node: self.skipTest('Node needed for deterministic pattern fixture')
        path = ROOT / 'tools/test-lab/src/pltest/host/static/patterns.js'
        code = '''const p=require(process.argv[1]);let data=new Uint8Array(4096);p.fillNoise(data,0x51dec0de);
let old=new Uint8Array(4096),seed=0x51dec0de;for(let i=0;i<old.length;i+=4){seed^=seed<<13;seed^=seed>>>17;seed^=seed<<5;const shade=32+(seed>>>25);old[i]=old[i+1]=old[i+2]=shade;old[i+3]=255;}
if(Buffer.compare(Buffer.from(data),Buffer.from(old)))throw Error('original noise changed');'''
        subprocess.run([node, '-e', code, str(path)], check=True)

    def test_animation_waits_for_async_texture_preparation(self):
        node = shutil.which('node')
        if not node: self.skipTest('Node needed for browser preparation fixture')
        source = ROOT / 'tools/test-lab/src/pltest/host/static/stimulus.js'
        code = r'''
const fs=require('fs'),vm=require('vm');let resolveHash,callbacks=0;const messages=[];
const ctx={drawImage(image){if(!image)throw Error('unprepared texture');},fillRect(){},createImageData(){return {data:new Uint8ClampedArray(16)};},putImageData(){}};
const canvas=()=>({getContext(){return ctx;},cloneNode(){return canvas();},replaceWith(){}}),nodes={'#scene':canvas(),'#ready':{},'#status':{}};
class WS {static OPEN=1;constructor(){this.readyState=1;}send(value){messages.push(JSON.parse(value));}}
const sandbox={document:{visibilityState:'visible',hasFocus:()=>true,fullscreenElement:{},querySelector:s=>nodes[s],createElement:canvas,body:{classList:{add(){},remove(){}}},addEventListener(){}},location:{host:'localhost'},WebSocket:WS,
devicePixelRatio:1,window:{screenX:0,screenY:0},outerWidth:2,outerHeight:2,screen:{width:2,height:2},
requestAnimationFrame(){callbacks++;},setTimeout,crypto:{subtle:{digest:()=>new Promise(r=>resolveHash=r)}},PLTestPatterns:{fillNoise(){}}};
vm.createContext(sandbox);vm.runInContext(fs.readFileSync(process.argv[1],'utf8'),sandbox);
const pending=vm.runInContext('prepare({width:2,height:2,seed:1,fps:60,display_refresh_hz:480,seconds:2,warmup_seconds:0,scene:"low-motion",run_id:"run"})',sandbox);
if(vm.runInContext('intervals.length',sandbox)<480*7)throw Error('measurement buffer ignores actual display refresh');
vm.runInContext('tick(1)',sandbox);if(callbacks!==2)throw Error('animation loop stopped during preparation');
resolveHash(new ArrayBuffer(32));pending.then(()=>{
 if(!vm.runInContext('prepared',sandbox))throw Error('preparation did not complete');
 for(const hz of [60,240,480,1000]){
  messages.length=0;vm.runInContext('preflightFrames=[];pendingReady=true;lastPulse=0',sandbox);
  for(let frame=0;frame<=Math.ceil(hz*2.1);frame++)vm.runInContext(`tick(${frame*1000/hz})`,sandbox);
  const ready=messages.filter(m=>m.type==='ready');
  if(ready.length!==1||Math.abs(ready[0].snapshot.raf_fps-hz)>.01)throw Error('Ready failed at '+hz+' Hz');
  if(vm.runInContext('preflightFrames.length',sandbox)>4096)throw Error('unbounded Ready samples');
 }
}).catch(e=>{console.error(e);process.exitCode=1;});
'''
        subprocess.run([node, '-e', code, str(source)], check=True)


@unittest.skipUnless(importlib.util.find_spec('aiohttp'), 'optional worker dependency aiohttp unavailable')
class FakeWorkerSession(unittest.IsolatedAsyncioTestCase):
    async def test_final_report_uses_actual_refresh_and_explains_invalidity(self):
        from pltest.host.worker import Worker
        from types import SimpleNamespace
        for rate, refresh, hidden, expected in ((480,480,0,None),(480,240,0,'RAF_ABOVE_TARGET'),
                                               (220,480,0,'RAF_BELOW_TARGET'),(480,480,1,'BROWSER_HIDDEN')):
            with tempfile.TemporaryDirectory() as directory:
                worker=Worker('a'*32,directory,mode='attached')
                worker.state='RUNNING';worker.run_id='fixture';worker.folder=Path(directory)
                worker.settings={'fps':240,'seconds':2};worker.monitor={'refresh_hz':refresh}
                worker.content_hash='a'*64;worker.ready={'ready':True}
                worker.logs=SimpleNamespace(finish=lambda path: {'supported':False})
                worker.report={'reason':'COMPLETE','raf_fps':rate,'intervals_ms':[1000/rate]*round(rate*2),
                               'hidden_events':hidden,'fullscreen_lost':0,'interval_overflow':False}
                await worker.finalize('COMPLETE')
                self.assertEqual(worker.report['measurement_validity'],'VALID' if expected is None else 'INCONCLUSIVE')
                self.assertEqual(worker.report['validity_reasons'],[] if expected is None else [expected])
                self.assertIn('worker.py',worker.report['worker_runtime_identity']['files'])

    async def test_authenticated_fake_page_prepare_start_and_report(self):
        from aiohttp.test_utils import TestServer, TestClient
        from pltest.host.worker import Worker
        from pltest.host.client import HostWorkerAdapter
        monitor = {'id': 'display', 'virtual': True, 'x': 0, 'y': 0, 'width': 1920, 'height': 1080, 'hdr': False}
        inventory = {'interactive': True, 'displays': [monitor]}
        page = {'visibility': 'visible', 'focused': True, 'fullscreen': True, 'raf_fps': 60,
                'dpr': 1, 'screen_x': 0, 'screen_y': 0, 'outer_width': 1920, 'outer_height': 1080,
                'screen_width': 1920, 'screen_height': 1080}
        import asyncio
        with tempfile.TemporaryDirectory() as directory, patch('pltest.host.worker.display.probe', return_value=inventory):
            worker = Worker('a' * 32, directory, mode='attached')
            server = TestServer(worker.app()); await server.start_server()
            worker.port = server.port
            client = TestClient(server); await client.start_server()
            self.assertEqual((await client.get('/health')).status, 401)
            self.assertEqual((await client.get('/page-token')).status, 403)
            page_socket = await client.ws_connect('/page', headers={'Origin': f'http://127.0.0.1:{server.port}'})
            await page_socket.send_json({'type': 'auth', 'token': worker.page_token})
            async def fake_page():
                prepared = await page_socket.receive_json(timeout=5)
                run_id = prepared['settings']['run_id']
                await page_socket.send_json({'type': 'prepared', 'run_id': run_id})
                await page_socket.send_json({'type': 'heartbeat', 'run_id': run_id, 'snapshot': page})
                await page_socket.send_json({'type': 'ready', 'run_id': run_id, 'snapshot': page})
                start = await page_socket.receive_json(timeout=5)
                self.assertEqual(start['action'], 'start')
                await page_socket.send_json({'type': 'started', 'run_id': run_id,
                                            'command_id': start['command_id'], 'browser_origin_ms': 100})
                probe = await page_socket.receive_json(timeout=5)
                self.assertEqual(probe['action'], 'clock_probe')
                await page_socket.send_json({'type':'clock_probe','run_id':run_id,
                    'command_id':probe['command_id'],'browser_now_ms':1900})
                await page_socket.send_json({'type': 'report', 'run_id': run_id, 'reason': 'COMPLETE',
                    'intervals_ms': [1000 / 60] * 120, 'measured_intervals': 120, 'raf_fps': 999,
                    'rendered_frames': 121, 'hidden_events': 0, 'fullscreen_lost': 0, 'interval_overflow': False})
            task = asyncio.create_task(fake_page())
            async with HostWorkerAdapter(str(server.make_url('')).rstrip('/'), 'a' * 32) as adapter:
                settings = {'scene': 'low-motion', 'seed': 1, 'fps': 60, 'seconds': 2, 'monitor': 'virtual'}
                ack = await adapter.command('run', 'prepare', settings)
                self.assertEqual(ack['state'], 'READY')
                await adapter.command('run', 'start')
                clock = await adapter.command('run', 'clock_probe')
                self.assertEqual(clock['browser_clock']['command_id'], clock['command_id'])
                self.assertEqual(clock['browser_clock']['browser_now_ms'],1900)
                report = await adapter.wait_report('run', timeout=5)
                self.assertEqual(report['measurement_validity'], 'VALID')
                self.assertAlmostEqual(report['raf_fps'], 60)
            await task; await page_socket.close(); await client.close()
            self.assertTrue((Path(directory) / 'run/host-report.json').is_file())


class AdvancedColorTests(unittest.TestCase):
    def test_hdr_specific_bits_do_not_confuse_wide_color_with_hdr(self):
        import ctypes
        from pltest.host.advanced_color import ColorInfo, DisplayPath, ModeInfo, SourceName, decode
        self.assertEqual(ctypes.sizeof(DisplayPath),72)
        self.assertEqual(ctypes.sizeof(ModeInfo),64)
        self.assertEqual(ctypes.sizeof(SourceName),84)
        value=ColorInfo();value.value=3 | (1<<6);value.active_mode=1;value.bits=10
        result=decode(value);self.assertFalse(result['hdr']);self.assertFalse(result['hdr_active'])
        value.value |= (1<<4)|(1<<5);value.active_mode=2
        result=decode(value);self.assertTrue(result['hdr']);self.assertTrue(result['hdr_active'])
        self.assertTrue(result['hdr_verified']);self.assertEqual(result['color_bits'],10)
