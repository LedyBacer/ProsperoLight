# ps5-native-app-boilerplate - Persistent client launch preferences.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class ClientPreferencesTests(unittest.TestCase):
    def test_defaults_and_reload_in_another_process(self):
        compiler=shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        code=r'''
#include "client_preferences.hpp"
#include <cassert>
int main(int argc,char **argv) {
 std::snprintf(storage::g_paths.config,sizeof(storage::g_paths.config),"%s/config.bin",argv[1]);
 auto p=prosperolight::client_preferences();
 if(argc==2) {
  assert(!p.custom_fps && p.optimize && p.mute_host);
  p={true,false,false};assert(prosperolight::client_preferences_save(p));
 } else { assert(p.custom_fps && !p.optimize && !p.mute_host); }
}'''
        with tempfile.TemporaryDirectory() as tmp:
            source=Path(tmp)/'test.cpp';source.write_text(code);exe=Path(tmp)/'test'
            subprocess.run([compiler,'-std=c++17','-I'+str(ROOT/'include'),str(source),'-o',str(exe)],check=True)
            subprocess.run([str(exe),tmp],check=True)
            subprocess.run([str(exe),tmp,'reload'],check=True)
