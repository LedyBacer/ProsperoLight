/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "launcher/launcher_view.hpp"

#include "ui/widgets.hpp"
#include "presentation_preferences.hpp"
#include "stream_profile.hpp"
#include "ui_sound_preferences.hpp"
#include "lan_http_report.hpp"

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
};

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
    t.family = "Frosted glass";
    t.summary = "ProsperoLight's night blue under frosted panels";
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
    storage_access_ = storage.access;
    files_.set_items(
        {{"Settings", storage.settings}, {"Pairing", storage.pairing}, {"Logs", storage.logs}});
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
    tabs_.set_tabs({{"PCs"}, {"Games"}, {"Settings"}, {"About"}});
    tabs_.style.kind = ui::TabKind::pill;
    tabs_.style.on_page = true;
    tabs_.style.height = 52.0f;
    tabs_.style.text_size = 25.0f;
    tabs_.style.padding = 28.0f;
    tabs_.set_bounds({470.0f, 62.0f, 680.0f, 52.0f});
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

    host_actions_.set_items({{"Change port"}, {"Unpair"}});
    host_actions_.style.exits.left = true;
    host_actions_.style.exits.right = true;

    remove_.label = "Remove";
    remove_.hint = "Hold";
    remove_.style.variant = ui::HoldVariant::fill;
    remove_.style.role = ui::ButtonRole::secondary;
    remove_.style.size = ui::ButtonSize::medium;

    empty_.title = "No PCs yet";
    empty_.body = "Start Sunshine on a PC on this network and it appears here, or add it by its "
                  "address.";
    empty_.action = "Add a PC";
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
    stop_.label = "Stop app";
    stop_.glyph = ui::Button::square;
    stop_.style.role = ui::ButtonRole::secondary;
    stop_.style.size = ui::ButtonSize::large;
    stop_.set_bounds({kMargin + 350.0f, 374.0f, 250.0f, 72.0f});
    shelf_.title = "Apps on this PC";
    shelf_.style.rule = ui::SectionRule::trailing;
    shelf_.set_bounds({kMargin, 494.0f, kRight - kMargin, 40.0f});
    no_apps_.style.max_text_width = 640.0f;
    no_apps_.style.title_size = 36.0f;
    no_apps_.style.body_size = 24.0f;
    no_apps_.set_bounds({460.0f, 300.0f, 1000.0f, 440.0f});
    loading_apps_.style.kind = ui::SpinnerKind::arc;
    loading_apps_.set_bounds({930.0f, 500.0f, 60.0f, 60.0f});

    // ---- Settings ----
    form_.add_header("Video");
    form_
        .add_choice(kResolution, "Resolution",
                    {"1920 \xC3\x97 1080", "2560 \xC3\x97 1440", "3840 \xC3\x97 2160"}, 0)
        .description = "The picture Sunshine encodes. 1440p is scaled to the 4K output.";
    form_.add_action(kFrameRate, "Frame rate").description =
        "Open to enter 30-120 FPS. Above 60 FPS uses high-refresh output.";
    form_.add_choice(kCodec, "Video codec", {"H.264", "HEVC", "PyroWave"}, 0).description =
        "PyroWave needs a compatible host, high bitrate and wired LAN.";
    form_.add_choice(kChroma, "Chroma sampling", {"4:2:0", "4:4:4"}, 0).description =
        "4:4:4 is available with PyroWave; native codecs use 4:2:0.";
    form_.add_toggle(kHdr, "HDR", false).description =
        "HDR10 through HEVC Main10 or 10-bit PyroWave, when advertised by the PC.";
    form_.add_action(kBitrate, "Bitrate").description = "Open to enter 1-1000 Mbps.";
    form_.add_header("Sound");
    form_.add_choice(kAudio, "Audio", {"Stereo", "5.1 surround"}, 0).description =
        "48 kHz Opus, decoded on the console.";
    form_.add_toggle(kUiSound, "Menu sounds", true).description =
        "Menu navigation and confirmation sounds.";
    form_.add_header("Display");
    form_.add_choice(kArea, "Picture size", {"TV safe", "Edge to edge"}, 0).description =
        "TV safe keeps a margin for televisions that crop the picture.";
    form_.add_toggle(kVsync, "V-Sync", true).description =
        "Off shows each frame at once: lower latency, visible tearing.";
    form_.add_choice(kPacing, "Frame pacing", {"Unpaced", "Paced", "Paced+VRR"}, 1).description =
        "Smooth frame timing; VRR uses fixed refresh if unavailable.";
    form_.add_header("Decoder");
    form_.add_choice(kPipeline, "Pipeline", {"Classic", "Adaptive (experimental)"}, 0).description =
        "Classic decodes one frame at a time. Adaptive overlaps frames when decoding falls behind.";
    form_
        .add_stepper(kCores, "CPU cores", MOONLIGHT_DECODER_CORES_DEFAULT,
                     MOONLIGHT_DECODER_CORES_MIN, MOONLIGHT_DECODER_CORES_MAX)
        .description = "Cores reserved for decoding; the stream uses the rest.";
    form_.add_header("Diagnostics");
    form_.add_toggle(kLogging, "Diagnostic logs", true).description =
        "Bounded logs and output interval traces saved after the stream.";
    form_.style.row_height = 66.0f;
    form_.style.header_height = 54.0f;
    form_.style.label_size = 26.0f;
    form_.style.control_width = 440.0f;
    form_.style.number_width = 124.0f;
    form_.style.highlight.kind = ui::HighlightKind::tint;
    form_.set_bounds({kMargin - 20.0f, kContentTop - 8.0f, 1060.0f, 720.0f});

    headroom_.label = "Decoder load";
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
    pair_timer_.label = "left";
    pair_timer_.set_bounds({kPairPanel.cx() - 60.0f, kPairPanel.y + 368.0f, 120.0f, 120.0f});

    number_prompt_.style.width = 560.0f;
    number_prompt_.style.max_length = 4;
    number_prompt_.style.auto_capital = false;
    number_prompt_.style.allow_empty = false;
    number_prompt_.style.key_height = 66.0f;
    number_prompt_.style.buttons = false;
    number_prompt_.keyboard.set_layouts({ui::KeyboardLayout::numeric()});

    port_prompt_.style.width = 560.0f;
    port_prompt_.style.max_length = 5;
    port_prompt_.style.auto_capital = false;
    port_prompt_.style.allow_empty = true;
    port_prompt_.style.key_height = 66.0f;
    port_prompt_.style.buttons = false;
    port_prompt_.set_title("Sunshine port");
    port_prompt_.field.set_helper("Sunshine's default is 47989");
    port_prompt_.keyboard.set_layouts({ui::KeyboardLayout::numeric()});

    host_prompt_.style.width = 560.0f;
    host_prompt_.style.max_length = 21;
    host_prompt_.style.auto_capital = false;
    host_prompt_.style.key_height = 62.0f;
    host_prompt_.style.buttons = false;
    host_prompt_.style.empty_error = "Type the PC's address";
    host_prompt_.set_title("Add a PC");
    host_prompt_.field.set_label("Address of the PC");
    host_prompt_.field.set_helper("For example 192.168.1.50, or 192.168.1.50:48989");
    host_prompt_.keyboard.set_layouts({address_layout()});

    unpair_dialog_.style.width = 720.0f;

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
    number_prompt_.style.theme = t;
    host_prompt_.style.theme = t;
    unpair_dialog_.style.theme = t;
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
    add.title = "Add a PC";
    add.subtitle = "By address, with a port if needed";
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
    char text[64];
    std::vector<ui::DetailItem> items;
    items.push_back({"Address", host.address});
    std::snprintf(text, sizeof(text),
                  port == MOONLIGHT_CONFIG_DEFAULT_HTTP_PORT ? "%u (Sunshine's default)" : "%u",
                  port);
    items.push_back({"Port", text});
    items.push_back({"Connection", !status.known   ? "Checking"
                                   : status.online ? "Local network"
                                                   : "Not answering"});
    items.push_back({"Pairing", !status.known || !status.online ? "Unknown"
                                : status.paired                 ? "Paired with this PS5"
                                                                : "Not paired yet"});
    if (status.online && status.paired)
    {
        std::snprintf(text, sizeof(text), "%u available", status.app_count);
        items.push_back({"Apps", text});
        items.push_back({"Running now", status.current_app_id == 0 ? "Nothing"
                                        : status.running[0]        ? status.running
                                                                   : "An app"});
    }
    host_details_.set_items(std::move(items));
    open_games_.label = status.online && status.paired ? "Open games"
                        : status.online                ? "Pair this PC"
                                                       : "Try again";
    host_actions_.item(1).label = status.online && status.paired ? "Unpair" : "Pair";
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
    start_.label = running ? "Resume stream" : "Start stream";
    stop_.set_disabled(backend.current_app_id == 0 || model_.busy() == Busy::stopping);
}

void View::sync_settings_from_config()
{
    const moonlight_config_t &config = model_.config();
    form_.set_choice(kResolution, static_cast<int>(std::min(config.stream_resolution, 2u)));
    form_.set_value_text(kFrameRate, std::to_string(config.stream_fps) + " FPS");
    form_.set_choice(kCodec, static_cast<int>(std::min(config.video_codec, 2u)));
    form_.set_choice(kChroma, config.chroma_sampling == MOONLIGHT_CHROMA_444 ? 1 : 0);
    form_.row(kChroma)->disabled = config.video_codec != MOONLIGHT_VIDEO_CODEC_PYROWAVE;
    form_.set_toggle(kHdr, config.hdr_enabled != 0);
    form_.set_value_text(kBitrate, std::to_string(config.bitrate_mbps) + " Mbps");
    form_.set_choice(kPacing, static_cast<int>(moonlight::presentation_mode()));
    form_.set_toggle(kLogging, prosperolight_logs_enabled() != 0);
    form_.set_toggle(kUiSound, prosperolight::ui_sound_enabled());
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
    case kPacing:
        if (!moonlight::save_presentation_mode(static_cast<unsigned>(form_.choice_index(kPacing))))
            toasts_.push(ui::StatusKind::danger, "Could not save frame pacing", "Try again.");
        sync_settings_from_config();
        return;
    case kLogging:
        if (!prosperolight_logs_set_enabled(form_.toggle_value(kLogging)))
            toasts_.push(ui::StatusKind::danger, "Could not save logging", "Try again.");
        sync_settings_from_config();
        return;
    case kUiSound:
        if (!prosperolight::ui_sound_set_enabled(form_.toggle_value(kUiSound)))
            toasts_.push(ui::StatusKind::danger, "Could not save menu sounds", "Try again.");
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
    // The limits were measured for 4K HEVC; nothing is claimed for the rest.
    limits_apply_ = config.stream_resolution == MOONLIGHT_STREAM_RESOLUTION_2160P && hevc;
    const float scale = limits_apply_ ? limit.freezes * 1.3f : 1000.0f;
    headroom_.label = limits_apply_ ? "Decoder load" : "Requested bitrate";
    headroom_.style.value_scale = scale;
    headroom_.style.warning_at = limits_apply_ ? (limit.smooth + 2.0f) / scale : 2.0f;
    headroom_.style.danger_at = limits_apply_ ? limit.freezes / scale : 2.0f;
    headroom_.style.zone_strip = limits_apply_;
    headroom_.set_value(std::min(bitrate / scale, 1.0f), snap);

    const bool fast = config.stream_fps > MOONLIGHT_STREAM_FPS_60;
    std::vector<ui::DetailItem> items;
    items.push_back({"TV output", fast ? "119.88 Hz" : "59.94 Hz"});
    items.push_back({"Codec", pyro                 ? codec_name(config)
                              : config.hdr_enabled ? "HEVC Main10, HDR10"
                              : hevc               ? "HEVC Main"
                                                   : "H.264 High"});
    items.push_back({"Sound", config.audio_configuration == MOONLIGHT_AUDIO_51_SURROUND
                                  ? "5.1 surround, 48 kHz"
                                  : "Stereo, 48 kHz"});
    profile_details_.set_items(std::move(items));

    const bool online = model_.backend_valid() && backend.online;
    const bool no_hdr = online && !pyro && config.hdr_enabled && !backend.main10_supported;
    const bool no_hevc = online && hevc && !backend.hevc_supported;
    const auto selected_profile = moonlight::resolve_stream_profile(
        config.video_codec, config.chroma_sampling, config.hdr_enabled != 0);
    const bool no_pyro =
        online && pyro && !(backend.pyrowave_profiles & selected_profile.capability);
    char text[200];
    if (no_hdr || no_hevc || no_pyro)
    {
        warning_.title = no_pyro  ? "PyroWave profile unavailable"
                         : no_hdr ? "This PC cannot encode HDR"
                                  : "This PC cannot encode HEVC";
        std::snprintf(text, sizeof(text), "%s does not advertise it. The stream will not start.",
                      backend.name[0] ? backend.name : "The PC");
    }
    else
    {
        warning_.title = "Above the decoder's limit";
        std::snprintf(text, sizeof(text),
                      "Smooth up to %.0f Mbps. Above %.0f Mbps the picture freezes about once a "
                      "second.",
                      static_cast<double>(limit.smooth), static_cast<double>(limit.freezes));
    }
    warning_.body = text;
    const bool warn = no_pyro || no_hdr || no_hevc || (limits_apply_ && bitrate > limit.smooth);
    warning_.set_shown(warn, snap);

    static const char *const kShort[] = {"1080p", "1440p", "4K"};
    char chip[64];
    std::snprintf(chip, sizeof(chip), "%s  \xC2\xB7  %u FPS%s",
                  kShort[std::min(config.stream_resolution, 2u)], config.stream_fps,
                  config.hdr_enabled ? "  \xC2\xB7  HDR" : "");
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
        toasts_.push(ui::StatusKind::danger, "The stream ended", pending_error_, 10.0f);
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

    if (launching_)
    {
        load_progress_ =
            std::min(load_progress_ + dt / kLaunchSeconds * kHandoverProgress, kHandoverProgress);
        if (load_progress_ >= kHandoverProgress)
            start_stream_ = true;
    }
    else if (number_prompt_.is_open())
    {
        if (number_prompt_.handle(input, feedback) == ui::Event::activated)
        {
            const auto text = number_prompt_.text();
            unsigned value = 0;
            bool valid = !text.empty() && text.size() <= 4;
            for (char ch : text)
            {
                if (ch < '0' || ch > '9')
                    valid = false;
                else
                    value = value * 10 + static_cast<unsigned>(ch - '0');
            }
            const unsigned minimum = number_setting_ == kFrameRate ? MOONLIGHT_STREAM_FPS_MIN : 1u;
            const unsigned maximum =
                number_setting_ == kFrameRate ? MOONLIGHT_STREAM_FPS_MAX : 1000u;
            if (valid && value >= minimum && value <= maximum)
            {
                auto &config = model_.settings();
                if (number_setting_ == kFrameRate)
                    config.stream_fps = value;
                else
                    config.bitrate_mbps = value;
                model_.SettingsChanged();
                sync_settings_from_config();
                sync_profile(false);
                feedback.play(audio::Cue::saved);
            }
            else
            {
                number_prompt_.open(feedback, text);
                number_prompt_.field.set_error("Enter " + std::to_string(minimum) + "-" +
                                               std::to_string(maximum));
            }
        }
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
    files_.update(dt);
    headroom_.update(dt);
    profile_details_.update(dt);
    warning_.update(dt);
    pin_.update(dt);
    pair_timer_.update(dt);
    port_prompt_.update(dt);
    number_prompt_.update(dt);
    host_prompt_.update(dt);
    unpair_dialog_.update(dt);
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
                port_prompt_.field.set_label(std::string("Port of ") +
                                             (host && host->name[0] ? host->name : "this PC"));
                port_prompt_.open(feedback, port);
            }
            else if (model_.backend().paired)
            {
                ui::DialogContent content;
                content.icon = ui::StatusKind::question;
                content.title = std::string("Unpair ") +
                                (model_.backend().name[0] ? model_.backend().name : "this PC") +
                                "?";
                content.body = "This PS5 will need a new PIN to use it again.";
                content.buttons = {{"Cancel"}, {"Unpair", ui::ButtonKind::primary, true}};
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
                               ? "Resume stream"
                               : "Start stream";
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
    char text[200];
    std::snprintf(text, sizeof(text),
                  "%s  \xC2\xB7  %s  \xC2\xB7  %u FPS  \xC2\xB7  %s  \xC2\xB7  %u Mbps",
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
    const auto event = form_.handle(input, feedback);
    if (event == ui::Event::changed)
        apply_setting(form_.changed_id());
    else if (event == ui::Event::activated)
    {
        number_setting_ = form_.changed_id();
        if (number_setting_ != kFrameRate && number_setting_ != kBitrate)
            return;
        const bool fps = number_setting_ == kFrameRate;
        number_prompt_.style.max_length = fps ? 3 : 4;
        number_prompt_.set_title(fps ? "Stream frame rate" : "Video bitrate");
        number_prompt_.field.set_helper(fps ? "30-120 FPS" : "1-1000 Mbps");
        number_prompt_.open(feedback, std::to_string(fps ? model_.config().stream_fps
                                                         : model_.config().bitrate_mbps));
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
    number_prompt_.draw(above);
    host_prompt_.draw(above);
    unpair_dialog_.draw(above);
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
        paint.body(busy == Busy::searching ? "Searching this network for Sunshine"
                                           : "No PC selected",
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
        const char *state = busy == Busy::stopping     ? "Stopping the app"
                            : busy == Busy::unpairing  ? "Unpairing"
                            : busy == Busy::refreshing ? "Checking"
                            : !valid                   ? "Checking"
                            : model_.reconnecting()    ? "Reconnecting"
                            : ready                    ? "Sunshine ready"
                            : backend.online           ? "Pairing required"
                                                       : "Not answering";
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
        hints[count++] = {ui::Button::dpad, "Choose"};
        hints[count++] = {ui::Button::cross, !on_host() || model_.config().host_count == 0
                                                 ? "Add a PC"
                                             : host_zone_ == 3                ? "Hold to remove"
                                             : host_zone_ == 2                ? "Select"
                                             : status.online && status.paired ? "Open"
                                             : status.online                  ? "Pair"
                                                                              : "Try again"};
        hints[count++] = {ui::Button::triangle, "Search again"};
    }
    else if (screen_ == 1)
    {
        hints[count++] = {ui::Button::dpad, "Choose"};
        if (!apps_.items().empty())
            hints[count++] = {ui::Button::cross, "Start"};
        else
            hints[count++] = {ui::Button::cross, !host                               ? "PCs"
                                                 : backend.online && !backend.paired ? "Pair"
                                                                                     : "Try again"};
        if (backend.current_app_id)
            hints[count++] = {ui::Button::square, "Stop app"};
        hints[count++] = {ui::Button::circle, "PCs"};
    }
    else if (screen_ == 2)
    {
        hints[count++] = {ui::Button::dpad, "Choose and change"};
        hints[count++] = {ui::Button::circle, "PCs"};
    }
    else
    {
        hints[count++] = {ui::Button::circle, "PCs"};
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
    const char *text = !status.known    ? "Checking"
                       : !status.online ? "Offline"
                       : status.paired  ? "Ready"
                                        : "Pair to use";
    const Color ink = !status.known || !status.online ? t.text_muted
                      : status.paired                 ? t.success
                                                      : t.warning;
    paint.label(text, row.x + row.w - 28.0f, row.cy() + 8.0f, 22.0f, ink, gfx::Align::right);
}

void View::draw_hosts(ui::Canvas &canvas, ui::Painter &paint) const
{
    const ui::Theme &t = theme_;
    const moonlight_config_t &config = model_.config();
    paint.heading("Your PCs", kMargin - 2.0f, 212.0f, 52.0f, paint.page_text());
    if (config.host_count == 0)
    {
        empty_.draw(canvas);
        return;
    }
    char saved[24];
    std::snprintf(saved, sizeof(saved), "%u saved", config.host_count);
    paint.body(saved, kMargin + paint.heading_width("Your PCs", 52.0f) + 22.0f, 212.0f, 24.0f,
               paint.page_text_muted());
    hosts_.draw(canvas);

    host_panel_.draw(canvas, kHostPanel);
    const Rect inside = host_inside();
    if (!on_host())
    {
        paint.heading("Add a PC", inside.x, inside.y + 44.0f, 40.0f);
        ui::paragraph(canvas.list, canvas.fonts.regular,
                      "PCs running Sunshine on this network appear by themselves. Add one by hand "
                      "when it is on another network, or when discovery is blocked.",
                      inside.x, inside.y + 100.0f, 25.0f, inside.w, 38.0f, t.text_muted);
        ui::paragraph(canvas.list, canvas.fonts.regular,
                      "Type its address, for example 192.168.1.50. Add a port after a colon if "
                      "Sunshine does not use 47989: 192.168.1.50:48989.",
                      inside.x, inside.y + 250.0f, 25.0f, inside.w, 38.0f, t.text_muted);
        return;
    }
    const unsigned index = static_cast<unsigned>(host_focus());
    const moonlight_config_host_t &host = config.hosts[index];
    const HostStatus status = model_.host_status(index);
    const std::string name =
        canvas.fonts.display.font->fit(host.name[0] ? host.name : host.address, 44.0f, 520.0f);
    paint.heading(name, inside.x, inside.y + 44.0f, 44.0f);
    const float width = paint.heading_width(name, 44.0f);
    const bool reconnecting = index == config.selected_host && model_.reconnecting();
    const Color state = !status.known || reconnecting ? t.text_muted
                        : !status.online              ? t.text_muted
                        : status.paired               ? t.success
                                                      : t.warning;
    const char *word = reconnecting     ? "Reconnecting"
                       : !status.known  ? "Checking"
                       : !status.online ? "Offline"
                       : status.paired  ? "Online and paired"
                                        : "Online, not paired";
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
        paint.heading("Games", kMargin - 2.0f, 212.0f, 52.0f, ink);
        const bool waiting = host && (!model_.backend_valid() || model_.busy() == Busy::refreshing);
        if (waiting)
        {
            loading_apps_.draw(canvas);
            paint.body("Asking the PC for its apps", 960.0f, 610.0f, 26.0f, quiet,
                       gfx::Align::center);
            return;
        }
        ui::EmptyState state = no_apps_;
        if (!host)
        {
            state.title = "No PC selected";
            state.body = "Choose a PC first, or add one by its address.";
            state.action = "PCs";
        }
        else if (!backend.online)
        {
            state.title = model_.reconnecting() ? "Reconnecting" : "The PC is not answering";
            state.body = "ProsperoLight keeps trying. Check that Sunshine is running on the PC.";
            state.action = "Try again";
        }
        else if (!backend.paired)
        {
            state.title = "This PC is not paired";
            state.body = "Pair it once with a PIN to see its apps.";
            state.action = "Pair this PC";
        }
        else
        {
            state.title = "No apps on this PC";
            state.body = "Sunshine returned an empty list. Add apps in Sunshine, then try again.";
            state.action = "Try again";
        }
        state.draw(canvas);
        return;
    }

    const moonlight_backend_app_t &app =
        backend.apps[std::min(model_.selected_app(), backend.app_count - 1)];
    const bool running = backend.current_app_id == app.id;
    paint.label(ui::upper(backend.name), kMargin, 186.0f, 20.0f, gfx::mix(t.primary, ink, 0.25f));
    paint.heading(canvas.fonts.display.font->fit(app.name, 76.0f, 1180.0f), kMargin - 3.0f, 268.0f,
                  76.0f, ink);
    const bool offline = !backend.online;
    const Color state = offline ? t.text_muted : running ? t.success : quiet;
    list.circle(kMargin + 8.0f, 316.0f, 7.0f, state);
    const float word =
        paint.label(offline   ? (model_.reconnecting() ? "Reconnecting" : "The PC is not answering")
                    : running ? "Running on the PC"
                              : "Ready to start",
                    kMargin + 26.0f, 325.0f, 24.0f, running && !offline ? t.success : ink);
    char text[160];
    std::snprintf(text, sizeof(text), "%s   \xC2\xB7   %u FPS   \xC2\xB7   %s   \xC2\xB7   %u Mbps",
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
    paint.heading("Settings", kMargin - 2.0f, 212.0f, 52.0f, paint.page_text());
    paint.body("Changes are saved as you make them",
               kMargin + paint.heading_width("Settings", 52.0f) + 22.0f, 212.0f, 24.0f,
               paint.page_text_muted());
    if (!version_.empty())
        paint.body("ProsperoLight " + version_, kRight, 212.0f, 21.0f, paint.page_text_muted(),
                   gfx::Align::right);
    form_.draw(canvas);

    profile_panel_.draw(canvas, kProfilePanel);
    const Rect inside = profile_inside();
    paint.label(ui::upper("This stream"), inside.x, inside.y + 20.0f, 19.0f, t.text_muted);
    paint.heading(profile_.label, inside.x, inside.y + 72.0f, 36.0f);
    headroom_.draw(canvas);
    if (warning_.visible())
    {
        warning_.draw(canvas);
    }
    else
    {
        char text[120];
        if (limits_apply_)
            std::snprintf(text, sizeof(text), "Smooth up to %.0f Mbps at this frame rate.",
                          static_cast<double>(limit_for(model_.config().stream_fps).smooth));
        else if (model_.config().video_codec == MOONLIGHT_VIDEO_CODEC_PYROWAVE)
            std::snprintf(text, sizeof(text), "High bitrate and wired LAN recommended.");
        else
            std::snprintf(text, sizeof(text), "No measured decoder limit for this profile.");
        paint.body(text, inside.x, inside.y + 214.0f, 22.0f, t.text_muted);
        profile_details_.draw(canvas);
    }

    // What the controller does during a stream, in the buttons' own shapes.
    shortcut_panel_.draw(canvas, kShortcutPanel);
    const Rect keys = shortcut_panel_.content_rect(kShortcutPanel).inset(12.0f);
    paint.label(ui::upper("Hold touchpad click"), keys.x, keys.y + 20.0f, 19.0f, t.text_muted);
    struct Shortcut
    {
        ui::Button second;
        const char *what;
    };
    static constexpr Shortcut kShortcuts[] = {
        {ui::Button::l1, "Return"},
        {ui::Button::r1, "Statistics"},
        {ui::Button::square, "Mouse mode"},
        {ui::Button::triangle, "Keyboard"},
        {ui::Button::left_stick, "Select/Back"},
        {ui::Button::right_stick, "PS/Guide"},
    };
    const ui::GlyphStyle glyph = t.dark ? ui::GlyphStyle::dark() : ui::GlyphStyle::light();
    for (int i = 0; i < 6; ++i)
    {
        const float cy = keys.y + 60.0f + static_cast<float>(i % 3) * 44.0f;
        const float column = keys.x + static_cast<float>(i / 3) * keys.w * 0.5f;
        float x = column;
        ui::draw_button(list, canvas.fonts, glyph, ui::Button::touchpad, x, cy, 32.0f);
        x += ui::button_width(ui::Button::touchpad, 32.0f) + 10.0f;
        x += paint.body("+", x, cy + 8.0f, 24.0f, t.text_muted) + 10.0f;
        ui::draw_button(list, canvas.fonts, glyph, kShortcuts[i].second, x, cy, 32.0f);
        paint.body(kShortcuts[i].what, column + 138.0f, cy + 8.0f, 21.0f, t.text);
    }
}

// Credits and first steps, after ProsperoEden's About page.
void View::draw_about(ui::Canvas &canvas, ui::Painter &paint) const
{
    gfx::DrawList &list = canvas.list;
    const ui::Theme &t = theme_;
    const ui::FontRef &regular = canvas.fonts.regular;
    const Color rule = t.text_muted.with_alpha(0.28f);
    paint.heading("About ProsperoLight", kMargin - 2.0f, 212.0f, 52.0f, paint.page_text());
    paint.body("Credits and first steps",
               kMargin + paint.heading_width("About ProsperoLight", 52.0f) + 22.0f, 212.0f, 24.0f,
               paint.page_text_muted());

    credits_panel_.draw(canvas, kCreditsPanel);
    const Rect left = credits_panel_.content_rect(kCreditsPanel).inset(14.0f);
    const float bottom = left.y + left.h - 6.0f;
    paint.label(ui::upper("Project credits"), left.x, left.y + 20.0f, 19.0f, t.text_muted);
    paint.heading("Powered by Moonlight", left.x, left.y + 72.0f, 38.0f);
    ui::paragraph(list, regular,
                  "ProsperoLight speaks the Moonlight protocol through moonlight-common-c. All "
                  "credit for it goes to the Moonlight developers and contributors.",
                  left.x, left.y + 118.0f, 24.0f, left.w, 34.0f, t.text, 3);
    paint.label("moonlight-stream.org", left.x, left.y + 226.0f, 24.0f, t.primary);
    list.rounded_rect({left.x, left.y + 252.0f, left.w, 1.0f}, 0.0f, rule);
    paint.label(ui::upper("Thanks"), left.x, left.y + 288.0f, 19.0f, t.text_muted);
    ui::paragraph(list, regular,
                  "Thanks to the Sunshine developers for the host on the PC, to the whole PS5 "
                  "homebrew community, and to every developer whose tools and libraries make "
                  "ProsperoLight possible.",
                  left.x, left.y + 326.0f, 24.0f, left.w, 34.0f, t.text, 3);
    list.rounded_rect({left.x, left.y + 426.0f, left.w, 1.0f}, 0.0f, rule);
    paint.label(ui::upper("PS5 edition"), left.x, left.y + 462.0f, 19.0f, t.text_muted);
    ui::paragraph(list, regular,
                  "ProsperoLight is an unofficial PS5 client brought to you by BlackBearReloaded.",
                  left.x, left.y + 500.0f, 24.0f, left.w, 34.0f, t.text, 2);
    paint.body("Menu sound effects made with ElevenLabs.", left.x, bottom, 20.0f, t.text_muted);
    if (!version_.empty())
        paint.label("Version " + version_, left.x + left.w, bottom, 20.0f, t.primary,
                    gfx::Align::right);

    start_panel_.draw(canvas, kStartPanel);
    const Rect right = start_panel_.content_rect(kStartPanel).inset(14.0f);
    paint.label(ui::upper("Getting started"), right.x, right.y + 20.0f, 19.0f, t.text_muted);
    paint.heading("Stream from your PC", right.x, right.y + 72.0f, 38.0f);
    struct Step
    {
        const char *title;
        const char *body;
    };
    static constexpr Step kSteps[] = {
        {"Run Sunshine on the PC", "Keep it open, on the same network as this PS5."},
        {"Pair once", "Choose the PC in PCs, then type the PIN shown here into Sunshine."},
        {"Play", "Pick an app in Games and start it."},
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
    paint.label(ui::upper("Files on this PS5"), right.x, right.y + 386.0f, 19.0f, t.text_muted);
    files_.draw(canvas);
    paint.body(storage_access_ ? "An update or a reinstall does not touch them."
                               : "Kept in the app's own storage: no filesystem access at start-up.",
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
    const char *name = model_.backend().name[0] ? model_.backend().name : "the PC";
    const bool ready = model_.pairing_pin()[0] != '\0';
    paint.label(ui::upper("Pairing"), panel.cx(), panel.y + 62.0f, 20.0f, t.text_muted,
                gfx::Align::center);
    paint.heading(canvas.fonts.display.font->fit(ready ? std::string("Enter this PIN on ") + name
                                                       : "Preparing a PIN",
                                                 40.0f, panel.w - 80.0f),
                  panel.cx(), panel.y + 122.0f, 40.0f, gfx::Align::center);
    paint.body(ready ? "Open Sunshine on the PC, choose PIN, and type the code."
                     : "Asking Sunshine for a pairing request.",
               panel.cx(), panel.y + 168.0f, 24.0f, t.text_muted, gfx::Align::center);
    pin_.draw(canvas);
    pair_timer_.draw(canvas);
    paint.body("This closes by itself when Sunshine accepts the PIN.", panel.cx(),
               panel.y + panel.h - 36.0f, 21.0f, t.text_muted, gfx::Align::center);
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
    paint.label("Connecting", kConnectBar.x, kConnectBar.y - 20.0f, 22.0f, ink.with_alpha(0.85f));
    list.rounded_rect(kConnectBar, radius, ink.with_alpha(0.16f));
    if (!plate_ && load_progress_ > 0.0f)
        list.rounded_rect({kConnectBar.x, kConnectBar.y,
                           std::max(kConnectBar.w * load_progress_, kConnectBar.h), kConnectBar.h},
                          radius, theme_.primary);
    list.pop_opacity();
}

// The connecting screen's tip, with the buttons drawn as the controller shows them.
void View::draw_loader_tip(ui::Canvas &canvas) const
{
    const float shown = loader_.opacity();
    if (shown <= 0.01f)
        return;
    gfx::DrawList &list = canvas.list;
    list.push_opacity(shown);
    ui::Painter paint(list, canvas.fonts, theme_, 0);
    const Color ink = Color::rgb(0xffffff);
    paint.label("Tip", kMargin, 780.0f, 21.0f, ink.with_alpha(0.7f));
    const ui::GlyphStyle glyph = ui::GlyphStyle::dark();
    const float cy = 826.0f;
    float x = kMargin;
    ui::draw_button(list, canvas.fonts, glyph, ui::Button::touchpad, x, cy, 38.0f);
    x += ui::button_width(ui::Button::touchpad, 38.0f) + 12.0f;
    x += paint.body("+", x, cy + 9.0f, 26.0f, ink.with_alpha(0.7f)) + 12.0f;
    ui::draw_button(list, canvas.fonts, glyph, ui::Button::l1, x, cy, 38.0f);
    x += ui::button_width(ui::Button::l1, 38.0f) + 18.0f;
    paint.body("ends the stream and returns here", x, cy + 9.0f, 26.0f, ink);
    list.pop_opacity();
}

} // namespace launcher
