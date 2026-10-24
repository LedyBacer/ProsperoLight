# ps5-native-app-boilerplate - Full host app library and snapshot ownership.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class AppLibraryTests(unittest.TestCase):
    def test_large_library_copy_refresh_and_comparison(self):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        code = r'''
#include "moonlight_backend.hpp"
#include "fake_world.hpp"
#include <cassert>
#include <cstdio>
int main() {
 fake::Pc pc;
 pc.address="192.168.1.20";
 for(int i=0;i<512;++i) pc.apps.push_back({i+1,"Game "+std::to_string(i+1)});
 pc.current_app=500;
 fake::world().push_back(pc);
 moonlight_backend_snapshot_t first{};
 assert(moonlight_backend_refresh(pc.address.c_str(),47989,&first)==0);
 assert(first.app_count==512 && first.apps.size()==512);
 assert(first.apps[64].id==65 && first.apps.back().id==512);
 assert(std::string(first.apps.back().name)=="Game 512");
 assert(first.current_app_id==500);
 const unsigned char learned_mac[] = {0x02,0x11,0x22,0x33,0x44,0x55};
 std::memcpy(first.mac,learned_mac,sizeof(learned_mac));
 first.mac_valid=1;
 auto copy=first;
 assert(copy==first);
 copy.mac[5]^=1;
 assert(!(copy==first));
 copy=first;
 copy.mac_valid=0;
 assert(!(copy==first));
 copy=first;
 copy.apps[499].hdr_supported=1;
 assert(!(copy==first));
 assert(first.apps[499].hdr_supported==0);
 first={};
 assert(first.app_count==0 && first.apps.empty());
 assert(copy.apps.size()==512 && copy.apps.back().id==512);
 fake::world()[0].apps.resize(150);
 assert(moonlight_backend_refresh(pc.address.c_str(),47989,&first)==0);
 assert(first.app_count==150 && first.apps.back().id==150);
 fake::world()[0].apps.clear();
 assert(moonlight_backend_refresh(pc.address.c_str(),47989,&first)==0);
 assert(first.apps.empty() && first.app_count==0);
}'''
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            (path/'check.cpp').write_text(code)
            exe = path/'check'
            subprocess.run([compiler, '-std=c++20', '-pthread', '-ffunction-sections',
                            '-fdata-sections', '-I'+str(ROOT/'include'),
                            '-I'+str(ROOT/'tools/launcher-host'), str(path/'check.cpp'),
                            str(ROOT/'tools/launcher-host/fake_world.cpp'),
                            '-Wl,-dead_strip' if __import__('sys').platform=='darwin'
                            else '-Wl,--gc-sections', '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
        # The production adapter must consume the complete parsed linked list.
        source = (ROOT/'src/moonlight_backend.cpp').read_text()
        self.assertNotIn('MOONLIGHT_BACKEND_MAX_APPS', source)
        self.assertIn('snapshot->apps.push_back(output)', source)

    def test_sunshine_mac_path_preserves_http_and_reaches_unpaired_snapshots(self):
        client = (ROOT/'src/gamestream/client.c').read_text()
        client_header = (ROOT/'src/gamestream/client.h').read_text()
        backend = (ROOT/'src/moonlight_backend.cpp').read_text()
        snapshot = (ROOT/'include/moonlight_backend.hpp').read_text()

        self.assertIn('uint8_t mac[MOONLIGHT_WOL_MAC_BYTES];', client_header)
        self.assertIn('bool mac_valid;', client_header)
        load = client[client.index('static int load_server_info('):client.index('int gs_init(')]
        self.assertIn('char *mac = NULL;', load)
        self.assertIn('xml_search(response.body, response.length, "mac", &mac)', load)
        self.assertIn('if (mac && moonlight_wol_parse_mac(mac, server->mac))', load)
        self.assertIn('server->mac_valid = true;', load)
        self.assertIn('free(mac);', load)
        # load_server_info() is called on the same zero-initialized server first
        # over HTTP and then HTTPS. Only a valid parse writes the MAC or flag.
        init = client[client.index('int gs_init('):client.index('static int check_paired(')]
        self.assertLess(init.index('load_server_info(server, false)'),
                        init.index('load_server_info(server, true)'))

        refresh = backend[backend.index('int moonlight_backend_refresh('):
                          backend.index('static void *pairing_thread(')]
        self.assertLess(refresh.index('memcpy(snapshot->mac, server.mac'),
                        refresh.index('if (!server.paired)'))
        self.assertLess(refresh.index('snapshot->mac_valid = server.mac_valid'),
                        refresh.index('if (!server.paired)'))
        self.assertIn('uint8_t mac[MOONLIGHT_WOL_MAC_BYTES];', snapshot)
        self.assertIn('uint32_t mac_valid;', snapshot)
        self.assertIn('std::memcmp(mac, other.mac, sizeof(mac)) == 0', snapshot)


if __name__ == '__main__':
    unittest.main()
