/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "fake_world.hpp"

#include "moonlight_backend.hpp"
#include "moonlight_discovery.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unistd.h>

namespace fake
{

namespace
{

std::mutex guard;
std::vector<Pc> pcs;
std::atomic<int> pair_state{MOONLIGHT_BACKEND_PAIR_IDLE};
std::string pair_host;
std::uint16_t pair_port = 0;
moonlight_backend_snapshot_t pair_snapshot;
std::vector<unsigned char> saved_file;
std::vector<unsigned char> temporary_file;
bool temporary_open = false;
std::size_t read_offset = 0;
int artwork_ready = 0;

Pc *find(const char *host, std::uint16_t port)
{
    for (Pc &pc : pcs)
    {
        if (pc.address == host && pc.port == (port ? port : 47989))
            return &pc;
    }
    return nullptr;
}

int describe(const char *host, std::uint16_t port, moonlight_backend_snapshot_t *snapshot)
{
    *snapshot = {};
    std::snprintf(snapshot->host, sizeof(snapshot->host), "%s", host);
    snapshot->http_port = port;
    const Pc *pc = find(host, port);
    if (!pc || !pc->online)
    {
        snapshot->result = -1;
        std::snprintf(snapshot->error, sizeof(snapshot->error), "Connection timed out");
        return -1;
    }
    snapshot->online = 1;
    snapshot->paired = pc->paired ? 1u : 0u;
    snapshot->https_port = static_cast<std::uint16_t>(pc->port - 5);
    snapshot->current_app_id = pc->current_app;
    snapshot->hevc_supported = pc->hevc ? 1u : 0u;
    snapshot->main10_supported = pc->main10 ? 1u : 0u;
    snapshot->pyrowave_profiles = pc->pyrowave_profiles;
    std::snprintf(snapshot->name, sizeof(snapshot->name), "%s", pc->name.c_str());
    std::snprintf(snapshot->unique_id, sizeof(snapshot->unique_id), "id-%s", pc->name.c_str());
    std::snprintf(snapshot->server_version, sizeof(snapshot->server_version), "7.1.431.-1");
    if (pc->paired)
    {
        for (const App &app : pc->apps)
        {
            snapshot->apps.emplace_back();
            moonlight_backend_app_t &out = snapshot->apps.back();
            ++snapshot->app_count;
            out.id = app.id;
            std::snprintf(out.name, sizeof(out.name), "%s", app.name.c_str());
        }
    }
    return 0;
}

} // namespace

std::vector<Pc> &world()
{
    return pcs;
}

void finish_pairing(bool accepted)
{
    std::lock_guard<std::mutex> lock(guard);
    if (pair_state.load() != MOONLIGHT_BACKEND_PAIR_WAITING)
        return;
    if (Pc *pc = find(pair_host.c_str(), pair_port))
        pc->paired = accepted;
    (void)describe(pair_host.c_str(), pair_port, &pair_snapshot);
    if (!accepted)
    {
        pair_snapshot.result = -1;
        std::snprintf(pair_snapshot.error, sizeof(pair_snapshot.error),
                      "The PIN was not typed in time");
    }
    pair_state.store(accepted ? MOONLIGHT_BACKEND_PAIR_SUCCEEDED : MOONLIGHT_BACKEND_PAIR_FAILED);
}

void erase_saved_file()
{
    saved_file.clear();
}

} // namespace fake

using namespace fake;

extern "C"
{

int moonlight_backend_refresh(const char *host, uint16_t http_port,
                              moonlight_backend_snapshot_t *snapshot)
{
    usleep(2000);
    std::lock_guard<std::mutex> lock(guard);
    return describe(host, http_port, snapshot);
}

int moonlight_backend_pair_start(const char *host, uint16_t http_port)
{
    std::lock_guard<std::mutex> lock(guard);
    pair_host = host;
    pair_port = http_port;
    pair_state.store(MOONLIGHT_BACKEND_PAIR_WAITING);
    return 0;
}

moonlight_backend_pair_state_t moonlight_backend_pair_poll(moonlight_backend_snapshot_t *snapshot,
                                                           char pin[5])
{
    std::lock_guard<std::mutex> lock(guard);
    const int state = pair_state.load();
    if (pin)
        std::snprintf(pin, 5, "%s", state >= MOONLIGHT_BACKEND_PAIR_WAITING ? "4827" : "");
    if (snapshot &&
        (state == MOONLIGHT_BACKEND_PAIR_SUCCEEDED || state == MOONLIGHT_BACKEND_PAIR_FAILED))
        *snapshot = pair_snapshot;
    return static_cast<moonlight_backend_pair_state_t>(state);
}

int moonlight_backend_unpair(const char *host, uint16_t http_port,
                             moonlight_backend_snapshot_t *snapshot)
{
    usleep(2000);
    std::lock_guard<std::mutex> lock(guard);
    if (Pc *pc = find(host, http_port))
        pc->paired = false;
    return describe(host, http_port, snapshot);
}

int moonlight_backend_stop_app(const char *host, uint16_t http_port,
                               moonlight_backend_snapshot_t *snapshot)
{
    usleep(2000);
    std::lock_guard<std::mutex> lock(guard);
    if (Pc *pc = find(host, http_port))
        pc->current_app = 0;
    return describe(host, http_port, snapshot);
}

int moonlight_backend_fetch_app_artwork(const char *, uint16_t, int app_id)
{
    usleep(1000);
    artwork_ready = app_id;
    return 0;
}

const unsigned char *moonlight_backend_find_app_artwork(int app_id, size_t *size)
{
    // The picture itself is drawn by the host program: one byte stands for it.
    static unsigned char marker[1];
    marker[0] = static_cast<unsigned char>(app_id);
    const bool ready = artwork_ready == app_id;
    if (size)
        *size = ready ? 1 : 0;
    return ready ? marker : nullptr;
}

void moonlight_backend_clear_app_artwork(void)
{
    artwork_ready = 0;
}

uint32_t moonlight_discover_hosts(moonlight_discovered_host_t *hosts, uint32_t capacity)
{
    usleep(4000);
    std::lock_guard<std::mutex> lock(guard);
    uint32_t count = 0;
    for (const Pc &pc : pcs)
    {
        if (!pc.online || !pc.discoverable || count == capacity)
            continue;
        std::snprintf(hosts[count].address, sizeof(hosts[count].address), "%s",
                      pc.address.c_str());
        std::snprintf(hosts[count].name, sizeof(hosts[count].name), "%s", pc.name.c_str());
        hosts[count].http_port = pc.port;
        ++count;
    }
    return count;
}

// ---- the saved file, kept in memory ----

int sceKernelOpen(const char *path, int flags, uint16_t)
{
    const bool temporary = std::strstr(path, ".tmp") != nullptr;
    if (flags != 0)
    {
        temporary_file.clear();
        temporary_open = true;
        return temporary ? 4 : 5;
    }
    if (saved_file.empty())
        return -1;
    read_offset = 0;
    return 3;
}

int64_t sceKernelRead(int, void *data, size_t size)
{
    const size_t left = saved_file.size() - read_offset;
    const size_t count = size < left ? size : left;
    std::memcpy(data, saved_file.data() + read_offset, count);
    read_offset += count;
    return static_cast<int64_t>(count);
}

int64_t sceKernelWrite(int, const void *data, size_t size)
{
    const unsigned char *bytes = static_cast<const unsigned char *>(data);
    temporary_file.insert(temporary_file.end(), bytes, bytes + size);
    return static_cast<int64_t>(size);
}

int sceKernelClose(int)
{
    return 0;
}

int sceKernelRename(const char *, const char *)
{
    if (!temporary_open)
        return -1;
    saved_file = temporary_file;
    temporary_open = false;
    return 0;
}

int sceKernelUnlink(const char *)
{
    return 0;
}

} // extern "C"
