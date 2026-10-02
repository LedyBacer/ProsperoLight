// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
constexpr const char *PYRO_COMMIT = "186f0393b77f7755953b5ecde994bb1cec2e4155";
void log_line(const char *fmt, ...);
[[noreturn]] void fail(const char *reason);
