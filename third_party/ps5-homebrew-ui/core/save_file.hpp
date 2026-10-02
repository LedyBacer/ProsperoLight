// ps5-homebrew-ui - Checked save container and atomic file writes.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hui::save
{

// Kinds of payload stored in the container.
enum class Kind : std::uint16_t
{
    settings = 1,
    library = 2,
    stats = 3,
    game = 4,
    prefs = 5,
};

// Layout (little-endian): "HUIL" | u16 version | u16 kind | u32 size |
// payload | u32 CRC-32 of everything before it.
std::string encode(Kind kind, std::uint16_t version, std::string_view payload);

struct Decoded
{
    bool ok = false;
    std::uint16_t version = 0;
    std::string payload;
    std::string error;
};

// Rejects a wrong magic, kind, size, checksum or trailing bytes.
Decoded decode(Kind kind, std::string_view data);

std::uint32_t crc32(std::string_view data);

// Writes path via "path.tmp": write, fsync, close, rename (unlinking and
// retrying if the rename fails). Returns an error or "".
std::string write_atomic(const std::string &path, std::string_view data);

// Reads a whole file of at most max_bytes. Returns false if it is missing or
// unreadable.
bool read_file(const std::string &path, std::string *data, std::size_t max_bytes = 4u << 20);

// Names of the files in a directory. Reads the build-generated index.txt
// (one name per line) when present, because directory listing returns
// nothing under /app0 on the console; otherwise lists the directory.
std::vector<std::string> list_files(const std::string &directory);

// Creates a directory if it does not exist (single level).
bool ensure_directory(const std::string &path);

} // namespace hui::save
