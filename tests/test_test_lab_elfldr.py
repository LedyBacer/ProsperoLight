# ps5-native-app-boilerplate - Payload Manager method negotiation safety.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import sys
from pathlib import Path
import unittest
from unittest.mock import MagicMock, patch
from urllib.error import HTTPError

sys.path[:0] = [str(Path(__file__).resolve().parents[1] / 'tools/test-lab/src'),
                str(Path(__file__).resolve().parents[1] / 'tools/test-lab')]
from pltest.ps5.elfldr import ElfLoader


class LoaderTests(unittest.TestCase):
    def execute(self, replies):
        response = MagicMock(status=200)
        with patch('pltest.ps5.elfldr.elf_bytes', return_value=b'ELF'), \
             patch('pltest.ps5.elfldr.upload'), \
             patch('pltest.ps5.elfldr.urlopen', side_effect=replies + [response]) as request:
            ElfLoader('console.invalid', manager_port=8084).execute(MagicMock(), 'helper.elf')
        return request

    def test_legacy_get_is_not_repeated(self):
        self.assertEqual(self.execute([]).call_count, 1)

    def test_explicit_method_rejection_negotiates_post(self):
        calls = self.execute([HTTPError('url', 405, 'method', {}, None)])
        self.assertEqual(calls.call_count, 2)
        request = calls.call_args.args[0]
        self.assertEqual(request.get_method(), 'POST')
        self.assertEqual(request.get_header('X-pldmgr-request'), '1')

    def test_no_retry_on_uncertain_or_other_failure(self):
        for failure in (TimeoutError(), HTTPError('url', 403, 'forbidden', {}, None),
                        HTTPError('url', 500, 'server', {}, None)):
            with self.subTest(failure=type(failure).__name__):
                with patch('pltest.ps5.elfldr.elf_bytes', return_value=b'ELF'), \
                     patch('pltest.ps5.elfldr.upload'), \
                     patch('pltest.ps5.elfldr.urlopen', side_effect=failure) as call:
                    with self.assertRaises(type(failure)):
                        ElfLoader('console.invalid', manager_port=8084).execute(MagicMock(), 'helper.elf')
                    self.assertEqual(call.call_count, 1)
