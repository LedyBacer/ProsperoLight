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
#include <cstdio>
#include <cstdlib>
#include <string>
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
    if (!hui::save::read_file(path, &data) || !font->load(data))
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
        {0x2a0f4f, 0x8a2f8f, 0xff5fa2}, {0x0f3d3a, 0x2fa59a, 0x86f0dc}, {0x1d2e12, 0x4f7a28, 0xcfe07a},
        {0x08142e, 0x1f5fb0, 0x35e0ff}, {0x3a1c10, 0xb0683a, 0xffd9a0}, {0x101820, 0x3a4a52, 0xf2b84b},
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
    const std::string output = argv[2];
    const int width = argc > 4 ? std::atoi(argv[3]) : 1920;
    const int height = argc > 4 ? std::atoi(argv[4]) : 1080;
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

    // ---- the pretend network ----
    fake::Pc gaming;
    gaming.address = "192.168.4.20";
    gaming.name = "Gaming-PC";
    gaming.current_app = 1;
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
    const std::uint32_t triangle = hui::action_bit(Action::north);
    const std::uint32_t square = hui::action_bit(Action::west);

    std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * 4));
    stbi_flip_vertically_on_write(1);
    launcher::Frame frame;
    std::uint64_t now_ms = 1000;
    std::size_t cue_count = 0;
    std::size_t missing_cues = 0;

    // Runs one launcher from start-up until its script ends; returns whether
    // it asked for a stream.
    const auto session = [&](const char *prefix, const std::vector<Step> &script,
                             const char *stream_error) -> bool
    {
        launcher::Model model;
        model.set_artwork_decoder(make_poster);
        model.Initialize(now_ms);
        launcher::View view(model, fonts);
        view.set_version("01.000.080");
        view.set_players(2);
        view.show_stream_error(stream_error);
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
            usleep(400); // lets the worker thread answer between frames
        };
        const auto render = [&](const std::string &name)
        {
            frame.reset();
            frame.glass_texture = renderer.glass_texture();
            view.draw(frame);
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
        model.Shutdown();
        return started;
    };

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
    const bool started = session(
        "games",
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
    expect(fake::world()[0].current_app == 0, "Square stops the running app");

    // ---- back from a stream that failed ----
    session("games", {{80, 0, Direction::none, "stream-error"}}, "Sunshine closed the connection");

    // ---- Settings ----
    session("settings",
            {
                {30, previous},
                {60, 0, Direction::down, "video"},
                {10, 0, Direction::down},
                {10, 0, Direction::down},
                {10, 0, Direction::down},
                {10, 0, Direction::right},
                {10, 0, Direction::right},
                {10, 0, Direction::right},
                {10, 0, Direction::right},
                {60, 0, Direction::up, "above-limit"},
                {10, 0, Direction::up},
                {10, 0, Direction::left},
                {60, 0, Direction::none, "h264"},
                {120, 0},
            },
            "");
    {
        moonlight_config_t saved;
        (void)moonlight_config_load(&saved);
        expect(saved.bitrate_mbps == 120, "four steps of the slider save 120 Mbps");
        expect(saved.video_codec == MOONLIGHT_VIDEO_CODEC_H264 && saved.hdr_enabled == 0,
               "choosing H.264 turns HDR off");
    }

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
