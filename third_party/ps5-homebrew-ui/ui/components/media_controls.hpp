// ps5-homebrew-ui - Component: MediaControls, the transport of an audio or video player.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/card.hpp"
#include "ui/components/component.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hui::ui
{

// What the player asked for in the last handle().
enum class MediaCommand : std::uint8_t
{
    none,
    play,     // activated: start playing, then set_playing(true)
    pause,    // activated
    previous, // activated: the previous track or chapter
    next,     // activated
    rewind,   // activated: seek_position() is style.skip_seconds earlier
    forward,  // activated
    seek,     // changed: the scrubber moved to seek_position()
    shuffle,  // changed: read shuffle()
    repeat,   // changed: read repeat()
    volume,   // changed: read volume()
    mute,     // changed: read muted()
};

// The things the focus can be on.
enum class MediaControl : std::uint8_t
{
    shuffle,
    previous,
    rewind,
    play,
    forward,
    next,
    repeat,
    volume,
    scrubber,
};

enum class MediaLayout : std::uint8_t
{
    full,    // artwork, title and artist, the scrubber with both times, the buttons
    compact, // one row of buttons and text under a scrubber line: a video overlay
};

enum class MediaButtons : std::uint8_t
{
    surface, // the theme's buttons: play is the primary one
    ghost,   // icons only
};

enum class MediaRepeat : std::uint8_t
{
    off,
    all,
    one,
};

struct MediaChapter
{
    float time = 0.0f; // seconds from the start
    std::string title;
};

struct MediaControlsStyle : ComponentStyle
{
    MediaLayout layout = MediaLayout::full;
    MediaButtons buttons = MediaButtons::surface;
    // ---- geometry ----
    float padding = 22.0f;       // between the bounds and the content
    float button_size = 56.0f;   // every button but play
    float play_size = 68.0f;     // the play button
    float button_gap = 12.0f;    // between buttons
    float track_height = 8.0f;   // the scrubber's track (framed themes add their border)
    float thumb_size = 24.0f;    // the scrubber's thumb at full focus
    float volume_width = 110.0f; // the level bar beside the volume button
    float art_gap = 24.0f;       // between the artwork and the rest (full)
    // ---- type ----
    float title_size = 28.0f;
    float artist_size = 21.0f;
    float time_size = 21.0f; // both times and the bubble, in tabular digits
    // ---- look ----
    bool panel = true;    // a themed panel behind the controls
    bool show_art = true; // full: the artwork square (the `art` slot draws into it)
    bool show_shuffle = true;
    bool show_repeat = true;
    bool show_skip = true;   // rewind and forward
    bool show_tracks = true; // previous and next
    bool show_volume = true;
    bool remaining = true;     // full: the right-hand time counts down ("-3:12")
    bool chapter_marks = true; // ticks over the track at the chapters
    bool bubble = true;        // the time above the thumb while scrubbing
    // ---- behaviour ----
    float skip_seconds = 10.0f; // rewind and forward
    float seek_step = 5.0f;     // one press of left or right on the scrubber
    float seek_growth = 1.3f;   // each repeat of a held direction seeks this much further
    float seek_max = 60.0f;     // ... up to this many seconds a step
    float volume_step = 0.05f;  // one press while the volume is being set
    float auto_hide = 0.0f;     // seconds without input before it hides; 0 never hides
    bool hide_paused = false;   // hide while paused as well
    // up: from the scrubber; down, left and right: from the button row. Hand
    // the focus on instead of refusing.
    EdgeExits exits;
};

// The controls of a player. It owns what is on screen (the focus, the thumb,
// the toggles, the volume) and nothing of the playback: it reports what the
// player asked for and the screen feeds it the clock.
//
//   ui::MediaControls controls;
//   controls.style.theme = theme;
//   controls.title = "Lantern Pass";
//   controls.artist = "Ninefold";
//   controls.set_duration(234.0f);
//   controls.set_bounds({776, 700, 1048, 236});
//   ...
//   const ui::Event event = controls.handle(input, feedback);
//   if (event != ui::Event::none)
//       switch (controls.command())
//       {
//       case ui::MediaCommand::play:  player.play();  break;
//       case ui::MediaCommand::pause: player.pause(); break;
//       case ui::MediaCommand::seek:
//       case ui::MediaCommand::rewind:
//       case ui::MediaCommand::forward: player.seek(controls.seek_position()); break;
//       default: break;
//       }
//   controls.set_playing(player.playing());
//   controls.set_position(player.position());
//   controls.update(dt);
//   controls.draw(canvas);
class MediaControls
{
  public:
    // box is the artwork square; radius the corner it should have.
    using Art = std::function<void(Canvas &canvas, const gfx::Rect &box, float radius)>;

    MediaControlsStyle style;
    std::string title;
    std::string artist;
    Art art; // draws the artwork; without it a placeholder is drawn

    void set_bounds(const gfx::Rect &bounds);
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    // The height the layout needs with the current style.
    float preferred_height() const;
    void set_active(bool active)
    {
        active_ = active;
    }

    // ---- fed by the screen ----
    void set_duration(float seconds);
    // snap skips the thumb's glide (a new track).
    void set_position(float seconds, bool snap = false);
    void set_buffered(float seconds);
    void set_playing(bool playing)
    {
        playing_ = playing;
    }
    void set_chapters(std::vector<MediaChapter> chapters);
    void set_shuffle(bool on)
    {
        shuffle_ = on;
    }
    void set_repeat(MediaRepeat mode)
    {
        repeat_ = mode;
    }
    void set_volume(float level, bool muted = false);

    float duration() const
    {
        return duration_;
    }
    float position() const
    {
        return position_;
    }
    bool playing() const
    {
        return playing_;
    }
    bool shuffle() const
    {
        return shuffle_;
    }
    MediaRepeat repeat() const
    {
        return repeat_;
    }
    float volume() const
    {
        return volume_;
    }
    bool muted() const
    {
        return muted_;
    }
    // The chapter a time falls in, or -1.
    int chapter_at(float seconds) const;

    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

    // What the last handle() asked for; none when it returned none, moved,
    // refused or cancelled.
    MediaCommand command() const
    {
        return command_;
    }
    // Where seek, rewind and forward want the playback to be, in seconds. The
    // component has already moved its own thumb there.
    float seek_position() const
    {
        return seek_position_;
    }
    // For a screen that binds mute to a button of its own.
    Event toggle_mute(Feedback &feedback);

    MediaControl focus() const
    {
        return focus_;
    }
    // Without sound. A control the style hides is replaced by play.
    void set_focus(MediaControl control);
    // True while confirm on the volume button has captured left and right.
    bool adjusting() const
    {
        return adjusting_;
    }

    // ---- auto-hide ----
    // 0 hidden .. 1 shown, eased. Dim your own chrome with it.
    float visibility() const
    {
        return tween::clamp01(visible_.value);
    }
    bool hidden() const;
    // Shows the controls again and restarts the countdown.
    void wake()
    {
        idle_ = 0.0f;
    }

    // The edge the focus asked to leave through in the last handle() (see
    // MediaControlsStyle::exits), or Direction::none. handle() returned none.
    Direction exit() const
    {
        return exit_;
    }
    // Where a control is on screen (the scrubber: its thumb).
    gfx::Rect control_rect(MediaControl control) const;

  private:
    static constexpr int kButtons = 8; // every MediaControl but the scrubber
    struct Geometry
    {
        gfx::Rect inner;
        gfx::Rect art;  // w == 0: none
        gfx::Rect text; // title and artist
        gfx::Rect track;
        gfx::Rect volume_track;
        gfx::Rect button[kButtons]; // w == 0: not shown
        float time_left = 0.0f;     // the elapsed time starts here
        float time_right = 0.0f;    // the other one ends here (full)
        float time_baseline = 0.0f;
        float row_cy = 0.0f;
    };
    Geometry geometry() const;
    bool shown(MediaControl control) const;
    int stops(MediaControl *out) const;
    float track_thickness() const;
    gfx::Rect thumb_rect(const Geometry &g) const;
    gfx::Rect focus_rect(const Geometry &g) const;
    bool hours() const
    {
        return duration_ >= 3600.0f;
    }
    Event seek(int direction, const InputFrame &input, Feedback &feedback);
    Event activate(MediaControl control, const InputFrame &input, Feedback &feedback);
    Event adjust_volume(int direction, const InputFrame &input, Feedback &feedback);
    void draw_button(Canvas &canvas, Painter &paint, MediaControl control,
                     const gfx::Rect &r) const;
    void draw_scrubber(Canvas &canvas, Painter &paint, const Geometry &g) const;
    gfx::Rect bubble_plate(const Painter &paint, const Geometry &g) const;
    void draw_bubble(Canvas &canvas, Painter &paint, const Geometry &g) const;
    void draw_art(Canvas &canvas, Painter &paint, const gfx::Rect &box) const;

    gfx::Rect bounds_{0.0f, 0.0f, 960.0f, 236.0f};
    bool active_ = true;
    MediaControl focus_ = MediaControl::play;
    MediaControl row_focus_ = MediaControl::play; // where down from the scrubber returns to
    MediaControl pressed_ = MediaControl::play;
    MediaCommand command_ = MediaCommand::none;
    Direction exit_ = Direction::none;
    bool adjusting_ = false;

    float duration_ = 0.0f;
    float position_ = 0.0f;
    float buffered_ = 0.0f;
    float seek_position_ = 0.0f;
    bool playing_ = false;
    bool shuffle_ = false;
    MediaRepeat repeat_ = MediaRepeat::off;
    float volume_ = 0.7f;
    bool muted_ = false;
    std::vector<MediaChapter> chapters_;

    int streak_ = 0;      // repeats of a held seek, for the acceleration
    float linger_ = 0.0f; // seconds the bubble stays after a seek
    float idle_ = 0.0f;   // seconds since the last input, for auto-hide
    Highlight ring_;
    bool ring_set_ = false;
    tween::Spring shown_;       // the thumb, 0..1, chasing the clock
    tween::Spring morph_;       // 0 play icon .. 1 pause icon
    tween::Spring scrub_focus_; // the track thickens, the thumb grows
    tween::Spring bubble_;
    tween::Spring level_{0.7f, 0.0f, 0.7f}; // the volume bar
    tween::Spring adjust_;                  // the volume bar's thumb appears
    tween::Spring shuffle_on_;
    tween::Spring repeat_on_;
    tween::Spring visible_{1.0f, 0.0f, 1.0f};
    tween::Spring active_amount_{1.0f, 0.0f, 1.0f};
    Pulse press_;
};

} // namespace hui::ui
