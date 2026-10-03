/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

// Where ProsperoLight keeps its files.
//
// With filesystem access (src/elevation, asked for first thing in main)
// everything the app writes lives under /data/prosperolight:
//   config/    prosperolight-config.bin: saved PCs and stream settings
//   pairing/   cert.pem and key.pem: the identity Sunshine paired with
//   logs/      prosperolight-launcher.log, the previous launch's log, and the
//              last stream's performance report; crash-last.txt after a crash
//   cache/     the shaders the launcher compiled, so the next launch is faster
// and the app's own files are read from the folder it was installed in.
// Without it (no compatible upstream Lapy service, or the request was refused) the
// sandbox paths stay: /app0 and /download0, as in every earlier version.

namespace storage
{

struct Paths
{
    // -1 before Initialize, 0 with filesystem access, otherwise the
    // elevation::Status that refused it.
    int status = -1;
    // The folder eboot.bin, assets/ and sce_sys/ are in.
    char app[96] = "/app0";
    char config[128] = "/download0/prosperolight-config.bin";
    char config_temporary[128] = "/download0/prosperolight-config.tmp";
    // The folder the pairing identity is in; the identity code creates it.
    char pairing[128] = "/download0/moonlight";
    char logs[128] = "/download0";
    // The folder a stream leaves its performance report in.
    char performance[128] = "/download0/moonlight";
};

inline Paths g_paths;

inline const Paths &paths()
{
    return g_paths;
}

// Asks for filesystem access, settles every path, creates the folders, brings
// over what an earlier version kept in the sandbox, and opens the log. Call it
// once, first, while the process still has a single thread.
void Initialize();
// The log file itself (not the pipe that feeds it), for the crash report; -1
// when there is none.
int log_descriptor();

} // namespace storage
