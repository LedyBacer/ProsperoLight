/*
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include <cstdio>
namespace moonlight {
// Independent storage avoids changing legacy codec/host configuration layouts.
inline unsigned presentation_mode()
{
    unsigned char bytes[5]{};
    FILE *file = std::fopen("/download0/prosperolight-presentation.bin", "rb");
    if (!file)
        return 1;
    const bool valid = std::fread(bytes, 1, sizeof(bytes), file) == sizeof(bytes) &&
                       bytes[0] == 'P' && bytes[1] == 'L' && bytes[2] == 'V' &&
                       bytes[3] == 1 && bytes[4] <= 2;
    std::fclose(file);
    return valid ? bytes[4] : 1;
}
inline bool save_presentation_mode(unsigned mode)
{
    if (mode > 2)
        return false;
    const char *temporary = "/download0/prosperolight-presentation.tmp";
    FILE *file = std::fopen(temporary, "wb");
    if (!file)
        return false;
    const unsigned char bytes[] = {'P', 'L', 'V', 1, static_cast<unsigned char>(mode)};
    const bool written = std::fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
    const int closed = std::fclose(file);
    if (!written || closed ||
        std::rename(temporary, "/download0/prosperolight-presentation.bin") != 0)
    {
        std::remove(temporary);
        return false;
    }
    return true;
}
} // namespace moonlight
