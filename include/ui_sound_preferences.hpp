/*
 * ps5-native-app-boilerplate - ProsperoLight launcher sound preference.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include "app_storage.hpp"
#include <cstdio>
namespace prosperolight
{
inline int ui_sound_preference = -1;
inline bool ui_sound_enabled()
{
    if (ui_sound_preference < 0)
    {
        ui_sound_preference = 1;
        if (FILE *file =
                std::fopen(storage::setting_file("prosperolight-ui-sound.bin").c_str(), "rb"))
        {
            unsigned char bytes[5]{};
            if (std::fread(bytes, 1, sizeof(bytes), file) == sizeof(bytes) && bytes[0] == 'P' &&
                bytes[1] == 'L' && bytes[2] == 'S' && bytes[3] == 1 && bytes[4] <= 1)
                ui_sound_preference = bytes[4];
            std::fclose(file);
        }
    }
    return ui_sound_preference != 0;
}
inline bool ui_sound_set_enabled(bool enabled)
{
    const auto path = storage::setting_file("prosperolight-ui-sound.bin");
    const auto temporary = path + ".tmp";
    FILE *file = std::fopen(temporary.c_str(), "wb");
    if (!file)
        return false;
    const unsigned char bytes[] = {'P', 'L', 'S', 1, static_cast<unsigned char>(enabled)};
    const bool written = std::fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
    const int closed = std::fclose(file);
    if (!written || closed || std::rename(temporary.c_str(), path.c_str()) != 0)
    {
        std::remove(temporary.c_str());
        return false;
    }
    ui_sound_preference = enabled;
    return true;
}
} // namespace prosperolight
