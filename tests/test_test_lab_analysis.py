# ps5-native-app-boilerplate - evidence, trace and paired comparison regressions.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import copy
import json
from pathlib import Path
import tempfile
import unittest
import uuid

from pltest.analysis import analyze_run, parse_trace, analyze_pyrowave
from pltest.comparison import compare_pairs
from pltest.suites import apply_suite
from pltest.report import archive_analysis

ROOT = Path(__file__).resolve().parents[1]


def run_fixture(fps=60, age=3000, ready=1500):
    return {'run_id': str(uuid.uuid4()), 'profile': {'profile': {
        'source_sha': 'a'*40, 'build_label': 'test-lab-fixture',
        'client': {'codec': 'hevc', 'fps': 60},
        'analysis_environment': dict(verified=True, firmware='x', host_version='v1',
            encoder_settings={'codec':'hevc'}, network_class='wired', build_flags={'TEST_LAB':1}, renderer='native')}},
        'report': {'execution_mode':'real', 'host_measurement': {
            'selected_display':{'id':'virtual', 'width':1920, 'height':1080,'refresh_hz':240},
            'scene':'low-motion','seed':137,'content_sha256':'a'*64,'pixel_sample_sha256':'b'*64},
            'analysis': {'functional_verdict':'PASS','measurement_validity':'VALID','trace':{'omitted':0},
                'metrics':{'unique_presented_fps':fps,'frame_age_us':{'p95':age},'callback_to_ready_us':{'p95':ready}}}}}


class AnalysisTests(unittest.TestCase):
    def test_negotiation_requires_callback_identity_and_every_material_field(self):
        from pltest.session_evidence import validate_negotiation
        client = dict(codec='hevc', hdr=False, chroma='420', resolution='1080p', fps=60)
        receipt = {'negotiated': {'codec': 'hevc'}}
        fields, issues = validate_negotiation(client, receipt)
        self.assertEqual(len(issues), 7)
        self.assertFalse(fields['codec']['verified'])
        receipt['negotiated'] = dict(source='Limelight.renderer.setup.success', verified=True,
                                    video_format=256, width=1920, height=1080, fps=60)
        self.assertEqual(validate_negotiation(client, receipt)[1], [])
        receipt['negotiated']['fps'] = 120
        self.assertIn('NEGOTIATED_FIELD_MISMATCH:fps', validate_negotiation(client, receipt)[1])
        receipt['negotiated']['fps'] = True
        self.assertIn('NEGOTIATED_FIELD_UNVERIFIED:fps', validate_negotiation(client, receipt)[1])
        client.update(codec='pyrowave', hdr=True, chroma='444')
        receipt['negotiated'].update(video_format=0x80000, fps=60)
        self.assertEqual(validate_negotiation(client, receipt)[1], [])
        receipt['negotiated']['verified'] = False
        self.assertEqual(len(validate_negotiation(client, receipt)[1]), 7)

    def test_canonical_terminal_classifications(self):
        from pltest.verdicts import classify
        report = {'execution_mode': 'real', 'verdict': 'PASS', 'analysis': {
            'functional_verdict': 'PASS', 'measurement_validity': 'INCONCLUSIVE'}}
        value = classify(report)
        self.assertEqual((value['execution_state'], value['overall_verdict']),
                         ('COMPLETED', 'INCONCLUSIVE'))
        report['analysis']['measurement_validity'] = 'VALID'
        self.assertEqual(classify(report)['overall_verdict'], 'PASS')
        report['analysis']['performance_delta'] = 'REGRESSED'
        self.assertEqual(classify(report)['overall_verdict'], 'FAIL')
        report['analysis']['performance_delta'] = 'IMPROVED'
        self.assertEqual(classify(report)['overall_verdict'], 'PASS')
        report['analysis']['functional_verdict'] = 'FAIL'
        self.assertEqual(classify(report)['overall_verdict'], 'FAIL')
        report['analysis']['functional_verdict'] = 'PASS'
        report['reason_code'] = 'CLEANUP_FAILED'
        self.assertEqual(classify(report)['overall_verdict'], 'INCONCLUSIVE')
        report.pop('reason_code'); report['verdict'] = 'CANCELLED'
        self.assertEqual(classify(report)['overall_verdict'], 'CANCELLED')
        report['verdict'] = 'PASS'; report['execution_mode'] = 'fake'
        self.assertEqual(classify(report)['overall_verdict'], 'SIMULATED')

    def pairs(self, fps=60, age=3000):
        return [(run_fixture(),run_fixture(fps,age)) for _ in range(3)]

    def test_three_pairs_regression_and_improvement_screening(self):
        self.assertEqual(compare_pairs(self.pairs(57))['performance_delta'],'REGRESSED')
        self.assertEqual(compare_pairs(self.pairs(63))['performance_delta'],'IMPROVED')
        self.assertEqual(compare_pairs(self.pairs(60,4500))['verdict'],'FAIL')
        self.assertEqual(compare_pairs(self.pairs(60,3300))['performance_delta'],'NO_CLEAR_CHANGE')

    def test_missing_environment_mismatched_scene_and_invalid_measurement(self):
        pairs=self.pairs();pairs[0][1]['profile']['profile']['analysis_environment'].pop('firmware')
        self.assertEqual(compare_pairs(pairs)['reason_code'],'FINGERPRINT_INCOMPLETE')
        pairs=self.pairs();pairs[1][1]['report']['host_measurement']['seed']=138
        self.assertEqual(compare_pairs(pairs)['reason_code'],'ENVIRONMENT_MISMATCH')
        pairs=self.pairs();pairs[2][1]['report']['analysis']['measurement_validity']='INCONCLUSIVE'
        self.assertEqual(compare_pairs(pairs)['reason_code'],'INVALID_MEASUREMENT')

    def test_duplicate_pairs_and_insufficient_samples(self):
        pairs=self.pairs()
        self.assertEqual(compare_pairs(pairs[:1])['reason_code'],'INSUFFICIENT_REPEAT_PAIRS')
        pairs[2]=pairs[0]
        self.assertEqual(compare_pairs(pairs)['reason_code'],'REUSED_RUN_IN_PAIRS')

    def test_new_fault_is_functional_failure(self):
        pairs=self.pairs();pairs[1][1]['report']['analysis']['functional_verdict']='FAIL'
        value=compare_pairs(pairs)
        self.assertEqual(value['functional_verdict'],'FAIL')
        self.assertEqual(value['performance_delta'],'NOT_COMPARABLE')

    def test_trace_metadata_count_and_truncation(self):
        trace=b'# schema=2,count=1,omitted=2\nframe,bytes,pending,outcome,enqueue_us,callback_network_us,callback_us,decode_us,ready_us,completion_us\n1,1,0,1,1,2,100,10,10,120\n'
        self.assertEqual(parse_trace(trace)[1],2)
        for invalid in (trace.replace(b'count=1',b'count=2'),trace.replace(b'schema=2',b'schema=99')):
            with self.assertRaises(ValueError):parse_trace(invalid)

    def test_native_validity_fault_loss_truncation_and_settings(self):
        from test_decoder_benchmark import fixture
        browser, performance, trace = fixture()
        performance.update(vsync_active=1, output_width=3840, output_height=2160, refresh_x100=12000)
        profile = {'client': {'codec':'hevc','chroma':'420','hdr':False,'resolution':'2160p',
            'fps':120,'bitrate_mbps':80,'decoder_pipeline':'adaptive','decoder_cores':3,'vsync':True}}
        report = {'execution_mode':'real','verdict':'PASS','host_measurement':{'measurement_validity':'VALID'},
            'ps5_measurement':{'title_exit_verified':True,'stale_artifacts':[], 'receipt':{
                'functional_verdict':'PASS','negotiated':{'source':'Limelight.renderer.setup.success','verified':True,'video_format':256,'width':3840,'height':2160,'fps':120},'artifacts_complete':True,
                'measure_start_monotonic_us':1000000,'measure_end_monotonic_us':1030000,
                'trace':{'name':'performance-frames-mode0.csv'}}}}
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)/'ps5/artifacts';root.mkdir(parents=True)
            (root/'performance-last.json').write_text(json.dumps(performance))
            (root/'performance-frames-mode0.csv').write_bytes(trace)
            result=analyze_run(profile,report,directory)
            self.assertEqual(result['measurement_validity'],'VALID')
            self.assertEqual(result['metrics']['frame_age_us']['p95'],20000)
            performance['decode_errors']=1
            (root/'performance-last.json').write_text(json.dumps(performance))
            self.assertEqual(analyze_run(profile,report,directory)['functional_verdict'],'FAIL')
            performance.pop('decode_errors');performance['network_frame_gaps']=1
            (root/'performance-last.json').write_text(json.dumps(performance))
            self.assertEqual(analyze_run(profile,report,directory)['measurement_validity'],'INCONCLUSIVE')
            performance.pop('network_frame_gaps');performance['decoder_pipeline_depth']=1
            (root/'performance-last.json').write_text(json.dumps(performance))
            self.assertIn('EFFECTIVE_SETTINGS_MISMATCH',analyze_run(profile,report,directory)['issues'])
            performance['decoder_pipeline_depth']=3;performance['fps']=60
            (root/'performance-last.json').write_text(json.dumps(performance))
            self.assertIn('EFFECTIVE_SETTINGS_MISMATCH',analyze_run(profile,report,directory)['issues'])
            (root/'performance-frames-mode0.csv').write_bytes(trace.replace(b'omitted=0',b'omitted=1'))
            self.assertIn('TRACE_TRUNCATED',analyze_run(profile,report,directory)['issues'])

    def test_fake_never_produces_valid_performance_measurement(self):
        result=analyze_run({}, {'execution_mode':'fake','verdict':'PASS'},'.')
        self.assertEqual(result['functional_verdict'],'PASS')
        self.assertEqual(result['measurement_validity'],'INCONCLUSIVE')
        self.assertEqual(result['performance_delta'],'NOT_COMPARABLE')

    def test_pyrowave_output_pictures_repeats_and_clock(self):
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory)/'pyrowave-output-mode1.csv'
            p.write_text('# mode=1,fps=60,count=3,omitted=0\nframe,pts_us,ready_us,submit_us,observed_us,flip_count\n1,0,10,20,30,1\n1,0,10,20,40,2\n2,1,20,30,50,3\n')
            result=analyze_pyrowave(directory,{'measure_start_monotonic_us':1,'measure_end_monotonic_us':100},
                {'client':{'pacing':'paced','fps':60}})
            self.assertEqual(result['repeat_samples'],1)
            self.assertEqual(result['unique_pictures'],2)
            self.assertIn('not physical',result['clock'])
            p.write_text(p.read_text().replace('omitted=0','omitted=1'))
            with self.assertRaises(ValueError):analyze_pyrowave(directory,{}, {'client':{'pacing':'paced','fps':60}})

    def test_regression_suite_requires_exact_phases_and_soak_segments(self):
        profile=json.loads((ROOT/'tools/test-lab/profiles/pr2-native-smoke.json').read_text())
        profile['timing'].update(warmup_seconds=10,measure_seconds=80)
        self.assertEqual(apply_suite('decoder-matrix',profile)['host_stimulus']['scene'],'decoder-regression')
        profile['timing']['measure_seconds']=600
        with self.assertRaises(ValueError):apply_suite('soak',profile)

    def test_archive_html_escapes_untrusted_input_and_hashes_raw_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);(root/'raw.txt').write_text('evidence')
            archive_analysis(root,{}, {'run_id':'<script>alert(1)</script>', 'verdict':'PASS','analysis':{}},[])
            self.assertNotIn('<script>',(root/'report.html').read_text())
            manifest=json.loads((root/'manifest.json').read_text())
            self.assertEqual(len(manifest['files']['raw.txt']['sha256']),64)
