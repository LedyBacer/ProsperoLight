# ps5-native-app-boilerplate / ProsperoLight - isolated qdisc qualification tests.
# Copyright (C) 2026 LedyBacer and contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""No root, network namespace or PS5 required for these safety checks."""

import importlib.util
import json
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "tools/test-lab/netem_calibrate.py"


class NetemCalibrationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location("netem_calibrate", SOURCE)
        cls.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.module)

    def test_independent_loss_profile_preserves_historical_profile(self):
        new=self.module.netem.SCENARIOS['wifi-loss-independent']
        old=self.module.netem.SCENARIOS['wifi-congested']
        self.assertEqual(old['loss_correlation_percent'],20)
        self.assertNotIn('loss_correlation_percent',new)
        self.assertEqual(new['loss_percent'],1.0)
        self.assertEqual(new['delay_ms'],old['delay_ms'])
        self.assertEqual(new['jitter_ms'],old['jitter_ms'])
        self.assertEqual(self.module.netem.tc_args('wifi-loss-independent'),
            ['delay','20ms','15ms','distribution','normal','loss','1.0%',
             'reorder','0.2%'])

    def test_refuses_non_impairment_and_tiny_sample(self):
        with self.assertRaises(ValueError):
            self.module.calibrate("lan", 12000)
        with self.assertRaises(ValueError):
            self.module.calibrate("wifi-congested", 10)

    def test_isolated_namespace_creation_and_cleanup(self):
        calls = []

        def command(args):
            calls.append(args)
            if "--worker" in args:
                return SimpleNamespace(stdout=json.dumps({
                    "sent": 12000, "unique_received": 11880, "missing": 120}))
            if "qdisc" in args and "show" in args:
                drops = 120 if sum("show" in c for c in calls) == 2 else 0
                return SimpleNamespace(stdout=json.dumps([{
                    "root": True, "kind": "netem", "handle": "51:",
                    "drops": drops, "packets": 12000}]))
            return SimpleNamespace(stdout="")

        with patch.object(self.module, "command", side_effect=command), \
                patch.object(self.module.secrets, "token_hex", return_value="abc123"):
            result = self.module.calibrate("wifi-congested", 12000)

        self.assertTrue(result["qualified"])
        self.assertEqual(result["loss_counter_verdict"], "PLAUSIBLE")
        created = [args for args in calls if args[:3] == ["ip", "netns", "add"]]
        deleted = [args for args in calls if args[:3] == ["ip", "netns", "del"]]
        self.assertEqual(len(created), 1)
        self.assertEqual(deleted[0][-1], created[0][-1])
        self.assertTrue(all("route" not in args for args in calls))
        self.assertTrue(all("ps5" not in " ".join(args) for args in calls))
        self.assertEqual(result["scope"], "isolated_loopback_only")


if __name__ == "__main__":
    unittest.main()
