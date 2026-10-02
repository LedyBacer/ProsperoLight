#!/usr/bin/env python3
# ps5-native-app-boilerplate - Host tooling regression tests.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Exercises identity initialization and deployment resolution without a console.

import json
import os
from pathlib import Path
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
        source = (ROOT / "src/moonlight_app.cpp").read_text(encoding="utf-8")
        start = source.index("void MoonlightApp::FinishStopActiveApp()")
        end = source.index("void MoonlightApp::RefreshBackend(", start)
        stop = source[start:end]

        self.assertIn("moonlight_backend_snapshot_t refreshed{};", stop)
        self.assertIn(
            "moonlight_backend_stop_app(SelectedHostAddress(), &refreshed)", stop
        )
        self.assertIn("if (result == 0 || refreshed.app_count)", stop)
        self.assertNotIn(
            "moonlight_backend_stop_app(SelectedHostAddress(), &backend_)", stop
        )

    def test_stop_status_renders_before_the_sunshine_request(self):
        source = (ROOT / "src/moonlight_app.cpp").read_text(encoding="utf-8")
        start = source.index("void MoonlightApp::RequestStopActiveApp()")
        end = source.index("void MoonlightApp::FinishStopActiveApp()", start)
        request = source[start:end]

        self.assertIn('SetText(document_, "stop-app-label", "Stopping...")', request)
        self.assertIn("stop_due_ms_ = SDL_GetTicks64() + 50", request)
        self.assertNotIn("moonlight_backend_stop_app", request)

    def test_hdr_supports_every_stream_resolution(self):
        stream = (ROOT / "src/moonlight_stream.cpp").read_text(encoding="utf-8")
        modes = stream[stream.index("static const native_video_mode_t video_modes[]") :]
        modes = modes[: modes.index("static_assert")]
        self.assertEqual(modes.count("VIDEO_FORMAT_H265_MAIN10"), 3)
        self.assertIn("MOONLIGHT_STREAM_RESOLUTION_1440P, VIDEO_FORMAT_H265_MAIN10", modes)
        self.assertIn("MOONLIGHT_STREAM_RESOLUTION_2160P, VIDEO_FORMAT_H265_MAIN10", modes)

        launcher = (ROOT / "src/moonlight_app.cpp").read_text(encoding="utf-8")
        settings = launcher[launcher.index("case Screen::Settings:") :]
        settings = settings[: settings.index("default:")]
        resolution = settings[settings.index("else if (focus_ == 4)") :]
        resolution = resolution[: resolution.index("else if (focus_ == 5)")]
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
        ui = (ROOT / "ui/main.rml").read_text(encoding="utf-8")

        for fps in (60, 90, 120):
            self.assertIn(f"#define MOONLIGHT_STREAM_FPS_{fps} {fps}U", config)
        self.assertIn('id="setting-framerate"', ui)
        self.assertIn("selection->stream_fps = app.StreamFps();", launcher)
        self.assertIn("options.stream_fps = selection.stream_fps;", launcher)
        self.assertIn("stream_config.fps = (int)stream_fps;", stream)
        self.assertIn(
            "moonlight::client_refresh_x100(stream_fps, loading.output_refresh_x100)", stream
        )
        self.assertIn("stream_config.clientRefreshRateX100 = (int)renderer.client_refresh_x100;", stream)
        negotiation = stream.index("stream_config.clientRefreshRateX100 =")
        self.assertLess(stream.index("start_connection_loading(&loading, frame_memory"), negotiation)
        self.assertLess(negotiation, stream.index("result = prepare_native_session(&client_identity"))
        self.assertLess(negotiation, stream.index("connection_result = LiStartConnection("))
        loading = stream[stream.index("static int start_connection_loading("):]
        self.assertLess(loading.index("native_agc_output_status("), loading.index("pthread_create("))
        self.assertIn("redraw_rate != (int)state->stream_fps", stream)

    def test_selectable_surround_audio_reaches_sunshine_and_ps5_audioout(self):
        config = (ROOT / "include/moonlight_config.hpp").read_text(encoding="utf-8")
        launcher = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        stream = (ROOT / "src/moonlight_stream.cpp").read_text(encoding="utf-8")
        ui = (ROOT / "ui/main.rml").read_text(encoding="utf-8")

        self.assertIn("#define MOONLIGHT_AUDIO_51_SURROUND 1U", config)
        self.assertIn('id="setting-audio"', ui)
        self.assertIn("selection->audio_configuration = app.AudioConfiguration();", launcher)
        self.assertIn("options.audio_configuration = selection.audio_configuration;", launcher)
        self.assertIn("audio_configuration = AUDIO_CONFIGURATION_51_SURROUND;", stream)
        self.assertIn("#define PS5_AUDIO_FORMAT_S16_8CH 2", stream)
        self.assertIn("opus->channelCount == AUDIO_51_CHANNELS", stream)
        self.assertIn("state->output_channels - state->channels", stream)

    def test_frame_rate_is_independent_of_resolution_and_bitrate(self):
        source = (ROOT / "src/moonlight_app.cpp").read_text(encoding="utf-8")
        settings = source[source.index("case Screen::Settings:") :]
        settings = settings[: settings.index("default:")]
        resolution = settings[settings.index("else if (focus_ == 4)") :]
        resolution = resolution[: resolution.index("else if (focus_ == 5)")]
        frame_rate = settings[settings.index("else if (focus_ == 5)") :]
        frame_rate = frame_rate[: frame_rate.index("else if (focus_ == 6)")]

        self.assertNotIn("stream_fps", resolution)
        self.assertIn("config_.stream_fps = NextFrameRate(config_.stream_fps);", frame_rate)
        self.assertNotIn("stream_resolution", frame_rate)
        self.assertNotIn("bitrate", frame_rate)

    def test_high_refresh_self_test_is_compile_time_disabled(self):
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        app = (ROOT / "src/moonlight_app.cpp").read_text(encoding="utf-8")

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
            "config_.stream_resolution = PROSPEROLIGHT_STREAM_SELF_TEST_RESOLUTION", app
        )
        poll_body = app[
            app.index("void MoonlightApp::Poll()") : app.index("void MoonlightApp::Shutdown()")
        ]
        self.assertNotIn("moonlight_config_save(&config_)", poll_body)

    def test_launcher_presents_before_refreshing_sunshine(self):
        app = (ROOT / "src/moonlight_app.cpp").read_text(encoding="utf-8")
        launcher = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        initialize = app[app.index("bool MoonlightApp::Initialize(") :]
        initialize = initialize[: initialize.index("void MoonlightApp::ShowStreamError")]
        run_launcher = launcher[launcher.index("MoonlightApp::Command RunLauncher(") :]
        run_launcher = run_launcher[: run_launcher.index("} // namespace")]

        self.assertNotIn("RefreshBackend();", initialize)
        self.assertIn("initial_discovery_pending_ = true;", initialize)
        self.assertNotIn("PresentColor(renderer, window, 2, 9, 20);", run_launcher)
        self.assertLess(
            run_launcher.index("PresentLauncher(context, renderer, window);"),
            run_launcher.index("sceSystemServiceHideSplashScreen();"),
        )

    def test_launcher_bridges_the_hfr_hdmi_resync_with_app_side_artwork(self):
        launcher = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        markup = (ROOT / "ui/main.rml").read_text(encoding="utf-8")
        styles = (ROOT / "ui/styles/app.rcss").read_text(encoding="utf-8")

        self.assertIn('id="startup-splash"', markup)
        self.assertIn('src="startup-background.tga"', markup)
        self.assertIn("#startup-splash", styles)
        self.assertIn("document && play_open_sound", launcher)
        self.assertIn("kStartupHoldMilliseconds = 1500", launcher)
        self.assertIn("kStartupFadeFrames = 45", launcher)
        self.assertIn('startup_splash->SetProperty("opacity", opacity);', launcher)
        self.assertIn('startup_splash->SetClass("hidden", true);', launcher)
        self.assertTrue((ROOT / "ui/startup-background.tga").is_file())

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
        early_loading = source.index(
            "start_connection_loading(&loading, NULL, 0, profile.hdr, stream_width, stream_height,"
        )
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
        self.assertIn("if (requested_fps == 90u)", presenter)
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

    def test_loading_labels_match_the_embedded_dimensions(self):
        expected = {
            "loading-prosperolight-alpha.bin": (232, 35),
            "loading-connecting-alpha.bin": (287, 52),
        }
        for name, (width, height) in expected.items():
            self.assertEqual((ROOT / "assets/private" / name).stat().st_size, width * height)

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
        for shortcut in (
            "Select + L1",
            "Select + R1",
            "Select + Square",
            "Select + Triangle",
        ):
            self.assertIn(shortcut, readme)

    def test_launcher_has_no_diagnostics_page(self):
        markup = (ROOT / "ui/main.rml").read_text(encoding="utf-8")
        styles = (ROOT / "ui/styles/app.rcss").read_text(encoding="utf-8")
        header = (ROOT / "include/moonlight_app.hpp").read_text(encoding="utf-8")
        source = (ROOT / "src/moonlight_app.cpp").read_text(encoding="utf-8")

        self.assertEqual(markup.count('id="nav-'), 3)
        self.assertNotIn("nav-diagnostics", markup + styles + source)
        self.assertNotIn("screen-diagnostics", markup + source)
        self.assertNotIn("Screen::Diagnostics", source)
        self.assertNotIn("Diagnostics,", header)
        self.assertFalse((ROOT / "ui/chrome/panel-diagnostic.tga").exists())

    def test_launcher_screens_share_the_same_body_top(self):
        styles = (ROOT / "ui/styles/app.rcss").read_text(encoding="utf-8")
        for selector in (".host-card", ".game-card-0", ".setting-row-0"):
            rule = styles[styles.index(selector) :]
            rule = rule[: rule.index("}")]
            self.assertIn("top: 164px;", rule)

    def test_settings_rows_fit_above_the_footer(self):
        markup = (ROOT / "ui/main.rml").read_text(encoding="utf-8")
        styles = (ROOT / "ui/styles/app.rcss").read_text(encoding="utf-8")

        self.assertEqual(markup.count('class="button-chrome setting-chrome'), 22)
        self.assertEqual(markup.count('width="1380" height="88"'), 22)
        self.assertIn(".setting-chrome { width: 1380px; height: 64px; }", styles)
        self.assertIn(".setting-row { position: absolute; left: 34px; width: 1380px; height: 60px;", styles)
        # Eleven rows on a 61-pixel pitch end at 834; the note and footer follow.
        self.assertIn(".setting-row-10 { top: 774px; }", styles)
        self.assertIn(".settings-note { position: absolute; left: 34px; top: 850px;", styles)
        self.assertIn("#controller-footer { position: absolute; left: 0px; top: 1004px;", styles)
        for setting in ("chroma", "vsync", "decoder", "cores"):
            self.assertIn(f'id="setting-{setting}"', markup)
            self.assertIn(f'id="setting-{setting}-value"', markup)

    def test_games_hide_placeholder_catalog_while_initial_refresh_is_pending(self):
        markup = (ROOT / "ui/main.rml").read_text(encoding="utf-8")
        source = (ROOT / "src/moonlight_app.cpp").read_text(encoding="utf-8")
        initialize = source[source.index("bool MoonlightApp::Initialize(") :]
        initialize = initialize[: initialize.index("void MoonlightApp::ShowStreamError")]
        update_games = source[source.index("void MoonlightApp::UpdateGames()") :]
        update_games = update_games[: update_games.index("void MoonlightApp::UpdateSettings()")]

        for slot in range(6):
            self.assertIn(
                f'id="app-card-{slot}" class="game-card game-card-{slot} hidden"',
                markup,
            )
        self.assertIn("UpdateGames();", initialize)
        self.assertIn("const bool refreshing =", update_games)
        self.assertIn('"REFRESHING APPLICATIONS..."', update_games)
        self.assertIn('"Refreshing the Sunshine application list..."', update_games)

    def test_game_actions_match_the_selected_app_panel_edges(self):
        styles = (ROOT / "ui/styles/app.rcss").read_text(encoding="utf-8")
        self.assertIn(".games-stop { left: 884px;", styles)
        self.assertIn(".games-back { left: 1164px;", styles)

    def test_launcher_quiets_process_scoped_ui_audio_before_streaming(self):
        launcher = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        stream = (ROOT / "src/moonlight_stream.cpp").read_text(encoding="utf-8")
        sound = (ROOT / "src/ui_sound.cpp").read_text(encoding="utf-8")
        start = launcher.index("MoonlightApp::Command RunLauncher(")
        end = launcher.index("} // namespace", start)
        run_launcher = launcher[start:end]

        self.assertIn("prosperolight::ui_sound_clear_for_stream();", run_launcher)
        self.assertIn("SDL_QuitSubSystem(SDL_INIT_VIDEO);", run_launcher)
        clear = sound[sound.index("void ui_sound_clear_for_stream()") :]
        self.assertIn("SDL_ClearQueuedAudio(device);", clear)
        self.assertNotIn("SDL_CloseAudioDevice", clear)
        self.assertNotIn("ui_sound_clear_for_stream", stream)

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
