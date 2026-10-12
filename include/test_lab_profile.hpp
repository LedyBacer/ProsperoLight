/* ps5-native-app-boilerplate - bounded developer session profile.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include "moonlight_stream.hpp"
#include "host_preferences.hpp"
#include <optional>
#include <cstdio>
#include <string>
#include <string_view>
#ifndef PROSPEROLIGHT_TEST_LAB_ROOT
#define PROSPEROLIGHT_TEST_LAB_ROOT "/data/prosperolight/test-lab"
#endif
namespace prosperolight::test_lab
{
struct TestProfile
{
    std::string run_id, host, app, suite, source_sha, build_label;
    std::optional<unsigned> codec, chroma, resolution, fps, bitrate_mbps, pipeline, cores, pacing,
        vrr_profile, host_vrr, display, scale;
    std::optional<bool> hdr, vsync, extensions, optimize, mute_host, quit_host, coordinated;
    unsigned startup_seconds = 30, ready_seconds = 30, hard_seconds = 120;
    unsigned warmup_seconds = 0, measure_seconds = 30, cooldown_seconds = 0;
};
bool safe_run_id(std::string_view id);
bool parse_profile(std::string_view json, std::string_view expected_id, TestProfile &out,
                   std::string &error);
bool read_profile(std::string_view id, TestProfile &out, std::string &error);
bool write_resolved_options(FILE *file, const moonlight_stream_options_t &options);
// Copies saved options and overlays only explicit values; never persists settings.
bool resolve_profile(const TestProfile &profile, const moonlight_stream_options_t &saved,
                     const HostPreferences &host, unsigned pacing, unsigned vrr_profile,
                     moonlight_stream_options_t &out, std::string &error);
bool write_profile_status(std::string_view id, const char *state, unsigned sequence,
                          const moonlight_stream_options_t *resolved, std::string_view error = {},
                          std::string_view command_id = {}, std::string_view command_result = {},
                          std::string_view command_action = {}, bool stream_ready = false);
TestProfile &active_profile();
bool &active();
struct ControlCommand
{
    std::string command_id, action;
};
bool parse_control(std::string_view json, std::string_view id, ControlCommand &out);
} // namespace prosperolight::test_lab
