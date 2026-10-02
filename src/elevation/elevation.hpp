/*
 * ps5-native-app-boilerplate - Application-side elevation API.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "protocol.hpp"

namespace elevation
{
// Call during single-threaded startup, before creating workers or opening files
// outside the sandbox. Only Status::ok permits continuing with elevated work.
// Each call launches one bundled helper; no persistent service is installed.
[[nodiscard]] Status request(Capability capability,
                             const char *helper_path = "/app0/sandbox-elevator.elf") noexcept;
} // namespace elevation
