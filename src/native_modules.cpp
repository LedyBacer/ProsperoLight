/*
 * ps5-native-app-boilerplate / ProsperoLight - Process-owned native modules.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "native_modules.hpp"
#include <cstdio>

extern "C" int sceSysmoduleLoadModule(std::uint32_t id);

namespace prosperolight::native_modules
{
namespace
{
struct Module
{
    std::uint32_t id;
    int result;
};
Module modules[] = {{video_decoder, -1}, {keyboard, -1}, {mouse, -1}};
bool prepared = false;
} // namespace

void PrepareBeforeStorage()
{
    if (prepared)
        return;
    // Resolve modules before granting filesystem access and keep process-owned
    // references across reconnects. The helper must also preserve root/jail:
    // VideoDec2 lazily loads VdecCore and codec PRXs after this initial load.
    for (Module &module : modules)
        module.result = sceSysmoduleLoadModule(module.id);
    prepared = true;
}

void LogResults()
{
    std::printf("[PL] native modules: phase=before-storage video=%08x keyboard=%08x mouse=%08x; "
                "lifetime=process\n",
                static_cast<unsigned>(Result(video_decoder)),
                static_cast<unsigned>(Result(keyboard)), static_cast<unsigned>(Result(mouse)));
}

int Result(std::uint32_t id)
{
    for (const Module &module : modules)
        if (module.id == id)
            return module.result;
    return -1;
}
} // namespace prosperolight::native_modules
