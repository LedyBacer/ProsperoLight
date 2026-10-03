// ps5-homebrew-ui - Component: ProfilePicker, "Who is playing?".
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/badge.hpp"
#include "ui/components/card.hpp"

#include <functional>
#include <string>
#include <vector>

namespace hui::ui
{

// One player.
struct Profile
{
    std::string name;
    std::string detail;      // under the name ("Played 2 hours ago")
    std::string extra;       // shown only while the card has the focus ("Level 12, 63 h")
    std::uint32_t image = 0; // the avatar's picture; 0 draws initials
    gfx::Rect uv = gfx::kCanvasUv;
    gfx::Color accent{0.0f, 0.0f, 0.0f, 0.0f}; // alpha 0: a colour made from the name
    int controller = 0; // 1..4: signed in on that controller; 0: not signed in
    int tag = 0;        // yours
};

struct ProfilePickerStyle : ComponentStyle
{
    // ---- geometry ----
    int columns = 0; // 0: one row; otherwise a grid this wide
    float card_width = 230.0f;
    float card_height = 252.0f;
    float gap = 28.0f; // between cards, both ways
    float avatar_size = 116.0f;
    AvatarShape avatar_shape = AvatarShape::circle;
    gfx::Align align = gfx::Align::center; // where the cards and the title sit in the bounds
    // ---- type ----
    std::string title = "Who is playing?"; // empty for none
    float title_size = 40.0f;
    float name_size = 25.0f;
    float detail_size = 19.0f;
    bool on_surface = false; // the picker sits on a panel: the title takes the panel's ink
    // ---- focus ----
    float focus_scale = 1.1f;      // the focused card's size
    float lift = 12.0f;            // pixels it rises
    float expand = 38.0f;          // pixels it grows downward to show `extra`; 0 keeps its height
    bool accent_by_profile = true; // light and avatar ring in the profile's own colour
    float dim = 0.0f;              // 0..1: how far the other cards fade
    // ---- extras ----
    bool add_card = true; // a last card that adds a profile
    std::string add_label = "Add profile";
    bool slots = true; // a "P1".."P4" tag on profiles signed in on a controller
    float slot_size = 30.0f;
    std::string slot_prefix = "P";
    // ---- behaviour ----
    bool wrap = false;           // past the last card of a row comes the first
    EdgeExits exits;             // edges that hand the focus back to the screen
    float entrance_step = 0.06f; // seconds between cards arriving; 0 for none
};

// The profile select every console game opens with: a row (or grid) of cards,
// each a person, and one more to add a person. The focused card rises, grows
// and shows more about its player.
//
//   ui::ProfilePicker who;
//   who.style.theme = theme;
//   who.set_profiles({{"Mara Voss", "Played today"}, {"Guest", "New here"}});
//   who.set_bounds({96, 300, 1728, 480});
//   ...
//   if (who.handle(input, feedback) == ui::Event::activated)
//   {
//       if (who.add_focused()) create_profile(); else sign_in(who.focus());
//   }
//   who.update(dt);
//   who.draw(canvas);
class ProfilePicker
{
  public:
    // area is the avatar's square; focus is 0..1.
    using Slot = std::function<void(Canvas &canvas, const gfx::Rect &area, const Profile &profile,
                                    int index, float focus)>;

    ProfilePickerStyle style;
    Slot avatar; // draws a profile's picture instead of the ui::Avatar

    void set_profiles(std::vector<Profile> profiles);
    const std::vector<Profile> &profiles() const
    {
        return profiles_;
    }
    Profile &profile(int index)
    {
        return profiles_[static_cast<std::size_t>(index)];
    }
    void set_bounds(const gfx::Rect &bounds);
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }

    // Cards, the "add" card included.
    int count() const;
    // The focused card; equal to profiles().size() on the "add" card.
    int focus() const
    {
        return focus_;
    }
    bool add_focused() const;
    // Moves the focus without sound; snap skips the glide (use it on open).
    void set_focus(int index, bool snap = true);
    void set_active(bool active)
    {
        active_ = active;
    }
    // Replays the entrance animation.
    void enter();

    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

    // The edge the focus asked to leave through in the last handle() (see
    // style.exits), or Direction::none. handle() returned Event::none.
    Direction exit() const
    {
        return exit_;
    }
    // Where a card rests (scroll applied, before its focus scale).
    gfx::Rect card_rect(int index) const;
    // The height the picker needs in this style, title included.
    float height() const;

  private:
    int columns() const;
    int row_count() const;
    int row_length(int row) const;
    float head() const;
    float content_width() const;
    gfx::Rect rest_rect(int index) const;    // content space: before the scroll
    gfx::Rect focused_rect(int index) const; // content space: risen, grown and expanded
    int nearest_in_row(int row, float x) const;
    gfx::Color accent_of(int index) const;
    void layout(bool snap);
    void draw_card(Canvas &canvas, int index, float entrance) const;

    std::vector<Profile> profiles_;
    std::vector<Avatar> avatars_;
    std::vector<tween::Spring> amounts_; // per card, 0..1
    gfx::Rect bounds_{0.0f, 0.0f, 1200.0f, 420.0f};
    int focus_ = 0;
    bool active_ = true;
    float age_ = 10.0f;
    Direction exit_ = Direction::none;
    Highlight highlight_;
    Scroller scroll_;
    Pulse press_;
};

} // namespace hui::ui
