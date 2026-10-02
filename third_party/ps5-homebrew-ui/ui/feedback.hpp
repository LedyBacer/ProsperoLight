// ps5-homebrew-ui - Feedback: the sounds and rumble a screen asks for this frame.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "audio/cues.hpp"

#include <vector>

namespace hui::ui
{

// What a screen or a component asks the platform to do this frame besides
// drawing. The owner of the frame loop plays the cues and starts the rumble.
struct Feedback
{
    std::vector<audio::CueEvent> cues;
    float rumble_strength = 0.0f;
    float rumble_seconds = 0.0f;

    void play(audio::Cue cue, float pitch = 1.0f, float pan = 0.0f, float gain = 1.0f)
    {
        cues.push_back({cue, pitch, pan, gain, audio::SoundSet::count});
    }
    // strength 0..1; keep UI rumbles short (0.04-0.15 s).
    void rumble(float strength, float seconds)
    {
        if (strength >= rumble_strength)
        {
            rumble_strength = strength;
            rumble_seconds = seconds;
        }
    }
    void clear()
    {
        cues.clear();
        rumble_strength = 0.0f;
        rumble_seconds = 0.0f;
    }
};

} // namespace hui::ui
