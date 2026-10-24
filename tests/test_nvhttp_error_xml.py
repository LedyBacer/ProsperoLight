# ps5-native-app-boilerplate - NVHTTP root status diagnostics regression tests.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class NvhttpStatusTests(unittest.TestCase):
    def test_status_message_from_root_is_bounded_and_decoded(self):
        code = r'''
#include "mini_xml.h"
#include "gs_errors.h"
#include <assert.h>
#include <string.h>

int main(void) {
    const char denied[] =
        "<root status_message='Permission denied: missing &quot;Launch applications&quot; "
        "permission &amp; retry' status_code='403'><resume>0</resume></root>";
    char text[160];
    assert(xml_status(denied, strlen(denied)) == 403);
    assert(xml_root_attribute(denied, strlen(denied), "status_message", text,
                              sizeof(text)) == GS_OK);
    assert(strcmp(text, "Permission denied: missing \"Launch applications\" "
                        "permission & retry") == 0);

    char short_text[12];
    assert(xml_root_attribute(denied, strlen(denied), "status_message", short_text,
                              sizeof(short_text)) == GS_OK);
    assert(short_text[sizeof(short_text) - 1] == '\0');
    assert(strlen(short_text) == sizeof(short_text) - 1);

    const char no_message[] = "<root status_code=\"503\"><status_message>fake</status_message></root>";
    assert(xml_status(no_message, strlen(no_message)) == 503);
    assert(xml_root_attribute(no_message, strlen(no_message), "status_message", text,
                              sizeof(text)) == GS_INVALID);
    assert(text[0] == '\0');

    const char self_close[] = "<root status_code='409' status_message='Busy'/>";
    assert(xml_root_attribute(self_close, strlen(self_close), "status_message", text,
                              sizeof(text)) == GS_OK);
    assert(strcmp(text, "Busy") == 0);

    const char malformed[] = "<root status_code='500'";
    assert(xml_root_attribute(malformed, strlen(malformed), "status_message", text,
                              sizeof(text)) == GS_INVALID);
    assert(xml_root_attribute(denied, strlen(denied), "status_message", text, 0) == GS_INVALID);
    return 0;
}
'''
        compiler = shutil.which("cc")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "nvhttp_status.c"
            binary = Path(directory) / "nvhttp_status"
            source.write_text(code)
            subprocess.run(
                [
                    compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(ROOT / "src/gamestream"),
                    str(source), str(ROOT / "src/gamestream/mini_xml.c"),
                    "-o", str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)
