# ps5-native-app-boilerplate - developer profile parsing and pure option overlays.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]


class TestLabProfileTests(unittest.TestCase):
    def test_parser_resolver_and_test_argument_gate(self):
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler unavailable')
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory)
            (p / 'test.cpp').write_text(r'''
#include "test_lab_profile.hpp"
#include "forwarded_launch.hpp"
#include "stream_selection.hpp"
#include <cassert>
#include <cstring>
using namespace prosperolight::test_lab;
int main() {
    const char *base=R"({"schema":"prosperolight-test/1","run_id":"run-1","suite":"decoder-smoke","client":{"host":"PC:47989","app":"Desktop","codec":"hevc","chroma":"420","hdr":false,"resolution":"1080p","fps":60,"bitrate_mbps":20,"vsync":true,"decoder_pipeline":"classic","decoder_cores":3,"pacing":"paced","vrr_profile":"smooth","host_extensions":{"enabled":true,"vrr":"off","display":"virtual","scale":75,"optimize":false,"mute_host":false,"quit_host":false}},"timing":{"hard_seconds":120}})";
    TestProfile profile; std::string error;
    assert(parse_profile(base,"run-1",profile,error));
    assert(!profile.decoder_capacity_probe);
    std::string probe=base; probe.insert(1,"\"capture\":{\"decoder_capacity_probe\":true},");
    assert(parse_profile(probe,"run-1",profile,error) && profile.decoder_capacity_probe);
    auto early=probe;early.insert(early.find("decoder_capacity_probe")-1,"\"decoder_capacity_probe_first\":true,");
    assert(parse_profile(early,"run-1",profile,error) && profile.decoder_capacity_probe_first);
    std::string only_first=base;only_first.insert(1,"\"capture\":{\"decoder_capacity_probe_first\":true},");
    assert(!parse_profile(only_first,"run-1",profile,error));
    auto wrong_probe=probe; auto at=wrong_probe.find("decoder_capacity_probe\":true");
    wrong_probe.replace(at,strlen("decoder_capacity_probe\":true"),"decoder_capacity_probe\":1");
    assert(!parse_profile(wrong_probe,"run-1",profile,error));
    assert(parse_profile(base,"run-1",profile,error));
    assert(!profile.coordinated.value_or(false));
    std::string coordinated=base;coordinated.insert(1,"\"coordinated\":true,");
    assert(parse_profile(coordinated,"run-1",profile,error) && profile.coordinated.value());
    assert(parse_profile(base,"run-1",profile,error));
    moonlight_stream_options_t saved{}; saved.video_codec=2;saved.hdr_enabled=1;saved.stream_fps=120;
    saved.bitrate_kbps=80000;saved.decoder_cores=5;saved.vsync_enabled=0;
    saved.chroma_sampling=MOONLIGHT_CHROMA_444;
    moonlight_backend_snapshot_t caps{};
    assert(moonlight::validate_stream_selection(saved,caps).code); // normal saved launch stays strict
    TestProfile h264;h264.codec=0;h264.chroma=0;h264.hdr=false;
    moonlight_stream_options_t lab{};prosperolight::HostPreferences defaults;
    std::string why;
    auto snapshot=saved;
    assert(resolve_profile(h264,saved,defaults,1,0,lab,why));
    assert(!moonlight::validate_stream_selection(lab,caps).code); // saved Pyro/HDR cannot veto H264 lab
    assert(!memcmp(&saved,&snapshot,sizeof(saved)));
    lab.video_codec=MOONLIGHT_VIDEO_CODEC_HEVC;
    assert(!strcmp(moonlight::validate_stream_selection(lab,caps).code,"HEVC_UNSUPPORTED"));
    caps.hevc_supported=true;lab.hdr_enabled=1;
    assert(!strcmp(moonlight::validate_stream_selection(lab,caps).code,"HDR_UNSUPPORTED"));
    caps.main10_supported=true;assert(!moonlight::validate_stream_selection(lab,caps).code);
    lab.video_codec=MOONLIGHT_VIDEO_CODEC_PYROWAVE;
    assert(!strcmp(moonlight::validate_stream_selection(lab,caps).code,"PYROWAVE_PROFILE_UNSUPPORTED"));
    auto before=saved; prosperolight::HostPreferences host;
    moonlight_stream_options_t resolved{};
    assert(resolve_profile(profile,saved,host,0,1,resolved,error));
    assert(!memcmp(&saved,&before,sizeof(saved)));
    assert(resolved.video_codec==1 && resolved.hdr_enabled==0 && resolved.chroma_sampling==0);
    assert(resolved.stream_resolution==0 && resolved.stream_fps==60 && resolved.bitrate_kbps==20000);
    assert(resolved.vsync_enabled==1 && resolved.decoder_pipeline==0 && resolved.decoder_cores==3);
    auto o=resolved.session_overrides;
    assert(o.enabled && o.pacing==1 && o.vrr_profile==2 && o.extensions && o.host_vrr==1);
    assert(o.display==2 && o.scale==75 && !o.optimize && !o.mute_host && !o.quit_host);
    assert(!parse_profile(base,"wrong-id",profile,error));
    for(auto bad:{"", "{}", "{\"schema\":\"bad\"}", "{\"x\":1,\"x\":2}", "{\"x\":01}", "{\"x\":-1}", "{\"x\":true,}", "{\"x\":[]} "})
        assert(!parse_profile(bad,"run-1",profile,error));
    auto reject=[&](const char *find,const char *replacement) {
        std::string input(base); auto at=input.find(find); assert(at!=std::string::npos);
        input.replace(at,strlen(find),replacement);assert(!parse_profile(input,"run-1",profile,error));
    };
    reject("\"fps\":60","\"fps\":121"); reject("\"bitrate_mbps\":20","\"bitrate_mbps\":0");
    reject("\"hdr\":false","\"hdr\":1");reject("\"codec\":\"hevc\"","\"codec\":\"bogus\"");
    reject("\"chroma\":\"420\"","\"chroma\":\"444\"");reject("\"hard_seconds\":120","\"hard_seconds\":1");
    reject("\"vsync\":true","\"vsync\":true,\"unknown\":0");
    assert(!parse_profile(std::string(16385,' '),"run-1",profile,error));
    assert(!safe_run_id("../x") && !safe_run_id(std::string(128,'a')));
    assert(!read_profile("../x",profile,error) && error=="INVALID_RUN_ID");
    assert(parse_profile(R"({"schema":"prosperolight-test/1","run_id":"run-1","suite":"smoke","client":{"host":"PC","app":"Desktop"}})","run-1",profile,error));
    assert(resolve_profile(profile,saved,host,2,1,resolved,error));
    assert(resolved.stream_fps==120 && resolved.hdr_enabled==1 && resolved.video_codec==2);
    assert(resolved.session_overrides.pacing==2 && !resolved.session_overrides.extensions);
    profile.display=2;assert(!resolve_profile(profile,saved,host,2,1,resolved,error));
    profile.display.reset();profile.codec=0;assert(!resolve_profile(profile,saved,host,2,1,resolved,error));
    char *args[]={const_cast<char *>("app"),const_cast<char *>("--test-run"),const_cast<char *>("../bad")};
    auto launch=prosperolight::parse_forwarded_launch(3,args);
#if PROSPEROLIGHT_TEST_LAB
    assert(launch.test_requested && launch.test_run=="../bad" && launch.requested());
    launch=prosperolight::parse_forwarded_launch(2,args);assert(launch.test_requested && launch.test_run.empty());
#else
    assert(!launch.requested());
#endif
}
''')
            for enabled in (0, 1):
                subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror',
                                '-DPROSPEROLIGHT_TEST_LAB=' + str(enabled), '-I', str(ROOT / 'include'),
                                '-I', str(ROOT / 'third_party/moonlight-common-c/src'),
                                str(p / 'test.cpp'), str(ROOT / 'src/test_lab_profile.cpp'),
                                '-o', str(p / 'test')], check=True)
                subprocess.run([str(p / 'test')], check=True)
