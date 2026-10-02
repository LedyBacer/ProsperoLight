// ps5-homebrew-ui - Components: Badge, Chip, Avatar and AvatarStack.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Small things that sit on or beside other things: a count on a button, a
// label with a dot, a person. They take no input; a change of value answers
// with a small bounce so the eye catches it.

#pragma once

#include "ui/components/progress.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hui::ui
{

// ---- Badge -----------------------------------------------------------------

enum class BadgeFill : std::uint8_t
{
    solid,   // the kind's colour, text in the colour that reads on it
    tinted,  // a wash of the kind's colour, bordered by it
    outline, // the surface colour, bordered by the kind's colour
};

struct BadgeStyle : ComponentStyle
{
    Status kind = Status::primary;
    BadgeFill fill = BadgeFill::solid;
    bool dot = false;     // no text: a dot of dot_size
    float height = 30.0f; // the pill
    float dot_size = 14.0f;
    float text_size = 18.0f;
    float padding = 10.0f;                 // left and right of the text
    int max_count = 99;                    // above it the badge shows "99+"
    bool hide_zero = true;                 // a count of 0 hides the badge
    gfx::Align align = gfx::Align::center; // where the pill sits in the bounds
    float cutout = 0.0f; // a rim in the backing colour, to part it from what it overlaps
    gfx::Color backing{0.0f, 0.0f, 0.0f, 0.0f}; // alpha 0: the surface, made opaque
    bool pulse = false;                         // dot: a halo that breathes
    float pop = 1.0f;                           // how hard a change bounces; 0 for none
};

// A count or a short word in a pill, or just a dot.
//
//   ui::Badge badge;
//   badge.style.kind = ui::Status::danger;
//   badge.set_bounds({x, y, 40, 30});
//   badge.set_count(3);   // pops
class Badge
{
  public:
    BadgeStyle style;

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    void set_count(int count);
    int count() const
    {
        return count_;
    }
    // A word instead of a number ("NEW"); an empty word returns to the count.
    void set_text(std::string_view text);
    // What the pill says right now.
    std::string text() const;
    bool visible() const;
    // The pill's width in this theme, for laying things out beside it.
    float width(const Painter &paint) const;

    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    void bump();

    gfx::Rect bounds_{0.0f, 0.0f, 40.0f, 30.0f};
    int count_ = 0;
    std::string text_;
    tween::Bounce scale_{1.0f, 0.0f, 1.0f};
    tween::Spring shown_;
    bool started_ = false;
    float phase_ = 0.0f;
};

// ---- Chip ------------------------------------------------------------------

struct ChipStyle : ComponentStyle
{
    float height = 40.0f;
    float text_size = 19.0f;
    float padding = 16.0f;               // left and right of the content
    bool leading_dot = false;            // a status dot before the label
    Status dot = Status::success;        // ... in this colour
    bool removable = false;              // a small "x" after the label
    gfx::Align align = gfx::Align::left; // where the chip sits in the bounds
};

// A label in a capsule: a tag, a filter, a state. It only displays; the
// screen that owns it decides what selecting or removing means.
class Chip
{
  public:
    ChipStyle style;
    std::string label;

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    void set_selected(bool selected, bool snap = false);
    bool selected() const
    {
        return selected_.target > 0.5f;
    }
    void set_focused(bool focused);
    float width(const Painter &paint) const;

    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    gfx::Rect bounds_{0.0f, 0.0f, 120.0f, 40.0f};
    tween::Bounce selected_;
    tween::Spring focus_;
};

// ---- Avatar ----------------------------------------------------------------

enum class AvatarShape : std::uint8_t
{
    circle,
    rounded, // a square with the theme's corners
};

enum class Presence : std::uint8_t
{
    none,
    online,  // success
    away,    // warning
    busy,    // danger
    offline, // a hollow dot
};

struct AvatarStyle : ComponentStyle
{
    AvatarShape shape = AvatarShape::circle;
    float size = 0.0f;        // 0: the smaller side of the bounds
    float text_scale = 0.38f; // initials, as a share of the size
    float saturation = 0.55f; // of the colour made from the name
    float brightness = 0.72f;
    float status_scale = 0.28f;                 // the presence dot, as a share of the size
    float cutout = 0.0f;                        // a rim in the backing colour around the avatar
    gfx::Color backing{0.0f, 0.0f, 0.0f, 0.0f}; // alpha 0: the surface, made opaque
};

// A person: a picture, or initials on a colour that both come from the name,
// so the same person looks the same everywhere.
class Avatar
{
  public:
    AvatarStyle style;

    // "Mara Voss" -> "MV"; one word gives one letter.
    static std::string initials_of(std::string_view name);
    static gfx::Color color_of(std::string_view name, float saturation, float brightness);

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    void set_name(std::string_view name);
    const std::string &name() const
    {
        return name_;
    }
    const std::string &initials() const
    {
        return initials_;
    }
    // texture 0 returns to the initials.
    void set_image(std::uint32_t texture, const gfx::Rect &uv = gfx::kCanvasUv);
    void set_presence(Presence presence);
    Presence presence() const
    {
        return presence_;
    }

    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    gfx::Rect bounds_{0.0f, 0.0f, 64.0f, 64.0f};
    std::string name_;
    std::string initials_;
    std::uint32_t image_ = 0;
    gfx::Rect uv_ = gfx::kCanvasUv;
    Presence presence_ = Presence::none;
    tween::Bounce dot_{1.0f, 0.0f, 1.0f};
};

struct AvatarPerson
{
    std::string name;
    std::uint32_t image = 0; // a texture, or 0 for initials
    gfx::Rect uv = gfx::kCanvasUv;
};

struct AvatarStackStyle : AvatarStyle
{
    float overlap = 0.22f; // how much of each avatar the next one covers
    int max_shown = 4;     // the rest become "+N"

    AvatarStackStyle()
    {
        // Smaller initials than a lone avatar: part of each one is covered.
        text_scale = 0.32f;
        cutout = 3.0f;
    }
};

// People in a row, overlapping, with the overflow counted.
class AvatarStack
{
  public:
    AvatarStackStyle style;

    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    void set_people(std::vector<AvatarPerson> people);
    const std::vector<AvatarPerson> &people() const
    {
        return people_;
    }
    // How many are behind the "+N".
    int hidden() const;
    float width() const;

    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    float diameter() const;

    gfx::Rect bounds_{0.0f, 0.0f, 240.0f, 48.0f};
    std::vector<AvatarPerson> people_;
    tween::Bounce more_{1.0f, 0.0f, 1.0f};
};

} // namespace hui::ui
