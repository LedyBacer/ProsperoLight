# ps5-native-app-boilerplate - Per-host preference and launch compatibility checks.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class HostPreferencesTests(unittest.TestCase):
    def test_host_isolation_identity_migration_and_query_contract(self):
        code=r'''
#include "host_preferences.hpp"
#include "host_launch_options.h"
#include <cassert>
#include <cstring>
extern "C" unsigned short moonlight_config_host_port(const moonlight_config_host_t *host) {return host && host->http_port ? host->http_port : 47989;}
int main(int argc,char **argv) {
 std::snprintf(storage::g_paths.config,sizeof(storage::g_paths.config),"%s/config.bin",argv[1]);
 moonlight_config_host_t a{},b{};strcpy(a.address,"192.168.1.5");strcpy(b.address,"192.168.1.6");
 auto p=prosperolight::host_preferences(&a);
 assert(!p.extensions && p.vrr==0 && p.display==0 && p.scale==100 && p.optimize && p.mute_host && !p.quit_host);
 p.extensions=true;p.vrr=2;p.display=2;p.scale=75;p.quit_host=true;
 assert(prosperolight::host_preferences_save(a,p));
 assert(!prosperolight::host_preferences(&b).extensions);
 b=a;b.http_port=48989;assert(!prosperolight::host_preferences(&b).extensions);
 strcpy(a.unique_id,"stable-host");assert(prosperolight::host_preferences(&a).scale==75);
 assert(prosperolight::host_preferences_save(a,p));strcpy(a.address,"192.168.1.99");a.http_port=48989;
 assert(prosperolight::host_preferences(&a).scale==75);
 char query[160];gs_host_options_t options{false,1,1,75,15};
 gs_host_query(query,sizeof(query),&options,5);assert(query[0]==0);
 options.extensions=true;gs_host_query(query,sizeof(query),&options,5);
 assert(strcmp(query,"&psmap=5&clientVrrRequested=1&virtualDisplay=1&scaleFactor=75")==0);
 options.vrr=-1;options.virtual_display=-1;options.scale=100;
 gs_host_query(query,sizeof(query),&options,5);assert(strcmp(query,"&psmap=5")==0);
 gs_host_query(query,sizeof(query),&options,0);assert(strcmp(query,"&psmap=0")==0);
 options.vrr=0;options.virtual_display=0;gs_host_query(query,sizeof(query),&options,5);
 assert(strcmp(query,"&psmap=5&clientVrrRequested=0&virtualDisplay=0")==0);
 p.scale=49;assert(!prosperolight::host_preferences_save(a,p));
 prosperolight::host_preferences_remove(a);assert(!prosperolight::host_preferences(&a).extensions);
 const auto path=prosperolight::host_preferences_path(a);FILE *f=fopen(path.c_str(),"wb");fputs("broken",f);fclose(f);
 assert(!prosperolight::host_preferences(&a).extensions);
}'''
        compiler=shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as tmp:
            src=Path(tmp)/'host.cpp';src.write_text(code);exe=Path(tmp)/'host'
            subprocess.run([compiler,'-std=c++17','-I'+str(ROOT/'include'),'-I'+str(ROOT/'src/gamestream'),str(src),'-o',str(exe)],check=True)
            subprocess.run([str(exe),tmp],check=True)
