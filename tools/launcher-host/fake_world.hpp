/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// A pretend network for the launcher on the PC: a few PCs running Sunshine,
// answered by the same functions the console build calls.

#pragma once

#include "moonlight_wol.hpp"

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
    bool keeps_app = false; // answers a stop with "done" and keeps the app running
    std::uint8_t mac[MOONLIGHT_WOL_MAC_BYTES] = {};
    bool mac_valid = false;
    std::vector<App> apps;
};

std::vector<Pc> &world();
// Ends the pairing request in flight, as Sunshine does when the PIN is typed.
void finish_pairing(bool accepted);
// Forgets the saved file, so the next launcher starts as a first run.
void erase_saved_file();
// Deterministic wake transport for model tests: no socket and no live traffic.
int send_wake(const std::uint8_t mac[MOONLIGHT_WOL_MAC_BYTES], const char *host,
              std::uint16_t http_base_port, moonlight_wol_send_result *out_result);
void reset_wake();
void set_wake_failure(bool fails);
int wake_sends();
int wake_last_mac_valid();
int refresh_calls();
void reset_refresh_calls();
// Deterministic scenario controls: change one pretend PC without touching live I/O.
void set_pc_online(unsigned index, bool online);
void set_pc_mac(unsigned index, const std::uint8_t mac[MOONLIGHT_WOL_MAC_BYTES], bool valid);

} // namespace fake
