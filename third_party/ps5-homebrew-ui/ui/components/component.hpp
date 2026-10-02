// ps5-homebrew-ui - Components: what every reusable component shares.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A component is a small class that owns one piece of interface completely:
// its values, its focus, its springs, its sounds and its drawing. A screen
// places it, forwards input to it and reads what happened. Every component
// follows the same five rules, so knowing one is knowing all of them:
//
//   1. `style` is a public struct of plain values. It starts from a ui::Theme
//      (palette, surface construction, corners, type, motion, sound set) and
//      adds the component's own knobs. Change any of it at any time.
//   2. `set_bounds(rect)` says where it is, in virtual pixels.
//   3. `handle(input, feedback)` takes one frame of input while the component
//      has the focus and returns an Event. It plays its own cues.
//   4. `update(dt)` advances its animation; call it every frame.
//   5. `draw(canvas)` is const and draws it. It may run more than once a frame.
//
// Slots (std::function members) replace parts of the drawing with your own,
// so a component is customised by data first and by code where data ends.
// docs/COMPONENTS.md lists every component with its knobs and pictures.

#pragma once

#include "audio/cues.hpp"
#include "core/input.hpp"
#include "core/tween.hpp"
#include "gfx/draw_list.hpp"
#include "ui/feedback.hpp"
#include "ui/fonts.hpp"
#include "ui/motion.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace hui::ui
{

// What a component draws with. Make one where you draw:
//   ui::Canvas canvas{frame.scene, context.fonts, frame.glass_texture, clock};
struct Canvas
{
    gfx::DrawList &list;
    const Fonts &fonts;
    std::uint32_t glass = 0; // the frame's blurred copy; 0 when there is none
    float time = 0.0f;       // free-running seconds, for idle motion
};

// What handle() reports. One event per frame is enough for a pad-driven UI.
enum class Event : std::uint8_t
{
    none,
    moved,     // the focus went to another item
    changed,   // a value changed (a switch, a slider step, a tab)
    activated, // confirm was pressed on the focused item
    cancelled, // back was pressed
    refused,   // the input could not be honoured (an edge, a disabled item)
};

// The cues a component plays. Assign another cue to restyle its voice, or
// audio::Cue::count to silence one.
struct Sounds
{
    audio::Cue move = audio::Cue::focus;
    audio::Cue activate = audio::Cue::select;
    audio::Cue cancel = audio::Cue::back;
    audio::Cue refuse = audio::Cue::error;
    audio::Cue change = audio::Cue::toggle;
    audio::Cue step = audio::Cue::slider;
    audio::Cue page = audio::Cue::tab;
    audio::Cue open = audio::Cue::modal_open;
    audio::Cue close = audio::Cue::modal_close;
    audio::Cue notify = audio::Cue::notify;
    // SoundSet::count plays in the screen's own set; name one to force it.
    audio::SoundSet set = audio::SoundSet::count;
    float gain = 1.0f;   // scales every cue
    float rumble = 1.0f; // scales every rumble; 0 turns them off
    bool pan = true;     // place cues in the stereo field by screen position
};

// The first built-in theme: what a component looks like before it is styled.
const Theme &default_theme();

// The part of a style every component has. Component styles derive from it.
struct ComponentStyle
{
    Theme theme = default_theme();
    Sounds sounds;
    bool reduced_motion = false; // mirror the player's setting here

    // How fast and how bouncy things move, from the theme.
    float omega() const
    {
        return reduced_motion ? 60.0f : theme.omega;
    }
    float damping() const
    {
        return reduced_motion ? 1.0f : theme.damping;
    }
};

// Plays a cue the way the style asks. x is where on screen it happened
// (virtual pixels; negative for "nowhere in particular").
void play_cue(Feedback &feedback, const ComponentStyle &style, audio::Cue cue, float x = -1.0f,
              float pitch = 1.0f, float gain = 1.0f);

// A soft refusal: a quiet error cue, a short rumble and a shake pulse. Silent
// when the input is an auto-repeat, so holding a direction against an edge
// does not machine-gun the sound. Returns Event::refused for convenience.
Event refuse(Feedback &feedback, const ComponentStyle &style, const InputFrame &input, Pulse &pulse,
             float x = -1.0f);

// Text cut to fit a width, ending in "..." when something was removed. The
// label and body faces differ per theme, hence the two.
std::string fit_label(const Painter &paint, std::string_view text, float size, float width);
std::string fit_body(const Painter &paint, std::string_view text, float size, float width);

// ---- the focus highlight ---------------------------------------------------

enum class HighlightKind : std::uint8_t
{
    ring,      // the theme's own focus indicator around the item
    fill,      // a plate in the primary colour; text on it turns on_primary
    tint,      // a translucent plate in the focus colour
    bar,       // a faint plate with a solid bar on its leading edge
    underline, // a line under the item
    glow,      // light around the item and a hairline
    none,
};

struct HighlightStyle
{
    HighlightKind kind = HighlightKind::tint;
    gfx::Color color{0.0f, 0.0f, 0.0f, 0.0f}; // alpha 0: the theme's own colour
    float radius = -1.0f;                     // negative: the theme's control radius
    float thickness = 5.0f;                   // bar and underline
    float grow = 0.0f;                        // extends past the item on every side
    bool breathe = true;                      // a slow pulse while idle
};

// One highlight that glides between items instead of jumping. snap() it when
// the component appears, set target() every frame, then update() and draw().
class Highlight
{
  public:
    void snap(const gfx::Rect &r);
    void target(const gfx::Rect &r);
    void update(float dt, const ComponentStyle &style);
    // Shake it: the item it sits on refused an input.
    Pulse &refusal()
    {
        return refusal_;
    }
    // Where it is now, shake included.
    gfx::Rect rect(float time) const;
    // amount fades it (a list that lost the focus keeps a faint highlight).
    void draw(Canvas &canvas, const ComponentStyle &style, const HighlightStyle &look,
              float amount = 1.0f) const;
    // How much of `item` the highlight covers, 0..1: an item's own focus
    // amount, without a spring per item.
    float coverage(const gfx::Rect &item) const;

    // The text colour for an item `focus` (0..1) under this highlight.
    static gfx::Color text_color(const ComponentStyle &style, const HighlightStyle &look,
                                 float focus);
    // The same for text whose resting colour is not theme.text (text drawn
    // straight on the page, for instance).
    static gfx::Color text_color(const ComponentStyle &style, const HighlightStyle &look,
                                 float focus, gfx::Color resting);

  private:
    tween::Bounce x_, y_, w_, h_;
    Pulse refusal_;
};

} // namespace hui::ui
