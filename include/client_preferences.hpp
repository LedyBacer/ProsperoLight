/*
 * ps5-native-app-boilerplate - Client launch preferences without config ABI changes.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include "app_storage.hpp"
#include <cstdio>
namespace prosperolight
{
struct ClientPreferences
{
    bool custom_fps = false;
    bool optimize = true;
    bool mute_host = true;
};
inline ClientPreferences &client_preferences()
{
    static ClientPreferences value = []
    {
        ClientPreferences p;
        if (FILE *f = std::fopen(storage::setting_file("prosperolight-client.bin").c_str(), "rb"))
        {
            unsigned char b[7]{};
            if (std::fread(b, 1, 7, f) == 7 && b[0] == 'P' && b[1] == 'L' && b[2] == 'C' &&
                b[3] == 1 && b[4] <= 1 && b[5] <= 1 && b[6] <= 1)
                p = {b[4] != 0, b[5] != 0, b[6] != 0};
            std::fclose(f);
        }
        return p;
    }();
    return value;
}
inline bool client_preferences_save(ClientPreferences p)
{
    const auto path = storage::setting_file("prosperolight-client.bin"), tmp = path + ".tmp";
    FILE *f = std::fopen(tmp.c_str(), "wb");
    if (!f)
        return false;
    const unsigned char b[] = {'P',
                               'L',
                               'C',
                               1,
                               static_cast<unsigned char>(p.custom_fps),
                               static_cast<unsigned char>(p.optimize),
                               static_cast<unsigned char>(p.mute_host)};
    const bool ok = std::fwrite(b, 1, 7, f) == 7;
    const int closed = std::fclose(f);
    if (!ok || closed || std::rename(tmp.c_str(), path.c_str()))
    {
        std::remove(tmp.c_str());
        return false;
    }
    client_preferences() = p;
    return true;
}
} // namespace prosperolight
