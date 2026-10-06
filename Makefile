# ps5-native-app-boilerplate - Linux/WSL build entry points.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

SHELL := /bin/bash
.DEFAULT_GOAL := app

-include .env

PYROWAVE ?= 1
LAN_TELEMETRY ?= 0
STREAM_SELF_TEST_FPS ?= 0
STREAM_SELF_TEST_RESOLUTION ?= 0
STREAM_SELF_TEST_CODEC ?= 1
# Development only: the update offer comes from update-offer.txt in the app's folder.
UPDATE_DEV_OFFER ?= 0
# With it: seconds after which the build accepts the offer by itself (0: the player answers).
UPDATE_AUTO_ACCEPT ?= 0
VIDEO_OUTPUT_SELF_TEST_FPS ?= 0
STOP_ACTIVE_APP_SELF_TEST ?= 0
# Tested one-flip overlap is on; dependency and audio experiments remain off.
FEC_SIMD ?= 0
OPUS_SIMD ?= 0
AUDIO_MAX_BACKLOG_MS ?= 0
PRESENT_OVERLAP ?= 1
FLIP_POLL_US ?= 500
PERFORMANCE_DETAIL ?= 0
# Slices above 1080p, adaptive Videodec2 depth (the Classic setting is depth 1).
VIDEO_SLICES_PER_FRAME ?= 8
DECODER_PIPELINE_DEPTH ?= 3
DECODER_CPU_PRIORITY ?= 700
INPUT_POLL_US ?= 2000
# Opt-in experiments: GPU render timestamps, keyframe catch-up after this many
# queued frames, and reference-frame invalidation instead of keyframes.
GPU_TIMESTAMPS ?= 0
CATCHUP_QUEUE_FRAMES ?= 0
REFERENCE_FRAME_INVALIDATION ?= 0
# Six-picture decoder slot. Off by default; Punktfunk's GameStream encoder needs it.
EXPERIMENTAL_HOST_DPB ?= 0
# Milliseconds the display is left alone after a stream above 60 Hz or in HDR.
HFR_SETTLE_MS ?= 5000
APP_DEFINITIONS ?= GL_GLEXT_PROTOTYPES=1
APP_DEFINITIONS += PROSPEROLIGHT_HFR_SETTLE_MS=$(HFR_SETTLE_MS)
APP_DEFINITIONS += PROSPEROLIGHT_PYROWAVE=$(PYROWAVE)
APP_DEFINITIONS += PROSPEROLIGHT_LAN_TELEMETRY=$(LAN_TELEMETRY)
APP_DEFINITIONS += PROSPEROLIGHT_STREAM_SELF_TEST_FPS=$(STREAM_SELF_TEST_FPS)
APP_DEFINITIONS += PROSPEROLIGHT_STREAM_SELF_TEST_RESOLUTION=$(STREAM_SELF_TEST_RESOLUTION)
APP_DEFINITIONS += PROSPEROLIGHT_STREAM_SELF_TEST_CODEC=$(STREAM_SELF_TEST_CODEC)
APP_DEFINITIONS += PROSPEROLIGHT_UPDATE_DEV_OFFER=$(UPDATE_DEV_OFFER)
APP_DEFINITIONS += PROSPEROLIGHT_UPDATE_AUTO_ACCEPT=$(UPDATE_AUTO_ACCEPT)
APP_DEFINITIONS += PROSPEROLIGHT_VIDEO_OUTPUT_SELF_TEST_FPS=$(VIDEO_OUTPUT_SELF_TEST_FPS)
APP_DEFINITIONS += PROSPEROLIGHT_STOP_ACTIVE_APP_SELF_TEST=$(STOP_ACTIVE_APP_SELF_TEST)
APP_DEFINITIONS += PROSPEROLIGHT_FEC_SIMD=$(FEC_SIMD)
APP_DEFINITIONS += PROSPEROLIGHT_OPUS_SIMD=$(OPUS_SIMD)
APP_DEFINITIONS += PROSPEROLIGHT_AUDIO_MAX_BACKLOG_MS=$(AUDIO_MAX_BACKLOG_MS)
APP_DEFINITIONS += PROSPEROLIGHT_PRESENT_OVERLAP=$(PRESENT_OVERLAP)
APP_DEFINITIONS += PROSPEROLIGHT_FLIP_POLL_US=$(FLIP_POLL_US)
APP_DEFINITIONS += PROSPEROLIGHT_PERFORMANCE_DETAIL=$(PERFORMANCE_DETAIL)
APP_DEFINITIONS += VIDEO_SLICES_PER_FRAME=$(VIDEO_SLICES_PER_FRAME)
APP_DEFINITIONS += DECODER_PIPELINE_DEPTH=$(DECODER_PIPELINE_DEPTH)
APP_DEFINITIONS += DECODER_CPU_PRIORITY=$(DECODER_CPU_PRIORITY)
APP_DEFINITIONS += INPUT_POLL_US=$(INPUT_POLL_US)
APP_DEFINITIONS += PROSPEROLIGHT_GPU_TIMESTAMPS=$(GPU_TIMESTAMPS)
APP_DEFINITIONS += PROSPEROLIGHT_CATCHUP_QUEUE_FRAMES=$(CATCHUP_QUEUE_FRAMES)
APP_DEFINITIONS += PROSPEROLIGHT_REFERENCE_FRAME_INVALIDATION=$(REFERENCE_FRAME_INVALIDATION)
APP_DEFINITIONS += PROSPEROLIGHT_EXPERIMENTAL_HOST_DPB=$(EXPERIMENTAL_HOST_DPB)
APP_INCLUDE_PATHS ?= third_party/ps5-homebrew-ui third_party/update-check .deps/ps5-opengl/current/include include src src/gamestream platform/ps5 third_party/moonlight-common-c/src third_party/moonlight-common-c/enet/include third_party/moonlight-common-c/nanors third_party/moonlight-common-c/nanors/deps third_party/moonlight-common-c/nanors/deps/obl third_party/mbedtls/include third_party/opus/include
APP_STATIC_ARCHIVES ?= .deps/ps5-opengl/libps5opengl-group.a build/stream-deps/libmoonlight-common-c.a build/stream-deps/libopus.a build/stream-deps/libmbedtls.a build/stream-deps/libmbedx509.a build/stream-deps/libmbedcrypto.a
# The launcher draws with ps5-opengl: its AGC import libraries replace the app's own.
APP_IMPORT_STUBS ?= .deps/ps5-opengl/current/lib/libSceAgc.so build/stubs/libSceAgcDriver.so
# Empty selects the pinned ps5-opengl release (tools/fetch-opengl-sdk.sh).
PS5_OPENGL_PREFIX ?=
APP_RUNTIME_MODULES ?=
# The update check asks homebrew.page through libcurl (third_party/update-check).
PACBREW_PACKAGES ?= libcurl libpng
PACBREW_INCLUDE_PATHS ?=
PACBREW_STATIC_ARCHIVES ?=
PS5_HOST ?=
FTP_PORT ?= 2121
DEPLOY_FORMAT ?= folder
PS5_FTP_USER ?= anonymous
PS5_FTP_PASSWORD ?= codex
DEPLOY_DRY_RUN ?= 0
TITLE_ID ?=
APP_NAME ?=
APP_CATEGORY ?= game
CONTENT_SUFFIX ?=
HOST_CXX ?= clang++
HOST_TEST_CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror \
	-ffunction-sections -fdata-sections
HOST_TEST_LDFLAGS ?= -Wl,--gc-sections
GTEST_ARGS ?=
export FEC_SIMD OPUS_SIMD PYROWAVE
export APP_DEFINITIONS APP_INCLUDE_PATHS APP_STATIC_ARCHIVES APP_IMPORT_STUBS APP_RUNTIME_MODULES
export PS5_OPENGL_PREFIX
export PACBREW_PACKAGES PACBREW_INCLUDE_PATHS PACBREW_STATIC_ARCHIVES
export PS5_HOST FTP_PORT DEPLOY_FORMAT PS5_FTP_USER PS5_FTP_PASSWORD DEPLOY_DRY_RUN
export TITLE_ID APP_NAME APP_CATEGORY CONTENT_SUFFIX

RUNTIME := runtime/libc.prx
RUNTIME_INPUTS := tools/rebuild-libc.sh \
	$(wildcard tooling/native/*.cpp tooling/native/*.hpp) \
	$(wildcard tooling/native/runtime/*.txt)
HOST_UNIT_TEST := build/tests/prosperolight_tests
HOST_RUNTIME_TEST := build/tests/cpp_runtime_tests
HOST_ELEVATION_TEST := build/tests/elevation_client
STREAM_ARCHIVES := build/stream-deps/libmoonlight-common-c.a \
	build/stream-deps/libopus.a build/stream-deps/libmbedtls.a \
	build/stream-deps/libmbedx509.a build/stream-deps/libmbedcrypto.a
STREAM_INPUTS := tools/pyrowave/apply-transport.sh tools/pyrowave/patches/moonlight/0001-vibepollo-independent-record-protocol.patch tools/build-stream-deps.sh tools/native-toolchain.sh \
	$(wildcard src/gamestream/* platform/ps5/*) \
	$(wildcard third_party/moonlight-common-c/src/* third_party/moonlight-common-c/enet/*) \
	$(wildcard third_party/mbedtls/library/* third_party/mbedtls/include/mbedtls/*) \
	$(wildcard third_party/opus/src/* third_party/opus/include/*)

.PHONY: transport-deps all app build init doctor test test-deps test-unit test-integration libc deps pacbrew pacbrew-list stream-deps assets-check format format-check tidy lint check ffpkg ffpfsc packages deploy undeploy clean distclean help

all: app
build: app

init:
	@printf '%s\n' '==> [init] Configuring the application identity in sce_sys/param.json'
	@bash tools/init-project.sh sce_sys/param.json

doctor:
	@printf '%s\n' '==> [doctor] Checking the Linux/WSL host without changing it'
	@bash tools/doctor.sh

test: test-unit test-integration test-performance-guards test-self-update

transport-deps:
	@bash tools/pyrowave/apply-transport.sh

# The self-update helper, a payload for the console's loader, travels in the
# app's folder (third_party/self-update-helper, third_party/update-check).
SELF_UPDATE_HELPER := build/self-update/self-updater.elf
HOST_CC ?= clang

.PHONY: self-update-helper test-self-update
self-update-helper:
	@printf '%s\n' '==> [self-update] Building the helper for the payload loader'
	@bash tools/setup-native-dependencies.sh >/dev/null
	@$(MAKE) --no-print-directory -s -C third_party/self-update-helper \
		PS5_PAYLOAD_SDK="$(CURDIR)/.deps/native/ps5-payload-sdk" \
		OUTPUT="$(CURDIR)/$(SELF_UPDATE_HELPER)"
	@python3 tools/validate-loader-elf.py "$(SELF_UPDATE_HELPER)"

test-self-update: test-deps
	@printf '%s\n' '==> [test-self-update] Running the self-update engine against its helper'
	@mkdir -p build/tests/self-update
	@for name in miniz miniz_tinfl miniz_tdef miniz_zip; do \
		$(HOST_CC) -std=c11 -O2 -w -g -fsanitize=address,undefined \
			-c third_party/miniz/$$name.c -o build/tests/self-update/$$name.o || exit 1; \
	done
	@$(HOST_CXX) $(HOST_TEST_CXXFLAGS) -g -fsanitize=address,undefined -fno-sanitize-recover=all \
		-Ithird_party -Ithird_party/update-check \
		third_party/self-update-helper/test_self_update.cpp third_party/self-update-helper/updater.cpp \
		third_party/self-update-helper/archive.cpp third_party/self-update-helper/files.cpp \
		build/tests/self-update/*.o -pthread $(HOST_TEST_LDFLAGS) -o build/tests/test_self_update
	@build/tests/test_self_update

test-deps:
	@printf '%s\n' '==> [test-deps] Fetching the pinned host-only GoogleTest source'
	@bash tools/setup-test-dependencies.sh >/dev/null

test-unit: $(HOST_UNIT_TEST) $(HOST_RUNTIME_TEST) $(HOST_ELEVATION_TEST)
	@printf '%s\n' '==> [test-unit] Running host-native GoogleTest application tests'
	@$(HOST_UNIT_TEST) $(GTEST_ARGS)
	@printf '%s\n' '==> [test-unit] Running C++ allocation runtime tests'
	@$(HOST_RUNTIME_TEST)
	@printf '%s\n' '==> [test-unit] Running Lapy one-host elevation client tests'
	@$(HOST_ELEVATION_TEST)

$(HOST_UNIT_TEST): tests/test_prosperolight.cpp include/moonlight_config.hpp \
		include/moonlight_performance.hpp include/moonlight_tuning.hpp \
		include/moonlight_pipeline.hpp \
		include/native_agc_output.hpp \
		include/moonlight_health.hpp \
		include/moonlight_physical_input.hpp \
		include/moonlight_stream_input.hpp src/moonlight_config.cpp \
		include/moonlight_discovery.hpp src/moonlight_discovery.cpp \
		include/lan_http_report.hpp src/lan_http_report.cpp \
		include/connecting_plate.hpp src/connecting_plate.cpp \
		tools/setup-test-dependencies.sh | test-deps transport-deps
	@printf '%s\n' '==> [test-unit] Compiling the host-native GoogleTest binary'
	@mkdir -p -- $(@D)
	@gtest=$$(bash tools/setup-test-dependencies.sh); \
		$(HOST_CXX) -std=c++20 -O2 -pthread \
			-isystem "$$gtest/googletest/include" -I"$$gtest/googletest" \
			-c "$$gtest/googletest/src/gtest-all.cc" -o $(@D)/gtest-all.o; \
		$(HOST_CXX) -std=c++20 -O2 -pthread \
			-isystem "$$gtest/googletest/include" -I"$$gtest/googletest" \
			-c "$$gtest/googletest/src/gtest_main.cc" -o $(@D)/gtest-main.o; \
		$(HOST_CXX) $(HOST_TEST_CXXFLAGS) -pthread -Iinclude \
			-isystem "$$gtest/googletest/include" \
			tests/test_prosperolight.cpp src/moonlight_config.cpp src/moonlight_discovery.cpp \
			src/lan_http_report.cpp src/connecting_plate.cpp \
			$(@D)/gtest-all.o $(@D)/gtest-main.o \
			$(HOST_TEST_LDFLAGS) -o $@

$(HOST_RUNTIME_TEST): tests/test_cpp_runtime.cpp tooling/native/app_cpp_runtime.cpp
	@printf '%s\n' '==> [test-unit] Compiling the C++ allocation runtime test binary'
	@mkdir -p -- $(@D)
	@$(HOST_CXX) $(HOST_TEST_CXXFLAGS) -fno-exceptions -fno-rtti \
		tests/test_cpp_runtime.cpp tooling/native/app_cpp_runtime.cpp \
		$(HOST_TEST_LDFLAGS) -o $@

$(HOST_ELEVATION_TEST): tests/test_elevation.cpp src/elevation/elevation.cpp \
		src/elevation/elevation.hpp src/elevation/protocol.hpp
	@printf '%s\n' '==> [test-unit] Compiling the Lapy one-host elevation client test'
	@mkdir -p -- $(@D)
	@$(HOST_CXX) $(HOST_TEST_CXXFLAGS) -Isrc/elevation \
		tests/test_elevation.cpp $(HOST_TEST_LDFLAGS) -o $@

test-integration:
	@printf '%s\n' '==> [test-integration] Running host tooling integration tests'
	@python3 -m unittest discover -s tests -p 'test_*.py' -v

.PHONY: launcher-check fonts
# The launcher on the PC: behaviour checks and a picture of every state.
launcher-check:
	@printf '%s\n' '==> [launcher-check] Running the launcher against a pretend Sunshine network'
	@bash tools/render-launcher.sh build/launcher-pictures

fonts:
	@printf '%s\n' '==> [fonts] Baking the launcher fonts from third_party/fonts'
	@bash tools/bake-fonts.sh

.PHONY: test-stream-performance
test-stream-performance:
	@bash tools/test-stream-performance.sh

.PHONY: performance-candidates
performance-candidates:
	@bash tools/build-performance-candidates.sh

.PHONY: performance-round3-candidates
performance-round3-candidates:
	@bash tools/build-performance-candidates.sh --round3

.PHONY: test-performance-guards
test-performance-guards: | transport-deps
	@mkdir -p build/tests
	@clang -std=c11 -D_DEFAULT_SOURCE -DUSE_MBEDTLS -O2 -c third_party/moonlight-common-c/src/FakeCallbacks.c \
		-Ithird_party/moonlight-common-c/src -Ithird_party/moonlight-common-c/enet/include \
		-Ithird_party/mbedtls/include -o build/tests/fake_callbacks.o
	@$(HOST_CXX) $(HOST_TEST_CXXFLAGS) -pthread -Wno-unused-function -Wno-missing-field-initializers \
		-Iinclude -Isrc -Iplatform/ps5 \
		-Ithird_party/opus/include -Ithird_party/mbedtls/include -Ithird_party/moonlight-common-c/src \
		tests/test_decoder_pipeline.cpp build/tests/fake_callbacks.o $(HOST_TEST_LDFLAGS) \
		-o build/tests/decoder_pipeline
	@build/tests/decoder_pipeline
	@$(HOST_CXX) $(HOST_TEST_CXXFLAGS) -pthread -Wno-unused-function -Wno-missing-field-initializers \
		-Iinclude -Isrc -Iplatform/ps5 \
		-Ithird_party/opus/include -Ithird_party/mbedtls/include -Ithird_party/moonlight-common-c/src \
		tests/test_controllers.cpp $(HOST_TEST_LDFLAGS) -o build/tests/controllers
	@build/tests/controllers
	@clang -std=c11 -D_DEFAULT_SOURCE -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -fvisibility=hidden \
		tests/test_socket_metrics.c -Wl,--gc-sections -o build/tests/socket_metrics
	@build/tests/socket_metrics
	@clang -std=c11 -D_DEFAULT_SOURCE -DPS5_THREAD_PLACEMENT_HOST_TEST -O2 -Wall -Wextra -Werror \
		tests/test_thread_placement.c -o build/tests/thread_placement
	@build/tests/thread_placement
	@$(HOST_CXX) $(HOST_TEST_CXXFLAGS) -Wno-unused-function -Iinclude -Isrc \
		tests/test_presentation_lifetime.cpp $(HOST_TEST_LDFLAGS) -o build/tests/presentation_lifetime
	@build/tests/presentation_lifetime
	@$(HOST_CXX) $(HOST_TEST_CXXFLAGS) -pthread -Wno-unused-function -Wno-missing-field-initializers \
		-DVIDEO_SLICES_PER_FRAME=$(VIDEO_SLICES_PER_FRAME) \
		-DDECODER_PIPELINE_DEPTH=$(DECODER_PIPELINE_DEPTH) \
		-DDECODER_CPU_PRIORITY=$(DECODER_CPU_PRIORITY) -DINPUT_POLL_US=$(INPUT_POLL_US) \
		-Iinclude -Isrc -Iplatform/ps5 -Ithird_party/opus/include -Ithird_party/mbedtls/include \
		-Ithird_party/moonlight-common-c/src \
		tests/test_performance_summary.cpp $(HOST_TEST_LDFLAGS) -o build/tests/performance_summary
	@build/tests/performance_summary | python3 -c 'import json,sys; r=json.load(sys.stdin); assert r["schema"]==3 and r["present_overlap"]==1 and r["presented"]==95 and r["refresh_x100"]==11988 and r["client_refresh_x100"]==11988; t=r["timings_us"]; assert t["decode"]["count"]==100 and t["decode"]["mean"]==3000; assert t["receive_to_enqueue"]["mean"]==3000 and r["reassembly_invalid_samples"]==1; assert len(t)==20; assert r["performance_detail"]==1 and r["stream_bytes"]==123456 and r["flip_queries"]==7 and r["flip_sleeps"]==3; assert t["agc_prepare"]["mean"]==150 and t["agc_cache_flush"]["mean"]==50 and t["agc_submit"]["mean"]==250 and t["flush"]["mean"]==1250 and t["completion_wait"]["mean"]==500 and t["ready_to_present"]["mean"]==100; assert r["decoded"]==99 and r["not_displayed"]==4 and r["network_frame_gaps"]==2 and r["decoder_frame_gaps"]==30; assert r["decoder_mode"]=="adaptive" and r["decoder_pipeline_depth"]==3 and r["decoder_drain"]==1 and r["drain_calls"]==40 and r["decoder_cores"]==5 and r["decoder_cpu_affinity"]==1023; assert r["placement_applied"]==11 and r["placement_failed"]==1 and r["receive_placement_verified"]==1024; assert r["vsync_requested"]==1 and r["vsync_active"]==1 and r["flip_events_active"]==1 and r["flip_event_wakeups"]==2; assert r["controllers_peak"]==3 and r["controller_arrivals"]==2 and r["controller_removals"]==1 and r["controller_open_errors"]==4 and r["controller_send_errors"]==5 and r["user_scan_errors"]==6; print("Performance JSON / partial-write failure checks PASS")'

deps: test-deps
	@printf '%s\n' '==> [deps] Fetching declared native dependencies'
	@bash tools/setup-native-dependencies.sh
	@bash tools/setup-pacbrew-dependencies.sh --environment

pacbrew:
	@printf '%s\n' '==> [pacbrew] Fetching the pinned prebuilt ports sysroot'
	@bash tools/setup-pacbrew-dependencies.sh --all

pacbrew-list:
	@printf '%s\n' '==> [pacbrew] Listing available pkg-config modules'
	@bash tools/setup-pacbrew-dependencies.sh --list

assets-check:
	@printf '%s\n' '==> [assets] Validating icon, backgrounds, and selection audio'
	@bash tools/validate-assets.sh

libc:
	@printf '%s\n' '==> [libc] Rebuilding and verifying the clean-room runtime'
	@bash tools/rebuild-libc.sh

$(RUNTIME): $(RUNTIME_INPUTS)
	@printf '%s\n' '==> [libc] Generating the missing or outdated runtime'
	@bash tools/rebuild-libc.sh

app: $(RUNTIME) $(STREAM_ARCHIVES)
	@bash tools/build-stream-deps.sh --ensure
	@printf '%s\n' '==> [app] Compiling, linking, signing, and assembling the app folder'
	@bash tools/build.sh Folder

stream-deps: $(STREAM_ARCHIVES)
	@bash tools/build-stream-deps.sh --ensure

$(STREAM_ARCHIVES) &: $(STREAM_INPUTS)
	@printf '%s\n' '==> [stream] Building pinned Moonlight, mbedTLS, and Opus archives'
	@bash tools/build-stream-deps.sh

ffpkg: $(RUNTIME) $(STREAM_ARCHIVES)
	@bash tools/build-stream-deps.sh --ensure
	@printf '%s\n' '==> [ffpkg] Building the app folder and UFS2 image'
	@bash tools/build.sh Ffpkg

ffpfsc: $(RUNTIME) $(STREAM_ARCHIVES)
	@bash tools/build-stream-deps.sh --ensure
	@printf '%s\n' '==> [ffpfsc] Building the app folder and compressed image'
	@bash tools/build.sh Ffpfsc

packages: $(RUNTIME) $(STREAM_ARCHIVES)
	@bash tools/build-stream-deps.sh --ensure
	@printf '%s\n' '==> [packages] Building the app folder and both package formats'
	@bash tools/build.sh All

deploy:
	@printf '%s\n' '==> [deploy] Building and publishing the selected app output over FTP'
	@bash tools/deploy.sh

undeploy:
	@printf '%s\n' '==> [undeploy] Removing staged development files for this title over FTP'
	@bash tools/deploy.sh undeploy

format:
	@printf '%s\n' '==> [format] Formatting C and C++ sources'
	@bash tools/run_clang_format.sh

format-check:
	@printf '%s\n' '==> [format] Checking C and C++ formatting'
	@bash tools/run_clang_format.sh --check

tidy:
	@printf '%s\n' '==> [tidy] Running Clang static analysis'
	@bash tools/run_clang_tidy.sh

lint:
	@printf '%s\n' '==> [lint] Running source, metadata, and shell checks'
	@bash tools/lint.sh

check: lint test app

clean:
	@printf '%s\n' '==> [clean] Removing generated build outputs'
	@rm -rf -- build dist
	@rm -f -- $(RUNTIME)

distclean: clean
	@printf '%s\n' '==> [distclean] Removing downloaded dependency caches'
	@rm -rf -- .deps

help:
	@printf '%s\n' \
	  'make                 Generate libc.prx and build the Hello World folder' \
	  'make init TITLE_ID=PPSA12345 APP_NAME="My App"  Configure app identity' \
	  'make doctor          Check required and optional Linux/WSL tools' \
	  'make test            Run all host unit and integration tests' \
	  'make test-deps       Fetch verified host-only GoogleTest source' \
	  'make test-unit       Run host-native GoogleTest application tests' \
	  'make test-integration  Run host tooling integration tests' \
	  'make deps            Fetch native dependencies into .deps/' \
	  'make pacbrew         Fetch the pinned PacBrew ports sysroot' \
	  'make pacbrew-list    List PacBrew pkg-config module names' \
	  'make assets-check    Validate the current presentation assets' \
	  'make launcher-check  Run the launcher on the PC and write a picture of every state' \
	  'make fonts           Bake the launcher fonts from third_party/fonts' \
	  'make libc            Force a deterministic runtime/libc.prx rebuild' \
	  'make format          Apply the shared Clang formatting policy' \
	  'make format-check    Check formatting without modifying files' \
	  'make tidy            Run the shared Clang static-analysis policy' \
	  'make lint            Run format, tidy, metadata, and shell checks' \
	  'make check           Run lint and build the skeleton app' \
	  'make ffpkg           Build the folder and UFS2 .ffpkg image' \
	  'make ffpfsc          Build the folder and compressed .ffpfsc image' \
	  'make packages        Build folder, .ffpkg, and .ffpfsc outputs' \
	  'make deploy PS5_HOST=<address>  Build and FTP-deploy the app folder' \
	  'make undeploy PS5_HOST=<address>  Remove this title from /data/homebrew' \
	  'Build variables:     APP_DEFINITIONS, APP_INCLUDE_PATHS, APP_STATIC_ARCHIVES, APP_RUNTIME_MODULES' \
	  'PacBrew variables:   PACBREW_PACKAGES, PACBREW_INCLUDE_PATHS, PACBREW_STATIC_ARCHIVES' \
	  'Diagnostics:         LAN_TELEMETRY=1 enables the optional port-8767 development sink' \
	  'Deploy variables:    FTP_PORT=2121, DEPLOY_FORMAT=folder|ffpfsc|ffpkg, DEPLOY_DRY_RUN=0|1' \
	  'Local defaults:      Copy .env.example to the ignored .env file' \
	  'make clean           Remove build/, dist/, and generated libc.prx' \
	  'make distclean       Also remove the ignored .deps/ cache'

.PHONY: controller-deps
controller-deps: $(if $(wildcard tools/pyrowave/apply-transport.sh),transport-deps)
	@bash tools/controllers/apply-haptics.sh

$(HOST_UNIT_TEST) test-integration test-stream-performance test-performance-guards: | controller-deps

# Every package carries the helper that installs the next release.
app ffpkg ffpfsc packages: self-update-helper
