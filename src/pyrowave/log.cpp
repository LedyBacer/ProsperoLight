// SPDX-License-Identifier: GPL-3.0-or-later
#include "common.hpp"
#include "lan_http_report.hpp"
#include <cstdarg>
#include <stdexcept>
#include <mutex>
void log_line(const char *fmt, ...)
{
    static std::mutex lock;
    std::lock_guard<std::mutex> guard(lock);
    char text[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    FILE *f = fopen("/download0/prosperolight-pyrowave.log", "a");
    if (f)
    {
        fprintf(f, "%s\n", text);
        fclose(f);
    }
    (void)lan_http_report_text(text);
}
[[noreturn]] void fail(const char *reason)
{
    throw std::runtime_error(reason);
}
