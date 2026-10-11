# ps5-native-app-boilerplate - ProsperoLight: launch arguments of a home-screen forwarder.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

PROGRAM = r"""
#include "forwarded_launch.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <vector>
using namespace prosperolight;
// The saved port, or Sunshine's default: what the app's own function answers.
extern "C" uint16_t moonlight_config_host_port(const moonlight_config_host_t *host)
{
    return host->http_port ? static_cast<uint16_t>(host->http_port) : 47989;
}
static ForwardedLaunch parse(std::initializer_list<const char *> list)
{
    std::vector<char *> argv;
    for (const char *item : list)
        argv.push_back(const_cast<char *>(item));
    return parse_forwarded_launch(static_cast<int>(argv.size()), argv.data());
}
int main()
{
    // Nothing asked for: the launcher opens as always.
    assert(!parse({}).requested());
    assert(!parse({"/app0/eboot.bin"}).requested());
    assert(!parse_forwarded_launch(0, nullptr).requested());
    assert(!parse({"--pc", "Gaming-PC"}).requested()); // a PC alone starts nothing
    // Both spellings, any order, quotes and spaces around a value, unknown arguments between.
    auto a = parse({"eboot.bin", "--pc", "Gaming-PC", "--verbose", "--app", "Paper Kites"});
    assert(a.requested() && a.pc == "Gaming-PC" && a.app == "Paper Kites" && !a.exit_after_stream);
    auto b = parse({"--exit-after-stream", "--app= \"Desktop\" ", "--pc=192.168.1.50:48989"});
    assert(b.requested() && b.app == "Desktop" && b.pc == "192.168.1.50:48989" && b.exit_after_stream);
    // A name that is missing or far too long does no harm.
    assert(!parse({"--app"}).requested());
    assert(parse({"--app", std::string(500, 'x').c_str()}).app.size() == 127);

    moonlight_config_t config{};
    config.host_count = 2;
    std::snprintf(config.hosts[0].name, sizeof(config.hosts[0].name), "Gaming-PC");
    std::snprintf(config.hosts[0].address, sizeof(config.hosts[0].address), "192.168.1.50");
    std::snprintf(config.hosts[1].name, sizeof(config.hosts[1].name), "Office");
    std::snprintf(config.hosts[1].address, sizeof(config.hosts[1].address), "192.168.1.60");
    config.hosts[1].http_port = 48989;
    config.selected_host = 1;
    assert(forwarded_host(config, "") == 1);
    assert(forwarded_host(config, "gaming-pc") == 0);
    assert(forwarded_host(config, "192.168.1.50") == 0);
    assert(forwarded_host(config, "192.168.1.60:48989") == 1);
    assert(forwarded_host(config, "192.168.1.60:1") == -1);
    assert(forwarded_host(config, "Gaming") == -1); // a whole name, never a part of one
    config.host_count = 0;
    assert(forwarded_host(config, "") == -1);

    moonlight_backend_snapshot_t backend{};
    const char *names[] = {"Desktop", "Steam Big Picture", "2048"};
    const int ids[] = {1298491830, 7, 12};
    for (int i = 0; i < 3; ++i)
    {
        moonlight_backend_app_t app{};
        app.id = ids[i];
        std::snprintf(app.name, sizeof(app.name), "%s", names[i]);
        backend.apps.push_back(app);
    }
    backend.app_count = 3;
    assert(forwarded_app(backend, "steam big picture") == 1);
    assert(forwarded_app(backend, "1298491830") == 0); // by number
    assert(forwarded_app(backend, "7") == 1);
    assert(forwarded_app(backend, "2048") == 2);        // a name made of digits is still a name
    assert(forwarded_app(backend, "Steam") == -1);
    assert(forwarded_app(backend, "") == -1);
    assert(forwarded_app(backend, "99999999999999999999") == -1);
    return 0;
}
"""


class ForwardedLaunchTests(unittest.TestCase):
    def test_arguments_and_matching(self):
        compiler = shutil.which("clang++") or shutil.which("g++")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            (folder / "check.cpp").write_text(PROGRAM)
            subprocess.run([compiler, "-std=c++20", "-I" + str(ROOT / "include"),
                            str(folder / "check.cpp"), "-o", str(folder / "check")], check=True)
            subprocess.run([str(folder / "check")], check=True)

    def test_the_app_acts_on_them_once(self):
        main = (ROOT / "src/main.cpp").read_text()
        launcher = (ROOT / "src/launcher/launcher_ps5.cpp").read_text()
        self.assertIn("int main(int argc, char **argv)", main)
        self.assertIn("parse_forwarded_launch(argc, argv)", main)
        self.assertIn("selection.exit_after_stream && stream_result == 0", main)
        # Only the first launcher of the process starts it; a stream that ended does not restart.
        self.assertIn("if (first_start)", launcher.split("ForwardedStart forwarded;")[1][:400])
        self.assertIn("prosperolight::pending_forwarded_launch() = {};", launcher)

    def test_it_goes_straight_to_the_connecting_screen(self):
        launcher = (ROOT / "src/launcher/launcher_ps5.cpp").read_text()
        view = (ROOT / "src/launcher/launcher_view.cpp").read_text()
        self.assertIn("view.begin_forwarded(forwarded.launch().app, forwarded.launch().pc);", launcher)
        self.assertIn("view.end_forwarded();", launcher)
        # Nothing is pressed on screens that are out of sight.
        self.assertIn("if (view.forwarding())", launcher)
        begin = view.split("void View::begin_forwarded(")[1].split("\n}\n")[0]
        self.assertIn("welcomed_ = true;", begin)
        self.assertIn("loader_.show(silent);", begin)
        # No second fade and no launch sound when the connecting screen is already up.
        self.assertIn("if (forwarding_ && launching_)\n        launch_age_ = kLaunchSeconds;", view)
        self.assertIn("if (launching_ && !plate_ && !forwarding_)", view)


if __name__ == "__main__":
    unittest.main()
