# ps5-native-app-boilerplate - executable sweep and advanced plan regressions.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from typer.testing import CliRunner
from pltest.coordinator.cli import app
from pltest.sweeps import bitrate_profiles, benchmark_profiles, saturation

BASE={'schema':'prosperolight-test/1','source_sha':'a'*40,'build_label':'developer',
      'client':{'codec':'hevc','chroma':'420','hdr':False,'resolution':'1080p','fps':60,
                'bitrate_mbps':20,'decoder_pipeline':'classic','decoder_cores':3,'pacing':'paced','vsync':True}}


class SweepTests(unittest.TestCase):
    def test_deterministic_plans_preserve_identity_and_backend_semantics(self):
        before=copy.deepcopy(BASE)
        values=bitrate_profiles(BASE,[10,20,40]);self.assertEqual(BASE,before)
        self.assertEqual([p['client']['bitrate_mbps'] for p in values],[10,20,40])
        self.assertEqual(values[0]['host_stimulus']['scene'],'gpu-entropy')
        for invalid in ([10],[20,10],[10,10],[True,20],[0,20],list(range(1,22))):
            with self.assertRaises(ValueError):bitrate_profiles(BASE,invalid)
        for name in ('native-4k120','pyrowave-4k120'):
            value=benchmark_profiles(BASE,name)[0]
            self.assertEqual((value['client']['resolution'],value['client']['fps']),('2160p',120))
            self.assertEqual(value['source_sha'],BASE['source_sha'])
            self.assertEqual(value['host_stimulus']['render_width'],3840)
        values=benchmark_profiles(BASE,'pyrowave-chroma-hdr');self.assertEqual(len(values),4)
        self.assertTrue(all('decoder_pipeline' not in p['client'] for p in values))
        self.assertEqual([p['client']['pacing'] for p in benchmark_profiles(BASE,'pacing')],['unpaced','paced','vrr'])
        self.assertEqual(benchmark_profiles(BASE,'soak')[0]['timing']['measure_seconds'],120)

    def test_saturation_requires_measured_plateau_and_fps_drop(self):
        def report(mbps,fps):return {'analysis':{'metrics':{'actual_mbps':mbps,'unique_presented_fps':fps}}}
        self.assertTrue(saturation(report(20,60),report(20.5,54)))
        self.assertFalse(saturation(report(20,60),report(10,60)))
        self.assertFalse(saturation(report(20,60),report(30,54)))
        self.assertIsNone(saturation(report(20,60),report(None,54)))

    def test_sweep_runs_sequentially_and_stops_without_relabeling_points(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'profile.json';path.write_text(json.dumps(BASE))
            created=[]
            def api(method,path,**kwargs):
                if path.endswith('/capabilities'):return {'api_mode':'real'}
                if path=='/api/v1/runs':
                    created.append(kwargs['json']['profile']);return {'run_id':str(len(created))}
                if path.endswith('/start'):return {}
                if path.endswith('/report'):
                    return {'run_id':str(len(created)),'overall_verdict':'PASS',
                        'analysis':{'metrics':{'actual_mbps':20 if len(created)==1 else 20.5,
                                               'unique_presented_fps':60 if len(created)==1 else 54}}}
                return {'state':'COMPLETED'}
            with patch('pltest.coordinator.cli.request',side_effect=api):
                result=CliRunner().invoke(app,['sweep','--profile',str(path),'--bitrates','20,40,60'])
            self.assertEqual(result.exit_code,3,result.output)
            self.assertEqual(len(created),2)
            summary=json.loads(result.output.splitlines()[-1])
            self.assertEqual(summary['reason_code'],'THROUGHPUT_PLATEAU_WITH_FPS_DROP')
            self.assertEqual(summary['performance_delta'],'NOT_COMPARABLE')
            self.assertEqual(summary['overall_verdict'],'INCONCLUSIVE')
            self.assertEqual(summary['sweep_outcome'],'SATURATION_DETECTED')
            self.assertEqual((summary['requested_points'],summary['completed_points']),(3,2))
            self.assertEqual(summary['completed_mbps'],[20,40])
            self.assertTrue(all(json.loads(line)['overall_verdict']=='PASS' for line in result.output.splitlines()[:-1]))
            self.assertEqual(summary['requested_mbps'],[20,40,60])

    def test_sweep_functional_failure_is_fail_not_invalid_measurement(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'profile.json';path.write_text(json.dumps(BASE))
            def api(method,path,**kwargs):
                if path.endswith('/capabilities'):return {'api_mode':'real'}
                if path=='/api/v1/runs':return {'run_id':'failed'}
                if path.endswith('/start'):return {}
                if path.endswith('/report'):return {'run_id':'failed','overall_verdict':'FAIL',
                    'execution_state':'FAILED','functional_verdict':'FAIL'}
                return {'state':'FAILED'}
            with patch('pltest.coordinator.cli.request',side_effect=api):
                result=CliRunner().invoke(app,['sweep','--profile',str(path),'--bitrates','20,40'])
            self.assertEqual(result.exit_code,1,result.output)
            summary=json.loads(result.output.splitlines()[-1])
            self.assertEqual(summary['sweep_outcome'],'FUNCTIONAL_FAILURE')
            self.assertEqual(summary['overall_verdict'],'FAIL')
            self.assertEqual(summary['execution_state'],'FAILED')
            self.assertEqual(summary['completed_points'],1)

    def test_sweep_api_failure_preserves_completed_count_and_exit_contract(self):
        import typer
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'profile.json';path.write_text(json.dumps(BASE))
            created=[]
            def api(method,path,**kwargs):
                if path.endswith('/capabilities'):return {'api_mode':'real'}
                if path=='/api/v1/runs':
                    if created:raise typer.Exit(2)
                    created.append(True);return {'run_id':'first'}
                if path.endswith('/start'):return {}
                if path.endswith('/report'):return {'run_id':'first','overall_verdict':'PASS',
                    'analysis':{'metrics':{'actual_mbps':20,'unique_presented_fps':60}}}
                return {'state':'COMPLETED'}
            with patch('pltest.coordinator.cli.request',side_effect=api):
                result=CliRunner().invoke(app,['sweep','--profile',str(path),'--bitrates','20,40'])
            self.assertEqual(result.exit_code,2,result.output)
            summary=json.loads(result.output.splitlines()[-1])
            self.assertEqual(summary['sweep_outcome'],'INFRASTRUCTURE_FAILURE')
            self.assertEqual((summary['requested_points'],summary['completed_points']),(2,1))
            self.assertEqual(summary['overall_verdict'],'INCONCLUSIVE')

    def test_paired_calibration_is_explicit_and_interleaves_six_independent_runs(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            manifest=dict(source_sha='a'*40,build_label='test-lab-a',eboot_sha256='a'*64,
                          build_flags={'TEST_LAB':1,'PERFORMANCE_DETAIL':1},profile=BASE)
            path=root/'variant.json';path.write_text(json.dumps(manifest))
            created=[];compared=[]
            def api(method,path,**kwargs):
                if path.endswith('/capabilities'):return {'api_mode':'real'}
                if path=='/api/v1/runs':
                    created.append(kwargs['json']);return {'run_id':str(len(created))}
                if path.endswith('/compare'):
                    compared.append(kwargs['json']);return {'verdict':'PASS'}
                if path.endswith('/start'):return {}
                return {'state':'COMPLETED','verdict':'PASS'}
            args=['paired','--baseline',str(path),'--candidate',str(path),'--manifest-root',str(root),'--allow-deploy']
            with patch('pltest.build.validate_variant',return_value=manifest), patch('pltest.coordinator.cli.request',side_effect=api):
                rejected=CliRunner().invoke(app,args)
                self.assertEqual(rejected.exit_code,3);self.assertFalse(created)
                result=CliRunner().invoke(app,args+['--calibration'])
            self.assertEqual(result.exit_code,0,result.output)
            self.assertEqual(len(created),6)
            self.assertEqual(compared,[{'pairs':[['1','2'],['3','4'],['5','6']],'calibration':True}])

    def test_noisy_calibration_reference_stops_before_deployment(self):
        import uuid
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);paths=[]
            for letter in ('a','b'):
                manifest=dict(source_sha=letter*40,build_label='test-lab-'+letter,eboot_sha256=letter*64,
                              build_flags={'TEST_LAB':1,'PERFORMANCE_DETAIL':1},profile=BASE)
                path=root/(letter+'.json');path.write_text(json.dumps(manifest));paths.append(path)
            reference=root/'reference.json';reference.write_text(json.dumps([[str(uuid.uuid4()),str(uuid.uuid4())] for _ in range(3)]))
            calls=[]
            def api(method,path,**kwargs):
                calls.append(path)
                if path.endswith('/capabilities'):return {'api_mode':'real'}
                if path.endswith('/compare'):return {'verdict':'INCONCLUSIVE','screening_usable':False,'reason_code':'AA_NOISE_CALIBRATION'}
                raise AssertionError('deployment must not start')
            with patch('pltest.build.validate_variant',side_effect=lambda value,root:value), patch('pltest.coordinator.cli.request',side_effect=api):
                result=CliRunner().invoke(app,['paired','--baseline',str(paths[0]),'--candidate',str(paths[1]),
                    '--manifest-root',str(root),'--allow-deploy','--calibration-pairs',str(reference)])
            self.assertEqual(result.exit_code,3,result.output)
            self.assertEqual(calls,['/api/v1/capabilities','/api/v1/compare'])
