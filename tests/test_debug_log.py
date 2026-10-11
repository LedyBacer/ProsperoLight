# ps5-native-app-boilerplate - Unified diagnostics: timed lines, bounded storage, one setting.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

PROGRAM = r"""
#include "app_storage.hpp"
#include "lan_http_report.hpp"
#include <cassert>
#include <cstdio>
#include <string>
#include <unistd.h>
int main(int, char **argv)
{
    const std::string folder = argv[1];
    std::snprintf(storage::g_paths.config, sizeof(storage::g_paths.config), "%s/config/settings.bin",
                  folder.c_str());
    const std::string logs = folder + "/logs";
    // One switch silences both the detailed trace and structured reports.
    assert(prosperolight_logs_set_enabled(0) == 1);
    assert(prosperolight_debug_enabled() == 0);
    prosperolight_debug_start(logs.c_str());
    prosperolight_debug_line("launcher", "never written %d", 1);
    prosperolight_debug_flush();
    assert(argv[2][0] == 'a' || access((logs + "/debug-trace.txt").c_str(), F_OK) != 0);
    assert(prosperolight_debug_set_enabled(1) == 1 && prosperolight_debug_enabled() == 1);
    prosperolight_debug_line("launcher", "first frame after %d ms\n", 483);
    assert(prosperolight_logs_enabled() == 1);
    prosperolight_log_append("prosperolight-session.log", "Moonlight connection started");
    assert(lan_http_report_text("Stream profile: codec=1") == 0);
    prosperolight_debug_flush();
    if (argv[2][0] == 'f')
    {
        // More than the file may hold: the beginning is kept and the end says so.
        const std::string big(1000, 'x');
        for (int i = 0; i < 20000; ++i)
        {
            prosperolight_debug_line("fill", "%s", big.c_str());
            if (i % 200 == 0)
                prosperolight_debug_flush();
        }
        prosperolight_debug_flush();
        prosperolight_debug_line("fill", "after the limit");
        prosperolight_debug_flush();
    }
    assert(prosperolight_debug_set_enabled(0) == 1 && prosperolight_logs_enabled() == 0);
    prosperolight_log_append("prosperolight-session.log", "disabled stream report");
    prosperolight_debug_line("launcher", "after switching off");
    prosperolight_debug_flush();
    return 0;
}
"""


class DebugLogTests(unittest.TestCase):
    def build(self, folder):
        compiler = shutil.which("clang++") or shutil.which("g++")
        self.assertIsNotNone(compiler)
        (folder / "check.cpp").write_text(PROGRAM)
        subprocess.run([compiler, "-std=c++20", "-pthread", "-I" + str(ROOT / "include"),
                        str(folder / "check.cpp"), str(ROOT / "src/lan_http_report.cpp"),
                        "-o", str(folder / "check")], check=True)
        for name in ("config", "logs"):
            (folder / name).mkdir()

    def test_trace_is_timed_tagged_and_separate(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            self.build(folder)
            subprocess.run([str(folder / "check"), str(folder), "plain"], check=True)
            lines = (folder / "logs/debug-trace.txt").read_text().splitlines()
            self.assertRegex(lines[0], r"^\[ +\d+\.\d{3}\] \[trace\] ProsperoLight debug log, opened \d{4}-")
            for line in lines:
                self.assertRegex(line, r"^\[ +\d+\.\d{3}\] \[[a-z-]+\] ")
            text = "\n".join(lines)
            self.assertIn("[launcher] first frame after 483 ms", text)
            self.assertIn("[session] Moonlight connection started", text)
            self.assertIn("[session] Stream profile: codec=1", text)
            self.assertNotIn("never written", text)
            self.assertNotIn("after switching off", text)
            self.assertNotIn("disabled stream report", text)
            self.assertEqual(text.count("Moonlight connection started"), 1)
            # The setting survives a restart; a second run keeps the first as the previous trace.
            self.assertEqual((folder / "config/prosperolight-logging.bin").read_bytes(), b"PLL\x01\x00")
            subprocess.run([str(folder / "check"), str(folder), "again"], check=True)
            self.assertIn("first frame", (folder / "logs/debug-trace.prev.txt").read_text())

    def test_full_trace_keeps_its_beginning(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            self.build(folder)
            subprocess.run([str(folder / "check"), str(folder), "fill"], check=True)
            trace = folder / "logs/debug-trace.txt"
            self.assertLess(trace.stat().st_size, 17 * 1024 * 1024 + 600 * 1024)
            text = trace.read_text()
            self.assertIn("first frame after 483 ms", text)
            self.assertTrue(text.rstrip().endswith("the debug log is full; nothing more is written"))
            self.assertNotIn("after the limit", text)

    def test_the_trace_is_fed_where_problems_show(self):
        sources = {name: (ROOT / name).read_text() for name in (
            "src/moonlight_stream.cpp", "src/launcher/launcher_model.cpp",
            "src/launcher/launcher_view.cpp", "src/launcher/launcher_ps5.cpp",
            "src/app_storage.cpp", "src/main.cpp")}
        squeezed = {name: " ".join(text.split()) for name, text in sources.items()}
        self.assertIn('prosperolight_debug_line( "stream",', squeezed["src/moonlight_stream.cpp"])
        self.assertIn('prosperolight_debug_line("notice"', squeezed["src/launcher/launcher_model.cpp"])
        self.assertIn('"job",', squeezed["src/launcher/launcher_model.cpp"])
        self.assertIn('"launch",', squeezed["src/launcher/launcher_view.cpp"])
        self.assertIn('form_.add_toggle(kLogging, i18n::tr("Diagnostic logs"), true)',
                      squeezed["src/launcher/launcher_view.cpp"])
        # Every launcher line goes through the one macro that also feeds the trace.
        self.assertEqual(squeezed["src/launcher/launcher_ps5.cpp"].count("sys::log("), 0)
        self.assertIn("prosperolight_debug_start(paths.logs);", squeezed["src/app_storage.cpp"])
        self.assertIn('prosperolight_debug_line("main"', squeezed["src/main.cpp"])
        # Nothing that identifies the pairing is written: no key, certificate or PIN.
        for name, text in sources.items():
            for call in re.findall(r"prosperolight_debug_line\((?:[^;]|\n)*?\);", text):
                self.assertNotRegex(call, r"(?i)\bpin\b|cert|key_hex|rikey|private", name)


if __name__ == "__main__":
    unittest.main()
