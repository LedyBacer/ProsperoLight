/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// The launcher on the PC: the same model and view as the console build, a
// pretend Sunshine network, scripted controller input and a fixed 60 Hz clock.
// It writes a PNG of every state the script names and fails if a check about
// the launcher's behaviour does not hold.
//
// usage: launcher_host <assets dir> <output dir> [width height]

#include "fake_world.hpp"
#include "app_storage.hpp"
#include "presentation_preferences.hpp"
#include "host_quit_preferences.hpp"
#include "stream_profile.hpp"
#include "lan_http_report.hpp"
#include "connecting_plate.hpp"
#include "i18n.hpp"
#include "launcher/launcher_model.hpp"
#include "launcher/launcher_view.hpp"

#include "audio/cues.hpp"
#include "core/save_file.hpp"
#include "gfx/gl_program.hpp"
#include "gfx/renderer.hpp"
#include "ui/fonts.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <set>
#include <unistd.h>
#include <vector>

namespace
{

using hui::Action;
using hui::Direction;
using hui::gfx::Color;

bool open_context()
{
    auto get_platform_display = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));
    EGLDisplay display =
        get_platform_display != nullptr
            ? get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr)
            : eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0;
    EGLint minor = 0;
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor) ||
        !eglBindAPI(EGL_OPENGL_API))
        return false;
    const EGLint attributes[] = {EGL_CONTEXT_MAJOR_VERSION,
                                 4,
                                 EGL_CONTEXT_MINOR_VERSION,
                                 5,
                                 EGL_CONTEXT_OPENGL_PROFILE_MASK,
                                 EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                                 EGL_NONE};
    EGLContext context = eglCreateContext(display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attributes);
    return context != EGL_NO_CONTEXT &&
           eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context);
}

bool load_font(hui::gfx::Renderer &renderer, const std::string &path, hui::gfx::Font *font,
               hui::ui::FontRef *ref)
{
    std::string data;
    if (!hui::save::read_file(path, &data, 32u << 20) || !font->load(data))
    {
        std::fprintf(stderr, "cannot load font %s\n", path.c_str());
        return false;
    }
    ref->font = font;
    ref->texture = renderer.batch().create_font_texture(*font);
    return true;
}

// A poster for an app: two tones and a few shapes, different for each one.
// The pretend network sends one byte for a picture; its value picks the poster.
bool make_poster(const unsigned char *png, std::size_t size, launcher::ArtworkImage *image)
{
    constexpr int kWidth = 400;
    constexpr int kHeight = 600;
    if (size != 1)
        return false;
    const int app_id = png[0];
    launcher::Palette tones;
    launcher::Palette *palette = &tones;
    static const std::uint32_t kTones[][3] = {
        {0x2a0f4f, 0x8a2f8f, 0xff5fa2}, {0x0f3d3a, 0x2fa59a, 0x86f0dc},
        {0x1d2e12, 0x4f7a28, 0xcfe07a}, {0x08142e, 0x1f5fb0, 0x35e0ff},
        {0x3a1c10, 0xb0683a, 0xffd9a0}, {0x101820, 0x3a4a52, 0xf2b84b},
        {0x2e0d12, 0xa02a3a, 0xff8a7a}, {0x14103a, 0x4a3fb0, 0xb9a8ff},
    };
    const std::uint32_t *tone = kTones[static_cast<unsigned>(app_id) % 8u];
    palette->dark = Color::rgb(tone[0]);
    palette->mid = Color::rgb(tone[1]);
    palette->accent = Color::rgb(tone[2]);
    std::vector<std::uint8_t> &pixels = image->rgba;
    pixels.resize(static_cast<std::size_t>(kWidth * kHeight * 4));
    const float cx = 120.0f + static_cast<float>((app_id * 37) % 160);
    const float cy = 180.0f + static_cast<float>((app_id * 53) % 240);
    const float radius = 92.0f + static_cast<float>((app_id * 11) % 60);
    for (int y = 0; y < kHeight; ++y)
    {
        for (int x = 0; x < kWidth; ++x)
        {
            const float t = static_cast<float>(y) / kHeight;
            Color c = hui::gfx::mix(palette->mid, palette->dark, t);
            const float d = std::hypot(static_cast<float>(x) - cx, static_cast<float>(y) - cy);
            if (d < radius)
                c = palette->accent;
            else if (std::fmod(static_cast<float>(x + y * 2 + app_id * 9), 128.0f) < 5.0f)
                c = hui::gfx::mix(c, palette->accent, 0.35f);
            std::uint8_t *p = &pixels[static_cast<std::size_t>((y * kWidth + x) * 4)];
            p[0] = static_cast<std::uint8_t>(c.r * 255.0f);
            p[1] = static_cast<std::uint8_t>(c.g * 255.0f);
            p[2] = static_cast<std::uint8_t>(c.b * 255.0f);
            p[3] = 255;
        }
    }
    image->width = kWidth;
    image->height = kHeight;
    return true;
}

struct Step
{
    int frames = 30;
    std::uint32_t press = 0;
    Direction nav = Direction::none;
    const char *capture = nullptr;
    std::uint32_t hold = 0;
    void (*before)() = nullptr; // changes the pretend network before the step
};

int failures = 0;

void expect(bool ok, const char *what)
{
    if (!ok)
    {
        ++failures;
        std::fprintf(stderr, "FAILED: %s\n", what);
    }
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: %s <assets dir> <output dir> [width height]\n", argv[0]);
        return 2;
    }
    const std::string assets = argv[1];
    setenv("PROSPEROLIGHT_LOCALE_DIR", (assets + "/locales").c_str(), 1);
    const std::string output = argv[2];
    const int width = argc > 4 ? std::atoi(argv[3]) : connecting::kWidth;
    const int height = argc > 4 ? std::atoi(argv[4]) : connecting::kHeight;
    if (!open_context())
    {
        std::fprintf(stderr, "no surfaceless EGL OpenGL 4.5 context\n");
        return 1;
    }
    hui::gfx::set_glsl_prefix("#version 450 core\n");

    hui::gfx::Renderer renderer;
    hui::gfx::Font regular;
    hui::gfx::Font semibold;
    hui::gfx::Font display;
    hui::gfx::Font mono;
    hui::ui::Fonts fonts;
    if (!renderer.init() ||
        !load_font(renderer, assets + "/fonts/inter-regular.huifont", &regular, &fonts.regular) ||
        !load_font(renderer, assets + "/fonts/inter-semibold.huifont", &semibold,
                   &fonts.semibold) ||
        !load_font(renderer, assets + "/fonts/montserrat-medium.huifont", &display,
                   &fonts.display) ||
        !load_font(renderer, assets + "/fonts/dejavu-sans-mono.huifont", &mono, &fonts.mono))
        return 1;
    fonts.pixel = fonts.mono;
    fonts.hand = fonts.regular;

    // Every sound the widgets ask for must have a recording in the launcher's set.
    hui::audio::SoundBank sounds;
    const auto bank = sounds.load(assets + "/audio/sfx");
    std::fprintf(stderr, "sounds: %d files, %d rejected\n", bank.files, bank.rejected);
    expect(bank.files > 0 && bank.rejected == 0, "the sound set loads");

    GLuint framebuffer = 0;
    GLuint color = 0;
    glGenFramebuffers(1, &framebuffer);
    glGenRenderbuffers(1, &color);
    glBindRenderbuffer(GL_RENDERBUFFER, color);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return 1;
    hui::save::ensure_directory(output);
    const auto preferences = output + "/preferences";
    hui::save::ensure_directory(preferences);
    std::snprintf(storage::g_paths.config, sizeof(storage::g_paths.config), "%s/config.bin",
                  preferences.c_str());
    std::snprintf(storage::g_paths.logs, sizeof(storage::g_paths.logs), "%s", preferences.c_str());

    // ---- the pretend network ----
    fake::Pc gaming;
    gaming.address = "192.168.4.20";
    gaming.name = "Gaming-PC";
    gaming.current_app = 1;
    gaming.pyrowave_profiles =
        SCM_PYROWAVE | SCM_PYROWAVE_444 | SCM_PYROWAVE_HDR10 | SCM_PYROWAVE_HDR10_444;
    static const char *const kNames[] = {"Desktop",       "Lumen Drift",  "Tidewater",
                                         "Glass Orchard", "Neon Harbor",  "Paper Kites",
                                         "Hollow Signal", "Copper Vale",  "Night Ferry",
                                         "Salt & Ember",  "Quiet Engine", "Caf\xC3\xA9 Mistral"};
    for (int i = 0; i < 12; ++i)
        gaming.apps.push_back({i + 1, kNames[i]});
    fake::Pc office;
    office.address = "192.168.4.31";
    office.port = 48989;
    office.name = "Office-PC";
    office.paired = false;
    fake::Pc living;
    living.address = "192.168.4.44";
    living.name = "Living-Room";
    living.online = false;
    living.discoverable = false;
    fake::world() = {gaming, office, living};

    const std::uint32_t confirm = hui::action_bit(Action::confirm);
    const std::uint32_t back = hui::action_bit(Action::back);
    const std::uint32_t next = hui::action_bit(Action::page_next);
    const std::uint32_t previous = hui::action_bit(Action::page_prev);
    const std::uint32_t options = hui::action_bit(Action::menu);
    const std::uint32_t triangle = hui::action_bit(Action::north);
    const std::uint32_t square = hui::action_bit(Action::west);

    std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * 4));
    stbi_flip_vertically_on_write(1);
    launcher::Frame frame;
    std::uint64_t now_ms = 1000;
    std::size_t cue_count = 0;
    std::size_t missing_cues = 0;

    // A pretend catalog: when set, the launcher is told a newer version exists.
    static int update_checks = 0;
    bool offer_update = false;
    // A pretend update helper: so many frames of download, then of unpacking.
    struct PretendUpdate
    {
        bool installable = false;
        bool notes = false;
        bool fails = false;
        bool begun = false;
        bool cancelled = false;
        bool applied = false;
        int begins = 0;
        int polls = 0;
    };
    static PretendUpdate pretend;
    static int update_exits = 0;

    // The picture a session that starts a stream hands to the stream.
    std::vector<unsigned char> plate;
    launcher::View::ConnectBar plate_bar;

    // Runs one launcher from start-up until its script ends; returns whether
    // it asked for a stream.
    const auto session = [&](const char *prefix, const std::vector<Step> &script,
                             const char *stream_error,
                             const moonlight_config_t *initial = nullptr) -> bool
    {
        launcher::Model model;
        model.set_artwork_decoder(make_poster);
        if (offer_update)
            model.set_update_check(
                [](launcher::UpdateOffer *offer)
                {
                    ++update_checks;
                    std::snprintf(offer->version, sizeof(offer->version), "01.000.090");
                    offer->installable = pretend.installable;
                    offer->size = 84000000;
                    if (pretend.notes)
                        offer->notes =
                            "Streaming\n- PyroWave, a GPU codec for hosts that offer it.\n"
                            "- Frame pacing: Unpaced, Paced and Paced+VRR.\n"
                            "- More of the DualSense reaches the game: adaptive triggers, "
                            "light bar, motion and touchpad contacts.\nUpdates\n"
                            "- ProsperoLight installs new releases by itself.\n"
                            "Note: this release must be installed by hand; later ones "
                            "arrive through this dialog.\n"
                            "The decoder still limits the usable bitrate at 4K, as the "
                            "Settings screen shows.";
                    offer->notes_truncated = pretend.notes;
                    return true;
                });
        model.Initialize(now_ms);
        if (initial)
        {
            model.settings() = *initial;
            model.SettingsChanged();
        }
        launcher::View view(model, fonts);
        view.set_version("01.000.080");
        view.set_players(2);
        view.set_storage({true, "/data/prosperolight/config", "/data/prosperolight/pairing",
                          "/data/prosperolight/logs"});
        view.show_stream_error(stream_error);
        launcher::UpdateActions actions;
        actions.begin = []
        {
            pretend.begun = true;
            pretend.cancelled = pretend.applied = false;
            pretend.polls = 0;
            ++pretend.begins;
            return true;
        };
        actions.poll = [](launcher::UpdateProgress *progress)
        {
            *progress = launcher::UpdateProgress{};
            if (!pretend.begun)
                return;
            const int at = ++pretend.polls;
            if (pretend.cancelled)
            {
                progress->phase = launcher::UpdatePhase::cancelled;
            }
            else if (at < 20)
            {
                progress->phase = launcher::UpdatePhase::starting;
            }
            else if (pretend.fails && at > 70)
            {
                progress->phase = launcher::UpdatePhase::failed;
                std::snprintf(progress->error, sizeof(progress->error),
                              "The download doesn't match the catalog's listing");
            }
            else if (at < 140)
            {
                progress->phase = launcher::UpdatePhase::downloading;
                progress->total = 84000000;
                progress->done = static_cast<std::uint64_t>(at - 20) * 700000;
                std::snprintf(progress->time_left, sizeof(progress->time_left), "about 20 s left");
            }
            else if (at < 200)
            {
                progress->phase = launcher::UpdatePhase::unpacking;
                progress->total = 190000000;
                progress->done = static_cast<std::uint64_t>(at - 140) * 3166666;
            }
            else
            {
                progress->phase = launcher::UpdatePhase::ready;
            }
        };
        actions.cancel = [] { pretend.cancelled = true; };
        actions.apply = []
        {
            pretend.applied = true;
            return true;
        };
        actions.finish = [] { pretend.begun = false; };
        view.set_update_actions(actions);
        bool started = false;
        const auto step_frame = [&](const hui::InputFrame &input)
        {
            now_ms += 17;
            model.Poll(now_ms);
            launcher::ArtworkImage image;
            while (model.TakeArtwork(&image))
            {
                const std::uint32_t texture =
                    renderer.batch().create_texture(image.width, image.height, image.rgba.data());
                view.set_artwork(
                    image.app_id, texture, static_cast<float>(image.width) / image.height,
                    launcher::View::palette_of(image.rgba.data(), image.width, image.height));
            }
            for (std::uint32_t texture : view.take_released_textures())
                glDeleteTextures(1, &texture);
            hui::ui::Feedback feedback;
            view.update(input, 1.0f / 60.0f, feedback);
            for (const hui::audio::CueEvent &event : feedback.cues)
            {
                ++cue_count;
                if (!sounds.has_recording(event.cue, hui::audio::SoundSet::glass))
                {
                    ++missing_cues;
                    std::fprintf(stderr, "no glass recording for cue %s\n",
                                 hui::audio::cue_name(event.cue));
                }
            }
            started = view.take_start_stream() || started;
            if (view.take_update_exit())
                ++update_exits;
            usleep(400); // lets the worker thread answer between frames
        };
        const auto render = [&](const std::string &name)
        {
            frame.reset();
            frame.glass_texture = renderer.glass_texture();
            view.draw(frame);
            if (std::getenv("PROSPEROLIGHT_LAYOUT_CHECK") && name.find("row-") != std::string::npos)
                return;
            renderer.begin();
            renderer.backdrop(frame.backdrop);
            renderer.draw(frame.scene);
            renderer.glass();
            renderer.draw(frame.overlay);
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            renderer.present(framebuffer, width, height);
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            for (std::size_t i = 3; i < pixels.size(); i += 4)
                pixels[i] = 255;
            const std::string path = output + "/" + prefix + "-" + name + ".png";
            stbi_write_png(path.c_str(), width, height, 4, pixels.data(), width * 4);
            std::fprintf(stderr, "wrote %s: %zu shapes, %zu draw calls, GL error 0x%x\n",
                         path.c_str(), renderer.last_instances(), renderer.last_draw_calls(),
                         glGetError());
        };
        hui::InputFrame idle;
        idle.connected = true;
        for (const Step &step : script)
        {
            if (step.before)
                step.before();
            hui::InputFrame held = idle;
            held.held = step.hold;
            for (int i = 0; i < step.frames && !started; ++i)
                step_frame(held);
            if (step.capture)
                render(step.capture);
            hui::InputFrame input = idle;
            input.pressed = step.press;
            input.held = step.press | step.hold;
            input.nav = step.nav;
            if (!started)
                step_frame(input);
        }
        if (started)
        {
            // What the console does at this moment: the connecting screen once
            // more, without the fill of its bar, read back as pixels.
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
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            renderer.present(framebuffer, width, height);
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            const std::size_t row = static_cast<std::size_t>(width) * 4;
            plate.resize(pixels.size());
            for (int y = 0; y < height; ++y)
                std::memcpy(plate.data() + static_cast<std::size_t>(y) * row,
                            pixels.data() + static_cast<std::size_t>(height - 1 - y) * row, row);
            plate_bar = view.connecting_bar();
        }
        model.Shutdown();
        return started;
    };

    if (std::getenv("PROSPEROLIGHT_LAYOUT_CHECK"))
    {
        fake::erase_saved_file();
        session("layout-discovery", {{90, 0}}, "");
        static std::set<std::string> violations;
        static std::string locale;
        hui::gfx::DrawList::text_audit = [](std::string_view value, const hui::gfx::Rect &bounds,
                                            const hui::gfx::Rect &clip, bool readable)
        {
            if (value.ends_with("...") || value.ends_with("\xE2\x80\xA6"))
                violations.insert(locale + " | truncated: " + std::string(value));
            if (readable || bounds.y + bounds.h <= clip.y || bounds.y >= clip.y + clip.h)
                return;
            if (bounds.x < clip.x - 2.0f || bounds.x + bounds.w > clip.x + clip.w + 2.0f)
            {
                char geometry[128];
                std::snprintf(geometry, sizeof(geometry), " | x=%.1f width=%.1f lane=%.1f..%.1f",
                              bounds.x, bounds.w, clip.x, clip.x + clip.w);
                violations.insert(locale + " | " + std::string(value) + geometry);
            }
        };
        for (unsigned lang = 0; lang < std::size(i18n::languages); ++lang)
        {
            locale = i18n::languages[lang].code;
            expect(i18n::select(static_cast<int>(lang) + 1), "layout locale selection");
            std::vector<Step> script = {{30, options},
                                        {30, 0, Direction::none, "settings"},
                                        {10, confirm},
                                        {30, 0, Direction::none, "languages"},
                                        {10, back}};
            std::array<std::string, 32> captures;
            for (int row = 0; row < 32; ++row)
            {
                captures[row] = "row-" + std::to_string(row);
                script.push_back({10, 0, Direction::down});
                script.push_back({20, 0, Direction::none, captures[row].c_str()});
            }
            session(("layout-" + locale).c_str(), script, "");
            session(("layout-pcs-" + locale).c_str(),
                    {{90, 0, Direction::none, "pcs"},
                     {20, 0, Direction::right},
                     {20, confirm, Direction::none, "actions"},
                     {20, back},
                     {20, 0, Direction::right},
                     {20, confirm, Direction::none, "dialog"},
                     {20, back}},
                    "");
            session(("layout-error-" + locale).c_str(), {{80, 0, Direction::none, "error"}},
                    i18n::tr("Could not connect to Sunshine"));
            session(("layout-about-" + locale).c_str(),
                    {{30, previous}, {60, 0, Direction::none, "page"}}, "");
        }
        hui::gfx::DrawList::text_audit = nullptr;
        for (const auto &violation : violations)
            std::fprintf(stderr, "TEXT_OVERFLOW: %s\n", violation.c_str());
        (void)i18n::select(0);
        expect(violations.empty(), "all locales fit screen/clips or use readable scrolling");
        return failures ? 1 : 0;
    }

    // ---- first start: nothing saved, the network is searched ----
    fake::erase_saved_file();
    session("first",
            {
                {2, 0, Direction::none, "searching"},
                {90, 0, Direction::none, "found"},
            },
            "");
    {
        moonlight_config_t saved;
        expect(moonlight_config_load(&saved) && saved.host_count == 2,
               "the two PCs that answer the search are saved");
    }

    // ---- the PCs screen with three saved PCs ----
    {
        moonlight_config_t saved;
        (void)moonlight_config_load(&saved);
        (void)moonlight_config_upsert_host(&saved, "192.168.4.44", 0, "Living-Room", "", true);
        saved.selected_host = 0;
        saved.stream_resolution = MOONLIGHT_STREAM_RESOLUTION_2160P;
        saved.stream_fps = MOONLIGHT_STREAM_FPS_120;
        saved.video_codec = MOONLIGHT_VIDEO_CODEC_HEVC;
        saved.hdr_enabled = 1;
        saved.bitrate_mbps = 80;
        saved.display_area = MOONLIGHT_DISPLAY_AREA_FULL;
        (void)moonlight_config_save(&saved);
    }
    session("pcs",
            {
                {90, 0, Direction::right, "list"},
                {20, 0, Direction::right},
                {20, confirm, Direction::none, "actions"},
                {60, back, Direction::none, "port"},
                {20, 0, Direction::right},
                {20, confirm},
                {50, 0, Direction::none, "unpair-dialog"},
                {10, back},
                {20, 0, Direction::right},
                {20, confirm},
                {40, 0, Direction::none, "remove-hold", confirm},
                {20, 0, Direction::left},
                {20, 0, Direction::left},
                {20, 0, Direction::left},
                {20, 0, Direction::left},
                {20, 0, Direction::down},
                {40, 0, Direction::none, "not-paired"},
                {10, confirm},
                {60, 0, Direction::none, "pairing"},
                {10, 0, Direction::none, nullptr, 0, [] { fake::finish_pairing(true); }},
                {60, 0, Direction::none, "paired"},
                {10, 0, Direction::down},
                {60, 0, Direction::none, "offline"},
                {10, 0, Direction::down},
                {20, confirm, Direction::none, "add-row"},
                {60, back, Direction::none, "add-prompt"},
            },
            "");
    {
        moonlight_config_t saved;
        (void)moonlight_config_load(&saved);
        expect(saved.host_count == 3, "three PCs stay saved");
        expect(fake::world()[1].paired, "the PIN pairs Office-PC");
    }

    // ---- Games: posters, stopping the running app, starting a stream ----
    {
        moonlight_config_t saved;
        (void)moonlight_config_load(&saved);
        saved.selected_host = 0;
        (void)moonlight_config_save(&saved);
    }
    const bool started = session("games",
                                 {
                                     {30, next},
                                     {90, 0, Direction::right, "running"},
                                     {15, 0, Direction::right},
                                     {15, 0, Direction::right},
                                     {70, 0, Direction::down, "focus"},
                                     {60, 0, Direction::up, "second-row"},
                                     {20, square},
                                     {60, 0, Direction::none, "stopped"},
                                     {10, confirm},
                                     {30, 0, Direction::none, "connecting"},
                                     {120, 0},
                                 },
                                 "");
    expect(started, "Cross on an app starts the stream");

    // ---- the stream's side of the connecting screen ----
    // The picture becomes a video frame; the stream draws the bar on it.
    expect(width == connecting::kWidth && height == connecting::kHeight &&
               plate.size() == static_cast<std::size_t>(width) * height * 4,
           "the connecting screen is handed over as a full-resolution 4K picture");
    expect(plate_bar.progress > 0.29f && plate_bar.progress < 0.31f,
           "the launcher's bar stops at 30%, where the stream takes over");
    if (failures == 0)
    {
        connecting::Bar bar;
        bar.x = plate_bar.rect.x * 2;
        bar.y = plate_bar.rect.y * 2;
        bar.width = plate_bar.rect.w * 2;
        bar.height = plate_bar.rect.h * 2;
        bar.fill[0] = static_cast<std::uint8_t>(plate_bar.fill.r * 255.0f + 0.5f);
        bar.fill[1] = static_cast<std::uint8_t>(plate_bar.fill.g * 255.0f + 0.5f);
        bar.fill[2] = static_cast<std::uint8_t>(plate_bar.fill.b * 255.0f + 0.5f);
        connecting::Plate video;
        video.build(plate.data(), bar, false);
        std::vector<unsigned char> surface(connecting::surface_bytes(false));
        const std::size_t luma =
            static_cast<std::size_t>(connecting::kWidth) * connecting::kSurfaceHeight;
        // The frame as the television shows it: BT.709 video back to pixels.
        const auto save = [&](const char *name, float progress, float brightness)
        {
            video.compose(surface.data(), progress, brightness);
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    const std::size_t at = static_cast<std::size_t>(y) * width + x;
                    const std::size_t pair =
                        luma + static_cast<std::size_t>(y / 2) * width + (x & ~1);
                    const float l = (surface[at] - 16.0f) / 219.0f;
                    const float cb = (surface[pair] - 128.0f) / 224.0f;
                    const float cr = (surface[pair + 1] - 128.0f) / 224.0f;
                    const float r = l + 1.5748f * cr;
                    const float b = l + 1.8556f * cb;
                    const float g = (l - 0.2126f * r - 0.0722f * b) / 0.7152f;
                    unsigned char *out = pixels.data() + at * 4;
                    out[0] = static_cast<unsigned char>(std::clamp(r, 0.0f, 1.0f) * 255.0f + 0.5f);
                    out[1] = static_cast<unsigned char>(std::clamp(g, 0.0f, 1.0f) * 255.0f + 0.5f);
                    out[2] = static_cast<unsigned char>(std::clamp(b, 0.0f, 1.0f) * 255.0f + 0.5f);
                    out[3] = 255;
                }
            }
            const std::string path = output + "/stream-" + name + ".png";
            stbi_flip_vertically_on_write(0);
            stbi_write_png(path.c_str(), width, height, 4, pixels.data(), width * 4);
            stbi_flip_vertically_on_write(1);
            std::fprintf(stderr, "wrote %s\n", path.c_str());
        };
        const int middle = static_cast<int>(bar.y + bar.height * 0.5f);
        const auto bar_luma = [&](float share)
        {
            return surface[static_cast<std::size_t>(middle) * width +
                           static_cast<int>(bar.x + bar.width * share)];
        };
        save("connecting", 0.65f, 1.0f);
        const int filled = bar_luma(0.3f);
        const int empty = bar_luma(0.9f);
        expect(filled > empty + 40, "the stream fills the bar as far as the connection is");
        save("first-picture", 1.0f, 1.0f);
        expect(bar_luma(0.9f) == filled, "the bar is full when the first picture is ready");
        save("fading", 1.0f, 0.4f);
        video.compose(surface.data(), 1.0f, 0.0f);
        bool black = true;
        for (std::size_t at = 0; at < surface.size(); ++at)
            black = black && surface[at] == (at < luma ? 16 : 128);
        expect(black, "the connecting screen fades to black before the stream appears");
    }
    expect(fake::world()[0].current_app == 0, "Square stops the running app");

    // ---- back from a stream that failed ----
    session("games", {{80, 0, Direction::none, "stream-error"}}, "Sunshine closed the connection");

    // Language is a modal, scrolling Select rather than a cycling choice.
    session("language",
            {{30, options},
             {10, confirm},
             {30, 0, Direction::none, "dropdown"},
             {10, 0, Direction::down},
             {10, back},
             {20, 0}},
            "");
    expect(i18n::selected() == 0, "closing the language dropdown preserves Automatic");

    // ---- Settings: the frame-rate presets and the bitrate slider, beside the new rows ----
    (void)prosperolight::host_quit_set_enabled(false);
    moonlight_config_t initial_settings{};
    (void)moonlight_config_load(&initial_settings);
    initial_settings.video_codec = MOONLIGHT_VIDEO_CODEC_PYROWAVE;
    initial_settings.hdr_enabled = 1;
    initial_settings.chroma_sampling = MOONLIGHT_CHROMA_444;
    initial_settings.stream_fps = MOONLIGHT_STREAM_FPS_120;
    initial_settings.bitrate_mbps = 600;
    session("settings",
            {
                {30, options},
                {10, 0, Direction::down},
                {30, 0, Direction::none, "pyrowave-hdr"},
                {10, 0, Direction::down},
                {20, 0, Direction::left},
                {40, 0, Direction::none, "fps-preset"},
                {10, 0, Direction::down},
                {20, 0, Direction::right},
                {30, 0, Direction::none, "h264"},
                {10, 0, Direction::down},
                {10, 0, Direction::down},
                {10, 0, Direction::down},
                {20, 0, Direction::left},
                {40, 0, Direction::none, "bitrate-slider"},
                {10, 0, Direction::down},
                {10, 0, Direction::down},
                {10, 0, Direction::down},
                {10, confirm},
                {40, 0, Direction::none, "host-quit-on"},
                {10, 0, Direction::down},
                {10, 0, Direction::down},
                {10, 0, Direction::down},
                {10, 0, Direction::right},
                {10, 0},
                {10, 0, Direction::right},
                {40, 0, Direction::none, "paced-vrr"},
                {10, 0, Direction::down},
                {10, 0, Direction::down},
                {10, 0, Direction::down},
                {10, confirm},
                {40, 0, Direction::none, "logs-off"},
                {120, 0},
            },
            "", &initial_settings);
    {
        moonlight_config_t saved{};
        (void)moonlight_config_load(&saved);
        expect(saved.bitrate_mbps == 590 && saved.stream_fps == MOONLIGHT_STREAM_FPS_90,
               "the presets and the slider save frame rate and bitrate independently");
        expect(saved.video_codec == MOONLIGHT_VIDEO_CODEC_H264 && saved.hdr_enabled == 0,
               "choosing H.264 turns HDR off");
        expect(moonlight::presentation_mode() == 2u, "the new pacing row persists Paced+VRR");
        expect(prosperolight::host_quit_enabled(), "host app quit toggle persists");
        expect(!prosperolight_logs_enabled(), "the new diagnostics row persists logs off");
    }

    // ---- a newer version in the catalog: one notice, for ten seconds ----
    offer_update = true;
    session("update",
            {
                {150, 0, Direction::none, "notice"},
                {420, 0, Direction::none, "notice-after-nine-seconds"},
                {120, 0, Direction::none, "notice-gone"},
            },
            "");
    expect(update_checks == 1, "the catalog is asked once per launch");

    // ---- an update the app can install: asked, downloaded, unpacked, then the app closes ----
    pretend.installable = true;
    session("self-update",
            {
                {150, 0, Direction::none, "offer"},
                {10, confirm},
                {80, 0, Direction::none, "downloading"},
                {100, 0, Direction::none, "unpacking"},
                {70, 0, Direction::none, "closing"},
                {120, 0},
            },
            "");
    expect(pretend.begins == 1 && pretend.applied, "Update now downloads, stages and applies");
    expect(update_exits == 1, "a staged update asks the app to close, once");

    // Skip keeps the version that runs; nothing is started.
    pretend = PretendUpdate{};
    pretend.installable = true;
    session("self-update", {{150, 0}, {10, 0, Direction::left}, {10, confirm}, {60, 0}}, "");
    expect(pretend.begins == 0 && update_exits == 1, "Skip starts nothing");

    // Cancelled during the download: the launcher is back, nothing is applied.
    pretend = PretendUpdate{};
    pretend.installable = true;
    session("self-update",
            {{150, 0}, {10, confirm}, {60, 0}, {10, back}, {60, 0, Direction::none, "cancelled"}},
            "");
    expect(pretend.begins == 1 && pretend.cancelled && !pretend.applied && update_exits == 1,
           "cancelling leaves the app as it was");

    // A refused download says why and offers another try.
    pretend = PretendUpdate{};
    pretend.installable = true;
    pretend.fails = true;
    session("self-update",
            {{150, 0},
             {10, confirm},
             {120, 0, Direction::none, "failed"},
             {10, 0, Direction::left},
             {10, confirm},
             {60, 0}},
            "");
    expect(pretend.begins == 1 && !pretend.applied && update_exits == 1,
           "a failed update changes nothing and closes on request");
    // A release with notes: What's new shows them, Back returns to the question, Cross updates.
    pretend = PretendUpdate{};
    pretend.installable = pretend.notes = true;
    session("self-update",
            {{150, 0, Direction::none, "offer-with-notes"},
             {10, 0, Direction::left},
             {10, confirm},
             {60, 0, Direction::none, "notes"},
             {10, 0, Direction::down},
             {40, 0, Direction::none, "notes-scrolled"},
             {10, back},
             {50, 0, Direction::none, "offer-after-notes"},
             {10, confirm},
             {60, 0, Direction::none, "notes-again"},
             {10, confirm},
             {40, 0}},
            "");
    expect(pretend.begins == 1,
           "What's new can be read, left and come back to, and Cross updates from it");
    pretend = PretendUpdate{};
    offer_update = false;

    // ---- About: one step back from the first tab ----
    session("about", {{30, previous}, {60, 0, Direction::none, "page"}, {10, back}, {30, 0}}, "");

    // ---- a PC that stops answering, and one without apps ----
    fake::world()[0].online = false;
    session("games", {{30, next}, {200, triangle, Direction::none, "pc-offline"}}, "");
    fake::world()[0].online = true;
    fake::world()[0].apps.clear();
    session("games", {{30, next}, {90, 0, Direction::none, "no-apps"}}, "");

    expect(missing_cues == 0, "every cue the widgets play has a recording");
    std::fprintf(stderr, "cues played: %zu, missing: %zu, failures: %d\n", cue_count, missing_cues,
                 failures);
    return failures == 0 ? 0 : 1;
}
