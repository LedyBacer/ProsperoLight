/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "moonlight_backend.hpp"
#include "moonlight_config.hpp"
#include "moonlight_discovery.hpp"
#include "moonlight_health.hpp"

#include <pthread.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace launcher
{

// What the launcher last learned about one saved PC.
struct HostStatus
{
    bool known = false; // false until the PC has answered or failed once
    bool online = false;
    bool paired = false;
    unsigned app_count = 0;
    int current_app_id = 0;
    char running[64] = {}; // name of the app Sunshine is running, when it is in the list
};

enum class NoticeKind
{
    info,
    success,
    warning,
    error,
};

// One message for the player: the outcome of something they asked for.
struct Notice
{
    NoticeKind kind = NoticeKind::info;
    std::string title;
    std::string body;
};

// One app's box art, decoded: RGBA, top row first.
struct ArtworkImage
{
    int app_id = 0;
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;
};

// Turns the PNG Sunshine sent into pixels. It runs on the worker thread.
using ArtworkDecoder = bool (*)(const unsigned char *png, std::size_t size, ArtworkImage *image);

// The job the player is waiting for, if any.
enum class Busy
{
    none,
    searching,
    refreshing,
    unpairing,
    stopping,
};

// Everything the launcher knows and does, with no drawing in it: saved PCs
// and settings, Sunshine's state, pairing, and the requests that reach the
// network. Requests run one at a time on a worker thread, so the screen keeps
// moving while a PC is slow to answer; Poll() applies what they return.
class Model
{
  public:
    Model() = default;
    Model(const Model &) = delete;
    Model &operator=(const Model &) = delete;
    ~Model();

    void Initialize(std::uint64_t now_ms);
    void Poll(std::uint64_t now_ms);
    // Finishes the request in flight and saves pending settings.
    void Shutdown();

    // ---- what is known ----
    const moonlight_config_t &config() const
    {
        return config_;
    }
    const moonlight_config_host_t *selected_host() const;
    // Sunshine's state on the selected PC; valid() is false until it answered.
    const moonlight_backend_snapshot_t &backend() const
    {
        return backend_;
    }
    bool backend_valid() const
    {
        return backend_valid_;
    }
    HostStatus host_status(unsigned index) const;
    bool reconnecting() const
    {
        return health_.Reconnecting();
    }
    Busy busy() const;
    // Changes whenever the PC list, a PC's state or the app list changed.
    unsigned revision() const
    {
        return revision_;
    }
    unsigned selected_app() const
    {
        return selected_app_;
    }
    void SelectApp(unsigned index);

    // ---- pairing ----
    bool pairing() const
    {
        return pairing_requested_ || pairing_active_;
    }
    // Empty until Sunshine is ready for it.
    const char *pairing_pin() const
    {
        return pairing_pin_;
    }
    float pairing_seconds_left() const;

    // ---- requests ----
    void SelectHost(unsigned index);
    void Refresh(bool discover);
    void StartPairing();
    void Unpair();
    void RemoveHost();
    // On failure *error says what to type instead.
    bool AddHost(const char *text, std::string *error);
    bool SetPort(const char *text, std::string *error);
    void StopApp();
    // True when the stream may start; otherwise a notice says why not.
    bool RequestStream();
    // Settings: change the returned config, then call SettingsChanged().
    moonlight_config_t &settings()
    {
        return config_;
    }
    void SettingsChanged();

    // ---- box art ----
    void set_artwork_decoder(ArtworkDecoder decoder)
    {
        artwork_decoder_ = decoder;
    }
    // Runs first on every worker thread. On the console it moves the thread
    // off the screen's CPU: threads there are never time-sliced, and a TLS
    // request froze the screen for a second.
    using WorkerStart = void (*)();
    void set_worker_start(WorkerStart start)
    {
        worker_start_ = start;
    }
    // Asks for one app's box art from the selected PC (once per app).
    void RequestArtwork(int app_id);
    // The next picture that arrived, if any.
    bool TakeArtwork(ArtworkImage *image);

    std::vector<Notice> TakeNotices();

  private:
    enum class JobKind
    {
        none,
        discover,
        refresh, // the selected PC, asked for by the player
        health,  // the selected PC, on a timer
        sweep,   // another saved PC, for the list
        unpair,
        stop,
        artwork,
    };
    struct Job
    {
        JobKind kind = JobKind::none;
        char host[MOONLIGHT_CONFIG_ADDRESS_SIZE] = {};
        std::uint16_t port = 0;
        std::uint16_t https_port = 0;
        int app_id = 0;
        bool announce = false; // tell the player when the PC does not answer
    };
    struct JobResult
    {
        int result = 0;
        moonlight_backend_snapshot_t snapshot{};
        moonlight_discovered_host_t found[MOONLIGHT_DISCOVERY_MAX_HOSTS]{};
        std::uint32_t found_count = 0;
        ArtworkImage image;
    };
    struct StatusEntry
    {
        char host[MOONLIGHT_CONFIG_ADDRESS_SIZE] = {};
        std::uint16_t port = 0;
        HostStatus status;
    };
    static void *Worker(void *self);
    void Run(const Job &job, JobResult *result);
    bool StartNext();
    void Apply(const Job &job, JobResult &result);
    void ApplyDiscovery(const JobResult &result);
    bool ApplySelected(const Job &job, const JobResult &result);
    void PollPairing();
    void QueueSelectedRefresh(bool announce = false);
    void QueueSweep();
    void DropSelectedRequests();
    void ResetSelected();
    bool IsSelected(const char *host, std::uint16_t port) const;
    // Both return whether anything the screens show has changed.
    bool Remember(const char *host, std::uint16_t port, const moonlight_backend_snapshot_t &state,
                  bool online);
    bool Upsert(const char *host, std::uint16_t port, const moonlight_backend_snapshot_t &state);
    void Notify(NoticeKind kind, std::string title, std::string body = {});
    void Save();

    moonlight_config_t config_{};
    moonlight_backend_snapshot_t backend_{};
    bool backend_valid_ = false;
    MoonlightHealthState health_{};
    std::uint64_t health_due_ms_ = 0;
    std::uint64_t now_ms_ = 0;
    unsigned selected_app_ = 0;
    unsigned revision_ = 1;
    std::vector<StatusEntry> statuses_;
    std::vector<Notice> notices_;

    // The worker and what waits for it.
    bool worker_active_ = false;
    volatile int worker_done_ = 0;
    pthread_t worker_{};
    Job job_{};
    JobResult result_{};
    std::deque<Job> requests_; // asked for by the player: before everything else
    std::deque<Job> sweeps_;
    std::deque<int> artwork_wanted_;
    std::vector<int> artwork_asked_;
    std::deque<ArtworkImage> artwork_ready_;
    ArtworkDecoder artwork_decoder_ = nullptr;
    WorkerStart worker_start_ = nullptr;

    bool pairing_requested_ = false;
    bool pairing_active_ = false;
    moonlight_backend_pair_state_t pairing_state_ = MOONLIGHT_BACKEND_PAIR_IDLE;
    std::uint64_t pairing_started_ms_ = 0;
    char pairing_pin_[5] = {};

    bool save_pending_ = false;
    std::uint64_t save_due_ms_ = 0;
};

} // namespace launcher
