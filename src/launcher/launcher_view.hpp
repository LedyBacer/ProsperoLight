/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "launcher/launcher_model.hpp"

#include "core/input.hpp"
#include "core/tween.hpp"
#include "gfx/backdrop_spec.hpp"
#include "gfx/draw_list.hpp"
#include "ui/components.hpp"
#include "ui/feedback.hpp"
#include "ui/fonts.hpp"
#include "ui/glyphs.hpp"
#include "ui/motion.hpp"
#include "ui/theme.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace launcher
{

// ProsperoLight's own theme: frosted panels over a night-blue sky.
hui::ui::Theme moonlight_theme();

// One frame, back to front: backdrop, scene, a blurred copy of both, overlay.
struct Frame
{
    hui::gfx::BackdropSpec backdrop;
    hui::gfx::DrawList scene;
    hui::gfx::DrawList overlay;
    std::uint32_t glass_texture = 0;

    void reset()
    {
        backdrop = {};
        scene.clear();
        overlay.clear();
    }
};

// The colours a poster lends to the page while its app is selected.
struct Palette
{
    hui::gfx::Color dark;
    hui::gfx::Color mid;
    hui::gfx::Color accent;
};

// The launcher's four screens and their dialogs, drawn with the kit's
// widgets. It reads the Model and asks it for things; it owns no network or
// console call, so the same code draws on the PC for tests.
class View
{
  public:
    View(Model &model, const hui::ui::Fonts &fonts);

    void set_version(std::string version)
    {
        version_ = std::move(version);
    }
    // The app has just opened (true) or a stream has just ended (false).
    void set_first_start(bool first)
    {
        first_start_ = first;
    }
    // Where the app keeps its files, for the About screen.
    struct Storage
    {
        // False while the app uses its own sandboxed storage.
        bool access = false;
        std::string settings;
        std::string pairing;
        std::string logs;
    };
    void set_storage(const Storage &storage);
    // How many controllers have a signed-in user (1 to 4).
    void set_players(int count);
    // A stream that ended with an error: shown once, on the Games screen.
    void show_stream_error(const char *message);

    // The colours of a decoded poster (RGBA pixels).
    static Palette palette_of(const unsigned char *rgba, int width, int height);
    // Box art, uploaded by the platform when its picture arrived.
    void set_artwork(int app_id, std::uint32_t texture, float aspect, const Palette &palette);
    // Textures the view no longer shows: the platform deletes them.
    std::vector<std::uint32_t> take_released_textures();

    void update(const hui::InputFrame &input, float dt, hui::ui::Feedback &feedback);
    void draw(Frame &frame) const;

    // The connecting screen's bar: where it is, its colour and how far it is.
    struct ConnectBar
    {
        hui::gfx::Rect rect;
        hui::gfx::Color fill;
        float progress = 0.0f;
    };
    ConnectBar connecting_bar() const;
    // Draws the connecting screen without the fill of its bar: the picture
    // the stream is given, which draws the fill itself from then on.
    void set_plate(bool plate)
    {
        plate_ = plate;
    }

    // True once, when the stream should start.
    bool take_start_stream();
    // Installing a newer release (launcher_model.hpp): the actions that do it,
    // and, once, that the app must close now so the helper can replace its files.
    void set_update_actions(const UpdateActions &actions)
    {
        update_actions_ = actions;
    }
    bool take_update_exit();
    // The question "Update now?" is on screen (a development build answers it itself).
    bool update_offer_open() const
    {
        return update_ui_ == UpdateUi::offer;
    }
    int screen() const
    {
        return screen_;
    }
    // Puts a screen up at once, without sound or animation. The launcher draws
    // every screen once before its first frame, so that the graphics driver
    // compiles what each one needs then, not at the first visit.
    void warm(int screen)
    {
        screen_ = screen;
        screen_age_ = 10.0f;
    }
    static constexpr int kScreenCount = 4;

  private:
    struct Artwork
    {
        int app_id = 0;
        std::uint32_t texture = 0;
        float aspect = 1.0f;
        Palette palette;
    };

    void build();
    void restyle();
    void show(int screen, hui::ui::Feedback *feedback);
    void sync();
    void layout_actions();
    void sync_hosts();
    void sync_host_panel();
    void sync_games();
    void sync_settings_from_config();
    void sync_profile(bool snap);
    void apply_setting(int id);
    void apply_ambient(bool snap);
    void request_artwork();
    const Artwork *artwork(int app_id) const;
    int host_focus() const;
    bool on_host() const;
    hui::gfx::Rect host_inside() const;
    hui::gfx::Rect profile_inside() const;

    void update_hosts(const hui::InputFrame &input, hui::ui::Feedback &feedback);
    void set_host_zone(int zone, hui::ui::Feedback &feedback);
    void primary_host_action(hui::ui::Feedback &feedback);
    void update_games(const hui::InputFrame &input, hui::ui::Feedback &feedback);
    void set_games_zone(int zone, hui::ui::Feedback &feedback);
    void launch(hui::ui::Feedback &feedback);
    void update_settings(const hui::InputFrame &input, hui::ui::Feedback &feedback);

    void draw_header(hui::ui::Canvas &canvas, hui::ui::Painter &paint) const;
    void draw_footer(hui::ui::Canvas &canvas, hui::ui::Painter &paint) const;
    void draw_hosts(hui::ui::Canvas &canvas, hui::ui::Painter &paint) const;
    void draw_host_icon(hui::ui::Canvas &canvas, const hui::gfx::Rect &row, int index,
                        float focus) const;
    void draw_host_state(hui::ui::Canvas &canvas, const hui::gfx::Rect &row, int index) const;
    void draw_games(hui::ui::Canvas &canvas, hui::ui::Painter &paint) const;
    void draw_app_art(hui::ui::Canvas &canvas, const hui::gfx::Rect &art, float radius,
                      const hui::ui::CardItem &item) const;
    void draw_settings(hui::ui::Canvas &canvas, hui::ui::Painter &paint) const;
    void draw_about(hui::ui::Canvas &canvas, hui::ui::Painter &paint) const;
    void draw_pairing(hui::ui::Canvas &canvas) const;
    void draw_update(hui::ui::Canvas &canvas) const;
    void update_modal(const hui::InputFrame &input, float dt, hui::ui::Feedback &feedback);
    void open_update_failure(const char *reason, hui::ui::Feedback &feedback);
    void open_update_offer(hui::ui::Feedback &feedback, bool on_notes);
    void begin_update(hui::ui::Feedback &feedback);
    void draw_update_notes(hui::ui::Canvas &canvas) const;
    void draw_loader_tip(hui::ui::Canvas &canvas) const;
    void draw_connect_bar(hui::ui::Canvas &canvas) const;
    hui::ui::GlyphStyle glyphs(const hui::ui::Painter &paint) const;

    Model &model_;
    const hui::ui::Fonts &fonts_;
    hui::gfx::Rect host_panel_bounds_{816.0f, 262.0f, 1008.0f, 544.0f};
    std::vector<hui::gfx::Rect> host_action_rects_;
    hui::ui::Theme theme_;
    std::string version_;
    unsigned seen_revision_ = 0;
    int screen_ = 0;
    float age_ = 0.0f;
    float screen_age_ = 10.0f;
    float clock_ = 0.0f;
    hui::ui::SpringColor ambient_[4];
    bool welcomed_ = false;
    bool first_start_ = true;

    // header
    hui::ui::TabBar tabs_;
    hui::ui::StatusBar status_;
    hui::ui::Chip profile_;
    // PCs
    hui::ui::ListView hosts_;
    hui::ui::Panel host_panel_;
    hui::ui::DetailList host_details_;
    hui::ui::PushButton open_games_;
    hui::ui::ButtonGroup host_actions_;
    hui::ui::HoldButton remove_;
    hui::ui::EmptyState empty_;
    hui::ui::Spinner searching_;
    int host_zone_ = 0;
    int shown_host_ = -2;
    // Games
    hui::ui::GridView apps_;
    hui::ui::PushButton start_;
    hui::ui::PushButton stop_;
    hui::ui::SectionHeader shelf_;
    hui::ui::EmptyState no_apps_;
    hui::ui::Spinner loading_apps_;
    int games_zone_ = 0;
    std::vector<Artwork> artwork_;
    std::vector<std::uint32_t> released_;
    std::string art_host_;
    // Settings
    hui::ui::Form form_;
    hui::ui::Select language_;
    hui::ui::Panel profile_panel_;
    hui::ui::Meter headroom_;
    hui::ui::DetailList profile_details_;
    hui::ui::Banner warning_;
    hui::ui::Panel shortcut_panel_;
    bool limits_apply_ = false;
    // About
    hui::ui::Panel credits_panel_;
    hui::ui::Panel start_panel_;
    hui::ui::DetailList files_;
    bool storage_access_ = false;
    Storage storage_;
    // overlays
    hui::tween::Spring pair_fade_;
    hui::ui::PinEntry pin_;
    hui::ui::Countdown pair_timer_;
    bool pair_timer_started_ = false;
    hui::ui::InputPrompt port_prompt_;
    hui::ui::InputPrompt host_prompt_;
    hui::ui::Dialog unpair_dialog_;
    // The update: the question, then the ring while the helper works.
    enum class UpdateUi : std::uint8_t
    {
        hidden,
        offer,
        notes, // What's new: the release notes
        working,
        closing,
        failed,
    };
    UpdateUi update_ui_ = UpdateUi::hidden;
    UpdateActions update_actions_;
    UpdateOffer update_offer_;
    UpdateProgress update_progress_;
    hui::ui::Dialog update_dialog_;
    hui::ui::ProgressRing update_ring_;
    hui::ui::TextView update_notes_;
    hui::tween::Spring update_notes_fade_;
    hui::tween::Spring update_fade_;
    float update_closing_age_ = 0.0f;
    bool update_exit_ = false;
    hui::ui::LoadingScreen loader_;
    float load_progress_ = 0.0f;
    bool launching_ = false;
    bool plate_ = false;
    bool start_stream_ = false;
    hui::ui::ToastStack toasts_;
    std::string pending_error_;
};

} // namespace launcher
