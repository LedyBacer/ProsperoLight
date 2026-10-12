/* ps5-native-app-boilerplate - developer-only PR0 argv receipt.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include <cstdio>
#include <string_view>
#include <unistd.h>
#ifndef PROSPEROLIGHT_LAUNCH_PROOF_ROOT
#define PROSPEROLIGHT_LAUNCH_PROOF_ROOT "/data/prosperolight/test-lab/pr0"
#endif
namespace prosperolight
{
inline bool launch_proof_id(std::string_view value)
{
    if (value.empty() || value.size() > 127)
        return false;
    for (char c : value)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '-' || c == '_'))
            return false;
    return true;
}
// Called after storage initialization only in explicitly marked developer builds.
// The fixed expected sequence proves arguments reached this process, rather than
// treating a launcher-side return code as evidence of argv delivery.
inline bool write_launch_proof(int argc, char **argv)
{
    if (!argv || argc < 7 || argc > 64)
        return false;
    for (int i = 0; i + 6 < argc; ++i)
    {
        bool valid = true;
        for (int j = 0; j < 7; ++j)
            valid = valid && argv[i + j];
        if (!valid || std::string_view(argv[i]) != "--pc" ||
            std::string_view(argv[i + 2]) != "--app" ||
            std::string_view(argv[i + 3]) != "Desktop" ||
            std::string_view(argv[i + 4]) != "--exit-after-stream" ||
            std::string_view(argv[i + 5]) != "--launch-proof" || !launch_proof_id(argv[i + 6]))
            continue;
        const std::string_view pc{argv[i + 1]};
        if (pc.empty() || pc.size() > 127)
            return false;
        for (char c : pc)
            if (c < 32 || c > 126 || c == '"' || c == '\\')
                return false;
        char path[256], tmp[256];
        std::snprintf(path, sizeof(path), PROSPEROLIGHT_LAUNCH_PROOF_ROOT "/%.127s-app.json",
                      argv[i + 6]);
        std::snprintf(tmp, sizeof(tmp), PROSPEROLIGHT_LAUNCH_PROOF_ROOT "/%.127s-app.tmp",
                      argv[i + 6]);
        if (!access(path, F_OK))
            return false;
        FILE *f = std::fopen(tmp, "wx");
        if (!f)
            return false;
        const bool wrote =
            std::fprintf(f,
                         "{\"schema\":\"prosperolight-launch-proof/1\",\"request_id\":\"%s\","
                         "\"pid\":%d,\"pc\":\"%s\",\"app\":\"Desktop\","
                         "\"exit_after_stream\":true,\"argv_verified\":true}\n",
                         argv[i + 6], static_cast<int>(getpid()), argv[i + 1]) > 0;
        const int closed = std::fclose(f);
        return wrote && closed == 0 && std::rename(tmp, path) == 0;
    }
    return false;
}
} // namespace prosperolight
