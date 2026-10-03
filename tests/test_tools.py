#!/usr/bin/env python3
# ps5-native-app-boilerplate - Host tooling regression tests.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Exercises identity initialization and deployment resolution without a console.

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ToolTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("llvm-config-18"), "requires LLVM 18 host tools")
    def test_native_toolchain_is_shared_and_rejects_a_mismatched_compiler(self):
        environment = os.environ.copy()
        environment.pop("LLVM_CONFIG", None)
        environment.pop("PS5_CLANG", None)
        check = 'set -eu; source tools/native-toolchain.sh; "$PS5_CLANG" -dumpversion'
        selected = subprocess.run(["bash", "-c", check], cwd=ROOT, env=environment,
                                  text=True, capture_output=True)
        self.assertEqual(selected.returncode, 0, selected.stderr)
        self.assertTrue(selected.stdout.startswith("18."))
        environment["PS5_CLANG"] = "true"  # Exists, but is not a Clang 18 compiler.
        rejected = subprocess.run(["bash", "-c", check], cwd=ROOT, env=environment,
                                  text=True, capture_output=True)
        self.assertNotEqual(rejected.returncode, 0)
        self.assertIn("Clang 18", rejected.stderr)
        for script in ("build.sh", "build-stream-deps.sh"):
            self.assertIn('source "$root/tools/native-toolchain.sh"',
                          (ROOT / "tools" / script).read_text())

    def test_round3_candidates_keep_overlap_and_change_one_variable(self):
        builder = (ROOT / "tools/build-performance-candidates.sh").read_text()
        candidates = builder.split("candidates=$'", 1)[1].split("'", 1)[0].split(r"\n")
        rows = [row.split() for row in candidates]
        self.assertEqual(len(rows), 3)
        self.assertEqual(rows[0][1:], ["0", "0", "0", "1", "500", "0"])
        self.assertEqual(rows[1][1:-1], rows[0][1:-1])
        self.assertEqual(rows[1][-1], "1")
        self.assertEqual(rows[2][1:5], rows[1][1:5])
        self.assertEqual(rows[2][-2:], ["200", "1"])

    def test_release_enables_only_tested_presentation_overlap(self):
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        self.assertIn("PRESENT_OVERLAP ?= 1", makefile)
        self.assertIn("FLIP_POLL_US ?= 500", makefile)
        for option in ("FEC_SIMD", "OPUS_SIMD", "AUDIO_MAX_BACKLOG_MS", "PERFORMANCE_DETAIL",
                       "LAN_TELEMETRY", "STREAM_SELF_TEST_FPS",
                       "STREAM_SELF_TEST_RESOLUTION", "VIDEO_OUTPUT_SELF_TEST_FPS",
                       "STOP_ACTIVE_APP_SELF_TEST"):
            self.assertIn(f"{option} ?= 0", makefile)
        for name in ("moonlight_stream.cpp", "native_agc_present.cpp"):
            source = (ROOT / "src" / name).read_text(encoding="utf-8")
            self.assertIn("#define PROSPEROLIGHT_PRESENT_OVERLAP 1", source)
        builder = (ROOT / "tools/build-performance-candidates.sh").read_text(
            encoding="utf-8"
        )
        restore = builder.split("# The build always recreates the folder.", 1)[1]
        self.assertIn("PRESENT_OVERLAP=1", restore)
        self.assertIn('$out/04-presentation-overlap/$title/eboot.bin', restore)

    def test_stream_archives_share_one_parallel_build_recipe(self):
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        self.assertIn("$(STREAM_ARCHIVES) &: $(STREAM_INPUTS)", makefile)

    def test_stream_dependencies_use_ps5_nonblocking_socket_adapter(self):
        compatibility = (ROOT / "platform/ps5/ps5_compat.h").read_text(
            encoding="utf-8"
        )
        implementation = (ROOT / "platform/ps5/ps5_sockets.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("#define fcntl ps5_socket_fcntl", compatibility)
        self.assertIn("command == F_GETFL", implementation)
        self.assertIn("command == F_SETFL", implementation)
        self.assertIn("SCE_NET_SO_NBIO", implementation)

    def test_stream_threads_skip_unsafe_ps5_thread_rename(self):
        compatibility = (ROOT / "platform/ps5/ps5_compat.h").read_text(
            encoding="utf-8"
        )
        # The rename is still never issued; the hook places the thread instead.
        self.assertIn(
            "#define pthread_setname_np ps5_pthread_setname_place", compatibility
        )
        self.assertIn("ps5_thread_placement_apply_name(name);", compatibility)
        self.assertNotIn("pthread_rename_np(", compatibility)

    def test_release_build_disables_blocking_lan_telemetry(self):
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        self.assertIn("LAN_TELEMETRY ?= 0", makefile)
        self.assertIn(
            "APP_DEFINITIONS += PROSPEROLIGHT_LAN_TELEMETRY=$(LAN_TELEMETRY)",
            makefile,
        )

    def test_stop_refresh_preserves_the_last_application_catalog(self):
        source = (ROOT / "src/launcher/launcher_model.cpp").read_text(encoding="utf-8")
        run = source[source.index("void Model::Run(") : source.index("bool Model::StartNext()")]
        apply = source[source.index("bool Model::ApplySelected(") :]
        apply = apply[: apply.index("void Model::Apply(const Job")]

        self.assertIn(
            "moonlight_backend_stop_app(job.host, job.port, &result->snapshot)", run
        )
        # A stop that failed but still listed the apps keeps that list.
        self.assertIn(
            "if (success || (job.kind != JobKind::health && result.snapshot.app_count))", apply
        )
        self.assertIn("backend_ = result.snapshot;", apply)

    def test_stop_status_renders_before_the_sunshine_request(self):
        source = (ROOT / "src/launcher/launcher_model.cpp").read_text(encoding="utf-8")
        view = (ROOT / "src/launcher/launcher_view.cpp").read_text(encoding="utf-8")
        start = source.index("void Model::StopApp()")
        request = source[start : source.index("bool Model::RequestStream()", start)]

        # The request is queued for the worker thread; the screen keeps drawing
        # and says what it is waiting for.
        self.assertIn("job.kind = JobKind::stop;", request)
        self.assertIn("requests_.push_back(job);", request)
        self.assertNotIn("moonlight_backend_stop_app", request)
        self.assertIn('busy == Busy::stopping     ? "Stopping the app"', view)

    def test_hdr_supports_every_stream_resolution(self):
        stream = (ROOT / "src/moonlight_stream.cpp").read_text(encoding="utf-8")
        modes = stream[stream.index("static const native_video_mode_t video_modes[]") :]
        modes = modes[: modes.index("static_assert")]
        self.assertEqual(modes.count("VIDEO_FORMAT_H265_MAIN10"), 3)
        self.assertIn("MOONLIGHT_STREAM_RESOLUTION_1440P, VIDEO_FORMAT_H265_MAIN10", modes)
        self.assertIn("MOONLIGHT_STREAM_RESOLUTION_2160P, VIDEO_FORMAT_H265_MAIN10", modes)

        view = (ROOT / "src/launcher/launcher_view.cpp").read_text(encoding="utf-8")
        settings = view[view.index("void View::apply_setting(int id)") :]
        resolution = settings[settings.index("case kResolution:") : settings.index("case kCodec:")]
        self.assertNotIn("hdr_enabled", resolution)

    def test_hdr_overlay_identifies_hdr_on_the_first_line(self):
        source = (ROOT / "src/native_agc_present.cpp").read_text(encoding="utf-8")
        first_line = source[source.index('"Video stream: %ux%u %u.%02u FPS (Codec: %s%s)') :]
        first_line = first_line[: first_line.index("hud_line(luma, 0,")]
        self.assertIn('metrics->video_codec ? "HEVC" : "H.264"', first_line)
        self.assertIn('hdr ? " / HDR" : ""', first_line)

    def test_selectable_frame_rate_reaches_sunshine_and_decoder(self):
        config = (ROOT / "include/moonlight_config.hpp").read_text(encoding="utf-8")
        launcher = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        stream = (ROOT / "src/moonlight_stream.cpp").read_text(encoding="utf-8")
        platform = (ROOT / "src/launcher/launcher_ps5.cpp").read_text(encoding="utf-8")
        view = (ROOT / "src/launcher/launcher_view.cpp").read_text(encoding="utf-8")

        for fps in (60, 90, 120):
            self.assertIn(f"#define MOONLIGHT_STREAM_FPS_{fps} {fps}U", config)
        self.assertIn('form_.add_action(kFrameRate, "Frame rate")', view)
        self.assertIn('number_prompt_.keyboard.set_layouts({ui::KeyboardLayout::numeric()});', view)
        self.assertIn('MOONLIGHT_STREAM_FPS_MIN', view)
        self.assertIn('MOONLIGHT_STREAM_FPS_MAX', view)
        self.assertIn("selection->stream_fps = config.stream_fps;", platform)
        self.assertIn("options.stream_fps = selection.stream_fps;", launcher)
        self.assertIn("stream_config.fps = (int)stream_fps;", stream)
        self.assertIn(
            "moonlight::client_refresh_x100(stream_fps, connecting_screen.output_refresh_x100)",
            stream,
        )
        self.assertIn("stream_config.clientRefreshRateX100 = (int)renderer.client_refresh_x100;", stream)
        negotiation = stream.index("stream_config.clientRefreshRateX100 =")
        self.assertLess(stream.index("result = connecting_screen_begin("), negotiation)
        self.assertLess(negotiation, stream.index("result = prepare_native_session(&client_identity"))
        self.assertLess(negotiation, stream.index("connection_result = LiStartConnection("))
        begin = stream[stream.index("static int connecting_screen_begin("):]
        self.assertLess(begin.index("native_agc_output_status("), begin.index("pthread_create("))
        self.assertIn("redraw_rate != (int)state->stream_fps", stream)

    def test_selectable_surround_audio_reaches_sunshine_and_ps5_audioout(self):
        config = (ROOT / "include/moonlight_config.hpp").read_text(encoding="utf-8")
        launcher = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        stream = (ROOT / "src/moonlight_stream.cpp").read_text(encoding="utf-8")
        platform = (ROOT / "src/launcher/launcher_ps5.cpp").read_text(encoding="utf-8")
        view = (ROOT / "src/launcher/launcher_view.cpp").read_text(encoding="utf-8")

        self.assertIn("#define MOONLIGHT_AUDIO_51_SURROUND 1U", config)
        self.assertIn('form_.add_choice(kAudio, "Audio", {"Stereo", "5.1 surround"}, 0)', view)
        self.assertIn("selection->audio_configuration = config.audio_configuration;", platform)
        self.assertIn("options.audio_configuration = selection.audio_configuration;", launcher)
        self.assertIn("audio_configuration = AUDIO_CONFIGURATION_51_SURROUND;", stream)
        self.assertIn("#define PS5_AUDIO_FORMAT_S16_8CH 2", stream)
        self.assertIn("opus->channelCount == AUDIO_51_CHANNELS", stream)
        self.assertIn("state->output_channels - state->channels", stream)

    def test_frame_rate_is_independent_of_resolution_and_bitrate(self):
        source = (ROOT / "src/launcher/launcher_view.cpp").read_text(encoding="utf-8")
        settings = source[source.index("void View::apply_setting(int id)"):]
        resolution = settings[settings.index("case kResolution:"):settings.index("case kCodec:")]
        self.assertNotIn("stream_fps", resolution)
        numeric = source[source.index("else if (number_prompt_.is_open())"):]
        numeric = numeric[:numeric.index("else if (port_prompt_.is_open())")]
        self.assertIn("config.stream_fps = value;", numeric)
        self.assertNotIn("stream_resolution", numeric)
        self.assertIn("config.bitrate_mbps = value;", numeric)

    def test_high_refresh_self_test_is_compile_time_disabled(self):
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        app = (ROOT / "src/launcher/launcher_ps5.cpp").read_text(encoding="utf-8")

        self.assertIn("STREAM_SELF_TEST_FPS ?= 0", makefile)
        self.assertIn("STREAM_SELF_TEST_RESOLUTION ?= 0", makefile)
        self.assertIn("VIDEO_OUTPUT_SELF_TEST_FPS ?= 0", makefile)
        self.assertIn("STOP_ACTIVE_APP_SELF_TEST ?= 0", makefile)
        self.assertIn(
            "PROSPEROLIGHT_STREAM_SELF_TEST_FPS=$(STREAM_SELF_TEST_FPS)", makefile
        )
        self.assertIn(
            "PROSPEROLIGHT_STREAM_SELF_TEST_RESOLUTION=$(STREAM_SELF_TEST_RESOLUTION)",
            makefile,
        )
        self.assertIn(
            "PROSPEROLIGHT_VIDEO_OUTPUT_SELF_TEST_FPS=$(VIDEO_OUTPUT_SELF_TEST_FPS)",
            makefile,
        )
        self.assertIn(
            "PROSPEROLIGHT_STOP_ACTIVE_APP_SELF_TEST=$(STOP_ACTIVE_APP_SELF_TEST)",
            makefile,
        )
        self.assertIn("#if PROSPEROLIGHT_STREAM_SELF_TEST_FPS != 0", app)
        self.assertIn("high_refresh_self_test_consumed", app)
        stream = (ROOT / "src/moonlight_stream.cpp").read_text(encoding="utf-8")
        bounded_test = stream.split("#if PROSPEROLIGHT_STREAM_SELF_TEST_FPS != 0", 1)[1]
        bounded_test = bounded_test.split("#endif", 1)[0]
        self.assertIn("first_frame_wait_start_us >= UINT64_C(90000000)", bounded_test)
        self.assertIn("break;", bounded_test)
        self.assertIn("RunVideoOutputSelfTest", (ROOT / "src/main.cpp").read_text(encoding="utf-8"))
        self.assertIn(
            "PROSPEROLIGHT_STREAM_SELF_TEST_RESOLUTION == MOONLIGHT_STREAM_RESOLUTION_2160P",
            (ROOT / "src/main.cpp").read_text(encoding="utf-8"),
        )
        self.assertIn(
            "present_result = native_agc_wait_source_idle(frame_surface);",
            (ROOT / "src/main.cpp").read_text(encoding="utf-8"),
        )
        self.assertIn("stop_active_app_self_test_consumed", app)
        self.assertIn(
            "selection->stream_resolution = PROSPEROLIGHT_STREAM_SELF_TEST_RESOLUTION", app
        )
        # The self-test streams with its own values and saves none of them.
        self_test = app[app.index("#if PROSPEROLIGHT_STREAM_SELF_TEST_FPS != 0") :]
        self_test = self_test[: self_test.index("#endif")]
        self.assertNotIn("SettingsChanged", self_test)

    def test_files_are_kept_under_data_prosperolight(self):
        main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        storage = (ROOT / "src/app_storage.cpp").read_text(encoding="utf-8")
        helper = (ROOT / "tooling/elevation/helper/main.cpp").read_text(encoding="utf-8")
        build = (ROOT / "tools/build.sh").read_text(encoding="utf-8")
        title = json.loads((ROOT / "sce_sys/param.json").read_text(encoding="utf-8"))["titleId"]

        # Filesystem access is asked for before anything else runs.
        body = main[main.index("int main()") :]
        self.assertLess(body.index("storage::Initialize();"), body.index("RunVideoOutputSelfTest();"))
        self.assertLess(body.index("storage::Initialize();"), body.index("launcher::Run("))
        # The helper only answers this title, and the paths name it.
        self.assertIn(f'target_title_id[] = "{title}"', helper)
        self.assertIn(f'kTitleId[] = "{title}"', storage)
        self.assertIn(f'kInstallDir[] = "/data/homebrew/{title}"', storage)
        self.assertIn('kDataDir[] = "/data/prosperolight"', storage)
        # What the sandbox kept is read before the process leaves it, and the
        # log opens once its folder is known.
        initialize = storage[storage.index("void storage::Initialize()") :]
        self.assertLess(initialize.index("remember(kept);"), initialize.index("elevation::request("))
        self.assertLess(initialize.index("elevation::request("), initialize.index("open_log("))
        self.assertIn("setegid(getgid())", initialize)
        # The build checks the helper and ships it beside eboot.bin.
        self.assertIn('validate-helper.py" "$build/elevation/sandbox-elevator.elf"', build)
        self.assertIn('"$app/sandbox-elevator.elf"', build)
        # No other source names a sandbox path.
        for path in sorted((ROOT / "src").rglob("*")):
            if path.suffix not in {".c", ".cpp", ".h", ".hpp"} or "elevation" in path.parts:
                continue
            if path.name == "app_storage.cpp":
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            self.assertNotIn('"/download0', text, path)
            self.assertNotIn('"/app0', text, path)

    def test_launcher_presents_before_refreshing_sunshine(self):
        model = (ROOT / "src/launcher/launcher_model.cpp").read_text(encoding="utf-8")
        platform = (ROOT / "src/launcher/launcher_ps5.cpp").read_text(encoding="utf-8")
        initialize = model[model.index("void Model::Initialize(") : model.index("void Model::Shutdown()")]

        # Nothing asks the network before the first frame: requests are queued
        # for the worker thread, and the splash goes after the first swap.
        self.assertNotIn("moonlight_backend_", initialize)
        self.assertNotIn("moonlight_discover_hosts", initialize)
        self.assertIn("QueueSelectedRefresh();", initialize)
        self.assertLess(platform.index("const bool swapped = display.swap();"),
                        platform.index("sys::hide_splash_screen()"))

    def test_launcher_and_stream_take_turns_with_the_display(self):
        main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        platform = (ROOT / "src/launcher/launcher_ps5.cpp").read_text(encoding="utf-8")
        presenter = (ROOT / "src/native_agc_present.cpp").read_text(encoding="utf-8")
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")

        # The launcher gives back the audio port, the controller and the
        # display, in that order, before the stream opens them for itself.
        close = platform[platform.index("model.Shutdown();") :]
        self.assertLess(close.index("audio_out.stop();"), close.index("pad.close();"))
        self.assertLess(close.index("pad.close();"), close.index("display.close();"))
        loop = main[main.index("for (;;)", main.index("int main()")) :]
        self.assertLess(loop.index("launcher::Run("), loop.index("moonlight_stream_run("))
        # After a stream above 60 Hz the television is left to switch back.
        self.assertIn("HFR_SETTLE_MS ?= 5000", makefile)
        self.assertIn("APP_DEFINITIONS += PROSPEROLIGHT_HFR_SETTLE_MS=$(HFR_SETTLE_MS)", makefile)
        self.assertIn(
            "selection.stream_fps > MOONLIGHT_STREAM_FPS_60 || selection.hdr_enabled != 0;", main
        )
        self.assertIn("mode_changed ? PROSPEROLIGHT_HFR_SETTLE_MS : 100;", main)
        # AGC is initialised once per process, by whichever renderer drew first.
        self.assertIn("native_agc_note_initialized();", platform)
        self.assertEqual(presenter.count("sceAgcInit(8)"), 1)
        self.assertIn("if (!agc_initialized)", presenter)

    def test_main10_descriptors_follow_the_visible_resolution(self):
        source = (ROOT / "src/native_agc_present.cpp").read_text(encoding="utf-8")
        start = source.index("static void bind_main10_source")
        end = source.index("static int render_frame", start)
        binding = source[start:end]

        self.assertIn("uint32_t y_width = visible_width - 1u", binding)
        self.assertIn("uint32_t y_height = visible_height - 1u", binding)
        self.assertIn("uint32_t uv_width = visible_width / 2u - 1u", binding)
        self.assertIn("uint32_t uv_height = (visible_height + 1u) / 2u - 1u", binding)
        self.assertIn("descriptor[2] = (y_width >> 2) | (y_height << 14)", binding)
        self.assertIn("descriptor[10] = (uv_width >> 2) | (uv_height << 14)", binding)
        self.assertNotIn("0x010dc1dfu", binding)
        self.assertNotIn("0x0086c0efu", binding)

    def test_stream_opens_pad_after_decoder_loading_worker_stops(self):
        source = (ROOT / "src/moonlight_stream.cpp").read_text(encoding="utf-8")
        early_loading = source.index("result = start_connection_loading(&loading, NULL);")
        early_loading_stop = source.index("stop_connection_loading();", early_loading)
        pad_init = source.index("controller_result = ps5_controller_init(&controller)")
        self.assertLess(early_loading, early_loading_stop)
        self.assertLess(early_loading_stop, pad_init)

    def test_stream_forwards_only_the_newest_pad_sample(self):
        source = (ROOT / "src/moonlight_stream.cpp").read_text(encoding="utf-8")
        start = source.index("static void ps5_controller_poll(")
        end = source.index("static void ps5_controller_stop(", start)
        poll = source[start:end]

        self.assertIn("ps5_controller_newest_sample(state, count)", poll)
        self.assertNotIn("&state->sample_batch[index]", poll)

    def test_stream_uses_one_mbedtls_layout_and_refuses_inputless_video(self):
        certgen = (ROOT / "src/gamestream/certgen.h").read_text(encoding="utf-8")
        source = (ROOT / "src/moonlight_stream.cpp").read_text(encoding="utf-8")
        start = source.index("static int ps5_controller_open(")
        end = source.index("static int ps5_keyboard_has_key", start)
        controller_handoff = source[start:end]
        stream = source[source.index("int moonlight_stream_run(") :]

        self.assertIn("attempt < PS5_PAD_OPEN_ATTEMPTS", controller_handoff)
        self.assertIn("sceKernelUsleep(PS5_PAD_OPEN_RETRY_US);", controller_handoff)
        self.assertIn('#define MBEDTLS_CONFIG_FILE "mbedtls_ps5_config.h"', certgen)
        failure = stream.index(
            'gs_error = "PS5 controller ownership was unavailable; retry the stream";'
        )
        self.assertLess(failure, stream.index("prepare_native_session("))

    def test_physical_input_loads_modules_and_batch_drains_all_handles(self):
        source = (ROOT / "src/moonlight_stream.cpp").read_text(encoding="utf-8")
        start = source.index("static int ps5_physical_input_init(")
        end = source.index("static ps5_pad_sample_t *", start)
        physical_input = source[start:end]

        self.assertLess(
            physical_input.index("sceSysmoduleLoadModule(UINT32_C(0x0106))"),
            physical_input.index("sceKeyboardInit()"),
        )
        self.assertLess(
            physical_input.index("sceSysmoduleLoadModule(UINT32_C(0x00a9))"),
            physical_input.index("sceMouseInit()"),
        )
        self.assertIn("sceKeyboardRead(", physical_input)
        self.assertNotIn("sceKeyboardReadState(", physical_input)
        self.assertIn("state->keyboard_handles[slot]", physical_input)
        self.assertIn("state->mouse_handles[slot]", physical_input)

    def test_launch_advertises_moonlight_hdr_capabilities_for_main10(self):
        source = (ROOT / "src/gamestream/client.c").read_text(encoding="utf-8")
        self.assertIn("configuration->supportedVideoFormats & VIDEO_FORMAT_MASK_10BIT", source)
        self.assertIn("&hdrMode=1&clientHdrCapVersion=0", source)
        self.assertIn("&clientHdrCapSupportedFlagsInUint32=0", source)
        self.assertIn("&clientHdrCapMetaDataId=NV_STATIC_METADATA_TYPE_1", source)
        self.assertIn("&clientHdrCapDisplayData=0x0x0x0x0x0x0x0x0x0x0", source)

    def test_hdr_frames_wait_for_control_stream_confirmation(self):
        source = (ROOT / "src/moonlight_stream.cpp").read_text(encoding="utf-8")
        start = source.index("static int decode_access_unit(")
        end = source.index("static void fallback_to_classic_decoder(", start)
        submit = source[start:end]

        self.assertIn("(hdr_transitions && !hdr_active)", submit)
        self.assertNotIn("!decode_unit->hdrActive", submit)

    def test_hdr_overlay_uses_a_distinct_relocatable_shader_header(self):
        source = (ROOT / "src/native_agc_present.cpp").read_text(encoding="utf-8")

        self.assertIn("#define HDR_HUD_SHADER_HEADER_OFFSET 0xe000u", source)
        self.assertIn(
            "presenter.shader_memory + HDR_HUD_SHADER_HEADER_OFFSET, 0x1000,",
            source,
        )
        self.assertIn(
            "presenter.shader_memory + HDR_HUD_SHADER_HEADER_OFFSET,\n"
            "                                    presenter.shader_memory + HDR_HUD_SHADER_CODE_OFFSET",
            source,
        )
        self.assertNotIn(
            "&presenter.hud_pixel_shader, presenter.shader_memory + 0x1000,", source
        )

    def test_hdr_overlay_uses_distinct_sdr_conversion_resources(self):
        source = (ROOT / "src/native_agc_present.cpp").read_text(encoding="utf-8")

        self.assertIn("#define HDR_HUD_RESOURCES_OFFSET 0xf000u", source)
        self.assertIn(
            "prepare_resources(presenter.shader_memory + HDR_HUD_RESOURCES_OFFSET, 0)",
            source,
        )
        self.assertIn(
            "uint8_t *hud_resources = hdr ? memory + HDR_HUD_RESOURCES_OFFSET : resources;",
            source,
        )
        self.assertIn("bind_pixel_source(&command, hud_resources,", source)

    def test_4k_videoout_follows_the_hardware_accepted_registration_path(self):
        source = (ROOT / "src/native_agc_present.cpp").read_text(encoding="utf-8")
        initialize = source[
            source.index("static int initialize_presenter(") : source.index(
                "static int present_frame(", source.index("static int initialize_presenter(")
            )
        ]

        self.assertNotIn("sceVideoOutAddBuffer4k2kPrivilege", source)
        self.assertLess(
            initialize.index("sceVideoOutOpen"),
            initialize.index("sceVideoOutSetFlipRate"),
        )
        self.assertLess(
            initialize.index("sceVideoOutSetFlipRate"),
            initialize.index("sceKernelAllocateDirectMemory", initialize.index("sceVideoOutOpen")),
        )
        self.assertLess(
            initialize.index("sceVideoOutSetBufferAttribute2"),
            initialize.index("sceVideoOutRegisterBuffers2"),
        )

    def test_high_refresh_videoout_uses_firmware_mode_ids_and_reports_scanout(self):
        presenter = (ROOT / "src/native_agc_present.cpp").read_text(encoding="utf-8")
        launcher = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        stream = (ROOT / "src/moonlight_stream.cpp").read_text(encoding="utf-8")
        build_script = (ROOT / "tools/build.sh").read_text(encoding="utf-8")

        self.assertIn("#define VIDEO_OUT_REFRESH_RATE_89_91 UINT64_C(35)", presenter)
        self.assertIn("#define VIDEO_OUT_REFRESH_RATE_119_88 UINT64_C(13)", presenter)
        self.assertIn("#define VIDEO_OUT_REQUEST_120_HZ 15u", presenter)
        self.assertIn("#define VIDEO_OUT_REQUEST_DEFAULT 1u", presenter)
        self.assertNotIn("native_agc_restore_launcher_output", presenter)
        self.assertNotIn("native_agc_restore_launcher_output();", launcher)
        self.assertIn("restore_result = configure_launcher_output(presenter.video);", presenter)
        self.assertIn("sceVideoOutVrrUnpegFromFixedRate", presenter)
        self.assertNotIn("VIDEO_OUT_BUS_TYPE_4K_HIGH_REFRESH", presenter)
        self.assertNotIn("sceVideoOutOpen4kHighRefresh", presenter)
        self.assertIn("presenter.requested_fps > 60u ? 100000u : 500000u", presenter)
        # Flip status decides; an event only wakes the waiter early, and no
        # vblank wait may stand between a completed flip and its observer.
        self.assertIn("sceKernelWaitEqueue(presenter.flip_queue, event, 1, &count, &timeout)", presenter)
        wait = presenter[presenter.index("static int wait_for_marker(") :]
        wait = wait[: wait.index("int native_agc_wait_source_idle(")]
        self.assertNotIn("sceVideoOutWaitVblank", wait)
        self.assertIn("#define PROSPEROLIGHT_FLIP_POLL_US 500", presenter)
        self.assertIn("sceKernelUsleep(PROSPEROLIGHT_FLIP_POLL_US);", presenter)
        self.assertIn("requested_fps > 60u ? VIDEO_OUT_REQUEST_120_HZ : VIDEO_OUT_REQUEST_DEFAULT", presenter)
        self.assertIn("sceVideoOutIsOutputSupported", presenter)
        self.assertIn("sceVideoOutConfigureOutput", presenter)
        self.assertNotIn("sceVideoOutSysConfigureOutput", presenter)
        self.assertNotIn("VIDEO_OUT_REQUEST_HIGH_RES", presenter)
        self.assertIn("*vrr_result = sceVideoOutVrrUnpegFromFixedRate(handle);", presenter)
        self.assertIn(
            "native_agc_output_geometry(output_source_width, output_source_height, requested_fps)",
            presenter,
        )
        self.assertIn(
            "videoout_stub=$(build_system_link_stub libSceVideoOut "
            "vendor/ps5/sdk/stubs/videoout_link_stub.c)",
            build_script,
        )
        self.assertIn('--stub "$videoout_stub"', build_script)
        self.assertIn("sceVideoOutGetResolutionStatus", presenter)
        self.assertIn("sceVideoOutGetOutputStatus", presenter)
        self.assertIn("static_assert(sizeof(video_output_status_t) == 48", presenter)
        self.assertIn("offsetof(video_output_status_t, refresh_rate) == 8", presenter)
        self.assertIn("video_output_status_t output = {};", presenter)
        self.assertNotIn("uint64_t output_status[8]", presenter)
        self.assertIn(
            "reported_refresh = video_output_refresh_x100(resolution.refresh_rate)",
            presenter,
        )
        self.assertIn(
            "reported_refresh = video_output_refresh_x100(output.refresh_rate)",
            presenter,
        )
        self.assertNotIn("video_output_refresh_x100(output_status[0])", presenter)
        self.assertIn("const uint32_t reported_width = presenter.output_width;", presenter)
        self.assertLess(
            presenter.index("sceVideoOutIsOutputSupported", presenter.index("configure_high_refresh_output")),
            presenter.index("sceVideoOutConfigureOutput(handle", presenter.index("configure_high_refresh_output")),
        )
        self.assertIn("state->requested_fps", stream)
        self.assertIn("mode->visible_height, state->stream_fps, &hud)", stream)
        # A frame slot returns to the decoder only after its flip was observed.
        complete = stream[stream.index("static int complete_presentation(") :]
        complete = complete[: complete.index("static void *video_present_thread(")]
        self.assertLess(
            complete.index("native_agc_finish_frame()"),
            complete.index("state->frames.release(item.slot);"),
        )
        self.assertIn("if (!defer_flip)", presenter)
        self.assertIn("result = wait_for_marker(render_marker, &render_waits)", presenter)
        self.assertIn("source == presenter.pending_source ? native_agc_finish_frame() : 0", presenter)
        frame_start = presenter.index("static int present_frame(")
        self.assertLess(presenter.index("result = native_agc_finish_frame();", frame_start),
                        presenter.index("result = render_frame(", frame_start))

    def test_presenter_viewport_matches_registered_framebuffer(self):
        presenter = (ROOT / "src/native_agc_present.cpp").read_text(encoding="utf-8")

        self.assertIn("render_width = output.width;", presenter)
        self.assertIn("render_height = output.height;", presenter)
        self.assertNotIn(
            "render_width = presenter.scanout_width ? presenter.scanout_width : output.width;",
            presenter,
        )

    def test_renderer_pulls_frames_and_presents_the_newest_picture(self):
        source = (ROOT / "src/moonlight_stream.cpp").read_text(encoding="utf-8")

        self.assertIn("decode_unit->enqueueTimeUs", source)
        self.assertIn("LiGetPendingVideoFrames()", source)
        # Pull renderer: this client owns the decode thread, and no submit
        # callback may be registered alongside the capability.
        self.assertIn("LiWaitForNextVideoFrame(&handle, &decode_unit)", source)
        self.assertIn("LiCompleteVideoFrame(handle, status);", source)
        self.assertIn(".submitDecodeUnit = nullptr,", source)
        self.assertIn("CAPABILITY_PULL_RENDERER | CAPABILITY_SLICES_PER_FRAME(slices)", source)
        # moonlight-common-c writes a placeholder into the empty submit slot of
        # the caller's struct and refuses a pull renderer that has one, so the
        # slot is cleared before every connection. The host test links the real
        # FakeCallbacks.c; these lines pin the library behaviour it mirrors.
        library = ROOT / "third_party/moonlight-common-c/src"
        self.assertIn(
            "(drCallbacks->capabilities & CAPABILITY_PULL_RENDERER) && drCallbacks->submitDecodeUnit)",
            (library / "Connection.c").read_text(encoding="utf-8"),
        )
        self.assertIn(
            "(*drCallbacks)->submitDecodeUnit = fakeDrSubmitDecodeUnit;",
            (library / "FakeCallbacks.c").read_text(encoding="utf-8"),
        )
        run = source[source.index("int moonlight_stream_run(") :]
        self.assertLess(
            run.index("prepare_video_callbacks(stream_slices, mode->codec_type == 1u);"),
            run.index("connection_result = LiStartConnection("),
        )
        prepare = source[source.index("static void prepare_video_callbacks(") :]
        prepare = prepare[: prepare.index("static int moonlight_renderer_setup(")]
        self.assertIn("moonlight_video_callbacks.submitDecodeUnit = nullptr;", prepare)
        # Decoded pictures are never held back by a time rule: the newest
        # one replaces any picture the presenter has not taken yet.
        self.assertNotIn("drop_stale_presentation", source)
        self.assertIn("state->mailbox.publish(ready, &displaced)", source)
        self.assertIn("moonlight::should_drain(state->drain_enabled, state->pipeline_depth,", source)
        self.assertIn("moonlight::record_reassembly(state->reassembly_timing, decode_unit->receiveTimeUs,", source)
        self.assertIn("decode_unit->enqueueTimeUs, callback_network_us)", source)
        # Threshold/progress behavior is exercised by the host C++ policy test.

    def test_stream_sampler_uses_the_filtered_probe_variant(self):
        header = (ROOT / "include/native_agc_output.hpp").read_text(encoding="utf-8")
        source = (ROOT / "src/native_agc_present.cpp").read_text(encoding="utf-8")

        self.assertIn("kNativeAgcBilinearSamplerWord = 0x09500000u", header)
        self.assertEqual(source.count("kNativeAgcBilinearSamplerWord"), 2)
        self.assertNotIn("descriptor[18] = descriptor[22] = 0x08000000", source)

    def test_connecting_screen_stays_until_the_first_picture(self):
        stream = (ROOT / "src/moonlight_stream.cpp").read_text(encoding="utf-8")
        presenter = (ROOT / "src/native_agc_present.cpp").read_text(encoding="utf-8")
        platform = (ROOT / "src/launcher/launcher_ps5.cpp").read_text(encoding="utf-8")
        view = (ROOT / "src/launcher/launcher_view.cpp").read_text(encoding="utf-8")
        main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        run = stream[stream.index("int moonlight_stream_run(") :]

        # The launcher takes a picture of its connecting screen, without the
        # bar's fill, and the stream is given it.
        self.assertIn("CaptureConnecting(renderer, view, frame, selection);", platform)
        self.assertIn("if (!plate_ && load_progress_ > 0.0f)", view)
        self.assertIn("options.connecting = &picture;", main)
        # The stream shows it before anything slow, and moves the bar on at
        # each step of the connection.
        begin = run.index("result = connecting_screen_begin(")
        self.assertLess(begin, run.index("sceSysmoduleLoadModule(207)"))
        steps = [run.index(f"connecting_screen_stage({step}") for step in
                 ("connecting_screen.progress, 0.45f", "0.45f, 0.70f", "0.70f, 0.88f", "0.88f, 0.97f")]
        self.assertEqual(steps, sorted(steps))
        self.assertLess(steps[2], run.index("connection_result = LiStartConnection("))
        self.assertLess(run.index("connection_result = LiStartConnection("), steps[3])
        # It leaves when the first picture is about to be shown, on the thread
        # that shows it, and never later than the stream's own teardown.
        present = stream[stream.index("static void *video_present_thread(") :]
        self.assertLess(present.index("connecting_screen_finish();"),
                        present.index("submit_presentation(state, current)"))
        done = run[run.index("\ndone:") :]
        self.assertLess(done.index("connecting_screen_stop();"),
                        done.index("native_agc_present_shutdown();"))
        self.assertLess(done.index("native_agc_present_shutdown();"),
                        done.index("connecting_screen_release();"))
        # The picture is drawn edge to edge, whatever the picture-size setting.
        still = presenter[presenter.index("int native_agc_present_still(") :]
        still = still[: still.index("void native_agc_flush_source(")]
        self.assertIn("std::atomic_exchange_explicit(&tv_safe_area, 0", still)
        self.assertIn("std::atomic_store_explicit(&tv_safe_area, safe_area", still)
        # Cancelling and the set-up timeout are watched on their own thread.
        worker = stream[stream.index("static void *connection_loading_thread(") :]
        worker = worker[: worker.index("static int start_connection_loading(")]
        self.assertIn("CONNECTION_SETUP_TIMEOUT_US", worker)
        self.assertIn("LiInterruptConnection();", worker)
        self.assertNotIn("native_agc_present", worker)
        self.assertFalse((ROOT / "assets/private/loading-connecting-alpha.bin").exists())

    def test_splash_stays_until_the_launcher_has_drawn(self):
        build = (ROOT / "tools/build.sh").read_text(encoding="utf-8")
        shims = (ROOT / "src/runtime/runtime_shims.c").read_text(encoding="utf-8")
        platform = (ROOT / "src/launcher/launcher_ps5.cpp").read_text(encoding="utf-8")
        storage = (ROOT / "src/app_storage.cpp").read_text(encoding="utf-8")

        # Every request to hide the splash picture goes through the app, which
        # lets it through once the launcher's first frame is on screen.
        self.assertIn('wrap_options+=("--wrap=sceSystemServiceHideSplashScreen")', build)
        self.assertIn("int __wrap_sceSystemServiceHideSplashScreen(void)", shims)
        first = platform[platform.index("if (++frames == 1)") :]
        self.assertLess(first.index("prosperolight_release_splash();"),
                        first.index("sys::hide_splash_screen();"))
        self.assertLess(platform.index("display.swap();"), platform.index("if (++frames == 1)"))
        # Compiled shaders are kept between launches, per OpenGL runtime version, and
        # the temporary file the cache writes through is made by the app itself.
        fetch = (ROOT / "tools/fetch-opengl-sdk.sh").read_text(encoding="utf-8")
        self.assertIn('setenv("PS5_SHADER_CACHE_DIR", shaders, 1)', storage)
        version = re.search(r"^version=(\S+)$", fetch, re.M).group(1)
        self.assertIn(f'kShaderCache[] = "opengl-{version}"', storage)
        self.assertIn("int mkstemp(char *template_name)", shims)
        self.assertIn("int isatty(int descriptor)", shims)

    def test_launcher_worker_has_room_for_tls_requests(self):
        model = (ROOT / "src/launcher/launcher_model.cpp").read_text(encoding="utf-8")
        backend = (ROOT / "src/moonlight_backend.cpp").read_text(encoding="utf-8")

        # Stop and unpair nest a refresh inside a TLS request on the worker.
        self.assertIn("constexpr std::size_t kWorkerStackBytes = 1u << 20;", model)
        self.assertIn("pthread_attr_setstacksize(&attributes, kWorkerStackBytes);", model)
        action = backend[backend.index("static int run_paired_action(") :]
        self.assertLess(action.index("memset(&identity, 0, sizeof(identity));"),
                        action.index("identity_init(&identity"))

    def test_launcher_worker_stays_off_the_screens_cpu(self):
        platform = (ROOT / "src/launcher/launcher_ps5.cpp").read_text(encoding="utf-8")
        model = (ROOT / "src/launcher/launcher_model.cpp").read_text(encoding="utf-8")

        # Console threads are never time-sliced: a TLS request on the screen's
        # CPU froze the launcher. The screen gets a core, the worker the rest,
        # and the main thread's mask is given back before the stream.
        self.assertEqual(int(re.search(r"kScreenCpus = (0x[0-9a-f]+);", platform).group(1), 16)
                         & int(re.search(r"kWorkerCpus = (0x[0-9a-f]+);", platform).group(1), 16), 0)
        self.assertIn("model.set_worker_start(PlaceWorker);", platform)
        worker = model[model.index("void *Model::Worker(") :]
        self.assertLess(worker.index("model->worker_start_();"), worker.index("model->Run("))
        self.assertLess(platform.index("display.open("), platform.index("ps5_thread_affinity_set(kScreenCpus)"))
        self.assertLess(platform.index("display.close();"),
                        platform.index("(void)ps5_thread_affinity_set(main_cpus);"))

    def test_log_is_written_by_its_own_thread(self):
        storage = (ROOT / "src/app_storage.cpp").read_text(encoding="utf-8")
        report = (ROOT / "src/crash_report.cpp").read_text(encoding="utf-8")

        # Writes to /data take tens of milliseconds: the streams are buffered
        # and a thread of its own flushes them. No stream internals (fileno):
        # the console's C library lays its FILE out differently.
        opened = storage[storage.index("void open_log(") :]
        self.assertIn("pthread_create(&flusher", opened)
        self.assertIn("std::setvbuf(file, nullptr, _IOFBF", storage)
        self.assertIn("bound_log(g_log_file);", storage)
        self.assertIn("prosperolight_logs_enabled()", storage)
        self.assertNotIn("fileno(", storage)
        self.assertNotIn("dup2(", storage)
        # The crash report goes to the file itself.
        self.assertIn("(void)write(g_log_file, g_text.data, g_text.size);", report)
        self.assertNotIn("write(1,", report)

    def test_update_check_follows_the_boilerplate_kit(self):
        model = (ROOT / "src/launcher/launcher_model.cpp").read_text(encoding="utf-8")
        platform = (ROOT / "src/launcher/launcher_ps5.cpp").read_text(encoding="utf-8")
        view = (ROOT / "src/launcher/launcher_view.cpp").read_text(encoding="utf-8")
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        build = (ROOT / "tools/build.sh").read_text(encoding="utf-8")
        curl = (ROOT / "third_party/update-check/console_curl.c").read_text(encoding="utf-8")
        shims = (ROOT / "src/runtime/runtime_shims.c").read_text(encoding="utf-8")
        notices = (ROOT / "THIRD_PARTY_NOTICES.md").read_text(encoding="utf-8")

        # The kit's transport: libcurl with the fcntl wrap.
        self.assertIn("PACBREW_PACKAGES ?= libcurl", makefile)
        self.assertIn('wrap_options+=("--wrap=fcntl")', build)
        self.assertIn("int __wrap_fcntl(int descriptor, int command, ...)", curl)
        # One definition of each function the app and the kit both had.
        for name in ("int isatty(", "int mkstemp(", "FILE *popen(", "void openlog("):
            self.assertNotIn(name, curl)
            self.assertIn(name, shims)
        self.assertNotIn("int getaddrinfo(", curl)
        # Asked once per launch, on the worker, when nothing else waits; the
        # app's own param.json is read from where the app really is.
        self.assertIn("if (first_start)\n                model.set_update_check(CheckForUpdate);", platform)
        check = platform[platform.index("bool CheckForUpdate(") :]
        self.assertIn('std::string(storage::paths().app) + "/sce_sys/param.json"', check)
        self.assertNotIn("update_check_run_self", platform)
        next_job = model[model.index("bool Model::StartNext()") :]
        self.assertLess(next_job.index("next.kind = JobKind::artwork;"),
                        next_job.index("next.kind = JobKind::update;"))
        # Only an available update is shown, for ten seconds.
        self.assertIn("constexpr float kUpdateNoticeSeconds = 10.0f;", model)
        self.assertIn("if (result.update_available)", model)
        self.assertIn("notice.seconds > 0.0f", view)
        # Shipping libcurl means shipping its notices.
        for name in ("libcurl", "OpenSSL", "zlib", "zstd", "libpsl"):
            self.assertIn(name, notices)
        for name in ("curl.txt", "openssl.txt", "zlib.txt", "zstd.txt", "libpsl.txt"):
            self.assertTrue((ROOT / "third_party/licenses" / name).is_file(), name)

    def test_crash_report_is_installed_first(self):
        main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        report = (ROOT / "src/crash_report.cpp").read_text(encoding="utf-8")
        script = (ROOT / "tooling/native/ps5-pie.ld").read_text(encoding="utf-8")
        tool = (ROOT / "tools/symbolize-crash.py").read_text(encoding="utf-8")

        body = main[main.index("int main()") :]
        self.assertLess(body.index("storage::Initialize();"), body.index("crash::Install("))
        self.assertLess(body.index("crash::Install("), body.index("launcher::Run("))
        for name in ("SIGSEGV", "SIGBUS", "SIGILL", "SIGFPE", "SIGABRT"):
            self.assertIn(f"{{{name}, \"{name}\"", report)
        self.assertIn("PROVIDE_HIDDEN(__pl_text_start = .);", script)
        self.assertIn("PROVIDE_HIDDEN(__pl_text_end = .);", script)
        self.assertIn("(code size 0x", report)
        self.assertIn("code size 0x", tool)

    def test_release_metadata_preserves_hdr_and_high_resolution_hfr_capabilities(self):
        configured = json.loads(
            (ROOT / "sce_sys/param.json").read_text(encoding="utf-8")
        )
        self.assertEqual(configured["applicationCategoryType"], 0)
        self.assertEqual(configured["attribute"], 0x62000000)
        self.assertEqual(configured["attribute2"], 0)
        self.assertEqual(configured["attribute3"], 0x80040)

    def test_release_workflow_publishes_ffpfsc_folder_zip_and_checksums(self):
        workflow = (ROOT / ".github/workflows/tooling.yml").read_text(
            encoding="utf-8"
        )
        self.assertIn('"Jinja2==3.1.6" "jsonschema==4.25.1"', workflow)
        self.assertIn('python3 -m zipfile -c "$TITLE_ID.zip" "$TITLE_ID"', workflow)
        self.assertIn(
            'sha256sum "$TITLE_ID.ffpfsc" "$TITLE_ID.zip" > SHA256SUMS',
            workflow,
        )
        self.assertIn("dist/${{ env.TITLE_ID }}.zip", workflow)
        self.assertIn("dist/SHA256SUMS", workflow)
        self.assertIn(
            "Expected one FFPFSC image, one app-folder ZIP, and SHA256SUMS.",
            workflow,
        )
        self.assertIn("find . -type f -name 'PPSA*.ffpfsc' -print0", workflow)
        self.assertIn("find . -type f -name 'PPSA*.zip' -print0", workflow)
        self.assertIn("find . -type f -name 'SHA256SUMS' -print0", workflow)
        self.assertIn('sha256sum -c "$(basename "${checksums[0]}")"', workflow)
        self.assertIn(
            'assets=("release/$IMAGE" "release/$ARCHIVE" "release/$CHECKSUM")',
            workflow,
        )
        self.assertIn("gh release delete-asset", workflow)
        self.assertIn('awk -v version="$GITHUB_REF_NAME"', workflow)
        self.assertEqual(workflow.count("--notes-file release-notes.md"), 2)
        self.assertNotIn(".ffpkg", workflow)

    def test_readme_bitrate_limits_match_the_measured_model(self):
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        result = subprocess.run(
            [sys.executable, str(ROOT / "tools/plot-bitrate-limits.py"), "--check"],
            capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("120 FPS: smooth up to 80 Mbps, limit 100 Mbps (measured", result.stdout)
        self.assertIn(" 90 FPS: smooth up to 115 Mbps, limit 145 Mbps (measured", result.stdout)
        self.assertIn(" 60 FPS: smooth up to 115 Mbps, limit 190 Mbps (extrapolated", result.stdout)
        self.assertIn("> [!WARNING]", readme)
        self.assertIn("](docs/images/bitrate-limits.svg)", readme)
        self.assertIn("| 120 FPS | 80 Mbps | 100 Mbps | **80 Mbps** or lower |", readme)
        self.assertIn("| 90 FPS | 115 Mbps | 145 Mbps | **100 Mbps** or lower |", readme)
        self.assertIn("| 60 FPS | 115 Mbps | about 190 Mbps (extrapolated) |", readme)

    def test_readme_tracks_identity_and_stream_shortcuts(self):
        configured = json.loads(
            (ROOT / "sce_sys/param.json").read_text(encoding="utf-8")
        )
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        self.assertIn(configured["titleId"], readme)
        self.assertIn(configured["contentVersion"], readme)
        for button, label in (("L1", "l1"), ("R1", "r1"), ("Square", "square"),
                              ("Triangle", "triangle")):
            self.assertIn(f"| ![Touchpad][touchpad] + ![{button}][{label}] |", readme)

    def test_stream_shortcuts_are_named_after_the_touchpad(self):
        # The button was once written "Select"; the changelog keeps one note of it.
        old_name = re.compile(r"Select ?\+")
        for name in ("README.md", "docs/TROUBLESHOOTING.md", "docs/VALIDATION.md",
                     "docs/PERFORMANCE_ROUND_3.md", "src/launcher/launcher_view.cpp",
                     "src/moonlight_stream.cpp", "include/moonlight_stream_input.hpp"):
            text = (ROOT / name).read_text(encoding="utf-8")
            self.assertIsNone(old_name.search(text), name)
        changelog = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
        self.assertEqual(len(old_name.findall(changelog)), 1)
        stream = (ROOT / "src/moonlight_stream.cpp").read_text(encoding="utf-8")
        self.assertIn("Touchpad + Square switches to %s.", stream)

    def test_buttons_are_shown_as_icons(self):
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        result = subprocess.run(
            [sys.executable, str(ROOT / "tools/generate-button-icons.py"), "--check"],
            capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        references = dict(re.findall(r"^\[(\w+)\]: (docs/images/buttons/\w+\.svg) ", readme, re.M))
        self.assertEqual(len(references), 12)
        for label, path in references.items():
            self.assertTrue((ROOT / path).is_file(), path)
            self.assertIn(f"][{label}]", readme)
        controls = readme[readme.index("## Controls"):]
        controls = controls[: controls.index("[cross]:")]
        for name in ("Cross", "Circle", "Square", "Triangle", "Options", "L1", "R1", "D-pad"):
            self.assertIsNone(re.search(rf"(?<!\[){name}(?!\])", controls), name)

        # The launcher draws the buttons' glyphs and names none of them in text.
        view = (ROOT / "src/launcher/launcher_view.cpp").read_text(encoding="utf-8")
        self.assertEqual(view.count("ui::Button::touchpad"), 4)
        self.assertIn("ui::draw_hints(list, canvas.fonts, glyphs(paint), hints, count, kRight, true);",
                      view)
        for text in re.findall(r'"((?:[^"\\]|\\.)*)"', view):
            for name in ("Cross", "Circle", "Square", "Triangle", "Options", "Touchpad", "L1", "R1"):
                self.assertIsNone(re.search(rf"\b{name}\b", text), text)

    def test_launcher_has_four_screens(self):
        view = (ROOT / "src/launcher/launcher_view.cpp").read_text(encoding="utf-8")
        platform = (ROOT / "src/launcher/launcher_ps5.cpp").read_text(encoding="utf-8")
        self.assertIn('tabs_.set_tabs({{"PCs"}, {"Games"}, {"Settings"}, {"About"}});', view)
        self.assertIn("constexpr int kScreens = 4;", view)
        # About gives credit, as ProsperoEden's page does, and names the folders.
        about = view[view.index("void View::draw_about(") : view.index("void View::draw_pairing(")]
        for text in ("Powered by Moonlight", "moonlight-stream.org", "Sunshine developers",
                     "brought to you by BlackBearReloaded", "made with ElevenLabs",
                     '"Version " + version_', "files_.draw(canvas);"):
            self.assertIn(text, about)
        self.assertIn("view.set_storage(StorageFolders());", platform)
        self.assertIn('form_.add_header("Diagnostics");', view)

    def test_every_sunshine_request_uses_the_port_saved_for_that_pc(self):
        config = (ROOT / "include/moonlight_config.hpp").read_text(encoding="utf-8")
        backend = (ROOT / "src/moonlight_backend.cpp").read_text(encoding="utf-8")
        app = (ROOT / "src/launcher/launcher_model.cpp").read_text(encoding="utf-8")
        platform = (ROOT / "src/launcher/launcher_ps5.cpp").read_text(encoding="utf-8")
        launcher = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        stream = (ROOT / "src/moonlight_stream.cpp").read_text(encoding="utf-8")
        client = (ROOT / "src/gamestream/client.c").read_text(encoding="utf-8")

        # One default, in the configuration and in the protocol client.
        self.assertIn("#define MOONLIGHT_CONFIG_DEFAULT_HTTP_PORT 47989U", config)
        self.assertIn("server_endpoint_parse(address, server->address, sizeof(server->address), http_port,", client)
        self.assertIn("uint16_t value = fallback_port ? fallback_port : 47989;", (ROOT / "include/server_endpoint.h").read_text())
        for source in (backend, app, launcher, stream):
            self.assertNotIn("47989)", source)
        calls = [line for line in backend.splitlines() if "gs_init(" in line]
        self.assertEqual(len(calls), 3)
        for call in calls:
            self.assertIn("http_port);", call)
        self.assertIn("result = gs_init(server, identity, host, host_port);", stream)
        self.assertIn("selection->host_port = moonlight_config_host_port(host);", platform)
        self.assertIn("options.host_port = selection.host_port;", launcher)
        # Every request to a PC carries that PC's port, and an answer from
        # another endpoint than the selected one never becomes its state.
        self.assertEqual(app.count("job.port = moonlight_config_host_port("), 4)
        self.assertEqual(app.count("next.port = moonlight_config_host_port("), 2)
        self.assertIn("moonlight_backend_refresh(job.host, job.port, &result->snapshot)", app)
        self.assertIn("if (!IsSelected(job.host, job.port))", app)

    def test_pcs_page_offers_the_port_of_the_selected_pc(self):
        view = (ROOT / "src/launcher/launcher_view.cpp").read_text(encoding="utf-8")
        model = (ROOT / "src/launcher/launcher_model.cpp").read_text(encoding="utf-8")

        self.assertIn('host_actions_.set_items({{"Change port"}, {"Unpair"}});', view)
        self.assertIn('port_prompt_.set_title("Sunshine port");', view)
        self.assertIn("port_prompt_.keyboard.set_layouts({ui::KeyboardLayout::numeric()});", view)
        # An empty entry means Sunshine's default port.
        self.assertIn("port_prompt_.style.allow_empty = true;", view)
        self.assertIn("model_.SetPort(text.c_str(), &error)", view)
        set_port = model[model.index("bool Model::SetPort(") : model.index("void Model::StopApp()")]
        # Requests queued for the old endpoint are dropped when the port changes.
        self.assertLess(set_port.index("moonlight_config_set_host_port("),
                        set_port.index("DropSelectedRequests();"))
        self.assertIn("QueueSelectedRefresh();", set_port)

    def test_stream_forwards_a_pad_for_every_signed_in_user(self):
        stream = (ROOT / "src/moonlight_stream.cpp").read_text()
        controller = (ROOT / "src/ps5_dualsense.cpp").read_text()
        self.assertIn("prosperolight::dualsense::Poll();", stream)
        self.assertIn("controller_ready ? prosperolight::dualsense::ActiveMask() : 0", stream)
        self.assertIn("sceUserServiceGetLoginUserIdList(users)", controller)
        self.assertIn("LiSendControllerArrivalEvent(index, active_mask", controller)
        self.assertNotIn("LiSendMultiControllerEvent(0, 1,", stream + controller)
        self.assertNotIn("ps5_controller_launch_mask", stream)
        self.assertIn("controller_summary = prosperolight::dualsense::GetStatistics();", stream)

    def test_games_show_no_apps_while_the_first_answer_is_pending(self):
        view = (ROOT / "src/launcher/launcher_view.cpp").read_text(encoding="utf-8")
        model = (ROOT / "src/launcher/launcher_model.cpp").read_text(encoding="utf-8")
        games = view[view.index("void View::draw_games(") : view.index("void View::draw_settings(")]

        self.assertIn("backend_valid_ = false;", model)
        self.assertIn(
            "const bool waiting = host && (!model_.backend_valid() || model_.busy() == Busy::refreshing);",
            games,
        )
        self.assertIn('"Asking the PC for its apps"', games)

    def test_launcher_sounds_come_from_the_widgets(self):
        platform = (ROOT / "src/launcher/launcher_ps5.cpp").read_text(encoding="utf-8")
        view = (ROOT / "src/launcher/launcher_view.cpp").read_text(encoding="utf-8")
        stream = (ROOT / "src/moonlight_stream.cpp").read_text(encoding="utf-8")

        # Each widget asks for a cue; the launcher plays it from its sound set.
        self.assertIn("for (const audio::CueEvent &event : feedback.cues)", platform)
        self.assertIn("sounds.play(mixer,", platform)
        self.assertIn("t.sounds = audio::SoundSet::glass;", view)
        recordings = sorted(path.name for path in (ROOT / "assets/audio/sfx/glass").glob("*.wav"))
        for cue in ("focus", "select", "back", "tab", "toggle", "slider", "error", "notify",
                    "modal_open", "modal_close", "launch", "welcome", "saved"):
            self.assertTrue(any(name.startswith(cue + "_") for name in recordings), cue)
        # The audio port is closed before the stream opens its own.
        self.assertIn("audio_out.stop();", platform)
        self.assertNotIn("SoundBank", stream)

    def run_init(self, param, **values):
        environment = os.environ.copy()
        environment.update(values)
        return subprocess.run(
            ["bash", str(ROOT / "tools/init-project.sh"), str(param)],
            cwd=ROOT,
            env=environment,
            check=False,
            capture_output=True,
            text=True,
        )

    def test_init_coordinates_media_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            param = Path(directory) / "param.json"
            param.write_text(
                json.dumps(
                    {
                        "contentId": "UP9000-PPSA99999_00-HELLOWORLD000001",
                        "localizedParameters": {
                            "defaultLanguage": "en-US",
                            "en-US": {"titleName": "Old"},
                        },
                        "gameIntent": {"permittedIntents": [{"intentType": "launchActivity"}]},
                    }
                ),
                encoding="utf-8",
            )
            result = self.run_init(
                param,
                TITLE_ID="PPSA12345",
                APP_NAME="Moon Client",
                APP_CATEGORY="media",
                CONTENT_SUFFIX="MOONCLIENT000001",
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            configured = json.loads(param.read_text(encoding="utf-8"))
            self.assertEqual(configured["titleId"], "PPSA12345")
            self.assertEqual(configured["conceptId"], "12345")
            self.assertEqual(configured["contentId"], "UP9000-PPSA12345_00-MOONCLIENT000001")
            self.assertEqual(configured["localizedParameters"]["en-US"]["titleName"], "Moon Client")
            self.assertEqual(configured["applicationCategoryType"], 65536)
            self.assertEqual(configured["contentBadgeType"], 2)
            self.assertNotIn("gameIntent", configured)

    def test_init_rejects_invalid_title_without_rewriting(self):
        with tempfile.TemporaryDirectory() as directory:
            param = Path(directory) / "param.json"
            original = '{"contentId":"UP9000-PPSA99999_00-HELLOWORLD000001"}\n'
            param.write_text(original, encoding="utf-8")
            result = self.run_init(param, TITLE_ID="PPSA12", APP_NAME="Broken")
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(param.read_text(encoding="utf-8"), original)

    def test_init_derives_game_suffix_and_preserves_mode(self):
        with tempfile.TemporaryDirectory() as directory:
            param = Path(directory) / "param.json"
            param.write_text(
                json.dumps(
                    {
                        "contentId": "UP9000-PPSA99999_00-HELLOWORLD000001",
                        "localizedParameters": {
                            "defaultLanguage": "en-US",
                            "en-US": {"titleName": "Old"},
                        },
                    }
                ),
                encoding="utf-8",
            )
            param.chmod(0o640)
            result = self.run_init(
                param, TITLE_ID="PPSA54321", APP_NAME="Native Sample", CONTENT_SUFFIX=""
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            configured = json.loads(param.read_text(encoding="utf-8"))
            self.assertEqual(configured["contentId"], "UP9000-PPSA54321_00-NATIVESAMPLE0000")
            self.assertEqual(configured["applicationCategoryType"], 0)
            self.assertEqual(configured["contentBadgeType"], 1)
            self.assertEqual(
                configured["gameIntent"]["permittedIntents"],
                [{"intentType": "launchActivity"}],
            )
            self.assertEqual(param.stat().st_mode & 0o777, 0o640)

    def test_undeploy_dry_run_resolves_only_current_title(self):
        environment = os.environ.copy()
        environment.update(PS5_HOST="192.0.2.1", DEPLOY_DRY_RUN="1")
        result = subprocess.run(
            ["bash", str(ROOT / "tools/deploy.sh"), "undeploy"],
            cwd=ROOT,
            env=environment,
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        title_id = json.loads(
            (ROOT / "sce_sys/param.json").read_text(encoding="utf-8")
        )["titleId"]
        self.assertIn(f"/data/homebrew/{title_id}/", result.stdout)
        self.assertIn(f"{title_id}.{{ffpkg,ffpfsc}}", result.stdout)
        self.assertIn("no network request was sent", result.stdout)

    def test_deploy_dry_run_uses_mocked_build_and_no_network(self):
        with tempfile.TemporaryDirectory() as directory:
            sandbox = Path(directory)
            (sandbox / "tools").mkdir()
            (sandbox / "sce_sys").mkdir()
            shutil.copy2(ROOT / "tools/deploy.sh", sandbox / "tools/deploy.sh")
            (sandbox / "sce_sys/param.json").write_text(
                '{"titleId":"PPSA12345"}\n', encoding="utf-8"
            )

            mock_bin = sandbox / "mock-bin"
            mock_bin.mkdir()
            mock_make = mock_bin / "make"
            mock_make.write_text(
                "#!/usr/bin/env bash\n"
                "mkdir -p \"$MOCK_ROOT/dist\"\n"
                "printf package > \"$MOCK_ROOT/dist/PPSA12345.ffpkg\"\n",
                encoding="utf-8",
            )
            mock_make.chmod(0o755)

            environment = os.environ.copy()
            environment.update(
                PS5_HOST="192.0.2.1",
                DEPLOY_DRY_RUN="1",
                DEPLOY_FORMAT="ffpkg",
                MOCK_ROOT=str(sandbox),
                PATH=f"{mock_bin}{os.pathsep}{environment['PATH']}",
            )
            result = subprocess.run(
                ["bash", str(sandbox / "tools/deploy.sh")],
                cwd=sandbox,
                env=environment,
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("/data/homebrew/PPSA12345.ffpkg", result.stdout)
            self.assertIn("no network request was sent", result.stdout)


if __name__ == "__main__":
    unittest.main()
