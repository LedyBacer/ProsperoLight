// ps5-homebrew-ui - PS5 system services: logging, clock, splash screen.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/ps5/system.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>

extern "C"
{
    int sceKernelDebugOutText(int channel, const char *text);
    int sceKernelUsleep(unsigned int microseconds);
    int sceSystemServiceHideSplashScreen(void);
    int sceSystemServiceLoadExec(const char *path, const char **arguments);
}

namespace hui::sys
{

std::int64_t monotonic_us()
{
    timespec now{};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return static_cast<std::int64_t>(now.tv_sec) * 1000000 + now.tv_nsec / 1000;
}

void log(const char *format, ...)
{
    // klog lines are short; longer messages are truncated rather than split.
    char line[384];
    va_list arguments;
    va_start(arguments, format);
    int length = std::vsnprintf(line, sizeof(line) - 1, format, arguments);
    va_end(arguments);
    if (length < 0)
        return;
    std::size_t used = std::strlen(line);
    line[used] = '\n';
    line[used + 1] = '\0';
    std::fputs(line, stdout);
    sceKernelDebugOutText(0, line);
}

bool hide_splash_screen()
{
    return sceSystemServiceHideSplashScreen() == 0;
}

void sleep_us(std::uint32_t microseconds)
{
    sceKernelUsleep(microseconds);
}

void park()
{
    std::fflush(nullptr);
    for (;;)
        sceKernelUsleep(100000);
}

void quit()
{
    log("[HUI] quit requested");
    std::fflush(nullptr);
    const int refused = sceSystemServiceLoadExec("exit", nullptr);
    log("[HUI] quit refused rc=0x%x", static_cast<unsigned>(refused));
    park();
}

} // namespace hui::sys
