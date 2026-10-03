// ps5-homebrew-ui - Component: SearchField, a search box with a suggestions list.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/card.hpp"
#include "ui/components/component.hpp"
#include "ui/components/progress.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hui::ui
{

struct Suggestion
{
    std::string text;
    std::string detail; // right-aligned, quiet ("Racing", "12 results")
    int tag = 0;        // yours
};

struct SearchFieldStyle : ComponentStyle
{
    // ---- geometry ----
    float field_height = 60.0f;
    float row_height = 52.0f;    // one suggestion
    float gap = 10.0f;           // between the field and the list
    float list_padding = 8.0f;   // inside the list's panel
    float header_height = 34.0f; // the "Recent" line
    int max_rows = 5;            // suggestions shown at most
    // ---- type ----
    float text_size = 24.0f;
    float row_size = 23.0f;
    float detail_size = 19.0f;
    float header_size = 16.0f;
    // ---- look ----
    HighlightStyle highlight;    // of the focused suggestion
    bool list_panel = true;      // the list floats on a panel; false: rows on the page
    bool match_underline = true; // a line under the part of a suggestion that matches
    bool clear_button = true;    // an "x" at the end of the field while it has text
    bool row_icons = true;       // a magnifier or a clock before each row
    bool keep_open = false;      // the list stays while the field is not active
    bool pill = false;           // the field is a pill whatever the theme's radius
    bool focus_ring = true;      // false: only the caret says the field is active
    bool on_page = false;        // text without a panel under it sits on the page
    std::string recent_title = "Recent";
    std::string empty_text = "No matches";
    // ---- behaviour ----
    int max_length = 0;     // characters; 0 for no limit
    float debounce = 0.25f; // seconds after the last edit before the provider is asked
    bool remember = true;   // a picked suggestion joins the recent searches
    int max_recent = 5;
    bool fill_on_pick = true;             // a picked suggestion becomes the text
    EdgeExits exits;                      // edges that hand the focus back
    float caret_period = 1.0f;            // seconds per blink; 0 keeps the caret lit
    audio::Cue type = audio::Cue::type;   // insert(c, feedback)
    audio::Cue erase = audio::Cue::erase; // backspace(feedback) and the clear button
};

// A search box: a magnifier, the query, a spinner while it "searches", a
// clear button, and under it a list of suggestions with the matching part of
// each one emphasised. With an empty query the list shows recent searches.
// Like TextField it has no keyboard of its own: confirm on the field returns
// activated so the screen can open one and feed insert() and backspace().
//
//   ui::SearchField search;
//   search.style.theme = theme;
//   search.set_placeholder("Search the library");
//   search.set_recent({"racing", "tide"});
//   search.provider = [&](std::string_view query, std::vector<ui::Suggestion> &out) {
//       for (const Game &game : games)
//           if (matches(game.title, query)) out.push_back({game.title, game.genre});
//   };
//   search.set_bounds({1100, 290, 440, search.preferred_height()});
//   ...
//   search.set_active(focused);
//   if (search.handle(input, feedback) == ui::Event::activated)
//   {
//       if (search.has_pick()) open(search.picked());   // a suggestion was chosen
//       else open_keyboard();                            // confirm on the field
//   }
//   search.update(dt);
//   search.draw(canvas);
class SearchField
{
  public:
    using Provider = std::function<void(std::string_view query, std::vector<Suggestion> &out)>;

    // focus() values that are not a suggestion row.
    static constexpr int kFocusField = -1;
    static constexpr int kFocusClear = -2;

    SearchFieldStyle style;
    Provider provider; // fills the suggestions for a query

    SearchField()
    {
        // Nothing is being searched for yet.
        spinner_.set_spinning(false, true);
    }

    void set_placeholder(std::string placeholder)
    {
        placeholder_ = std::move(placeholder);
    }
    // Shown while the query is empty, newest first.
    void set_recent(std::vector<std::string> recent);
    const std::vector<std::string> &recent() const
    {
        return recent_;
    }
    void add_recent(std::string_view query);

    // ---- the value ----
    void set_text(std::string_view text);
    const std::string &text() const
    {
        return text_;
    }
    int length() const;
    bool full() const
    {
        return style.max_length > 0 && length() >= style.max_length;
    }
    // Silent editing, for code and for a Keyboard (which plays its own cues).
    // False when nothing changed.
    bool insert(char c);
    bool insert(std::string_view utf8);
    bool backspace();
    void clear();
    // Editing with the field's own voice, like TextField.
    Event insert(char c, Feedback &feedback);
    Event backspace(Feedback &feedback);

    // ---- searching ----
    // True from an edit until the provider answered, and while set_busy(true).
    bool searching() const
    {
        return pending_ > 0.0f || busy_;
    }
    // For a search that runs elsewhere: keeps the spinner turning.
    void set_busy(bool busy)
    {
        busy_ = busy;
    }
    // Asks the provider now, without the debounce.
    void refresh();
    const std::vector<Suggestion> &suggestions() const
    {
        return rows_;
    }
    // True while the rows are recent searches rather than answers.
    bool showing_recent() const
    {
        return showing_recent_;
    }

    // ---- focus ----
    // kFocusField, kFocusClear, or the index of a suggestion.
    int focus() const
    {
        return focus_;
    }
    bool in_list() const
    {
        return focus_ >= 0;
    }
    // After Event::activated: whether a suggestion was chosen, and which.
    bool has_pick() const
    {
        return has_pick_;
    }
    const Suggestion &picked() const
    {
        return picked_;
    }
    // The edge the focus left through when handle() returned none on a
    // direction (style.exits).
    Direction exit() const
    {
        return exit_;
    }

    // ---- the five rules ----
    // The bounds give the position and the width; the field is at the top
    // and the list drops below it, as tall as its rows.
    void set_bounds(const gfx::Rect &bounds);
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    // The field and a list of max_rows suggestions with its header.
    float preferred_height() const;
    gfx::Rect field_rect() const;
    gfx::Rect list_rect() const; // at its full height for the rows it has
    void set_active(bool active);

    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    int shown_rows() const;
    bool has_header() const;
    bool has_clear() const;
    float list_height() const;
    gfx::Rect row_rect(int index) const;
    void edited();
    void ask();
    void retarget(bool snap);
    Event leave(const InputFrame &input, Feedback &feedback);
    std::string shown(const Painter &paint, float room) const;
    void draw_field(Canvas &canvas, Painter &paint) const;
    void draw_list(Canvas &canvas, Painter &paint) const;

    std::string text_;
    std::string placeholder_;
    std::vector<std::string> recent_;
    std::vector<Suggestion> rows_;
    Suggestion picked_;
    bool has_pick_ = false;
    bool showing_recent_ = true;
    bool asked_ = false; // the provider answered the current query
    gfx::Rect bounds_{0.0f, 0.0f, 440.0f, 360.0f};
    int focus_ = kFocusField;
    int last_row_ = 0; // where the highlight rests while the focus is on the field
    bool active_ = false;
    bool busy_ = false;
    float pending_ = 0.0f;
    Direction exit_ = Direction::none;
    Spinner spinner_;
    // Animation.
    float blink_ = 0.0f;
    float results_age_ = 10.0f;
    tween::Spring field_focus_;
    tween::Spring clear_focus_;
    tween::Spring clear_shown_;
    tween::Spring list_focus_;
    tween::Spring open_;
    tween::Spring height_;
    Highlight highlight_;
    Pulse shake_;
    Pulse press_;
};

} // namespace hui::ui
