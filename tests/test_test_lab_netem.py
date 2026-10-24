# ps5-native-app-boilerplate - safe netem harness regression checks.
# Copyright (C) 2026 LedyBacer and contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""No root privilege, network access, or physical PS5 required."""
import importlib
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

PACKAGE = Path(__file__).resolve().parents[1] / 'tools/test-lab/src'
sys.path.insert(0, str(PACKAGE))
netem = importlib.import_module('pltest.netem')


class NetemTest(unittest.TestCase):
    def test_egress_scenarios_have_bounded_impairment(self):
        self.assertEqual(netem.tc_args('lan'), [])
        for name, value in netem.SCENARIOS.items():
            if name == 'lan':
                continue
            self.assertLessEqual(value['delay_ms'] + value['jitter_ms'] * 3, 150)
            self.assertLessEqual(value['loss_percent'], 2)
            self.assertIn('delay', netem.tc_args(name))

    def test_ownership_cleanup_and_evidence(self):
        state = {'kind': 'fq_codel', 'handle': '0:'}
        commands = []

        def execute(args, **kwargs):
            commands.append(args)
            if args[:4] == ['ip', '-j', 'route', 'show']:
                return type('Result', (), {'stdout': '[{"dev":"wan0"}]'})()
            if args[:4] == ['tc', '-j', 'qdisc', 'show'] or args[:5] == [
                    'tc', '-s', '-j', 'qdisc', 'show']:
                return type('Result', (), {'stdout': json.dumps([dict(state, root=True)])})()
            if args[:3] == ['tc', 'qdisc', 'add']:
                state.update(kind='netem', handle='51:')
            if args[:3] == ['tc', 'qdisc', 'del']:
                state.update(kind='fq_codel', handle='0:')
            return type('Result', (), {'stdout': ''})()

        class Child:
            def __enter__(self): return self
            def __exit__(self, *_args): return None
            def wait(self): return 1

        with tempfile.TemporaryDirectory() as folder, patch.object(netem.platform, 'system', return_value='Linux'), \
                patch.object(netem, 'execute', side_effect=execute), \
                patch.object(netem.subprocess, 'Popen', return_value=Child()):
            evidence = Path(folder) / 'wifi.json'
            result = netem.run('test0', 'wifi-congested', ['pltest', 'run'], evidence)
            self.assertEqual(result, 1)  # Preserve the test's actual exit status.
            record = json.loads(evidence.read_text())
            self.assertTrue(record['applied'])
            self.assertTrue(record['cleanup_verified'])
            self.assertEqual(record['command_exit_code'], 1)
            self.assertEqual(record['one_way_egress']['jitter_ms'], 15)
            self.assertEqual(sum(x[:3] == ['tc', 'qdisc', 'del'] for x in commands), 1)

    def test_rejects_default_route_and_foreign_qdisc(self):
        with patch.object(netem, 'execute') as execute:
            execute.return_value.stdout = '[{"dev":"test0"}]'
            with self.assertRaisesRegex(ValueError, 'default-route'):
                netem.validate_interface('test0')
            with self.assertRaisesRegex(ValueError, 'unsafe'):
                netem.validate_interface('../eth0')
            execute.side_effect = [type('Result', (), {'stdout': '[]'})(),
                                   type('Result', (), {'stdout': '[{"root":true,"kind":"htb"}]'})()]
            with self.assertRaisesRegex(ValueError, 'nondefault'):
                netem.validate_interface('test0')

    def test_interrupt_still_removes_owned_qdisc(self):
        kind = ['fq_codel']
        operations = []

        def execute(args, **_kwargs):
            operations.append(args)
            if args[0] == 'ip':
                value = '[]'
            elif args[1:3] == ['-j', 'qdisc'] or args[1:4] == ['-s', '-j', 'qdisc']:
                value = json.dumps([{'root': True, 'kind': kind[0],
                                     'handle': '51:' if kind[0] == 'netem' else '0:'}])
            else:
                if 'add' in args: kind[0] = 'netem'
                if 'del' in args: kind[0] = 'fq_codel'
                value = ''
            return type('Result', (), {'stdout': value})()

        class InterruptedChild:
            waits = 0
            def __enter__(self): return self
            def __exit__(self, *_args): return None
            def wait(self, **_kwargs):
                self.waits += 1
                if self.waits == 1: raise KeyboardInterrupt()
                return 130
            def poll(self): return None
            def terminate(self): pass

        with tempfile.TemporaryDirectory() as folder, patch.object(netem.platform, 'system', return_value='Linux'), \
                patch.object(netem, 'execute', side_effect=execute), \
                patch.object(netem.subprocess, 'Popen', return_value=InterruptedChild()):
            evidence = Path(folder) / 'interrupted.json'
            with self.assertRaises(KeyboardInterrupt):
                netem.run('test0', 'wan-50', ['pltest', 'run'], evidence)
            self.assertTrue(json.loads(evidence.read_text())['cleanup_verified'])
            self.assertEqual(kind[0], 'fq_codel')
            self.assertEqual(sum('del' in x for x in operations), 1)

    def test_two_directions_with_independent_conditions_and_stats(self):
        states = {'toward-ps5': 'fq_codel', 'toward-host': 'fq_codel'}
        operations = []

        def execute(args, **_kwargs):
            operations.append(args)
            if args[0] == 'ip':
                return type('Result', (), {'stdout': '[{"dev":"wan0"}]'})()
            if 'show' in args:
                dev = args[-1]
                kind = states[dev]
                return type('Result', (), {'stdout': json.dumps([{
                    'root': True, 'kind': kind, 'handle': '51:' if kind == 'netem' else '0:',
                    'bytes': 12345 if '-s' in args else 0}])})()
            dev = args[args.index('dev') + 1]
            states[dev] = 'netem' if 'add' in args else 'fq_codel'
            return type('Result', (), {'stdout': ''})()

        class Child:
            def __enter__(self): return self
            def __exit__(self, *_args): return None
            def wait(self): return 0

        with tempfile.TemporaryDirectory() as folder, \
                patch.object(netem.platform, 'system', return_value='Linux'), \
                patch.object(netem, 'execute', side_effect=execute), \
                patch.object(netem.subprocess, 'Popen', return_value=Child()):
            evidence = Path(folder) / 'paired.json'
            self.assertEqual(netem.run('toward-ps5', 'wan-50', ['pltest'], evidence,
                                       return_interface='toward-host',
                                       return_scenario='wifi-light'), 0)
            record = json.loads(evidence.read_text())
            self.assertTrue(record['cleanup_verified'])
            self.assertEqual(len(record['directions']), 2)
            self.assertEqual([x['direction'] for x in record['directions']],
                             ['host_to_ps5', 'ps5_to_host'])
            self.assertEqual(record['directions'][0]['qdisc_stats_before_cleanup'][0]['bytes'],
                             12345)
            self.assertEqual(record['directions'][1]['scenario'], 'wifi-light')
            self.assertEqual([x[4] for x in operations if x[:3] == ['tc', 'qdisc', 'del']],
                             ['toward-host', 'toward-ps5'])
            self.assertEqual(states, {'toward-ps5': 'fq_codel', 'toward-host': 'fq_codel'})

    def test_second_direction_install_failure_rolls_back_first(self):
        states = {'ps5-egress': 'fq', 'host-egress': 'fq'}
        operations = []

        def execute(args, **_kwargs):
            operations.append(args)
            if args[0] == 'ip':
                value = '[]'
            elif 'show' in args:
                dev = args[-1]
                value = json.dumps([{'root': True, 'kind': states[dev],
                                     'handle': '51:' if states[dev] == 'netem' else '0:'}])
            else:
                dev = args[args.index('dev') + 1]
                if dev == 'host-egress' and 'add' in args:
                    raise subprocess.CalledProcessError(2, args)
                states[dev] = 'netem' if 'add' in args else 'fq'
                value = ''
            return type('Result', (), {'stdout': value})()

        import subprocess
        with tempfile.TemporaryDirectory() as folder, \
                patch.object(netem.platform, 'system', return_value='Linux'), \
                patch.object(netem, 'execute', side_effect=execute):
            evidence = Path(folder) / 'rollback.json'
            with self.assertRaises(subprocess.CalledProcessError):
                netem.run('ps5-egress', 'wan-100', ['pltest'], evidence,
                          return_interface='host-egress', return_scenario='wan-burst')
            record = json.loads(evidence.read_text())
            self.assertFalse(record['applied'])
            self.assertTrue(record['cleanup_verified'])
            self.assertEqual(states, {'ps5-egress': 'fq', 'host-egress': 'fq'})
            self.assertEqual(sum('del' in x for x in operations), 1)

    def test_stats_capture_failure_does_not_leave_netem_installed(self):
        state = ['fq']

        def execute(args, **_kwargs):
            if args[0] == 'ip':
                return type('Result', (), {'stdout': '[]'})()
            if '-s' in args:
                raise OSError('tc stats unavailable')
            if 'show' in args:
                return type('Result', (), {'stdout': json.dumps([{
                    'root': True, 'kind': state[0],
                    'handle': '51:' if state[0] == 'netem' else '0:'}])})()
            state[0] = 'netem' if 'add' in args else 'fq'
            return type('Result', (), {'stdout': ''})()

        class Child:
            def __enter__(self): return self
            def __exit__(self, *_args): return None
            def wait(self): return 0

        with tempfile.TemporaryDirectory() as folder, \
                patch.object(netem.platform, 'system', return_value='Linux'), \
                patch.object(netem, 'execute', side_effect=execute), \
                patch.object(netem.subprocess, 'Popen', return_value=Child()):
            evidence = Path(folder) / 'stats-fail.json'
            self.assertEqual(netem.run('test0', 'wifi-light', ['pltest'], evidence), 0)
            self.assertEqual(state[0], 'fq')
            record = json.loads(evidence.read_text())
            self.assertTrue(record['cleanup_verified'])
            self.assertIn('tc stats unavailable', record['directions'][0]['stats_error'])

    def test_no_partial_mutation_if_return_interface_unsafe(self):
        with patch.object(netem.platform, 'system', return_value='Linux'), \
                patch.object(netem, 'execute') as execute:
            execute.side_effect = [type('Result', (), {'stdout': '[]'})(),
                                   type('Result', (), {'stdout': '[{"root":true,"kind":"fq"}]'})(),
                                   type('Result', (), {'stdout': '[{"dev":"host0"}]'})()]
            with self.assertRaisesRegex(ValueError, 'default-route'):
                netem.run('ps5-egress', 'wan-50', ['pltest'],
                          return_interface='host0', return_scenario='wan-100')
            self.assertFalse(any('add' in x for x in execute.call_args_list[0][0]))
            with self.assertRaisesRegex(ValueError, 'separate'):
                netem.run('ps5-egress', 'wan-50', ['pltest'],
                          return_interface='ps5-egress', return_scenario='wan-50')


if __name__ == '__main__':
    unittest.main()
