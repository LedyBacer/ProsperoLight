# ps5-native-app-boilerplate - Title metadata path and validation regression.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class TitleMetadataTests(unittest.TestCase):
    def test_explicit_paths_and_invalid_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = pathlib.Path(directory)
            source = folder / 'probe.c'
            source.write_text('#include "ps5_title_metadata.h"\n'
                              'int main(int argc, char **argv) {\n'
                              'return argc == 2 && ps5_title_declares_hfr(argv[1]) ? 0 : 1;\n}\n')
            binary = folder / 'probe'
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'include'),
                            str(source), '-o', str(binary)], check=True)
            # Actual install roots need not be /app0. No alternate installation is read.
            for name in ('homebrew/PPSA99002', 'sandbox/PPSA99002_000/app0'):
                metadata = folder / name / 'sce_sys/param.json'
                metadata.parent.mkdir(parents=True)
                for contents, expected in [('{"attribute3":262208}', 0),
                                           ('{"attribute3":64}', 0),
                                           ('{"attribute3":0}', 1),
                                           ('{}', 1), ('{"attribute3":"64"}', 1),
                                           ('{"attribute3":-64}', 1),
                                           ('{"attribute3":4294967360}', 1),
                                           ('{"attribute3":64garbage}', 1),
                                           ('x' * 17000, 1)]:
                    with self.subTest(path=name, contents=contents[:50]):
                        metadata.write_text(contents)
                        result = subprocess.run([str(binary), str(metadata)], capture_output=True)
                        self.assertEqual(result.returncode, expected)
            missing = subprocess.run([str(binary), str(folder / 'missing')], capture_output=True)
            self.assertEqual(missing.returncode, 1)
            self.assertIn(b'metadata unreadable', missing.stderr)
