// ps5-homebrew-ui - Sound cues: names, sound sets and the loaded sound bank.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "audio/mixer.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace hui::audio
{

// What happened, not which file plays: screens ask for a cue and the sound
// set of the active design decides how it sounds. A recording is found by
// name, "assets/audio/sfx/<set>/<cue>_NN.wav" (NN numbers the variations).
enum class Cue : std::uint8_t
{
    // ---- interface (ui bus) ----
    focus,        // the highlight moved
    select,       // confirm
    back,         // cancel, or leave a screen
    tab,          // switched page, tab or filter
    open,         // entered a screen or drawer
    modal_open,   // a dialog or pause menu appeared
    modal_close,  // ... and went away
    toggle,       // switch flipped
    slider,       // slider or stepper moved one step
    error,        // refused: edge of a list, invalid input
    notify,       // toast, achievement
    launch,       // started something big
    favorite_on,  // starred
    favorite_off, // unstarred
    saved,        // settings stored
    welcome,      // the app, or a design, opened
    resume,       // back into the content
                  // ---- feedback (sfx bus) ----
    tick,         // fine movement: a cursor, a dial detent
    type,         // a character was entered
    place,        // put something down
    mark,         // flagged or noted
    erase,        // removed
    rotate,       // turned
    slide,        // moved along a track
    flip,         // turned over
    pickup,       // grabbed
    drop,         // released
    connect,      // linked two things
    reveal,       // uncovered
    cascade,      // a chain of things resolving
    merge,        // two became one (pitch it up as values grow)
    spawn,        // something new appeared
    invalid,      // that move is not allowed
    undo,         // stepped back
    redo,         // stepped forward
    new_game,     // fresh start
    restart,      // reset
    solve,        // long reveal
    complete,     // success fanfare
    new_record,   // personal best
    explode,      // failure with weight
    game_over,    // the end
    count,
};

constexpr std::size_t kCueCount = static_cast<std::size_t>(Cue::count);

// A family of recordings that belong together. Every design picks one; a cue
// its set does not record falls back to the other sets, then to a tone.
enum class SoundSet : std::uint8_t
{
    paper, // warm, wooden, tactile (ProsperoPuzzles)
    glass, // soft, airy, tuned chimes (ProsperoEden)
    count,
};

constexpr std::size_t kSoundSetCount = static_cast<std::size_t>(SoundSet::count);

const char *cue_name(Cue cue);
bool cue_from_name(std::string_view name, Cue *cue);
Bus cue_bus(Cue cue);
const char *sound_set_name(SoundSet set);
// Synthesized stand-in used when no set records the cue.
Tone placeholder_tone(Cue cue);

// One request to play a cue. pan follows where the thing is on screen
// (-1 left .. 1 right); pitch rises with progress, value or position.
struct CueEvent
{
    Cue cue = Cue::focus;
    float pitch = 1.0f;
    float pan = 0.0f;
    float gain = 1.0f;
    // SoundSet::count plays the active design's own set; a sound board or a
    // preview may name another one.
    SoundSet set = SoundSet::count;
};

// Recorded sounds per set and cue, with round-robin variations and a small
// pitch jitter so repeats never sound mechanical.
class SoundBank
{
  public:
    struct Stats
    {
        int files = 0;
        int rejected = 0;
        std::vector<std::string> errors;
    };

    // Scans root/<set>/ for every sound set ("<cue>_NN.wav").
    Stats load(const std::string &root);
    // Adds one decoded sound (used by load and by tests).
    void add(SoundSet set, Cue cue, std::vector<float> samples, std::size_t frames);

    // Posts the cue to the mixer, preferring the recordings of `set`.
    void play(Mixer &mixer, SoundSet set, const CueEvent &event);

    // Whether `set` itself records the cue (no fallback).
    bool has_recording(Cue cue, SoundSet set) const;

  private:
    struct Sound
    {
        std::vector<float> samples;
        Clip clip;
    };
    struct Variations
    {
        std::vector<std::unique_ptr<Sound>> sounds;
        std::size_t next = 0;
    };

    Variations *find(Cue cue, SoundSet set);

    std::array<std::array<Variations, kCueCount>, kSoundSetCount> sets_;
    std::uint32_t jitter_state_ = 0x2545f491u;
};

} // namespace hui::audio
