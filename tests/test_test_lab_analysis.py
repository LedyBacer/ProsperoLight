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


def run_fixture(fps=60, age=3000, ready=1500, variant='a'):
    value = {'state':'COMPLETED','run_id': str(uuid.uuid4()), 'profile': {'profile': {
        'source_sha': variant*40, 'build_label': 'test-lab-fixture-' + variant,
        'client': {'codec': 'hevc', 'fps': 60},
        'analysis_environment': dict(verified=True, firmware='x', host_version='v1',
            encoder_settings={'codec':'hevc'}, network_class='wired', build_flags={'TEST_LAB':1}, renderer='native')}},
        'report': {'execution_state':'COMPLETED','overall_verdict':'PASS','execution_mode':'real', 'ps5_measurement': {
            'installed_identity_verified':True,'title_exit_verified':True,
            'installed':{'build_label':'test-lab-fixture-' + variant,'eboot_sha256':variant*64},
            'build_variant':{'source_sha':variant*40,'build_label':'test-lab-fixture-' + variant,'eboot_sha256':variant*64}},
            'host_measurement': {
            'selected_display':{'id':'virtual', 'width':1920, 'height':1080,'refresh_hz':240},
            'scene':'low-motion','seed':137,'content_sha256':'a'*64,'pixel_sample_sha256':'b'*64},
            'analysis': {'functional_verdict':'PASS','measurement_validity':'VALID','trace':{'omitted':0},
                'metrics':{'unique_presented_fps':fps,'frame_age_us':{'p95':age},'callback_to_ready_us':{'p95':ready}}}}}
    value['report']['analysis']['environment'] = copy.deepcopy(value['profile']['profile']['analysis_environment'])
    value['report']['analysis']['session_settings'] = {'codec':{'value':'hevc','verified':True}}
    return value


class AnalysisTests(unittest.TestCase):
    def test_authenticated_counter_rates_keep_clock_bounds_and_capture_unknown(self):
        from pltest.session_evidence import host_transport_rates
        samples=[{'source':'Vibepollo /api/rtsp/sessions','verified':True,
                  'coordinator_start_us':begin,'coordinator_end_us':begin+100,
                  'session':{'uuid':'client','frames_sent':frames,'bytes_sent':frames*10000}}
                 for begin,frames in ((0,0),(1000000,60),(2000000,120))]
        result=host_transport_rates(samples)
        self.assertTrue(result['verified']);self.assertIsNone(result['capture_fps'])
        self.assertEqual(result['rates']['frames_sent']['per_second'],60)
        self.assertLess(result['rates']['frames_sent']['lower_per_second'],60)
        self.assertGreater(result['rates']['frames_sent']['upper_per_second'],60)
        samples[-1]['session']['frames_sent']=50
        self.assertEqual(host_transport_rates(samples)['reason'],'HOST_SENDER_COUNTER_RESET')
        samples[-1]['session']['frames_sent']=120;samples[-1]['session']['uuid']='other'
        self.assertEqual(host_transport_rates(samples)['reason'],'HOST_COUNTER_SESSION_CHANGED')
        samples[-1]['session']['uuid']='client';samples[-1]['coordinator_start_us']=1000000
        self.assertEqual(host_transport_rates(samples)['reason'],'HOST_COUNTER_CLOCK_BOUNDS_INVALID')
        samples[-1]['coordinator_start_us']=2000000;samples[-1]['session']['frames_sent']=None
        self.assertFalse(host_transport_rates(samples)['verified'])

    def test_environment_uses_observed_sources_and_explicit_topology(self):
        from pltest.environment import environment
        profile={'analysis_environment':{'verified':True,'firmware':'fabricated',
            'host_version':'fabricated','network_class':'ethernet','network_source':'operator-confirmed'}}
        report={'ps5_preflight':{'firmware_verified':True,'firmware_source':'kernel_get_fw_version','firmware_u32':123},
            'host_session_preflight':{'advertised':{'verified':True,'version':'2','commit':'a'*40}},
            'ps5_measurement':{'build_variant':{'build_flags':{'TEST_LAB':1,'PERFORMANCE_DETAIL':1}}},
            'host_measurement':{'capture_evidence':{'verified':True,'encoders':['hevc_nvenc']}}}
        analysis={'session_settings':{key:dict(verified=True,value=value) for key,value in
            dict(codec='hevc',hdr=False,chroma='420',host_encoder_bitrate_kbps=15000,host_requested_bitrate_kbps=20000).items()}}
        actual=environment(profile,report,analysis)
        self.assertTrue(actual['verified'])
        self.assertEqual(actual['firmware'],'0x7b')
        self.assertEqual(actual['host_version'],'2@'+'a'*40)
        self.assertFalse(environment(profile,{},analysis)['verified'])
        noisy = [(run_fixture(), run_fixture(age=value)) for value in (1000,3000,5000)]
        self.assertFalse(compare_pairs(noisy, calibration=True)['screening_usable'])
        pairs=self.pairs();pairs[0][1]['report']['analysis']['session_settings']['codec']['value']='h264'
        self.assertEqual(compare_pairs(pairs)['reason_code'],'ENVIRONMENT_MISMATCH')

    def test_pyrowave_effective_output_fallback_and_unavailable(self):
        from pltest.session_evidence import backend_effective_settings
        client = dict(fps=120, hdr=True, vsync=False, pacing='vrr')
        receipt = {'backend_output': dict(source='PyroWave.backend.initialize.success', verified=True,
            width=3840,height=2160,refresh_x100=12000,pacing=2,vsync=True,hdr=True)}
        self.assertEqual(backend_effective_settings(receipt,client)[1], [])
        receipt['backend_output']['refresh_x100'] = 11988
        self.assertEqual(backend_effective_settings(receipt,client)[1], [])
        receipt['backend_output']['refresh_x100'] = 11900
        self.assertIn('OUTPUT_MODE_FALLBACK',backend_effective_settings(receipt,client)[1])
        receipt['backend_output']['refresh_x100'] = 6000
        self.assertIn('OUTPUT_MODE_FALLBACK',backend_effective_settings(receipt,client)[1])
        receipt['backend_output']['hdr'] = False
        self.assertIn('EFFECTIVE_FIELD_MISMATCH:output_hdr',backend_effective_settings(receipt,client)[1])
        self.assertEqual(len(backend_effective_settings({},client)[1]), 6)

    def test_progress_detects_freeze_starvation_static_and_recovery(self):
        from pltest.progress import progress_diagnostics
        incoming = list(range(0, 5000000, 100000))
        complete = list(range(0, 2000000, 100000))
        result = progress_diagnostics(0, 5000000, incoming, complete, 'gpu-entropy')
        self.assertEqual(result['classification'], 'CLIENT_PRESENTATION_STALL')
        self.assertEqual(result['completed']['longest_no_progress_us'], 3100000)
        self.assertEqual(progress_diagnostics(0, 5000000, complete, complete, 'noise')['classification'],
                         'INPUT_STARVATION_OR_HOST_REDUCED_CAPTURE')
        self.assertEqual(progress_diagnostics(0, 5000000, incoming, complete, 'static-keepalive')['classification'],
                         'STATIC_CONTENT_OR_STALL_UNRESOLVED')
        recovered = complete + list(range(3500000, 5000000, 100000))
        self.assertEqual(progress_diagnostics(0, 5000000, incoming, recovered, 'noise')['completed']['stall_count'], 1)
        self.assertEqual(progress_diagnostics(0, 5000000, incoming, incoming, 'noise')['classification'], 'CONTINUOUS')
        early_starvation = list(range(1500000, 5000000, 100000))
        self.assertEqual(progress_diagnostics(0, 5000000, early_starvation, complete, 'noise')['classification'],
                         'CLIENT_PRESENTATION_STALL')

    def test_polled_completion_health_requires_bounded_sampling(self):
        from pltest.progress import polled_progress
        value = dict(source='confirmed unique completion counter polled on control worker',
            clock='PS5 monotonic', longest_no_progress_us=100000, stall_count=0,
            stall_threshold_us=1000000, poll_max_us=100000, samples=300, gap_log2_bins=[0]*64)
        value['gap_log2_bins'][16] = 299
        result = polled_progress({'continuous_progress':value}, 'gpu-entropy')
        self.assertTrue(result['verified']); self.assertEqual(result['classification'], 'CONTINUOUS')
        self.assertEqual(result['gap_distribution'][0]['lower_us'], 65536)
        value.update(stall_count=1, longest_no_progress_us=2000000)
        self.assertEqual(polled_progress({'continuous_progress':value}, 'gpu-entropy')['classification'],
                         'CLIENT_OR_INPUT_STALL_UNRESOLVED')
        self.assertEqual(polled_progress({'continuous_progress':value}, 'static-keepalive')['classification'],
                         'STATIC_CONTENT_OR_STALL_UNRESOLVED')
        value['poll_max_us'] = 1200000
        self.assertFalse(polled_progress({'continuous_progress':value}, 'noise')['verified'])
        self.assertFalse(polled_progress({}, 'noise')['verified'])

    def test_phase_ack_bounds_and_transition_exclusion(self):
        from pltest.phases import start_alignment
        from pltest.analysis import browser_phases
        report = {'run_id': 'run', 'host_measurement': {'start_command_id': 'browser'}, 'phase_anchors': {
            'browser_start': {'coordinator_start_us':1000,'coordinator_end_us':1100,
                'ack': {'browser_start': {'run_id':'run','command_id':'browser','browser_origin_ms':2}}},
            'ps5_start': {'coordinator_start_us':1200,'coordinator_end_us':1300,
                'ack': {'run_id':'run','command_id':'ps5','command_action':'start_measure',
                        'command_result':'accepted','monotonic_us':5000}}}}
        value = start_alignment(report)
        self.assertEqual(value['ps5_minus_browser_us'], [2700,2900])
        self.assertFalse(value['steady_state_comparable'])
        from pltest.phases import bounded_alignment, steady_windows
        report['phase_anchors'].update(
            browser_end_probe={'coordinator_start_us':10000,'coordinator_end_us':10100,
                'ack': {'command_id':'end-browser','browser_clock':{'run_id':'run','command_id':'end-browser','browser_now_ms':11}}},
            ps5_end_probe={'coordinator_start_us':10200,'coordinator_end_us':10300,
                'ack': {'run_id':'run','command_id':'end-ps5','command_action':'clock_probe',
                        'command_result':'accepted','monotonic_us':14000}})
        report['host_measurement'].update(measure_start_phase_id='run:measure-start', measure_end_phase_id='run:measure-end', reason='COMPLETE')
        report['ps5_measurement']={'receipt':{'measure_start_phase_id':'run:measure-start','measure_end_phase_id':'run:measure-end','measurement_complete':True}}
        value = bounded_alignment(report)
        self.assertTrue(value['clock_drift_verified']); self.assertEqual(value['endpoint_drift_bounds_us'],[-200,200])
        report['host_measurement'].update(measure_end_ms=12,
            phase_events=[{'run_id':'run','phase':'low-motion','phase_id':'run:low-motion','browser_ms':2},
                          {'run_id':'run','phase':'high-entropy','phase_id':'run:high-entropy','browser_ms':6}])
        report['ps5_measurement']['receipt'].update(measure_start_monotonic_us=5000,measure_end_monotonic_us=15000)
        windows=steady_windows(report,value)
        self.assertEqual(windows[0]['start_ps5_us'],5000)
        self.assertEqual(windows[0]['end_ps5_us'],8700)
        self.assertEqual(windows[1]['start_ps5_us'],8900)
        report['phase_anchors']['browser_end_probe']['ack']['browser_clock']['command_id']='wrong'
        self.assertFalse(bounded_alignment(report)['steady_state_comparable'])

        report['phase_anchors']['ps5_start']['ack']['command_result'] = 'rejected'
        self.assertFalse(start_alignment(report)['verified'])
        host = {'measure_start_ms':100,'measure_end_ms':160,'intervals_ms':[10]*6,
                'phase_events':[{'phase':'warmup','browser_ms':0,'phase_id':'run:warmup'},
                                {'phase':'low-motion','browser_ms':100,'phase_id':'run:low'},
                                {'phase':'high-entropy','browser_ms':125,'phase_id':'run:high'}]}
        phases = browser_phases(host)
        self.assertEqual([p['interval_ms']['count'] for p in phases], [2,2])
        self.assertEqual(phases[0]['excluded_transition_intervals'], 2)

    def test_negotiation_requires_callback_identity_and_every_material_field(self):
        from pltest.session_evidence import validate_negotiation
        client = dict(codec='hevc', hdr=False, chroma='420', resolution='1080p', fps=60, bitrate_mbps=20)
        samples = [{'source':'Vibepollo /api/rtsp/sessions','verified':True,'session':dict(width=1920,height=1080,fps=60,video_format=1,hdr=False,yuv444=False,encoder_bitrate_kbps=20000,requested_bitrate_kbps=20000)}]
        receipt = {'negotiated': {'codec': 'hevc'}}
        fields, issues = validate_negotiation(client, receipt, samples)
        self.assertEqual(len([issue for issue in issues if issue.startswith('NEGOTIATED_FIELD_UNVERIFIED')]), 7)
        self.assertFalse(fields['codec']['verified'])
        receipt['negotiated'] = dict(source='Limelight.renderer.setup.success', verified=True,
                                    video_format=256, width=1920, height=1080, fps=60)
        self.assertEqual(validate_negotiation(client, receipt, samples)[1], [])
        receipt['negotiated']['fps'] = 120
        self.assertIn('HOST_CLIENT_SETTINGS_MISMATCH:fps', validate_negotiation(client, receipt, samples)[1])
        receipt['negotiated']['fps'] = True
        self.assertIn('NEGOTIATED_FIELD_UNVERIFIED:fps', validate_negotiation(client, receipt, samples)[1])
        client.update(codec='pyrowave', hdr=True, chroma='444')
        receipt['negotiated'].update(video_format=0x80000, fps=60)
        samples[0]['session'].update(video_format=3, hdr=True, yuv444=True)
        self.assertEqual(validate_negotiation(client, receipt, samples)[1], [])
        receipt['negotiated']['verified'] = False
        self.assertEqual(len([issue for issue in validate_negotiation(client, receipt, samples)[1] if issue.startswith('NEGOTIATED_FIELD_UNVERIFIED')]), 7)

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
        return [(run_fixture(),run_fixture(fps,age,variant='b')) for _ in range(3)]

    def test_binary_identity_calibration_and_thresholds(self):
        pairs = [(run_fixture(),run_fixture()) for _ in range(3)]
        self.assertEqual(compare_pairs(pairs)['reason_code'],'DISTINCT_BUILDS_REQUIRED')
        self.assertTrue(compare_pairs(pairs,calibration=True)['screening_usable'])
        pairs = [(run_fixture(),run_fixture(55)) for _ in range(3)]
        self.assertFalse(compare_pairs(pairs,calibration=True)['screening_usable'])
        self.assertEqual(compare_pairs(self.pairs(),screening={'fps_drop_fraction':float('nan')})['reason_code'],
                         'SCREENING_CONFIGURATION_INVALID')
        noisy = [(run_fixture(), run_fixture(age=value)) for value in (1000,3000,5000)]
        self.assertFalse(compare_pairs(noisy, calibration=True)['screening_usable'])
        pairs=self.pairs();pairs[0][1]['report']['ps5_measurement']['installed']['eboot_sha256']='c'*64
        self.assertEqual(compare_pairs(pairs)['reason_code'],'BUILD_IDENTITY_UNVERIFIED')

    def test_three_pairs_regression_and_improvement_screening(self):
        self.assertEqual(compare_pairs(self.pairs(57))['performance_delta'],'REGRESSED')
        self.assertEqual(compare_pairs(self.pairs(63))['performance_delta'],'IMPROVED')
        self.assertEqual(compare_pairs(self.pairs(60,4500))['verdict'],'FAIL')
        self.assertEqual(compare_pairs(self.pairs(60,3300))['performance_delta'],'NO_CLEAR_CHANGE')
        mixed_noise = [(run_fixture(),run_fixture(57,age,variant='b')) for age in (1000,3000,5000)]
        result = compare_pairs(mixed_noise)
        self.assertEqual(result['performance_delta'],'REGRESSED')
        self.assertEqual(result['metrics']['frame_age_us']['performance_delta'],'NO_CLEAR_CHANGE')

    def test_independent_metric_thresholds_and_calibration_noise(self):
        from pltest.coordinator.models import CompareRequest
        overrides = {'metric_thresholds': {'frame_age_us': {'absolute': 2000}}}
        ids = [run_fixture()['run_id'],run_fixture()['run_id']]
        self.assertEqual(CompareRequest(pairs=[ids], screening=overrides).screening, overrides)
        result = compare_pairs(self.pairs(60,4500), screening=overrides)
        self.assertEqual(result['performance_delta'], 'NO_CLEAR_CHANGE')
        self.assertEqual(result['metrics']['frame_age_us']['threshold'], {'fraction': .1, 'absolute': 2000})
        self.assertEqual(compare_pairs(self.pairs(57,4500), screening=overrides)['performance_delta'], 'REGRESSED')
        noisy = [(run_fixture(),run_fixture(age=4500)) for _ in range(3)]
        result = compare_pairs(noisy, calibration=True)
        self.assertTrue(result['screening_usable_by_metric']['unique_presented_fps'])
        self.assertFalse(result['screening_usable_by_metric']['frame_age_us'])
        self.assertTrue(compare_pairs(noisy, calibration=True, screening=overrides)['screening_usable'])
        for invalid in ({'unknown': {'fraction': .1}}, {'frame_age_us': {'absolute': float('inf')}},
                        {'frame_age_us': {'fraction': 0}}, {'frame_age_us': {'absolute': -1}},
                        {'frame_age_us': {'ignored': 1}}):
            self.assertEqual(compare_pairs(self.pairs(), screening={'metric_thresholds': invalid})['reason_code'],
                             'SCREENING_CONFIGURATION_INVALID')
        for invalid in (False, [], ''):
            self.assertEqual(compare_pairs(self.pairs(),screening=invalid)['reason_code'],'SCREENING_CONFIGURATION_INVALID')

    def test_calibration_reference_blocks_noisy_or_incompatible_comparison(self):
        pairs=self.pairs(57)
        clean=[(run_fixture(),run_fixture()) for _ in range(3)]
        result=compare_pairs(pairs,calibration_pairs=clean)
        self.assertEqual(result['calibration_status'],'VERIFIED')
        self.assertEqual(result['performance_delta'],'REGRESSED')
        noisy=[(run_fixture(),run_fixture(age=5000)) for _ in range(3)]
        result=compare_pairs(pairs,calibration_pairs=noisy)
        self.assertEqual(result['reason_code'],'CALIBRATION_NOISE_EXCEEDS_THRESHOLDS')
        self.assertEqual(result['overall_verdict'],'INCONCLUSIVE')
        self.assertEqual(result['performance_delta'],'NOT_COMPARABLE')
        other_build=[(run_fixture(variant='c'),run_fixture(variant='c')) for _ in range(3)]
        self.assertEqual(compare_pairs(pairs,calibration_pairs=other_build)['reason_code'],'CALIBRATION_BUILD_MISMATCH')
        clean[0][0]['report']['host_measurement']['seed']=138
        self.assertEqual(compare_pairs(pairs,calibration_pairs=clean)['reason_code'],'CALIBRATION_REFERENCE_INVALID')
        clean=[(run_fixture(),run_fixture()) for _ in range(3)]
        clean[0]=(pairs[0][0],run_fixture())
        self.assertEqual(compare_pairs(pairs,calibration_pairs=clean)['reason_code'],'CALIBRATION_RUNS_NOT_INDEPENDENT')

    def test_missing_environment_mismatched_scene_and_invalid_measurement(self):
        noisy = [(run_fixture(), run_fixture(age=value)) for value in (1000,3000,5000)]
        self.assertFalse(compare_pairs(noisy, calibration=True)['screening_usable'])
        pairs=self.pairs();pairs[0][1]['report']['analysis']['environment'].pop('firmware')
        self.assertEqual(compare_pairs(pairs)['reason_code'],'FINGERPRINT_INCOMPLETE')
        pairs=self.pairs();pairs[1][1]['report']['host_measurement']['seed']=138
        self.assertEqual(compare_pairs(pairs)['reason_code'],'ENVIRONMENT_MISMATCH')
        pairs=self.pairs();pairs[2][1]['report']['analysis']['measurement_validity']='INCONCLUSIVE'
        self.assertEqual(compare_pairs(pairs)['reason_code'],'INVALID_MEASUREMENT')
        pairs=self.pairs()
        for pair in pairs:
            for run in pair:run['report']['host_measurement']['worker_runtime_identity']={'files':{'worker.py':'a'*64}}
        self.assertEqual(compare_pairs(pairs)['measurement_validity'],'VALID')
        pairs[1][1]['report']['host_measurement']['worker_runtime_identity']['files']['worker.py']='b'*64
        self.assertEqual(compare_pairs(pairs)['reason_code'],'ENVIRONMENT_MISMATCH')

    def test_fps_regression_dominates_latency_improvement(self):
        result=compare_pairs(self.pairs(57,1000))
        self.assertEqual(result['performance_delta'],'REGRESSED')
        self.assertEqual(result['overall_verdict'],'FAIL')
        self.assertEqual(result['metrics']['frame_age_us']['performance_delta'],'IMPROVED')
        result=compare_pairs(self.pairs(63,1000))
        self.assertEqual(result['performance_delta'],'IMPROVED')
        self.assertEqual(result['overall_verdict'],'PASS')

    def test_terminal_failure_cannot_reuse_valid_measurement(self):
        for state in ('CANCELLED','FAILED','INCONCLUSIVE','PREFLIGHT'):
            pairs=self.pairs();pairs[0][1]['state']=state
            self.assertEqual(compare_pairs(pairs)['reason_code'],'RUN_EXECUTION_NOT_COMPLETED')
        pairs=self.pairs();pairs[0][1]['report']['cleanup_errors']=['TITLE_EXIT_UNVERIFIED']
        self.assertEqual(compare_pairs(pairs)['reason_code'],'RUN_CLEANUP_FAILED')
        pairs=self.pairs();pairs[0][1]['report']['overall_verdict']='INCONCLUSIVE'
        self.assertEqual(compare_pairs(pairs)['reason_code'],'RUN_OVERALL_NOT_PASS')

        pairs=self.pairs();pairs[0][1]['report']['ps5_measurement']['title_exit_verified']=False
        self.assertEqual(compare_pairs(pairs)['reason_code'],'RUN_HARDWARE_STATE_UNCERTAIN')

    def test_duplicate_pairs_and_insufficient_samples(self):
        pairs=self.pairs()
        pairs[1][1]['report']['analysis']['environment']['host_tls_peer_sha256']='a'*64
        self.assertEqual(compare_pairs(pairs)['reason_code'],'ENVIRONMENT_MISMATCH')
        pairs=self.pairs()
        pairs[1][1]['report']['host_measurement']['gpu_samples']=[{'selected_gpu_uuid':'GPU-other','gpu_inventory':[]}]
        self.assertEqual(compare_pairs(pairs)['reason_code'],'ENVIRONMENT_MISMATCH')
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
        report = {'execution_mode':'real','verdict':'PASS','host_measurement':{'measurement_validity':'VALID',
            'selected_display':{'id':'virtual'},'capture_evidence':{'verified':True,'output':'virtual'},
            'session_samples':[{'source':'Vibepollo /api/rtsp/sessions','verified':True,'session':dict(width=3840,height=2160,fps=120,video_format=1,hdr=False,yuv444=False,encoder_bitrate_kbps=80000,requested_bitrate_kbps=80000)}]},
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
