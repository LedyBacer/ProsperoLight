// ps5-native-app-boilerplate - ProsperoLight: system services on the PC, for the launcher harness.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/ps5/system.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <unistd.h>

namespace hui::sys
{

std::int64_t monotonic_us()
{
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<std::int64_t>(now.tv_sec) * 1000000 + now.tv_nsec / 1000;
}

void log(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    std::vfprintf(stderr, format, arguments);
    va_end(arguments);
    std::fputc('\n', stderr);
}

bool hide_splash_screen()
{
    return true;
}

void sleep_us(std::uint32_t microseconds)
{
    usleep(microseconds);
}

void park()
{
    std::exit(1);
}

void quit()
{
    std::exit(0);
}

} // namespace hui::sys
