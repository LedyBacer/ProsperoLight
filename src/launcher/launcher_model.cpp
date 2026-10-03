/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "launcher/launcher_model.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace launcher
{

namespace
{
// The worker's stack: the TLS requests to Sunshine run on it.
constexpr std::size_t kWorkerStackBytes = 1u << 20;
} // namespace

namespace
{

// Settings changed with a slider are written once the player pauses.
constexpr std::uint64_t kSaveDelayMs = 600;

void Copy(char *output, std::size_t capacity, const char *text)
{
    std::snprintf(output, capacity, "%s", text ? text : "");
}

} // namespace

Model::~Model()
{
    Shutdown();
}

void Model::Initialize(std::uint64_t now_ms)
{
    now_ms_ = now_ms;
    (void)moonlight_config_load(&config_);
    ResetSelected();
    if (selected_host())
    {
        QueueSelectedRefresh();
        QueueSweep();
    }
    else
    {
        Job job;
        job.kind = JobKind::discover;
        requests_.push_back(job);
    }
}

void Model::Shutdown()
{
    if (worker_active_)
    {
        (void)pthread_join(worker_, nullptr);
        worker_active_ = false;
    }
    requests_.clear();
    sweeps_.clear();
    artwork_wanted_.clear();
    if (save_pending_)
        Save();
}

const moonlight_config_host_t *Model::selected_host() const
{
    return config_.host_count && config_.selected_host < config_.host_count
               ? &config_.hosts[config_.selected_host]
               : nullptr;
}

HostStatus Model::host_status(unsigned index) const
{
    if (index >= config_.host_count)
        return {};
    const moonlight_config_host_t &host = config_.hosts[index];
    const std::uint16_t port = moonlight_config_host_port(&host);
    for (const StatusEntry &entry : statuses_)
    {
        if (entry.port == port && std::strcmp(entry.host, host.address) == 0)
            return entry.status;
    }
    return {};
}

Busy Model::busy() const
{
    JobKind kind = worker_active_ ? job_.kind : JobKind::none;
    if (kind != JobKind::discover && kind != JobKind::refresh && kind != JobKind::unpair &&
        kind != JobKind::stop)
        kind = requests_.empty() ? JobKind::none : requests_.front().kind;
    switch (kind)
    {
    case JobKind::discover:
        return Busy::searching;
    case JobKind::refresh:
        return Busy::refreshing;
    case JobKind::unpair:
        return Busy::unpairing;
    case JobKind::stop:
        return Busy::stopping;
    default:
        return Busy::none;
    }
}

void Model::SelectApp(unsigned index)
{
    if (index < backend_.app_count)
        selected_app_ = index;
}

float Model::pairing_seconds_left() const
{
    if (!pairing_active_)
        return static_cast<float>(MOONLIGHT_BACKEND_PAIR_TIMEOUT_SECONDS);
    const float elapsed = static_cast<float>(now_ms_ - pairing_started_ms_) / 1000.0f;
    return std::max(0.0f, static_cast<float>(MOONLIGHT_BACKEND_PAIR_TIMEOUT_SECONDS) - elapsed);
}

void Model::Notify(NoticeKind kind, std::string title, std::string body)
{
    notices_.push_back({kind, std::move(title), std::move(body)});
}

std::vector<Notice> Model::TakeNotices()
{
    std::vector<Notice> taken;
    taken.swap(notices_);
    return taken;
}

void Model::Save()
{
    save_pending_ = false;
    if (!moonlight_config_save(&config_))
        Notify(NoticeKind::error, "Settings were not saved", "The console refused the file.");
}

void Model::SettingsChanged()
{
    save_pending_ = true;
    save_due_ms_ = now_ms_ + kSaveDelayMs;
}

bool Model::IsSelected(const char *host, std::uint16_t port) const
{
    const moonlight_config_host_t *selected = selected_host();
    return selected && moonlight_config_host_port(selected) == port &&
           std::strcmp(selected->address, host) == 0;
}

// The selected PC changed: forget what the last one said.
void Model::ResetSelected()
{
    std::memset(&backend_, 0, sizeof(backend_));
    backend_valid_ = false;
    health_ = {};
    health_due_ms_ = 0;
    selected_app_ = 0;
    artwork_wanted_.clear();
    artwork_asked_.clear();
    artwork_ready_.clear();
    if (const moonlight_config_host_t *host = selected_host())
    {
        Copy(backend_.host, sizeof(backend_.host), host->address);
        backend_.http_port = moonlight_config_host_port(host);
        Copy(backend_.name, sizeof(backend_.name), host->name);
    }
    ++revision_;
}

void Model::DropSelectedRequests()
{
    requests_.erase(std::remove_if(requests_.begin(), requests_.end(),
                                   [](const Job &job) { return job.kind != JobKind::discover; }),
                    requests_.end());
}

void Model::QueueSelectedRefresh(bool announce)
{
    const moonlight_config_host_t *host = selected_host();
    if (!host)
        return;
    for (Job &queued : requests_)
    {
        if (queued.kind == JobKind::refresh)
        {
            queued.announce = queued.announce || announce;
            return;
        }
    }
    Job job;
    job.kind = JobKind::refresh;
    job.announce = announce;
    Copy(job.host, sizeof(job.host), host->address);
    job.port = moonlight_config_host_port(host);
    requests_.push_back(job);
}

// The other saved PCs are asked once each, so the list can show their state.
void Model::QueueSweep()
{
    sweeps_.clear();
    for (unsigned index = 0; index < config_.host_count; ++index)
    {
        if (index == config_.selected_host)
            continue;
        Job job;
        job.kind = JobKind::sweep;
        Copy(job.host, sizeof(job.host), config_.hosts[index].address);
        job.port = moonlight_config_host_port(&config_.hosts[index]);
        sweeps_.push_back(job);
    }
}

void Model::SelectHost(unsigned index)
{
    if (index >= config_.host_count || index == config_.selected_host)
        return;
    config_.selected_host = index;
    SettingsChanged();
    DropSelectedRequests();
    ResetSelected();
    QueueSelectedRefresh();
}

void Model::Refresh(bool discover)
{
    if (discover)
    {
        for (const Job &queued : requests_)
        {
            if (queued.kind == JobKind::discover)
                return;
        }
        Job job;
        job.kind = JobKind::discover;
        requests_.push_back(job);
        return;
    }
    QueueSelectedRefresh(true);
}

void Model::StartPairing()
{
    const moonlight_config_host_t *host = selected_host();
    if (!host)
    {
        Notify(NoticeKind::error, "No PC to pair", "Add a Sunshine PC first.");
        return;
    }
    if (pairing())
        return;
    if (!backend_.online)
    {
        Notify(NoticeKind::error, "Sunshine is not answering",
               "The PC must be online before pairing.");
        return;
    }
    if (backend_.paired)
        return;
    if (backend_.current_app_id)
    {
        Notify(NoticeKind::error, "An app is running on this PC",
               "Stop it on the device that started it, then search again.");
        return;
    }
    pairing_requested_ = true;
    pairing_pin_[0] = '\0';
}

void Model::Unpair()
{
    if (!selected_host() || pairing())
        return;
    if (!backend_.online)
    {
        Notify(NoticeKind::error, "Sunshine is not answering", "The PC must be online to unpair.");
        return;
    }
    if (backend_.current_app_id)
    {
        Notify(NoticeKind::error, "An app is running", "Stop it before unpairing.");
        return;
    }
    Job job;
    job.kind = JobKind::unpair;
    Copy(job.host, sizeof(job.host), selected_host()->address);
    job.port = moonlight_config_host_port(selected_host());
    requests_.push_back(job);
}

void Model::RemoveHost()
{
    const moonlight_config_host_t *host = selected_host();
    if (!host || pairing())
        return;
    const std::string name = host->name[0] ? host->name : host->address;
    moonlight_config_t updated = config_;
    if (!moonlight_config_remove_host(&updated, config_.selected_host) ||
        !moonlight_config_save(&updated))
    {
        Notify(NoticeKind::error, "Could not remove this PC", "The PC list was not saved.");
        return;
    }
    config_ = updated;
    save_pending_ = false;
    if (config_.host_count && config_.selected_host >= config_.host_count)
        config_.selected_host = config_.host_count - 1;
    DropSelectedRequests();
    ResetSelected();
    QueueSelectedRefresh();
    Notify(NoticeKind::success, name + " removed", "Sunshine still has this PS5 in its list.");
}

bool Model::AddHost(const char *text, std::string *error)
{
    char address[MOONLIGHT_CONFIG_ADDRESS_SIZE];
    std::uint16_t port = 0;
    if (!moonlight_config_parse_endpoint(text, address, &port))
    {
        *error = "Type an address such as 192.168.1.50, or 192.168.1.50:48989";
        return false;
    }
    // Without a port, a PC already saved at this address keeps the port it has.
    const int index =
        moonlight_config_upsert_host(&config_, address, port, "Sunshine PC", "", true);
    if (index < 0)
    {
        *error = "The list is full. Remove a PC first.";
        return false;
    }
    config_.selected_host = static_cast<std::uint32_t>(index);
    Save();
    DropSelectedRequests();
    ResetSelected();
    QueueSelectedRefresh();
    return true;
}

bool Model::SetPort(const char *text, std::string *error)
{
    std::uint16_t port = MOONLIGHT_CONFIG_DEFAULT_HTTP_PORT;
    const bool blank = !text || text[std::strspn(text, " \t")] == '\0';
    if (!selected_host() || (!blank && !moonlight_config_parse_port(text, &port)))
    {
        *error = "Type a port from 1 to 65535";
        return false;
    }
    const int index = moonlight_config_set_host_port(&config_, config_.selected_host, port);
    if (index < 0)
    {
        *error = "Could not change the port of this PC";
        return false;
    }
    config_.selected_host = static_cast<std::uint32_t>(index);
    Save();
    DropSelectedRequests();
    ResetSelected();
    QueueSelectedRefresh();
    return true;
}

void Model::StopApp()
{
    if (!selected_host() || pairing())
        return;
    if (!backend_.current_app_id)
    {
        Notify(NoticeKind::info, "Nothing is running on this PC");
        return;
    }
    if (!backend_.paired)
    {
        Notify(NoticeKind::error, "This PS5 is not paired",
               "Stop the app on the device that started it, or in Sunshine.");
        return;
    }
    for (const Job &queued : requests_)
    {
        if (queued.kind == JobKind::stop)
            return;
    }
    if (worker_active_ && job_.kind == JobKind::stop)
        return;
    Job job;
    job.kind = JobKind::stop;
    Copy(job.host, sizeof(job.host), selected_host()->address);
    job.port = moonlight_config_host_port(selected_host());
    requests_.push_back(job);
}

bool Model::RequestStream()
{
    if (pairing() || selected_app_ >= backend_.app_count)
        return false;
    if (!backend_.online)
    {
        Notify(NoticeKind::error,
               health_.Reconnecting() ? "Sunshine is reconnecting" : "Sunshine is not answering",
               health_.Reconnecting() ? "Try again in a moment." : "The PC is offline.");
        return false;
    }
    if (config_.video_codec == MOONLIGHT_VIDEO_CODEC_HEVC && !backend_.hevc_supported)
    {
        Notify(NoticeKind::error, "This PC cannot encode HEVC",
               "Choose H.264 in Settings, or change the encoder on the PC.");
        return false;
    }
    if (config_.hdr_enabled && !backend_.main10_supported)
    {
        Notify(NoticeKind::error, "This PC cannot encode HDR",
               "Turn HDR off in Settings, or enable HEVC Main10 on the PC.");
        return false;
    }
    return true;
}

void Model::RequestArtwork(int app_id)
{
    if (app_id <= 0 || !backend_.online || !backend_.paired || !backend_.https_port)
        return;
    if (std::find(artwork_asked_.begin(), artwork_asked_.end(), app_id) != artwork_asked_.end())
        return;
    artwork_asked_.push_back(app_id);
    artwork_wanted_.push_back(app_id);
}

bool Model::TakeArtwork(ArtworkImage *image)
{
    if (artwork_ready_.empty())
        return false;
    *image = std::move(artwork_ready_.front());
    artwork_ready_.pop_front();
    return true;
}

// ---- the worker --------------------------------------------------------

void *Model::Worker(void *self)
{
    Model *model = static_cast<Model *>(self);
    model->Run(model->job_, &model->result_);
    __atomic_store_n(&model->worker_done_, 1, __ATOMIC_RELEASE);
    return nullptr;
}

void Model::Run(const Job &job, JobResult *result)
{
    switch (job.kind)
    {
    case JobKind::discover:
        result->found_count =
            moonlight_discover_hosts(result->found, MOONLIGHT_DISCOVERY_MAX_HOSTS);
        break;
    case JobKind::refresh:
    case JobKind::health:
    case JobKind::sweep:
        result->result = moonlight_backend_refresh(job.host, job.port, &result->snapshot);
        break;
    case JobKind::unpair:
        std::printf("[PL] launcher: unpairing %s:%u\n", job.host, job.port);
        result->result = moonlight_backend_unpair(job.host, job.port, &result->snapshot);
        std::printf("[PL] launcher: unpair answered %d\n", result->result);
        break;
    case JobKind::stop:
        std::printf("[PL] launcher: stopping the app on %s:%u\n", job.host, job.port);
        result->result = moonlight_backend_stop_app(job.host, job.port, &result->snapshot);
        std::printf("[PL] launcher: stop answered %d (online=%u running=%d)\n", result->result,
                    result->snapshot.online, result->snapshot.current_app_id);
        break;
    case JobKind::artwork:
    {
        // The backend keeps a few pictures; this is the only thread that
        // touches them, and each one is taken out as soon as it arrives.
        result->result = moonlight_backend_fetch_app_artwork(job.host, job.https_port, job.app_id);
        std::size_t size = 0;
        const unsigned char *png = moonlight_backend_find_app_artwork(job.app_id, &size);
        if (png && size && artwork_decoder_ && artwork_decoder_(png, size, &result->image))
            result->image.app_id = job.app_id;
        else
            result->image = {};
        moonlight_backend_clear_app_artwork();
        break;
    }
    default:
        break;
    }
}

bool Model::StartNext()
{
    Job next;
    if (!requests_.empty())
    {
        next = requests_.front();
        requests_.pop_front();
    }
    else if (selected_host() && now_ms_ >= health_due_ms_ && backend_valid_)
    {
        next.kind = JobKind::health;
        Copy(next.host, sizeof(next.host), selected_host()->address);
        next.port = moonlight_config_host_port(selected_host());
    }
    else if (!sweeps_.empty())
    {
        next = sweeps_.front();
        sweeps_.pop_front();
    }
    else if (!artwork_wanted_.empty() && selected_host() && backend_.online && backend_.paired)
    {
        next.kind = JobKind::artwork;
        Copy(next.host, sizeof(next.host), selected_host()->address);
        next.port = moonlight_config_host_port(selected_host());
        next.https_port = backend_.https_port;
        next.app_id = artwork_wanted_.front();
        artwork_wanted_.pop_front();
    }
    else
    {
        return false;
    }

    job_ = next;
    result_ = {};
    __atomic_store_n(&worker_done_, 0, __ATOMIC_RELEASE);
    // Stopping an app or unpairing runs a TLS request and then a refresh inside
    // it, each with its own identity and server on the stack: more than a
    // thread's default stack holds. The old launcher ran them on the main thread.
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setstacksize(&attributes, kWorkerStackBytes);
    const int created = pthread_create(&worker_, &attributes, Worker, this);
    pthread_attr_destroy(&attributes);
    if (created != 0)
    {
        // Without a thread the request runs here; the screen waits for it.
        Run(job_, &result_);
        Apply(job_, result_);
        return true;
    }
    worker_active_ = true;
    return true;
}

void Model::Poll(std::uint64_t now_ms)
{
    now_ms_ = now_ms;
    if (worker_active_ && __atomic_load_n(&worker_done_, __ATOMIC_ACQUIRE) != 0)
    {
        (void)pthread_join(worker_, nullptr);
        worker_active_ = false;
        Apply(job_, result_);
        result_ = {};
    }

    // Pairing uses the same connection state as the worker: one at a time.
    if (pairing_requested_ && !worker_active_)
    {
        pairing_requested_ = false;
        const moonlight_config_host_t *host = selected_host();
        const int result =
            host ? moonlight_backend_pair_start(host->address, moonlight_config_host_port(host))
                 : -1;
        if (result != 0)
        {
            moonlight_backend_snapshot_t failed{};
            (void)moonlight_backend_pair_poll(&failed, nullptr);
            Notify(NoticeKind::error, "Pairing did not start",
                   failed.error[0] ? failed.error : "Sunshine refused the request.");
        }
        else
        {
            pairing_active_ = true;
            pairing_state_ = MOONLIGHT_BACKEND_PAIR_PREPARING;
            pairing_started_ms_ = now_ms_;
        }
    }
    if (pairing_active_)
        PollPairing();

    if (!worker_active_ && !pairing())
        (void)StartNext();

    if (save_pending_ && now_ms_ >= save_due_ms_)
        Save();
}

void Model::PollPairing()
{
    char pin[5] = {};
    moonlight_backend_snapshot_t completed{};
    const moonlight_backend_pair_state_t state = moonlight_backend_pair_poll(&completed, pin);
    if (pin[0])
        std::memcpy(pairing_pin_, pin, sizeof(pairing_pin_));
    pairing_state_ = state;
    if (state != MOONLIGHT_BACKEND_PAIR_SUCCEEDED && state != MOONLIGHT_BACKEND_PAIR_FAILED)
        return;

    pairing_active_ = false;
    pairing_pin_[0] = '\0';
    const bool paired = state == MOONLIGHT_BACKEND_PAIR_SUCCEEDED;
    const char *host = completed.host;
    if (IsSelected(host, completed.http_port))
    {
        // A failed attempt still tells whether the PC is online.
        const std::string name = backend_.name;
        backend_ = completed;
        if (!backend_.name[0])
            Copy(backend_.name, sizeof(backend_.name), name.c_str());
        backend_valid_ = true;
        health_due_ms_ = now_ms_ + health_.Record(backend_.online != 0);
        if (selected_app_ >= backend_.app_count)
            selected_app_ = 0;
    }
    (void)Remember(host, completed.http_port, completed, completed.online != 0);
    if (paired)
        Notify(NoticeKind::success, std::string("Paired with ") + backend_.name);
    else
        Notify(NoticeKind::error, "Pairing failed",
               completed.error[0] ? completed.error : "The PIN was not accepted in time.");
    ++revision_;
}

bool Model::Remember(const char *host, std::uint16_t port,
                     const moonlight_backend_snapshot_t &state, bool online)
{
    StatusEntry *entry = nullptr;
    for (StatusEntry &candidate : statuses_)
    {
        if (candidate.port == port && std::strcmp(candidate.host, host) == 0)
            entry = &candidate;
    }
    if (!entry)
    {
        statuses_.emplace_back();
        entry = &statuses_.back();
        Copy(entry->host, sizeof(entry->host), host);
        entry->port = port;
    }
    HostStatus &status = entry->status;
    const HostStatus before = status;
    status.known = true;
    status.online = online;
    if (online)
    {
        status.paired = state.paired != 0;
        status.app_count = state.app_count;
        status.current_app_id = state.current_app_id;
        status.running[0] = '\0';
        for (unsigned index = 0; index < state.app_count; ++index)
        {
            if (state.apps[index].id == state.current_app_id)
                Copy(status.running, sizeof(status.running), state.apps[index].name);
        }
    }
    return before.known != status.known || before.online != status.online ||
           before.paired != status.paired || before.app_count != status.app_count ||
           before.current_app_id != status.current_app_id ||
           std::strcmp(before.running, status.running) != 0;
}

// Stores what a PC said about itself. Sunshine's identity can merge two saved
// entries into one, which moves indexes: the selection follows its PC.
bool Model::Upsert(const char *host, std::uint16_t port, const moonlight_backend_snapshot_t &state)
{
    char selected_address[MOONLIGHT_CONFIG_ADDRESS_SIZE] = {};
    std::uint16_t selected_port = 0;
    if (const moonlight_config_host_t *selected = selected_host())
    {
        Copy(selected_address, sizeof(selected_address), selected->address);
        selected_port = moonlight_config_host_port(selected);
    }
    bool manual = false;
    for (unsigned index = 0; index < config_.host_count; ++index)
    {
        if (moonlight_config_host_port(&config_.hosts[index]) == port &&
            std::strcmp(config_.hosts[index].address, host) == 0)
            manual = config_.hosts[index].manual != 0;
    }
    const moonlight_config_t before = config_;
    const int index =
        moonlight_config_upsert_host(&config_, host, port, state.name, state.unique_id, manual);
    if (index < 0)
        return false;
    const bool was_selected = selected_port == port && std::strcmp(selected_address, host) == 0;
    if (was_selected)
    {
        config_.selected_host = static_cast<std::uint32_t>(index);
    }
    else
    {
        for (unsigned candidate = 0; candidate < config_.host_count; ++candidate)
        {
            if (moonlight_config_host_port(&config_.hosts[candidate]) == selected_port &&
                std::strcmp(config_.hosts[candidate].address, selected_address) == 0)
                config_.selected_host = candidate;
        }
    }
    if (std::memcmp(&before, &config_, sizeof(config_)) == 0)
        return false;
    Save();
    return true;
}

void Model::ApplyDiscovery(const JobResult &result)
{
    const bool had_host = selected_host() != nullptr;
    const moonlight_config_t before = config_;
    unsigned added = 0;
    for (std::uint32_t index = 0; index < result.found_count; ++index)
    {
        const std::uint32_t count = config_.host_count;
        (void)moonlight_config_upsert_host(&config_, result.found[index].address,
                                           result.found[index].http_port, result.found[index].name,
                                           "", false);
        if (config_.host_count > count)
            ++added;
    }
    if (std::memcmp(&before, &config_, sizeof(config_)) != 0)
        Save();
    if (!had_host && selected_host())
        ResetSelected();
    if (added == 1)
        Notify(NoticeKind::success, "Found a new PC");
    else if (added > 1)
        Notify(NoticeKind::success, "Found " + std::to_string(added) + " new PCs");
    else if (!selected_host())
        Notify(NoticeKind::info, "No PC found on this network",
               "Start Sunshine on the PC, or add it by its address.");
    QueueSelectedRefresh();
    QueueSweep();
    ++revision_;
}

bool Model::ApplySelected(const Job &job, const JobResult &result)
{
    const moonlight_backend_snapshot_t before = backend_;
    const bool was_valid = backend_valid_;
    const bool was_reconnecting = health_.Reconnecting();
    bool changed = false;
    const bool success = result.result == 0 && result.snapshot.online;
    health_due_ms_ = now_ms_ + health_.Record(success);
    if (success || (job.kind != JobKind::health && result.snapshot.app_count))
    {
        backend_ = result.snapshot;
        changed = Upsert(job.host, job.port, backend_);
    }
    else
    {
        backend_.online = 0;
        backend_.result = result.snapshot.result;
        Copy(backend_.error, sizeof(backend_.error),
             result.snapshot.error[0] ? result.snapshot.error : "Sunshine did not answer");
    }
    backend_valid_ = true;
    if (selected_app_ >= backend_.app_count)
        selected_app_ = 0;
    // While it reconnects, the list keeps showing the PC as it was.
    if (success || !health_.Reconnecting())
        changed = Remember(job.host, job.port, backend_, backend_.online != 0) || changed;
    return changed || !was_valid || was_reconnecting != health_.Reconnecting() ||
           std::memcmp(&before, &backend_, sizeof(backend_)) != 0;
}

void Model::Apply(const Job &job, JobResult &result)
{
    switch (job.kind)
    {
    case JobKind::discover:
        ApplyDiscovery(result);
        return;
    case JobKind::sweep:
    {
        const bool online = result.result == 0 && result.snapshot.online;
        bool changed = Remember(job.host, job.port, result.snapshot, online);
        if (online)
            changed = Upsert(job.host, job.port, result.snapshot) || changed;
        if (changed)
            ++revision_;
        return;
    }
    case JobKind::artwork:
        if (IsSelected(job.host, job.port) && !result.image.rgba.empty())
            artwork_ready_.push_back(std::move(result.image));
        return;
    default:
        break;
    }

    // The player may have moved to another PC while this one answered.
    if (!IsSelected(job.host, job.port))
    {
        if (Remember(job.host, job.port, result.snapshot,
                     result.result == 0 && result.snapshot.online != 0))
            ++revision_;
        return;
    }
    const bool was_paired = backend_.paired != 0;
    const int was_running = backend_.current_app_id;
    // A health check that found nothing new leaves the screens as they are.
    if (ApplySelected(job, result))
        ++revision_;
    if (job.kind == JobKind::unpair)
    {
        if (result.result != 0)
            Notify(NoticeKind::error, "Unpairing failed",
                   backend_.error[0] ? backend_.error : "Sunshine rejected the request.");
        else if (was_paired)
            Notify(NoticeKind::success, "Unpaired", "Pair again to use this PC.");
    }
    else if (job.kind == JobKind::stop)
    {
        if (result.result != 0)
            Notify(NoticeKind::error,
                   health_.Reconnecting() ? "Could not confirm the stop" : "The app did not stop",
                   backend_.error[0] ? backend_.error : "Sunshine rejected the request.");
        else if (was_running)
            Notify(NoticeKind::success, "App stopped");
    }
    else if (job.kind == JobKind::refresh && job.announce && !backend_.online)
    {
        Notify(NoticeKind::warning,
               std::string(backend_.name[0] ? backend_.name : job.host) + " is not answering",
               backend_.error);
    }
}

} // namespace launcher
