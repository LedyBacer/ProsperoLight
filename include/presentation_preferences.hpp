/*
 * ps5-native-app-boilerplate - Persistent presentation and endpoint policies.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include <cstdio>
#include "app_storage.hpp"
namespace moonlight
{
// Independent storage avoids changing legacy codec/host configuration layouts.
inline unsigned presentation_mode()
{
    unsigned char bytes[5]{};
    FILE *file = std::fopen(storage::setting_file("prosperolight-presentation.bin").c_str(), "rb");
    if (!file)
        return 0;
    const bool valid = std::fread(bytes, 1, sizeof(bytes), file) == sizeof(bytes) &&
                       bytes[0] == 'P' && bytes[1] == 'L' && bytes[2] == 'V' && bytes[3] == 1 &&
                       bytes[4] <= 2;
    std::fclose(file);
    return valid ? bytes[4] : 0;
}
inline bool save_presentation_mode(unsigned mode)
{
    if (mode > 2)
        return false;
    const auto destination = storage::setting_file("prosperolight-presentation.bin");
    const auto temporary = destination + ".tmp";
    FILE *file = std::fopen(temporary.c_str(), "wb");
    if (!file)
        return false;
    const unsigned char bytes[] = {'P', 'L', 'V', 1, static_cast<unsigned char>(mode)};
    const bool written = std::fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
    const int closed = std::fclose(file);
    if (!written || closed || std::rename(temporary.c_str(), destination.c_str()) != 0)
    {
        std::remove(temporary.c_str());
        return false;
    }
    return true;
}
} // namespace moonlight
