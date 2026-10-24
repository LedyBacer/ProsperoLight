# ps5-native-app-boilerplate / ProsperoLight - deterministic network/audio replay.
# Copyright (C) 2026 LedyBacer and contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Discrete-event PCM queue model, not an acoustic or PS5 hardware test.

Replicates the worker's initial queue bounds and continuous output-grain
behavior, holding the adaptive prebuffer at its initial target (the separate
C++ policy tests exercise adaptation and rollback). Video's
actual C++ arrival estimator/pacer is replayed in test_frame_pacing_policy.
"""
import random
import unittest
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[1] / 'src/moonlight_stream.cpp'
GRAIN = 512
PREBUFFER = 960
RING = 8640
PACKET = 480
OUTPUT_INTERVAL_US = GRAIN * 1_000_000 // 48_000


def run_pcm_replay(*, seed, duration_ms=25000, latency_ms=0, jitter_ms=0,
                   burst_ms=0, loss_every=0):
    """Packets arrive at 10ms intervals; the worker consumes 512/48k grains."""
    rng = random.Random(seed)
    incoming = []
    for index in range(duration_ms // 10):
        if loss_every and index % loss_every == 0:
            continue
        variation = rng.uniform(-jitter_ms, jitter_ms)
        # Bounded three-packet burst about every 1.5 seconds, not a real WAN trace.
        stall = burst_ms if index % 150 in (60, 61, 62) else 0
        arrival = round((index * 10 + latency_ms + variation + stall) * 1000)
        incoming.append((max(0, arrival), index))
    incoming.sort()
    ring = cursor = overruns = recoveries = underruns = completed = 0
    silence_grains = short_grains = output_calls = 0
    mean_ring_sum = observations = 0
    prebuffering = True
    wait_start = 0
    recovering = False
    wait_total = wait_max = startup_wait = 0
    for now in range(0, duration_ms * 1000, OUTPUT_INTERVAL_US):
        output_calls += 1  # AudioOut clock NEVER stops on network starvation.
        while cursor < len(incoming) and incoming[cursor][0] <= now:
            ring += PACKET
            if ring > RING:
                ring = RING  # Evict old PCM, never silently grow latency.
                overruns += 1
            cursor += 1
            mean_ring_sum += ring
            observations += 1
        required = PREBUFFER if prebuffering else GRAIN
        if prebuffering and ring < required:
            silence_grains += 1
            if wait_start is None:
                wait_start = now
        else:
            take = min(ring, GRAIN)
            if take < GRAIN:
                underruns += 1
                recovering = True
                prebuffering = True
                short_grains += 1 if take else 0
                silence_grains += 0 if take else 1
                if wait_start is None:
                    wait_start = now
            if take == GRAIN and wait_start is not None:
                waited = now - wait_start
                if not completed:
                    startup_wait += waited
                else:
                    wait_total += waited
                    wait_max = max(wait_max, waited)
                if recovering:
                    recoveries += 1
                    recovering = False
                wait_start = None
            ring -= take
            if take == GRAIN:
                prebuffering = False
            if take:
                completed += 1
            mean_ring_sum += ring
            observations += 1
        assert 0 <= ring <= RING
    return {'underruns': underruns, 'recoveries': recoveries, 'overruns': overruns,
            'max_ring_frames': RING, 'tail_frames': ring,
            'startup_wait_us': startup_wait, 'wait_total_us': wait_total,
            'wait_max_us': wait_max, 'completed': completed,
            'ring_mean_frames': mean_ring_sum // observations,
            'output_calls': output_calls, 'silence_grains': silence_grains,
            'short_grains': short_grains}


class AudioArrivalReplay(unittest.TestCase):
    def test_model_thresholds_match_ps5_worker(self):
        source = SOURCE.read_text(encoding='utf-8')
        for bound in ('#define AUDIO_GRAIN_FRAMES 512u',
                      '#define AUDIO_PREBUFFER_FRAMES 960u',
                      '#define AUDIO_RING_FRAMES 8640u'):
            self.assertIn(bound, source)

    def test_stable_lan_does_not_rebuffer_or_overflow(self):
        run = run_pcm_replay(seed=10, jitter_ms=0.5, latency_ms=1)
        self.assertEqual(run['underruns'], 0)
        self.assertEqual(run['recoveries'], 0)
        self.assertEqual(run['overruns'], 0)
        self.assertGreater(run['completed'], 2200)
        self.assertGreaterEqual(run['output_calls'], run['completed'])

    def test_wan_bursts_recover_without_unbounded_backlog(self):
        run = run_pcm_replay(seed=42, latency_ms=50, jitter_ms=20, burst_ms=120,
                             loss_every=97)
        self.assertGreater(run['underruns'], 0)
        self.assertGreater(run['recoveries'], 0)
        self.assertLessEqual(run['recoveries'], run['underruns'])
        self.assertLessEqual(run['tail_frames'], RING)
        self.assertLessEqual(run['ring_mean_frames'], RING)
        self.assertGreater(run['wait_total_us'], 0)
        self.assertLess(run['wait_max_us'], 400000)
        self.assertGreater(run['silence_grains'], 0)
        self.assertEqual(run['output_calls'], run['completed'] + run['silence_grains'])

    def test_repeatability(self):
        conditions = dict(seed=921, latency_ms=20, jitter_ms=15, burst_ms=100,
                          loss_every=47)
        self.assertEqual(run_pcm_replay(**conditions), run_pcm_replay(**conditions))


if __name__ == '__main__':
    unittest.main()
