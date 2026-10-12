# ps5-native-app-boilerplate - control mailbox identity, ACK and overwrite safety.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
import json
from pathlib import Path
import sys
import unittest
from ftplib import error_perm
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/test-lab'))
try:
    spec = importlib.util.spec_from_file_location('control_run', ROOT / 'tools/test-lab/control_run.py')
    control = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(control)
finally:
    sys.path.pop(0)

class ControlTests(unittest.TestCase):
    def test_matching_ack_and_refusal_of_terminal_or_outstanding_run(self):
        class Fake:
            def __init__(self):
                self.status = {'schema': 'prosperolight-test-status/1', 'run_id': 'run', 'state': 'connecting'}
                self.terminal = False; self.outstanding = False; self.uploads = 0; self.data = b''
            def mkd(self, path): pass
            def storbinary(self, command, stream): self.uploads += 1; self.data = stream.read()
            def rename(self, source, destination):
                payload = json.loads(self.data)
                self.status.update(command_id=payload['command_id'], command_action=payload['action'], command_result='accepted')
            def retrbinary(self, command, append):
                if '/status/' in command: data = self.status
                elif '/receipts/' in command:
                    if not self.terminal: raise error_perm('550 Not found')
                    data = {'schema': 'prosperolight-test-receipt/1'}
                elif command.endswith('.tmp'): data = json.loads(self.data)
                elif self.outstanding: data = {'command_id': 'unacknowledged'}
                else: raise error_perm('550 Not found')
                append(json.dumps(data).encode())
        ftp = Fake()
        self.assertEqual(control.command(ftp, 'run', 'cancel')['command_result'], 'accepted')
        self.assertEqual(ftp.uploads, 1)
        for terminal, outstanding in ((True, False), (False, True)):
            ftp = Fake(); ftp.terminal = terminal; ftp.outstanding = outstanding
            with self.assertRaises(RuntimeError): control.command(ftp, 'run', 'stop')
            self.assertEqual(ftp.uploads, 0)
        ftp = Fake(); ftp.status['run_id'] = 'other'
        with self.assertRaises(ValueError): control.command(ftp, 'run', 'stop')
        self.assertEqual(ftp.uploads, 0)
