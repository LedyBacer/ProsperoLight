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
            if args[:4] == ['tc', '-j', 'qdisc', 'show']:
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
            elif args[1:3] == ['-j', 'qdisc']:
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


if __name__ == '__main__':
    unittest.main()
