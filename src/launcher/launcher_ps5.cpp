/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "launcher/launcher.hpp"
#include "ui_sound_preferences.hpp"
#if PROSPEROLIGHT_PYROWAVE
#include "pyrowave/video/ps5_presentation_stats.hpp"
#endif

#include "app_storage.hpp"
#include "../../platform/ps5/ps5_thread_placement.h"
#include "connecting_plate.hpp"
#include "self_update.h"
#include "update_check.h"
#include "launcher/launcher_model.hpp"
#include "launcher/launcher_view.hpp"
#include "native_agc_present.hpp"
#include "launcher/launcher_artwork.hpp"

#include "audio/cues.hpp"
#include "audio/mixer.hpp"
#include "core/input.hpp"
#include "core/save_file.hpp"
#include "core/version.hpp"
#include "gfx/renderer.hpp"
#include "platform/ps5/audio_out.hpp"
#include "platform/ps5/display_egl.hpp"
#include "platform/ps5/pad.hpp"
#include "platform/ps5/system.hpp"
#include "ui/fonts.hpp"

#include <GL/glcorearb.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <mutex>
#include <string>
#include <vector>

extern "C" int sceUserServiceGetLoginUserIdList(std::int32_t user_ids[4]);

#ifndef PROSPEROLIGHT_STREAM_SELF_TEST_FPS
#define PROSPEROLIGHT_STREAM_SELF_TEST_FPS 0
#endif
#ifndef PROSPEROLIGHT_STREAM_SELF_TEST_RESOLUTION
#define PROSPEROLIGHT_STREAM_SELF_TEST_RESOLUTION MOONLIGHT_STREAM_RESOLUTION_1080P
#endif
#ifndef PROSPEROLIGHT_UPDATE_DEV_OFFER
#define PROSPEROLIGHT_UPDATE_DEV_OFFER 0
#endif
#ifndef PROSPEROLIGHT_UPDATE_AUTO_ACCEPT
#define PROSPEROLIGHT_UPDATE_AUTO_ACCEPT 0
#endif
#ifndef PROSPEROLIGHT_STOP_ACTIVE_APP_SELF_TEST
#define PROSPEROLIGHT_STOP_ACTIVE_APP_SELF_TEST 0
#endif

static_assert(PROSPEROLIGHT_STREAM_SELF_TEST_FPS == 0 ||
                  (PROSPEROLIGHT_STREAM_SELF_TEST_FPS >= 30 &&
                   PROSPEROLIGHT_STREAM_SELF_TEST_FPS <= 120),
              "STREAM_SELF_TEST_FPS must be 0, or 30 to 120");
static_assert(PROSPEROLIGHT_STREAM_SELF_TEST_RESOLUTION == MOONLIGHT_STREAM_RESOLUTION_1080P ||
                  PROSPEROLIGHT_STREAM_SELF_TEST_RESOLUTION == MOONLIGHT_STREAM_RESOLUTION_1440P ||
                  PROSPEROLIGHT_STREAM_SELF_TEST_RESOLUTION == MOONLIGHT_STREAM_RESOLUTION_2160P,
              "STREAM_SELF_TEST_RESOLUTION must be 0, 1, or 2");
static_assert(PROSPEROLIGHT_STOP_ACTIVE_APP_SELF_TEST == 0 ||
                  PROSPEROLIGHT_STOP_ACTIVE_APP_SELF_TEST == 1,
              "STOP_ACTIVE_APP_SELF_TEST must be 0 or 1");

extern "C" void prosperolight_release_splash(void);

// The self-update kit's paths (third_party/update-check/self_update_paths.h):
// with filesystem access /app0 and /download0 are not where the app's files are.
extern "C" const char *prosperolight_self_update_path(int which)
{
    static const std::string helper = std::string(storage::paths().app) + "/self-updater.elf";
    static const std::string param = std::string(storage::paths().app) + "/sce_sys/param.json";
    static const std::string sequence = storage::setting_file("self-update-sequence");
    return which == 0 ? helper.c_str() : which == 1 ? param.c_str() : sequence.c_str();
}

namespace launcher
{

namespace
{

using namespace hui;

// The app's own files: /app0 in the sandbox, the install folder outside it.
std::string Assets()
{
    return std::string(storage::paths().app) + "/assets";
}

// The folders the About screen names.
View::Storage StorageFolders()
{
    const storage::Paths &paths = storage::paths();
    View::Storage folders;
    folders.access = paths.status == 0;
    folders.settings = paths.config;
    const std::size_t slash = folders.settings.rfind('/');
    if (slash != std::string::npos && slash != 0)
        folders.settings.resize(slash);
    folders.pairing = paths.pairing;
    folders.logs = paths.logs;
    return folders;
}

#if PROSPEROLIGHT_STREAM_SELF_TEST_FPS != 0
#ifndef PROSPEROLIGHT_STREAM_SELF_TEST_CODEC
#define PROSPEROLIGHT_STREAM_SELF_TEST_CODEC MOONLIGHT_VIDEO_CODEC_HEVC
#endif
bool high_refresh_self_test_consumed;
#endif
#if PROSPEROLIGHT_STOP_ACTIVE_APP_SELF_TEST != 0
bool stop_active_app_self_test_consumed;
#endif

bool LoadFont(gfx::Renderer &renderer, const char *name, gfx::Font *font, ui::FontRef *ref)
{
    std::string data;
    const std::string path = Assets() + "/fonts/" + name;
    if (!save::read_file(path, &data, 8u << 20) || !font->load(data))
    {
        sys::log("[PL] launcher: font %s failed: %s", name, font->error().c_str());
        return false;
    }
    ref->font = font;
    ref->texture = renderer.batch().create_font_texture(*font);
    return true;
}

// What the player chose, for the stream. False when nothing can be streamed.
bool FillSelection(const Model &model, Selection *selection)
{
    const moonlight_config_t &config = model.config();
    const moonlight_config_host_t *host = model.selected_host();
    const moonlight_backend_snapshot_t &backend = model.backend();
    if (!host || model.selected_app() >= backend.app_count)
        return false;
    const moonlight_backend_app_t &app = backend.apps[model.selected_app()];
    std::snprintf(selection->host, sizeof(selection->host), "%s", host->address);
    selection->host_port = moonlight_config_host_port(host);
    std::snprintf(selection->app_name, sizeof(selection->app_name), "%s", app.name);
    selection->app_id = app.id;
    selection->bitrate_kbps = config.bitrate_mbps * 1000u;
    selection->display_area = config.display_area;
    selection->video_codec = config.video_codec;
    selection->stream_resolution = config.stream_resolution;
    selection->stream_fps = config.stream_fps;
    selection->hdr_enabled = config.hdr_enabled;
    selection->chroma_sampling = config.chroma_sampling;
    selection->audio_configuration = config.audio_configuration;
    selection->vsync_enabled = config.vsync_enabled;
    selection->decoder_pipeline = config.decoder_pipeline;
    selection->decoder_cores = config.decoder_cores;
    return true;
}

int SignedInUsers()
{
    std::int32_t users[4] = {-1, -1, -1, -1};
    if (sceUserServiceGetLoginUserIdList(users) < 0)
        return 1;
    int count = 0;
    for (std::int32_t user : users)
        count += user != -1 ? 1 : 0;
    return count > 0 ? count : 1;
}

// A picture of the connecting screen for the stream to keep showing while it
// connects. The bar's fill is left out: the stream draws it, further each frame.
void CaptureConnecting(gfx::Renderer &renderer, View &view, Frame &frame, Selection *selection)
{
    constexpr int kWidth = connecting::kWidth;
    constexpr int kHeight = connecting::kHeight;
    const std::int64_t started = sys::monotonic_us();
    selection->connecting_rgba.clear();

    GLuint framebuffer = 0;
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kWidth, kHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (ok)
    {
        view.set_plate(true);
        frame.reset();
        frame.glass_texture = renderer.glass_texture();
        view.draw(frame);
        view.set_plate(false);
        renderer.begin();
        renderer.backdrop(frame.backdrop);
        renderer.draw(frame.scene);
        renderer.glass();
        renderer.draw(frame.overlay);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        renderer.present(framebuffer, kWidth, kHeight);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kWidth) * kHeight * 4u);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, kWidth, kHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        ok = glGetError() == GL_NO_ERROR;
        // A picture with nothing in it was not read back.
        bool lit = false;
        for (std::size_t at = 0; ok && !lit && at < pixels.size(); at += 4u * 997u)
            lit = pixels[at] != 0 || pixels[at + 1] != 0 || pixels[at + 2] != 0;
        ok = ok && lit;
        if (ok)
        {
            // OpenGL's first row is the bottom one.
            const std::size_t row = static_cast<std::size_t>(kWidth) * 4u;
            selection->connecting_rgba.resize(pixels.size());
            for (int y = 0; y < kHeight; ++y)
                std::memcpy(selection->connecting_rgba.data() + static_cast<std::size_t>(y) * row,
                            pixels.data() + static_cast<std::size_t>(kHeight - 1 - y) * row, row);
            const View::ConnectBar bar = view.connecting_bar();
            selection->connecting_bar[0] = bar.rect.x;
            selection->connecting_bar[1] = bar.rect.y;
            selection->connecting_bar[2] = bar.rect.w;
            selection->connecting_bar[3] = bar.rect.h;
            selection->connecting_fill[0] = static_cast<std::uint8_t>(bar.fill.r * 255.0f + 0.5f);
            selection->connecting_fill[1] = static_cast<std::uint8_t>(bar.fill.g * 255.0f + 0.5f);
            selection->connecting_fill[2] = static_cast<std::uint8_t>(bar.fill.b * 255.0f + 0.5f);
            selection->connecting_progress = bar.progress;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteTextures(1, &texture);
    sys::log("[PL] launcher: connecting picture %s in %lld ms", ok ? "taken" : "not available",
             static_cast<long long>((sys::monotonic_us() - started) / 1000));
}

// Updates (third_party/update-check). Once per launch the worker asks the
// homebrew.page catalog whether a newer ProsperoLight is listed: the kit
// verifies the catalog's signature and the hashes before it believes the
// answer. When the player says so, the release ZIP is downloaded from GitHub
// and streamed to the self-update helper (self-updater.elf in the app's
// folder, sent to the console's payload loader), which checks and unpacks it
// beside the app and replaces the app's files once the app has closed.
// Nothing is changed before the player confirms; a failure or a cancel leaves
// the app as it was.
std::mutex g_update_lock;
self_update_check_result g_update_answer = SELF_UPDATE_UNKNOWN;
self_update_offer g_update_offer{};
self_update_job g_update_job{}; // zero until the first begin, as the kit asks
bool g_update_begun = false;

#if PROSPEROLIGHT_UPDATE_DEV_OFFER
// Development only: update-offer.txt in the app's folder replaces the
// catalog's answer, and with it the catalog's signature. Five lines: the new
// content version, the release's name, its ZIP on GitHub, its SHA-256, its
// size in bytes.
bool DevelopmentOffer(self_update_offer *out)
{
    const std::string path = std::string(storage::paths().app) + "/update-offer.txt";
    std::FILE *file = std::fopen(path.c_str(), "r");
    if (!file)
        return false;
    char lines[5][600] = {};
    bool complete = true;
    for (auto &line : lines)
    {
        if (!std::fgets(line, sizeof(line), file))
        {
            complete = false;
            break;
        }
        line[std::strcspn(line, "\r\n")] = '\0';
    }
    std::fclose(file);
    self_update_offer filled{};
    const std::string param = std::string(storage::paths().app) + "/sce_sys/param.json";
    if (!complete || !update_check_read_param(param.c_str(), filled.title, filled.installed))
        return false;
    std::snprintf(filled.name, sizeof(filled.name), "ProsperoLight");
    std::snprintf(filled.available, sizeof(filled.available), "%s", lines[0]);
    std::snprintf(filled.version, sizeof(filled.version), "%s", lines[1]);
    std::snprintf(filled.artifact, sizeof(filled.artifact), "%s", lines[2]);
    std::snprintf(filled.sha256, sizeof(filled.sha256), "%s", lines[3]);
    filled.size = std::strtoull(lines[4], nullptr, 10);
    // Like the catalog, only a newer version is offered (content versions compare as text).
    if (std::strcmp(filled.available, filled.installed) <= 0)
        return false;
    *out = filled;
    return true;
}
#endif

// Blocks on the network; it runs on the model's worker.
bool CheckForUpdate(UpdateOffer *offer)
{
    static const char *const kNames[] = {"available", "up-to-date", "unknown", "untrusted",
                                         "not-installable"};
    const std::int64_t started = sys::monotonic_us();
    self_update_offer found{};
    self_update_check_result state = self_update_check_self(&found);
#if PROSPEROLIGHT_UPDATE_DEV_OFFER
    if (DevelopmentOffer(&found))
    {
        sys::log("[PL] update check: DEVELOPMENT: update-offer.txt replaces the catalog's answer");
        state = SELF_UPDATE_AVAILABLE;
    }
#endif
    sys::log("[PL] update check: result=%s installed=%s available=%s version=%s size=%llu in "
             "%lld ms",
             static_cast<unsigned>(state) < 5 ? kNames[state] : "?",
             found.installed[0] ? found.installed : "-", found.available[0] ? found.available : "-",
             found.version[0] ? found.version : "-", static_cast<unsigned long long>(found.size),
             static_cast<long long>((sys::monotonic_us() - started) / 1000));
    if (state != SELF_UPDATE_AVAILABLE && state != SELF_UPDATE_NOT_INSTALLABLE)
        return false;
    {
        const std::lock_guard<std::mutex> guard(g_update_lock);
        g_update_answer = state;
        g_update_offer = found;
    }
    offer->installable = state == SELF_UPDATE_AVAILABLE;
    std::snprintf(offer->version, sizeof(offer->version), "%s",
                  found.version[0] ? found.version : found.available);
    offer->size = found.size;
    return true;
}

bool BeginUpdate()
{
    const std::lock_guard<std::mutex> guard(g_update_lock);
    if (g_update_answer != SELF_UPDATE_AVAILABLE)
        return false;
    if (g_update_begun)
        self_update_finish(&g_update_job);
    g_update_begun = self_update_start(&g_update_job, self_update_console(), &g_update_offer) == 1;
    sys::log("[PL] update: %s %s", g_update_begun ? "updating to" : "could not begin for",
             g_update_offer.available);
    return g_update_begun;
}

void PollUpdate(UpdateProgress *progress)
{
    static int reported = -1;
    const std::lock_guard<std::mutex> guard(g_update_lock);
    *progress = UpdateProgress{};
    if (!g_update_begun)
        return;
    self_update_status status{};
    self_update_poll(&g_update_job, &status);
    static constexpr UpdatePhase kPhases[] = {
        UpdatePhase::idle,  UpdatePhase::starting, UpdatePhase::downloading, UpdatePhase::unpacking,
        UpdatePhase::ready, UpdatePhase::applying, UpdatePhase::cancelled,   UpdatePhase::failed};
    progress->phase =
        static_cast<unsigned>(status.phase) < 8 ? kPhases[status.phase] : UpdatePhase::idle;
    progress->done = status.done;
    progress->total = status.total;
    std::snprintf(progress->time_left, sizeof(progress->time_left), "%s", status.time_left);
    std::snprintf(progress->error, sizeof(progress->error), "%s", status.error);
    if (static_cast<int>(status.phase) != reported)
    {
        reported = static_cast<int>(status.phase);
        sys::log("[PL] update: phase=%d done=%llu total=%llu error=%s", reported,
                 static_cast<unsigned long long>(status.done),
                 static_cast<unsigned long long>(status.total),
                 status.error[0] ? status.error : "-");
    }
}

void CancelUpdate()
{
    const std::lock_guard<std::mutex> guard(g_update_lock);
    if (g_update_begun)
        self_update_cancel(&g_update_job);
}

bool ApplyUpdate()
{
    const std::lock_guard<std::mutex> guard(g_update_lock);
    if (!g_update_begun)
        return false;
    const bool going = self_update_apply(&g_update_job) == 1;
    sys::log("[PL] update: %s",
             going ? "staged; the helper replaces the files once ProsperoLight has closed"
                   : "the helper did not take the go-ahead");
    return going;
}

void FinishUpdate()
{
    const std::lock_guard<std::mutex> guard(g_update_lock);
    if (!g_update_begun)
        return;
    self_update_finish(&g_update_job);
    g_update_begun = false;
}

// Where the launcher's threads run. Threads on the console run first-in
// first-out at one priority and are never time-sliced, so a worker busy with
// a TLS handshake on the screen's CPU stopped the screen for a second. The
// screen gets one core (CPU 0 and its twin); requests to Sunshine run on the
// other eleven CPUs. The stream sets its own placement.
constexpr std::uint64_t kScreenCpus = 0x0003;
constexpr std::uint64_t kWorkerCpus = 0x1ffc;

void PlaceWorker()
{
    (void)ps5_thread_affinity_set(kWorkerCpus);
}

// The recorded sounds are read once and kept: a stream does not need the
// memory back, and the launcher returns after every stream.
audio::SoundBank &Sounds()
{
    static audio::SoundBank bank;
    static bool loaded = false;
    if (!loaded)
    {
        loaded = true;
        const auto stats = bank.load(Assets() + "/audio/sfx");
        sys::log("[PL] launcher: sounds files=%d rejected=%d", stats.files, stats.rejected);
    }
    return bank;
}

} // namespace

Result Run(Selection *selection, const char *stream_error, bool first_start)
{
    const std::int64_t opened_at = sys::monotonic_us();
    // The screen's thread gets its own core while the launcher runs; the
    // stream finds the mask it had.
    std::uint64_t main_cpus = 0;
    const bool main_cpus_known = ps5_thread_affinity_get(&main_cpus) == 0;
    // Milliseconds since the launcher began to open, for the log.
    const auto elapsed = [opened_at]
    { return static_cast<long long>((sys::monotonic_us() - opened_at) / 1000); };
    sys::log("[PL] launcher: opening (first=%d)", first_start ? 1 : 0);

#if PROSPEROLIGHT_PYROWAVE
    struct LauncherOutputPolicy
    {
        LauncherOutputPolicy()
        {
            ps5_launcher_output_policy(true);
        }
        ~LauncherOutputPolicy()
        {
            ps5_launcher_output_policy(false);
        }
    } launcher_output_policy;
#endif
    // 4K when the television takes it, else 1080p.
    ps5::Display display;
    if (!(ps5::Display::supports_display_modes() && display.open(3840, 2160)) &&
        !display.open(1920, 1080))
    {
        sys::log("[PL] launcher: display open failed");
        return Result::failed;
    }
    sys::log("[PL] launcher: display %dx%d after %lld ms", display.width(), display.height(),
             elapsed());
    // After the display: the graphics runtime's own threads keep the full mask.
    const int placed = ps5_thread_affinity_set(kScreenCpus);
    sys::log("[PL] launcher: screen thread on CPUs %llx (was %llx), result %d",
             static_cast<unsigned long long>(kScreenCpus),
             static_cast<unsigned long long>(main_cpus), placed);

    Result outcome = Result::failed;
    {
        gfx::Renderer renderer;
        gfx::Font regular;
        gfx::Font semibold;
        gfx::Font headline;
        gfx::Font mono;
        ui::Fonts fonts;
        bool ready = renderer.init();
        sys::log("[PL] launcher: renderer after %lld ms", elapsed());
        ready = ready && LoadFont(renderer, "inter-regular.huifont", &regular, &fonts.regular) &&
                LoadFont(renderer, "inter-semibold.huifont", &semibold, &fonts.semibold);
        sys::log("[PL] launcher: two fonts after %lld ms", elapsed());
        ready = ready &&
                LoadFont(renderer, "montserrat-medium.huifont", &headline, &fonts.display) &&
                LoadFont(renderer, "dejavu-sans-mono.huifont", &mono, &fonts.mono);
        // The launcher's theme uses neither of these two faces.
        fonts.pixel = fonts.mono;
        fonts.hand = fonts.regular;
        sys::log("[PL] launcher: renderer and fonts after %lld ms", elapsed());
        if (!ready)
        {
            sys::log("[PL] launcher: renderer or fonts failed");
        }
        else
        {
            ps5::Pad pad;
            pad.open();
            InputTracker tracker;
            audio::Mixer mixer;
            ps5::AudioOut audio_out;
            audio_out.start(mixer);
            audio::SoundBank &sounds = Sounds();

            Model model;
            model.set_artwork_decoder(DecodePoster);
            model.set_worker_start(PlaceWorker);
            // Once per launch of the app, not after every stream.
            if (first_start)
                model.set_update_check(CheckForUpdate);
            model.Initialize(static_cast<std::uint64_t>(sys::monotonic_us() / 1000));
            View view(model, fonts);
            view.set_version(
                read_content_version(std::string(storage::paths().app) + "/sce_sys/param.json"));
            view.set_storage(StorageFolders());
            view.set_first_start(first_start);
            view.show_stream_error(stream_error);
            view.set_players(SignedInUsers());
            view.set_update_actions(
                {BeginUpdate, PollUpdate, CancelUpdate, ApplyUpdate, FinishUpdate});
            sys::log("[PL] launcher: sound, controller and screens after %lld ms", elapsed());

            Frame frame;
            ui::Feedback feedback;
            PadSample samples[64];
            std::vector<std::uint32_t> posters;
            std::uint64_t frames = 0;
            unsigned slow_frames = 0;
            std::int64_t last_frame = sys::monotonic_us();
            for (;;)
            {
                const std::int64_t now = sys::monotonic_us();
                // A frame that took long enough to be seen as a stutter.
                if (frames > 1 && now - last_frame > 50000 && slow_frames < 60)
                {
                    ++slow_frames;
                    sys::log("[PL] launcher: slow frame %lld ms on screen %d (frame %llu)",
                             static_cast<long long>((now - last_frame) / 1000), view.screen(),
                             static_cast<unsigned long long>(frames));
                }
                float dt = frames == 0 ? 1.0f / 60.0f : static_cast<float>(now - last_frame) / 1e6f;
                last_frame = now;
                if (dt > 0.05f)
                    dt = 0.05f; // a hitch must not teleport the animations
                std::int64_t mark = now;
                long long parts[6] = {};
                const auto lap = [&mark](long long &part)
                {
                    const std::int64_t at = sys::monotonic_us();
                    part = static_cast<long long>(at - mark);
                    mark = at;
                };
                const std::size_t count = pad.read(samples);
                const InputFrame input = tracker.update(std::span<const PadSample>(samples, count),
                                                        static_cast<std::uint64_t>(now));

                model.Poll(static_cast<std::uint64_t>(now / 1000));
                lap(parts[0]);
                ArtworkImage image;
                while (model.TakeArtwork(&image))
                {
                    const std::uint32_t texture = renderer.batch().create_texture(
                        image.width, image.height, image.rgba.data());
                    posters.push_back(texture);
                    view.set_artwork(
                        image.app_id, texture,
                        static_cast<float>(image.width) / static_cast<float>(image.height),
                        View::palette_of(image.rgba.data(), image.width, image.height));
                }
                for (std::uint32_t texture : view.take_released_textures())
                {
                    glDeleteTextures(1, &texture);
                    std::erase(posters, texture);
                }
                if (frames % 60 == 30)
                    view.set_players(SignedInUsers());
                lap(parts[1]);

                feedback.clear();
                view.update(input, dt, feedback);
                if (prosperolight::ui_sound_enabled())
                    for (const audio::CueEvent &event : feedback.cues)
                        sounds.play(mixer,
                                    event.set == audio::SoundSet::count ? audio::SoundSet::glass
                                                                        : event.set,
                                    event);

                lap(parts[2]);
                frame.reset();
                frame.glass_texture = renderer.glass_texture();
                view.draw(frame);
                lap(parts[3]);
                renderer.begin();
                renderer.backdrop(frame.backdrop);
                renderer.draw(frame.scene);
                renderer.glass();
                renderer.draw(frame.overlay);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
                renderer.present(0, display.width(), display.height());
                lap(parts[4]);
                const bool swapped = display.swap();
                lap(parts[5]);
                if (parts[0] + parts[1] + parts[2] + parts[3] + parts[4] + parts[5] > 50000 &&
                    slow_frames < 60)
                {
                    ++slow_frames;
                    sys::log("[PL] launcher: frame %llu took (us) poll %lld, artwork %lld, update "
                             "%lld, draw %lld, render %lld, swap %lld",
                             static_cast<unsigned long long>(frames), parts[0], parts[1], parts[2],
                             parts[3], parts[4], parts[5]);
                }
                if (!swapped)
                {
                    sys::log("[PL] launcher: swap failed frame=%llu error=%s",
                             static_cast<unsigned long long>(frames),
                             ps5::egl_error_name(display.last_error()));
                    break;
                }
                if (++frames == 1)
                {
                    // The console's splash picture has covered the wait.
                    prosperolight_release_splash();
                    const bool hidden = sys::hide_splash_screen();
                    sys::log("[PL] launcher: first frame after %lld ms, splash hidden=%d",
                             elapsed(), hidden ? 1 : 0);
                }

#if PROSPEROLIGHT_STOP_ACTIVE_APP_SELF_TEST != 0
                if (!stop_active_app_self_test_consumed && model.backend_valid() &&
                    model.backend().online && model.backend().paired &&
                    model.backend().current_app_id)
                {
                    stop_active_app_self_test_consumed = true;
                    model.StopApp();
                }
#endif
#if PROSPEROLIGHT_STREAM_SELF_TEST_FPS != 0
                // A test build starts one stream by itself, with its own values.
                if (!high_refresh_self_test_consumed && model.backend_valid() &&
                    model.backend().online && model.backend().paired && model.backend().app_count)
                {
                    high_refresh_self_test_consumed = true;
                    model.SelectApp(0);
                    if (FillSelection(model, selection))
                    {
                        selection->bitrate_kbps = 80000;
                        selection->video_codec = PROSPEROLIGHT_STREAM_SELF_TEST_CODEC;
                        selection->stream_resolution = PROSPEROLIGHT_STREAM_SELF_TEST_RESOLUTION;
                        selection->stream_fps = PROSPEROLIGHT_STREAM_SELF_TEST_FPS;
                        selection->hdr_enabled = 0;
                        outcome = Result::start_stream;
                        break;
                    }
                }
#endif
#if PROSPEROLIGHT_UPDATE_DEV_OFFER && PROSPEROLIGHT_UPDATE_AUTO_ACCEPT > 0
                // Development only: an unattended console run says yes by itself.
                {
                    static std::int64_t offered_at = 0;
                    static bool accepted = false;
                    if (view.update_offer_open() && !accepted)
                    {
                        if (offered_at == 0)
                            offered_at = sys::monotonic_us();
                        else if (sys::monotonic_us() - offered_at >
                                 PROSPEROLIGHT_UPDATE_AUTO_ACCEPT * INT64_C(1000000))
                        {
                            accepted = true;
                            InputFrame yes;
                            yes.connected = true;
                            yes.pressed = hui::action_bit(hui::Action::confirm);
                            view.update(yes, 0.0f, feedback);
                            sys::log(
                                "[PL] update: DEVELOPMENT: the offer was accepted by the build");
                        }
                    }
                }
#endif
                if (view.take_update_exit())
                {
                    outcome = Result::update_exit;
                    break;
                }
                if (view.take_start_stream() && FillSelection(model, selection))
                {
                    CaptureConnecting(renderer, view, frame, selection);
                    outcome = Result::start_stream;
                    break;
                }
            }

            // Hand everything back before the stream opens it for itself:
            // the worker thread, the audio port, the controller, then the
            // graphics objects (they die with the context) and the display.
            sys::log("[PL] launcher: closing after %llu frames (stream=%d)",
                     static_cast<unsigned long long>(frames),
                     outcome == Result::start_stream ? 1 : 0);
            model.Shutdown();
            audio_out.stop();
            pad.close();
            if (!posters.empty())
                glDeleteTextures(static_cast<GLsizei>(posters.size()), posters.data());
            if (frames > 0)
                native_agc_note_initialized();
        }
        renderer.release();
    }
    display.close();
    if (main_cpus_known)
        (void)ps5_thread_affinity_set(main_cpus);
    sys::log("[PL] launcher: closed");
    return outcome;
}

} // namespace launcher
