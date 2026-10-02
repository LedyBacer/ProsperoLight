/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "moonlight_app.hpp"
#include "moonlight_discovery.hpp"
#include "radio_ime.hpp"
#include "ui_sound.hpp"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/StringUtilities.h>

#include <SDL2/SDL.h>

#include <cstdio>
#include <cstring>

#ifndef PROSPEROLIGHT_STREAM_SELF_TEST_FPS
#define PROSPEROLIGHT_STREAM_SELF_TEST_FPS 0
#endif
#ifndef PROSPEROLIGHT_STREAM_SELF_TEST_RESOLUTION
#define PROSPEROLIGHT_STREAM_SELF_TEST_RESOLUTION MOONLIGHT_STREAM_RESOLUTION_1080P
#endif
#ifndef PROSPEROLIGHT_STOP_ACTIVE_APP_SELF_TEST
#define PROSPEROLIGHT_STOP_ACTIVE_APP_SELF_TEST 0
#endif

static_assert(PROSPEROLIGHT_STREAM_SELF_TEST_FPS == 0 ||
                  PROSPEROLIGHT_STREAM_SELF_TEST_FPS == MOONLIGHT_STREAM_FPS_90 ||
                  PROSPEROLIGHT_STREAM_SELF_TEST_FPS == MOONLIGHT_STREAM_FPS_120,
              "STREAM_SELF_TEST_FPS must be 0, 90, or 120");
static_assert(PROSPEROLIGHT_STREAM_SELF_TEST_RESOLUTION == MOONLIGHT_STREAM_RESOLUTION_1080P ||
                  PROSPEROLIGHT_STREAM_SELF_TEST_RESOLUTION == MOONLIGHT_STREAM_RESOLUTION_1440P ||
                  PROSPEROLIGHT_STREAM_SELF_TEST_RESOLUTION == MOONLIGHT_STREAM_RESOLUTION_2160P,
              "STREAM_SELF_TEST_RESOLUTION must be 0, 1, or 2");
static_assert(PROSPEROLIGHT_STOP_ACTIVE_APP_SELF_TEST == 0 ||
                  PROSPEROLIGHT_STOP_ACTIVE_APP_SELF_TEST == 1,
              "STOP_ACTIVE_APP_SELF_TEST must be 0 or 1");

namespace
{

#if PROSPEROLIGHT_STREAM_SELF_TEST_FPS != 0
bool high_refresh_self_test_consumed;
#endif
#if PROSPEROLIGHT_STOP_ACTIVE_APP_SELF_TEST != 0
bool stop_active_app_self_test_consumed;
#endif

Rml::Element *Find(Rml::ElementDocument *document, const char *id)
{
    return document ? document->GetElementById(id) : nullptr;
}

void SetText(Rml::ElementDocument *document, const char *id, const char *text)
{
    if (Rml::Element *element = Find(document, id))
        element->SetInnerRML(Rml::StringUtilities::EncodeRml(text ? text : ""));
}

void SetClass(Rml::ElementDocument *document, const char *id, const char *class_name, bool enabled)
{
    if (Rml::Element *element = Find(document, id))
        element->SetClass(class_name, enabled);
}

void SetImageSource(Rml::ElementDocument *document, const char *id, const char *source)
{
    if (Rml::Element *element = Find(document, id))
        element->SetAttribute("src", Rml::String(source ? source : ""));
}

struct FocusList
{
    const char *const *ids;
    unsigned count;
};

const char *const kHostFocus[] = {"nav-hosts", "nav-games",     "nav-settings",
                                  "host-card", "refresh-hosts", "pair-host",
                                  "add-host",  "remove-host",   "port-host"};
const char *const kGameFocus[] = {"nav-hosts",  "nav-games",  "nav-settings", "app-card-0",
                                  "app-card-1", "app-card-2", "app-card-3",   "app-card-4",
                                  "app-card-5", "stop-app",   "back-hosts"};
const char *const kSettingFocus[] = {
    "nav-hosts",          "nav-games",         "nav-settings",    "setting-codec",
    "setting-resolution", "setting-framerate", "setting-bitrate", "setting-display-area",
    "setting-hdr",        "setting-audio",     "setting-vsync",   "setting-decoder",
    "setting-cores"};

FocusList FocusFor(unsigned screen)
{
    switch (screen)
    {
    case 0:
        return {kHostFocus, sizeof(kHostFocus) / sizeof(kHostFocus[0])};
    case 1:
        return {kGameFocus, sizeof(kGameFocus) / sizeof(kGameFocus[0])};
    default:
        return {kSettingFocus, sizeof(kSettingFocus) / sizeof(kSettingFocus[0])};
    }
}

// The port is shown only when it differs from Sunshine's default.
void FormatEndpoint(char *output, size_t capacity, const char *address, unsigned port)
{
    if (port && port != MOONLIGHT_CONFIG_DEFAULT_HTTP_PORT)
        std::snprintf(output, capacity, "%s:%u", address, port);
    else
        std::snprintf(output, capacity, "%s", address);
}

const char *CodecName(unsigned codec, unsigned hdr = 0)
{
    if (hdr)
        return "HEVC Main10 HDR";
    return codec == MOONLIGHT_VIDEO_CODEC_HEVC ? "HEVC Main" : "H.264 High";
}

void StreamDimensions(unsigned resolution, unsigned &width, unsigned &height)
{
    if (resolution == MOONLIGHT_STREAM_RESOLUTION_1440P)
    {
        width = 2560;
        height = 1440;
    }
    else if (resolution == MOONLIGHT_STREAM_RESOLUTION_2160P)
    {
        width = 3840;
        height = 2160;
    }
    else
    {
        width = 1920;
        height = 1080;
    }
}

unsigned NextBitrate(unsigned current)
{
    static const unsigned values[] = {20, 40, 50, 60, 70, 80, 100, 150, 200, 300, 400, 500};
    for (unsigned value : values)
        if (value > current)
            return value;
    return values[0];
}

unsigned NextFrameRate(unsigned current)
{
    switch (current)
    {
    case MOONLIGHT_STREAM_FPS_60:
        return MOONLIGHT_STREAM_FPS_90;
    case MOONLIGHT_STREAM_FPS_90:
        return MOONLIGHT_STREAM_FPS_120;
    default:
        return MOONLIGHT_STREAM_FPS_60;
    }
}

} // namespace

bool MoonlightApp::Initialize(Rml::ElementDocument *document)
{
    document_ = document;
    if (!document_)
        return false;
    (void)moonlight_config_load(&config_);
    bitrate_mbps_ = config_.bitrate_mbps;
    const moonlight_config_host_t *host = SelectedHost();
    if (host)
    {
        std::snprintf(backend_.host, sizeof(backend_.host), "%s", host->address);
        backend_.http_port = moonlight_config_host_port(host);
        std::snprintf(backend_.name, sizeof(backend_.name), "%s", host->name);
        if (config_.host_count == 1)
        {
            screen_ = Screen::Games;
            focus_ = 3;
        }
    }
    else
        initial_discovery_pending_ = true;
    UpdateScreen();
    UpdateSettings();
    UpdateGames();
    UpdateFocus();
    return true;
}

void MoonlightApp::ShowStreamError(const char *message)
{
    if (!message || !message[0])
        return;
    prosperolight::ui_sound_play(prosperolight::UiSoundCue::Error);
    std::snprintf(stream_error_, sizeof(stream_error_), "%s", message);
    SetScreen(Screen::Games);
    SetText(document_, "launch-status", stream_error_);
}

void MoonlightApp::Poll()
{
    if (initial_discovery_pending_)
    {
        initial_discovery_pending_ = false;
        RefreshBackend();
        return;
    }
    if (manual_entry_active_ && !radio_ime_busy())
        manual_entry_active_ = false;
    if (stop_pending_ && SDL_GetTicks64() >= stop_due_ms_)
    {
        FinishStopActiveApp();
        return;
    }
#if PROSPEROLIGHT_STOP_ACTIVE_APP_SELF_TEST != 0
    if (!stop_active_app_self_test_consumed && backend_.online && backend_.paired &&
        backend_.current_app_id)
    {
        stop_active_app_self_test_consumed = true;
        screen_ = Screen::Games;
        focus_ = 9;
        UpdateScreen();
        UpdateFocus();
        RequestStopActiveApp();
        return;
    }
#endif
#if PROSPEROLIGHT_STREAM_SELF_TEST_FPS != 0
    if (!high_refresh_self_test_consumed && backend_.online && backend_.paired &&
        backend_.app_count)
    {
        high_refresh_self_test_consumed = true;
        screen_ = Screen::Games;
        focus_ = 3;
        selected_app_ = 0;
        bitrate_mbps_ = 80;
        config_.video_codec = MOONLIGHT_VIDEO_CODEC_HEVC;
        config_.stream_resolution = PROSPEROLIGHT_STREAM_SELF_TEST_RESOLUTION;
        config_.stream_fps = PROSPEROLIGHT_STREAM_SELF_TEST_FPS;
        config_.hdr_enabled = 0;
        UpdateScreen();
        UpdateSettings();
        UpdateFocus();
        command_ = Command::StartStream;
        return;
    }
#endif
    PollPairing();
    PollHealth();
    PollArtwork();
}

void MoonlightApp::Shutdown()
{
    radio_ime_cancel();
    FinishHealthWorker();
    FinishArtworkWorker(true);
    document_ = nullptr;
}

void MoonlightApp::FinishHealthWorker()
{
    if (health_worker_active_)
    {
        (void)pthread_join(health_thread_, nullptr);
        health_worker_active_ = false;
    }
    __atomic_store_n(&health_worker_state_, 0, __ATOMIC_RELEASE);
}

void *MoonlightApp::HealthWorker(void *argument)
{
    MoonlightApp *app = static_cast<MoonlightApp *>(argument);
    const int result =
        moonlight_backend_refresh(app->health_host_, app->health_port_, &app->health_snapshot_);
    __atomic_store_n(&app->health_worker_state_, result == 0 ? 2 : 3, __ATOMIC_RELEASE);
    return nullptr;
}

void MoonlightApp::PollHealth()
{
    const uint64_t now = SDL_GetTicks64();
    if (health_worker_active_)
    {
        const int state = __atomic_load_n(&health_worker_state_, __ATOMIC_ACQUIRE);
        if (state == 1)
            return;
        (void)pthread_join(health_thread_, nullptr);
        health_worker_active_ = false;
        const bool success = state == 2 && health_snapshot_.online;
        health_due_ms_ = now + health_.Record(success);
        if (success)
        {
            backend_ = health_snapshot_;
            const moonlight_config_host_t *host = SelectedHost();
            const bool manual = host && host->manual;
            const moonlight_config_t previous_config = config_;
            const int index =
                moonlight_config_upsert_host(&config_, backend_.host, backend_.http_port,
                                             backend_.name, backend_.unique_id, manual);
            if (index >= 0)
            {
                config_.selected_host = static_cast<uint32_t>(index);
                if (std::memcmp(&previous_config, &config_, sizeof(config_)) != 0)
                    (void)moonlight_config_save(&config_);
            }
            if (selected_app_ >= backend_.app_count)
                selected_app_ = 0;
        }
        else
        {
            backend_.online = 0;
            backend_.result = health_snapshot_.result;
            std::snprintf(backend_.error, sizeof(backend_.error), "%s",
                          health_snapshot_.error[0] ? health_snapshot_.error
                                                    : "Sunshine did not respond");
        }
        __atomic_store_n(&health_worker_state_, 0, __ATOMIC_RELEASE);
        UpdateHost();
        UpdateGames();
        UpdateSettings();
    }

    if (health_worker_active_ || pairing_active_ || artwork_worker_active_ ||
        now < health_due_ms_ || !SelectedHostAddress()[0])
        return;

    std::snprintf(health_host_, sizeof(health_host_), "%s", SelectedHostAddress());
    health_port_ = SelectedHostPort();
    std::memset(&health_snapshot_, 0, sizeof(health_snapshot_));
    __atomic_store_n(&health_worker_state_, 1, __ATOMIC_RELEASE);
    if (pthread_create(&health_thread_, nullptr, HealthWorker, this) != 0)
    {
        __atomic_store_n(&health_worker_state_, 0, __ATOMIC_RELEASE);
        health_due_ms_ = now + health_.Record(false);
        backend_.online = 0;
        backend_.result = -1;
        std::snprintf(backend_.error, sizeof(backend_.error),
                      "Could not start Sunshine health check");
        UpdateHost();
        UpdateGames();
        UpdateSettings();
        return;
    }
    health_worker_active_ = true;
}

void MoonlightApp::FinishArtworkWorker(bool clear_cache)
{
    if (artwork_worker_active_)
    {
        (void)pthread_join(artwork_thread_, nullptr);
        artwork_worker_active_ = false;
    }
    __atomic_store_n(&artwork_worker_state_, 0, __ATOMIC_RELEASE);
    if (clear_cache)
        moonlight_backend_clear_app_artwork();
}

void *MoonlightApp::ArtworkWorker(void *argument)
{
    MoonlightApp *app = static_cast<MoonlightApp *>(argument);
    const int result = moonlight_backend_fetch_app_artwork(
        app->artwork_host_, app->artwork_https_port_, app->artwork_app_id_);
    __atomic_store_n(&app->artwork_worker_state_, result == 0 ? 2 : 3, __ATOMIC_RELEASE);
    return nullptr;
}

void MoonlightApp::PollArtwork()
{
    const uint64_t now = SDL_GetTicks64();
    if (artwork_worker_active_)
    {
        const int state = __atomic_load_n(&artwork_worker_state_, __ATOMIC_ACQUIRE);
        if (state == 1)
            return;
        (void)pthread_join(artwork_thread_, nullptr);
        artwork_worker_active_ = false;
        if (state == 2)
            UpdateGames();
        artwork_due_ms_ = now + 50;
    }

    if (health_worker_active_ || screen_ != Screen::Games || !backend_.online || !backend_.paired ||
        !backend_.https_port || !backend_.app_count)
        return;

    const unsigned page_start = (selected_app_ / 6) * 6;
    if (page_start != artwork_page_start_)
    {
        moonlight_backend_clear_app_artwork();
        std::memset(artwork_attempted_, 0, sizeof(artwork_attempted_));
        artwork_page_start_ = page_start;
        artwork_next_slot_ = selected_app_ % 6;
        artwork_due_ms_ = now + 250;
        UpdateGames();
        return;
    }
    if (now < artwork_due_ms_)
        return;

    unsigned slot = selected_app_ % 6;
    if (artwork_attempted_[slot])
    {
        for (unsigned offset = 0; offset < 6; ++offset)
        {
            slot = (artwork_next_slot_ + offset) % 6;
            if (!artwork_attempted_[slot])
                break;
        }
        if (artwork_attempted_[slot])
            return;
    }
    const unsigned index = page_start + slot;
    if (index >= backend_.app_count)
    {
        artwork_attempted_[slot] = true;
        artwork_next_slot_ = (slot + 1) % 6;
        return;
    }

    artwork_attempted_[slot] = true;
    artwork_next_slot_ = (slot + 1) % 6;
    std::snprintf(artwork_host_, sizeof(artwork_host_), "%s", SelectedHostAddress());
    artwork_https_port_ = backend_.https_port;
    artwork_app_id_ = backend_.apps[index].id;
    __atomic_store_n(&artwork_worker_state_, 1, __ATOMIC_RELEASE);
    if (pthread_create(&artwork_thread_, nullptr, ArtworkWorker, this) != 0)
    {
        __atomic_store_n(&artwork_worker_state_, 3, __ATOMIC_RELEASE);
        artwork_due_ms_ = now + 250;
        return;
    }
    artwork_worker_active_ = true;
}

void MoonlightApp::SetScreen(Screen screen)
{
    const bool pending_confirmation = confirm_unpair_ || confirm_remove_;
    screen_ = screen;
    focus_ = screen_ == Screen::Games ? (backend_.app_count ? 3 + selected_app_ % 6 : 10) : 3;
    confirm_unpair_ = false;
    confirm_remove_ = false;
    if (pending_confirmation)
        UpdateHost();
    UpdateScreen();
    if (screen_ == Screen::Games)
        UpdateGames();
    UpdateFocus();
}

void MoonlightApp::MoveFocus(int direction)
{
    const FocusList list = FocusFor(static_cast<unsigned>(screen_));
    if (!list.count)
        return;
    int next = static_cast<int>(focus_) + direction;
    if (next < 0)
        next = static_cast<int>(list.count) - 1;
    if (next >= static_cast<int>(list.count))
        next = 0;
    focus_ = static_cast<unsigned>(next);
    if (screen_ == Screen::Hosts && confirm_unpair_ && focus_ != 5)
    {
        confirm_unpair_ = false;
        UpdateHost();
    }
    if (screen_ == Screen::Hosts && confirm_remove_ && focus_ != 7)
    {
        confirm_remove_ = false;
        UpdateHost();
    }
    UpdateFocus();
}

void MoonlightApp::CycleApp(int direction)
{
    if (backend_.app_count < 2)
        return;
    if (direction < 0)
    {
        selected_app_ = selected_app_ == 0 ? backend_.app_count - 1 : selected_app_ - 1;
    }
    else
    {
        selected_app_ = (selected_app_ + 1) % backend_.app_count;
    }
    focus_ = 3 + selected_app_ % 6;
    UpdateGames();
    UpdateFocus();
}

void MoonlightApp::MoveGameVertical(int direction)
{
    if (focus_ < 3)
    {
        if (direction > 0 && backend_.app_count)
        {
            focus_ = 3 + selected_app_ % 6;
            UpdateFocus();
        }
        else
        {
            MoveFocus(direction);
        }
        return;
    }

    const unsigned page_start = (selected_app_ / 6) * 6;
    if (focus_ <= 8 && backend_.app_count)
    {
        const unsigned slot = focus_ - 3;
        if (direction < 0)
        {
            if (slot >= 3)
            {
                selected_app_ -= 3;
                focus_ -= 3;
                UpdateGames();
            }
            else
            {
                focus_ = 1;
            }
        }
        else
        {
            const unsigned target = page_start + slot + 3;
            if (slot < 3 && target < backend_.app_count)
            {
                selected_app_ = target;
                focus_ += 3;
                UpdateGames();
            }
            else
            {
                focus_ = slot == 0 || slot == 3 ? 9 : 10;
            }
        }
        UpdateFocus();
        return;
    }

    if (focus_ == 9 || focus_ == 10)
    {
        if (direction < 0 && backend_.app_count)
        {
            const unsigned column = focus_ == 9 ? 0 : 2;
            unsigned target = page_start + 3 + column;
            if (target >= backend_.app_count)
                target = page_start + column;
            if (target >= backend_.app_count)
                target = backend_.app_count - 1;
            selected_app_ = target;
            focus_ = 3 + target - page_start;
            UpdateGames();
        }
        else
        {
            focus_ = 1;
        }
        UpdateFocus();
    }
}

const moonlight_config_host_t *MoonlightApp::SelectedHost() const
{
    return config_.host_count && config_.selected_host < config_.host_count
               ? &config_.hosts[config_.selected_host]
               : nullptr;
}

void MoonlightApp::CycleHost(int direction)
{
    if (config_.host_count < 2)
        return;
    if (direction < 0)
    {
        config_.selected_host =
            config_.selected_host == 0 ? config_.host_count - 1 : config_.selected_host - 1;
    }
    else
    {
        config_.selected_host = (config_.selected_host + 1) % config_.host_count;
    }
    selected_app_ = 0;
    confirm_remove_ = false;
    (void)moonlight_config_save(&config_);
    RefreshBackend();
}

void MoonlightApp::Activate()
{
    if (focus_ < 3)
    {
        prosperolight::ui_sound_play(prosperolight::UiSoundCue::Confirm);
        SetScreen(static_cast<Screen>(focus_));
        return;
    }

    switch (screen_)
    {
    case Screen::Hosts:
        if (focus_ == 3)
        {
            if (backend_.online && backend_.paired)
            {
                if (backend_.app_count)
                {
                    prosperolight::ui_sound_play(prosperolight::UiSoundCue::Confirm);
                    SetScreen(Screen::Games);
                }
                else
                {
                    prosperolight::ui_sound_play(prosperolight::UiSoundCue::Error);
                    SetText(document_, "host-action-status",
                            "Sunshine returned no launchable apps");
                }
            }
            else if (backend_.online)
            {
                prosperolight::ui_sound_play(prosperolight::UiSoundCue::Error);
                SetText(document_, "host-action-status",
                        "Choose Pair PC below to connect this client");
            }
            else
            {
                prosperolight::ui_sound_play(prosperolight::UiSoundCue::Error);
                SetText(document_, "host-action-status", "Sunshine is currently unreachable");
            }
        }
        else if (focus_ == 4)
        {
            prosperolight::ui_sound_play(prosperolight::UiSoundCue::Confirm);
            RefreshBackend();
        }
        else if (focus_ == 5)
            TogglePairing();
        else if (focus_ == 6)
            StartManualHostEntry();
        else if (focus_ == 7)
            RemoveHost();
        else
            StartPortEntry();
        break;
    case Screen::Games:
        if (focus_ >= 3 && focus_ <= 8 && selected_app_ < backend_.app_count)
        {
            char text[160];
            unsigned width, height;
            if (!backend_.online)
            {
                prosperolight::ui_sound_play(prosperolight::UiSoundCue::Error);
                SetText(document_, "launch-status",
                        health_.Reconnecting() ? "Sunshine is reconnecting. Please wait..."
                                               : "Sunshine is currently unreachable.");
                break;
            }
            if (config_.video_codec == MOONLIGHT_VIDEO_CODEC_HEVC && !backend_.hevc_supported)
            {
                prosperolight::ui_sound_play(prosperolight::UiSoundCue::Error);
                SetText(document_, "launch-status",
                        "The selected Sunshine PC does not advertise HEVC support.");
                break;
            }
            if (config_.hdr_enabled && !backend_.main10_supported)
            {
                prosperolight::ui_sound_play(prosperolight::UiSoundCue::Error);
                SetText(document_, "launch-status",
                        "The selected Sunshine PC does not advertise HEVC Main10 HDR support.");
                break;
            }
            StreamDimensions(config_.stream_resolution, width, height);
            stream_error_[0] = '\0';
            std::snprintf(text, sizeof(text), "%s %s %ux%u@%u stream...",
                          backend_.current_app_id == backend_.apps[selected_app_].id ? "Resuming"
                                                                                     : "Starting",
                          CodecName(config_.video_codec, config_.hdr_enabled), width, height,
                          config_.stream_fps);
            SetText(document_, "launch-status", text);
            prosperolight::ui_sound_play(prosperolight::UiSoundCue::StreamStart);
            command_ = Command::StartStream;
        }
        else if (focus_ == 9)
            RequestStopActiveApp();
        else
        {
            prosperolight::ui_sound_play(prosperolight::UiSoundCue::Back);
            SetScreen(Screen::Hosts);
        }
        break;
    case Screen::Settings:
        if (focus_ == 3)
        {
            config_.hdr_enabled = 0;
            config_.video_codec = config_.video_codec == MOONLIGHT_VIDEO_CODEC_H264
                                      ? MOONLIGHT_VIDEO_CODEC_HEVC
                                      : MOONLIGHT_VIDEO_CODEC_H264;
        }
        else if (focus_ == 4)
        {
            config_.stream_resolution = (config_.stream_resolution + 1) % 3;
        }
        else if (focus_ == 5)
        {
            config_.stream_fps = NextFrameRate(config_.stream_fps);
        }
        else if (focus_ == 6)
        {
            bitrate_mbps_ = NextBitrate(bitrate_mbps_);
            config_.bitrate_mbps = bitrate_mbps_;
        }
        else if (focus_ == 7)
        {
            config_.display_area = config_.display_area == MOONLIGHT_DISPLAY_AREA_TV_SAFE
                                       ? MOONLIGHT_DISPLAY_AREA_FULL
                                       : MOONLIGHT_DISPLAY_AREA_TV_SAFE;
        }
        else if (focus_ == 8)
        {
            config_.hdr_enabled = !config_.hdr_enabled;
            if (config_.hdr_enabled)
                config_.video_codec = MOONLIGHT_VIDEO_CODEC_HEVC;
        }
        else if (focus_ == 9)
        {
            config_.audio_configuration = config_.audio_configuration == MOONLIGHT_AUDIO_STEREO
                                              ? MOONLIGHT_AUDIO_51_SURROUND
                                              : MOONLIGHT_AUDIO_STEREO;
        }
        else if (focus_ == 10)
        {
            config_.vsync_enabled = !config_.vsync_enabled;
        }
        else if (focus_ == 11)
        {
            config_.decoder_pipeline =
                config_.decoder_pipeline == MOONLIGHT_DECODER_PIPELINE_ADAPTIVE
                    ? MOONLIGHT_DECODER_PIPELINE_CLASSIC
                    : MOONLIGHT_DECODER_PIPELINE_ADAPTIVE;
        }
        else if (focus_ == 12)
        {
            config_.decoder_cores = config_.decoder_cores <= MOONLIGHT_DECODER_CORES_MIN
                                        ? MOONLIGHT_DECODER_CORES_MAX
                                        : config_.decoder_cores - 1u;
        }
        (void)moonlight_config_save(&config_);
        prosperolight::ui_sound_play(prosperolight::UiSoundCue::Setting);
        UpdateSettings();
        UpdateGames();
        break;
    default:
        break;
    }
}

void MoonlightApp::StartManualHostEntry()
{
    const moonlight_config_host_t *host = SelectedHost();
    char initial[MOONLIGHT_CONFIG_ADDRESS_SIZE + 8] = "";
    if (host && host->manual)
        FormatEndpoint(initial, sizeof(initial), host->address, moonlight_config_host_port(host));
    if (radio_ime_request(initial, "Add Sunshine PC",
                          "IPv4 address, for example 192.168.1.50 or 192.168.1.50:48989",
                          ManualHostResult, this))
    {
        prosperolight::ui_sound_play(prosperolight::UiSoundCue::Confirm);
        manual_entry_active_ = true;
        SetText(document_, "host-action-status",
                "Enter the Sunshine PC IPv4 address. Add :port if Sunshine does not use 47989.");
    }
    else
    {
        prosperolight::ui_sound_play(prosperolight::UiSoundCue::Error);
        SetText(document_, "host-action-status", "Text entry is currently unavailable");
    }
}

void MoonlightApp::ManualHostResult(const char *text, void *user_data)
{
    MoonlightApp *app = static_cast<MoonlightApp *>(user_data);
    if (app)
        app->AddManualHost(text);
}

void MoonlightApp::AddManualHost(const char *text)
{
    char address[MOONLIGHT_CONFIG_ADDRESS_SIZE];
    uint16_t port = 0;
    const bool valid = moonlight_config_parse_endpoint(text, address, &port);
    manual_entry_active_ = false;
    if (!valid)
    {
        prosperolight::ui_sound_play(prosperolight::UiSoundCue::Error);
        SetText(document_, "host-action-status",
                "Enter an IPv4 address such as 192.168.1.50, or 192.168.1.50:48989 with a port");
        return;
    }
    // Without a port, a PC already saved at this address keeps the port it has.
    const int index =
        moonlight_config_upsert_host(&config_, address, port, "Sunshine PC", "", true);
    if (index < 0)
    {
        prosperolight::ui_sound_play(prosperolight::UiSoundCue::Error);
        SetText(document_, "host-action-status", "The PC list is full. Remove a PC first.");
        return;
    }
    config_.selected_host = static_cast<uint32_t>(index);
    (void)moonlight_config_save(&config_);
    selected_app_ = 0;
    RefreshBackend();
    prosperolight::ui_sound_play(prosperolight::UiSoundCue::Success);
}

void MoonlightApp::StartPortEntry()
{
    const moonlight_config_host_t *host = SelectedHost();
    if (!host)
    {
        prosperolight::ui_sound_play(prosperolight::UiSoundCue::Error);
        SetText(document_, "host-action-status", "Add a Sunshine PC before setting its port");
        return;
    }
    char current[8];
    std::snprintf(current, sizeof(current), "%u", moonlight_config_host_port(host));
    if (radio_ime_request_number(current, 5, "Sunshine port", "Sunshine port, 47989 by default",
                                 PortResult, this))
    {
        prosperolight::ui_sound_play(prosperolight::UiSoundCue::Confirm);
        manual_entry_active_ = true;
        SetText(document_, "host-action-status",
                "Enter the Port from Sunshine's Network settings. Leave it empty for 47989.");
    }
    else
    {
        prosperolight::ui_sound_play(prosperolight::UiSoundCue::Error);
        SetText(document_, "host-action-status", "Text entry is currently unavailable");
    }
}

void MoonlightApp::PortResult(const char *text, void *user_data)
{
    MoonlightApp *app = static_cast<MoonlightApp *>(user_data);
    if (app)
        app->SetHostPort(text);
}

void MoonlightApp::SetHostPort(const char *text)
{
    uint16_t port = MOONLIGHT_CONFIG_DEFAULT_HTTP_PORT;
    const bool blank = !text || text[std::strspn(text, " \t")] == '\0';
    manual_entry_active_ = false;
    if (!SelectedHost() || (!blank && !moonlight_config_parse_port(text, &port)))
    {
        prosperolight::ui_sound_play(prosperolight::UiSoundCue::Error);
        SetText(document_, "host-action-status",
                "Enter a port from 1 to 65535. Sunshine uses 47989 unless it was changed.");
        return;
    }
    // Workers hold the previous endpoint: finish them before it changes.
    FinishHealthWorker();
    FinishArtworkWorker(true);
    const int index = moonlight_config_set_host_port(&config_, config_.selected_host, port);
    if (index < 0)
    {
        prosperolight::ui_sound_play(prosperolight::UiSoundCue::Error);
        SetText(document_, "host-action-status", "Could not change the port of this PC.");
        return;
    }
    config_.selected_host = static_cast<uint32_t>(index);
    (void)moonlight_config_save(&config_);
    selected_app_ = 0;
    prosperolight::ui_sound_play(prosperolight::UiSoundCue::Success);
    RefreshBackend(false);
}

void MoonlightApp::DiscoverHosts()
{
    moonlight_discovered_host_t discovered[MOONLIGHT_DISCOVERY_MAX_HOSTS];
    moonlight_config_t before = config_;
    const uint32_t count = moonlight_discover_hosts(discovered, MOONLIGHT_DISCOVERY_MAX_HOSTS);
    for (uint32_t index = 0; index < count; ++index)
    {
        (void)moonlight_config_upsert_host(&config_, discovered[index].address,
                                           discovered[index].http_port, discovered[index].name, "",
                                           false);
    }
    if (std::memcmp(&before, &config_, sizeof(config_)) != 0)
        (void)moonlight_config_save(&config_);
}

void MoonlightApp::TogglePairing()
{
    FinishHealthWorker();
    FinishArtworkWorker(true);
    artwork_page_start_ = MOONLIGHT_BACKEND_MAX_APPS;
    const char *host = SelectedHostAddress();
    const uint16_t port = SelectedHostPort();
    if (!host[0])
    {
        prosperolight::ui_sound_play(prosperolight::UiSoundCue::Error);
        SetText(document_, "host-action-status", "Refresh discovery or add a Sunshine PC address");
        return;
    }
    if (!backend_.online)
    {
        prosperolight::ui_sound_play(prosperolight::UiSoundCue::Error);
        SetText(document_, "host-action-status", "Sunshine must be online before pairing");
        return;
    }
    if (!backend_.paired && backend_.current_app_id)
    {
        prosperolight::ui_sound_play(prosperolight::UiSoundCue::Error);
        char text[192];
        std::snprintf(text, sizeof(text),
                      "App %d is active. Stop it on the original client, then Refresh.",
                      backend_.current_app_id);
        SetText(document_, "host-action-status", text);
        return;
    }
    if (!backend_.paired)
    {
        const int result = moonlight_backend_pair_start(host, port);
        if (result != 0)
        {
            prosperolight::ui_sound_play(prosperolight::UiSoundCue::Error);
            moonlight_backend_snapshot_t failed{};
            moonlight_backend_pair_poll(&failed, nullptr);
            SetText(document_, "host-action-status",
                    failed.error[0] ? failed.error : "Could not start the pairing request");
            return;
        }
        pairing_active_ = true;
        pairing_state_ = MOONLIGHT_BACKEND_PAIR_PREPARING;
        pairing_started_ms_ = SDL_GetTicks64();
        pairing_remaining_ = MOONLIGHT_BACKEND_PAIR_TIMEOUT_SECONDS + 1;
        prosperolight::ui_sound_play(prosperolight::UiSoundCue::Confirm);
        SetText(document_, "pair-modal-pin", "----");
        SetText(document_, "pair-modal-status", "Preparing a secure PIN...");
        SetClass(document_, "pair-modal", "hidden", false);
        PollPairing();
        return;
    }
    if (backend_.current_app_id)
    {
        prosperolight::ui_sound_play(prosperolight::UiSoundCue::Error);
        SetText(document_, "host-action-status", "Stop the active Sunshine app before unpairing");
        return;
    }
    if (!confirm_unpair_)
    {
        prosperolight::ui_sound_play(prosperolight::UiSoundCue::Confirm);
        confirm_unpair_ = true;
        SetText(document_, "pair-host-label", "Confirm unpair");
        SetText(document_, "host-action-status",
                "Press Cross again to forget this PC and require a new PIN");
        return;
    }

    confirm_unpair_ = false;
    SetText(document_, "host-action-status", "Unpairing this PS5...");
    const int result = moonlight_backend_unpair(host, port, &backend_);
    health_due_ms_ = SDL_GetTicks64() + health_.Record(backend_.online != 0);
    UpdateHost();
    UpdateGames();
    UpdateSettings();
    if (result != 0)
    {
        prosperolight::ui_sound_play(prosperolight::UiSoundCue::Error);
        char text[180];
        std::snprintf(text, sizeof(text), "Unpair failed: %s",
                      backend_.error[0] ? backend_.error : "Sunshine rejected the request");
        SetText(document_, "host-action-status", text);
    }
    else
    {
        prosperolight::ui_sound_play(prosperolight::UiSoundCue::Success);
        SetText(document_, "host-action-status", "Unpaired locally. Pair again to reconnect.");
    }
}

void MoonlightApp::RemoveHost()
{
    if (!SelectedHost())
        return;
    if (!confirm_remove_)
    {
        confirm_remove_ = true;
        prosperolight::ui_sound_play(prosperolight::UiSoundCue::Confirm);
        SetText(document_, "remove-host-label", "Confirm remove");
        SetText(document_, "host-action-status",
                "Press Cross again to forget this PC locally. Pairing on the host is unchanged.");
        return;
    }

    moonlight_config_t updated = config_;
    if (!moonlight_config_remove_host(&updated, config_.selected_host) ||
        !moonlight_config_save(&updated))
    {
        prosperolight::ui_sound_play(prosperolight::UiSoundCue::Error);
        SetText(document_, "host-action-status", "Could not save the updated PC list.");
        return;
    }
    FinishHealthWorker();
    FinishArtworkWorker(true);
    config_ = updated;
    confirm_remove_ = false;
    confirm_unpair_ = false;
    selected_app_ = 0;
    artwork_page_start_ = MOONLIGHT_BACKEND_MAX_APPS;
    health_ = {};
    health_due_ms_ = 0;
    std::memset(&backend_, 0, sizeof(backend_));
    RefreshBackend(false);
    UpdateFocus();
    prosperolight::ui_sound_play(prosperolight::UiSoundCue::Success);
    SetText(document_, "host-action-status",
            "PC removed locally. Refresh to discover available PCs again.");
}

void MoonlightApp::PollPairing()
{
    if (!pairing_active_)
        return;

    char pin[5] = {};
    moonlight_backend_snapshot_t completed{};
    const moonlight_backend_pair_state_t state = moonlight_backend_pair_poll(&completed, pin);
    if (pin[0] && state != pairing_state_)
    {
        SetText(document_, "pair-modal-pin", pin);
        SetText(document_, "pair-modal-status", "Enter this PIN in Sunshine to approve the PS5");
    }
    pairing_state_ = state;

    const uint64_t elapsed = (SDL_GetTicks64() - pairing_started_ms_) / 1000;
    const unsigned remaining =
        elapsed >= MOONLIGHT_BACKEND_PAIR_TIMEOUT_SECONDS
            ? 0
            : MOONLIGHT_BACKEND_PAIR_TIMEOUT_SECONDS - static_cast<unsigned>(elapsed);
    if (remaining != pairing_remaining_)
    {
        char timer[16];
        std::snprintf(timer, sizeof(timer), "%u:%02u", remaining / 60, remaining % 60);
        SetText(document_, "pair-modal-timer", timer);
        pairing_remaining_ = remaining;
    }

    if (state != MOONLIGHT_BACKEND_PAIR_SUCCEEDED && state != MOONLIGHT_BACKEND_PAIR_FAILED)
        return;

    pairing_active_ = false;
    backend_ = completed;
    prosperolight::ui_sound_play(state == MOONLIGHT_BACKEND_PAIR_SUCCEEDED
                                     ? prosperolight::UiSoundCue::Success
                                     : prosperolight::UiSoundCue::Error);
    health_due_ms_ = SDL_GetTicks64() + health_.Record(backend_.online != 0);
    SetClass(document_, "pair-modal", "hidden", true);
    UpdateHost();
    UpdateGames();
    UpdateSettings();
}

void MoonlightApp::RequestStopActiveApp()
{
    if (stop_pending_)
        return;
    if (!backend_.current_app_id)
    {
        prosperolight::ui_sound_play(prosperolight::UiSoundCue::Error);
        SetText(document_, "launch-status", "No Sunshine app is currently running.");
        return;
    }

    stop_pending_ = true;
    stop_due_ms_ = SDL_GetTicks64() + 50;
    SetClass(document_, "stop-app", "disabled", true);
    SetText(document_, "stop-app-label", "Stopping...");
    SetText(document_, "launch-status", "Stopping the active Sunshine app...");
}

void MoonlightApp::FinishStopActiveApp()
{
    stop_pending_ = false;
    FinishHealthWorker();
    FinishArtworkWorker(false);
    moonlight_backend_snapshot_t refreshed{};
    const int result =
        moonlight_backend_stop_app(SelectedHostAddress(), SelectedHostPort(), &refreshed);
    if (result == 0 || refreshed.app_count)
    {
        backend_ = refreshed;
    }
    else
    {
        backend_.online = 0;
        backend_.result = refreshed.result;
        std::snprintf(backend_.error, sizeof(backend_.error), "%s",
                      refreshed.error[0] ? refreshed.error : "Sunshine did not respond");
    }
    health_due_ms_ = SDL_GetTicks64() + health_.Record(backend_.online != 0);
    UpdateHost();
    UpdateGames();
    if (result != 0)
    {
        prosperolight::ui_sound_play(prosperolight::UiSoundCue::Error);
        char text[180];
        std::snprintf(text, sizeof(text), "%s: %s",
                      health_.Reconnecting() ? "Could not confirm stop; retrying automatically"
                                             : "Stop failed",
                      backend_.error[0] ? backend_.error : "Sunshine rejected the request");
        SetText(document_, "launch-status", text);
    }
    else
    {
        prosperolight::ui_sound_play(prosperolight::UiSoundCue::Success);
    }
}

void MoonlightApp::RefreshBackend(bool discover)
{
    FinishHealthWorker();
    FinishArtworkWorker(true);
    artwork_page_start_ = MOONLIGHT_BACKEND_MAX_APPS;
    confirm_unpair_ = false;
    confirm_remove_ = false;
    SetText(document_, "host-action-status", "Discovering Sunshine PCs...");
    if (discover)
        DiscoverHosts();
    const moonlight_config_host_t *host = SelectedHost();
    if (!host)
    {
        std::memset(&backend_, 0, sizeof(backend_));
        UpdateHost();
        UpdateGames();
        UpdateSettings();
        return;
    }
    SetText(document_, "host-action-status", "Refreshing Sunshine status...");
    const uint16_t port = moonlight_config_host_port(host);
    if (std::strcmp(backend_.host, host->address) != 0 || backend_.http_port != port)
    {
        std::memset(&backend_, 0, sizeof(backend_));
        std::snprintf(backend_.host, sizeof(backend_.host), "%s", host->address);
        backend_.http_port = port;
        health_ = {};
    }
    moonlight_backend_snapshot_t refreshed{};
    const int result = moonlight_backend_refresh(host->address, port, &refreshed);
    const bool success = result == 0 && refreshed.online;
    health_due_ms_ = SDL_GetTicks64() + health_.Record(success);
    if (success)
    {
        backend_ = refreshed;
        const int index = moonlight_config_upsert_host(&config_, host->address, port, backend_.name,
                                                       backend_.unique_id, host->manual != 0);
        if (index >= 0)
        {
            config_.selected_host = static_cast<uint32_t>(index);
            (void)moonlight_config_save(&config_);
        }
    }
    else
    {
        backend_.online = 0;
        backend_.result = refreshed.result;
        std::snprintf(backend_.error, sizeof(backend_.error), "%s",
                      refreshed.error[0] ? refreshed.error : "Sunshine did not respond");
    }
    if (selected_app_ >= backend_.app_count)
        selected_app_ = 0;
    UpdateHost();
    UpdateGames();
    UpdateSettings();
}

void MoonlightApp::UpdateScreen()
{
    const unsigned screen = static_cast<unsigned>(screen_);
    const char *const ids[] = {"screen-hosts", "screen-games", "screen-settings"};
    const char *const nav_ids[] = {"nav-hosts", "nav-games", "nav-settings"};
    const char *const titles[] = {"Your PCs", "Games", "Settings"};
    for (unsigned i = 0; i < static_cast<unsigned>(Screen::Count); ++i)
    {
        SetClass(document_, ids[i], "hidden", i != screen);
        SetClass(document_, nav_ids[i], "active", i == screen);
    }
    SetText(document_, "page-title", titles[screen]);
}

void MoonlightApp::UpdateFocus()
{
    const char *const all[] = {"nav-hosts",          "nav-games",
                               "nav-settings",       "host-card",
                               "refresh-hosts",      "pair-host",
                               "add-host",           "remove-host",
                               "port-host",          "app-card-0",
                               "app-card-1",         "app-card-2",
                               "app-card-3",         "app-card-4",
                               "app-card-5",         "stop-app",
                               "back-hosts",         "setting-codec",
                               "setting-resolution", "setting-framerate",
                               "setting-bitrate",    "setting-display-area",
                               "setting-hdr",        "setting-audio",
                               "setting-vsync",      "setting-decoder",
                               "setting-cores"};
    for (const char *id : all)
        SetClass(document_, id, "focused", false);

    const FocusList list = FocusFor(static_cast<unsigned>(screen_));
    if (focus_ >= list.count)
        focus_ = 0;
    SetClass(document_, list.ids[focus_], "focused", true);
}

void MoonlightApp::UpdateHost()
{
    char text[160];
    const moonlight_config_host_t *selected_host = SelectedHost();
    const char *host_name = backend_.name[0]                          ? backend_.name
                            : selected_host && selected_host->name[0] ? selected_host->name
                                                                      : "No PC found";
    SetText(document_, "host-name", host_name);
    SetText(document_, "host-detail-title", host_name);
    SetText(document_, "sidebar-host-name", host_name);
    char endpoint[MOONLIGHT_CONFIG_ADDRESS_SIZE + 8] = "No PC selected";
    const unsigned port = moonlight_config_host_port(selected_host);
    if (selected_host)
        FormatEndpoint(endpoint, sizeof(endpoint), selected_host->address, port);
    SetText(document_, "sidebar-host-address", endpoint);
    if (selected_host)
    {
        std::snprintf(text, sizeof(text), "PC %u OF %u / LEFT OR RIGHT TO SWITCH",
                      config_.selected_host + 1, config_.host_count);
        SetText(document_, "host-position", text);
    }
    else
    {
        SetText(document_, "host-position", "REFRESH DISCOVERY OR ADD A PC");
    }
    SetText(document_, "host-address", selected_host ? endpoint : backend_.host);
    SetText(document_, "host-detail-address", selected_host ? endpoint : backend_.host);
    std::snprintf(text, sizeof(text), "Port %u", port);
    SetText(document_, "port-host-label", text);
    SetClass(document_, "port-host", "disabled", !selected_host);
    SetClass(document_, "host-status-dot", "online", backend_.online != 0);
    SetClass(document_, "footer-status-dot", "online", backend_.online != 0);
    SetClass(document_, "host-status", "offline", !backend_.online || !backend_.paired);
    SetClass(document_, "host-detail-ready", "offline", !backend_.online || !backend_.paired);
    SetClass(document_, "footer-status", "offline", !backend_.online || !backend_.paired);
    SetClass(document_, "pair-host", "action-danger", backend_.paired != 0);
    SetClass(document_, "pair-host", "disabled",
             health_.Reconnecting() || backend_.online == 0 ||
                 (!backend_.paired && backend_.current_app_id != 0));
    SetClass(document_, "remove-host", "disabled", !selected_host);
    SetText(document_, "remove-host-label", confirm_remove_ ? "Confirm remove" : "Remove PC");

    if (!selected_host)
    {
        SetText(document_, "host-address", "No saved or discovered address");
        SetText(document_, "host-detail-address", "Not configured");
        SetText(document_, "host-status", "NO PCS FOUND");
        SetText(document_, "host-detail-connection", "Discovery complete");
        SetText(document_, "host-detail-pairing", "Not configured");
        SetText(document_, "host-detail-ready", "ADD A SUNSHINE PC");
        SetText(document_, "footer-status", "NO SUNSHINE PC");
        SetText(document_, "host-card-action", "ADD");
        SetText(document_, "pair-host-label", "Pair unavailable");
        SetText(document_, "host-action-status",
                "No PC found. Select Add PC to enter an IPv4 address.");
        return;
    }

    if (health_.Reconnecting())
    {
        SetText(document_, "host-status", "RECONNECTING");
        SetText(document_, "host-detail-connection", "Waiting for Sunshine");
        SetText(document_, "host-detail-pairing", backend_.paired ? "Paired" : "Checking");
        SetText(document_, "host-detail-ready", "RETRYING AUTOMATICALLY");
        SetText(document_, "footer-status", "SUNSHINE RECONNECTING");
        SetText(document_, "host-card-action", "WAIT");
        SetText(document_, "pair-host-label", "Pair unavailable");
        SetText(document_, "host-action-status",
                "Sunshine did not respond. Retrying automatically...");
        return;
    }

    if (!backend_.online)
    {
        SetText(document_, "host-status", "OFFLINE");
        SetText(document_, "host-detail-connection", "Unavailable");
        SetText(document_, "host-detail-pairing", "Unknown");
        SetText(document_, "host-detail-ready", "RETRY REFRESH");
        SetText(document_, "footer-status", "SUNSHINE OFFLINE");
        SetText(document_, "host-card-action", "RETRY");
        SetText(document_, "pair-host-label", "Pair unavailable");
        std::snprintf(text, sizeof(text), "Refresh failed: %s",
                      backend_.error[0] ? backend_.error : "host unreachable");
        SetText(document_, "host-action-status", text);
        return;
    }

    std::snprintf(text, sizeof(text), "Sunshine %s",
                  backend_.server_version[0] ? backend_.server_version : "online");
    SetText(document_, "host-detail-connection", text);
    if (!backend_.paired)
    {
        SetText(document_, "host-status", "ONLINE / NOT PAIRED");
        SetText(document_, "host-detail-pairing", "Pairing required");
        if (backend_.current_app_id)
        {
            SetText(document_, "host-detail-ready", "ACTIVE SESSION");
            SetText(document_, "footer-status", "SUNSHINE BUSY");
            SetText(document_, "host-card-action", "BUSY");
            SetText(document_, "pair-host-label", "Pair unavailable");
            std::snprintf(text, sizeof(text),
                          "App %d is active. Stop it on the original client, then Refresh.",
                          backend_.current_app_id);
            SetText(document_, "host-action-status", text);
        }
        else
        {
            SetText(document_, "host-detail-ready", "PAIRING REQUIRED");
            SetText(document_, "footer-status", "PAIRING REQUIRED");
            SetText(document_, "host-card-action", "PAIR");
            SetText(document_, "pair-host-label", "Pair PC");
            SetText(document_, "host-action-status",
                    "Select Pair PC, then enter the onscreen PIN in Sunshine");
        }
        if (backend_.result != 0 && backend_.error[0])
        {
            std::snprintf(text, sizeof(text), "Pairing failed: %s", backend_.error);
            SetText(document_, "host-action-status", text);
        }
        return;
    }

    SetText(document_, "host-status", "ONLINE / PAIRED");
    SetText(document_, "host-card-action", "OPEN");
    SetText(document_, "pair-host-label", confirm_unpair_ ? "Confirm unpair" : "Unpair PC");
    SetText(document_, "host-detail-pairing", "Client certificate");
    std::snprintf(text, sizeof(text), "%u APPS READY", backend_.app_count);
    SetText(document_, "host-detail-ready", text);
    SetText(document_, "footer-status", "SUNSHINE READY");
    std::snprintf(text, sizeof(text), "%u apps advertised by Sunshine", backend_.app_count);
    SetText(document_, "host-action-status", text);
}

void MoonlightApp::UpdateGames()
{
    char id[48];
    char text[192];
    const bool available = selected_app_ < backend_.app_count;
    const bool refreshing =
        backend_.app_count == 0 && backend_.host[0] && !backend_.online && backend_.result == 0;
    SetClass(document_, "stop-app", "disabled",
             stop_pending_ || !backend_.online || backend_.current_app_id == 0);
    SetText(document_, "stop-app-label",
            stop_pending_             ? "Stopping..."
            : refreshing              ? "Refreshing PC"
            : !backend_.online        ? (health_.Reconnecting() ? "PC reconnecting" : "PC offline")
            : backend_.current_app_id ? "Stop active app"
                                      : "Nothing running");
    unsigned width, height;
    StreamDimensions(config_.stream_resolution, width, height);
    std::snprintf(text, sizeof(text), "%u x %u / %u FPS", width, height, config_.stream_fps);
    SetText(document_, "selected-app-stream", text);
    std::snprintf(text, sizeof(text), "%s / %u Mbps",
                  CodecName(config_.video_codec, config_.hdr_enabled), bitrate_mbps_);
    SetText(document_, "profile-video-value", text);
    const unsigned page_start = available ? (selected_app_ / 6) * 6 : 0;
    const unsigned page_count = backend_.app_count ? (backend_.app_count + 5) / 6 : 0;
    for (unsigned slot = 0; slot < 6; ++slot)
    {
        const unsigned index = page_start + slot;
        const bool visible = index < backend_.app_count;
        std::snprintf(id, sizeof(id), "app-card-%u", slot);
        SetClass(document_, id, "hidden", !visible);
        SetClass(document_, id, "selected", visible && index == selected_app_);
        if (!visible)
            continue;

        const moonlight_backend_app_t &card = backend_.apps[index];
        std::snprintf(id, sizeof(id), "app-index-%u", slot);
        std::snprintf(text, sizeof(text), "APP %02u", index + 1);
        SetText(document_, id, text);
        std::snprintf(id, sizeof(id), "app-name-%u", slot);
        SetText(document_, id, card.name);
        std::snprintf(id, sizeof(id), "app-art-%u", slot);
        if (moonlight_backend_find_app_artwork(card.id, nullptr))
        {
            std::snprintf(text, sizeof(text), "artwork://%d", card.id);
            SetImageSource(document_, id, text);
        }
        else
        {
            SetImageSource(document_, id, "icons/app-placeholder.tga");
        }
        std::snprintf(id, sizeof(id), "app-meta-%u", slot);
        std::snprintf(text, sizeof(text), "ID %d", card.id);
        SetText(document_, id, text);
        std::snprintf(id, sizeof(id), "app-state-%u", slot);
        SetClass(document_, id, "running", backend_.current_app_id == card.id);
        SetText(document_, id, backend_.current_app_id == card.id ? "RUNNING" : "READY");
    }

    if (refreshing)
    {
        SetText(document_, "games-page", "REFRESHING APPLICATIONS...");
    }
    else if (page_count)
    {
        std::snprintf(text, sizeof(text), "PAGE %u OF %u / %u APPS", selected_app_ / 6 + 1,
                      page_count, backend_.app_count);
        SetText(document_, "games-page", text);
    }
    else
    {
        SetText(document_, "games-page", "NO APPS AVAILABLE");
    }

    if (!available)
    {
        if (refreshing)
        {
            SetText(document_, "selected-app-name", "Refreshing applications");
            SetText(document_, "selected-app-position", "Waiting for Sunshine");
            SetText(document_, "selected-app-state", "REFRESHING");
            SetClass(document_, "selected-app-state", "offline", true);
            SetText(document_, "launch-status", "Refreshing the Sunshine application list...");
            return;
        }
        SetText(document_, "selected-app-name", "No apps returned");
        SetText(document_, "selected-app-position", "Refresh the selected Sunshine PC");
        SetText(document_, "selected-app-state", "NOT READY");
        SetClass(document_, "selected-app-state", "offline", true);
        SetText(document_, "launch-status", "No launchable Sunshine apps were returned.");
        return;
    }

    const moonlight_backend_app_t &app = backend_.apps[selected_app_];
    SetText(document_, "selected-app-name", app.name);
    std::snprintf(text, sizeof(text), "APP %u OF %u / ID %d", selected_app_ + 1, backend_.app_count,
                  app.id);
    SetText(document_, "selected-app-position", text);
    SetClass(document_, "selected-app-state", "offline", !backend_.online);
    SetText(document_, "selected-app-state",
            !backend_.online ? (health_.Reconnecting() ? "RECONNECTING" : "PC OFFLINE")
            : backend_.current_app_id == app.id ? "CURRENTLY RUNNING"
                                                : "READY TO STREAM");
    if (!backend_.online)
        std::snprintf(text, sizeof(text), "%s",
                      health_.Reconnecting() ? "Waiting for Sunshine to reconnect automatically."
                                             : "Sunshine is offline. Automatic checks continue.");
    else if (backend_.current_app_id == app.id)
        std::snprintf(text, sizeof(text), "Cross resumes %s. Square closes the active app.",
                      app.name);
    else if (backend_.current_app_id)
        std::snprintf(text, sizeof(text), "Cross switches to %s. Square closes the active app.",
                      app.name);
    else
        std::snprintf(text, sizeof(text), "Cross launches %s. Left / right selects another app.",
                      app.name);
    SetText(document_, "launch-status", text);
    if (stream_error_[0])
        SetText(document_, "launch-status", stream_error_);
}

void MoonlightApp::UpdateSettings()
{
    char text[128];
    unsigned width, height;
    StreamDimensions(config_.stream_resolution, width, height);
    SetText(document_, "setting-codec-value", CodecName(config_.video_codec, config_.hdr_enabled));
    if (config_.hdr_enabled && backend_.online && !backend_.main10_supported)
    {
        SetText(document_, "setting-codec-help",
                "Selected PC does not advertise HEVC Main10 support");
    }
    else if (config_.video_codec == MOONLIGHT_VIDEO_CODEC_HEVC && backend_.online &&
             !backend_.hevc_supported)
    {
        SetText(document_, "setting-codec-help", "Selected PC does not advertise HEVC support");
    }
    else
    {
        SetText(document_, "setting-codec-help",
                config_.hdr_enabled ? "10-bit HEVC Main10 hardware decode"
                : config_.video_codec == MOONLIGHT_VIDEO_CODEC_HEVC
                    ? "8-bit HEVC Main hardware decode"
                    : "H.264 High hardware decode");
    }
    std::snprintf(text, sizeof(text), "%u x %u", width, height);
    SetText(document_, "setting-resolution-value", text);
    std::snprintf(text, sizeof(text), "%u FPS", config_.stream_fps);
    SetText(document_, "setting-framerate-value", text);
    SetText(document_, "setting-display-area-value",
            config_.display_area == MOONLIGHT_DISPLAY_AREA_FULL ? "Edge to edge" : "TV safe");
    std::snprintf(text, sizeof(text), "%u Mbps", bitrate_mbps_);
    SetText(document_, "setting-bitrate-value", text);
    SetText(document_, "setting-hdr-value", config_.hdr_enabled ? "On / HDR10" : "Off / SDR");
    SetText(document_, "setting-hdr-help",
            config_.hdr_enabled && backend_.online && !backend_.main10_supported
                ? "Unavailable: selected PC does not advertise HEVC Main10"
            : config_.hdr_enabled ? "HEVC Main10 / BT.2020 PQ with metrics HUD"
                                  : "Enabling selects HEVC Main10 at the current resolution");
    SetText(document_, "setting-audio-value",
            config_.audio_configuration == MOONLIGHT_AUDIO_51_SURROUND ? "5.1 surround" : "Stereo");
    SetText(document_, "setting-vsync-value", config_.vsync_enabled ? "On" : "Off");
    SetText(document_, "setting-vsync-help",
            config_.vsync_enabled ? "On shows whole frames at each display refresh"
                                  : "Off flips immediately: lower latency, visible tearing");
    const bool adaptive = config_.decoder_pipeline == MOONLIGHT_DECODER_PIPELINE_ADAPTIVE;
    SetText(document_, "setting-decoder-value", adaptive ? "Adaptive (experimental)" : "Classic");
    SetText(document_, "setting-decoder-help",
            adaptive ? "Overlaps frames while decoding falls behind; may help some hosts"
                     : "Decodes one frame at a time");
    std::snprintf(text, sizeof(text), "%u cores", config_.decoder_cores);
    SetText(document_, "setting-cores-value", text);
    SetText(document_, "setting-cores-help",
            config_.decoder_cores == MOONLIGHT_DECODER_CORES_MIN
                ? "Reserved for decoding; stream threads use the remaining cores"
                : "Experimental: more cores for decoding, fewer for the stream");
    std::snprintf(text, sizeof(text), "%s / %sP%u",
                  config_.hdr_enabled                                 ? "HDR10"
                  : config_.video_codec == MOONLIGHT_VIDEO_CODEC_HEVC ? "HEVC"
                                                                      : "H.264",
                  config_.stream_resolution == MOONLIGHT_STREAM_RESOLUTION_2160P   ? "2160"
                  : config_.stream_resolution == MOONLIGHT_STREAM_RESOLUTION_1440P ? "1440"
                                                                                   : "1080",
                  config_.stream_fps);
    SetText(document_, "header-mode", text);
}

void MoonlightApp::HandleInput(const radio_input_event_t &event)
{
    if (!event.pressed)
        return;
    if (pairing_active_ || stop_pending_)
        return;
    const Screen previous_screen = screen_;
    const unsigned previous_focus = focus_;
    const unsigned previous_app = selected_app_;
    const uint32_t previous_host = config_.selected_host;
    bool navigation = false;
    switch (event.key)
    {
    case RADIO_INPUT_UP:
        navigation = true;
        if (screen_ == Screen::Games)
            MoveGameVertical(-1);
        else
            MoveFocus(-1);
        break;
    case RADIO_INPUT_DOWN:
        navigation = true;
        if (screen_ == Screen::Games)
            MoveGameVertical(1);
        else
            MoveFocus(1);
        break;
    case RADIO_INPUT_LEFT:
        navigation = true;
        if (screen_ == Screen::Hosts && focus_ == 3)
            CycleHost(-1);
        else if (screen_ == Screen::Games && focus_ >= 3 && focus_ <= 8)
            CycleApp(-1);
        else if (screen_ == Screen::Games && focus_ >= 9)
        {
            focus_ = focus_ == 9 ? 10 : 9;
            UpdateFocus();
        }
        else
            MoveFocus(-1);
        break;
    case RADIO_INPUT_RIGHT:
        navigation = true;
        if (screen_ == Screen::Hosts && focus_ == 3)
            CycleHost(1);
        else if (screen_ == Screen::Games && focus_ >= 3 && focus_ <= 8)
            CycleApp(1);
        else if (screen_ == Screen::Games && focus_ >= 9)
        {
            focus_ = focus_ == 9 ? 10 : 9;
            UpdateFocus();
        }
        else
            MoveFocus(1);
        break;
    case RADIO_INPUT_CROSS:
        Activate();
        break;
    case RADIO_INPUT_CIRCLE:
        if (screen_ != Screen::Hosts)
        {
            prosperolight::ui_sound_play(prosperolight::UiSoundCue::Back);
            SetScreen(Screen::Hosts);
        }
        break;
    case RADIO_INPUT_OPTIONS:
        if (screen_ != Screen::Settings)
        {
            prosperolight::ui_sound_play(prosperolight::UiSoundCue::Confirm);
            SetScreen(Screen::Settings);
        }
        break;
    case RADIO_INPUT_SQUARE:
        if (backend_.current_app_id)
        {
            if (!backend_.paired)
            {
                char text[192];
                SetScreen(Screen::Hosts);
                std::snprintf(text, sizeof(text),
                              "Not paired: stop the active app on its original client or in "
                              "Sunshine.");
                SetText(document_, "host-action-status", text);
            }
            else
            {
                if (screen_ != Screen::Games)
                    SetScreen(Screen::Games);
                RequestStopActiveApp();
            }
        }
        else if (screen_ == Screen::Games)
        {
            RequestStopActiveApp();
        }
        break;
    case RADIO_INPUT_TRIANGLE:
        prosperolight::ui_sound_play(prosperolight::UiSoundCue::Confirm);
        RefreshBackend();
        break;
    case RADIO_INPUT_L1:
    {
        navigation = true;
        const unsigned current = static_cast<unsigned>(screen_);
        SetScreen(static_cast<Screen>(current == 0 ? static_cast<unsigned>(Screen::Count) - 1
                                                   : current - 1));
        break;
    }
    case RADIO_INPUT_R1:
    {
        navigation = true;
        const unsigned current = static_cast<unsigned>(screen_);
        SetScreen(static_cast<Screen>((current + 1) % static_cast<unsigned>(Screen::Count)));
        break;
    }
    default:
        break;
    }
    if (navigation && (screen_ != previous_screen || focus_ != previous_focus ||
                       selected_app_ != previous_app || config_.selected_host != previous_host))
    {
        prosperolight::ui_sound_play(prosperolight::UiSoundCue::Move);
    }
}

MoonlightApp::Command MoonlightApp::TakeCommand()
{
    const Command command = command_;
    command_ = Command::None;
    return command;
}

const char *MoonlightApp::SelectedAppName() const
{
    return selected_app_ < backend_.app_count ? backend_.apps[selected_app_].name : "";
}

int MoonlightApp::SelectedAppId() const
{
    return selected_app_ < backend_.app_count ? backend_.apps[selected_app_].id : 0;
}

const char *MoonlightApp::SelectedHostAddress() const
{
    const moonlight_config_host_t *host = SelectedHost();
    return host ? host->address : "";
}

uint16_t MoonlightApp::SelectedHostPort() const
{
    return moonlight_config_host_port(SelectedHost());
}

unsigned MoonlightApp::BitrateKbps() const
{
    return bitrate_mbps_ * 1000;
}

unsigned MoonlightApp::DisplayArea() const
{
    return config_.display_area;
}

unsigned MoonlightApp::VideoCodec() const
{
    return config_.video_codec;
}

unsigned MoonlightApp::StreamResolution() const
{
    return config_.stream_resolution;
}

unsigned MoonlightApp::StreamFps() const
{
    return config_.stream_fps;
}

unsigned MoonlightApp::HdrEnabled() const
{
    return config_.hdr_enabled;
}

unsigned MoonlightApp::AudioConfiguration() const
{
    return config_.audio_configuration;
}

unsigned MoonlightApp::VsyncEnabled() const
{
    return config_.vsync_enabled;
}

unsigned MoonlightApp::DecoderPipeline() const
{
    return config_.decoder_pipeline;
}

unsigned MoonlightApp::DecoderCores() const
{
    return config_.decoder_cores;
}
