/*
 * ps5-native-app-boilerplate - ProsperoLight: starting one stream from launch arguments.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// A home-screen forwarder (a tile of its own that opens ProsperoLight with
// launch arguments) names a PC and one of its apps; ProsperoLight then starts
// that stream instead of waiting on its launcher. See docs/FORWARDER.md.
#pragma once

#include "moonlight_backend.hpp"
#include "moonlight_config.hpp"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

namespace prosperolight
{

struct ForwardedLaunch
{
    std::string pc;  // a saved PC's name or address; empty: the PC last selected
    std::string app; // an app's name, or its number on the PC
    bool exit_after_stream = false;

    bool requested() const
    {
        return !app.empty();
    }
};

namespace forwarded_detail
{
inline std::string tidy(std::string_view text)
{
    while (!text.empty() &&
           (text.front() == ' ' || text.front() == '\t' || text.front() == '"'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' ||
                             text.back() == '\n' || text.back() == '"'))
        text.remove_suffix(1);
    return std::string(text.substr(0, 127));
}

inline bool same_text(std::string_view left, std::string_view right)
{
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0; index < left.size(); ++index)
    {
        const auto lower = [](char c)
        { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
        if (lower(left[index]) != lower(right[index]))
            return false;
    }
    return true;
}
} // namespace forwarded_detail

// --pc <name or address>, --app <name or number>, --exit-after-stream; each
// value may also follow an equals sign. Anything else is ignored: the system
// and other tools may pass arguments of their own.
inline ForwardedLaunch parse_forwarded_launch(int argc, char **argv)
{
    ForwardedLaunch result;
    if (argc <= 0 || !argv)
        return result;
    const auto value = [&](int &index, std::string_view name, std::string *out)
    {
        if (!argv[index])
            return false;
        const std::string_view argument{argv[index]};
        if (argument == name)
        {
            if (index + 1 < argc && argv[index + 1])
                *out = forwarded_detail::tidy(argv[++index]);
            return true;
        }
        if (argument.size() > name.size() && argument.substr(0, name.size()) == name &&
            argument[name.size()] == '=')
        {
            *out = forwarded_detail::tidy(argument.substr(name.size() + 1));
            return true;
        }
        return false;
    };
    for (int index = 0; index < argc; ++index)
    {
        if (!argv[index])
            break;
        if (std::string_view{argv[index]} == "--exit-after-stream")
            result.exit_after_stream = true;
        else if (!value(index, "--pc", &result.pc))
            (void)value(index, "--app", &result.app);
    }
    return result;
}

// The saved PC a forwarder means, or -1. A name is compared without regard to
// case; an address may carry the Sunshine port ("192.168.1.50:48989").
inline int forwarded_host(const moonlight_config_t &config, std::string_view pc)
{
    if (pc.empty())
        return config.host_count && config.selected_host < config.host_count
                   ? static_cast<int>(config.selected_host)
                   : -1;
    for (std::uint32_t index = 0; index < config.host_count; ++index)
    {
        const moonlight_config_host_t &host = config.hosts[index];
        if (forwarded_detail::same_text(host.name, pc) || pc == host.address ||
            pc == std::string(host.address) + ":" +
                      std::to_string(moonlight_config_host_port(&host)))
            return static_cast<int>(index);
    }
    return -1;
}

// The app on that PC, or -1. A number is the app's own number in Sunshine,
// which stays the same when the app is renamed; otherwise the name is
// compared without regard to case.
inline int forwarded_app(const moonlight_backend_snapshot_t &backend, std::string_view app)
{
    if (app.empty())
        return -1;
    bool number = true;
    for (char c : app)
        number = number && c >= '0' && c <= '9';
    if (number && app.size() <= 10)
    {
        const long long id = std::atoll(std::string(app).c_str());
        for (std::uint32_t index = 0; index < backend.app_count; ++index)
        {
            if (backend.apps[index].id == id)
                return static_cast<int>(index);
        }
    }
    for (std::uint32_t index = 0; index < backend.app_count; ++index)
    {
        if (forwarded_detail::same_text(backend.apps[index].name, app))
            return static_cast<int>(index);
    }
    return -1;
}

// What the process was started with. The launcher takes it once.
inline ForwardedLaunch &pending_forwarded_launch()
{
    static ForwardedLaunch pending;
    return pending;
}

} // namespace prosperolight
