/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ui_sound.hpp"

#include <SDL2/SDL.h>

#include <array>
#include <cstdio>

namespace prosperolight
{
namespace
{

bool sound_enabled = true;
constexpr int kSampleRate = 48000;
constexpr Uint8 kChannels = 2;
constexpr Uint16 kBufferFrames = 1024;

struct Clip
{
    Uint8 *data = nullptr;
    Uint32 length = 0;
};

constexpr std::array<const char *, static_cast<size_t>(UiSoundCue::Count)> kPaths = {
    "assets/sfx/ui_open.wav",    "assets/sfx/ui_move.wav",         "assets/sfx/ui_confirm.wav",
    "assets/sfx/ui_setting.wav", "assets/sfx/ui_back.wav",         "assets/sfx/ui_success.wav",
    "assets/sfx/ui_error.wav",   "assets/sfx/ui_stream_start.wav",
};

std::array<Clip, kPaths.size()> clips;
SDL_AudioDeviceID device = 0;
bool clips_loaded = false;

bool LoadClip(size_t index)
{
    SDL_AudioSpec format{};
    const char *path = kPaths[index];
    if (!SDL_LoadWAV(path, &format, &clips[index].data, &clips[index].length))
    {
        char app_path[128];
        std::snprintf(app_path, sizeof(app_path), "/app0/%s", path);
        if (!SDL_LoadWAV(app_path, &format, &clips[index].data, &clips[index].length))
            return false;
    }

    if (format.freq != kSampleRate || format.format != AUDIO_S16LSB || format.channels != kChannels)
    {
        SDL_FreeWAV(clips[index].data);
        clips[index] = {};
        return false;
    }
    return true;
}

void LoadClips()
{
    if (clips_loaded)
        return;
    for (size_t index = 0; index < clips.size(); ++index)
        (void)LoadClip(index);
    clips_loaded = true;
}

} // namespace

bool ui_sound_initialize()
{
    FILE *settings = std::fopen("/download0/prosperolight-ui-sound.bin", "rb");
    if (settings)
    {
        unsigned char bytes[5]{};
        if (std::fread(bytes, 1, sizeof(bytes), settings) == sizeof(bytes) &&
            bytes[0] == 'P' && bytes[1] == 'L' && bytes[2] == 'S' &&
            bytes[3] == 1 && bytes[4] <= 1)
            sound_enabled = bytes[4] != 0;
        std::fclose(settings);
    }
    LoadClips();
    if (device)
        return true;

    SDL_AudioSpec desired{};
    SDL_AudioSpec obtained{};
    desired.freq = kSampleRate;
    desired.format = AUDIO_S16LSB;
    desired.channels = kChannels;
    desired.samples = kBufferFrames;
    device = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (!device)
        return false;
    if (obtained.freq != desired.freq || obtained.format != desired.format ||
        obtained.channels != desired.channels)
    {
        SDL_CloseAudioDevice(device);
        device = 0;
        return false;
    }
    SDL_PauseAudioDevice(device, 0);
    return true;
}

void ui_sound_play(UiSoundCue cue)
{
    const size_t index = static_cast<size_t>(cue);
    if (!sound_enabled || !device || index >= clips.size() || !clips[index].data || !clips[index].length)
        return;

    SDL_ClearQueuedAudio(device);
    (void)SDL_QueueAudio(device, clips[index].data, clips[index].length);
}

bool ui_sound_enabled()
{
    return sound_enabled;
}

bool ui_sound_set_enabled(bool enabled)
{
    const char *temporary = "/download0/prosperolight-ui-sound.tmp";
    FILE *settings = std::fopen(temporary, "wb");
    if (!settings)
        return false;
    const unsigned char bytes[] = {'P', 'L', 'S', 1, static_cast<unsigned char>(enabled)};
    const bool written = std::fwrite(bytes, 1, sizeof(bytes), settings) == sizeof(bytes);
    const int closed = std::fclose(settings);
    if (!written || closed ||
        std::rename(temporary, "/download0/prosperolight-ui-sound.bin") != 0)
    {
        std::remove(temporary);
        return false;
    }
    sound_enabled = enabled;
    if (!enabled && device)
        SDL_ClearQueuedAudio(device);
    return true;
}

void ui_sound_clear_for_stream()
{
    if (!device)
        return;
    SDL_ClearQueuedAudio(device);
}

} // namespace prosperolight
