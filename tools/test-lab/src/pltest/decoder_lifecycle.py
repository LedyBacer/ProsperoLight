# ps5-native-app-boilerplate - decoder lifecycle evidence.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

def decoder_lifecycle(performance):
    """PS5-local software clocks; never RTSP or physical scanout evidence."""
    if 'decoder_generation' not in performance:
        return {'available': False, 'reason': 'legacy build has no lifecycle evidence'}
    integer_fields = (
        'decoder_generation', 'decoder_initialization_us', 'decoder_reconfiguration_count',
        'decoder_reconfiguration_us', 'decoder_recovery_attempts', 'decoder_recovery_successes',
        'decoder_recovery_failures', 'decoder_ready_us', 'decoder_first_decoded_us',
        'decoder_first_presented_us', 'stream_coded_width', 'stream_coded_height',
        'stream_required_dpb_frames', 'max_dpb_frames', 'decoder_cpu_memory_bytes',
        'decoder_gpu_memory_bytes', 'decoder_shared_memory_bytes', 'decoder_frame_pool_bytes')
    values = {key: performance.get(key) for key in integer_fields}
    if any(type(value) is not int or value < 0 for value in values.values()):
        return {'available': True, 'valid': False, 'reason': 'invalid or missing lifecycle counters'}
    injected = performance.get('decoder_test_capacity_errors', 0)
    if type(injected) is not int or injected < 0:
        return {'available': True, 'valid': False, 'reason': 'invalid developer probe counter'}
    values['decoder_test_capacity_errors'] = injected
    for key in ('decoder_test_capacity_decoded_before', 'decoder_test_capacity_presented_before',
                'decoder_parameter_transition_waits'):
        if key in performance:
            if type(performance[key]) is not int or performance[key] < 0:
                return {'available': True, 'valid': False, 'reason': 'invalid transition/probe counter'}
            values[key] = performance[key]

    ready, decoded, presented = (values[key] for key in (
        'decoder_ready_us', 'decoder_first_decoded_us', 'decoder_first_presented_us'))
    valid = bool(performance.get('stream_parameters_verified') == 1 and
                 values['decoder_generation'] > 0 and ready > 0 and ready <= decoded <= presented)
    return dict(available=True, valid=valid, state=performance.get('decoder_lifecycle_state'),
        decoder_initialized=bool(ready), first_decoded=bool(decoded), first_presented=bool(presented),
        ready_to_first_decode_us=decoded - ready if decoded >= ready > 0 else None,
        decode_to_first_present_us=presented - decoded if presented >= decoded > 0 else None,
        reason=None if valid else 'initialization/first decoded/first presented evidence incomplete',
        counters=values, clock='PS5 monotonic software timestamps; not physical scanout')
