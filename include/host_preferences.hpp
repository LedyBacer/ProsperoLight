/*
 * ps5-native-app-boilerplate - Per-PC session preferences.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include "client_preferences.hpp"
#include "host_quit_preferences.hpp"
#include "moonlight_config.hpp"
#include <cstdint>
#include <cstdio>
#include <string>
namespace prosperolight
{
struct HostPreferences
{
    bool extensions = false;
    unsigned vrr = 0;     // 0: host default (omit), 1: off, 2: on
    unsigned display = 0; // 0: host default (omit), 1: physical, 2: virtual
    unsigned scale = 100;
    bool optimize = true, mute_host = true, quit_host = false;
};
inline std::string host_preferences_path(const moonlight_config_host_t &host, bool identity = true)
{
    std::string key;
    if (identity && host.unique_id[0])
        key = "id:" + std::string(host.unique_id);
    else
        key = "endpoint:" + std::string(host.address) + ":" +
              std::to_string(moonlight_config_host_port(&host));
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char byte : key)
    {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    char name[64];
    std::snprintf(name, sizeof(name), "prosperolight-pc-%016llx.bin",
                  static_cast<unsigned long long>(hash));
    return storage::setting_file(name);
}
inline bool read_host_preferences(const std::string &path, HostPreferences &p)
{
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f)
        return false;
    unsigned char b[11]{};
    bool ok = std::fread(b, 1, sizeof(b), f) == sizeof(b) && std::fgetc(f) == EOF;
    std::fclose(f);
    if (!ok || b[0] != 'P' || b[1] != 'L' || b[2] != 'H' || b[3] != 1 || b[4] > 1 || b[5] > 2 ||
        b[6] > 2 || b[7] < 50 || b[7] > 100 || b[8] > 1 || b[9] > 1 || b[10] > 1)
        return false;
    p = {b[4] != 0, b[5], b[6], b[7], b[8] != 0, b[9] != 0, b[10] != 0};
    return true;
}
inline bool host_preferences_save(const moonlight_config_host_t &host, const HostPreferences &p);
inline HostPreferences host_preferences(const moonlight_config_host_t *host)
{
    HostPreferences p;
    p.optimize = client_preferences().optimize;
    p.mute_host = client_preferences().mute_host;
    p.quit_host = host_quit_enabled();
    if (host && !read_host_preferences(host_preferences_path(*host), p) && host->unique_id[0] &&
        read_host_preferences(host_preferences_path(*host, false), p))
        (void)host_preferences_save(*host, p);
    return p;
}
inline bool host_preferences_save(const moonlight_config_host_t &host, const HostPreferences &p)
{
    if (p.vrr > 2 || p.display > 2 || p.scale < 50 || p.scale > 100)
        return false;
    auto path = host_preferences_path(host), tmp = path + ".tmp";
    FILE *f = std::fopen(tmp.c_str(), "wb");
    if (!f)
        return false;
    const unsigned char b[] = {'P',
                               'L',
                               'H',
                               1,
                               static_cast<unsigned char>(p.extensions),
                               static_cast<unsigned char>(p.vrr),
                               static_cast<unsigned char>(p.display),
                               static_cast<unsigned char>(p.scale),
                               static_cast<unsigned char>(p.optimize),
                               static_cast<unsigned char>(p.mute_host),
                               static_cast<unsigned char>(p.quit_host)};
    bool ok = std::fwrite(b, 1, sizeof(b), f) == sizeof(b);
    int closed = std::fclose(f);
    if (!ok || closed || std::rename(tmp.c_str(), path.c_str()))
    {
        std::remove(tmp.c_str());
        return false;
    }
    // Identity has become available: replace the pre-pairing endpoint preference.
    if (host.unique_id[0])
        std::remove(host_preferences_path(host, false).c_str());
    return true;
}
inline void host_preferences_remove(const moonlight_config_host_t &host)
{
    std::remove(host_preferences_path(host).c_str());
    std::remove(host_preferences_path(host, false).c_str());
}
} // namespace prosperolight
