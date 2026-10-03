/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

// A crash report. When the app faults, the handler writes what happened, where
// (as an offset into the build's llvm-pie.elf) and the calls found on the
// stack to <logs>/crash-last.txt (the one before is kept as crash-prev.txt)
// and into the log, then lets the system end the app as before.
// tools/symbolize-crash.py names the functions.

namespace crash
{

// Once, early in main, after the log is open.
void Install(const char *logs_dir);
// Names the calling thread in reports. The name must outlive the thread.
void NameThread(const char *name);

} // namespace crash
