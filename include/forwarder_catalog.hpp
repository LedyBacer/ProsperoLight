/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "moonlight_backend.hpp"
#include "moonlight_config.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

// What a forwarder tool needs to offer a choice instead of asking for names: the
// saved PCs, the apps each one lists, and their box art (docs/FORWARDER.md).
//
//   <root>/pcs/<key>.json            one paired PC and its apps
//   <root>/posters/<key>/<id>.png    one app's box art, as Sunshine sent it
//
// <key> is the PC's address and port. The launcher writes a PC's file when its
// app list arrives or changes, and a poster when it is fetched; nothing here is
// read back by ProsperoLight. Without a root (no filesystem access) it is off.
namespace prosperolight::forwarder_catalog
{

inline constexpr char kFormat[] = "prosperolight-forwarder-catalog/1";

namespace detail
{

inline std::string &root()
{
    static std::string value;
    return value;
}

// The document last written for each PC, so an unchanged list costs no write.
inline std::map<std::string, std::string> &written()
{
    static std::map<std::string, std::string> value;
    return value;
}

inline bool write_whole(const std::string &path, const void *data, std::size_t size)
{
    const std::string temporary = path + ".tmp";
    std::FILE *file = std::fopen(temporary.c_str(), "wb");
    if (!file)
        return false;
    const bool wrote = std::fwrite(data, 1, size, file) == size;
    const bool closed = std::fclose(file) == 0;
    if (wrote && closed && std::rename(temporary.c_str(), path.c_str()) == 0)
        return true;
    (void)std::remove(temporary.c_str());
    return false;
}

inline void remove_folder(const std::string &folder)
{
    if (DIR *directory = opendir(folder.c_str()))
    {
        while (const dirent *entry = readdir(directory))
            if (std::strcmp(entry->d_name, ".") != 0 && std::strcmp(entry->d_name, "..") != 0)
                (void)std::remove((folder + "/" + entry->d_name).c_str());
        closedir(directory);
    }
    (void)rmdir(folder.c_str());
}

} // namespace detail

inline bool enabled()
{
    return !detail::root().empty();
}

// Call once, before any thread uses the catalog. An empty folder turns it off.
inline void set_root(const char *folder)
{
    detail::root() = folder ? folder : "";
    detail::written().clear();
    if (!enabled())
        return;
    mkdir(detail::root().c_str(), 0777);
    mkdir((detail::root() + "/pcs").c_str(), 0777);
    mkdir((detail::root() + "/posters").c_str(), 0777);
}

// A file name for one PC: its address and port, with nothing a path could trip on.
inline std::string pc_key(const char *address, unsigned port)
{
    std::string key;
    for (const char *c = address ? address : ""; *c; ++c)
        key += (*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') ||
                       *c == '.' || *c == '-'
                   ? *c
                   : '_';
    return key + "_" + std::to_string(port);
}

inline std::string json_text(const char *text)
{
    std::string out = "\"";
    for (const char *c = text ? text : ""; *c; ++c)
    {
        const unsigned char byte = static_cast<unsigned char>(*c);
        if (byte == '"' || byte == '\\')
        {
            out += '\\';
            out += *c;
        }
        else if (byte < 0x20)
        {
            char escaped[8];
            std::snprintf(escaped, sizeof(escaped), "\\u%04x", byte);
            out += escaped;
        }
        else
            out += *c;
    }
    return out + "\"";
}

inline std::string pc_document(const char *name, const char *address, unsigned port,
                               const std::vector<moonlight_backend_app_t> &apps)
{
    std::string out = "{\n  \"format\": " + json_text(kFormat) +
                      ",\n  \"key\": " + json_text(pc_key(address, port).c_str()) +
                      ",\n  \"name\": " + json_text(name && name[0] ? name : address) +
                      ",\n  \"address\": " + json_text(address) +
                      ",\n  \"port\": " + std::to_string(port) + ",\n  \"apps\": [";
    for (std::size_t i = 0; i < apps.size(); ++i)
        out += std::string(i ? "," : "") + "\n    {\"id\": " + std::to_string(apps[i].id) +
               ", \"name\": " + json_text(apps[i].name) + "}";
    return out + (apps.empty() ? "" : "\n  ") + "]\n}\n";
}

// Main thread. True when the file was written (the list was new or had changed).
inline bool publish_pc(const char *name, const char *address, unsigned port,
                       const std::vector<moonlight_backend_app_t> &apps)
{
    if (!enabled() || !address || !address[0] || apps.empty())
        return false;
    const std::string key = pc_key(address, port);
    std::string document = pc_document(name, address, port, apps);
    std::string &last = detail::written()[key];
    if (last == document)
        return false;
    if (!detail::write_whole(detail::root() + "/pcs/" + key + ".json", document.data(),
                             document.size()))
        return false;
    last = std::move(document);
    return true;
}

inline std::string poster_path(const char *address, unsigned port, int app_id)
{
    return detail::root() + "/posters/" + pc_key(address, port) + "/" + std::to_string(app_id) +
           ".png";
}

inline bool has_poster(const char *address, unsigned port, int app_id)
{
    struct stat status;
    return enabled() && stat(poster_path(address, port, app_id).c_str(), &status) == 0 &&
           status.st_size > 0;
}

// Any thread: each poster is a file of its own. Only a real PNG is kept.
inline bool save_poster(const char *address, unsigned port, int app_id, const unsigned char *png,
                        std::size_t size)
{
    static const unsigned char kSignature[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    if (!enabled() || !address || !address[0] || !png || size <= sizeof(kSignature) ||
        std::memcmp(png, kSignature, sizeof(kSignature)) != 0)
        return false;
    mkdir((detail::root() + "/posters/" + pc_key(address, port)).c_str(), 0777);
    return detail::write_whole(poster_path(address, port, app_id), png, size);
}

inline void remove_key(const std::string &key)
{
    if (!enabled() || key.empty())
        return;
    (void)std::remove((detail::root() + "/pcs/" + key + ".json").c_str());
    detail::remove_folder(detail::root() + "/posters/" + key);
    detail::written().erase(key);
}

// The PC was removed from the list.
inline void remove_pc(const char *address, unsigned port)
{
    remove_key(pc_key(address, port));
}

// Drops what belongs to PCs that are no longer saved. Once, at start.
inline void prune(const moonlight_config_t &config)
{
    if (!enabled())
        return;
    std::vector<std::string> stale;
    if (DIR *directory = opendir((detail::root() + "/pcs").c_str()))
    {
        while (const dirent *entry = readdir(directory))
        {
            const std::string file = entry->d_name;
            if (file.size() <= 5 || file.compare(file.size() - 5, 5, ".json") != 0)
                continue;
            const std::string key = file.substr(0, file.size() - 5);
            bool saved = false;
            for (std::uint32_t i = 0; i < config.host_count && !saved; ++i)
                saved = key == pc_key(config.hosts[i].address,
                                      moonlight_config_host_port(&config.hosts[i]));
            if (!saved)
                stale.push_back(key);
        }
        closedir(directory);
    }
    for (const std::string &key : stale)
        remove_key(key);
}

} // namespace prosperolight::forwarder_catalog
