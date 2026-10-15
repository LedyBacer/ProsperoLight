# ps5-native-app-boilerplate - Windows source waveform channel/level contract.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
import pathlib
import struct
import unittest


class WaveformSourceTests(unittest.TestCase):
    def test_only_actuator_channels_and_bounded_segments(self):
        path = pathlib.Path(__file__).resolve().parents[1] / 'tools/controllers/waveform_source.py'
        spec = importlib.util.spec_from_file_location('waveform_source', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        frames = list(struct.iter_unpack('<hhhh', module.pcm()))
        self.assertEqual(len(frames), 26880)
        for index, (speaker_l, speaker_r, left, right) in enumerate(frames):
            self.assertEqual((speaker_l, speaker_r), (0, 0))
            self.assertLessEqual(max(abs(left), abs(right)), 1000)
            if index < 7680:
                self.assertEqual(right, 0)
            elif index < 15360:
                self.assertEqual(left, 0)
            elif index < 23040:
                self.assertEqual(left, right)
            else:
                self.assertEqual((left, right), (0, 0))
        self.assertTrue(any(f[2] < 0 for f in frames))
        self.assertTrue(any(f[2] > 0 for f in frames))

    def test_explicit_stronger_preset_keeps_gaps_and_speaker_silent(self):
        path = pathlib.Path(__file__).resolve().parents[1] / 'tools/controllers/waveform_source.py'
        spec = importlib.util.spec_from_file_location('waveform_source', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        frames = list(struct.iter_unpack('<hhhh', module.pcm(True)))
        self.assertEqual(len(frames), 336000)
        for index, (speaker_l, speaker_r, left, right) in enumerate(frames):
            self.assertEqual((speaker_l, speaker_r), (0, 0))
            self.assertLessEqual(max(abs(left), abs(right)), 6000)
            if index < 72000:
                self.assertEqual(right, 0)
            elif index < 120000:
                self.assertEqual((left, right), (0, 0))
            elif index < 192000:
                self.assertEqual(left, 0)
            elif index < 240000:
                self.assertEqual((left, right), (0, 0))
            elif index < 312000:
                self.assertEqual(left, right)
            else:
                self.assertEqual((left, right), (0, 0))
