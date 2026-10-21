# ps5-native-app-boilerplate - Whole-session delivery provenance and conservation.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

COUNTERS = ('video_packets', 'video_bytes', 'completed_frame_events', 'recovery_filtered_aus',
            'empty_aus', 'assembled_aus', 'offered_aus', 'queue_accepted_aus', 'dequeued_aus',
            'overflow_discarded_aus', 'refresh_discarded_aus', 'shutdown_discarded_aus',
            'interrupted_discarded_aus', 'allocation_discarded_aus')


def session_delivery(performance):
    value = performance.get('video_delivery') or {}
    result = dict(available=False, scope='whole-session', phase_comparable=False,
                  conservation_verified=False, reason='UPSTREAM_COUNTERS_UNAVAILABLE',
                  counters={key: None for key in COUNTERS},
                  provenance={
                      'video_packets': 'positive UDP reads on the video RTP socket, before decryption/FEC; includes parity and malformed datagrams',
                      'completed_frame_events': 'depacketizer end-of-frame after payload validation, before recovery filtering; not host production or all wire frame IDs',
                      'assembled_aus': 'nonempty codec AUs eligible after recovery filtering, before allocation/offer',
                      'offered_aus': 'complete allocated AUs offered to queue/direct submit',
                      'queue_discards': 'actual queued elements freed plus overflow/interrupted rejected AU; reasons counted separately',
                      'dequeued_aus': 'actual Wait/Poll removals or direct callbacks, before native validation/submission; Peek excluded',
                      'frame_id_gaps': 'diagnostic discontinuities; cause cannot be assigned from frame IDs alone',
                  })
    if value.get('available') is not True or value.get('version') != 1:
        return result
    if (any(type(value.get(k)) is not int or value[k] < 0 for k in COUNTERS) or
            type(value.get('direct_submit')) is not bool or
            type(value.get('start_us')) is not int or type(value.get('end_us')) is not int or
            value['start_us'] <= 0 or value['end_us'] < 0):
        result['reason'] = 'UPSTREAM_COUNTER_SCHEMA_INVALID'
        return result
    result.update(available=True, counters={k: value[k] for k in COUNTERS},
                  quiescent=value.get('quiescent') is True,
                  start_us=value.get('start_us'), end_us=value.get('end_us'))
    if not result['quiescent']:
        result['reason'] = 'LIVE_RELAXED_SNAPSHOT_NOT_TRANSACTIONAL'
        return result
    dropped = sum(value[k] for k in ('overflow_discarded_aus', 'refresh_discarded_aus',
                                     'shutdown_discarded_aus', 'interrupted_discarded_aus'))
    valid = (value['end_us'] >= value['start_us'] and
             value['queue_accepted_aus'] <= value['offered_aus'] and
             (value['direct_submit'] or value['dequeued_aus'] <= value['queue_accepted_aus']) and
             value['offered_aus'] == value['dequeued_aus'] + dropped and
             value['assembled_aus'] == value['offered_aus'] + value['allocation_discarded_aus'] and
             value['completed_frame_events'] == value['assembled_aus'] +
                value['recovery_filtered_aus'] + value['empty_aus'])
    result.update(conservation_verified=valid,
                  local_predecode_discards=dropped + value['allocation_discarded_aus'] + value['recovery_filtered_aus'],
                  reason=None if valid else 'COUNTER_CONSERVATION_FAILED')
    return result
