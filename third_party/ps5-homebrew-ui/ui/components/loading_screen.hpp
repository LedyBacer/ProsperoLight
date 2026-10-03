// ps5-homebrew-ui - Component: LoadingScreen, the full-screen loader with stages and tips.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/component.hpp"
#include "ui/components/progress.hpp"
#include "ui/glyphs.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hui::ui
{

enum class LoadingLayout : std::uint8_t
{
    corner, // title at the top left, tips and a full-width bar along the bottom
    center, // one centred column: title, bar, tip
};

enum class LoadingIndicator : std::uint8_t
{
    both,    // a bar, and a spinner beside the stage's name
    bar,     // the bar alone
    spinner, // the spinner alone: for loads that cannot be measured
    none,
};

// A named part of a load. Its weight is its share of the bar.
struct LoadingStage
{
    std::string name;
    float weight = 1.0f;
};

struct LoadingScreenStyle : ComponentStyle
{
    LoadingLayout layout = LoadingLayout::corner;
    LoadingIndicator indicator = LoadingIndicator::both;
    SpinnerKind spinner = SpinnerKind::arc;
    // ---- geometry ----
    float margin = 96.0f;     // from the left and right edges
    float bottom = 96.0f;     // from the lower edge to the bar (corner) or the prompt (center)
    float bar_height = 10.0f; // framed themes add their border
    float bar_width = 720.0f; // center: the bar's width; corner fills the screen's width
    float spinner_size = 30.0f;
    float tip_width = 820.0f; // the tip wraps to this
    int tip_lines = 3;        // ... and to this many lines
    // ---- type ----
    float title_size = 64.0f;
    float subtitle_size = 26.0f;
    float stage_size = 22.0f; // the stage's name and the percentage
    float tip_size = 26.0f;
    float prompt_size = 26.0f;
    float glyph_size = 40.0f; // the controller glyph of the prompt
    // ---- look ----
    gfx::Color tint{0.0f, 0.0f, 0.0f, 0.0f}; // what covers the screen; alpha 0: the theme's page
    float veil = 0.55f;                      // how much of the tint lies over the artwork
    bool ambience = true; // without artwork: soft pools of the theme's colours, where
                          // the theme's material is soft enough to carry them
    bool percent = true;  // "64%"
    bool sheen = true;    // a band of light crossing the bar's fill
    bool tip_dots = true; // one dot per tip
    std::string tip_label = "Tip";
    std::string ready_text = "Ready";     // replaces the stage's name when the load is done
    std::string prompt = "Continue";      // beside the glyph
    Button prompt_button = Button::cross; // Button::circle when confirm is swapped
    // ---- behaviour ----
    float tip_seconds = 5.0f; // a tip stays this long; 0 never rotates
    bool tips_by_hand = true; // left and right step through the tips
    float fade_in = 0.35f;    // seconds
    float fade_out = 0.3f;
    bool close_on_continue = true; // confirm, once ready, hides the screen
};

// The screen between two screens: what is loading, how far it is, something
// to read meanwhile, and a prompt when it is done. It is honest: the bar shows
// the progress it is given, and only set_ready(true) brings the prompt.
//
//   ui::LoadingScreen loader;
//   loader.style.theme = theme;
//   loader.title = "Lantern Pass";
//   loader.set_stages({{"Reading save", 1}, {"Compiling shaders", 3}, {"Building world", 2}});
//   loader.set_tips({"Hold Square to sprint.", "Lanterns mark safe paths."});
//   loader.show(feedback);
//   ...
//   loader.set_progress(job.progress());      // 0..1; negative: unknown
//   if (job.done()) loader.set_ready(true);
//   if (loader.is_open() && loader.handle(input, feedback) == ui::Event::activated)
//       start_game();
//   loader.update(dt);
//   loader.draw(canvas);     // last: it covers the screen (a page's draw_modal)
class LoadingScreen
{
  public:
    // screen is the whole area; progress is the eased value of the bar.
    using Art = std::function<void(Canvas &canvas, const gfx::Rect &screen, float progress)>;

    LoadingScreenStyle style;
    std::string title;
    std::string subtitle;
    Art art; // draws the artwork behind everything; without it the tint covers the screen

    // The area it covers: the whole canvas by default.
    void set_bounds(const gfx::Rect &bounds)
    {
        bounds_ = bounds;
    }
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    void set_stages(std::vector<LoadingStage> stages);
    void set_tips(std::vector<std::string> tips);

    // 0..1 of the whole load. A negative value means "unknown": the bar
    // travels instead of filling and the percentage is hidden.
    void set_progress(float value);
    // The same from a stage and how far that stage is (0..1).
    void set_stage(int index, float share = 0.0f);
    float progress() const
    {
        return progress_;
    }
    // The stage the progress is in, by the stages' weights; -1 without stages.
    int stage() const;
    // The load is done: the bar fills, the spinner stops, the prompt appears.
    // Its chime plays at the next handle(), which has the Feedback.
    void set_ready(bool ready);
    bool ready() const
    {
        return ready_;
    }

    // show() restarts the entrance and clears ready and progress.
    void show(Feedback &feedback);
    void hide();
    bool is_open() const
    {
        return open_;
    }
    // True while it is still fading out.
    bool visible() const
    {
        return open_ || fade_ > 0.001f;
    }
    float opacity() const;

    int tip() const
    {
        return tip_;
    }
    // Without sound; restarts the tip's timer.
    void step_tip(int direction);

    // While open it takes every input: confirm continues once ready (and
    // refuses before), left and right step the tips, back is reported.
    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

  private:
    struct TipCache
    {
        const Fonts *fonts = nullptr;
        const char *theme = nullptr;
        int index = -1;
        float width = 0.0f;
        float size = 0.0f;
        int lines_max = 0;
        std::vector<std::string> lines;
    };
    const std::vector<std::string> &tip_lines(const Canvas &canvas, const Painter &paint, int index,
                                              int slot) const;
    void sync_parts();
    float entrance(int order) const;
    void draw_tip(Canvas &canvas, Painter &paint, int index, int slot, float x, float first,
                  float alpha, float dx, gfx::Align align, gfx::Color ink) const;
    void draw_stage(Canvas &canvas, Painter &paint, float x, float baseline, gfx::Align align,
                    gfx::Color ink, gfx::Color quiet) const;
    float draw_prompt(Canvas &canvas, Painter &paint, float x, float cy, gfx::Align align,
                      gfx::Color ink, bool dark) const;

    gfx::Rect bounds_{0.0f, 0.0f, gfx::kVirtualWidth, gfx::kVirtualHeight};
    std::vector<LoadingStage> stages_;
    std::vector<std::string> tips_;
    bool open_ = false;
    bool ready_ = false;
    bool announced_ = false; // the ready chime has played
    float fade_ = 0.0f;      // 0..1, linear; eased where it is drawn
    float age_ = 0.0f;       // seconds since show()
    float progress_ = 0.0f;
    int shown_stage_ = -1;
    int previous_stage_ = -1;
    tween::Timer stage_swap_;
    int tip_ = 0;
    int previous_tip_ = 0;
    float tip_direction_ = 1.0f;
    float tip_clock_ = 0.0f;
    tween::Timer tip_fade_;
    tween::Spring ready_amount_;
    Pulse refusal_;
    ProgressBar bar_;
    Spinner spinner_;
    mutable TipCache cache_[2]; // the tip on screen and the one leaving
};

} // namespace hui::ui
