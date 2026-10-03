/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// A pretend network for the launcher on the PC: a few PCs running Sunshine,
// answered by the same functions the console build calls.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fake
{

struct App
{
    int id;
    std::string name;
};

struct Pc
{
    std::string address;
    std::uint16_t port = 47989;
    std::string name;
    bool online = true;
    bool paired = true;
    bool discoverable = true;
    bool hevc = true;
    bool main10 = true;
    std::uint32_t pyrowave_profiles = 0;
    int current_app = 0;
    std::vector<App> apps;
};

std::vector<Pc> &world();
// Ends the pairing request in flight, as Sunshine does when the PIN is typed.
void finish_pairing(bool accepted);
// Forgets the saved file, so the next launcher starts as a first run.
void erase_saved_file();

} // namespace fake
