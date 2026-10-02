// ps5-homebrew-ui - Component: Transition, how a region arrives, leaves or changes its content.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// "Nothing teleports" is easy for one value and tedious for a region of a
// screen. A Transition is the few numbers of an entrance or an exit (an
// opacity, an offset, a scale), eased at the theme's pace, and a pair of calls
// that apply them to everything drawn in between:
//
//   ui::Transition appear;
//   appear.start(ui::TransitionKind::slide, Direction::up);   // rises into place
//   ...
//   appear.update(dt);
//   ...
//   appear.begin(canvas, region);
//   draw_the_region();
//   appear.end(canvas);
//
// A push changes one content for another, both moving the same way (the
// shared axis: forward is left, back is right; down a list is up):
//
//   swap.start(ui::TransitionKind::push, Direction::left);    // when B replaces A
//   ...
//   if (swap.running())
//   {
//       swap.begin(canvas, region, ui::TransitionPhase::outgoing);
//       draw(a);                                              // A slides out and fades
//       swap.end(canvas);
//   }
//   swap.begin(canvas, region);
//   draw(b);                                                  // B slides in behind it
//   swap.end(canvas);
//
// With reduced motion every kind is a plain fade.

#pragma once

#include "ui/components/component.hpp"

namespace hui::ui
{

enum class TransitionKind : std::uint8_t
{
    fade,  // opacity only
    slide, // fades while it travels style.distance in a direction
    scale, // fades while it grows from style.scale to full size
    push,  // one content replaces another along a shared axis
};

// Which of the two contents of a push is being drawn. Entrances and exits
// have one content: the incoming one.
enum class TransitionPhase : std::uint8_t
{
    incoming,
    outgoing,
};

struct TransitionStyle : ComponentStyle
{
    float duration = 0.0f;    // seconds; 0: from the theme's speed (about 0.3 at omega 16)
    float distance = 40.0f;   // how far a slide or a push travels
    float scale = 0.92f;      // the size a scaled region starts from (and leaves to)
    float exit_speed = 1.4f;  // an exit is this much quicker than an entrance
    bool clip = false;        // begin() clips to the region, so moving content cannot spill
    float clip_bleed = 12.0f; // room past the region inside that clip
};

class Transition
{
  public:
    TransitionStyle style;

    // An entrance, or a push from the old content to the new. `direction` is
    // the way things travel: a slide `up` rises into place from below; a push
    // `left` sends the old content off to the left and brings the new one in
    // from the right. It restarts if one is running.
    void start(TransitionKind kind, Direction direction = Direction::none);
    // An exit: the region leaves the same way, and visible() is false after.
    void leave(TransitionKind kind, Direction direction = Direction::none);
    // Jumps to the end of whatever is running.
    void finish();
    // Shows or hides the region at once.
    void show();
    void hide();

    bool running() const
    {
        return running_;
    }
    bool leaving() const
    {
        return leaving_;
    }
    // False once an exit has finished (and until the next start()): skip the
    // region's drawing and its input.
    bool visible() const
    {
        return running_ || !hidden_;
    }
    // 0..1 through the transition, linear in time.
    float progress() const;

    void update(float dt);

    // The numbers, for drawing that cannot go through begin()/end().
    float opacity(TransitionPhase phase = TransitionPhase::incoming) const;
    float scale(TransitionPhase phase = TransitionPhase::incoming) const;
    float offset_x(TransitionPhase phase = TransitionPhase::incoming) const;
    float offset_y(TransitionPhase phase = TransitionPhase::incoming) const;

    // Pushes the opacity and the transform (and the clip, style.clip) for a
    // region; end() pops them. A scale is about the region's centre.
    void begin(Canvas &canvas, const gfx::Rect &region,
               TransitionPhase phase = TransitionPhase::incoming) const;
    void end(Canvas &canvas) const;

  private:
    float seconds() const;
    float travel(TransitionPhase phase) const;

    TransitionKind kind_ = TransitionKind::fade;
    Direction direction_ = Direction::none;
    bool running_ = false;
    bool leaving_ = false;
    bool hidden_ = false;
    float elapsed_ = 0.0f;
};

} // namespace hui::ui
