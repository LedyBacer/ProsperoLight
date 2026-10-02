// ps5-homebrew-ui - Component: Keyboard, an on-screen keyboard for a controller.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/card.hpp"
#include "ui/components/component.hpp"
#include "ui/components/progress.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace hui::ui
{

enum class KeyKind : std::uint8_t
{
    character, // types `text` (or `shifted`)
    space,
    shift,
    backspace,
    layout, // switches to the next layout
    done,
};

struct KeyboardKey
{
    KeyKind kind = KeyKind::character;
    std::string text;    // what it types, UTF-8; may be several characters (".com")
    std::string shifted; // what it types while shift is on; empty: the same
    int span = 1;        // columns it covers
    std::string label;   // shown instead of the text or the default word
};

// A layout is data: rows of keys on a grid of `columns`. A row that covers
// fewer columns than the grid is centred.
struct KeyboardLayout
{
    std::string name; // shown on the key that switches to it ("abc", "#+=")
    int columns = 10;
    std::vector<std::vector<KeyboardKey>> rows;

    // One key per ASCII character; letters get their capital as `shifted`.
    KeyboardLayout &add_row(std::string_view characters);
    KeyboardLayout &add_row(std::vector<KeyboardKey> keys);

    static KeyboardLayout letters(); // digits, qwerty, punctuation; shift, symbols, space
    static KeyboardLayout symbols(); // the companion of letters()
    static KeyboardLayout numeric(); // a 3 x 4 pad: digits, backspace, done
    static KeyboardLayout email();   // letters with @ . _ - and a ".com" key
};

enum class KeyboardShift : std::uint8_t
{
    off,
    once, // the next letter only
    lock, // until it is pressed again
};

// The buttons that act without moving the focus. Action::count means "none".
// Nothing is bound by default, so the keyboard never takes a button a screen
// uses for something else; standard() is the mapping players know.
struct KeyboardBindings
{
    Action backspace = Action::count; // repeats while held
    Action space = Action::count;
    Action shift = Action::count;
    Action layout = Action::count;
    Action done = Action::count;

    // Square deletes, Triangle is space, L2 is shift, R2 switches to symbols.
    static KeyboardBindings standard()
    {
        KeyboardBindings bindings;
        bindings.backspace = Action::west;
        bindings.space = Action::north;
        bindings.shift = Action::jump_prev;
        bindings.layout = Action::jump_next;
        return bindings;
    }
};

struct KeyboardStyle : ComponentStyle
{
    KeyboardStyle()
    {
        // A filled plate is what players know from console keyboards.
        highlight.kind = HighlightKind::fill;
    }

    // ---- geometry ----
    float key_width = 0.0f;  // one column; 0: the columns share the bounds' width
    float key_height = 0.0f; // one row; 0: the rows share the bounds' height
    float gap = 8.0f;        // between keys
    RadiusSource radius_source = RadiusSource::theme;
    float radius = 10.0f;        // RadiusSource::custom
    float panel_padding = 16.0f; // between the panel and the keys (panel = true)
    float icon_size = 26.0f;     // the symbols of shift, backspace, space and done
    float glyph_size = 30.0f;    // the controller glyph of a bound key
    // ---- type ----
    float label_size = 28.0f;      // characters
    float wide_label_size = 21.0f; // the words on wide keys
    // ---- look ----
    HighlightStyle highlight;   // fill by default
    bool surfaces = true;       // every key is a themed surface; false: flat caps
    bool panel = true;          // a themed panel behind the keys
    bool binding_glyphs = true; // a bound key shows its controller button
    bool wide_labels = true;    // wide keys show a word beside their symbol when it fits
    std::string space_label = "Space";
    std::string done_label = "Done";
    // ---- behaviour ----
    bool wrap = true;          // left / right leave through one edge and enter by the other
    bool column_memory = true; // up and down return to the column the player left
    EdgeExits exits;           // edges that hand the focus back; an exit beats the wrap
    bool shift_lock = true;    // a second press of shift locks it
    bool auto_capital = false; // arm shift while the text is empty (needs set_length)
    int max_length = 0;        // refuse characters from here on; 0: no limit (needs set_length)
    KeyboardBindings bindings;
    float repeat_delay = 0.42f;    // a held backspace waits this long ...
    float repeat_interval = 0.09f; // ... then deletes once per interval
    // ---- feel ----
    float press_dip = 0.06f;              // a pressed key shrinks by this share
    float entrance_step = 0.04f;          // seconds between rows arriving; 0 for none
    bool pitch_by_column = true;          // keys to the right sound a little higher
    audio::Cue type = audio::Cue::type;   // a character went out
    audio::Cue erase = audio::Cue::erase; // a backspace went out
};

// An on-screen keyboard: a grid of keys under one gliding highlight. It does
// not own the text. Every character goes out through on_text (and typed()),
// every deletion through on_backspace (and erased()), so it can feed a
// TextField, a SearchField, a PinEntry or your own buffer.
//
//   ui::Keyboard keys;
//   keys.style.theme = theme;
//   keys.style.bindings = ui::KeyboardBindings::standard();
//   keys.style.max_length = 12;
//   keys.set_bounds({560, 520, 800, 360});
//   keys.on_text = [&](std::string_view utf8) { name.insert(utf8); };
//   keys.on_backspace = [&] { name.backspace(); };
//   ...
//   keys.set_length(name.length());            // lets it refuse at the limit
//   const ui::Event event = keys.handle(input, feedback);
//   if (event == ui::Event::activated) accept(name.text());   // the Done key
//   if (event == ui::Event::cancelled) close();
//   keys.update(dt);
//   keys.draw(canvas);
class Keyboard
{
  public:
    KeyboardStyle style;
    std::function<void(std::string_view utf8)> on_text;
    std::function<void()> on_backspace;
    std::function<void()> on_done;

    // Starts with letters() and symbols().
    Keyboard();

    // ---- layouts ----
    void set_layouts(std::vector<KeyboardLayout> layouts);
    const std::vector<KeyboardLayout> &layouts() const
    {
        return layouts_;
    }
    int layout() const
    {
        return layout_;
    }
    // Silent; the focus stays as near as the new layout allows.
    void set_layout(int index);
    KeyboardShift shift() const
    {
        return shift_;
    }
    void set_shift(KeyboardShift shift)
    {
        shift_ = shift;
    }

    // How long the text it feeds is, in characters: lets the keyboard refuse
    // at max_length and on an empty text, and arm shift for a first capital.
    // It keeps count of what it emits; call this when the text changed some
    // other way. A negative length means "unknown": nothing is refused.
    void set_length(int length);
    int length() const
    {
        return length_;
    }

    // ---- the five rules ----
    void set_bounds(const gfx::Rect &bounds);
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    // The size the current layout needs with key_width and key_height (72 by
    // 64 where the style leaves them at 0).
    float preferred_width() const;
    float preferred_height() const;
    // An inactive keyboard keeps a faint highlight.
    void set_active(bool active)
    {
        active_ = active;
    }
    // Replays the entrance animation.
    void enter();

    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

    // ---- focus ----
    int focus_row() const
    {
        return row_;
    }
    int focus_key() const
    {
        return key_;
    }
    const KeyboardKey &focused() const;
    void set_focus(int row, int key, bool snap = true);
    // The edge the focus left through when handle() returned none on a
    // direction (style.exits).
    Direction exit() const
    {
        return exit_;
    }
    gfx::Rect key_rect(int row, int key) const;

    // ---- what a shortcut does, for a screen that maps its own buttons ----
    Event backspace(Feedback &feedback);
    Event space(Feedback &feedback);
    Event done(Feedback &feedback);
    void toggle_shift(Feedback &feedback);
    void cycle_layout(Feedback &feedback);
    // Presses the key that types `utf8` as if the player had: the highlight
    // glides there and the key dips. Shift and the layout follow as needed.
    // "\b" is backspace and "\n" is done. For tutorials, demos and tests.
    Event tap(std::string_view utf8, Feedback &feedback);

    // What the last handle() or tap() sent out, for owners that poll instead
    // of setting the callbacks. Apply the deletions first.
    const std::string &typed() const
    {
        return typed_;
    }
    int erased() const
    {
        return erased_;
    }

  private:
    struct Metrics
    {
        gfx::Rect board; // the keys' own rectangle
        float unit_w = 0.0f;
        float unit_h = 0.0f;
    };

    const KeyboardLayout &current() const;
    bool empty() const;
    const KeyboardKey &key_at(int row, int key) const;
    Metrics metrics() const;
    float row_start(int row) const; // in columns
    float key_start(int row, int key) const;
    float key_centre(int row, int key) const;
    int key_under(int row, float column) const;
    bool find_kind(KeyKind kind, int *row, int *key) const;
    float key_radius(const Painter &paint, const gfx::Rect &r) const;
    bool shifted() const
    {
        return shift_ != KeyboardShift::off;
    }
    void relocate();
    void retarget(bool snap);
    void press(int row, int key);
    Event move(const InputFrame &input, Feedback &feedback);
    Event activate(int row, int key, Feedback &feedback);
    Event emit(std::string_view utf8, int row, int key, bool capital, Feedback &feedback);
    Event erase(bool repeat, Feedback &feedback);
    void draw_cap(Canvas &canvas, Painter &paint, const KeyboardKey &key, const gfx::Rect &rect,
                  gfx::Color ink) const;

    std::vector<KeyboardLayout> layouts_;
    int layout_ = 0;
    gfx::Rect bounds_{0.0f, 0.0f, 800.0f, 360.0f};
    int row_ = 0;
    int key_ = 0;
    float column_ = 0.5f; // where the player really is, in columns
    KeyboardShift shift_ = KeyboardShift::off;
    int length_ = -1;
    bool armed_for_empty_ = false;
    bool active_ = true;
    Direction exit_ = Direction::none;
    std::string typed_;
    int erased_ = 0;
    // A held backspace: handle() notes it, update() counts the time.
    bool holding_ = false;
    float hold_ = 0.0f;
    // Animation.
    float age_ = 10.0f;
    float layout_age_ = 10.0f;
    Highlight highlight_;
    Highlight ghost_; // the highlight leaving through an edge while it wraps
    Pulse wrap_;
    Pulse press_;
    int press_row_ = -1;
    int press_key_ = -1;
    tween::Spring shift_blend_;
    tween::Spring lock_blend_;
    tween::Spring active_amount_{1.0f, 0.0f, 1.0f};
};

} // namespace hui::ui
