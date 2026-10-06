/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "i18n.hpp"
#include "launcher/launcher_view.hpp"

#include "host_quit_preferences.hpp"
#include "client_preferences.hpp"
#include "lan_http_report.hpp"
#include "presentation_preferences.hpp"
#include "stream_profile.hpp"
#include "ui/widgets.hpp"
#include "ui_sound_preferences.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace launcher
{

namespace
{

using namespace hui;
using gfx::Color;
using gfx::Rect;

constexpr float kMargin = 96.0f;
constexpr float kRight = gfx::kVirtualWidth - kMargin;
constexpr float kContentTop = 262.0f;
constexpr Rect kHostPanel{816.0f, kContentTop, kRight - 816.0f, 544.0f};
constexpr Rect kProfilePanel{1176.0f, kContentTop, kRight - 1176.0f, 436.0f};
constexpr Rect kShortcutPanel{1176.0f, kContentTop + 456.0f, kRight - 1176.0f, 252.0f};
constexpr Rect kCreditsPanel{kMargin, kContentTop, 852.0f, 708.0f};
constexpr Rect kStartPanel{kMargin + 876.0f, kContentTop, 852.0f, 708.0f};
constexpr int kScreens = 4;
constexpr Rect kPairPanel{960.0f - 440.0f, 250.0f, 880.0f, 560.0f};
constexpr Rect kUpdatePanel{960.0f - 440.0f, 230.0f, 880.0f, 600.0f};
constexpr Rect kNotesPanel{960.0f - 560.0f, 120.0f, 1120.0f, 840.0f};
constexpr int kColumns = 7;
// How long the connecting screen stays before the stream takes the display,
// and how far its bar gets meanwhile. The stream carries the bar on from there.
constexpr float kLaunchSeconds = 1.0f;
constexpr float kHandoverProgress = 0.3f;
constexpr Rect kConnectBar{kMargin, 974.0f, kRight - kMargin, 10.0f};

// Measured on the console for 4K HEVC: smooth up to / freezes above, in Mbps.
struct BitrateLimit
{
    float smooth;
    float freezes;
};

BitrateLimit limit_for(unsigned fps)
{
    if (fps >= MOONLIGHT_STREAM_FPS_120)
        return {80.0f, 100.0f};
    if (fps >= MOONLIGHT_STREAM_FPS_90)
        return {115.0f, 145.0f};
    return {115.0f, 190.0f};
}

enum FormId
{
    kResolution = 1,
    kFrameRate,
    kCodec,
    kHdr,
    kBitrate,
    kAudio,
    kArea,
    kVsync,
    kPipeline,
    kCores,
    kChroma,
    kPacing,
    kLogging,
    kUiSound,
    kHostQuit,
    kLanguage,
    kCustomFps,
    kOptimizeHost,
    kMuteHost,
};

constexpr float kBitrateStep = 10.0f;
constexpr float kBitrateMax = 300.0f;
// PyroWave works in the hundreds of Mbps: the slider reaches further for it.
constexpr float kPyroWaveBitrateMax = 1000.0f;

ui::Theme theme_by_id(const char *id)
{
    for (const ui::Theme &theme : ui::themes())
    {
        if (std::string_view(theme.id) == id)
            return theme;
    }
    return ui::themes()[0];
}

void format_endpoint(char *output, std::size_t capacity, const moonlight_config_host_t &host)
{
    std::snprintf(output, capacity, "%s:%u", host.address, moonlight_config_host_port(&host));
}

const char *resolution_name(unsigned resolution)
{
    return resolution == MOONLIGHT_STREAM_RESOLUTION_2160P   ? "3840 \xC3\x97 2160"
           : resolution == MOONLIGHT_STREAM_RESOLUTION_1440P ? "2560 \xC3\x97 1440"
                                                             : "1920 \xC3\x97 1080";
}

const char *codec_name(const moonlight_config_t &config)
{
    if (config.video_codec == MOONLIGHT_VIDEO_CODEC_PYROWAVE)
        return config.hdr_enabled ? "PyroWave HDR" : "PyroWave SDR";
    return config.hdr_enabled                                 ? "HEVC Main10 HDR"
           : config.video_codec == MOONLIGHT_VIDEO_CODEC_HEVC ? "HEVC"
                                                              : "H.264";
}

ui::StatusKind toast_kind(NoticeKind kind)
{
    switch (kind)
    {
    case NoticeKind::success:
        return ui::StatusKind::success;
    case NoticeKind::warning:
        return ui::StatusKind::warning;
    case NoticeKind::error:
        return ui::StatusKind::danger;
    default:
        return ui::StatusKind::info;
    }
}

// A number pad that also types an address: digits, a dot and a colon.
ui::KeyboardLayout address_layout()
{
    ui::KeyboardLayout layout;
    layout.name = "123";
    layout.columns = 3;
    layout.add_row("123").add_row("456").add_row("789").add_row(".0:");
    ui::KeyboardKey erase;
    erase.kind = ui::KeyKind::backspace;
    ui::KeyboardKey done;
    done.kind = ui::KeyKind::done;
    done.span = 2;
    layout.add_row({erase, done});
    return layout;
}

} // namespace

ui::Theme moonlight_theme()
{
    ui::Theme t = theme_by_id("acrylic");
    t.id = "moonlight";
    t.name = "Moonlight";
    t.family = i18n::tr("Frosted glass");
    t.summary = i18n::tr("ProsperoLight's night blue under frosted panels");
    t.backdrop.colors[0] = Color::rgb(0x030816);
    t.backdrop.colors[1] = Color::rgb(0x0a1a4a);
    t.backdrop.colors[2] = Color::rgb(0x1f5fd0);
    t.backdrop.colors[3] = Color::rgb(0x1fa6b8);
    t.page = Color::rgb(0x081029);
    t.surface = Color::rgb(0x0e1a42, 0.5f);
    t.primary = Color::rgb(0x8fd3ff);
    t.on_primary = Color::rgb(0x03203a);
    t.accent = Color::rgb(0x8fd3ff);
    t.success = Color::rgb(0x5fe0a6);
    t.warning = Color::rgb(0xf6c86b);
    t.danger = Color::rgb(0xff6b7a);
    t.sounds = audio::SoundSet::glass;
    return t;
}

View::View(Model &model, const ui::Fonts &fonts)
    : model_(model), fonts_(fonts), theme_(moonlight_theme())
{
    build();
    restyle();
    sync();
    // With one saved PC the player wants its games, not the list of PCs.
    if (model_.config().host_count == 1)
        show(1, nullptr);
}

void View::set_players(int count)
{
    for (int index = 0; index < ui::StatusBar::kPlayers; ++index)
        status_.set_player(index, index < count);
}

void View::set_storage(const Storage &storage)
{
    storage_ = storage;
    storage_access_ = storage.access;
    files_.set_items({{i18n::tr("Settings"), storage.settings},
                      {i18n::tr("Pairing"), storage.pairing},
                      {i18n::tr("Logs"), storage.logs}});
}

void View::show_stream_error(const char *message)
{
    if (message && message[0])
        pending_error_ = message;
}

void View::set_artwork(int app_id, std::uint32_t texture, float aspect, const Palette &palette)
{
    bool known = false;
    for (Artwork &art : artwork_)
    {
        if (art.app_id == app_id)
        {
            released_.push_back(art.texture);
            art = {app_id, texture, aspect, palette};
            known = true;
        }
    }
    if (!known)
        artwork_.push_back({app_id, texture, aspect, palette});
    // The card gets its picture where it stands: the grid is not rebuilt.
    for (int index = 0; index < static_cast<int>(apps_.items().size()); ++index)
    {
        ui::CardItem &item = apps_.item(index);
        if (item.tag == app_id)
        {
            item.texture = texture;
            item.image_aspect = aspect;
            item.accent = palette.accent;
        }
    }
    apply_ambient(false);
}

// The poster's average colour, a darker tone of it, and its most vivid colour.
Palette View::palette_of(const unsigned char *rgba, int width, int height)
{
    double sum[3] = {0.0, 0.0, 0.0};
    double vivid[3] = {0.0, 0.0, 0.0};
    double vivid_weight = 0.0;
    const int step = std::max(1, std::min(width, height) / 48);
    int count = 0;
    for (int y = 0; y < height; y += step)
    {
        for (int x = 0; x < width; x += step)
        {
            const unsigned char *p = rgba + (static_cast<std::size_t>(y) * width + x) * 4;
            const double r = p[0] / 255.0;
            const double g = p[1] / 255.0;
            const double b = p[2] / 255.0;
            const double high = std::max(r, std::max(g, b));
            const double low = std::min(r, std::min(g, b));
            const double weight = (high - low) * high;
            sum[0] += r;
            sum[1] += g;
            sum[2] += b;
            vivid[0] += r * weight;
            vivid[1] += g * weight;
            vivid[2] += b * weight;
            vivid_weight += weight;
            ++count;
        }
    }
    Palette palette;
    if (count == 0)
        return palette;
    const Color mid{static_cast<float>(sum[0] / count), static_cast<float>(sum[1] / count),
                    static_cast<float>(sum[2] / count), 1.0f};
    palette.mid = mid;
    palette.dark = gfx::mix(mid, Color::rgb(0x000000), 0.6f);
    palette.accent = vivid_weight > 1e-4 ? Color{static_cast<float>(vivid[0] / vivid_weight),
                                                 static_cast<float>(vivid[1] / vivid_weight),
                                                 static_cast<float>(vivid[2] / vivid_weight), 1.0f}
                                         : gfx::mix(mid, Color::rgb(0xffffff), 0.4f);
    return palette;
}

std::vector<std::uint32_t> View::take_released_textures()
{
    std::vector<std::uint32_t> taken;
    taken.swap(released_);
    return taken;
}

const View::Artwork *View::artwork(int app_id) const
{
    for (const Artwork &art : artwork_)
    {
        if (art.app_id == app_id)
            return &art;
    }
    return nullptr;
}

bool View::take_start_stream()
{
    const bool start = start_stream_;
    start_stream_ = false;
    return start;
}

// ---- building ----------------------------------------------------------

void View::build()
{
    form_.clear();
    tabs_.set_tabs(
        {{i18n::tr("PCs")}, {i18n::tr("Games")}, {i18n::tr("Settings")}, {i18n::tr("About")}});
    tabs_.style.kind = ui::TabKind::pill;
    tabs_.style.width = ui::TabWidth::fill;
    tabs_.style.on_page = true;
    tabs_.style.height = 52.0f;
    tabs_.style.text_size = 25.0f;
    tabs_.style.padding = 18.0f;
    tabs_.set_bounds({470.0f, 62.0f, 840.0f, 52.0f});
    tabs_.set_active(0, true);
    tabs_.set_focused(false);

    status_.style.panel = false;
    status_.style.show_battery = false;
    status_.style.show_signal = false;
    status_.style.show_clock = false;
    status_.style.padding = 0.0f;
    status_.set_player(0, true);
    status_.set_bounds({kRight - 160.0f, 62.0f, 160.0f, 52.0f});

    profile_.style.align = gfx::Align::right;
    profile_.style.height = 38.0f;
    profile_.style.text_size = 19.0f;
    profile_.set_bounds({kRight - 500.0f, 69.0f, 320.0f, 38.0f});

    // ---- PCs ----
    hosts_.style.cards = true;
    hosts_.style.row_height = 124.0f;
    hosts_.style.gap = 16.0f;
    hosts_.style.padding = 26.0f;
    hosts_.style.leading_width = 80.0f;
    hosts_.style.title_size = 31.0f;
    hosts_.style.subtitle_size = 22.0f;
    hosts_.style.highlight.kind = ui::HighlightKind::ring;
    hosts_.style.focus_shift = 0.0f;
    hosts_.set_bounds({kMargin, kContentTop, 680.0f, 700.0f});
    hosts_.leading = [this](ui::Canvas &canvas, const Rect &row, const ui::ListItem &, int index,
                            float focus) { draw_host_icon(canvas, row, index, focus); };
    hosts_.trailing = [this](ui::Canvas &canvas, const Rect &row, const ui::ListItem &, int index,
                             float) { draw_host_state(canvas, row, index); };

    host_details_.style.panel = false;
    host_details_.style.row_height = 46.0f;
    host_details_.style.label_size = 21.0f;
    host_details_.style.value_size = 23.0f;
    host_details_.style.padding = 4.0f;
    host_details_.style.label_share = 0.34f;

    open_games_.style.role = ui::ButtonRole::primary;
    open_games_.glyph = ui::Button::cross;
    open_games_.style.justify = ui::ButtonJustify::between;

    host_actions_.set_items({{i18n::tr("Change port")}, {i18n::tr("Unpair")}});
    host_actions_.style.exits.left = true;
    host_actions_.style.exits.right = true;

    remove_.label = i18n::tr("Remove");
    remove_.hint = i18n::tr("Hold");
    remove_.style.variant = ui::HoldVariant::fill;
    remove_.style.role = ui::ButtonRole::secondary;
    remove_.style.size = ui::ButtonSize::medium;

    empty_.title = i18n::tr("No PCs yet");
    empty_.body = i18n::tr("Start Sunshine on a PC on this network and it appears here, "
                           "or add it by its "
                           "address.");
    empty_.action = i18n::tr("Add a PC");
    empty_.style.max_text_width = 620.0f;
    empty_.style.title_size = 36.0f;
    empty_.style.body_size = 24.0f;
    empty_.set_bounds({460.0f, 300.0f, 1000.0f, 440.0f});
    searching_.style.kind = ui::SpinnerKind::arc;
    searching_.set_bounds({kMargin, 992.0f, 34.0f, 34.0f});

    const Rect inside = host_inside();
    const float row = inside.y + inside.h - 64.0f;
    host_details_.set_bounds({inside.x, inside.y + 78.0f, inside.w, 300.0f});
    open_games_.set_bounds({inside.x, row, 252.0f, 64.0f});
    host_actions_.set_bounds({inside.x + 264.0f, row, 430.0f, 64.0f});
    remove_.set_bounds({inside.x + inside.w - 226.0f, row, 226.0f, 64.0f});

    // ---- Games ----
    apps_.style.columns = kColumns;
    apps_.style.gap_x = 26.0f;
    apps_.style.gap_y = 30.0f;
    apps_.style.card.art_aspect = 2.0f / 3.0f;
    apps_.style.card.text = ui::CardText::below;
    apps_.style.card.title_size = 23.0f;
    apps_.style.card.subtitle_size = 0.0f;
    apps_.style.card.glow = true;
    apps_.style.card.badge_size = 15.0f;
    apps_.style.exits.up = true;
    apps_.style.scroll_thumb = false;
    apps_.set_bounds({kMargin - 14.0f, 528.0f, kRight - kMargin + 28.0f, 426.0f});
    apps_.art = [this](ui::Canvas &canvas, const Rect &art, float radius, const ui::CardItem &item,
                       float) { draw_app_art(canvas, art, radius, item); };

    start_.style.role = ui::ButtonRole::primary;
    start_.glyph = ui::Button::cross;
    start_.style.size = ui::ButtonSize::large;
    start_.set_bounds({kMargin, 374.0f, 330.0f, 72.0f});
    stop_.label = i18n::tr("Stop app");
    stop_.glyph = ui::Button::square;
    stop_.style.role = ui::ButtonRole::secondary;
    stop_.style.size = ui::ButtonSize::large;
    stop_.set_bounds({kMargin + 350.0f, 374.0f, 250.0f, 72.0f});
    shelf_.title = i18n::tr("Apps on this PC");
    shelf_.style.rule = ui::SectionRule::trailing;
    shelf_.set_bounds({kMargin, 494.0f, kRight - kMargin, 40.0f});
    no_apps_.style.max_text_width = 640.0f;
    no_apps_.style.title_size = 36.0f;
    no_apps_.style.body_size = 24.0f;
    no_apps_.set_bounds({460.0f, 300.0f, 1000.0f, 440.0f});
    loading_apps_.style.kind = ui::SpinnerKind::arc;
    loading_apps_.set_bounds({930.0f, 500.0f, 60.0f, 60.0f});

    language_.set_label(i18n::tr("Language"));
    std::vector<ui::SelectOption> language_options{{i18n::tr("Automatic (console language)")}};
    for (const auto &language : i18n::languages)
        language_options.emplace_back(i18n::tr(language.name));
    language_.set_options(std::move(language_options));
    language_.set_index(i18n::selected());
    language_.style.label = ui::SelectLabel::none;
    language_.style.max_rows = 7;
    language_.style.popover_width = 640.0f;
    language_.set_limits({kMargin, 160.0f, kRight - kMargin, 770.0f});
    // ---- Settings ----
    form_.add_action(kLanguage, i18n::tr("Language")).text =
        i18n::selected() == 0 ? i18n::tr("Automatic (console language)")
                              : i18n::tr(i18n::languages[i18n::selected() - 1].name);
    form_.row(kLanguage)->description =
        i18n::tr("Language changes immediately. Automatic follows the console language.");
    form_.add_header(i18n::tr("Video"));
    form_
        .add_choice(kResolution, i18n::tr("Resolution"),
                    {"1920 \xC3\x97 1080", "2560 \xC3\x97 1440", "3840 \xC3\x97 2160"}, 0)
        .description = i18n::tr("The picture Sunshine encodes. 1440p is scaled to the 4K output.");
    form_
        .add_choice(kFrameRate, i18n::tr("Frame rate"),
                    {"30 FPS", "60 FPS", "90 FPS", "120 FPS", i18n::tr("Custom")}, 1)
        .description = i18n::tr("30–120 FPS. Custom adjusts one frame at a time.");
    form_.add_slider(kCustomFps, i18n::tr("Custom frame rate"), 60.0f, 30.0f, 120.0f, 1.0f).unit =
        " FPS";
    form_.add_choice(kCodec, i18n::tr("Video codec"), {"H.264", "HEVC", "PyroWave"}, 0)
        .description = i18n::tr("PyroWave needs a compatible host, high bitrate and wired LAN.");
    form_.add_choice(kChroma, i18n::tr("Chroma sampling"), {"4:2:0", "4:4:4"}, 0).description =
        i18n::tr("4:4:4 is available with PyroWave; native codecs use 4:2:0.");
    form_.add_toggle(kHdr, "HDR", false).description =
        i18n::tr("HDR10 through HEVC Main10 or 10-bit PyroWave, when advertised by the "
                 "PC.");
    ui::FormRow &bitrate = form_.add_slider(kBitrate, i18n::tr("Bitrate"), 20.0f, kBitrateStep,
                                            kBitrateMax, kBitrateStep);
    bitrate.unit = " Mbps";
    bitrate.description = i18n::tr("Higher is not always better: the decoder sets the limit.");
    form_.add_header(i18n::tr("Sound"));
    form_.add_choice(kAudio, i18n::tr("Audio"), {i18n::tr("Stereo"), i18n::tr("5.1 surround")}, 0)
        .description = i18n::tr("48 kHz Opus, decoded on the console.");
    form_.add_toggle(kUiSound, i18n::tr("Menu sounds"), true).description =
        i18n::tr("Menu navigation and confirmation sounds.");
    form_.add_header(i18n::tr("Host session"));
    form_.add_toggle(kMuteHost, i18n::tr("Mute host audio"), true).description = i18n::tr(
        "Stream audio to PS5 without playing it on the PC. Host audio routing must support this.");
    form_.add_toggle(kOptimizeHost, i18n::tr("Optimize game/display settings"), true).description =
        i18n::tr("GeForce Experience game settings or Sunshine display policy. Vibepollo may "
                 "ignore this flag.");
    form_.add_toggle(kHostQuit, i18n::tr("Quit host app after stream"), false).description =
        i18n::tr("Stop the game or app on the PC when leaving the stream.");
    form_.add_header(i18n::tr("Display"));
    form_
        .add_choice(kArea, i18n::tr("Picture size"),
                    {i18n::tr("TV safe"), i18n::tr("Edge to edge")}, 0)
        .description = i18n::tr("TV safe keeps a margin for televisions that crop the picture.");
    form_.add_toggle(kVsync, i18n::tr("V-Sync"), true).description =
        i18n::tr("Off shows each frame at once: lower latency, visible tearing.");
    form_
        .add_choice(kPacing, i18n::tr("Frame pacing"),
                    {i18n::tr("Unpaced"), i18n::tr("Paced"), i18n::tr("Paced+VRR")}, 0)
        .description = i18n::tr("Smooth frame timing; VRR uses fixed refresh if unavailable.");
    form_.add_header(i18n::tr("Decoder"));
    form_
        .add_choice(kPipeline, i18n::tr("Pipeline"),
                    {i18n::tr("Classic"), i18n::tr("Adaptive (experimental)")}, 0)
        .description = i18n::tr("Classic decodes one frame at a time. Adaptive overlaps "
                                "frames when decoding falls behind.");
    form_
        .add_stepper(kCores, i18n::tr("CPU cores"), MOONLIGHT_DECODER_CORES_DEFAULT,
                     MOONLIGHT_DECODER_CORES_MIN, MOONLIGHT_DECODER_CORES_MAX)
        .description = i18n::tr("Cores reserved for decoding; the stream uses the rest.");
    form_.add_header(i18n::tr("Diagnostics"));
    form_.add_toggle(kLogging, i18n::tr("Diagnostic logs"), true).description =
        i18n::tr("Bounded logs and output interval traces saved after the stream.");
    form_.style.row_height = 66.0f;
    form_.style.header_height = 54.0f;
    form_.style.label_size = 26.0f;
    form_.style.on_text = i18n::tr("On");
    form_.style.off_text = i18n::tr("Off");
    form_.style.control_width = 440.0f;
    // Wide enough for "1000 Mbps", which the PyroWave range reaches.
    form_.style.number_width = 156.0f;
    form_.style.highlight.kind = ui::HighlightKind::tint;
    form_.set_bounds({kMargin - 20.0f, kContentTop - 8.0f, 1060.0f, 720.0f});

    headroom_.label = i18n::tr("Decoder load");
    headroom_.style.shape = ui::MeterShape::linear;
    headroom_.style.height = 18.0f;
    headroom_.style.peak_hold = false;
    headroom_.style.unit = " Mbps";
    profile_details_.style.panel = false;
    profile_details_.style.row_height = 44.0f;
    profile_details_.style.label_size = 21.0f;
    profile_details_.style.value_size = 22.0f;
    profile_details_.style.padding = 4.0f;
    warning_.style.kind = ui::StatusKind::warning;
    warning_.style.look = ui::BannerLook::accent;
    warning_.style.dismissable = false;
    warning_.style.icon_size = 34.0f;
    warning_.style.min_height = 0.0f;
    const Rect profile = profile_inside();
    headroom_.set_bounds({profile.x, profile.y + 104.0f, profile.w, 74.0f});
    warning_.set_bounds({profile.x, profile.y + 196.0f, profile.w, 0.0f});
    profile_details_.set_bounds({profile.x, profile.y + 228.0f, profile.w, profile.h - 224.0f});

    // ---- About ----
    files_.style.panel = false;
    files_.style.row_height = 44.0f;
    files_.style.label_size = 21.0f;
    files_.style.value_size = 22.0f;
    files_.style.padding = 4.0f;
    files_.style.label_share = 0.2f;
    const Rect start = start_panel_.content_rect(kStartPanel).inset(14.0f);
    files_.set_bounds({start.x, start.y + 398.0f, start.w, 170.0f});

    // ---- overlays ----
    pin_.style.length = 4;
    pin_.style.box_width = 92.0f;
    pin_.style.box_height = 112.0f;
    pin_.style.digit_size = 54.0f;
    pin_.style.gap = 18.0f;
    pin_.style.chevrons = false;
    pin_.style.centered = true;
    pin_.style.spin = false;
    pin_.set_active(false);
    pin_.set_bounds({kPairPanel.x + 60.0f, kPairPanel.y + 214.0f, kPairPanel.w - 120.0f, 130.0f});
    pair_timer_.style.shape = ui::CountdownShape::ring;
    pair_timer_.style.thickness = 8.0f;
    pair_timer_.style.on_panel = true;
    pair_timer_.label = i18n::tr("left");
    pair_timer_.set_bounds({kPairPanel.cx() - 60.0f, kPairPanel.y + 368.0f, 120.0f, 120.0f});

    port_prompt_.style.width = 560.0f;
    port_prompt_.style.max_length = 5;
    port_prompt_.style.auto_capital = false;
    port_prompt_.style.allow_empty = true;
    port_prompt_.style.key_height = 66.0f;
    port_prompt_.style.buttons = false;
    port_prompt_.set_title(i18n::tr("Sunshine port"));
    port_prompt_.field.set_helper(i18n::tr("Sunshine's default is 47989"));
    port_prompt_.keyboard.set_layouts({ui::KeyboardLayout::numeric()});

    host_prompt_.style.width = 560.0f;
    host_prompt_.style.max_length = 21;
    host_prompt_.style.auto_capital = false;
    host_prompt_.style.key_height = 62.0f;
    host_prompt_.style.buttons = false;
    host_prompt_.style.empty_error = i18n::tr("Type the PC's address");
    host_prompt_.set_title(i18n::tr("Add a PC"));
    host_prompt_.field.set_label(i18n::tr("Address of the PC"));
    host_prompt_.field.set_helper(i18n::tr("For example 192.168.1.50, or 192.168.1.50:48989"));
    host_prompt_.keyboard.set_layouts({address_layout()});

    unpair_dialog_.style.width = 720.0f;
    update_dialog_.style.width = 800.0f;
    update_ring_.style.thickness = 14.0f;
    update_notes_.style.panel = false;
    update_notes_.style.focus_ring = false;
    update_notes_.style.footer = false;
    update_notes_.style.padding = 8.0f;
    update_notes_.style.subheading_size = 27.0f;
    update_notes_.set_bounds({kNotesPanel.x + 56.0f, kNotesPanel.y + 150.0f, kNotesPanel.w - 112.0f,
                              kNotesPanel.h - 270.0f});
    update_notes_.set_active(true);
    update_ring_.set_bounds({kUpdatePanel.cx() - 95.0f, kUpdatePanel.y + 196.0f, 190.0f, 190.0f});

    loader_.style.layout = ui::LoadingLayout::corner;
    loader_.style.veil = 0.62f;
    loader_.style.prompt = "";
    // The bar is drawn here (draw_connect_bar), not by the loading screen:
    // the stream continues the same bar after the launcher has closed.
    loader_.style.indicator = ui::LoadingIndicator::none;
    loader_.style.percent = false;
    loader_.art = [this](ui::Canvas &canvas, const Rect &screen, float)
    {
        const moonlight_backend_snapshot_t &backend = model_.backend();
        if (model_.selected_app() >= backend.app_count)
            return;
        if (const Artwork *art = artwork(backend.apps[model_.selected_app()].id))
        {
            // The poster fills the screen's width; its middle is what shows.
            const float height = screen.w / art->aspect;
            canvas.list.image(art->texture,
                              {screen.x, screen.y + (screen.h - height) * 0.5f, screen.w, height},
                              gfx::kFullUv, Color::rgb(0xffffff));
        }
    };

    toasts_.style.anchor = ui::ToastAnchor::top_right;
    toasts_.style.margin = 96.0f;
    toasts_.style.width = 470.0f;
    toasts_.set_bounds({0.0f, 60.0f, gfx::kVirtualWidth, gfx::kVirtualHeight - 60.0f});

    sync_settings_from_config();
}

void View::restyle()
{
    const ui::Theme &t = theme_;
    tabs_.style.theme = t;
    status_.style.theme = t;
    profile_.style.theme = t;
    hosts_.style.theme = t;
    host_panel_.style.theme = t;
    host_details_.style.theme = t;
    open_games_.style.theme = t;
    host_actions_.style.theme = t;
    remove_.style.theme = t;
    empty_.style.theme = t;
    searching_.style.theme = t;
    apps_.style.theme = t;
    start_.style.theme = t;
    stop_.style.theme = t;
    shelf_.style.theme = t;
    no_apps_.style.theme = t;
    loading_apps_.style.theme = t;
    form_.style.theme = t;
    language_.style.theme = t;
    profile_panel_.style.theme = t;
    headroom_.style.theme = t;
    profile_details_.style.theme = t;
    warning_.style.theme = t;
    shortcut_panel_.style.theme = t;
    credits_panel_.style.theme = t;
    start_panel_.style.theme = t;
    files_.style.theme = t;
    pin_.style.theme = t;
    pair_timer_.style.theme = t;
    port_prompt_.style.theme = t;
    host_prompt_.style.theme = t;
    unpair_dialog_.style.theme = t;
    update_dialog_.style.theme = t;
    update_ring_.style.theme = t;
    update_notes_.style.theme = t;
    loader_.style.theme = t;
    toasts_.style.theme = t;
    apply_ambient(true);
}

Rect View::host_inside() const
{
    return host_panel_.content_rect(kHostPanel).inset(14.0f);
}

Rect View::profile_inside() const
{
    return profile_panel_.content_rect(kProfilePanel).inset(12.0f);
}

// ---- keeping up with the model -------------------------------------------

void View::sync()
{
    seen_revision_ = model_.revision();
    sync_hosts();
    sync_games();
    sync_profile(true);
}

int View::host_focus() const
{
    return hosts_.items().empty() ? 0 : hosts_.focus();
}

bool View::on_host() const
{
    return host_focus() < static_cast<int>(model_.config().host_count);
}

void View::sync_hosts()
{
    const moonlight_config_t &config = model_.config();
    const bool on_add = !hosts_.items().empty() && !on_host();
    std::vector<ui::ListItem> items;
    for (unsigned index = 0; index < config.host_count; ++index)
    {
        const moonlight_config_host_t &host = config.hosts[index];
        ui::ListItem item;
        item.title = host.name[0] ? host.name : host.address;
        char endpoint[MOONLIGHT_CONFIG_ADDRESS_SIZE + 8];
        format_endpoint(endpoint, sizeof(endpoint), host);
        item.subtitle = endpoint;
        items.push_back(std::move(item));
    }
    ui::ListItem add;
    add.title = i18n::tr("Add a PC");
    add.subtitle = i18n::tr("By address, with a port if needed");
    items.push_back(std::move(add));
    const int count = static_cast<int>(items.size());
    hosts_.set_items(std::move(items));
    hosts_.set_focus(
        on_add ? count - 1 : std::min(static_cast<int>(config.selected_host), count - 1), true);
    if (!on_host() && host_zone_ != 0)
    {
        host_zone_ = 0;
        hosts_.set_active(true);
        open_games_.set_active(false);
        host_actions_.set_active(false);
        remove_.set_active(false);
    }
    shown_host_ = -2;
    sync_host_panel();
}

void View::sync_host_panel()
{
    const moonlight_config_t &config = model_.config();
    if (!on_host())
        return;
    const unsigned index = static_cast<unsigned>(host_focus());
    const moonlight_config_host_t &host = config.hosts[index];
    const HostStatus status = model_.host_status(index);
    const unsigned port = moonlight_config_host_port(&host);
    char text[256];
    std::vector<ui::DetailItem> items;
    items.push_back({i18n::tr("Address"), host.address});
    std::snprintf(text, sizeof(text),
                  port == MOONLIGHT_CONFIG_DEFAULT_HTTP_PORT ? i18n::tr("%u (Sunshine's default)")
                                                             : "%u",
                  port);
    items.push_back({i18n::tr("Port"), text});
    items.push_back({i18n::tr("Connection"), !status.known   ? i18n::tr("Checking")
                                             : status.online ? i18n::tr("Local network")
                                                             : i18n::tr("Not answering")});
    items.push_back({i18n::tr("Pairing"), !status.known || !status.online ? i18n::tr("Unknown")
                                          : status.paired ? i18n::tr("Paired with this PS5")
                                                          : i18n::tr("Not paired yet")});
    if (status.online && status.paired)
    {
        std::snprintf(text, sizeof(text), i18n::tr("%u available"), status.app_count);
        items.push_back({i18n::tr("Apps"), text});
        items.push_back({i18n::tr("Running now"), status.current_app_id == 0 ? i18n::tr("Nothing")
                                                  : status.running[0]        ? status.running
                                                                             : i18n::tr("An app")});
    }
    host_details_.set_items(std::move(items));
    open_games_.label = status.online && status.paired ? i18n::tr("Open games")
                        : status.online                ? i18n::tr("Pair this PC")
                                                       : i18n::tr("Try again");
    host_actions_.item(1).label =
        status.online && status.paired ? i18n::tr("Unpair") : i18n::tr("Pair");
    host_actions_.item(1).disabled = !status.online;
    shown_host_ = static_cast<int>(index);
}

void View::sync_games()
{
    const moonlight_backend_snapshot_t &backend = model_.backend();
    // Box art belongs to one PC: another PC's apps start without it.
    char key[160];
    std::snprintf(key, sizeof(key), "%s:%u", backend.host, backend.http_port);
    if (art_host_ != key)
    {
        art_host_ = key;
        for (const Artwork &art : artwork_)
            released_.push_back(art.texture);
        artwork_.clear();
    }
    std::vector<ui::CardItem> items;
    for (unsigned index = 0; index < backend.app_count; ++index)
    {
        const moonlight_backend_app_t &app = backend.apps[index];
        ui::CardItem item;
        item.title = app.name;
        item.tag = app.id;
        if (backend.current_app_id == app.id)
            item.badge = "RUNNING";
        if (const Artwork *art = artwork(app.id))
        {
            item.texture = art->texture;
            item.image_aspect = art->aspect;
            item.accent = art->palette.accent;
        }
        items.push_back(std::move(item));
    }
    const bool had_apps = !apps_.items().empty();
    const int count = static_cast<int>(items.size());
    apps_.set_items(std::move(items));
    if (count > 0)
        apps_.set_focus(std::min(static_cast<int>(model_.selected_app()), count - 1), true);
    if (count > 0 && !had_apps)
        apps_.enter();
    shelf_.set_count(count);
    if (count == 0 && games_zone_ != 0)
    {
        games_zone_ = 0;
        start_.set_active(false);
        stop_.set_active(false);
    }
    const bool running = model_.selected_app() < backend.app_count &&
                         backend.apps[model_.selected_app()].id == backend.current_app_id;
    start_.label = running ? i18n::tr("Resume stream") : i18n::tr("Start stream");
    stop_.set_disabled(backend.current_app_id == 0 || model_.busy() == Busy::stopping);
}

void View::sync_settings_from_config()
{
    const moonlight_config_t &config = model_.config();
    form_.set_choice(kResolution, static_cast<int>(std::min(config.stream_resolution, 2u)));
    const bool custom = prosperolight::client_preferences().custom_fps ||
                        (config.stream_fps != 30 && config.stream_fps != 60 &&
                         config.stream_fps != 90 && config.stream_fps != 120);
    form_.set_choice(kFrameRate, custom                    ? 4
                                 : config.stream_fps == 30 ? 0
                                 : config.stream_fps == 60 ? 1
                                 : config.stream_fps == 90 ? 2
                                                           : 3);
    form_.set_visible(kCustomFps, custom);
    form_.set_slider(kCustomFps, static_cast<float>(config.stream_fps));
    form_.set_toggle(kMuteHost, prosperolight::client_preferences().mute_host);
    form_.set_toggle(kOptimizeHost, prosperolight::client_preferences().optimize);
    form_.set_choice(kCodec, static_cast<int>(std::min(config.video_codec, 2u)));
    form_.set_choice(kChroma, config.chroma_sampling == MOONLIGHT_CHROMA_444 ? 1 : 0);
    form_.row(kChroma)->disabled = config.video_codec != MOONLIGHT_VIDEO_CODEC_PYROWAVE;
    form_.set_toggle(kHdr, config.hdr_enabled != 0);
    if (ui::FormRow *bitrate = form_.row(kBitrate))
        bitrate->maximum =
            std::max(config.video_codec == MOONLIGHT_VIDEO_CODEC_PYROWAVE ? kPyroWaveBitrateMax
                                                                          : kBitrateMax,
                     static_cast<float>(config.bitrate_mbps));
    form_.set_slider(kBitrate, static_cast<float>(config.bitrate_mbps));
    form_.set_choice(kPacing, static_cast<int>(moonlight::presentation_mode()));
    form_.set_toggle(kLogging, prosperolight_logs_enabled() != 0);
    form_.set_toggle(kUiSound, prosperolight::ui_sound_enabled());
    form_.set_toggle(kHostQuit, prosperolight::host_quit_enabled());
    const bool native = config.video_codec != MOONLIGHT_VIDEO_CODEC_PYROWAVE;
    form_.row(kPipeline)->disabled = !native;
    form_.row(kCores)->disabled = !native;
    form_.set_choice(kAudio, config.audio_configuration == MOONLIGHT_AUDIO_51_SURROUND ? 1 : 0);
    form_.set_choice(kArea, config.display_area == MOONLIGHT_DISPLAY_AREA_FULL ? 1 : 0);
    form_.set_toggle(kVsync, config.vsync_enabled != 0);
    form_.set_choice(kPipeline,
                     config.decoder_pipeline == MOONLIGHT_DECODER_PIPELINE_ADAPTIVE ? 1 : 0);
    form_.set_stepper(kCores, static_cast<int>(config.decoder_cores));
}

void View::apply_setting(int id)
{
    moonlight_config_t &config = model_.settings();
    switch (id)
    {
    case kResolution:
        config.stream_resolution = static_cast<std::uint32_t>(form_.choice_index(kResolution));
        break;
    case kFrameRate:
    {
        const int selected = std::clamp(form_.choice_index(kFrameRate), 0, 4);
        auto preferences = prosperolight::client_preferences();
        preferences.custom_fps = selected == 4;
        if (!prosperolight::client_preferences_save(preferences))
        {
            toasts_.push(ui::StatusKind::danger, i18n::tr("Could not save client settings"),
                         i18n::tr("Try again."));
            sync_settings_from_config();
            return;
        }
        if (selected < 4)
        {
            static constexpr unsigned kRates[] = {30, 60, 90, 120};
            config.stream_fps = kRates[selected];
        }
        break;
    }
    case kCustomFps:
        config.stream_fps =
            static_cast<unsigned>(std::clamp(form_.slider_value(kCustomFps), 30.0f, 120.0f));
        break;
    case kMuteHost:
    case kOptimizeHost:
    {
        auto preferences = prosperolight::client_preferences();
        if (id == kMuteHost)
            preferences.mute_host = form_.toggle_value(kMuteHost);
        else
            preferences.optimize = form_.toggle_value(kOptimizeHost);
        if (!prosperolight::client_preferences_save(preferences))
            toasts_.push(ui::StatusKind::danger, i18n::tr("Could not save client settings"),
                         i18n::tr("Try again."));
        sync_settings_from_config();
        return;
    }
    case kCodec:
        config.video_codec = static_cast<std::uint32_t>(form_.choice_index(kCodec));
        if (config.video_codec == MOONLIGHT_VIDEO_CODEC_H264)
            config.hdr_enabled = 0;
        break;
    case kChroma:
        config.chroma_sampling =
            form_.choice_index(kChroma) == 1 ? MOONLIGHT_CHROMA_444 : MOONLIGHT_CHROMA_420;
        break;
    case kHdr:
        config.hdr_enabled = form_.toggle_value(kHdr) ? 1u : 0u;
        if (config.hdr_enabled && config.video_codec == MOONLIGHT_VIDEO_CODEC_H264)
            config.video_codec = MOONLIGHT_VIDEO_CODEC_HEVC;
        break;
    case kBitrate:
        config.bitrate_mbps = static_cast<std::uint32_t>(std::lround(form_.slider_value(kBitrate)));
        break;
    case kPacing:
        if (!moonlight::save_presentation_mode(static_cast<unsigned>(form_.choice_index(kPacing))))
            toasts_.push(ui::StatusKind::danger, i18n::tr("Could not save frame pacing"),
                         i18n::tr("Try again."));
        sync_settings_from_config();
        return;
    case kLogging:
        if (!prosperolight_logs_set_enabled(form_.toggle_value(kLogging)))
            toasts_.push(ui::StatusKind::danger, i18n::tr("Could not save logging"),
                         i18n::tr("Try again."));
        sync_settings_from_config();
        return;
    case kHostQuit:
        if (!prosperolight::host_quit_set_enabled(form_.toggle_value(kHostQuit)))
            toasts_.push(ui::StatusKind::danger, i18n::tr("Could not save host app quit"),
                         i18n::tr("Try again."));
        sync_settings_from_config();
        return;
    case kUiSound:
        if (!prosperolight::ui_sound_set_enabled(form_.toggle_value(kUiSound)))
            toasts_.push(ui::StatusKind::danger, i18n::tr("Could not save menu sounds"),
                         i18n::tr("Try again."));
        sync_settings_from_config();
        return;
    case kAudio:
        config.audio_configuration =
            form_.choice_index(kAudio) == 1 ? MOONLIGHT_AUDIO_51_SURROUND : MOONLIGHT_AUDIO_STEREO;
        break;
    case kArea:
        config.display_area = form_.choice_index(kArea) == 1 ? MOONLIGHT_DISPLAY_AREA_FULL
                                                             : MOONLIGHT_DISPLAY_AREA_TV_SAFE;
        break;
    case kVsync:
        config.vsync_enabled = form_.toggle_value(kVsync) ? 1u : 0u;
        break;
    case kPipeline:
        config.decoder_pipeline = form_.choice_index(kPipeline) == 1
                                      ? MOONLIGHT_DECODER_PIPELINE_ADAPTIVE
                                      : MOONLIGHT_DECODER_PIPELINE_CLASSIC;
        break;
    case kCores:
        config.decoder_cores = static_cast<std::uint32_t>(form_.stepper_value(kCores));
        break;
    default:
        return;
    }
    model_.SettingsChanged();
    sync_settings_from_config();
    sync_profile(false);
}

void View::sync_profile(bool snap)
{
    const moonlight_config_t &config = model_.config();
    const moonlight_backend_snapshot_t &backend = model_.backend();
    const BitrateLimit limit = limit_for(config.stream_fps);
    const float bitrate = static_cast<float>(config.bitrate_mbps);
    const bool pyro = config.video_codec == MOONLIGHT_VIDEO_CODEC_PYROWAVE;
    const bool hevc =
        !pyro && (config.video_codec == MOONLIGHT_VIDEO_CODEC_HEVC || config.hdr_enabled);
    const bool h264 = !pyro && !hevc;
    const bool h264_4k = h264 && config.stream_resolution == MOONLIGHT_STREAM_RESOLUTION_2160P;
    const bool h264_red = h264_4k && config.stream_fps == 120;
    const bool h264_yellow = h264_4k && config.stream_fps == 90;
    const bool h264_60 = h264_4k && config.stream_fps == 60;
    const bool hevc_limits = config.stream_resolution == MOONLIGHT_STREAM_RESOLUTION_2160P && hevc;
    limits_apply_ = pyro || hevc_limits || h264_red || h264_yellow || h264_60;
    const float scale = hevc_limits ? limit.freezes * 1.3f : 1000.0f;
    headroom_.label = limits_apply_ ? i18n::tr("Decoder load") : i18n::tr("Requested bitrate");
    headroom_.style.value_scale = scale;
    headroom_.style.warning_at = pyro                      ? 0.500001f
                                 : h264_red || h264_yellow ? 0.0f
                                 : h264_60                 ? 0.080001f
                                 : hevc_limits             ? (limit.smooth + 2.0f) / scale
                                                           : 2.0f;
    headroom_.style.danger_at = pyro          ? 0.7f
                                : h264_red    ? 0.0f
                                : h264_60     ? 0.080001f
                                : hevc_limits ? limit.freezes / scale
                                              : 2.0f;
    headroom_.style.zone_strip = limits_apply_;
    headroom_.set_value(std::min(bitrate / scale, 1.0f), snap);

    const bool fast = config.stream_fps > MOONLIGHT_STREAM_FPS_60;
    std::vector<ui::DetailItem> items;
    items.push_back({i18n::tr("TV output"), fast ? "119.88 Hz" : "59.94 Hz"});
    items.push_back({i18n::tr("Codec"), pyro                 ? codec_name(config)
                                        : config.hdr_enabled ? "HEVC Main10, HDR10"
                                        : hevc               ? "HEVC Main"
                                                             : "H.264 High"});
    items.push_back({i18n::tr("Sound"), config.audio_configuration == MOONLIGHT_AUDIO_51_SURROUND
                                            ? i18n::tr("5.1 surround, 48 kHz")
                                            : i18n::tr("Stereo, 48 kHz")});
    profile_details_.set_items(std::move(items));

    const bool online = model_.backend_valid() && backend.online;
    const bool no_hdr = online && !pyro && config.hdr_enabled && !backend.main10_supported;
    const bool no_hevc = online && hevc && !backend.hevc_supported;
    const auto selected_profile = moonlight::resolve_stream_profile(
        config.video_codec, config.chroma_sampling, config.hdr_enabled != 0);
    const bool no_pyro =
        online && pyro && !(backend.pyrowave_profiles & selected_profile.capability);
    char text[2048];
    if (no_hdr || no_hevc || no_pyro)
    {
        warning_.title = no_pyro  ? i18n::tr("PyroWave profile unavailable")
                         : no_hdr ? i18n::tr("This PC cannot encode HDR")
                                  : i18n::tr("This PC cannot encode HEVC");
        std::snprintf(text, sizeof(text),
                      i18n::tr("%s does not advertise it. The stream will not start."),
                      backend.name[0] ? backend.name : i18n::tr("The PC"));
    }
    else
    {
        warning_.title = i18n::tr("Decoder load recommendation");
        if (pyro)
            std::snprintf(
                text, sizeof(text), "%s",
                i18n::tr("Smooth up to 500 Mbps. Above 500 Mbps stability may decrease; above "
                         "700 Mbps packet loss is more likely. Use wired LAN."));
        else if (h264_red)
            std::snprintf(
                text, sizeof(text), "%s",
                i18n::tr("H.264 4K120 can drop frames at any bitrate. Lower the frame rate or "
                         "use HEVC/PyroWave."));
        else if (h264_yellow)
            std::snprintf(text, sizeof(text), "%s",
                          i18n::tr("H.264 4K90 may drop frames at any bitrate."));
        else if (h264_60)
            std::snprintf(text, sizeof(text), "%s",
                          i18n::tr("Smooth up to 80 Mbps at 4K60. Above 80 Mbps the picture "
                                   "may stutter."));
        else
            std::snprintf(text, sizeof(text),
                          i18n::tr("Smooth up to %.0f Mbps. Above %.0f Mbps the picture "
                                   "freezes about once a second."),
                          static_cast<double>(limit.smooth), static_cast<double>(limit.freezes));
    }
    warning_.body = text;
    const bool warn = no_pyro || no_hdr || no_hevc || (pyro && bitrate > 500.0f) || h264_red ||
                      h264_yellow || (h264_60 && bitrate > 80.0f) ||
                      (hevc_limits && bitrate > limit.smooth);
    warning_.set_shown(warn, snap);

    static const char *const kShort[] = {"1080p", "1440p", i18n::tr("4K")};
    char chip[64];
    std::snprintf(chip, sizeof(chip), "%s  \xC2\xB7  %u FPS%s",
                  kShort[std::min(config.stream_resolution, 2u)], config.stream_fps,
                  config.hdr_enabled ? i18n::tr("  \xC2\xB7  HDR") : "");
    profile_.label = chip;
}

// The glass page takes its colours from the selected app's poster on the
// Games screen, and from the theme everywhere else.
void View::apply_ambient(bool snap)
{
    Color targets[4] = {theme_.backdrop.colors[0], theme_.backdrop.colors[1],
                        theme_.backdrop.colors[2], theme_.backdrop.colors[3]};
    const moonlight_backend_snapshot_t &backend = model_.backend();
    if (screen_ == 1 && model_.selected_app() < backend.app_count)
    {
        if (const Artwork *art = artwork(backend.apps[model_.selected_app()].id))
        {
            targets[1] = gfx::mix(targets[1], art->palette.dark, 0.5f);
            targets[2] = gfx::mix(targets[2], art->palette.mid, 0.6f);
            targets[3] = gfx::mix(targets[3], art->palette.accent, 0.6f);
        }
    }
    for (int i = 0; i < 4; ++i)
    {
        if (snap)
            ambient_[i].snap(targets[i]);
        else
            ambient_[i].target(targets[i]);
    }
}

// Posters are fetched for the rows on screen and the one after them.
void View::request_artwork()
{
    const moonlight_backend_snapshot_t &backend = model_.backend();
    if (screen_ != 1 || backend.app_count == 0)
        return;
    const unsigned row = model_.selected_app() / kColumns;
    const unsigned first = row * kColumns;
    const unsigned last = std::min(backend.app_count, first + 2 * kColumns);
    for (unsigned index = first; index < last; ++index)
    {
        if (!artwork(backend.apps[index].id))
            model_.RequestArtwork(backend.apps[index].id);
    }
}

void View::show(int screen, ui::Feedback *feedback)
{
    if (screen == screen_)
        return;
    screen_ = screen;
    screen_age_ = 0.0f;
    tabs_.set_active(screen, feedback == nullptr);
    if (screen_ == 0)
        hosts_.enter();
    else if (screen_ == 1)
        apps_.enter();
    else if (screen_ == 2)
        form_.enter();
    apply_ambient(false);
}

// ---- input ---------------------------------------------------------------

void View::update(const InputFrame &input, float dt, ui::Feedback &feedback)
{
    age_ += dt;
    clock_ += dt;
    screen_age_ += dt;

    if (!welcomed_)
    {
        welcomed_ = true;
        feedback.play(first_start_ ? audio::Cue::welcome : audio::Cue::resume);
    }

    for (const Notice &notice : model_.TakeNotices())
        toasts_.push(toast_kind(notice.kind), notice.title, notice.body,
                     notice.seconds > 0.0f              ? notice.seconds
                     : notice.kind == NoticeKind::error ? 7.0f
                                                        : 4.0f);
    if (!pending_error_.empty() && age_ > 0.6f)
    {
        show(1, nullptr);
        toasts_.push(ui::StatusKind::danger, i18n::tr("The stream ended"), pending_error_, 10.0f);
        pending_error_.clear();
    }
    if (model_.revision() != seen_revision_)
        sync();

    const bool pairing = model_.pairing();
    if (pairing && !pair_timer_started_)
    {
        pair_timer_.start(static_cast<float>(MOONLIGHT_BACKEND_PAIR_TIMEOUT_SECONDS));
        feedback.play(audio::Cue::modal_open);
    }
    else if (!pairing && pair_timer_started_)
    {
        feedback.play(audio::Cue::modal_close);
    }
    pair_timer_started_ = pairing;
    if (pairing)
    {
        pair_timer_.set_remaining(model_.pairing_seconds_left());
        pin_.set_value(model_.pairing_pin());
    }

    // A newer release waits for a quiet moment: no other question is open.
    UpdateOffer offered;
    if (update_ui_ == UpdateUi::hidden && !launching_ && !pairing && age_ > 0.8f &&
        !host_prompt_.is_open() && !port_prompt_.is_open() && !unpair_dialog_.is_open() &&
        model_.TakeUpdateOffer(&offered))
    {
        update_offer_ = offered;
        // The notes as an article: list items, callouts, headings and text.
        std::vector<ui::TextBlock> blocks;
        std::size_t at = 0;
        const std::string &notes = update_offer_.notes;
        while (at < notes.size())
        {
            std::size_t end = notes.find('\n', at);
            if (end == std::string::npos)
                end = notes.size();
            const std::string line = notes.substr(at, end - at);
            at = end + 1;
            if (line.find_first_not_of(" \t\r") == std::string::npos)
                continue;
            const auto starts = [&line](const char *prefix) { return line.rfind(prefix, 0) == 0; };
            const char last = line.back();
            if (starts("- "))
                blocks.push_back(ui::TextBlock::bullet(line.substr(2)));
            else if (starts(i18n::tr("Warning:")) || starts(i18n::tr("Caution:")) ||
                     starts(i18n::tr("Important:")) || starts(i18n::tr("Note:")) ||
                     starts(i18n::tr("Tip:")))
                blocks.push_back(ui::TextBlock::quote(line));
            else if (line.size() <= 60 && last != '.' && last != ':' && last != '!' &&
                     last != '?' && last != ',' && last != ';')
                blocks.push_back(ui::TextBlock::heading(line, 2));
            else
                blocks.push_back(ui::TextBlock::paragraph(line));
        }
        if (!blocks.empty() && update_offer_.notes_truncated)
            blocks.push_back(ui::TextBlock::paragraph(
                i18n::tr("The rest is on the app's page on homebrew.page.")));
        update_notes_.set_content(std::move(blocks));
        open_update_offer(feedback, false);
    }

    if (update_ui_ != UpdateUi::hidden)
    {
        update_modal(input, dt, feedback);
    }
    else if (launching_)
    {
        load_progress_ =
            std::min(load_progress_ + dt / kLaunchSeconds * kHandoverProgress, kHandoverProgress);
        if (load_progress_ >= kHandoverProgress)
            start_stream_ = true;
    }
    else if (host_prompt_.is_open())
    {
        if (host_prompt_.handle(input, feedback) == ui::Event::activated)
        {
            std::string error;
            const std::string text = host_prompt_.text();
            if (model_.AddHost(text.c_str(), &error))
            {
                feedback.play(audio::Cue::saved);
            }
            else
            {
                host_prompt_.open(feedback, text);
                host_prompt_.field.set_error(error);
            }
        }
    }
    else if (port_prompt_.is_open())
    {
        if (port_prompt_.handle(input, feedback) == ui::Event::activated)
        {
            std::string error;
            const std::string text = port_prompt_.text();
            if (model_.SetPort(text.c_str(), &error))
            {
                feedback.play(audio::Cue::saved);
            }
            else
            {
                port_prompt_.open(feedback, text);
                port_prompt_.field.set_error(error);
            }
        }
    }
    else if (unpair_dialog_.is_open())
    {
        if (unpair_dialog_.handle(input, feedback) == ui::Event::activated &&
            unpair_dialog_.choice() == 1)
            model_.Unpair();
    }
    else if (pairing)
    {
        // Sunshine holds the request until the PIN is typed or it times out.
    }
    else if (language_.is_open())
    {
        update_settings(input, feedback);
    }
    else if (input.is_pressed(Action::page_next) || input.is_pressed(Action::page_prev))
    {
        const int direction = input.is_pressed(Action::page_next) ? 1 : -1;
        const int next = (screen_ + direction + kScreens) % kScreens;
        feedback.play(audio::Cue::tab, 1.0f + 0.04f * static_cast<float>(next));
        show(next, &feedback);
    }
    else if (input.is_pressed(Action::menu) && screen_ != 2)
    {
        feedback.play(audio::Cue::tab, 1.08f);
        show(2, &feedback);
    }
    else if (input.is_pressed(Action::north))
    {
        feedback.play(audio::Cue::select, 1.1f);
        model_.Refresh(true);
    }
    else if (input.is_pressed(Action::west) && (screen_ == 1 || model_.backend().current_app_id))
    {
        feedback.play(audio::Cue::select, 0.92f);
        model_.StopApp();
    }
    else if (input.is_pressed(Action::back) && screen_ != 0)
    {
        feedback.play(audio::Cue::back);
        show(0, &feedback);
    }
    else if (screen_ == 0)
    {
        update_hosts(input, feedback);
    }
    else if (screen_ == 1)
    {
        update_games(input, feedback);
    }
    else if (screen_ == 2)
    {
        update_settings(input, feedback);
    }
    request_artwork();

    // ---- animation ----
    tabs_.update(dt);
    status_.update(dt);
    profile_.update(dt);
    hosts_.update(dt);
    host_details_.update(dt);
    open_games_.update(dt);
    host_actions_.update(dt);
    remove_.update(dt);
    empty_.update(dt);
    searching_.update(dt);
    apps_.update(dt);
    start_.update(dt);
    stop_.update(dt);
    shelf_.update(dt);
    no_apps_.update(dt);
    loading_apps_.update(dt);
    form_.update(dt);
    language_.update(dt);
    files_.update(dt);
    headroom_.update(dt);
    profile_details_.update(dt);
    warning_.update(dt);
    pin_.update(dt);
    pair_timer_.update(dt);
    port_prompt_.update(dt);
    host_prompt_.update(dt);
    unpair_dialog_.update(dt);
    update_dialog_.update(dt);
    update_ring_.update(dt);
    update_fade_.target =
        update_ui_ == UpdateUi::working || update_ui_ == UpdateUi::closing ? 1.0f : 0.0f;
    update_fade_.update(dt, 14.0f);
    update_notes_.update(dt);
    update_notes_fade_.target = update_ui_ == UpdateUi::notes ? 1.0f : 0.0f;
    update_notes_fade_.update(dt, 14.0f);
    toasts_.update(dt, feedback);
    pair_fade_.target = pairing ? 1.0f : 0.0f;
    pair_fade_.update(dt, 14.0f);
    loader_.update(dt);
    for (ui::SpringColor &colour : ambient_)
        colour.update(dt, 4.0f);
}

void View::update_hosts(const InputFrame &input, ui::Feedback &feedback)
{
    if (model_.config().host_count == 0 && input.is_pressed(Action::confirm))
    {
        host_prompt_.open(feedback);
        return;
    }
    switch (host_zone_)
    {
    case 0:
        if (input.nav == Direction::right && on_host())
        {
            set_host_zone(1, feedback);
            break;
        }
        switch (hosts_.handle(input, feedback))
        {
        case ui::Event::moved:
            if (on_host())
                model_.SelectHost(static_cast<unsigned>(host_focus()));
            sync_host_panel();
            break;
        case ui::Event::activated:
            if (on_host())
                primary_host_action(feedback);
            else
                host_prompt_.open(feedback);
            break;
        default:
            break;
        }
        break;
    case 1:
        if (input.nav == Direction::left)
            set_host_zone(0, feedback);
        else if (input.nav == Direction::right)
            set_host_zone(2, feedback);
        else if (open_games_.handle(input, feedback) == ui::Event::activated)
            primary_host_action(feedback);
        break;
    case 2:
    {
        const ui::Event event = host_actions_.handle(input, feedback);
        if (event == ui::Event::activated)
        {
            const moonlight_config_host_t *host = model_.selected_host();
            if (host_actions_.focus() == 0)
            {
                char port[8];
                std::snprintf(port, sizeof(port), "%u", moonlight_config_host_port(host));
                port_prompt_.field.set_label(
                    std::string(i18n::tr("Port of ")) +
                    (host && host->name[0] ? host->name : i18n::tr("this PC")));
                port_prompt_.open(feedback, port);
            }
            else if (model_.backend().paired)
            {
                ui::DialogContent content;
                content.icon = ui::StatusKind::question;
                content.title =
                    std::string(i18n::tr("Unpair ")) +
                    (model_.backend().name[0] ? model_.backend().name : i18n::tr("this PC")) + "?";
                content.body = i18n::tr("This PS5 will need a new PIN to use it again.");
                content.buttons = {{i18n::tr("Cancel")},
                                   {i18n::tr("Unpair"), ui::ButtonKind::primary, true}};
                unpair_dialog_.open(std::move(content), feedback);
            }
            else
            {
                model_.StartPairing();
            }
        }
        else if (event == ui::Event::none)
        {
            const Direction exit = host_actions_.exit();
            if (exit == Direction::left)
                set_host_zone(1, feedback);
            else if (exit == Direction::right)
                set_host_zone(3, feedback);
        }
        break;
    }
    default:
        if (input.nav == Direction::left)
        {
            set_host_zone(2, feedback);
        }
        else if (remove_.handle(input, feedback) == ui::Event::activated)
        {
            set_host_zone(0, feedback);
            model_.RemoveHost();
        }
        break;
    }
}

void View::set_host_zone(int zone, ui::Feedback &feedback)
{
    host_zone_ = zone;
    hosts_.set_active(zone == 0);
    open_games_.set_active(zone == 1);
    host_actions_.set_active(zone == 2);
    remove_.set_active(zone == 3);
    if (zone != 3)
        remove_.reset();
    feedback.play(audio::Cue::focus);
}

// What Cross does for the PC in focus: its games, its pairing, or another try.
void View::primary_host_action(ui::Feedback &feedback)
{
    const moonlight_backend_snapshot_t &backend = model_.backend();
    if (!model_.backend_valid())
        return;
    if (backend.online && backend.paired)
    {
        show(1, &feedback);
    }
    else if (backend.online)
    {
        model_.StartPairing();
    }
    else
    {
        model_.Refresh(false);
    }
}

void View::set_games_zone(int zone, ui::Feedback &feedback)
{
    games_zone_ = zone;
    apps_.set_active(zone == 0);
    start_.set_active(zone == 1);
    stop_.set_active(zone == 2);
    feedback.play(audio::Cue::focus);
}

void View::update_games(const InputFrame &input, ui::Feedback &feedback)
{
    if (apps_.items().empty())
    {
        // Nothing to choose: Cross does what the message offers.
        if (input.is_pressed(Action::confirm))
        {
            const moonlight_backend_snapshot_t &backend = model_.backend();
            if (!model_.selected_host())
                show(0, &feedback);
            else if (model_.backend_valid() && backend.online && !backend.paired)
                model_.StartPairing();
            else
                model_.Refresh(false);
            feedback.play(audio::Cue::select);
        }
        return;
    }
    if (games_zone_ == 0)
    {
        const ui::Event event = apps_.handle(input, feedback);
        if (event == ui::Event::moved)
        {
            model_.SelectApp(static_cast<unsigned>(apps_.focus()));
            const moonlight_backend_snapshot_t &backend = model_.backend();
            start_.label = backend.apps[model_.selected_app()].id == backend.current_app_id
                               ? i18n::tr("Resume stream")
                               : i18n::tr("Start stream");
            apply_ambient(false);
        }
        else if (event == ui::Event::activated)
        {
            launch(feedback);
        }
        else if (event == ui::Event::none && apps_.exit() == Direction::up)
        {
            set_games_zone(1, feedback);
        }
        return;
    }
    if (input.nav == Direction::down)
    {
        set_games_zone(0, feedback);
    }
    else if (input.nav == Direction::right && games_zone_ == 1 && !stop_.disabled())
    {
        set_games_zone(2, feedback);
    }
    else if (input.nav == Direction::left && games_zone_ == 2)
    {
        set_games_zone(1, feedback);
    }
    else if (games_zone_ == 1)
    {
        if (start_.handle(input, feedback) == ui::Event::activated)
            launch(feedback);
    }
    else if (stop_.handle(input, feedback) == ui::Event::activated)
    {
        model_.StopApp();
        set_games_zone(1, feedback);
    }
}

void View::launch(ui::Feedback &feedback)
{
    if (!model_.RequestStream())
        return;
    const moonlight_config_t &config = model_.config();
    const moonlight_backend_snapshot_t &backend = model_.backend();
    char text[2048];
    std::snprintf(text, sizeof(text),
                  i18n::tr("%s  \xC2\xB7  %s  \xC2\xB7  %u FPS  \xC2\xB7  %s  \xC2\xB7  %u Mbps"),
                  backend.name, resolution_name(config.stream_resolution), config.stream_fps,
                  codec_name(config), config.bitrate_mbps);
    loader_.title = backend.apps[model_.selected_app()].name;
    loader_.subtitle = text;
    load_progress_ = 0.0f;
    loader_.show(feedback);
    feedback.play(audio::Cue::launch);
    launching_ = true;
}

void View::update_settings(const InputFrame &input, ui::Feedback &feedback)
{
    if (language_.is_open())
    {
        if (language_.handle(input, feedback) == ui::Event::changed)
        {
            if (i18n::select(language_.index()))
            {
                build();
                restyle();
                set_storage(storage_);
                sync();
                tabs_.set_active(screen_, true);
                form_.set_focus(0, true);
            }
            else
            {
                language_.set_index(i18n::selected());
                toasts_.push(ui::StatusKind::danger, i18n::tr("Could not save language"),
                             i18n::tr("Try again."));
            }
        }
        return;
    }
    const auto event = form_.handle(input, feedback);
    if (event == ui::Event::changed)
        apply_setting(form_.changed_id());
    if (event == ui::Event::activated && form_.changed_id() == kLanguage)
    {
        const auto row = form_.row_rect(form_.focus());
        language_.set_bounds({row.x + row.w - 440.0f, row.y, 440.0f, row.h});
        language_.open(feedback);
    }
}

// ---- drawing -------------------------------------------------------------

ui::GlyphStyle View::glyphs(const ui::Painter &paint) const
{
    ui::GlyphStyle style = theme_.dark ? ui::GlyphStyle::dark() : ui::GlyphStyle::light();
    style.label = paint.page_text();
    return style;
}

void View::draw(Frame &frame) const
{
    const ui::Theme &theme = theme_;
    frame.backdrop = theme.backdrop;
    frame.backdrop.time = clock_;
    // Frosted panels need light behind them to blur: slow pools of the page's
    // two brightest colours drift under the widgets.
    for (int i = 0; i < 4; ++i)
        frame.backdrop.colors[i] = ambient_[i].value();
    for (int i = 0; i < 4; ++i)
    {
        const float phase = clock_ * 0.1f + static_cast<float>(i) * 1.9f;
        const float cx = 380.0f + static_cast<float>(i) * 400.0f + std::sin(phase) * 140.0f;
        const float cy = 600.0f + std::cos(phase * 1.3f) * 230.0f;
        frame.scene.shadow(
            {cx - 170.0f, cy - 170.0f, 340.0f, 340.0f}, 170.0f, 190.0f,
            gfx::mix(ambient_[2].value(), ambient_[3].value(), static_cast<float>(i % 2))
                .with_alpha(0.3f));
    }

    gfx::DrawList &list = frame.overlay;
    ui::Canvas canvas{list, fonts_, frame.glass_texture, clock_};
    ui::Painter paint(list, fonts_, theme, frame.glass_texture);

    const float arrive = tween::cubic_out(screen_age_ / 0.28f);
    list.push_opacity(tween::cubic_out(age_ / 0.35f));
    draw_header(canvas, paint);
    list.push_opacity(arrive);
    list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, 18.0f * (1.0f - arrive));
    if (screen_ == 0)
        draw_hosts(canvas, paint);
    else if (screen_ == 1)
        draw_games(canvas, paint);
    else if (screen_ == 2)
        draw_settings(canvas, paint);
    else
        draw_about(canvas, paint);
    list.pop_transform();
    list.pop_opacity();
    draw_footer(canvas, paint);
    list.pop_opacity();

    // Modal layers. The blurred copy holds the backdrop only, so they are
    // drawn solid and hide what is under them.
    ui::Canvas above{list, fonts_, 0, clock_};
    toasts_.draw(above);
    draw_pairing(above);
    port_prompt_.draw(above);
    host_prompt_.draw(above);
    unpair_dialog_.draw(above);
    draw_update(above);
    draw_update_notes(above);
    update_dialog_.draw(above);
    language_.draw_popover(above);
    loader_.draw(above);
    draw_loader_tip(above);
    draw_connect_bar(above);
}

void View::draw_header(ui::Canvas &canvas, ui::Painter &paint) const
{
    gfx::DrawList &list = canvas.list;
    const ui::Theme &t = theme_;
    // The mark: a crescent on a small plate, then the name.
    const Rect plate{kMargin, 62.0f, 52.0f, 52.0f};
    list.gradient_rect(plate, 14.0f, gfx::mix(t.primary, Color::rgb(0xffffff), 0.15f),
                       gfx::mix(t.primary, t.page, 0.45f));
    const Color cut = gfx::mix(t.primary, t.page, 0.3f);
    list.circle(plate.cx() - 2.0f, plate.cy() + 1.0f, 15.0f, t.on_primary);
    list.circle(plate.cx() + 6.0f, plate.cy() - 5.0f, 13.0f, cut);
    paint.heading("ProsperoLight", kMargin + 70.0f, 98.0f, 31.0f, paint.page_text());

    // Tabs between the two shoulder buttons that turn them.
    const ui::GlyphStyle keys = glyphs(paint);
    const Rect first = tabs_.tab_rect(canvas.fonts, 0);
    const Rect last = tabs_.tab_rect(canvas.fonts, kScreens - 1);
    ui::draw_button(list, canvas.fonts, keys, ui::Button::l1,
                    first.x - 22.0f - ui::button_width(ui::Button::l1, 32.0f), 88.0f, 32.0f);
    tabs_.draw(canvas);
    ui::draw_button(list, canvas.fonts, keys, ui::Button::r1, last.x + last.w + 22.0f, 88.0f,
                    32.0f);

    profile_.draw(canvas);
    status_.draw(canvas);
}

void View::draw_footer(ui::Canvas &canvas, ui::Painter &paint) const
{
    gfx::DrawList &list = canvas.list;
    const ui::Theme &t = theme_;
    const moonlight_backend_snapshot_t &backend = model_.backend();
    const moonlight_config_host_t *host = model_.selected_host();
    const Busy busy = model_.busy();

    // What the selected PC is doing, at the leading end.
    if (busy == Busy::searching || !host)
    {
        if (busy == Busy::searching)
            searching_.draw(canvas);
        paint.body(busy == Busy::searching ? i18n::tr("Searching this network for Sunshine")
                                           : i18n::tr("No PC selected"),
                   kMargin + (busy == Busy::searching ? 50.0f : 0.0f), 1018.0f, 24.0f,
                   paint.page_text_muted());
    }
    else
    {
        const bool valid = model_.backend_valid();
        const bool ready = valid && backend.online && backend.paired;
        const Color dot = !valid || model_.reconnecting() ? t.text_muted
                          : ready                         ? t.success
                          : backend.online                ? t.warning
                                                          : t.danger;
        const char *state = busy == Busy::stopping     ? i18n::tr("Stopping the app")
                            : busy == Busy::unpairing  ? i18n::tr("Unpairing")
                            : busy == Busy::refreshing ? i18n::tr("Checking")
                            : !valid                   ? i18n::tr("Checking")
                            : model_.reconnecting()    ? i18n::tr("Reconnecting")
                            : ready                    ? i18n::tr("Sunshine ready")
                            : backend.online           ? i18n::tr("Pairing required")
                                                       : i18n::tr("Not answering");
        list.circle(kMargin + 8.0f, 1009.0f, 7.0f, dot);
        if (ready)
            list.glow({kMargin + 1.0f, 1002.0f, 14.0f, 14.0f}, 7.0f, 10.0f, dot.with_alpha(0.5f));
        const char *name = backend.name[0] ? backend.name
                           : host->name[0] ? host->name
                                           : host->address;
        const float width = paint.label(name, kMargin + 30.0f, 1018.0f, 24.0f, paint.page_text());
        paint.body(state, kMargin + 44.0f + width, 1018.0f, 24.0f, paint.page_text_muted());
    }

    ui::Hint hints[5];
    int count = 0;
    if (screen_ == 0)
    {
        const HostStatus status =
            on_host() ? model_.host_status(static_cast<unsigned>(host_focus())) : HostStatus{};
        hints[count++] = {ui::Button::dpad, i18n::tr("Choose")};
        hints[count++] = {ui::Button::cross, !on_host() || model_.config().host_count == 0
                                                 ? i18n::tr("Add a PC")
                                             : host_zone_ == 3 ? i18n::tr("Hold to remove")
                                             : host_zone_ == 2 ? i18n::tr("Select")
                                             : status.online && status.paired ? i18n::tr("Open")
                                             : status.online                  ? i18n::tr("Pair")
                                                             : i18n::tr("Try again")};
        hints[count++] = {ui::Button::triangle, i18n::tr("Search again")};
    }
    else if (screen_ == 1)
    {
        hints[count++] = {ui::Button::dpad, i18n::tr("Choose")};
        if (!apps_.items().empty())
            hints[count++] = {ui::Button::cross, i18n::tr("Start")};
        else
            hints[count++] = {ui::Button::cross, !host ? i18n::tr("PCs")
                                                 : backend.online && !backend.paired
                                                     ? i18n::tr("Pair")
                                                     : i18n::tr("Try again")};
        if (backend.current_app_id)
            hints[count++] = {ui::Button::square, i18n::tr("Stop app")};
        hints[count++] = {ui::Button::circle, i18n::tr("PCs")};
    }
    else if (screen_ == 2)
    {
        hints[count++] = {ui::Button::dpad, i18n::tr("Choose and change")};
        hints[count++] = {ui::Button::circle, i18n::tr("PCs")};
    }
    else
    {
        hints[count++] = {ui::Button::circle, i18n::tr("PCs")};
    }
    ui::draw_hints(list, canvas.fonts, glyphs(paint), hints, count, kRight, true);
}

void View::draw_host_icon(ui::Canvas &canvas, const Rect &row, int index, float focus) const
{
    gfx::DrawList &list = canvas.list;
    const ui::Theme &t = theme_;
    const Color ink = gfx::mix(t.text_muted, t.text, focus);
    const float cx = row.x + 50.0f;
    const float cy = row.cy();
    if (index >= static_cast<int>(model_.config().host_count))
    {
        list.ring(cx, cy, 24.0f, 2.5f, ink);
        list.line(cx - 10.0f, cy, cx + 10.0f, cy, 3.0f, ink);
        list.line(cx, cy - 10.0f, cx, cy + 10.0f, 3.0f, ink);
        return;
    }
    // A monitor on its stand, with the PC's state as a dot on its corner.
    list.bordered_rect({cx - 24.0f, cy - 20.0f, 48.0f, 32.0f}, 5.0f, ink.with_alpha(0.0f), 3.0f,
                       ink);
    list.line(cx, cy + 12.0f, cx, cy + 20.0f, 3.0f, ink);
    list.line(cx - 12.0f, cy + 21.0f, cx + 12.0f, cy + 21.0f, 3.0f, ink);
    const HostStatus status = model_.host_status(static_cast<unsigned>(index));
    const Color dot = !status.known || !status.online ? t.text_muted
                      : status.paired                 ? t.success
                                                      : t.warning;
    list.circle(cx + 24.0f, cy - 20.0f, 9.0f, ui::solid_surface(t));
    if (!status.known || !status.online)
        list.ring(cx + 24.0f, cy - 20.0f, 6.5f, 2.0f, dot);
    else
        list.circle(cx + 24.0f, cy - 20.0f, 6.5f, dot);
}

void View::draw_host_state(ui::Canvas &canvas, const Rect &row, int index) const
{
    if (index >= static_cast<int>(model_.config().host_count))
        return;
    const ui::Theme &t = theme_;
    ui::Painter paint(canvas.list, canvas.fonts, t, canvas.glass);
    const HostStatus status = model_.host_status(static_cast<unsigned>(index));
    const char *text = !status.known    ? i18n::tr("Checking")
                       : !status.online ? i18n::tr("Offline")
                       : status.paired  ? i18n::tr("Ready")
                                        : i18n::tr("Pair to use");
    const Color ink = !status.known || !status.online ? t.text_muted
                      : status.paired                 ? t.success
                                                      : t.warning;
    paint.label(text, row.x + row.w - 28.0f, row.cy() + 8.0f, 22.0f, ink, gfx::Align::right);
}

void View::draw_hosts(ui::Canvas &canvas, ui::Painter &paint) const
{
    const ui::Theme &t = theme_;
    const moonlight_config_t &config = model_.config();
    paint.heading(i18n::tr("Your PCs"), kMargin - 2.0f, 212.0f, 52.0f, paint.page_text());
    if (config.host_count == 0)
    {
        empty_.draw(canvas);
        return;
    }
    char saved[256];
    std::snprintf(saved, sizeof(saved), i18n::tr("%u saved"), config.host_count);
    paint.body(saved, kMargin + paint.heading_width(i18n::tr("Your PCs"), 52.0f) + 22.0f, 212.0f,
               24.0f, paint.page_text_muted());
    hosts_.draw(canvas);

    host_panel_.draw(canvas, kHostPanel);
    const Rect inside = host_inside();
    if (!on_host())
    {
        paint.heading(i18n::tr("Add a PC"), inside.x, inside.y + 44.0f, 40.0f);
        ui::paragraph(
            canvas.list, canvas.fonts.regular,
            i18n::tr("PCs running Sunshine on this network appear by themselves. Add one by "
                     "hand "
                     "when it is on another network, or when discovery is blocked."),
            inside.x, inside.y + 100.0f, 25.0f, inside.w, 38.0f, t.text_muted);
        ui::paragraph(canvas.list, canvas.fonts.regular,
                      i18n::tr("Type its address, for example 192.168.1.50. Add a port "
                               "after a colon if "
                               "Sunshine does not use 47989: 192.168.1.50:48989."),
                      inside.x, inside.y + 250.0f, 25.0f, inside.w, 38.0f, t.text_muted);
        return;
    }
    const unsigned index = static_cast<unsigned>(host_focus());
    const moonlight_config_host_t &host = config.hosts[index];
    const HostStatus status = model_.host_status(index);
    const std::string name = host.name[0] ? host.name : host.address;
    paint.bounded_heading(name, inside.x, inside.y + 44.0f, 44.0f, t.text, 520.0f);
    const float width = std::min(paint.heading_width(name, 44.0f), 520.0f);
    const bool reconnecting = index == config.selected_host && model_.reconnecting();
    const Color state = !status.known || reconnecting ? t.text_muted
                        : !status.online              ? t.text_muted
                        : status.paired               ? t.success
                                                      : t.warning;
    const char *word = reconnecting     ? i18n::tr("Reconnecting")
                       : !status.known  ? i18n::tr("Checking")
                       : !status.online ? i18n::tr("Offline")
                       : status.paired  ? i18n::tr("Online and paired")
                                        : i18n::tr("Online, not paired");
    canvas.list.circle(inside.x + width + 34.0f, inside.y + 30.0f, 7.0f, state);
    paint.label(word, inside.x + width + 52.0f, inside.y + 39.0f, 23.0f, state);

    host_details_.draw(canvas);
    open_games_.draw(canvas);
    host_actions_.draw(canvas);
    remove_.draw(canvas);
}

void View::draw_app_art(ui::Canvas &canvas, const Rect &art, float radius,
                        const ui::CardItem &item) const
{
    gfx::DrawList &list = canvas.list;
    if (item.texture != 0)
    {
        // The poster covers the card: what does not fit is cut evenly.
        const float card = art.w / art.h;
        Rect uv = gfx::kFullUv;
        if (item.image_aspect > card)
        {
            uv.w = card / item.image_aspect;
            uv.x = (1.0f - uv.w) * 0.5f;
        }
        else
        {
            uv.h = item.image_aspect / card;
            uv.y = (1.0f - uv.h) * 0.5f;
        }
        list.image(item.texture, art, uv, Color::rgb(0xffffff), radius);
        return;
    }
    // No box art (yet): a screen drawn from shapes in the theme's colours.
    const ui::Theme &t = theme_;
    list.gradient_rect(art, radius, gfx::mix(t.primary, t.page, 0.55f),
                       gfx::mix(t.primary, t.page, 0.88f));
    const float cx = art.cx();
    const float cy = art.cy() - 10.0f;
    const float w = art.w * 0.52f;
    const float h = w * 0.64f;
    const Color ink = Color::rgb(0xffffff, 0.92f);
    list.bordered_rect({cx - w * 0.5f, cy - h * 0.5f, w, h}, 8.0f, ink.with_alpha(0.0f), 5.0f, ink);
    list.line(cx, cy + h * 0.5f, cx, cy + h * 0.5f + 16.0f, 5.0f, ink);
    list.line(cx - w * 0.24f, cy + h * 0.5f + 18.0f, cx + w * 0.24f, cy + h * 0.5f + 18.0f, 5.0f,
              ink);
}

void View::draw_games(ui::Canvas &canvas, ui::Painter &paint) const
{
    gfx::DrawList &list = canvas.list;
    const ui::Theme &t = theme_;
    const moonlight_backend_snapshot_t &backend = model_.backend();
    const moonlight_config_t &config = model_.config();
    const moonlight_config_host_t *host = model_.selected_host();
    const Color ink = paint.page_text();
    const Color quiet = paint.page_text_muted();

    if (apps_.items().empty())
    {
        paint.heading(i18n::tr("Games"), kMargin - 2.0f, 212.0f, 52.0f, ink);
        const bool waiting = host && (!model_.backend_valid() || model_.busy() == Busy::refreshing);
        if (waiting)
        {
            loading_apps_.draw(canvas);
            paint.body(i18n::tr("Asking the PC for its apps"), 960.0f, 610.0f, 26.0f, quiet,
                       gfx::Align::center);
            return;
        }
        ui::EmptyState state = no_apps_;
        if (!host)
        {
            state.title = i18n::tr("No PC selected");
            state.body = i18n::tr("Choose a PC first, or add one by its address.");
            state.action = i18n::tr("PCs");
        }
        else if (!backend.online)
        {
            state.title = model_.reconnecting() ? i18n::tr("Reconnecting")
                                                : i18n::tr("The PC is not answering");
            state.body = i18n::tr("ProsperoLight keeps trying. Check that Sunshine is running "
                                  "on the PC.");
            state.action = i18n::tr("Try again");
        }
        else if (!backend.paired)
        {
            state.title = i18n::tr("This PC is not paired");
            state.body = i18n::tr("Pair it once with a PIN to see its apps.");
            state.action = i18n::tr("Pair this PC");
        }
        else
        {
            state.title = i18n::tr("No apps on this PC");
            state.body = i18n::tr("Sunshine returned an empty list. Add apps in Sunshine, "
                                  "then try again.");
            state.action = i18n::tr("Try again");
        }
        state.draw(canvas);
        return;
    }

    const moonlight_backend_app_t &app =
        backend.apps[std::min(model_.selected_app(), backend.app_count - 1)];
    const bool running = backend.current_app_id == app.id;
    paint.label(ui::upper(backend.name), kMargin, 186.0f, 20.0f, gfx::mix(t.primary, ink, 0.25f));
    paint.bounded_heading(app.name, kMargin - 3.0f, 268.0f, 76.0f, ink, 1180.0f);
    const bool offline = !backend.online;
    const Color state = offline ? t.text_muted : running ? t.success : quiet;
    list.circle(kMargin + 8.0f, 316.0f, 7.0f, state);
    const float word =
        paint.label(offline   ? (model_.reconnecting() ? i18n::tr("Reconnecting")
                                                       : i18n::tr("The PC is not answering"))
                    : running ? i18n::tr("Running on the PC")
                              : i18n::tr("Ready to start"),
                    kMargin + 26.0f, 325.0f, 24.0f, running && !offline ? t.success : ink);
    char text[512];
    std::snprintf(text, sizeof(text),
                  i18n::tr("%s   \xC2\xB7   %u FPS   \xC2\xB7   %s   \xC2\xB7   %u Mbps"),
                  resolution_name(config.stream_resolution), config.stream_fps, codec_name(config),
                  config.bitrate_mbps);
    paint.body(text, kMargin + 26.0f + word + 30.0f, 325.0f, 24.0f, quiet);
    start_.draw(canvas);
    stop_.draw(canvas);

    shelf_.draw(canvas);
    apps_.draw(canvas);
}

void View::draw_settings(ui::Canvas &canvas, ui::Painter &paint) const
{
    gfx::DrawList &list = canvas.list;
    const ui::Theme &t = theme_;
    paint.heading(i18n::tr("Settings"), kMargin - 2.0f, 212.0f, 52.0f, paint.page_text());
    paint.body(i18n::tr("Changes are saved as you make them"),
               kMargin + paint.heading_width(i18n::tr("Settings"), 52.0f) + 22.0f, 212.0f, 24.0f,
               paint.page_text_muted());
    if (!version_.empty())
        paint.body(i18n::tr("ProsperoLight ") + version_, kRight, 212.0f, 21.0f,
                   paint.page_text_muted(), gfx::Align::right);
    form_.draw(canvas);

    list.push_clip(kProfilePanel);
    profile_panel_.draw(canvas, kProfilePanel);
    const Rect inside = profile_inside();
    paint.label(ui::upper(i18n::tr("This stream")), inside.x, inside.y + 20.0f, 19.0f,
                t.text_muted);
    paint.heading(profile_.label, inside.x, inside.y + 72.0f, 36.0f);
    headroom_.draw(canvas);
    if (warning_.visible())
    {
        warning_.draw(canvas);
    }
    else
    {
        char text[2048];
        if (model_.config().video_codec == MOONLIGHT_VIDEO_CODEC_PYROWAVE)
            std::snprintf(text, sizeof(text), "%s",
                          i18n::tr("Smooth up to 500 Mbps. Wired LAN recommended."));
        else if (model_.config().video_codec == MOONLIGHT_VIDEO_CODEC_H264 && limits_apply_)
            std::snprintf(text, sizeof(text), "%s",
                          i18n::tr("Smooth up to 80 Mbps at this frame rate."));
        else if (limits_apply_)
            std::snprintf(text, sizeof(text),
                          i18n::tr("Smooth up to %.0f Mbps at this frame rate."),
                          static_cast<double>(limit_for(model_.config().stream_fps).smooth));
        else
            std::snprintf(text, sizeof(text), "%s",
                          i18n::tr("No measured decoder limit for this profile."));
        paint.bounded_body(text, inside.x, inside.y + 214.0f, 22.0f, t.text_muted, inside.w);
        profile_details_.draw(canvas);
    }

    list.pop_clip();

    // What the controller does during a stream, in the buttons' own shapes.
    list.push_clip(kShortcutPanel);
    shortcut_panel_.draw(canvas, kShortcutPanel);
    const Rect keys = shortcut_panel_.content_rect(kShortcutPanel).inset(12.0f);
    paint.label(ui::upper(i18n::tr("During a stream")), keys.x, keys.y + 20.0f, 19.0f,
                t.text_muted);
    struct Shortcut
    {
        ui::Button second;
        const char *what;
    };
    const Shortcut kShortcuts[] = {
        {ui::Button::l1, i18n::tr("Leave stream")},
        {ui::Button::r1, i18n::tr("Statistics")},
        {ui::Button::square, i18n::tr("Mouse mode")},
        {ui::Button::triangle, i18n::tr("Keyboard")},
        {ui::Button::left_stick, i18n::tr("Host Back")},
        {ui::Button::right_stick, i18n::tr("Host Guide")},
    };
    const ui::GlyphStyle glyph = t.dark ? ui::GlyphStyle::dark() : ui::GlyphStyle::light();
    for (int i = 0; i < 6; ++i)
    {
        const float cy = keys.y + 36.0f + static_cast<float>(i) * 26.0f;
        const float column = keys.x;
        float x = column;
        ui::draw_button(list, canvas.fonts, glyph, ui::Button::touchpad, x, cy, 22.0f);
        x += ui::button_width(ui::Button::touchpad, 22.0f) + 10.0f;
        x += paint.body("+", x, cy + 8.0f, 24.0f, t.text_muted) + 10.0f;
        ui::draw_button(list, canvas.fonts, glyph, kShortcuts[i].second, x, cy, 22.0f);
        paint.bounded_body(kShortcuts[i].what, column + 118.0f, cy + 7.0f, 20.0f, t.text,
                           keys.w - 118.0f);
    }
    list.pop_clip();
}

// Credits and first steps, after ProsperoEden's About page.
void View::draw_about(ui::Canvas &canvas, ui::Painter &paint) const
{
    gfx::DrawList &list = canvas.list;
    const ui::Theme &t = theme_;
    const ui::FontRef &regular = canvas.fonts.regular;
    const Color rule = t.text_muted.with_alpha(0.28f);
    paint.heading(i18n::tr("About ProsperoLight"), kMargin - 2.0f, 212.0f, 52.0f,
                  paint.page_text());
    paint.body(i18n::tr("Credits and first steps"),
               kMargin + paint.heading_width(i18n::tr("About ProsperoLight"), 52.0f) + 22.0f,
               212.0f, 24.0f, paint.page_text_muted());

    credits_panel_.draw(canvas, kCreditsPanel);
    const Rect left = credits_panel_.content_rect(kCreditsPanel).inset(14.0f);
    const float bottom = left.y + left.h - 6.0f;
    paint.label(ui::upper(i18n::tr("Project credits")), left.x, left.y + 20.0f, 19.0f,
                t.text_muted);
    paint.heading(i18n::tr("Powered by Moonlight"), left.x, left.y + 72.0f, 38.0f);
    ui::paragraph(
        list, regular,
        i18n::tr("ProsperoLight speaks the Moonlight protocol through moonlight-common-c. "
                 "All "
                 "credit for it goes to the Moonlight developers and contributors."),
        left.x, left.y + 118.0f, 24.0f, left.w, 34.0f, t.text, 3);
    paint.label("moonlight-stream.org", left.x, left.y + 226.0f, 24.0f, t.primary);
    list.rounded_rect({left.x, left.y + 252.0f, left.w, 1.0f}, 0.0f, rule);
    paint.label(ui::upper(i18n::tr("Thanks")), left.x, left.y + 288.0f, 19.0f, t.text_muted);
    ui::paragraph(list, regular,
                  i18n::tr("Thanks to the Sunshine developers for the host on the PC, to "
                           "the whole PS5 "
                           "homebrew community, and to every developer whose tools and "
                           "libraries make "
                           "ProsperoLight possible."),
                  left.x, left.y + 326.0f, 24.0f, left.w, 34.0f, t.text, 3);
    list.rounded_rect({left.x, left.y + 426.0f, left.w, 1.0f}, 0.0f, rule);
    paint.label(ui::upper(i18n::tr("PS5 edition")), left.x, left.y + 462.0f, 19.0f, t.text_muted);
    ui::paragraph(list, regular,
                  i18n::tr("ProsperoLight is an unofficial PS5 client brought to you by "
                           "BlackBearReloaded."),
                  left.x, left.y + 500.0f, 24.0f, left.w, 34.0f, t.text, 2);
    paint.body(i18n::tr("Menu sound effects made with ElevenLabs."), left.x, bottom, 20.0f,
               t.text_muted);
    if (!version_.empty())
        paint.label(i18n::tr("Version ") + version_, left.x + left.w, bottom, 20.0f, t.primary,
                    gfx::Align::right);

    start_panel_.draw(canvas, kStartPanel);
    const Rect right = start_panel_.content_rect(kStartPanel).inset(14.0f);
    paint.label(ui::upper(i18n::tr("Getting started")), right.x, right.y + 20.0f, 19.0f,
                t.text_muted);
    paint.heading(i18n::tr("Stream from your PC"), right.x, right.y + 72.0f, 38.0f);
    struct Step
    {
        const char *title;
        const char *body;
    };
    const Step kSteps[] = {
        {i18n::tr("Run Sunshine on the PC"),
         i18n::tr("Keep it open, on the same network as this PS5.")},
        {i18n::tr("Pair once"),
         i18n::tr("Choose the PC in PCs, then type the PIN shown here into Sunshine.")},
        {i18n::tr("Play"), i18n::tr("Pick an app in Games and start it.")},
    };
    for (int i = 0; i < 3; ++i)
    {
        const float y = right.y + 132.0f + static_cast<float>(i) * 78.0f;
        const char number[2] = {static_cast<char>('1' + i), '\0'};
        list.circle(right.x + 20.0f, y + 6.0f, 20.0f, t.primary.with_alpha(0.16f));
        paint.label(number, right.x + 20.0f, y + 14.0f, 22.0f, t.primary, gfx::Align::center);
        paint.label(kSteps[i].title, right.x + 60.0f, y, 25.0f, t.text);
        paint.body(kSteps[i].body, right.x + 60.0f, y + 30.0f, 22.0f, t.text_muted);
    }
    list.rounded_rect({right.x, right.y + 350.0f, right.w, 1.0f}, 0.0f, rule);
    paint.label(ui::upper(i18n::tr("Files on this PS5")), right.x, right.y + 386.0f, 19.0f,
                t.text_muted);
    files_.draw(canvas);
    paint.body(storage_access_
                   ? i18n::tr("An update or a reinstall does not touch them.")
                   : i18n::tr("Kept in the app's own storage: no filesystem access at start-up."),
               right.x, right.y + right.h - 6.0f, 20.0f, t.text_muted);
}

void View::draw_pairing(ui::Canvas &canvas) const
{
    const float shown = pair_fade_.value;
    if (shown <= 0.01f)
        return;
    const ui::Theme &t = theme_;
    gfx::DrawList &list = canvas.list;
    list.rounded_rect({0, 0, gfx::kVirtualWidth, gfx::kVirtualHeight}, 0,
                      Color::rgb(0x000000, 0.55f * shown));
    list.push_opacity(shown);
    const Rect panel = kPairPanel;
    ui::draw_overlay_panel(canvas, t, panel, false, 0.6f);
    ui::Painter paint(list, canvas.fonts, t, canvas.glass);
    const char *name = model_.backend().name[0] ? model_.backend().name : i18n::tr("the PC");
    const bool ready = model_.pairing_pin()[0] != '\0';
    paint.label(ui::upper(i18n::tr("Pairing")), panel.cx(), panel.y + 62.0f, 20.0f, t.text_muted,
                gfx::Align::center);
    paint.bounded_heading(
        ready ? std::string(i18n::tr("Enter this PIN on ")) + name : i18n::tr("Preparing a PIN"),
        panel.cx(), panel.y + 122.0f, 40.0f, t.text, panel.w - 80.0f, gfx::Align::center);
    paint.body(ready ? i18n::tr("Open Sunshine on the PC, choose PIN, and type the code.")
                     : i18n::tr("Asking Sunshine for a pairing request."),
               panel.cx(), panel.y + 168.0f, 24.0f, t.text_muted, gfx::Align::center);
    pin_.draw(canvas);
    pair_timer_.draw(canvas);
    paint.body(i18n::tr("This closes by itself when Sunshine accepts the PIN."), panel.cx(),
               panel.y + panel.h - 36.0f, 21.0f, t.text_muted, gfx::Align::center);
    list.pop_opacity();
}

bool View::take_update_exit()
{
    const bool exit = update_exit_;
    update_exit_ = false;
    return exit;
}

// The question. With release notes it has a third answer, What's new.
void View::open_update_offer(ui::Feedback &feedback, bool on_notes)
{
    const bool with_notes = !update_notes_.content().empty();
    ui::DialogContent content;
    content.icon = ui::StatusKind::info;
    content.title = i18n::tr("Update available");
    char size[48] = "";
    if (update_offer_.size != 0)
        std::snprintf(size, sizeof(size), " (%.0f MB)",
                      static_cast<double>(update_offer_.size) / 1e6);
    content.body = std::string(i18n::tr("ProsperoLight ")) + update_offer_.version +
                   i18n::tr(" is out") + size +
                   i18n::tr(".\nUpdate now downloads and checks it. ProsperoLight then closes "
                            "while the new version is put in place.");
    content.buttons.push_back({i18n::tr("Skip")});
    if (with_notes)
        content.buttons.push_back({i18n::tr("What's new")});
    content.buttons.push_back({i18n::tr("Update now"), ui::ButtonKind::primary});
    content.default_button =
        with_notes && on_notes ? 1 : static_cast<int>(content.buttons.size()) - 1;
    update_dialog_.open(std::move(content), feedback);
    update_ui_ = UpdateUi::offer;
}

void View::begin_update(ui::Feedback &feedback)
{
    update_progress_ = UpdateProgress{};
    update_ring_.set_value(0.0f, true);
    if (update_actions_.begin && update_actions_.begin())
        update_ui_ = UpdateUi::working;
    else
        open_update_failure(i18n::tr("The update could not start."), feedback);
}

// What's new: the release notes, to read before deciding.
void View::draw_update_notes(ui::Canvas &canvas) const
{
    const float shown = update_notes_fade_.value;
    if (shown <= 0.01f)
        return;
    const ui::Theme &t = theme_;
    gfx::DrawList &list = canvas.list;
    list.rounded_rect({0, 0, gfx::kVirtualWidth, gfx::kVirtualHeight}, 0,
                      Color::rgb(0x000000, 0.55f * shown));
    list.push_opacity(shown);
    const Rect panel = kNotesPanel;
    ui::draw_overlay_panel(canvas, t, panel, false, 0.6f);
    ui::Painter paint(list, canvas.fonts, t, canvas.glass);
    paint.label(ui::upper(std::string(i18n::tr("ProsperoLight ")) + update_offer_.version),
                panel.x + 64.0f, panel.y + 62.0f, 20.0f, t.text_muted);
    paint.heading(i18n::tr("What's new"), panel.x + 64.0f, panel.y + 116.0f, 40.0f);
    update_notes_.draw(canvas);
    // What the buttons do here, as the buttons themselves.
    const ui::GlyphStyle glyph = t.dark ? ui::GlyphStyle::dark() : ui::GlyphStyle::light();
    struct Hint
    {
        ui::Button button;
        const char *what;
    };
    const Hint kHints[] = {{ui::Button::cross, i18n::tr("Update now")},
                           {ui::Button::circle, i18n::tr("Back")}};
    float x = panel.x + 64.0f;
    const float cy = panel.y + panel.h - 74.0f;
    for (const Hint &hint : kHints)
    {
        ui::draw_button(list, canvas.fonts, glyph, hint.button, x, cy, 32.0f);
        x += ui::button_width(hint.button, 32.0f) + 12.0f;
        x += paint.body(hint.what, x, cy + 8.0f, 24.0f, t.text) + 44.0f;
    }
    list.pop_opacity();
}

void View::open_update_failure(const char *reason, ui::Feedback &feedback)
{
    ui::DialogContent content;
    content.icon = ui::StatusKind::danger;
    content.title = i18n::tr("The update was not installed");
    content.body = std::string(reason && reason[0] ? reason : i18n::tr("Something went wrong.")) +
                   i18n::tr("\nProsperoLight is as it was.");
    content.buttons = {{i18n::tr("Close")}, {i18n::tr("Try again"), ui::ButtonKind::primary}};
    content.default_button = 1;
    update_dialog_.open(std::move(content), feedback);
    update_ui_ = UpdateUi::failed;
}

// The update's own input: while it shows, nothing under it is reached.
void View::update_modal(const InputFrame &input, float dt, ui::Feedback &feedback)
{
    switch (update_ui_)
    {
    case UpdateUi::offer:
    case UpdateUi::failed:
    {
        // Skip, [What's new,] Update now; or Close, Try again.
        const bool offer = update_ui_ == UpdateUi::offer;
        const bool with_notes = offer && !update_notes_.content().empty();
        const ui::Event event = update_dialog_.handle(input, feedback);
        const int choice = update_dialog_.choice();
        if (event == ui::Event::activated && choice == (with_notes ? 2 : 1))
            begin_update(feedback);
        else if (event == ui::Event::activated && with_notes && choice == 1)
            update_ui_ = UpdateUi::notes;
        else if (!update_dialog_.is_open())
            update_ui_ = UpdateUi::hidden;
        break;
    }
    case UpdateUi::notes:
        if ((input.pressed & hui::action_bit(hui::Action::confirm)) != 0)
            begin_update(feedback);
        else if ((input.pressed & hui::action_bit(hui::Action::back)) != 0)
            open_update_offer(feedback, true);
        else
            (void)update_notes_.handle(input, feedback);
        break;
    case UpdateUi::working:
    {
        if (update_actions_.poll)
            update_actions_.poll(&update_progress_);
        const UpdateProgress &now = update_progress_;
        update_ring_.style.mode =
            now.total != 0 ? ui::ProgressMode::determinate : ui::ProgressMode::indeterminate;
        if (now.total != 0)
            update_ring_.set_value(
                std::min(1.0f, static_cast<float>(static_cast<double>(now.done) /
                                                  static_cast<double>(now.total))));
        if (now.phase == UpdatePhase::ready)
        {
            if (update_actions_.apply && update_actions_.apply())
            {
                update_ui_ = UpdateUi::closing;
                update_closing_age_ = 0.0f;
                feedback.play(audio::Cue::saved);
            }
            else
            {
                if (update_actions_.finish)
                    update_actions_.finish();
                open_update_failure(i18n::tr("The update helper did not answer."), feedback);
            }
        }
        else if (now.phase == UpdatePhase::failed || now.phase == UpdatePhase::cancelled)
        {
            const bool cancelled = now.phase == UpdatePhase::cancelled;
            const std::string reason = now.error;
            if (update_actions_.finish)
                update_actions_.finish();
            if (cancelled)
                update_ui_ = UpdateUi::hidden;
            else
                open_update_failure(reason.c_str(), feedback);
        }
        else if ((input.pressed & hui::action_bit(hui::Action::back)) != 0 &&
                 update_actions_.cancel)
        {
            update_actions_.cancel();
            feedback.play(audio::Cue::modal_close);
        }
        break;
    }
    case UpdateUi::closing:
        // Long enough to read the last line; the helper waits for the app.
        // Asked once: the launcher closes as soon as it hears it.
        if (update_closing_age_ <= 1.5f && update_closing_age_ + dt > 1.5f)
            update_exit_ = true;
        update_closing_age_ += dt;
        break;
    default:
        break;
    }
}

// The update at work: a ring, what it is doing, and how much is left.
void View::draw_update(ui::Canvas &canvas) const
{
    const float shown = update_fade_.value;
    if (shown <= 0.01f)
        return;
    const ui::Theme &t = theme_;
    gfx::DrawList &list = canvas.list;
    list.rounded_rect({0, 0, gfx::kVirtualWidth, gfx::kVirtualHeight}, 0,
                      Color::rgb(0x000000, 0.55f * shown));
    list.push_opacity(shown);
    const Rect panel = kUpdatePanel;
    ui::draw_overlay_panel(canvas, t, panel, false, 0.6f);
    ui::Painter paint(list, canvas.fonts, t, canvas.glass);
    const bool closing = update_ui_ == UpdateUi::closing;
    const UpdateProgress &now = update_progress_;
    const char *doing = closing                                 ? i18n::tr("Updating")
                        : now.phase == UpdatePhase::downloading ? i18n::tr("Downloading")
                        : now.phase == UpdatePhase::unpacking   ? i18n::tr("Unpacking")
                        : now.phase == UpdatePhase::ready || now.phase == UpdatePhase::applying
                            ? i18n::tr("Finishing")
                            : i18n::tr("Preparing");
    paint.label(ui::upper(std::string(i18n::tr("ProsperoLight ")) + update_offer_.version),
                panel.cx(), panel.y + 62.0f, 20.0f, t.text_muted, gfx::Align::center);
    paint.heading(doing, panel.cx(), panel.y + 122.0f, 40.0f, gfx::Align::center);
    update_ring_.draw(canvas);
    char line[2048];
    std::snprintf(line, sizeof(line), "%s", i18n::tr("Starting the update helper"));
    if (closing)
        std::snprintf(line, sizeof(line), "%s", i18n::tr("ProsperoLight closes now."));
    else if (now.total != 0)
        std::snprintf(line, sizeof(line), i18n::tr("%.1f of %.1f MB%s%s"),
                      static_cast<double>(now.done) / 1e6, static_cast<double>(now.total) / 1e6,
                      now.time_left[0] ? "  \xC2\xB7  " : "", now.time_left);
    paint.body(line, panel.cx(), panel.y + 442.0f, 26.0f, t.text, gfx::Align::center);
    if (closing)
    {
        paint.body(i18n::tr("Open it again when the console says it was updated."), panel.cx(),
                   panel.y + panel.h - 54.0f, 22.0f, t.text_muted, gfx::Align::center);
    }
    else
    {
        // Nothing is changed before the download is checked; the way out is shown as its button.
        const ui::GlyphStyle glyph = t.dark ? ui::GlyphStyle::dark() : ui::GlyphStyle::light();
        const char *hint = i18n::tr("Cancel. Nothing is changed until the download is checked.");
        const float width = ui::button_width(ui::Button::circle, 30.0f) + 12.0f +
                            canvas.fonts.regular.font->measure(hint, 22.0f);
        float x = panel.cx() - width * 0.5f;
        ui::draw_button(list, canvas.fonts, glyph, ui::Button::circle, x, panel.y + panel.h - 70.0f,
                        30.0f);
        x += ui::button_width(ui::Button::circle, 30.0f) + 12.0f;
        paint.body(hint, x, panel.y + panel.h - 62.0f, 22.0f, t.text_muted);
    }
    list.pop_opacity();
}

View::ConnectBar View::connecting_bar() const
{
    return {kConnectBar, theme_.primary, load_progress_};
}

// The connecting screen's bar. Its track and its label belong to the picture
// the stream is given; the fill is drawn here first and by the stream after.
void View::draw_connect_bar(ui::Canvas &canvas) const
{
    const float shown = loader_.opacity();
    if (shown <= 0.01f)
        return;
    gfx::DrawList &list = canvas.list;
    list.push_opacity(shown);
    ui::Painter paint(list, canvas.fonts, theme_, 0);
    const Color ink = Color::rgb(0xffffff);
    const float radius = kConnectBar.h * 0.5f;
    paint.label(i18n::tr("Connecting"), kConnectBar.x, kConnectBar.y - 20.0f, 22.0f,
                ink.with_alpha(0.85f));
    list.rounded_rect(kConnectBar, radius, ink.with_alpha(0.16f));
    if (!plate_ && load_progress_ > 0.0f)
        list.rounded_rect({kConnectBar.x, kConnectBar.y,
                           std::max(kConnectBar.w * load_progress_, kConnectBar.h), kConnectBar.h},
                          radius, theme_.primary);
    list.pop_opacity();
}

// The connecting screen's tip, with the buttons drawn as the controller shows
// them.
void View::draw_loader_tip(ui::Canvas &canvas) const
{
    const float shown = loader_.opacity();
    if (shown <= 0.01f)
        return;
    gfx::DrawList &list = canvas.list;
    list.push_opacity(shown);
    ui::Painter paint(list, canvas.fonts, theme_, 0);
    const Color ink = Color::rgb(0xffffff);
    paint.label(i18n::tr("Tip"), kMargin, 780.0f, 21.0f, ink.with_alpha(0.7f));
    const ui::GlyphStyle glyph = ui::GlyphStyle::dark();
    const float cy = 826.0f;
    float x = kMargin;
    ui::draw_button(list, canvas.fonts, glyph, ui::Button::touchpad, x, cy, 38.0f);
    x += ui::button_width(ui::Button::touchpad, 38.0f) + 12.0f;
    x += paint.body("+", x, cy + 9.0f, 26.0f, ink.with_alpha(0.7f)) + 12.0f;
    ui::draw_button(list, canvas.fonts, glyph, ui::Button::l1, x, cy, 38.0f);
    x += ui::button_width(ui::Button::l1, 38.0f) + 18.0f;
    paint.body(i18n::tr("ends the stream and returns here"), x, cy + 9.0f, 26.0f, ink);
    list.pop_opacity();
}

} // namespace launcher
