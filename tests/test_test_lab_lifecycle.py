# ps5-native-app-boilerplate - deterministic lifecycle and mailbox safety tests.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]

class LifecycleTests(unittest.TestCase):
    def test_deadlines_unique_ready_phases_and_commands(self):
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler unavailable')
        code = r'''
#include "test_lab_lifecycle.hpp"
#include "test_lab_output_evidence.hpp"
#include <cassert>
using namespace prosperolight::test_lab;
int main() {
 OutputEvidence evidence;
 evidence.submitted(1,1);evidence.submitted(2,2);evidence.submitted(3,3);
 assert(evidence.observe(1,1));assert(!evidence.observe(1,1));
 assert(evidence.observe(3,3)); // skipped request 2 is not counted
 evidence.submitted(4,3);assert(!evidence.observe(4,4)); // repeat picture
 assert(!evidence.observe(UINT64_MAX,5));
 for(unsigned i=5;i<45;++i)evidence.submitted(i,i);
 assert(!evidence.observe(5,5)); // overwritten bounded evidence
 assert(evidence.observe(44,44));
 TestProfile p;p.coordinated=true;p.run_id="run";p.startup_seconds=2;p.ready_seconds=3;p.hard_seconds=40;
 p.warmup_seconds=1;p.measure_seconds=30;p.cooldown_seconds=1;
 Timeline t(p,100);t.tick(100,{true,false,0});assert(t.state==Timeline::waiting_frame);
 t.tick(200,{true,false,200});assert(!t.ready_seen); // submissions alone are insufficient
 t.tick(300,{true,true,1});assert(t.ready_seen && t.state==Timeline::stream_ready);
 t.tick(400,{true,true,1});assert(t.state==Timeline::warmup);
 assert(!t.measured && !t.measurement_requested);
 assert(t.command("clock_probe",450) && t.state==Timeline::warmup);
 assert(t.command("start_measure",500));
 t.tick(1000500,{true,true,20});assert(t.state==Timeline::measuring);
 t.tick(31000500,{true,true,100});assert(t.measured && t.state==Timeline::cooldown);
 t.tick(32000500,{true,true,100});assert(t.state==Timeline::stopping && t.reason=="COMPLETE");
 assert(t.command("cancel",33000500));assert(t.reason=="COMPLETE");
 TestProfile standalone=p;standalone.coordinated=false;
 Timeline autonomous(standalone,0);autonomous.tick(1,{true,false,0});
 autonomous.tick(2,{true,true,1});autonomous.tick(3,{true,true,1});
 autonomous.tick(1000003,{true,true,5});assert(autonomous.state==Timeline::measuring);
 Timeline startup(p,0);startup.tick(2000000,{});assert(startup.reason=="STARTUP_TIMEOUT");
 Timeline ready(p,0);ready.tick(1,{true,false,0});ready.tick(3000001,{true,false,0});assert(ready.reason=="READY_TIMEOUT");
 Timeline hard(p,0);hard.tick(40000000,{true,true,1});assert(hard.reason=="HARD_TIMEOUT");
 Timeline cancel(p,0);assert(cancel.command("cancel",1));assert(cancel.reason=="CANCELLED");
 Timeline early(p,0);assert(!early.command("start_measure",1));assert(!early.measured);
 Timeline lost(p,0);lost.tick(1,{true,true,0});lost.tick(2,{true,true,1});
 lost.tick(3,{false,false,1});assert(lost.reason=="OUTPUT_LOST");
 Timeline stalled(p,0);stalled.tick(1,{true,true,0});stalled.tick(2,{true,true,1});
 assert(stalled.command("start_measure",3));stalled.tick(1000003,{true,true,1});
 stalled.tick(31000003,{true,true,1});
 assert(!stalled.measured && stalled.state==Timeline::stopping);
 assert(stalled.progress_stalls == 1 && stalled.progress_longest_us == 30000000);
 Timeline recovered(p,0);recovered.tick(1,{true,true,0});recovered.tick(2,{true,true,1});
 assert(recovered.command("start_measure",3));recovered.tick(1000003,{true,true,1});
 recovered.tick(1100003,{true,true,2});recovered.tick(2200003,{true,true,2});
 recovered.tick(2300003,{true,true,3});recovered.tick(2400003,{true,true,4});
 assert(recovered.progress_stalls == 1 && recovered.progress_longest_us == 1200000);
 assert(recovered.progress_samples == 4 && !recovered.progress_stalled);
 assert(recovered.progress_gap_bins[16] == 2 && recovered.progress_gap_bins[20] == 1);
 ControlCommand c;
 const char *valid=R"({"schema":"prosperolight-test-control/1","run_id":"run","command_id":"cmd-1","action":"stop"})";
 assert(parse_control(valid,"run",c) && c.action=="stop");
 assert(parse_control(R"({"schema":"prosperolight-test-control/1","run_id":"run","command_id":"clock","action":"clock_probe"})","run",c));
 assert(!parse_control(valid,"other",c));
 assert(!parse_control(R"({"schema":"prosperolight-test-control/1","run_id":"run","command_id":"../x","action":"stop"})","run",c));
 assert(!parse_control(R"({"schema":"prosperolight-test-control/1","run_id":"run","command_id":"c","action":"stop","action":"cancel"})","run",c));
 assert(!parse_control(std::string(2049,' '),"run",c));
}
'''
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory)
            (p / 'check.cpp').write_text(code)
            subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-pthread',
                            '-I', str(ROOT / 'include'), str(p / 'check.cpp'),
                            str(ROOT / 'src/test_lab_profile.cpp'), str(ROOT / 'src/test_lab_lifecycle.cpp'),
                            '-o', str(p / 'check')], check=True)
            subprocess.run([str(p / 'check')], check=True)

    def test_worker_normal_stop_cancel_and_immutable_receipt(self):
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler unavailable')
        code = r'''
#include "test_lab_lifecycle.hpp"
#include "app_storage.hpp"
#include <cassert>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
using namespace prosperolight::test_lab;
static std::atomic<unsigned> stops{0};
static Observation ready(void *) {static unsigned frames=0;return {true,true,++frames};}
static Observation handshake(void *) {return {};}
static void stop(void *) {++stops;}
static void await_stop(Context &context) {
 for(int i=0;i<50 && !context.stop_requested();++i)std::this_thread::sleep_for(std::chrono::milliseconds(100));
 assert(context.stop_requested());context.join();
}
static void start_measure_after_ready(const std::string &root,const char *id) {
 for(int i=0;i<50;++i) {
  std::ifstream status(root+"/status/"+id+".json");std::string value((std::istreambuf_iterator<char>(status)),{});
  if(value.find("\"stream_ready\":true")!=std::string::npos)break;
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
 }
 std::ofstream(root+"/control/"+id+".tmp")<<"{\"schema\":\"prosperolight-test-control/1\",\"run_id\":\""<<id
  <<"\",\"command_id\":\"start1\",\"action\":\"start_measure\"}";
 std::filesystem::rename(root+"/control/"+id+".tmp",root+"/control/"+id+".json");
}
int main(int,char **argv) {
 std::filesystem::create_directories(std::string(argv[1])+"/control");
 std::filesystem::create_directories(std::string(argv[1])+"/status");
 std::filesystem::create_directories(std::string(argv[1])+"/logs");
 snprintf(storage::g_paths.logs,sizeof(storage::g_paths.logs),"%s/logs",argv[1]);
 TestProfile p;p.run_id="complete";p.measure_seconds=1;p.startup_seconds=2;p.ready_seconds=2;p.hard_seconds=5;
 Context context;assert(context.begin(p));moonlight_stream_options_t options{};options.session_overrides.enabled=1;
 context.attach(ready,stop,nullptr,options);start_measure_after_ready(argv[1],"complete");await_stop(context);assert(stops.load()>0);
 std::ofstream(std::string(argv[1])+"/logs/performance-last.json")<<"{}";
 std::ofstream(std::string(argv[1])+"/logs/debug-trace.txt")<<"finished";
 std::ofstream(std::string(argv[1])+"/logs/performance-frames-mode0.csv")<<"fixture trace";
 moonlight_stream_metrics_t metrics{};metrics.presented_frames=9;
 context.host_cleanup_confirmed();context.host_exit_cleanup_result(true);assert(context.finish(&metrics,0));assert(!context.finish(&metrics,0));
 std::ifstream f(std::string(argv[1])+"/receipts/complete.json");
 std::string receipt((std::istreambuf_iterator<char>(f)),{});assert(receipt.find("PASS")!=std::string::npos);
 assert(receipt.find("\"paired_host_cleanup_confirmed\":true")!=std::string::npos);
 std::string begin_error;assert(!context.begin(p,&begin_error));assert(begin_error=="RUN_ID_REPLAY");assert(context.stop_requested());
 p.run_id="pyrowave";assert(context.begin(p));options.video_codec=MOONLIGHT_VIDEO_CODEC_PYROWAVE;
 context.attach(ready,stop,nullptr,options);start_measure_after_ready(argv[1],"pyrowave");await_stop(context);
 std::ofstream(std::string(argv[1])+"/logs/debug-trace.txt")<<"pyrowave completed";
 std::ofstream(std::string(argv[1])+"/logs/pyrowave-output-mode0.csv")<<"fresh output trace";
 context.host_exit_cleanup_result(true);assert(context.finish(&metrics,0));
 std::ifstream pyro(std::string(argv[1])+"/receipts/pyrowave.json");receipt.assign(std::istreambuf_iterator<char>(pyro),{});
 assert(receipt.find("\"functional_verdict\":\"PASS\"")!=std::string::npos);
 assert(receipt.find("pyrowave-output-mode0.csv")!=std::string::npos);
 assert(receipt.find("\"paired_host_cleanup_confirmed\":false")!=std::string::npos);
 assert(receipt.find("performance-last.json")==std::string::npos); // never claim stale native metrics
 p.run_id="host-exit-failure";assert(context.begin(p));
 context.attach(ready,stop,nullptr,options);start_measure_after_ready(argv[1],"host-exit-failure");await_stop(context);
 context.host_exit_cleanup_result(false);assert(context.finish(&metrics,0));
 std::ifstream failed_host(std::string(argv[1])+"/receipts/host-exit-failure.json");receipt.assign(std::istreambuf_iterator<char>(failed_host),{});
 assert(receipt.find("HOST_EXIT_CLEANUP_UNCONFIRMED")!=std::string::npos);
 assert(receipt.find("\"functional_verdict\":\"FAIL\"")!=std::string::npos);
 options.video_codec=MOONLIGHT_VIDEO_CODEC_H264;
 p.run_id="cancel";assert(context.begin(p));context.attach(handshake,stop,nullptr,options);
 const auto temporary=std::string(argv[1])+"/control/cancel.tmp";
 std::ofstream(temporary)<<R"({"schema":"prosperolight-test-control/1","run_id":"cancel","command_id":"cmd1","action":"cancel"})";
 std::filesystem::rename(temporary,std::string(argv[1])+"/control/cancel.json");
 await_stop(context);assert(context.finish(nullptr,0));
 std::ifstream c(std::string(argv[1])+"/receipts/cancel.json");receipt.assign(std::istreambuf_iterator<char>(c),{});
 assert(receipt.find("CANCELLED")!=std::string::npos && receipt.find("\"stream_ready\":false")!=std::string::npos);
 assert(receipt.find("\"fresh\":false")!=std::string::npos); // old exports cannot qualify
 active()=true;active_profile()=p;active_profile().run_id="launch-refused";
 assert(prosperolight::test_lab::context().begin(active_profile()));
 reject_launch("PYROWAVE_PROFILE_UNSUPPORTED");
 assert(prosperolight::test_lab::context().stop_requested());
 std::ifstream refused_status(std::string(argv[1])+"/status/launch-refused.json");
 std::string refused((std::istreambuf_iterator<char>(refused_status)),{});
 assert(refused.find("PYROWAVE_PROFILE_UNSUPPORTED")!=std::string::npos && refused.find("failed")!=std::string::npos);
 assert(prosperolight::test_lab::context().finish(nullptr,-1));
 std::ifstream refused_receipt(std::string(argv[1])+"/receipts/launch-refused.json");
 refused.assign(std::istreambuf_iterator<char>(refused_receipt),{});
 assert(refused.find("PYROWAVE_PROFILE_UNSUPPORTED")!=std::string::npos && refused.find("STARTUP_TIMEOUT")==std::string::npos);
 active()=false;
 for (const char *reason : {"NO_CONTROLLER", "HOST_LOST", "UNSUPPORTED_MODE"}) {
  p.run_id=reason;assert(context.begin(p));context.fail(reason);assert(context.finish(nullptr,-1));
  std::ifstream failed(std::string(argv[1])+"/receipts/"+reason+".json");
  receipt.assign(std::istreambuf_iterator<char>(failed),{});
  assert(receipt.find("\"functional_verdict\":\"FAIL\"")!=std::string::npos);
  assert(receipt.find(reason)!=std::string::npos);
  assert(receipt.find("\"stream_ready\":false")!=std::string::npos);
 }
 p.run_id="reused-command";assert(context.begin(p));context.attach(handshake,stop,nullptr,options);
 const auto mailbox=std::string(argv[1])+"/control/reused-command.json";
 std::ofstream(mailbox+".tmp")<<R"({"schema":"prosperolight-test-control/1","run_id":"reused-command","command_id":"same","action":"clock_probe"})";
 std::filesystem::rename(mailbox+".tmp",mailbox);
 bool acknowledged=false;
 for(int i=0;i<20;++i) {
  std::ifstream status(std::string(argv[1])+"/status/reused-command.json");
  std::string value((std::istreambuf_iterator<char>(status)),{});
  if(value.find("\"command_id\":\"same\"")!=std::string::npos){acknowledged=true;break;}
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
 }
 assert(acknowledged);
 std::ofstream(mailbox+".tmp")<<R"({"schema":"prosperolight-test-control/1","run_id":"reused-command","command_id":"same","action":"cancel"})";
 std::filesystem::rename(mailbox+".tmp",mailbox);
 await_stop(context);assert(context.finish(nullptr,-1));
 std::ifstream replay(std::string(argv[1])+"/receipts/reused-command.json");receipt.assign(std::istreambuf_iterator<char>(replay),{});
 assert(receipt.find("CONTROL_ID_REUSE")!=std::string::npos);
 p.run_id="crash";assert(context.begin(p));context.join(); // abrupt execution has no receipt
 assert(!std::filesystem::exists(std::string(argv[1])+"/receipts/crash.json"));
}
'''
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory)
            (p / 'check.cpp').write_text(code)
            subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-pthread',
                            '-DPROSPEROLIGHT_TEST_LAB_ROOT="' + str(p) + '"',
                            '-I', str(ROOT / 'include'), str(p / 'check.cpp'),
                            str(ROOT / 'src/test_lab_profile.cpp'), str(ROOT / 'src/test_lab_lifecycle.cpp'),
                            '-o', str(p / 'check')], check=True)
            subprocess.run([str(p / 'check'), str(p)], check=True)
            import json
            completed = json.loads((p / 'receipts/complete.json').read_text())
            self.assertEqual(completed['functional_verdict'], 'PASS')
            self.assertEqual(completed['measurement_validity'], 'INCONCLUSIVE')
            self.assertGreaterEqual(completed['unique_completed_frames'], 9)
            self.assertEqual(json.loads((p / 'receipts/cancel.json').read_text())['functional_verdict'], 'CANCELLED')

    def test_existing_connection_worker_interrupts_handshake(self):
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler unavailable')
        source = (ROOT / 'src/moonlight_stream.cpp').read_text()
        start = source.index('typedef struct connection_loading_state')
        end = source.index('static int start_connection_loading', start)
        worker = source[start:end]
        callback_start = source.index('[](void *data)', source.index('p.renderer->presented'))
        callback_end = source.index(',\n            &test_probe', callback_start)
        callback = source[callback_start:callback_end]
        code = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <pthread.h>
struct ps5_controller_state_t {std::atomic<int> requested_stop{0};};
static constexpr uint64_t CONNECTION_SETUP_TIMEOUT_US=10000000;
static unsigned http_calls=0,li_calls=0;
static uint64_t monotonic_us(){return 100;}
static int ps5_controller_disconnect_only(ps5_controller_state_t *){return 0;}
static void http_interrupt(){++http_calls;}
static void LiInterruptConnection(){++li_calls;}
static int sceKernelUsleep(uint32_t){return 0;}
''' + worker + r'''
struct TestProbe {
 ps5_controller_state_t *controller;connection_loading_state_t *loading;
 std::atomic<bool> controller_initialized{false};
};
static auto request_stop = ''' + callback + r''';
int main(){
 (void)active_connection_loading.load();
 ps5_controller_state_t controller;
 connection_loading_state_t state{};state.controller=&controller;state.active=1;state.connection_pending=1;
 TestProbe probe{&controller,&state};request_stop(&probe);
 assert(controller.requested_stop==0 && state.cancel_requested==1);
 probe.controller_initialized=true;request_stop(&probe);assert(controller.requested_stop==1);
 http_calls=0;state.cancel_requested=0;
 connection_loading_thread(&state);
 assert(http_calls==1 && li_calls==1 && state.active==0 && state.cancel_requested==1);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory)
            (p / 'check.cpp').write_text(code)
            subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-pthread',
                            str(p / 'check.cpp'), '-o', str(p / 'check')], check=True)
            subprocess.run([str(p / 'check')], check=True)
