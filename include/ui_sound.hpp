/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

namespace prosperolight {

enum class UiSoundCue {
    Open,
    Move,
    Confirm,
    Setting,
    Back,
    Success,
    Error,
    StreamStart,
    Count
};

bool ui_sound_initialize();
void ui_sound_play(UiSoundCue cue);
bool ui_sound_set_enabled(bool enabled);
bool ui_sound_enabled();
void ui_sound_clear_for_stream();

} // namespace prosperolight
