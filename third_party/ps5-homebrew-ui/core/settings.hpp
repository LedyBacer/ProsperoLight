// ps5-homebrew-ui - Player settings and their save format.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace hui
{

// Everything the settings screen edits. The values are live: the app applies
// them the frame they change (see Shell::take_settings_changed) and stores
// them when the screen is left.
struct Settings
{
    int music_volume = 4; // 0..10
    int sfx_volume = 8;   // 0..10
    int ui_volume = 7;    // 0..10
    bool reduced_motion = false;
    bool swap_confirm = false; // Circle confirms, Cross goes back
    bool show_fps = false;
    bool haptics = true;   // controller rumble on errors and big moments
    bool light_bar = true; // the controller's light follows the design's accent
    // Display resolution, an index into kResolutions; applies at the next start.
    int resolution = 2;
    // How large on-screen controller hints are: 0 small, 1 medium, 2 large.
    int hint_size = 1;
    // The design shown when the app opens (index into the concept list).
    int concept_index = 0;

    struct Resolution
    {
        int width;
        int height;
        const char *label;
    };
    static constexpr int kResolutionCount = 3;
    static constexpr Resolution kResolutions[kResolutionCount] = {
        {1920, 1080, "1080p"}, {2560, 1440, "1440p"}, {3840, 2160, "4K"}};

    static float gain(int volume)
    {
        // Perceptual curve: 0..10 steps map to roughly -40..0 dB.
        const float t = static_cast<float>(volume) / 10.0f;
        return t * t;
    }
};

std::string encode_settings(const Settings &settings);
// Out-of-range values are clamped. Returns false (leaving settings
// untouched) for malformed data or an unknown version.
bool decode_settings(std::string_view data, Settings *settings);

} // namespace hui
