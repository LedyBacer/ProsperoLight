/*
 * ps5-native-app-boilerplate / ProsperoLight - Process-owned native modules.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include <cstdint>

namespace prosperolight::native_modules
{
constexpr std::uint32_t video_decoder = 207;
constexpr std::uint32_t keyboard = 0x0106;
constexpr std::uint32_t mouse = 0x00a9;
// Call once while the title still has its original filesystem namespace.
// Successful module references belong to the process, not an individual stream.
void PrepareBeforeStorage();
void LogResults();
int Result(std::uint32_t module);
} // namespace prosperolight::native_modules
