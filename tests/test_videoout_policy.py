# ps5-native-app-boilerplate - HFR metadata and actual VideoOut wrapper transitions.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

class VideoOutPolicyTests(unittest.TestCase):
    def test_capabilities_and_owner_transitions(self):
        code = r'''
#include "ps5_videoout_formats.h"
#include "presentation_preferences.hpp"
#include "native_agc_present.hpp"
#include "pyrowave/video/ps5_presentation_stats.hpp"
#include "lan_http_report.hpp"
#include <cassert>
#include <cstdlib>
#include <cstdarg>
#include <vector>
#include <string>
extern "C" int __wrap_sceVideoOutOpen(int32_t,int32_t,int32_t,const void*);
extern "C" int __wrap_sceVideoOutClose(int32_t);
extern "C" int __wrap_sceVideoOutConfigureOutput(int32_t,uint32_t,const void*,const void*,const void*);
extern "C" int __wrap_sceVideoOutVrrUnpegFromFixedRate(int32_t);
extern "C" int __wrap_sceVideoOutVrrPegToFixedRate(int32_t,uint64_t,uint64_t);
static int next_handle=10, configure_calls=0, close_rc=0;
static uint32_t refresh=5994;
static std::vector<int> unpeg,peg;
static std::vector<std::string> trace;
static int take(std::vector<int>& answers) {assert(!answers.empty());const int r=answers.front();answers.erase(answers.begin());return r;}
extern "C" int __real_sceVideoOutOpen(int32_t,int32_t,int32_t,const void*) {return ++next_handle;}
extern "C" int __real_sceVideoOutClose(int32_t) {return close_rc;}
extern "C" int __real_sceVideoOutConfigureOutput(int32_t,uint32_t mode,const void*,const void*,const void*) {++configure_calls;refresh=mode==15?11988:5994;return 0;}
extern "C" int sceVideoOutConfigureOutput(int32_t h,uint32_t m,const void*a,const void*b,const void*c) {return __wrap_sceVideoOutConfigureOutput(h,m,a,b,c);}
extern "C" int __real_sceVideoOutVrrUnpegFromFixedRate(int32_t) {return take(unpeg);}
extern "C" int __real_sceVideoOutVrrPegToFixedRate(int32_t,uint64_t,uint64_t) {return take(peg);}
extern "C" int __real_sceVideoOutSubmitFlip(int32_t,int32_t,uint32_t,int64_t) {return 0;}
extern "C" int sceVideoOutGetFlipStatus(int32_t,void* p) {auto *s=static_cast<uint64_t*>(p);s[0]=1;s[3]=1;return 0;}
extern "C" int sceVideoOutGetVblankStatus(int32_t,void*p) {static_cast<uint64_t*>(p)[0]=1;return 0;}
extern "C" int sceKernelUsleep(uint32_t) {return 0;}
extern "C" int prosperolight_logs_enabled() {return 0;}
extern "C" void prosperolight_log_append(const char*,const char*) {}
extern "C" void prosperolight_debug_line(const char*,const char* fmt,...) {char text[512];va_list ap;va_start(ap,fmt);vsnprintf(text,sizeof(text),fmt,ap);va_end(ap);trace.emplace_back(text);}
int native_videoout_hdr_active(int32_t) {return 0;}
uint32_t native_videoout_refresh_x100(int32_t h) {assert(h>=0);return refresh;}
[[noreturn]] void fail(const char*) {std::abort();}
int main(int,char **argv) {
    // HFR is independent of the optional/firmware-specific VRR declaration.
    for(uint64_t flags: {0x40u,0x40040u,0x80040u,0xc0040u}) assert(ps5_videoout_declares_hfr(flags));
    for(uint64_t flags: {0u,0x40000u,0x80000u}) assert(!ps5_videoout_declares_hfr(flags));
    assert(ps5_videoout_hfr_accepted(1,0));
    assert(!ps5_videoout_hfr_accepted(0,0)); // unsupported is not success
    assert(!ps5_videoout_hfr_accepted(-1,0));
    assert(!ps5_videoout_hfr_accepted(1,-1));
    snprintf(storage::g_paths.config,sizeof(storage::g_paths.config),"%s/settings.bin",argv[1]);
    assert(moonlight::save_presentation_mode(2));
    ps5_stream_output_policy(2);
    // The stream policy is independent of the saved launcher preference.
    assert(moonlight::save_presentation_mode(0));
    int h=__wrap_sceVideoOutOpen(0,0,0,nullptr);
    assert(!ps5_vrr_output_active());
    assert(__wrap_sceVideoOutConfigureOutput(h,15,nullptr,nullptr,nullptr)==0 && refresh==11988);
    unpeg={0};assert(__wrap_sceVideoOutVrrUnpegFromFixedRate(h)==0 && ps5_vrr_output_active());
    // A native placeholder closing must not reset a different streaming owner.
    assert(__wrap_sceVideoOutClose(h-1)==0 && ps5_vrr_output_active());
    close_rc=-1;assert(__wrap_sceVideoOutClose(h)==-1 && ps5_vrr_output_active());
    close_rc=0;assert(__wrap_sceVideoOutClose(h)==0 && !ps5_vrr_output_active());
    assert(!ps5_presentation_stats().available);
    h=__wrap_sceVideoOutOpen(0,0,0,nullptr);
    unpeg={int(0x8029001cu),0};peg={0};
    assert(__wrap_sceVideoOutVrrUnpegFromFixedRate(h)==0 && ps5_vrr_output_active());
    assert(unpeg.empty() && peg.empty());
    unpeg={int(0x8029001cu)};peg={int(0x8029001cu)};
    assert(__wrap_sceVideoOutVrrUnpegFromFixedRate(h)==0 && ps5_vrr_output_active());
    unpeg={0};peg={0};assert(__wrap_sceVideoOutVrrPegToFixedRate(h,0,0)==0 && !ps5_vrr_output_active());
    unpeg={int(0x8029001cu)};peg={int(0x8029001cu)};
    assert(__wrap_sceVideoOutVrrPegToFixedRate(h,0,0)==int(0x8029001cu) && ps5_vrr_output_active());
    unpeg={-2};assert(__wrap_sceVideoOutVrrUnpegFromFixedRate(h)==-2 && !ps5_vrr_output_active());
    ps5_launcher_output_policy(true);
    const int before=configure_calls;
    const int menu=__wrap_sceVideoOutOpen(0,0,0,nullptr);
    assert(configure_calls==before+1 && refresh==5994 && !ps5_vrr_output_active());
    assert(__wrap_sceVideoOutClose(menu)==0);
    ps5_launcher_output_policy(false);
    h=__wrap_sceVideoOutOpen(0,0,0,nullptr);
    assert(__wrap_sceVideoOutConfigureOutput(h,15,nullptr,nullptr,nullptr)==0 && refresh==11988);
    unpeg={0};assert(__wrap_sceVideoOutVrrUnpegFromFixedRate(h)==0 && ps5_vrr_output_active());
    bool saw_hfr=false,saw_menu=false;
    for(const auto& row:trace) {saw_hfr|=row.find("configure-15")!=std::string::npos;saw_menu|=row.find("owner=launcher")!=std::string::npos;}
    assert(saw_hfr && saw_menu);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            folder=pathlib.Path(directory)
            source=folder/'check.cpp';source.write_text(code)
            compiler=shutil.which('clang++') or shutil.which('g++')
            self.assertIsNotNone(compiler)
            binary=folder/'check'
            subprocess.run([compiler,'-std=c++20','-D__PROSPERO__','-pthread',
                            '-I'+str(ROOT/'include'),'-I'+str(ROOT/'src'),str(source),
                            str(ROOT/'src/pyrowave/video/ps5_presentation_stats.cpp'),'-o',str(binary)],check=True)
            subprocess.run([str(binary),str(folder)],check=True)

if __name__=='__main__':
    unittest.main()
